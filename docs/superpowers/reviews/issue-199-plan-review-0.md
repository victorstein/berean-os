Tier: standard

# Plan review 0 — issue #199 (square 7×10 chapter grid with tag and bookmark markers)

Plan: `docs/superpowers/plans/2026-09-30-issue-199-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-199-design.md`
Read at `2ebb1b24`.

## How this was checked

- **Host tests run.** Every host-test code block in the plan was applied to a scratch export of
  `HEAD`: Task 1's `BookGridLayoutTest` edits and `BookGridLayout.h` change, Task 2's
  replacement `NumberGridLayoutTest.cpp` and cap change, Task 3's appended tests and
  `gridRect`/`cellRect`, and Task 4's `GridMarks.h`, `GridMarksTest.cpp` and CMake block. I then
  ran the plan's own configure/build/ctest commands. All four executables compile with no
  warnings from project code, and **81/81 tests pass**.
- **Task 2's red state reproduced.** With the cap put back to 48 against the Task 2 test file,
  exactly the three tests plan:352-354 names fail (`PortraitBodyIsSevenByTen`,
  `PreCompactBodyIsSevenByTenWithoutTheClamp`, `TallPortraitBodiesStayAtTenRowsAboveTheTapFloor`).
  `RowClampFiresInsteadOfOverflowing` passes at both caps, as plan:354-355 says. The red states
  for Task 1 and Task 3 are compile failures on missing members (`BookGrid::MAX_CELLS`,
  `gridRect`/`cellRect`), and Task 4's is a missing header. All three follow directly from the
  code.
- **`old` snippets match the source.** Every "replace X" snippet in Tasks 1, 2 and 5-8 matches
  the current source verbatim: `BookGridLayoutTest.cpp:70-85`, `BookGridLayout.h:91`,
  `NumberGridLayout.h:15-19`, `UiAppHost.h:29-35`, `ReaderBookmarks.h:39`,
  `BibleNavigationActivity.cpp:33-37,50-54,112-113,136-140,143-144,153-155,486-490` and
  `EpubReaderActivity.cpp:724`. The insertion anchors named in Tasks 5-7 all exist
  (`entrySpine`, `verseSpine`, `CELL_LABEL_BYTES`, `buildGrid` decl, the `UITheme.h`/`UIScale.h`/
  `Memory.h` includes).
- **Firmware-only APIs match the source.** I checked every call Tasks 5-8 make against the
  source; none were compiled.
  - `Frame::stateFor(ActionId, int16_t, State)` (`FreeInkUICore.h:1423`), `target()`
    (`:1411`), `DrawTarget::bitmap(Rect, BitmapRef, BitmapMode, Paint)` (`:708`), `text`
    (`:705`) and `lineHeight` (`:689`).
  - `BoxStyle::foreground` (`:581`), `ThemeTokens::smallText` (`:661`), and `Screen::frame()`,
    `theme()` and `body()` (`FreeInkApp.h`).
  - BW1 set-bit-is-ink with white ink when `foreground.color == White`
    (`FreeInkUIGfxRenderer.h:197-209`). This matches `BaseTheme.cpp:34-41`'s MSB-first,
    2-bytes-per-row crop of `BookmarkStatusIcon`.
  - `theme().key` is populated (`FreeInkUI.cpp:100`), so `props.keyStyles.resolve` resolves
    the same styles `keyGrid` uses (`key-grid.h:73`).
  - `STUDY` (`StudyStore.h:203`), `TaggedPassage::start/end` (`TaggedPassage.h:32-33`) and the
    `BookmarkEntry` field types (`src/BookmarkEntry.h:11,18-19`) all match
    `BookmarkPosition{uint16_t,bool,uint32_t}` with no narrowing.
- **Threading (A7).** `loadChapters` is only called from `enterAtPosition` (`.cpp:170`) and the
  book-level branch of `activateIndex` (`.cpp:270`). `loadVerses` is only called from
  `openVerseList`, which is reached from the book or chapter level (`.cpp:267,278`). In every
  case the level whose bits are being cleared and refilled is not the one on screen. `markVerses`
  reads `selectedBook`, which is set before `openVerseList` on both paths (`.cpp:263`; the
  chapter level is only reachable with it set, `.cpp:179`).
- **Spec coverage.** Every spec requirement maps to a step:
  - A1: Task 2.
  - A2, A5: Task 2's tests.
  - A3: Task 1.
  - A4: Tasks 3 and 7.
  - A6, A8, A9 mapping: Task 4.
  - A9 hand-off: Task 5.
  - A7: Task 6.
  - A10: Task 7.
  - §5 memory log: Task 8.
  - §9 device list: Task 9.

  Every §9 host-test bullet has a corresponding test. The spec's non-goals (book-level dot, book
  layout, SDK, on-disk formats) are untouched.
- **`FILES:` lines.** Every file any step touches appears on a column-0 `FILES:` line outside a
  code block (plan:7-13). All are repo-relative paths, with no globs. `src/BookmarkEntry.h`,
  `components/icons/bookmark.h`, `StudyStore.h` and `<esp_heap_caps.h>` are only included,
  never edited. No file is touched that is missing from the lock.
- **Firmware-only tasks without a failing test.** Tasks 5-8 have no failing test of their own.
  This is acceptable here: the logic they wire up is the pure `GridMarks`/`NumberGrid`
  arithmetic, which Tasks 3-4 test. What remains is constructor copying, two 10-line glue
  methods and drawing, which only the build and the device check can verify. Each of those tasks
  still ends buildable and committable.

## Findings

No BLOCKER or MAJOR findings.

**MINOR 1 — Step 5.5's check command is an invalid regex.**
- Claim: plan:1076 says `rg -n "BibleNavigationActivity>(" src` "must show only this call site".
- Problem: ripgrep reads the pattern as a regex, and the unescaped `(` opens a group that never
  closes. The command exits with an error and lists nothing, so an implementer following the
  plan literally cannot complete the check.
- Evidence: running it in this worktree prints
  `rg: regex parse error: (?:BibleNavigationActivity>() ... unclosed group`.
- Fix: use `rg -nF "BibleNavigationActivity>(" src`, or `rg -n "BibleNavigationActivity>\(" src`.
  Today the correct result is the single hit at `EpubReaderActivity.cpp:724`.

**MINOR 2 — The plan renames and re-signs spec §5's `NumberGrid` helpers without recording it.**
- Claim: spec §5 (`design.md:222-223`) and A4/A10 (`:71`, `:169`) define three helpers:
  - `struct GridRect { int x, y, width, height; }`
  - `gridRect(bodyW, bodyH, geometry, gap)`, returning an offset
  - `cellRect(grid rect, cell, gap, cols, index)`
- Problem: the plan instead defines:
  - `struct Box`
  - `gridRect(bodyX, bodyY, bodyW, bodyH, geometry, gap)`
  - `cellRect(grid, geometry, index, gap)`

  These appear at plan:518-545 and are used that way at plan:1255,1282,1318. The plan's own
  versions are internally consistent and arguably better, since they return an absolute rect and
  take the geometry. But the spec gives `GridMarks`, and only `GridMarks`, "exact signatures are
  the plan's to settle" (`design.md:217`). The plan does record its other departure from the spec
  (the `BookmarkEntry.h` path, plan:28-29), so a reader comparing the spec with the code will
  find an unexplained mismatch here.
- Fix: add a line to "Ground rules for the implementer" noting that `NumberGrid::Box` stands in
  for the spec's `GridRect`, and that `gridRect`/`cellRect` take the body origin and the
  `Geometry` respectively. No code change.

**MINOR 3 — Task 9's PR-body instruction conflicts with the session attribution rule.**
- Claim: plan:1421 says the PR body "must end with `Closes #199`".
- Problem: this session's attribution rule requires PR descriptions to end with the
  `https://claude.ai/code/session_…` link. Both lines cannot come last, and an implementer
  following the plan literally will drop one of them.
- Evidence: plan:1421 against the session's attribution rule.
- Fix: "PR body contains `Closes #199` and ends with the session link."

VERDICT: CLEAR
