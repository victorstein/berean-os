# PR #73 — code quality review, pass 0

Reviewed: PR #73, `feature/34-return-stack-capacity` → `main`, 11 files.
Scope: **code quality only** — is this written the way the codebase is already
written? Intent, scope and acceptance are stage 1
(`docs/superpowers/reviews/issue-34-pr-review-intent-0.md`, CLEAR) and are not
re-litigated here.

Non-doc surface is two files: `src/activities/reader/ReturnStack.h` (+12/-3) and
`test/return_stack/ReturnStackTest.cpp` (rewritten bounds).

---

## Verdict summary

No blockers, no majors. The change extends the patterns already in the header and
its suite rather than inventing a parallel one: one constant, one `static_assert`
in the repo's established `sizeof`-pinning form, and test helpers that grow the
file's existing anonymous-namespace idiom. The rewritten suite is load-bearing —
I killed it with three independent mutants at `CAPACITY = 16`. **2 MINORs**, both
documentation-accuracy, both fixable inline.

## What I ran

| Check | Result |
|---|---|
| Host suite at the committed capacity | `[  PASSED  ] 13 tests.` (built into `/private/tmp/claude-501/qrev34`, never in `test/build/`) |
| Mutant `oldest()` → `&slots_[0]` | 3 tests fail |
| Mutant `pop()` index-by-count (`out = slots_[count_]`) | 4 tests fail |
| Mutant unbounded `count_++` (clamp removed) | 3 tests fail |
| `.clang-format` conformance of both changed files | Unchanged by the repo style (checked with the wrapper's own pinned clang-format 21.1.8 in `--dry-run --Werror`, so the tree was not mutated; CI's whole-tree `./bin/clang-format-fix` is the gate and is green) |

No `pio run`: nothing below depends on a firmware build, and the branch is green
in CI.

---

## MINOR 1 — the `static_assert` message cites a doc line that this PR's own last commit moved

`src/activities/reader/ReturnStack.h:61-64`:

```cpp
static_assert(sizeof(SavedPosition) == 8,
              "ReturnStack budgets CAPACITY * sizeof(SavedPosition) of internal SRAM -- widening SavedPosition "
              "(Unit addressing, 2026-09-13-berean-os-design.md:511) multiplies by CAPACITY, so decide the "
              "capacity again when this trips");
```

The citation was right when it landed and is wrong now:

- at `55f8c555` (the commit that added the assert), `design.md:511` was
  ``` `ReturnStack`'s `SavedPosition` is `(spineIndex, pageNumber)` — file-relative, the ``` — the intended paragraph;
- `f71f921d` ("apply PR intent review pass 0 minors") added two net lines at
  `design.md:211-220`, so at HEAD line 511 is "dedupe across books into the
  global store." and the intended paragraph is `design.md:513`.

So the PR invalidated its own source comment inside the PR, which is precisely
the failure mode the spec's *Comments — three that go false* section was written
to prevent. It is a MINOR because nothing executes on it.

It is also the only file:line-into-a-doc reference in this codebase's C++. Every
sibling cites a document by name and lets the reader grep:
`lib/hal/HalStorage.cpp:70` ("issue #518 and the HAL note in CLAUDE.md"),
`lib/StudyStore/StudyStore/MigrationPlanner.h:40`,
`lib/StudyStore/StudyStore/UnitIndexFormat.h:32`,
`src/study/MigrationProgress.h:9`.

Fix inline: drop `:511`. The section label plus the file name is enough to find
it, and the anchor text is greppable, so the reference stops drifting every time
the design doc is edited.

Note what is *not* a finding here: the length of the message. A multi-line
explanatory `static_assert` string with the same `--` aside is already house style
(`src/RecentBooksStore.cpp:125-130`), and pinning a struct's size this way mirrors
`src/network/OtaBootSwitch.h:25`, `lib/EpdFont/SdCardFont.h:179` and
`lib/EpdFont/SdCardFont.cpp:14-17`. Only the line number is wrong.

## MINOR 2 — `ROADMAP.md:72-74` now asks Phase 2 to decide something the same sentence says was decided

```
Two decisions this phase has to make first: the `ReturnStack` capacity, raised to 16 in #73 but
still silently evicting, before it becomes the primary Back; and portrait-only, which follows from a
fixed Left/Right mapping.
```

The list frame ("two decisions this phase has to make first") no longer matches
its first item: the capacity is settled, and what is open is the silent eviction /
visible-affordance half. The design doc got this right in the same commit —
`docs/superpowers/specs/2026-09-13-berean-os-design.md:214-220` ("the first landed
in #73 … The affordance is still open") and `:660-662` ("The capacity half is
settled") — and `ROADMAP.md:111` also got it right. Only this one sentence keeps
the old frame around a new fact.

Fix inline, e.g. make the first item the open half: "whether an evicted return
still needs a visible affordance now that the capacity is 16 (#73)". Editorial,
one phrase, no decision content changes.

---

## Checked and clear

- **One way to do it, not two.** Capacity is still the single
  `ReturnStack::CAPACITY` (`ReturnStack.h:19`); `push`/`pop`/`unpush`/`clear`/
  `count`/`oldest` are byte-for-byte unchanged, and no call site learned a
  capacity: `EpubReaderActivity.cpp:160, 547, 560, 1653, 1657, 1713` all read
  `count()`/`oldest()`/`pop()` only. No second constant, no `MAX_*` alias, no
  build flag.
- **Test helpers extend the file's idiom.** `pushRange` and `expectPopsDownTo`
  (`ReturnStackTest.cpp:28-34`) sit in the same anonymous namespace as the
  existing `at`, `popped` and `expectPosition`, and are the same shape. No
  fixture class, no `TEST_P` parameterisation — neither of which this repo's
  host suites use. `constexpr int PUSHES` (`:178`) matches the local-constant
  naming in `test/number_grid/NumberGridLayoutTest.cpp:15-20` and
  `test/bookmark_save_action/BookmarkSaveActionTest.cpp:14-15`.
- **The suite tests behaviour, not the implementation.** Names state the
  guarantee (`OnePastCapacityEvictsTheOldestAndPopsStayOneStepBack`,
  `OldestIsThePhysicallyOldestEntryNotSlotZero`), assertions carry failure
  messages (`:64, :76, :88`), and the three mutants above each kill a different,
  meaningful subset. The literal-3 and literal-2 counts that remain (`:57`,
  `:171`) are in the non-boundary tests, and the header comment (`:5-10`) now
  scopes its claim to wrap-boundary bounds, so it no longer overclaims.
  `static_assert(PUSHES > ReturnStack::CAPACITY * 2, …)` (`:179`) is the right
  way to keep `SurvivesRepeatedWrapping` honest if the constant moves again.
- **Comments are "why", not narration.** The three touched comments
  (`ReturnStack.h:3-7`, `:15-18`, `ReturnStackTest.cpp:5-10`) were re-stated
  without a hard-coded count rather than left to rot, and none of them paraphrase
  the next line. No dead code, no commented-out code, nothing left behind from
  the capacity sweep the plan ran.
- **Resource justification is satisfied.** `ReturnStack` is 136 bytes and is a
  by-value member (`EpubReaderActivity.h:78`) of an activity built with
  `makeUniqueNoThrow` (`ReaderActivity.cpp:29`), so the extra 104 bytes are heap,
  not a stack local — CLAUDE.md's 256-byte local rule is not in play, and no new
  allocation was introduced.
- **Error handling:** no new failure paths, so nothing to match. `pop()` still
  returns `false` and leaves `out` alone; `unpush()` still clamps.
- **Docs edits follow house convention.** Citing a PR number inline in a living
  doc is already the pattern
  (`docs/superpowers/specs/2026-09-13-berean-os-design.md:139`, "relabelled to
  'Ajustes' in #53"), and the research → spec → plan → review trail matches the
  contents of `docs/superpowers/*`.

**Deliberately not raised, recorded so pass 1 does not re-derive it:**

- `docs/superpowers/plans/2026-09-14-phase-2a-input-model.md:34-37` and `:620-621`
  still say "`ReturnStack` keeps `CAPACITY = 3`" because "the tests are not
  parameterised" — both halves now false. I am not calling it a finding: dated
  plans in this repo are records of what was decided then, not living guidance,
  and the same plan already disagrees with `ROADMAP.md:66` about the chord Back
  without anyone reconciling them. Updating it would be a new convention, not
  conformance to one.
- `ClearEmptiesAWrappedRing` still cannot observe `clear()`'s `top_ = 0`. The
  plan (`…plan.md:44-72`) and the intent review both state this up front; the
  rewrite is hygiene, and re-arguing it is stage-1 ground.

Minors: 2 (MINOR 1 and MINOR 2 above), both documentation-accuracy and both
fixable inline without touching behaviour or tests.

VERDICT: CLEAR
