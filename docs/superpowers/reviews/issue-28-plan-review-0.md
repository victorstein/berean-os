# Adversarial review — issue #28 implementation plan, pass 0

**Target:** `docs/superpowers/plans/2026-09-16-issue-28-plan.md`
**Against:** `docs/superpowers/specs/2026-09-16-issue-28-design.md` (cleared spec passes 0 and 1)
**Date:** 2026-09-16
**Counts:** 0 BLOCKERs, 2 MAJORs, 6 MINORs

## What was checked, and how

Every identifier the plan writes was traced to the file it has to compile against:
`src/util/BookmarkFile.{h,cpp}`, `src/BookmarkEntry.h`, `src/util/HighlightFileAction.h`,
`src/util/HighlightFile.{h,cpp}`, `lib/Serialization/{SaveBudget.h,PersistableStore.h}`,
`lib/hal/HalStorage.h`, `lib/Utf8/Utf8.h`, `src/activities/reader/EpubReaderActivity.{h,cpp}`,
`src/activities/reader/EpubReaderBookmarksActivity.cpp`, `src/activities/reader/ReaderUtils.h`,
`src/activities/ActivityResult.h`, `test/CMakeLists.txt` and the two donor suites.

Three claims were executed rather than reasoned about:

- The host suite was configured from `test/` into a scratch dir and `HighlightDocTest` /
  `HighlightFileActionTest` were built, confirming the plan's per-suite binary path convention
  (`build/test/<subdir>/<Target>`, used in the plan's iteration command at `plan:26`).
- The plan's `BookmarkDoc::toJson` (`plan:369-383` + the `"v"` stamp at `plan:485`) was compiled
  verbatim against the ArduinoJson v7.4.2 that `test/CMakeLists.txt:31` pins, and fed the plan's own
  step-5 fixtures. Numbers below are measured, not estimated.
- `utf8SafeSummary` was run on the step-4 fixture (40 × U+4E16): returns 72 bytes, `% 3 == 0`. Step
  4's assertions hold.

Every anchor the plan quotes for a search-and-replace matches the working tree byte for byte:
`EpubReaderActivity.h:46`, `EpubReaderActivity.cpp:496-502, 526-532, 1290-1292, 1747-1748,
1751-1804`, `EpubReaderBookmarksActivity.cpp:8-10, 33-35, 169-177`. `grep -rn "BookmarkFile::"`
returns exactly the four call sites the spec names, and step 6c covers the three that stop compiling.
The `test/CMakeLists.txt` append points (`:110`, then after the new line) are unambiguous. All six
`test/bookmark_save_action/` items in spec `:461-471` map to a test in step 1.

---

## MAJOR 1 — `MAX_RECORD_BYTES = 600` sits 214 bytes above the worst case it measures, so the only regression it claims to guard cannot trip it

**Claim.** `plan:633-637` introduces the constant with: *"Ceiling for one serialised record, pinned by
BookmarkDocTest. Adding a field to `BookmarkEntry` lowers how many bookmarks fit in
`SAVE_BYTE_BUDGET`; this makes that show up as a failing test rather than a quieter ceiling."* The
spec assigns it the same single job: *"A new field on `BookmarkEntry` fails here first, which is the
one thing this suite can usefully guard"* (`spec:484`).

**Problem.** The plan's own worst-case fixture measures **386 bytes**. A new `BookmarkEntry` field
costs on the order of 8–20 serialised bytes (`,"vo":41233` — the widest existing optional field — is
11). `EXPECT_LT(386, 600)` cannot be made to fail by any plausible field addition; roughly eighteen
new `vo`-sized fields would be needed. The assertion is a no-op, which is precisely the failure mode
spec pass 1's MAJOR 1 identified for the worst-case-*document* test and which caused the spec to
replace it with this per-record one (`spec:39`). The plan reintroduces the defect at a different
scale.

**Evidence.** Compiled `plan:369-383` plus the `doc["v"] = FORMAT_VERSION;` stamp from `plan:485`
against ArduinoJson v7.4.2 (`test/CMakeLists.txt:28-32`), driven by the step-5 fixture at
`plan:585-593` exactly as written — `"/body/DocFragment[1189]/body"` + 8 × `"/div[4127]"` +
`"/p[2718]/span[2]/text()[3].24816"`, a 72-byte all-`"` summary, `vo` present:

```
worst-case oneRecord (whole doc) = 386
typical perRecord                = 174   (45000/174 = 258)
worst-case incremental perRecord = 365
```

A second, smaller defect rides along: `oneRecord` is `measureJson(doc)` (`plan:593`), i.e. the whole
document — record **plus** the 22-byte `{"v":1,"bookmarks":[…]}` wrapper. The variable name, the
constant's name and its comment all say "one record"; what is asserted is one record plus a wrapper.

**Concrete fix.** In `plan:636`, replace the value and tighten the comment:

```cpp
// Ceiling for one serialised record plus the document wrapper, pinned by
// BookmarkDocTest. The worst case measures 386 bytes against ArduinoJson
// v7.4.2; the ~9% headroom absorbs a serialiser formatting change, not a new
// field. Adding a field to BookmarkEntry lowers how many bookmarks fit in
// SAVE_BYTE_BUDGET, and must fail here first.
inline constexpr size_t MAX_RECORD_BYTES = 420;
```

and rename `oneRecord` → `oneRecordDoc` at `plan:593, 595-596`. If the implementer would rather not
take a number on trust, add to step 5a: *"first pin `MAX_RECORD_BYTES = 1`, run the suite, read the
measured value out of the failure message, and set the ceiling to that value plus ~10%."* Either way
the step must end with a ceiling within ~40 bytes of the measurement, or it has not done the job the
spec gave it.

---

## MAJOR 2 — spec testing item 4 (A-5: an over-budget document still loads in full) has no step

**Claim.** `spec:480`: *"4. `fromJson` on an over-budget document keeps every entry and reports
success (A-5)."*

**Problem.** No step writes it. The plan's `test/bookmark_doc/` ends up with exactly seven tests —
`RoundTripsEveryField`, `FromJsonClearsTheTargetFirst`, `RejectsANonObject` (step 2), the four
version tests (step 3), the summary test (step 4), the two budget tests (step 5) — and none of them
constructs a document over `SAVE_BYTE_BUDGET`. Spec items 1, 2, 3, 5, 6 and 7 each map to a test;
item 4 does not.

This is not a cosmetic gap. A-5 is the one place `BookmarkDoc` deliberately diverges from its donor:
`PassageDoc::fromJson` ends `return measureBytes() <= SAVE_BYTE_BUDGET;`
(`lib/StudyStore/StudyStore/PassageDoc.cpp:131`), folding "too big" into the same failure as
"unreadable". `BookmarkDoc` must not, because `Failed` latches saving off for the session
(`plan:951-956`) and would freeze exactly the 45,000–50,000-byte legacy file that the whole shrink
exception (step 1, `spec:212-230`) exists to rescue. The divergence is implemented **by omission** —
`fromJson` simply never measures — and an omission has nothing to regress against. The next person
who copies `PassageDoc` into this file gets a green suite and a dead recovery path.

**Evidence.** `plan:202-278, 428-473, 515-541, 580-622` enumerate every test the plan writes;
`spec:473-488` enumerates the seven required. `plan:353-357` asserts the contract in a comment —
*"Returns false only when the document is not an object or carries a version this build does not know
— NEVER for size"* — that nothing executes.

**Concrete fix.** Append to step 4's test block (it already has `makeEntry` and the version stamp in
scope) — sized from the measured 174 bytes/typical record, so 300 records clears both the 45,000-byte
budget and the 50,000-byte read cap:

```cpp
TEST(BookmarkDocBudget, AnOverBudgetDocumentLoadsInFullRatherThanFailing) {
  // A-5: too big to write back is NOT the same as unreadable. Dropping the
  // tail here, or returning false, would latch saving off and freeze the file
  // the shrink exception exists to rescue.
  std::vector<BookmarkEntry> many;
  many.reserve(300);
  for (int i = 0; i < 300; ++i) {
    many.push_back(makeEntry("/body/DocFragment[27]/body/div[1]/p[14]/text()[1].117",
                             "In the beginning God created the heavens", true));
  }
  JsonDocument doc;
  BookmarkDoc::toJson(many, doc);
  ASSERT_GT(measureJson(doc), persist::SD_READ_TRUNCATION_CAP) << "fixture must be over the read cap";

  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
  EXPECT_EQ(out.size(), 300u) << "not one entry may be dropped for size";
}
```

That needs `#include <SaveBudget.h>` in the test and `${REPO_ROOT}/lib/Serialization` added to
`target_include_directories` in `plan:289-292` (the pattern `test/highlight_file/CMakeLists.txt:9-12`
already uses). If MINOR 1 below is applied, both are needed anyway.

---

## MINOR 1 — A-1 is implemented as a bare `45000`, not `persist::DEFAULT_SAVE_BUDGET`

`spec:110` and `spec:186` both state the decision as *"`SAVE_BYTE_BUDGET = persist::DEFAULT_SAVE_BUDGET`
(45,000)"*, with `src/study/TagPaletteFile.cpp:70` named as the store that uses the constant directly
and `src/util/HighlightFile.h:38-42` named as the one that merely *"arrives at the same 45,000 by
hardcoding it"*. `plan:345` hardcodes it and cites the hardcoding store as its model. Behaviour is
identical today; the cost is that the shared constant at `lib/Serialization/SaveBudget.h:23` can move
without bookmarks following, which is the whole reason it exists. Fix: `plan:345` becomes

```cpp
inline constexpr size_t SAVE_BYTE_BUDGET = persist::DEFAULT_SAVE_BUDGET;
```

with `#include <SaveBudget.h>` added to `BookmarkDoc.h` (`plan:320`) and
`${REPO_ROOT}/lib/Serialization` added to `plan:289-292`. Step 1's test may keep its local
`BUDGET = 45000` — that suite deliberately has no ArduinoJson dependency.

## MINOR 2 — step 5's budget assertion is `>= 200`, where the spec asks for the ≈218 the decision rests on

`spec:485-486`: *"`SAVE_BYTE_BUDGET / bytesPerRecord` is still at least the ≈218 records §Budget
quotes. Dropping the record cap rests on that number and nothing else tests it."* `plan:618` asserts
`>= 200u`. The measured figure is 258, so `>= 218` passes today with 40 records of margin and is the
number A-2 (`spec:196-210`) actually claims. Fix: change `200u` to `218u` at `plan:618` and drop the
now-stale "TwoHundred" from the test name (`plan:600`) in favour of e.g.
`TheBudgetStillHoldsTheRecordCountTheNoCapDecisionRestsOn`.

## MINOR 3 — step 2 commits a header whose comments describe behaviour that does not land until steps 3 and 4

`plan:339-340` (*"A file written before versioning carries no `v` and is read as version 1"*),
`plan:348-349` (*"the load path re-bounds it"*) and `plan:355-357` (*"carries a version this build
does not know"*) are all true only after steps 3 and 4. Each step is meant to leave a committable
tree, and step 2's commit message (`plan:418`) ships documentation that the code contradicts. Fix:
either move those three comments into steps 3b and 4b alongside the code they describe, or add one
line to step 2's preamble stating that the header is written for the merged state (which is the
repo's stated comment convention, `CLAUDE.md` §Comments) and that steps 3 and 4 complete it.

## MINOR 4 — `existingFileSize` logs a misleading SD failure on the common over-budget path

`plan:722-726` calls `Storage.openFileForRead(MODULE, path, file)` and treats `false` as "no file".
That path prints `"[…] [BKM] Failed to open file for reading: <path>"` from raw Serial
(`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:319-324`, reached through
`lib/hal/HalStorage.cpp:100-110`). On the first over-budget save against a book with no bookmark file
yet, the serial log therefore shows an SD-failure line immediately before the real
`"Bookmarks for %s measure %u bytes; not written"` — which is exactly the log a tester will be
reading while verifying hand-back item 3. Fix: gate the open, so absence is silent and only a real
open failure logs:

```cpp
size_t existingFileSize(const std::string& path) {
  if (!Storage.exists(path.c_str())) return 0;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return 0;
  return file.size();
}
```

## MINOR 5 — step 9b re-runs `pio run` after a formatting-only change, which the repo forbids

`plan:1219-1224` runs `~/.platformio/penv/bin/pio run` after step 9a's `clang-format-fix`. `CLAUDE.md`
§Testing checklist item 1 is explicit: *"build once after the last code edit … Do not clean by
default, repeat a target that already passed, or rebuild after formatting, comment-only or
documentation-only changes."* Step 8d (`plan:1202`) already built the final code. Fix: drop the
`pio run` from 9b, keep the `ctest` line.

Related, same step: `plan:1238-1241` issues an unconditional `git commit -m "style: …"` after
`git add -A src test`. `git commit` exits 1 when there is nothing staged, which is the expected
outcome if steps 1–8 were already formatted. Fix: make it conditional —
`git diff --cached --quiet || git commit -m "style: clang-format the bookmark save-budget work"` —
and note that `git add -A src test` deliberately does not stage any file the whole-tree format pass
may have touched outside `src/` and `test/`, so `git status --short` must come back clean at 9c
before the branch is handed over.

## MINOR 6 — three prose/code mismatches that a literal implementer has to reconcile

- `plan:653` says *"The API change breaks both call sites"*; `plan:813` says *"Fix the three call
  sites"*. Three is correct (`EpubReaderActivity.cpp:1801`, `EpubReaderBookmarksActivity.cpp:33` and
  `:175`); the fourth, `EpubReaderActivity.cpp:1747`, discards the result and still compiles.
- `plan:1116-1117` says *"Add `ReaderUtils.h` after the `BookmarkFile.h` include"*, but the code block
  beneath it (`plan:1119-1124`) correctly places it after `MappedInputManager.h`, which is where
  `.clang-format`'s `SortIncludes` (`.clang-format:269-271`) will put it regardless. Delete the prose.
- `plan:1158` says *"Replace its first six lines"*; the quoted block is nine
  (`EpubReaderBookmarksActivity.cpp:169-177`). The quoted block disambiguates, but the count should
  match.

---

## What holds up, and is worth saying so

- **Step 1 is exhaustive and correct against `bookmarkSaveAction`.** All six spec items
  (`spec:461-471`) map to an assertion, including the equal-is-not-shrinking case (`plan:82`) and the
  at-the-cap boundary with the spec's own note that the shrink arm is unreachable there (`plan:107-112`,
  mirroring `spec:468-471`).
- **Step 6's `load()` is a faithful structural copy of `HighlightFile::load`
  (`src/util/HighlightFile.cpp:23-70`)**, including promoting the `.tmp` *before* validating its
  contents and the post-switch `return LoadResult::Failed` that keeps `-Wswitch` quiet. Every HAL call
  it makes exists with the signature used (`HalStorage.h:35-41`, `:77`).
- **Step 7e's rollback is right.** Erased entries are collected in ascending original index order
  before the erase (`plan:1036-1042`) and re-inserted in that same order (`plan:1086-1089`), which
  restores the original positions exactly; `currentPageBookmarked = wasBookmarked` (`plan:1093`)
  restores the flag to the state the card still holds.
- **`bookmarkRemoved` is fully retired.** Its only reader (`EpubReaderActivity.cpp:1291`) and both
  writers (`:1773`, `:1797`) are inside blocks steps 7b and 7e replace wholesale.
- **A-9 is honoured.** Step 8b sets `isCancelled` before `finish()` (`plan:1147-1151`), matching the
  three existing exits at `EpubReaderBookmarksActivity.cpp:91-92, 136-137, 187-188` and avoiding the
  `std::get<ProgressChangeResult>` on `monostate` that `spec:374-392` traced.
- **The build/test plumbing is sound.** `${REPO_ROOT}/src` on the include path resolves
  `"util/BookmarkSaveAction.h"` and `"util/BookmarkDoc.h"`; `crosspoint_test_common` supplies
  `${REPO_ROOT}` and `${REPO_ROOT}/lib`; `BookmarkDoc.h`'s `"../BookmarkEntry.h"` resolves relative to
  `src/util/`. The per-suite binary path `build/test/bookmark_doc/BookmarkDocTest` (`plan:26`) matches
  what CMake actually produces — verified by building the two donor suites.
- **Steps 6–8 having no failing test first is not a finding.** `spec:506-510` states the reason
  (`BookmarkFile.cpp` reaches `Arduino.h` through `PersistableStore.h`) and the plan restates it at
  `plan:8-10` and `plan:651-652` rather than pretending otherwise. The hand-back (`plan:1259-1275`)
  correctly refuses to claim any of the device behaviour.

---

## Verdict reasoning

Both MAJORs are mechanical and inline-fixable: MAJOR 1 is a constant whose correct value is measured
above; MAJOR 2 is a ~18-line test plus two build-file lines. Neither reverses a spec decision, changes
scope, nor needs a judgment only the human can make — the plan's *decisions* all track the spec; it is
two of its *implementations of the spec's test list* that fall short. No BLOCKER-level correctness
defect was found in steps 1–8 after tracing every quoted anchor and every identifier to the tree.

Fix MAJORs 1–2 and MINORs 1–6 inline, then proceed.

VERDICT: CLEAR
