# Raise `ReturnStack::CAPACITY` and make its tests capacity-agnostic — issue #34

Date: 2026-09-19 · Branch: `feature/34-return-stack-capacity` · Surface: `ui`
Research: `docs/superpowers/research/2026-09-19-issue-34-research.md`

**Modelled on** `docs/superpowers/specs/2026-09-17-issue-40-design.md` — the same
shape of change (a bounded collection's constant, plus host tests that drive the
bound through the constant rather than through literals), landed as `2df75e5b`
(#40/#68). Its `test/recent_books_doc/RecentBooksDocTest.cpp` is the file the
rewritten `ReturnStackTest.cpp` mirrors.

---

## Problem

`src/activities/reader/ReturnStack.h:15` fixes the return ring at `CAPACITY = 3`.
`push` (`:19-23`) overwrites the oldest slot and clamps `count_`, so a fourth
push silently discards the first. `unpush`'s comment (`:33-34`) states the
consequence plainly: it "cannot restore an entry the push evicted."

Issue #34 frames this as a problem *ahead of* Phase 2, when Back becomes primary.
**Reading the input layer says the premise is already true on this board**, and
this is the finding that decides the whole spec.

### The left-edge swipe is already this device's only Return

Traced in this tree, in order:

1. `MappedInputManager::wasReleased(Button::Back)` returns **true** for a
   left-edge swipe, before it ever consults a GPIO —
   `src/MappedInputManager.cpp:308-309` (`if (button == Button::Back && wasBackGesture()) return true;`),
   with `wasBackGesture()` = a left-edge swipe at `:266-271`.
2. The reader's return branch gates on exactly that and **does not** exclude the
   gesture — `src/activities/reader/EpubReaderActivity.cpp:547-551`:

   ```cpp
   if (returnStack.count() > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
       mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
     restoreSavedPosition();
     return;
   }
   ```

3. The *exit* path immediately below it **does** exclude the gesture —
   `ReaderUtils::handleBackNavigation` returns false on `wasBackGesture()`
   before anything else (`src/activities/reader/ReaderUtils.h:255-257`), with the
   comment explaining why: "The reading surface deliberately has no left-edge
   swipe-to-exit path."
4. This board has **no physical Back button** (`CLAUDE.md`, "Input hardware, as
   confirmed on the bench"; GPIO0/GPIO7/GPIO3 are Left/Right/Power, and Home is a
   GT911 capacitive bit).
5. The only other return path, the short power press at
   `EpubReaderActivity.cpp:557-561`, is gated on
   `SETTINGS.shortPwrBtn == SHORT_PWRBTN::FOOTNOTES`, and the default is
   `IGNORE` — `src/CrossPointSettings.h:276` (`uint8_t shortPwrBtn = IGNORE;`),
   `IGNORE = 0` at `:178`.

So on a default X4 Pro the left-edge swipe is **the only Return that exists, and
it is nothing else** — when the ring is empty it is inert on the reading surface,
because step 3 refuses it and step 2's guard fails.

**Therefore eviction already strands the user today.** Walk a four-deep
cross-reference chain and the fourth swipe does nothing at all: it is not a Back,
it is not an exit, and the article origin is unreachable by any gesture. Phase 2
does not create this failure; it only widens it.

*This extends the research note, which characterised the empty-ring case as
falling through to `handleBackNavigation`. It does not, for a gesture. The
research note's call-site table and control-flow section are otherwise unchanged.*

### How the depth actually grows

Pushes come from exactly one place — `navigateToHref(href, savePosition = true)`
(`EpubReaderActivity.cpp:1706-1707`), with three callers, all footnote follows
(`:564`, `:571`, `:766`). `navigateTo`'s default policy is `ReturnPolicy::Clear`
(`EpubReaderActivity.h:147`), so every deliberate jump — sync (`:362`, `:690`),
percent (`:678`), TOC (`:712`), chapter pick (`:752`) — empties the ring.

The issue's justification ("a paragraph with four cited scriptures is ordinary")
does not survive that. Four *sibling* citations means follow → Return → follow →
Return, and each Return pops; depth never exceeds 1. Depth grows only on
**nested** follows with no intervening Return — a chain-reference walk. Ordinary
page turns do not touch `navigateTo` at all (`:915`, `:932`), so the ring
persists while reading onward, which is what lets a chain accumulate.

The change is still right. The argument for it is the chain, not the fan.

### A second live consequence

`~EpubReaderActivity` persists reading progress to `returnStack.oldest()` —
`EpubReaderActivity.cpp:160-162`:

```cpp
if (const SavedPosition* origin = returnStack.oldest(); origin && epub) {
  saveProgress(origin->spineIndex, origin->pageNumber, 0);
}
```

Once the ring has wrapped, `oldest()` is no longer the chain origin (pinned by
`test/return_stack/ReturnStackTest.cpp:72-84`). Closing the book from four deep
therefore persists the **second**-oldest position. Raising the capacity makes the
saved origin more faithful, for free, with no code change at that site.

---

## Goal

1. Move the eviction boundary well past any chain a study session reaches, so the
   only Return gesture this device has stops going inert on ordinary use.
2. Leave `ReturnStack`'s semantics — LIFO, wrap, `oldest()`, `clear()`,
   `unpush()` — bit-for-bit unchanged.
3. Make `test/return_stack/ReturnStackTest.cpp` derive every wrap-boundary
   assertion from `ReturnStack::CAPACITY`, so this constant is never again
   priced at "breaking four assertions."
4. Pin the memory footprint so a future `SavedPosition` growth trips the build
   rather than silently costing internal SRAM.

---

## Non-goals

- **No UI.** No return indicator, no eviction toast, no "Back vs Return"
  affordance. That is the design doc's *other* branch
  (`docs/superpowers/specs/2026-09-13-berean-os-design.md:217-219`) and it is
  Phase 2 chrome — see Assumption **A2**.
- **No sticky-oldest.** See **A3**.
- **No input-layer change.** `MappedInputManager` is untouched, per
  `.claude/agents/ui-dev.md` and `CLAUDE.md`. Nothing in this spec reads, writes
  or bypasses it.
- **No persistence and no format version.** The ring is a by-value member of a
  heap-allocated activity (`EpubReaderActivity.h:78`;
  `src/activities/reader/ReaderActivity.cpp:29` allocates via
  `makeUniqueNoThrow`) and dies with it. Nothing serialises a `ReturnStack`;
  grep for `ReturnStack|SavedPosition` across `src/`, `lib/` and `freeink-sdk/`
  returns only the sites in the research note's table. `CLAUDE.md`'s storage
  discipline and format-versioning rules therefore do not engage. See **A6**.
- **No change to `SavedPosition`'s `(spineIndex, pageNumber)` shape**, which
  `2026-09-13-berean-os-design.md:511-513` says needs Unit addressing before it
  is persisted anywhere. Out of scope here; **A5** keeps room for it.
- **No new caller for `unpush()`**, which has none outside the tests.

---

## Assumptions

Each is a behavioural decision. Attack them individually.

---

**A1 — `CAPACITY` becomes 16.**

*Why:* the two costs are wildly asymmetric. Undershooting makes this device's
only Return gesture silently inert mid-chain (Problem, step 5). Overshooting
costs 8 bytes per unused slot — `SavedPosition` is two `int`s (`ReturnStack.h:8-11`)
and `slots_` is `SavedPosition[CAPACITY]` (`:52`). 3 → 16 adds **104 bytes** to a
heap-allocated activity. `CLAUDE.md`'s resource protocol caps *stack locals* at
256 bytes; this is not a stack local, and the whole ring at 16 is 136 bytes —
about half that cap. When a silent, unrecoverable failure is on one side and 104
bytes on the other, err high.

Nothing else moves with the number: `push`/`pop`/`unpush`/`oldest` are all
`% CAPACITY` (`:21,27,36,49`), which does not care whether the value is a power
of two, and the deeper ring only makes the destructor's `oldest()` origin
(`EpubReaderActivity.cpp:160-162`) more faithful.

*Attack it:* **16 is not derived from a measurement, and cannot be.** I can bound
the cost from the code; I cannot bound chain depth from the repo, because it is a
reading behaviour, not a data structure. The honest claim is only "comfortably
above ordinary use, at a cost the budget does not notice" — so any number in
8..32 is defensible and 16 is a judgement call inside that band.

The named alternative is **8** (the draft rejected at
`docs/superpowers/plans/2026-09-14-phase-2a-input-model.md:34-37`, and the
issue's own first suggestion). I did not pick it because it is 2.7× the current
bound where 16 is 5.3×, and the difference between them is 40 bytes — too small a
saving to buy a boundary that ordinary chain-following could still reach. If a
reviewer prefers 8, **nothing else in this spec changes**: the tests are
capacity-agnostic by construction (**A4**) and the `static_assert` ceiling
(**A5**) holds at either value. That is deliberate — the number is the cheapest
thing here to revise.

---

**A2 — Do not surface the eviction in the UI, in this change.**

*Why:* it needs user-facing text, which means `tr(STR_*)` — `CLAUDE.md` and
`.claude/agents/ui-dev.md` both forbid hardcoded UI strings. That is a new key in
`lib/I18n/translations/english.yaml` (420 `STR_*` keys today) and its 29 sibling
files, and `lib/I18n/translations/*.yaml` is one of the three files
`.claude/agents/ui-dev.md` names as a **shared append point this surface must
report rather than edit**. It also needs a chrome slot on a 1-bit panel with a
~1.7 s full refresh, and `ROADMAP.md:111` ties the "whether Back and Return need
to be visibly different" question to exactly that chrome.

*Attack it:* an affordance and a bigger ring are not alternatives — a reviewer
could reasonably hold that a *visible* trail is the real fix and a bigger ring
only moves the cliff. That is right, and it is why this is scoped as "in this
change" rather than "ever". Raising the capacity forecloses nothing: if Phase 2
adds a return indicator, it reads `count()` (`ReturnStack.h:45`), which this
change does not touch.

---

**A3 — Do not make the oldest entry sticky.**

*Why:* it rewrites `push`, `pop` and `oldest` semantics, so it invalidates the
meaning of every existing test rather than re-parameterising three of them. More
decisively, it invents a pattern this surface does not establish — a ring where
one slot is exempt from LIFO — and `.claude/agents/ui-dev.md`'s prime directive
says to escalate rather than invent. And it makes the Nth Return land somewhere
the user never passed through, which is the failure mode the issue opens with.

*Attack it:* sticky-oldest is the only option that preserves "get me back to
where I started" at *unbounded* depth, which a bigger ring never does. If the
reviewer holds that the article origin must survive any chain, this is the right
change and A1 is the wrong one — but it is then a decision about Return
semantics, which is Phase 2's, and I would escalate rather than write it here.

---

**A4 — The three capacity-dependent tests are rewritten in terms of
`ReturnStack::CAPACITY` rather than deleted or left failing.**

*Why:* they pin real properties — eviction happens at one past capacity, `oldest()`
is not `slots_[0]` after a wrap, repeated wrapping stays coherent — and those
properties are exactly what `ReturnStack.h:3-7` says the host suite exists to
protect ("an index-by-count read of a wrapped ring is off by one"). Measured in
the research note: compiling the unmodified test file against a `CAPACITY = 8`
header fails **3 tests / 6 assertions** —
`AFourthPushEvictsTheOldestAndPopsStayOneStepBack` (`:61,69`),
`OldestIsThePhysicallyOldestEntryNotSlotZero` (`:83`) and
`SurvivesRepeatedWrapping` (`:163`). The other 9 pass untouched, including
`PopsInLifoOrder:45`, the line `2026-09-14-phase-2a-input-model.md:36` names as
hardcoding 3.

Capacity-agnostic forms were **proven**, not assumed: the three replacements were
compiled and run against headers patched to 3, 8 and 16, and pass at all three
(research note, "Capacity-agnostic tests are feasible").

*Attack it:* a test that derives its expectation from the constant it is testing
can tautologise — if `CAPACITY` were wrong, a test written in terms of it would
not notice. That is accepted here because these tests target the *wrap
arithmetic*, not the value: they assert that one push past the bound evicts
exactly one and that `oldest()` tracks the wrap, which are false for a buggy
modulo at any capacity. **A5** is what pins the value.

---

**A5 — A `static_assert` in `ReturnStack.h` pins the footprint as a ceiling, not
an equality.**

*Why:* `2026-09-13-berean-os-design.md:511-513` says `SavedPosition` needs Unit
addressing before it is persisted. When that lands, `CAPACITY` slots get more
expensive silently. A ceiling makes that trip the build. A `<=` rather than `==`
because struct padding is a toolchain property and an exact figure would be
brittle across host and Xtensa.

*Mirrors* `src/RecentBooksStore.cpp:125-129`, which asserts a budget relation
with a message explaining why the number is derived rather than tidy, and
`src/fontIds.h:18-27`, which is the header-level precedent (`ReturnStack.h` has
no `.cpp`).

*Attack it:* the ceiling is chosen, so it is one more unmeasured number. It is
set with headroom for the 16 slots this spec asks for and a little padding, not
tight to `sizeof`, so it catches a *growth* in `SavedPosition` rather than
policing the current layout.

---

**A6 — Budget the added bytes as internal SRAM, not PSRAM.**

*Why:* the prebuilt core sets `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
(`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2152`),
and `platformio.ini`'s `custom_sdkconfig` block (`:91-114`) does not override it —
so an allocation of 4096 bytes or less is **always** internal, and only a larger
one may land in PSRAM. `EpubReaderActivity.h` declares ~94 members and **no array
or buffer member** (`grep -nE "\[[0-9A-Z_]+\]|std::array|char .*\["` over it
returns nothing), so `sizeof(EpubReaderActivity)` is plausibly under 4096 and the
activity is plausibly an internal-SRAM allocation.

*Attack it:* **this is unmeasured.** It is the conservative reading, and it is
chosen so the spec never claims a PSRAM saving it has not demonstrated —
`CLAUDE.md` forbids asserting a memory gain without the mechanism. It does not
change the decision either way: 104 bytes against a ~380 KB-class pool is inside
the noise even if every byte is internal. The plan phase measures it (Testing
strategy, "Firmware gates") rather than leaving it asserted.

---

**A7 — `unpush()` stays, unused.**

*Why:* it has no caller in `src/` or `lib/` (research note), but it is correct,
tested (`ReturnStackTest.cpp:134,145`), free at runtime, and its documented
purpose — a caller that discovers the navigation failed after pushing — is a real
gap at `EpubReaderActivity.cpp:1701-1704`, where `navigateToHref` returns early
on an unresolved href. It happens that the early return is *before* the push
today, so nothing is stranded.

*Attack it:* an unused method with two tests is dead weight, and this spec's own
research is the argument for deleting it. Deleting it is out of scope: it is not
what #34 asks for, and it would remove the safety net for the next caller that
pushes before it validates.

---

## Architecture

One header and one test file. No new files, no new dependencies, no shared-file
edits.

### Changed: `src/activities/reader/ReturnStack.h`

Three edits, all local:

1. `CAPACITY` 3 → 16 (`:15`).
2. The eviction comment (`:17-18`) is rewritten for the merged state, per
   `CLAUDE.md`'s comment rules ("write them for the merged state, as if the code
   had always worked this way"; no before/after narration). It must carry the
   non-obvious *why* the number is what it is — that the trade is a silently
   inert Return gesture against 8 bytes a slot — because a bare `16` reads
   arbitrary and would be "tidied" by the next reader.
3. A `static_assert` after the class, pinning the footprint ceiling (**A5**),
   with a message that says why the number is derived.

The header's opening comment (`:3-7`) explains why the ring is free of firmware
includes. That constraint holds: `static_assert` and `sizeof` are core language,
so the header stays host-compilable and `test/return_stack/CMakeLists.txt` needs
no change.

### Changed: `test/return_stack/ReturnStackTest.cpp`

Three tests rewritten to derive their boundary from `ReturnStack::CAPACITY`; nine
untouched. One test added for the boundary that currently has no coverage at all
(exactly-`CAPACITY` pushes retain everything — today the 3-push case is covered
only incidentally by `PopsInLifoOrder`, which does not assert `oldest()`).

### Not changed, and why

| File | Why it does not move |
|---|---|
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | Every call site is capacity-blind: `push`, `pop`, `clear`, `count()`, `oldest()` take no bound and expose none. |
| `test/CMakeLists.txt` | `return_stack` is already registered at `:77`. A shared append point (`.claude/agents/ui-dev.md`) this change does not need to touch. |
| `lib/I18n/translations/*.yaml` | No user-facing string (**A2**). The other shared append point, also untouched. |
| `src/MappedInputManager.*` | Out of bounds for this surface, and unnecessary. |
| `docs/file-formats.md` | Nothing is serialised (**Non-goals**). |

---

## Data and control flow

### Push — unchanged mechanism, later boundary

`navigateToHref(href, true)` → `navigateTo(..., ReturnPolicy::Push)` →
`returnStack.push({currentSpineIndex, section->currentPage})`
(`EpubReaderActivity.cpp:1655-1660`). `push` writes `slots_[top_]`, advances
`top_` modulo `CAPACITY`, and clamps `count_` (`ReturnStack.h:19-23`). Identical
code; the clamp simply engages at the 17th push rather than the 4th.

### Return — unchanged

Left-edge swipe → `wasReleased(Button::Back)` true via
`MappedInputManager.cpp:308-309` → the guard at `EpubReaderActivity.cpp:547`
passes while `count() > 0` → `restoreSavedPosition()` (`:1712-1717`) pops and
navigates with `ReturnPolicy::Preserve`.

The observable change is only *when the guard stops passing*. Before: after the
3rd Return of a 4-deep chain, `count()` is 0 and the 4th swipe is inert. After:
the chain is walked all the way back to its origin, and the swipe goes inert only
past 16.

### Clear — unchanged

Any deliberate jump uses the default `ReturnPolicy::Clear`
(`EpubReaderActivity.h:147`) and empties the ring (`:1652-1654`), so depth never
accumulates across unrelated navigations. This is what bounds the *staleness* of
the deeper ring: `oldest()` is always the origin of the current chain, never some
position from an earlier reading session.

### Destructor — unchanged code, better input

`~EpubReaderActivity` saves progress to `oldest()`
(`EpubReaderActivity.cpp:160-162`). With a 16-deep ring, a chain of up to 16
still reports its true origin instead of the second-oldest.

### Nothing crosses a task or a storage boundary

The ring is touched only from the activity's `loop()`/destructor. It takes no
mutex, allocates nothing, and reaches neither `HalStorage` nor
`storageMutex` — `saveProgress` does, but through
`EpubReaderUtils::saveProgress` (`EpubReaderActivity.cpp:1315-1322`), which this
change does not touch and which already owns that path.

---

## Error handling

`ReturnStack` has no failure mode to handle and this change adds none.

- **No allocation.** `slots_` is a fixed by-value array (`ReturnStack.h:52`).
  There is no `new`, so `CLAUDE.md`'s `makeUniqueNoThrow` rule and its OOM
  logging pattern do not engage. This is the "why a static alternative was not
  rejected" that the resource-justification rule asks for: the ring is 136 bytes
  and inseparable from its activity's lifetime.
- **No I/O, no parsing, no network**, so none of `CLAUDE.md`'s four error
  patterns apply — there is nothing to `LOG_ERR` and return false about.
- **Underflow is already handled and stays handled.** `pop` returns false on an
  empty ring and leaves `out` untouched (`:25-31`, pinned by
  `ReturnStackTest.cpp:32-38`); `unpush` guards `count_ > 0` (`:37`, pinned by
  `:145-156`). Both are capacity-independent.
- **Overflow is the eviction**, which is the designed behaviour, not an error.
  It stays silent by design — surfacing it is **A2**.
- The one existing `LOG_DBG` pair (`EpubReaderActivity.cpp:1658`, `:1714`) prints
  `returnStack.count()`, so the serial log already shows the depth and needs no
  change to stay useful at 16.

---

## Testing strategy

### Host — `test/return_stack/`, TDD, red first

The three rewrites are **red before green**: each must be run against the
unmodified `CAPACITY = 3` header and fail (or pass, where the property is
capacity-independent and the test is merely being re-expressed) *before* the
constant moves, so the suite is demonstrably exercising the boundary rather than
following it.

Modelled on `test/recent_books_doc/RecentBooksDocTest.cpp`, which drives every
bound through `RecentBooksDoc::MAX_RECENT_BOOKS` (`:33,88,269,291`) and
`MAX_TITLE_BYTES`/`MAX_AUTHOR_BYTES` (`:113,124,148,241`).

| Test | Property | Status |
|---|---|---|
| `AFourthPushEvictsTheOldest…` → renamed for the general boundary | `CAPACITY + 1` pushes retain `CAPACITY`, evict exactly the first, and pop back to push 2 | rewritten |
| `OldestIsThePhysicallyOldestEntryNotSlotZero` | after a wrap, `oldest()` is push 2, not `slots_[0]` | rewritten |
| `SurvivesRepeatedWrapping` | 100 pushes leave `oldest()` at `100 - CAPACITY + 1` | rewritten |
| **new** — exactly-`CAPACITY` pushes retain everything | the boundary's *other* side: `count() == CAPACITY` and `oldest()` is push 1 | added |
| the other 9 | LIFO, empty-pop, clear (partial and wrapped), post-clear reuse, `unpush` × 2, `oldest` tracking pops | unchanged, must stay green |

Acceptance: the file compiles and passes at `CAPACITY` = 3, 8 and 16. The plan
phase verifies all three by building the unmodified test file against patched
headers — the mechanism the research note already used, so it is known to work.

Commands, from `README.md:143` and mirrored by `.github/workflows/ci.yml:188-195`:

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target ReturnStackTest
ctest --test-dir build/test --output-on-failure -R ReturnStack
```

### What cannot be host-tested

`EpubReaderActivity` is not in the host suite — it pulls `Epub`, `GfxRenderer`,
`MappedInputManager` and `HalStorage`. So the four *integration* claims in
Problem — that the swipe reaches `wasReleased(Button::Back)`, that
`handleBackNavigation` refuses the gesture, that the empty ring makes the swipe
inert, and that the destructor's `oldest()` save is the chain origin — are
established by reading the code, and are **not** covered by any test this change
adds. Stated plainly so the review can weigh them as read-not-run.

### Firmware gates

- `pio run` once, after the last edit (`CLAUDE.md` testing checklist). Note the
  worktree has no `.pio/build` cache and `custom_sdkconfig` rebuilds the Arduino
  core on first build (`platformio.ini:83-85`), so budget a cold build.
- `pio check`.
- `./bin/clang-format-fix` over the **whole tree**, not `-g` — the new/edited
  files must be reachable by CI's full-tree run.
- **Measure `sizeof(EpubReaderActivity)`** to settle **A6** rather than leaving it
  assumed: a temporary
  `static_assert(sizeof(EpubReaderActivity) < 4096, "");` in
  `EpubReaderActivity.cpp`, read off the failure message, then removed. It is a
  diagnostic, not a shipped assertion — the shipped one is **A5**'s, on
  `ReturnStack`.

### What only the human tester can verify — flag in the PR

1. **The eviction is gone where it bit.** In a Watchtower study article, follow a
   footnote, then a footnote from the target page, and so on four deep without
   swiping back. Then left-edge swipe four times. Each must land one step back,
   and the fourth must reach the article — where today it does nothing.
2. **The origin is persisted.** Repeat the four-deep chain, then leave the book
   from the reader menu. Reopen it: it must open at the article, not at the
   second cross-reference.
3. **Nothing regressed at shallow depth.** One footnote, one swipe back — the
   overwhelmingly common case.
4. **The swipe still does not exit the book.** With the ring empty, a left-edge
   swipe on the reading surface must remain inert (`ReaderUtils.h:255-257`); this
   change must not have made it an exit.
5. **Heap.** `ESP.getFreeHeap()` above ~50 KB, and unchanged within noise across
   an open-read-close cycle versus a pre-change build. 104 bytes should not be
   visible; if it is, something else moved.

No cache version moves, so `/.crosspoint/` does not need clearing.

---

## Risks

| Risk | Mitigation |
|---|---|
| 16 is a judgement call, not a measurement (**A1**) | Named as such, with 8 as the priced alternative and the tests written so the number is one line to change. |
| Capacity-derived tests could tautologise (**A4**) | They assert wrap *arithmetic*, false for a buggy modulo at any capacity; **A5** pins the value's cost. |
| The four integration claims are read, not run | Called out explicitly above and in the PR's human-verification list, items 1–4. |
| A reviewer holds this is Phase 2's to decide | Answered below. |

---

## Why this does not need Phase 2

`ROADMAP.md:73` reads as a gate ("Two decisions this phase has to make first"),
and `2026-09-14-phase-2a-input-model.md:34-37` already decided to keep 3. Three
things answer that:

1. **The design doc offers this branch as already-acceptable.**
   `2026-09-13-berean-os-design.md:217-219`: "Either raise the capacity **or**
   show a visible affordance… **Decide in Phase 2.**" Raising it is one of the
   two ratified outcomes, not a new invention.
2. **The Phase 2a plan's stated reason was a test cost, and it was overstated.**
   It priced the change at "four assertions" and named `ReturnStackTest.cpp:45`
   as hardcoding 3. Measured: 3 tests / 6 assertions, and `:45` does not fail at
   all (research note). The rewrite is mechanical and permanent.
3. **This option commits Phase 2 to nothing.** It changes no semantics, no call
   site, no UI, no input-layer code, and no on-disk format. A Phase 2 return
   indicator reads `count()`, which is untouched. The other two options *do*
   commit Phase 2 — one to reader chrome (**A2**), one to a new navigation
   semantic (**A3**) — which is exactly why this spec picks the one that does
   not.

And the premise that this can wait is itself wrong: the swipe is already the only
Return (Problem, step 5), so the failure is live on today's firmware.

---

## Files touched

| File | Change |
|---|---|
| `src/activities/reader/ReturnStack.h` | `CAPACITY` 3 → 16; eviction comment rewritten for the merged state; `static_assert` footprint ceiling |
| `test/return_stack/ReturnStackTest.cpp` | 3 tests re-expressed through `ReturnStack::CAPACITY`; 1 test added; 9 unchanged |

No shared append point is edited. Nothing for the orchestrator to apply on this
surface's behalf.
