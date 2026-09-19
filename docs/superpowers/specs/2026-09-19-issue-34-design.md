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
4. Pin `sizeof(SavedPosition)` so a future widening trips the build rather than
   silently multiplying by `CAPACITY` into internal SRAM.

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
  is persisted anywhere. Out of scope here; **A5** is the guard that makes such
  a widening announce itself.
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
`lib/I18n/translations/english.yaml` (420 `STR_*` keys today) and its 31 sibling
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
modulo at any capacity.

*But that was the wrong failure mode to worry about.* Review pass 0 (**MAJOR 1**)
found the real one, and it is the opposite: **a surviving literal that quietly
stops reaching the boundary.** `ClearEmptiesAWrappedRing` pushes a hardcoded 5,
so at `CAPACITY = 16` it never wraps and becomes a duplicate of the partial-ring
test — still green, still named for coverage it no longer provides. A
failure-count methodology cannot see that, which is precisely why the earlier
draft missed it. Hence the rule now stated in Architecture: *every* loop bound in
the file is `CAPACITY`-relative, not only the ones that currently fail.

---

**A5 — A `static_assert` in `ReturnStack.h` pins `sizeof(SavedPosition)`, the
thing that actually grows — not a chosen ceiling on `sizeof(ReturnStack)`.**

The shipped assertion is:

```cpp
static_assert(sizeof(SavedPosition) == 8,
              "ReturnStack budgets CAPACITY * sizeof(SavedPosition) of internal SRAM -- widening "
              "SavedPosition (Unit addressing, 2026-09-13-berean-os-design.md:511) multiplies by "
              "CAPACITY, so decide the capacity again when this trips");
```

*Why:* `2026-09-13-berean-os-design.md:511-513` says `SavedPosition` needs Unit
addressing before it is persisted. When that lands, every slot gets more
expensive at once. Asserting the *element* catches any widening, is
capacity-independent, and needs no edit when `CAPACITY` moves.

*Mirrors* `src/RecentBooksStore.cpp:125-129`, which asserts a budget relation
with a message explaining why the number is derived rather than tidy, and
`src/fontIds.h:18-27`, which is the header-level precedent (`ReturnStack.h` has
no `.cpp`).

*Attack it:* an element-level assert does not pin the aggregate, so it would not
catch a change to `ReturnStack`'s own layout (a fourth member, say). That is
accepted: such a change is visible in the same 55-line header being edited,
whereas a `SavedPosition` widening arrives from another file and is exactly the
silent multiplication this guards.

*Corrected after review pass 0 (MAJOR 2).* The earlier draft asserted a chosen
ceiling on `sizeof(ReturnStack)` and justified `<=` over `==` on the grounds that
"struct padding is a toolchain property and an exact figure would be brittle
across host and Xtensa." **That is disproven.** `SavedPosition` is two `int`s and
`ReturnStack` is that array plus two `int`s, so there is no padding, and the
figure is exact on both targets. Compiling the real header with `CAPACITY = 16`
against an exact assertion:

```
$ c++ -std=c++2a -c sz.cpp -o /dev/null                       # Apple clang 21.0.0
host: exact 136 OK
$ /Volumes/stein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++ \
    -std=c++2a -c sz.cpp -o /dev/null
xtensa: exact 136 OK
```

(`static_assert(sizeof(SavedPosition) == 8)` and
`static_assert(sizeof(ReturnStack) == 136)` both compile clean on each.) The
second half of the finding is the one that mattered: a ceiling left unspecified
and told to carry "headroom … and a little padding" would sail past the cheapest
Unit-addressing growth — +4 bytes a slot, `16 * 12 + 8 = 200` — which is the one
change it existed to catch, so **Goal 4 was not met as written**. Asserting
`sizeof(SavedPosition)` meets it without choosing a number at all.

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

Four edits, all local:

1. `CAPACITY` 3 → 16 (`:15`).
2. The eviction comment (`:17-18`) is rewritten for the merged state, per
   `CLAUDE.md`'s comment rules ("write them for the merged state, as if the code
   had always worked this way"; no before/after narration). It must carry the
   non-obvious *why* the number is what it is — that the trade is a silently
   inert Return gesture against 8 bytes a slot — because a bare `16` reads
   arbitrary and would be "tidied" by the next reader.
3. **The opening comment's worked example (`:5-7`) is rewritten.** See
   **Comments**.
4. A `static_assert` after the class, pinning `sizeof(SavedPosition)` (**A5**),
   with a message that says why the element is what is asserted.

The *reason* the opening comment gives — that the ring is free of firmware
includes so the wrap arithmetic is host-testable (`:3-5`) — holds unchanged:
`static_assert` and `sizeof` are core language, so the header stays
host-compilable and `test/return_stack/CMakeLists.txt` needs no change.

### Comments — three that go false the moment `CAPACITY` moves

*Added after review pass 0 (MAJOR 3).* The earlier draft enumerated three edits
to this header and affirmatively cleared `:3-7` ("that constraint holds"), which
would have left an implementer following the spec literally shipping a false
comment in the one file the change exists to edit — against the same `CLAUDE.md`
rule the draft invoked two lines earlier for `:17-18`.

| Where | What is false at 16 | Fix |
|---|---|---|
| `src/activities/reader/ReturnStack.h:5-7` | "following **four** citations in a row and noticing which of the **four** Back lands on" | Re-state without a count: *following citations until the ring turns over*. The point is the off-by-one on an index-by-count read of a wrapped ring, which does not need a number. |
| `test/return_stack/ReturnStackTest.cpp:5-7` | "The ring wraps at **three**, so a **fourth** citation makes 'index by count' read the wrong slot" | Same treatment, in the file whose entire purpose after this change is to be capacity-agnostic. The draft's test change list never mentioned it. |
| `.claude/agents/ui-dev.md:44` | "`ReturnStack.h` is the cross-reference return ring (`CAPACITY = 3`, silently evicts the oldest)" | Update the constant. This is the surface's authoritative agent briefing — left stale, the next `ui-dev` task starts from a wrong number. |

`.claude/agents/ui-dev.md` is **not** one of the three shared append points that
file names at `:26-32` (`test/CMakeLists.txt`,
`lib/I18n/translations/*.yaml`, `src/main.cpp`), so "no shared append point is
edited" still holds — but the earlier draft's "nothing for the orchestrator to
apply" did not, and the Files-touched table was incomplete. Both are corrected.

**The roadmap open-item lines are left alone, deliberately:**
`2026-09-13-berean-os-design.md:214,658` and `ROADMAP.md:73,111` record
`ReturnStack` capacity as an open Phase 2 item. This PR closes the *capacity*
half; the design doc's other branch — "whether Back and Return need to be
visibly different" (`ROADMAP.md:111`) — is still open and is **A2**. Striking
those lines wholesale would claim more than this change delivers, and editing
them partially is roadmap bookkeeping better done where the whole Phase 2 item is
being settled. The PR description says which half is closed; the files are not
touched.

### Changed: `test/return_stack/ReturnStackTest.cpp`

**Five** tests rewritten to derive their boundary from `ReturnStack::CAPACITY`;
**seven** untouched, one added — 12 today, 13 after. One test added for the boundary that currently has no
coverage at all (exactly-`CAPACITY` pushes retain everything — today the 3-push
case is covered only incidentally by `PopsInLifoOrder`, which does not assert
`oldest()`). The file's own header comment (`:5-7`) is rewritten too — see
**Comments** below.

**The rule for this file, and it is the whole point of the change:** *every* loop
bound and push count must be `CAPACITY`-relative. Not only the ones that
currently fail.

*Corrected after review pass 0 (MAJOR 1).* The earlier draft rewrote three tests
— the three the measurement showed failing at `CAPACITY = 8` — and asserted the
other nine were unaffected, listing "clear (partial and **wrapped**)" among the
coverage they preserve. **Two of those nine silently stop testing what they are
named for**, and a failure count cannot see it because they stay green:

| Test | Pushes today | What it stops being |
|---|---|---|
| `ClearEmptiesAWrappedRing` (`:112-121`) | hardcoded `5` (`:114`) | A wrap needs `CAPACITY + 1`. At 16, `top_` runs 0→5, never takes the modulo, never clamps `count_`. The test becomes a byte-for-byte duplicate of `ClearEmptiesAPartialRing` (`:100-110`) under a name claiming otherwise — its **setup stops matching its name**. |
| `PushesAfterAClearStartFromScratch` (`:123-132`) | hardcoded `4` (`:125`) | Post-*wrap* reuse at 3; post-*partial* reuse at 16. |

That coverage was commissioned by name in the design doc that created the class:
`docs/superpowers/specs/2026-09-12-reader-return-stack-design.md:283-284` —
"`clear()` on a partial **and a wrapped** ring". Goal 3 ("derive *every*
wrap-boundary assertion from `ReturnStack::CAPACITY`") was therefore not met by
the earlier change list.

Both take `CAPACITY`-relative bounds, and both keep their current literals at 3,
so the "passes at 3, 8 and 16" acceptance criterion is unchanged:

```cpp
// ClearEmptiesAWrappedRing        — CAPACITY + 2 == 5 at CAPACITY = 3
for (int i = 1; i <= ReturnStack::CAPACITY + 2; i++) stack.push(at(i, i * 10));
// PushesAfterAClearStartFromScratch — CAPACITY + 1 == 4 at CAPACITY = 3
for (int i = 1; i <= ReturnStack::CAPACITY + 1; i++) stack.push(at(i, i * 10));
```

This is the general lesson for **A4**, recorded there: the failure mode of a
capacity-agnostic sweep is not a tautological assertion, it is a surviving
literal that quietly stops reaching the boundary.

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

**The red state is the *existing* tests, not the rewrites.** Before the constant
moves, build the unmodified `ReturnStackTest.cpp` against a header patched to 16
and record the failures — 3 tests / 6 assertions, per the research note. That is
the demonstration that the suite reaches the boundary. Then move the constant and
rewrite, and the five rewrites are green at every capacity by construction.

*Corrected after review pass 0 (MINOR 1).* The earlier draft asked for the
rewrites themselves to be "red before green … (or pass, where the property is
capacity-independent)", which is vacuous — the research note had already proven
all three pass at 3, 8 and 16, and the acceptance criterion three paragraphs
below demands exactly that. A requirement that can never be red is a cycle the
implement phase would burn discovering.

Two of the five rewrites (**MAJOR 1**: `ClearEmptiesAWrappedRing`,
`PushesAfterAClearStartFromScratch`) are green at 16 *today* and stay green after
— they are rewritten because they stop **testing** anything, not because they
fail. Their red state cannot be shown by a failure count at all; it is shown by
the reasoning in the table above. Note that explicitly rather than letting a
green run stand in for coverage.

Modelled on `test/recent_books_doc/RecentBooksDocTest.cpp`, which drives every
bound through `RecentBooksDoc::MAX_RECENT_BOOKS` (`:33,88,269,291`) and
`MAX_TITLE_BYTES`/`MAX_AUTHOR_BYTES` (`:113,124,148,241`).

| Test | Property | Status |
|---|---|---|
| `AFourthPushEvictsTheOldest…` → renamed for the general boundary | `CAPACITY + 1` pushes retain `CAPACITY`, evict exactly the first, and pop back to push 2 | rewritten |
| `OldestIsThePhysicallyOldestEntryNotSlotZero` | after a wrap, `oldest()` is push 2, not `slots_[0]` | rewritten |
| `SurvivesRepeatedWrapping` | 100 pushes leave `oldest()` at `100 - CAPACITY + 1` | rewritten |
| `ClearEmptiesAWrappedRing` | `clear()` empties a ring that has **wrapped**, not only a partial one | rewritten (**MAJOR 1**) — `CAPACITY + 2` pushes |
| `PushesAfterAClearStartFromScratch` | post-**wrap** reuse starts clean | rewritten (**MAJOR 1**) — `CAPACITY + 1` pushes |
| **new** — exactly-`CAPACITY` pushes retain everything | the boundary's *other* side: `count() == CAPACITY` and `oldest()` is push 1 | added |
| the other 7 | `StartsEmpty`, `PopOnEmptyFails…`, `PopsInLifoOrder`, `OldestFollowsThePopsBackDown`, `ClearEmptiesAPartialRing`, `UnpushUndoesAPush`, `UnpushOnEmptyLeavesTheRingUsable` | unchanged, must stay green |

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
- **Settle **A6**'s yes/no rather than leaving it assumed:** a temporary
  `static_assert(sizeof(EpubReaderActivity) <= 4096, "");` in
  `EpubReaderActivity.cpp`, then removed. It is a diagnostic, not a shipped
  assertion — the shipped one is **A5**'s, on `SavedPosition`.

  **If it passes**, the activity is an internal-SRAM allocation, **A6**'s
  conservative budgeting is confirmed, and the 104 bytes are real internal SRAM —
  which changes nothing, as **A6** says.

  **If it fails, stop and escalate — do not just note the number.** Crossing 4096
  means the whole activity allocation flips to PSRAM, because the default
  allocator prefers SPIRAM above `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`
  (`…/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2152`). That would put
  a render-hot-path object on the slow external SPI bus, which is a far larger
  effect than this change and is not this change's to make. `EpubReaderActivity.h`
  is 180 lines with no array or buffer member, so it is very unlikely — but
  **A6**'s "it does not change the decision either way" is only true away from
  that boundary.

  *Corrected after review pass 0 (MINOR 4).* The earlier draft said to "read off
  the failure message". A passing `static_assert` emits nothing and a failing one
  prints its own message, not the operand's value, so that step described
  something that cannot happen. The gate answers a yes/no, which is all **A6**
  asks — and the consequence of "no" is now stated.

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
4. **The swipe still does not exit the book** — *with touch reader controls left
   at the default tap mode* (`touchReaderControls = TOUCH_READER_ON`,
   `src/CrossPointSettings.h:346`). With the ring empty, a left-edge swipe on the
   reading surface must remain inert (`ReaderUtils.h:255-257`); this change must
   not have made it an exit.

   **Under `TOUCH_READER_SWIPE` (`CrossPointSettings.h:233`) the same swipe pages
   back instead, and that is correct, not a regression**: `detectTouchPageTurn`
   maps a right-ward swipe to `result.prev` (`ReaderUtils.h:84-92`), consumed at
   `EpubReaderActivity.cpp:446,595`, which `ReaderUtils.h:248-250` documents on
   purpose ("in swipe page-turn mode a right swipe must page back instead").
   Stated so a tester with swipe controls on does not file a bug that is a
   setting. *Added after review pass 0 (MINOR 3).*
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
4. **The one document that argued for keeping 3, answered on its own terms.**
   *Added after review pass 0 (MAJOR 4).* The earlier draft said "three documents
   speak to it", as did the research note. There is a fourth, and it is the spec
   that created the class:
   `docs/superpowers/specs/2026-09-12-reader-return-stack-design.md`. It lists
   the change as a non-goal at `:20` — "**Raising `MAX_FOOTNOTE_DEPTH`.** Three
   slots stay three slots (decided)" — and at `:210-215`, under *"The eviction
   trade-off, stated honestly"*, gives the only reasoned defence of 3 anywhere in
   the repo:

   > At depth ≥ 4 the new behaviour gives three correct one-step returns and then
   > drops out of the book … The new behaviour is chosen because every individual
   > Back is correct and **depth ≥ 4 without an intervening Back is rare**; it is
   > not a free win.

   That rarity clause is the substantive prior objection to this entire spec, and
   the earlier draft neither cited nor rebutted it.

   **The answer is that its escape hatch no longer exists.** The trade is stated
   as three correct returns *and then* "drops out of the book" — a user who
   overran the ring still left by the same gesture. That is no longer what
   happens: `handleBackNavigation`'s gesture guard
   (`src/activities/reader/ReaderUtils.h:255-257`) landed after that document and
   makes the empty-ring swipe **inert** rather than an exit. So the failure the
   rarity argument was sized against ("you exit early") has been replaced by a
   strictly worse one ("nothing happens at all"), and rarity no longer carries
   it. That guard is also independent corroboration of this spec's central
   finding, from a direction the Problem section did not use.

   One clarification the same document invites: `MAX_FOOTNOTE_DEPTH` at `:20` is
   `CAPACITY`'s predecessor, not a second live cap.
   `grep -rn MAX_FOOTNOTE_DEPTH src/ lib/ test/` returns nothing — it survives in
   documentation only. Worth stating, because a reader arriving at `:20` and
   `:30-31` would otherwise wonder whether a separate depth cap still pins the
   chain at 3 regardless of `CAPACITY`, which would make this change inert.

And the premise that this can wait is itself wrong: the swipe is already the only
Return (Problem, step 5), so the failure is live on today's firmware.

---

## Files touched

| File | Change |
|---|---|
| `src/activities/reader/ReturnStack.h` | `CAPACITY` 3 → 16; eviction comment (`:17-18`) and the opening comment's worked example (`:5-7`) rewritten for the merged state; `static_assert(sizeof(SavedPosition) == 8)` |
| `test/return_stack/ReturnStackTest.cpp` | 5 tests re-expressed through `ReturnStack::CAPACITY`; header comment (`:5-7`) rewritten; 1 test added; 7 unchanged |
| `.claude/agents/ui-dev.md` | `:44` still says `CAPACITY = 3`; update the constant (**MAJOR 3**) |

No shared append point is edited — `.claude/agents/ui-dev.md` is not one of the
three that file names at `:26-32`. The `ui-dev.md` row is the only thing here the
orchestrator may prefer to apply itself, since it is the briefing the next task
on this surface reads.

`docs/superpowers/specs/2026-09-13-berean-os-design.md:214,658` and
`ROADMAP.md:73,111` are **deliberately left alone** — see *Comments* for why this
PR closes only the capacity half of that open item.

---

## Review pass 0 — what changed and why

Verdict **CLEAR**: 0 blockers, 4 MAJORs, 4 MINORs, all applied above and marked
in place. Review: `docs/superpowers/reviews/issue-34-spec-review-0.md`.

The reviewer independently reproduced the 3-tests / 6-assertions measurement and
re-verified the swipe-is-the-only-Return chain, adding one citation this spec had
not used — `HalGPIO.h:57`, where `BTN_BACK` is `PIN_UNASSIGNED` on this board.
The core decision (A1, 16) survived review unchanged.

| # | Finding | What changed |
|---|---|---|
| MAJOR 1 | `ClearEmptiesAWrappedRing` and `PushesAfterAClearStartFromScratch` push hardcoded 5 and 4, so at 16 they stop testing a wrapped ring while staying green | Architecture § *Changed: ReturnStackTest.cpp* rewritten: 5 tests rewritten / 7 untouched, both added with `CAPACITY`-relative bounds, plus an explicit rule that *every* loop bound is `CAPACITY`-relative. A4 and the Testing table follow. |
| MAJOR 2 | A5's "brittle across host and Xtensa" is factually wrong, and an unspecified ceiling misses the growth it exists to catch | A5 rewritten: the shipped assertion is now `sizeof(SavedPosition) == 8`, with the disproof (exact 136 on both toolchains) recorded. Goal 4 restated. |
| MAJOR 3 | `ReturnStack.h:5-7`, `ReturnStackTest.cpp:5-7` and `.claude/agents/ui-dev.md:44` all go false at 16; the spec affirmatively cleared the first | New Architecture § *Comments*; `ui-dev.md` added to Files touched; the design-doc/ROADMAP open-item lines explicitly left alone, with the reason. |
| MAJOR 4 | `2026-09-12-reader-return-stack-design.md:20,210-215` is a fourth document that rejected this change with a reason ("depth ≥ 4 … is rare") the spec never cited | Added as item 4 of *Why this does not need Phase 2*, answered on its own terms: its "drops out of the book" escape hatch was removed by `ReaderUtils.h:255-257`. `MAX_FOOTNOTE_DEPTH` confirmed docs-only. |
| MINOR 1 | "red first" as written can never be red | Testing § reworded: the *existing* tests are the red state. |
| MINOR 2 | "29 sibling" i18n files | 31 (32 YAML, less `english.yaml`). |
| MINOR 3 | Human-verification item 4 is false under `TOUCH_READER_SWIPE` | Qualified to the default tap mode, with the swipe-mode behaviour explained as correct. |
| MINOR 4 | The A6 gate's "read off the failure message" cannot happen | Reworded to a yes/no, with "if it fails, stop and escalate" and why. |

**One correction to the review, applied rather than adopted.** MAJOR 1's fix says
the change becomes "5 rewritten / 8 untouched". The file has 12 tests
(`grep -c "^TEST(ReturnStack," test/return_stack/ReturnStackTest.cpp` → 12), so 5
rewritten leaves **7** untouched, and 13 after the one addition. The Architecture
paragraph, the Testing table and Files touched all use 5 / 7 / 13 and name the
seven individually, so the count is checkable rather than asserted.

---

## Plan review pass 0 — one correction back into this spec

Plan review pass 0 (`docs/superpowers/reviews/issue-34-plan-review-0.md`, MAJOR 2)
found that a claim **this spec introduced** is wrong, so it is corrected above
rather than only in the plan.

**The claim.** Under *Changed: `test/return_stack/ReturnStackTest.cpp`*, this spec
said `ClearEmptiesAWrappedRing` pins "that `clear()` resets `top_` as well as
`count_`, so a wrapped ring is genuinely empty rather than half-rotated", and that
the property "loses **all** coverage" at `CAPACITY = 16`.

**Why it is wrong.** `top_ = 0` in `clear()` (`src/activities/reader/ReturnStack.h:41`)
is not observable through the public API. Every observable after `clear()` is a
function of `count_` alone: `count()` returns it, `pop()` and `oldest()` both
short-circuit on `count_ == 0`, and once `count_` is 0 the following pushes are
rotation-invariant — `oldest()` resolves to `slots_[top_]`, which is wherever the
first push went. So **no host test can pin that line, at 3 or at 16**, and the
property was never covered rather than losing coverage. Mutation-checked: delete
`top_ = 0` from `clear()` and all 13 rewritten tests pass at both capacities.

This came in through spec review pass 0's MAJOR 1, and I adopted it without
checking whether the property was observable. The finding's *conclusion* stands
untouched — both tests still need `CAPACITY`-relative bounds, because a setup that
no longer reaches the wrap makes the test's name a lie and leaves a literal in a
file whose whole purpose is to have none. Only the justification changes.

**What replaces it as evidence for A4.** A mutation check, now Step 4b of the plan:
breaking `oldest()` into the naive `&slots_[0]` kills 3 tests at `CAPACITY = 3`
*and* at 16. That is what demonstrates the rewritten bounds still catch a real wrap
bug rather than following the constant — which is the reassurance **A4**'s
anti-tautology paragraph was reaching for and never actually collected.
