# Issue #34 — `ReturnStack` capacity before Back becomes primary: research

Date: 2026-09-19 · Branch: `feature/34-return-stack-capacity` · Surface: `ui`

Issue #34 asks whether `CAPACITY = 3` survives the promotion of the return ring
to the device's primary Back, and offers three options: raise the capacity,
surface the eviction in the UI, or make the oldest entry sticky. It records that
the decision "belongs with whoever designs the Phase 2 input model."

This note establishes what is true in the tree today. It does not choose.

---

## Who owns the behaviour

| Concern | File:line |
|---|---|
| The ring, the constant, the eviction | `src/activities/reader/ReturnStack.h:15` (`CAPACITY = 3`) |
| Its one instance | `src/activities/reader/EpubReaderActivity.h:78` (`ReturnStack returnStack;`) |
| The only push | `src/activities/reader/EpubReaderActivity.cpp:1657` |
| The only pop | `src/activities/reader/EpubReaderActivity.cpp:1713` (`restoreSavedPosition`) |
| The clear | `src/activities/reader/EpubReaderActivity.cpp:1653` |
| The `oldest()` read | `src/activities/reader/EpubReaderActivity.cpp:160-162` (the **destructor**) |
| Back → restore | `src/activities/reader/EpubReaderActivity.cpp:547-551` |
| Power-button → restore | `src/activities/reader/EpubReaderActivity.cpp:560-561` |
| Host tests | `test/return_stack/ReturnStackTest.cpp` (12 tests), `test/return_stack/CMakeLists.txt` |

Grepped `ReturnStack|returnStack|SavedPosition` across `src/`, `lib/` and
`freeink-sdk/`: the list above is exhaustive. **`ReturnStack` has exactly one
owner and nine call sites.** Nothing outside `EpubReaderActivity` touches it.

`unpush()` (`ReturnStack.h:35-38`) has **no caller in `src/` or `lib/`** —
`grep -rn "unpush" src/ lib/` matches only its own definition. It is exercised
only by two host tests (`ReturnStackTest.cpp:134,145`). Its doc comment
describes a caller that does not exist yet.

---

## Current control flow, verified

`ReturnPolicy` (`EpubReaderActivity.h:130`) is `{ Clear, Push, Preserve }` and is
applied only inside `navigateTo` (`EpubReaderActivity.cpp:1641-1665`).

**Pushes.** `ReturnPolicy::Push` is reached from exactly one place:
`navigateToHref(href, savePosition = true)` (`:1706-1707`). That has three
callers, all footnote follows:

- `:564` — the single-footnote-on-page shortcut (power button, `FOOTNOTES` mode)
- `:571` — a pick from `EpubReaderFootnotesActivity`
- `:766` — the same pick, from the reader menu's footnote entry

So in this firmware "following a citation" is *following a footnote link*. There
is no other push path.

**Clears.** `navigateTo`'s default policy is `ReturnPolicy::Clear`
(`EpubReaderActivity.h:147`), so every deliberate jump — sync (`:362`, `:690`),
percent jump (`:678`), chapter pick (`:752`), TOC (`:712`) — empties the ring.
This is what commit `69d592e5` ("clear the return stack on deliberate
navigation", #6) established.

**Pops.** `restoreSavedPosition` (`:1712-1717`), reached from `:549` (Back
released under `GO_BACK_OR_HOME_MS`) and `:560` (short power press in
`SHORT_PWRBTN::FOOTNOTES` mode). It navigates with `ReturnPolicy::Preserve`.

**Ordinary page turns do not go through `navigateTo` at all** — they step
`section->currentPage` directly (`:915` forward, `:932` back). The ring
therefore survives reading onward after a jump.

### What this means for how the depth actually grows — a correction to the issue

The issue argues eviction is reachable because "a paragraph with four cited
scriptures is ordinary." That framing does not match the control flow. Tapping
four *sibling* citations in one paragraph means: follow, Back, follow, Back… and
each Back pops. That pattern never exceeds depth 1.

Depth grows only on **nested** follows with no intervening return: footnote →
land → follow a footnote from *there* → and so on. That is a real reading
pattern (a chain-reference walk through cross-references, or a scripture whose
target page itself carries the next citation), but it is a chain, not a fan.

This does not settle whether 3 is too small — it changes the argument that has
to be made for whatever number the spec picks. Recording it here so the spec
does not inherit an unverified premise.

### A second consequence of eviction, live today

`~EpubReaderActivity` (`EpubReaderActivity.cpp:160-162`) saves reading progress
to `returnStack.oldest()` — the position the user jumped *from* — rather than to
wherever the footnote chain left them. Once the ring has wrapped, `oldest()` is
no longer the article origin (pinned by `ReturnStackTest.cpp:72-84`), so closing
the book from four-deep persists the **second**-oldest position.

That is a present-day progress-fidelity effect, not a Phase 2 one. It is not
mentioned in the issue.

---

## What the repo has already decided about this

Three documents speak to it, and they do not agree.

1. **The design doc**, `docs/superpowers/specs/2026-09-13-berean-os-design.md:214-219`:

   > Either raise the capacity **or** show a visible affordance when the stack is
   > non-empty so *Back* and *Return* are distinguishable. **Decide in Phase 2.**

   Note it presents raising the capacity as one of two *acceptable* resolutions,
   not as something Phase 2 must invent. The same doc's Open items list
   (`:658-659`) and `ROADMAP.md:73,111` repeat the deferral.

2. **The Phase 2a plan**, `docs/superpowers/plans/2026-09-14-phase-2a-input-model.md:34-37`,
   already considered and *rejected* raising it:

   > **`ReturnStack` keeps `CAPACITY = 3`.** The draft raised it to 8 and asserted
   > the existing tests were parameterised. They are not — `ReturnStackTest.cpp:45`
   > hardcodes 3, and the eviction-semantics tests assert positions that a capacity
   > change invalidates. Not worth breaking four assertions for a marginal gain.

   Restated at `:620-622`: "left at 3. The tests are not parameterised and the
   gain is marginal; revisit if 2b's chrome wants a return indicator."

   **The stated reason is a test-maintenance cost, not a design objection.** It
   is measured below, and it is smaller than that plan asserts.

3. **Phase 2 has not happened.** Verified in this tree:
   `src/activities/Activity.h:22,28-29` still constructs `MappedInputManager`,
   and `EpubReaderActivity.cpp:547` still reads
   `mappedInput.wasReleased(MappedInputManager::Button::Back)`. So the input
   model that was supposed to own this decision does not exist yet, and Back is
   still the left-edge swipe.

---

## The test-cost claim, measured

Baseline, this tree, `CAPACITY = 3`:

```
$ cmake -S test -B <build> -DCMAKE_BUILD_TYPE=Release && cmake --build <build> --target ReturnStackTest
$ <build>/return_stack/ReturnStackTest
[==========] 12 tests from 1 test suite ran.
[  PASSED  ] 12 tests.
```

Then compiling the **unmodified** `ReturnStackTest.cpp` against a header whose
only edit is `CAPACITY = 8`:

```
[  FAILED  ] ReturnStack.AFourthPushEvictsTheOldestAndPopsStayOneStepBack
[  FAILED  ] ReturnStack.OldestIsThePhysicallyOldestEntryNotSlotZero
[  FAILED  ] ReturnStack.SurvivesRepeatedWrapping
[  PASSED  ] 9 tests.
 3 FAILED TESTS
```

**3 tests, 6 assertions** — at `ReturnStackTest.cpp:61,69` (eviction at the
fourth push), `:83` via `expectPosition` (`oldest()` is the 2nd push), and `:163`
(`oldest()` is push 98 of 100). The other 9 tests, including
`ClearEmptiesAWrappedRing` (`:112`, 5 pushes) and `PushesAfterAClearStartFromScratch`
(`:123`, 4 pushes), pass unchanged.

`PopsInLifoOrder:45` — the line the Phase 2a plan cites as hardcoding 3 —
**does not fail**: it pushes 3 and asserts `count() == 3`, which holds for any
`CAPACITY >= 3`.

### Capacity-agnostic tests are feasible — proven, not asserted

Each of the three failures is a wrap-boundary assertion re-expressible through
`ReturnStack::CAPACITY`. I wrote the three replacements and ran them against
headers patched to 3, 8 and 16:

```
CAPACITY=3  -> [  PASSED  ] 3 tests.
CAPACITY=8  -> [  PASSED  ] 3 tests.
CAPACITY=16 -> [  PASSED  ] 3 tests.
```

(The proof drives the loop bound from `ReturnStack::CAPACITY` — `CAP + 1` pushes
evict exactly one, `oldest()` is push 2; `CAP` pushes retain everything; 100
pushes leave `oldest()` at `100 - CAP + 1`.) So the cost the Phase 2a plan priced
as "breaking four assertions" is a mechanical rewrite of three tests that then
stop needing to be touched again.

### The nearest existing example of this shape

`test/recent_books_doc/RecentBooksDocTest.cpp` — a capped collection with
eviction whose tests drive every bound through the class constant
(`RecentBooksDoc::MAX_RECENT_BOOKS` at `:33,88,269,291`;
`MAX_TITLE_BYTES`/`MAX_AUTHOR_BYTES` at `:113,124,148,241`) rather than through
literals. Its constant lives at `src/util/RecentBooksDoc.h:22`. It landed in
`2df75e5b` (#68), the most recent non-release commit of this shape. That is the
file to model the rewritten `ReturnStackTest.cpp` on.

---

## Cost of each option, as this tree prices it

**Raise `CAPACITY`.** `SavedPosition` is two `int`s (`ReturnStack.h:8-11`) = 8
bytes; `slots_` is `SavedPosition[CAPACITY]` (`:52`). 3 → 8 adds 40 bytes; 3 → 16
adds 104. The ring is a by-value member of `EpubReaderActivity`, which is
heap-allocated via `makeUniqueNoThrow` (`src/activities/reader/ReaderActivity.cpp:29`).

  Placement caveat, **not measured**: the prebuilt core sets
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
  (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2152`),
  and `platformio.ini`'s `custom_sdkconfig` block (`:91-114`) does not override
  it — so an allocation of 4096 bytes or less is always internal SRAM, and only
  a larger one may land in PSRAM. `sizeof(EpubReaderActivity)` is unmeasured:
  the header declares ~94 members and **no array or buffer member**
  (`grep -nE "\[[0-9A-Z_]+\]|std::array|char .*\["` over
  `EpubReaderActivity.h` returns nothing), so it is plausibly under 4096 and the
  added bytes should be budgeted as **real internal SRAM**. That is the
  conservative reading; measure it with a `static_assert` at implement time
  rather than assuming PSRAM. Either way the delta is ≤ 104 bytes against a
  ~380 KB-class pool.

  Semantics unchanged: no call site, no UI, no input-layer code changes.

**Surface the eviction in the UI.** Needs user-facing text, which means
`tr(STR_*)` (`CLAUDE.md`, and `.claude/agents/ui-dev.md`: "All user-facing text
uses `tr(STR_*)`"). That means a new key in `lib/I18n/translations/english.yaml`
(420 `STR_*` keys today) and its 29 sibling language files — and
`lib/I18n/translations/*.yaml` is one of the three files `.claude/agents/ui-dev.md`
names as **shared append points this surface must report rather than edit**. It
also needs a chrome slot in the reader, on a 1-bit panel with a ~1.7 s full
refresh, and `ROADMAP.md:111` couples the "Back vs Return visibly different"
question to that same chrome.

**Make the oldest entry sticky.** No new strings and no UI. It changes `push`,
`pop` and `oldest` semantics, so it rewrites the meaning of every existing test
rather than re-parameterising three, and it introduces a behaviour this repo has
no precedent for: a ring where one slot is exempt from LIFO. `.claude/agents/ui-dev.md`'s
prime directive says to escalate before inventing a pattern the surface does not
establish. It would also make Back's Nth press land somewhere the user did not
pass through — the exact failure mode the issue opens with.

---

## The question this research cannot close

**Phase 2 has not happened, and the design doc assigns this decision to it.**
The `ROADMAP.md:73` framing — "two decisions this phase has to make first" —
reads as a gate. But the design doc (`:217-219`) offers raising the capacity as
one of the two acceptable resolutions, and raising it changes no semantics, no
call site, no UI and no input-layer code, so it forecloses nothing Phase 2 might
decide. Surfacing the eviction in the UI *does* commit Phase 2 chrome, and
sticky-oldest *does* invent a navigation semantic.

That asymmetry is the material finding. Carrying it to the spec.

---

## Environment, as actually installed

| Tool | Version | Notes |
|---|---|---|
| Host | `Darwin 25.5.0`, arm64 | `uname -s` → `Darwin` |
| C++ | Apple clang 21.0.0 (clang-2100.0.123.102) | host test suite |
| CMake | 4.4.2 | `cmake --version` |
| GoogleTest | v1.17.0 | pinned, `test/CMakeLists.txt:17` |
| clang-format | 21.1.8 | via `.venv/bin`; `./bin/clang-format-fix -g` exits 0 clean |
| PlatformIO | `/Volumes/stein/.platformio/penv/bin/pio` | **not on `PATH`** by default in this worktree |
| `freeink-sdk` | `310ec61506fc915836db7799a2e7f4fc135a570d` | submodule initialised |
| `.pio/build` | absent | no firmware build cache; a `pio run` here is cold, and `custom_sdkconfig` rebuilds the Arduino core on first build (`platformio.ini:83-85`) |

Host suite command, from `README.md:143` and mirrored by
`.github/workflows/ci.yml:188-195`:

```bash
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

C++20, `-Wall -Wextra -pedantic` (`test/CMakeLists.txt:4,42-46`). `test/CMakeLists.txt`
is a shared append point (`.claude/agents/ui-dev.md`), but `return_stack` is
already registered, so no edit to it is needed for a change confined to this ring.
