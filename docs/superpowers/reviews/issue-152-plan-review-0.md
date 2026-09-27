Tier: heavy

# Issue #152 plan review — pass 0

Plan: `docs/superpowers/plans/2026-09-27-issue-152-plan.md`.
Spec: `docs/superpowers/specs/2026-09-27-issue-152-design.md`.
Reviewed at `667055fa` (base `dc97292e`, still `origin/main`).

## How this was checked

I didn't just read the plan. I ran it literally in a scratch worktree (`git worktree add --detach`
of `HEAD`), applying every code step as an exact-text replacement that asserts its old text occurs
the expected number of times. I also ran both of the plan's Python scripts unchanged. Results:

- **1.1:** `StorageIoTest` fails to compile on `readCallsOn`, as the plan predicts. **1.2:** 68 tests
  pass (the baseline is 67).
- **1.3:** the new test fails with `actual: 60008 vs 600`, as the plan predicts. **1.4:** 69 tests
  pass, including all four `*PastTheReadCap*` tests.
- **Task 2:** all nine `mkdir` lines and all eight include removals match. For each removed include,
  `grep "Storage\.\|sdpaths::"` confirms nothing in the file still needs it.
- **Task 3:** the header replacement matches. The slicing script removes exactly `wasTapInRect`,
  `rowTouch`, `colTouch` and the trailing `getPressedFrontButton` block.
- **4.1 and 4.2:** every replacement matches.
- **4.3:** the red phase produces exactly the 4 predicted failures, with 21 passing.
- **4.4:** the `makeBook` script prints `20`. After it, 25/25 tests pass, and
  `AWorstCaseDocumentFitsTheDerivedBudget` fits exactly. The arithmetic checks out:
  18 + 9 + 10 × (34 + 2 × 224 + 512) = 9,967, and 18 + 9 + 10 × 34 = 367.
- **Task 5:** both check greps give exactly the output the plan states.
- **Task 7:** every old-text string in 7.1–7.5 occurs exactly once. After 7.1,
  `grep "Home screen\|Home ->\|Browse files\|Recent books" USER_GUIDE.md` is empty.
- **Whole host suite** after Tasks 1–5: `ctest` reports 100% of 984 passing.
- **Firmware:** `pio run` for `x4pro` (through the plan's own lock script) ends `SUCCESS`. None of
  the touched files adds a new warning. The one match for the plan's filter is
  `CrossPointWebServerActivity.cpp:204`, which is pre-existing and untouched.
- **Formatting:** `./bin/clang-format-fix -g` leaves the diff unchanged.

## Spec → plan coverage

| Spec requirement | Plan step |
|---|---|
| Item 1, A1–A4: `JsonFileReader` over a 512-byte `BufferedFileReader`, no `readBytes` | 1.4 |
| Item 1 testing: `readCallsOn`, a fake test, a < 600-read test | 1.1–1.3 |
| Item 2, A5–A6: nine `mkdir` lines and their orphaned includes | 2.1, 2.2 |
| Item 3, A7–A8 | 3.1, 3.2 |
| Item 4 table rows 1–13, A9–A11 | 4.1–4.4 |
| Item 4 `docs/file-formats.md` row and A9's recorded reasoning | 4.5 |
| Item 5, A12, including the rewritten launcher sentence (MINOR-3 of the spec review) | 5.1 |
| Item 6, A13: TOC, §4, `:51/:59/:88/:198/:251/:385`, §12 with only the Bible-tile route | 7.1 |
| Items 7, 8, 9a, 9b, A14 (the citation re-read after Task 1) | 7.2–7.5 |
| N4, N5, N7 as PR-body follow-ups; human verification | Task 8 |

Every file a step touches is on a `FILES:` line at column 0, outside any code fence, as a
repo-relative path (plan lines 8–22). This includes `lib/Epub/Epub.{h,cpp}`,
`src/network/PublicationDownloader.cpp`, the reader activities, and both spellings of
`AGENTS.md`/`CLAUDE.md`. None is a glob. Signatures stay consistent from step to step:
`updatePath(old, new)`, `addBook(path, title, author)`, `goHome(bool)`, `onGoHome()` and
`readCallsOn(const std::string&)` are used the same way everywhere they appear. Task 4 is ordered so
every commit compiles (dead callers go first, and red plus green land in one commit). The steps
with no red test (Tasks 2, 3, 4.1, 4.2 and 5) are the pure deletions and signature changes the
spec's testing table exempts.

## Findings

No BLOCKER and no MAJOR findings.

### MINOR-1: the 4.1 pre-check grep reports hits the plan says it won't

- **Claim.** Plan 4.1 (`:356`) says `grep -rn "updateBook\|getDataFromBook" src lib` returns
  "only `src/RecentBooksStore.*`; run it first".
- **Problem.** The pattern also matches `updateBookmarkFlag`. A literal implementer sees extra hits
  and may stop to escalate, which is the behaviour Task 3 asks for in the same situation.
- **Evidence.** The grep also returns `src/activities/reader/EpubReaderActivity.h:134` and
  `EpubReaderActivity.cpp:1254,1781,1872,1878`, all `updateBookmarkFlag`.
- **Fix.** Use `grep -rn "updateBook(\|getDataFromBook" src lib`. That returns only
  `RecentBooksStore.h:37,64` and `RecentBooksStore.cpp:57,110`.

### MINOR-2: the §4 launcher text drops the spec's "and confirm"

- **Claim.** Spec item 6 (`:223`) says: "Tap a tile, or move with **Left**/**Right** and confirm".
- **Problem.** Plan 7.1 (`:767`) writes "Tap a tile to open it; **Left** / **Right** move the
  selection." A reader who drives the launcher with buttons is not told how to open the tile they
  have selected.
- **Evidence.** The launcher activates the selected tile on
  `wasReleased(MappedInputManager::Button::Confirm)` (`LauncherActivity.cpp:600-602`).
  `USER_GUIDE.md:52` already documents Power's short press as configurable to "Confirm".
- **Fix.** Change the sentence to: "Tap a tile to open it, or move the selection with **Left** /
  **Right** and confirm." Alternatively, record why the plan departs from the spec.

### MINOR-3: the 3.2 narrative names the wrong last function

- **Claim.** Plan `:343-344` says that after the script runs, the file "ends with
  `mapDirectionalLabels`'s closing `}`".
- **Problem.** The file actually ends with `mapFrontLabels`. The script itself is correct. Only the
  description is wrong, but an implementer who checks against it will see a mismatch.
- **Evidence.** In the scratch run, the tail of `src/MappedInputManager.cpp` after the script is
  `MappedInputManager::Labels MappedInputManager::mapFrontLabels(...) { ... }` (at `dc97292e` it was
  followed by the `// The raw front-button scan…` block, `:378-404`).
- **Fix.** Replace `mapDirectionalLabels` with `mapFrontLabels` in that sentence.

## Verdict

The plan is complete against the spec. Its anchors are exact at `dc97292e`, its red and green
predictions come true when run literally, and following it produces a tree that passes the host
suite and builds as firmware. The three MINORs are wording fixes that can be made inline.

VERDICT: CLEAR
