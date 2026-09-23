# Adversarial review — `2026-09-19-issue-34-design.md`, pass 0

Target: `docs/superpowers/specs/2026-09-19-issue-34-design.md`
Against: issue #34, `docs/superpowers/research/2026-09-19-issue-34-research.md`
Tree: `feature/34-return-stack-capacity` @ `3f34834f`, clean before and after this review.

## What I verified and found sound

The spec's load-bearing chain is correct. I re-walked all five steps of the Problem
section against the tree:

1. `src/MappedInputManager.cpp:309` — `if (button == Button::Back && wasBackGesture()) return true;`
   inside `wasReleased`, ahead of `mapButton`. ✅ (`wasBackGesture()` at `:266-271`.)
2. `src/activities/reader/EpubReaderActivity.cpp:547` — the return branch gates on
   `wasReleased(Button::Back)` with no gesture exclusion. ✅
3. `src/activities/reader/ReaderUtils.h:255-257` — `handleBackNavigation` returns false
   on `wasBackGesture()` first. ✅
4. No physical Back: `mapButton` routes `Button::Back` to `SETTINGS.frontButtonBack`
   (`MappedInputManager.cpp:61-63`), whose default is `FRONT_HW_BACK`
   (`CrossPointSettings.h:286`) = `HalGPIO::BTN_BACK` (`lib/hal/HalGPIO.h:137`), and
   `HalGPIO.h:57` records "BTN_BACK and BTN_CONFIRM are PIN_UNASSIGNED on this board". ✅
5. `CrossPointSettings.h:276` `uint8_t shortPwrBtn = IGNORE;`, `IGNORE = 0` at `:178`. ✅

The exhaustiveness claims hold. `grep -rn -E "ReturnStack|SavedPosition" src/ lib/ freeink-sdk/`
excluding `ReturnStack.h` and `EpubReaderActivity.*` returns **nothing**; `grep -rn "unpush" src/ lib/`
matches only the definition (`ReturnStack.h:35`); the "no array or buffer member" grep over
`EpubReaderActivity.h` returns nothing. All of `:78`, `:130`, `:147`, `:160-162`, `:1653`,
`:1657-1658`, `:1701-1704`, `:1707`, `:1714` check out, as do
`RecentBooksStore.cpp:125-129`, `fontIds.h:18-27`, `test/CMakeLists.txt:77`,
`RecentBooksDocTest.cpp:{33,88,269,291,113,124,148,241}`, `RecentBooksDoc.h:22`,
`platformio.ini:83-85,91-114`, `framework-arduinoespressif32-libs/esp32s3/sdkconfig:2152`
(`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, not overridden — `grep -n SPIRAM platformio.ini`
is empty), `ROADMAP.md:73,111`, `2026-09-13-berean-os-design.md:217-219,511-513` and
`2026-09-14-phase-2a-input-model.md:34-37,620-622`.

I reproduced the measured test cost rather than trusting it:

```
baseline (CAPACITY=3):  [  PASSED  ] 12 tests.
CAPACITY=8:  3 FAILED TESTS (AFourthPush…, OldestIsThePhysicallyOldest…, SurvivesRepeatedWrapping), 6 Failure lines, 9 passed
CAPACITY=16: 3 FAILED TESTS (same three), 6 Failure lines, 9 passed
```

So "3 tests / 6 assertions" is right, and `PopsInLifoOrder:45` — the line the Phase 2a plan
named — does not fail at either value. The spec's rebuttal of that plan is factually correct.

Everything below is what survives that.

---

## MAJOR 1 — `ClearEmptiesAWrappedRing` stops testing a wrapped ring at `CAPACITY = 16`, and the spec asserts the opposite

**Claim.** Testing strategy, last table row: "the other 9 | LIFO, empty-pop, clear (**partial and
wrapped**), post-clear reuse, `unpush` × 2, `oldest` tracking pops | unchanged, must stay green".
And Architecture: "Three tests rewritten … **nine untouched**."

**Problem.** Green is not the same as still-testing-anything. `ReturnStackTest.cpp:112-121`:

```cpp
TEST(ReturnStack, ClearEmptiesAWrappedRing) {
  ReturnStack stack;
  for (int i = 1; i <= 5; i++) stack.push(at(i, i * 10));
  stack.clear();
```

A wrap requires `CAPACITY + 1` pushes. At `CAPACITY = 3`, five pushes wrap twice. At
`CAPACITY = 16`, `top_` runs 0→5 and never takes the modulo — `count_` never clamps, no slot is
ever overwritten. The test still passes, which is exactly why the spec's failure-count methodology
did not see it: it is now a byte-for-byte duplicate of `ClearEmptiesAPartialRing` (`:100-110`)
under a name that claims otherwise. The property it was written to pin — that `clear()` resets
`top_` as well as `count_`, so a *wrapped* ring is genuinely empty rather than half-rotated — has
no coverage at all after this change.

This is not incidental coverage. The owning design doc commissioned it by name:
`docs/superpowers/specs/2026-09-12-reader-return-stack-design.md:283-284` — "Cover: push/pop LIFO
ordering; wrap at capacity …; `clear()` on a partial **and a wrapped** ring; `pop` on empty;
`unpush` symmetry."

`PushesAfterAClearStartFromScratch` (`:123-132`, four pushes) degrades the same way for the same
reason: at 3 it is post-wrap reuse, at 16 it is post-partial reuse. The spec's table row
"post-clear reuse" survives in name only.

Goal 3 — "derive **every** wrap-boundary assertion from `ReturnStack::CAPACITY`" — is therefore not
met by the spec's own change list, and the spec is measurably wrong that nine tests are unaffected.

**Fix.** Add both to the rewritten set (making it 5 rewritten / 8 untouched, and updating the
Architecture paragraph, the Testing table and the Files-touched table to match):

```cpp
// ClearEmptiesAWrappedRing
for (int i = 1; i <= ReturnStack::CAPACITY + 2; i++) stack.push(at(i, i * 10));
// PushesAfterAClearStartFromScratch
for (int i = 1; i <= ReturnStack::CAPACITY + 1; i++) stack.push(at(i, i * 10));
```

Both still pass at 3 (`3 + 2 = 5`, `3 + 1 = 4` — the current literals), so the acceptance criterion
"passes at 3, 8 and 16" is unchanged. Add a line to the Testing section making the sweep explicit:
*every* loop bound in the file must be `CAPACITY`-relative, not just the ones that currently fail.

---

## MAJOR 2 — A5's justification for a ceiling instead of an equality is factually wrong, and the looseness it buys is what defeats Goal 4

**Claim.** A5: "A `<=` rather than `==` because struct padding is a toolchain property and an exact
figure would be brittle across host and Xtensa." Attack paragraph: the ceiling "is set with headroom
for the 16 slots this spec asks for and a little padding, not tight to `sizeof`". Goal 4: "Pin the
memory footprint so a future `SavedPosition` growth trips the build."

**Problem.** Two things.

First, the stated premise is false. `SavedPosition` is two `int`s and `ReturnStack` is
`SavedPosition[CAPACITY]` plus two `int`s — there is no padding to be a toolchain property of. I
compiled the real header against both toolchains:

```
$ xtensa-esp32s3-elf-g++ -std=c++2a -c sz.cpp      # CAPACITY=3
XTENSA OK: sizeof(ReturnStack)==32, sizeof(SavedPosition)==8, alignof==4
$ clang++ -std=c++2a -c sz.cpp
HOST(arm64 clang) OK: identical
$ # same file with CAPACITY=16, asserting sizeof(ReturnStack)==136 exactly
XTENSA: sizeof(ReturnStack)==136 exactly at CAPACITY=16
HOST:   sizeof(ReturnStack)==136 exactly at CAPACITY=16
```

(`static_assert(sizeof(ReturnStack) == 136)` compiles clean on
`/Volumes/stein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++` and on
Apple clang.) An equality is not brittle here; it is exact on both targets.

Second, and worse, the spec ships the *number* to the plan phase with no value and contradictory
guidance. Goal 4 only holds if the ceiling is tighter than the smallest plausible growth. The growth
A5 anticipates is Unit addressing (`2026-09-13-berean-os-design.md:511-513`), whose cheapest form is
+4 bytes per slot → `16 * 12 + 8 = 200`. A ceiling chosen with "headroom … and a little padding" —
say 160, or 192, or a tidy 256 — silently fails to trip on exactly the change it exists to catch.
"Not tight to `sizeof`" and "catches a growth in `SavedPosition`" are, at this scale, mutually
exclusive instructions.

**Fix.** Name the constant in the spec and derive it, rather than deferring it. Either:

- `static_assert(sizeof(SavedPosition) == 8, …)` — assert the thing that actually grows, which is
  capacity-independent and trips on *any* widening; or
- `static_assert(sizeof(ReturnStack) <= ReturnStack::CAPACITY * 8 + 8, …)`, which is an equality in
  ceiling clothing and is exact on both toolchains per the evidence above.

Either way, strike the "brittle across host and Xtensa" sentence — it is disproven — and replace
A5's attack paragraph with the real trade (an exact figure needs editing when `CAPACITY` moves; a
per-slot assert does not).

---

## MAJOR 3 — three comments the spec declares unaffected become false the moment `CAPACITY` moves

**Claim.** Architecture: "The header's opening comment (`:3-7`) explains why the ring is free of
firmware includes. **That constraint holds.**" Files touched: two rows, then "No shared append point
is edited. **Nothing for the orchestrator to apply on this surface's behalf.**"

**Problem.** `ReturnStack.h:3-7` says more than the include constraint:

```
// arithmetic can be tested on the host: reproducing a wrap on the device means
// following four citations in a row and noticing which of the four Back lands
// on, and an index-by-count read of a wrapped ring is off by one.
```

At 16 it is seventeen citations, and "which of the four Back lands on" is wrong. The spec enumerates
exactly three edits to this header (`:15`, `:17-18`, the `static_assert`) and affirmatively clears
`:3-7`, so an implementer following the spec literally leaves a false comment in the one file this
change exists to edit — against `CLAUDE.md`'s "write them for the merged state" rule that the same
paragraph invokes for `:17-18`.

`test/return_stack/ReturnStackTest.cpp:5-7` is worse, because the spec's test change list never
mentions it at all:

```cpp
// Back after a citation must land one step back, every time. The ring wraps at
// three, so a fourth citation makes "index by count" read the wrong slot and
// the destructor's origin stop being slots_[0]. These pin both.
```

"The ring wraps at three" and "a fourth citation" are both false at 16, in the header comment of the
file whose entire purpose after this change is to be capacity-agnostic.

Third: `.claude/agents/ui-dev.md:44` — "`ReturnStack.h` is the cross-reference return ring
(`CAPACITY = 3`, silently evicts the oldest)." That file is this surface's authoritative agent
briefing; leaving it saying 3 means the next ui-dev task starts from a wrong constant. It is not one
of the three shared append points (`test/CMakeLists.txt`, `lib/I18n/translations/*.yaml`,
`src/main.cpp`, per `ui-dev.md:26-32`), so the spec's "no shared append point is edited" stays true —
but "nothing for the orchestrator to apply" does not, and the Files-touched table is incomplete.

For completeness: `2026-09-13-berean-os-design.md:214,658` and `ROADMAP.md:73,111` also still record
the open item. Those are roadmap bookkeeping and are reasonably a PR-description concern, but the
spec should say which of them it expects the PR to strike.

**Fix.** Add to the Architecture edit list for `ReturnStack.h`: rewrite `:5-7`'s example so it does
not name a count (e.g. "reproducing a wrap on the device means following citations until the ring
turns over"). Add a fourth bullet to the test-file change list for `ReturnStackTest.cpp:5-7`. Add a
Files-touched row for `.claude/agents/ui-dev.md:44`, and state explicitly whether the design doc /
ROADMAP open-item lines are struck by this PR or left for Phase 2.

---

## MAJOR 4 — "Why this does not need Phase 2" misses the one document that owns `ReturnStack` and rejected this change with a reason

**Claim.** "`ROADMAP.md:73` reads as a gate … and `2026-09-14-phase-2a-input-model.md:34-37` already
decided to keep 3. **Three things answer that.**" The research note it builds on likewise says
"**Three documents** speak to it".

**Problem.** There is a fourth, and it is the design spec that created the class:
`docs/superpowers/specs/2026-09-12-reader-return-stack-design.md`.

```
:20  - **Raising `MAX_FOOTNOTE_DEPTH`.** Three slots stay three slots (decided).
```

and at `:210-215`, under the heading **"The eviction trade-off, stated honestly"**:

> The ring evicts the **oldest** push — the article origin, which is the position the user most
> wants back. At depth ≥ 4 the new behaviour gives three correct one-step returns and then drops out
> of the book … The new behaviour is chosen because every individual Back is correct and
> **depth ≥ 4 without an intervening Back is rare**; it is not a free win.

That last clause is the substantive prior objection to this whole spec — the claim that the chain
depth this change buys headroom for does not occur — and it is the only place in the repo where
keeping 3 was argued rather than merely asserted. The spec's Problem section makes the opposite case
(chain, not fan) without ever citing or rebutting it, so a reviewer reading `:210-215` would
reasonably conclude the spec did not know it existed.

Two knock-ons worth capturing while fixing this:

- `:213` — "then **drops out of the book**" is now stale: `handleBackNavigation`'s gesture guard
  (`ReaderUtils.h:255-257`) landed after that doc and makes the empty-ring swipe inert instead. That
  is *corroboration* for the spec's own central finding, and the spec forgoes it.
- `:20`'s `MAX_FOOTNOTE_DEPTH` is the predecessor of `CAPACITY`, not a second live cap — I confirmed
  `grep -rn MAX_FOOTNOTE_DEPTH src/ lib/ test/` matches documentation only. Worth one line, because
  a reader hitting `:20` and `:30-31` will otherwise wonder whether a separate depth cap still binds
  the chain at 3 independently of `CAPACITY`, which would make this change inert.

**Fix.** Add `2026-09-12-reader-return-stack-design.md:20,210-215` as a fourth item in "Why this does
not need Phase 2", quote the "depth ≥ 4 … is rare" clause, and answer it directly — the honest answer
is that the clause was written when a physical Back still existed in the fork's model, and that
`:213`'s "drops out of the book" escape hatch has since been removed, which is precisely why the
rarity argument no longer carries the failure. Note there too that `MAX_FOOTNOTE_DEPTH` no longer
exists in code.

---

## Minor findings (4)

**MINOR 1 — "red first" cannot be satisfied and the spec knows it.** Testing strategy: "The three
rewrites are **red before green**: each must be run against the unmodified `CAPACITY = 3` header and
fail (or pass, where the property is capacity-independent and the test is merely being
re-expressed)". The research note already proved all three rewrites pass at 3, 8 and 16, and the
acceptance criterion three paragraphs later is "the file compiles and passes at `CAPACITY` = 3, 8
and 16" — i.e. never red. The parenthetical makes the requirement vacuous while the heading keeps
promising a red step, which will cost the implement phase a cycle. The real red state is the one I
reproduced above: the *current* tests fail at 8 and 16. Reword to: "the unmodified tests are the red
— run them against a patched header first and record the 3 failures / 6 assertions; the rewrites are
green at every capacity by construction."

**MINOR 2 — "29 sibling files" is 31.** A2 and the Non-goals lean on the i18n fan-out.
`ls lib/I18n/translations/*.yaml | wc -l` → **32**, of which `english.yaml` is the reference, so 31
siblings. (`grep -cE "^\s*STR_[A-Z0-9_]+:" english.yaml` → 420, so the key count is right.) The error
understates the argument, but A2 is load-bearing for the "no UI" non-goal and the number should be
correct.

**MINOR 3 — human-verification item 4 is configuration-dependent and is stated absolutely.** "With
the ring empty, a left-edge swipe on the reading surface must remain inert (`ReaderUtils.h:255-257`)."
That holds on defaults (`touchReaderControls = TOUCH_READER_ON`, `CrossPointSettings.h:346`), which
is why the Problem section's "on a **default** X4 Pro" qualifier is correct. But with
`TOUCH_READER_SWIPE` (`:233`), `detectTouchPageTurn` maps a left-to-right swipe to
`result.prev` (`ReaderUtils.h:84-92`), consumed at `EpubReaderActivity.cpp:446,595` — the swipe
pages *back* rather than doing nothing, which `ReaderUtils.h:248-250` documents on purpose ("in
swipe page-turn mode a right swipe must page back instead"). A tester with swipe controls on will
report item 4 as a regression that is not one. Add "with touch reader controls left at the default
tap mode" to item 4.

**MINOR 4 — the A6 firmware gate as written yields no number.** "a temporary
`static_assert(sizeof(EpubReaderActivity) < 4096, "");` in `EpubReaderActivity.cpp`, **read off the
failure message**, then removed." A passing `static_assert` emits nothing at all, and a failing one
prints the requirement, not the operand's value. As specified the gate answers only the yes/no
question A6 actually asks — which is fine, but the "read off the failure message" sentence describes
something that cannot happen and should be struck. While editing it, say what a *failure* means:
crossing 4096 does not merely make the number interesting, it flips the whole activity's allocation
from internal SRAM to PSRAM (`heap_caps_malloc_default` prefers SPIRAM above
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`), putting a render-hot-path object on the slow external bus.
`EpubReaderActivity.h` is 180 lines with ~52 data members and no buffers, so this is very unlikely —
but "it does not change the decision either way" in A6 is only true away from that boundary, and the
gate should say "if it fails, stop and escalate" rather than "read the number".

---

## On the assumptions the spec asked me to attack

- **A1 (16).** Honestly labelled, correctly priced. `3 → 16` is +104 B and 136 B total — I confirmed
  both figures exactly on the Xtensa toolchain. The `% CAPACITY` claim is right: 16 being a power of
  two buys nothing and costs nothing. No finding.
- **A2 (no UI).** Sound, modulo MINOR 2. `ui-dev.md:26-32` does name `lib/I18n/translations/*.yaml`
  as report-don't-edit. No finding.
- **A3 (no sticky-oldest).** Sound, and the escalation framing matches `ui-dev.md:12-18`. No finding.
- **A4 (capacity-agnostic tests).** The tautology answer is correct as far as it goes — but MAJOR 1
  is the part A4 missed: the failure mode of a capacity-agnostic sweep is not a tautological
  assertion, it is a surviving literal that quietly stops reaching the boundary.
- **A5 (`static_assert` ceiling).** See MAJOR 2.
- **A6 (budget as internal SRAM).** Conservative and correct as a budgeting choice; the gate wording
  is MINOR 4.
- **A7 (`unpush` stays).** Verified and consistent with `2026-09-12-reader-return-stack-design.md:222-226`
  ("Resolve the target first, push only on success … It is provided anyway for any future caller that
  can fail later"), and with the early return at `EpubReaderActivity.cpp:1701-1704` preceding the push
  at `:1657`. No finding.

---

## Verdict rationale

No BLOCKER. Nothing here reverses a decision, changes scope beyond the two files already in play, or
needs a judgment only the human can make — A1's number survives review, A3's escalation is correctly
scoped, and all four MAJORs are edits to this spec plus, for MAJOR 1, two extra tests in a file the
spec is already rewriting. Fix the four MAJORs and four MINORs inline and proceed.

VERDICT: CLEAR
