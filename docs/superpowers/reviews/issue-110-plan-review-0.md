Tier: heavy

# Plan review 0 — issue #110 (bookmark and auto-turn extractions)

Reviewed: `docs/superpowers/plans/2026-09-27-issue-110-plan.md` against
`docs/superpowers/specs/2026-09-27-issue-110-design.md` and the code at HEAD `1df7d841`.
`git diff 9a307341 --stat -- src test` is empty, so every line anchor the plan quotes against
`9a307341` still holds.

## What I verified and found correct

- **Every quoted replacement anchor exists verbatim.** Header block `EpubReaderActivity.h:41-59`
  and methods `:131-134` match 2.3b/2.3c character for character. In the `.cpp`: includes `:25`
  and `:54`; `loadCachedBookmarks();` at `:244` and `:717`; `!cachedBookmarks.empty(), hasHighlights,
  isBible),` at `:317`; the comment phrase at `:382` (on one line, so the plain replace works);
  toast expiry `:521-524`; `        if (!showBookmarkMessage) {` at `:557`; `toggleAutoPageTurn`
  `:916-923`; `if ((millis() - lastPageTurnTime) >= pageTurnDuration) {` at `:514`;
  `updateBookmarkFlag();` at `:1271`; toast draw `:1318-1320`; status bar `:1660`, `:1683`.
- **The 4.2d count is right.** `automaticPageTurnActive = false;` occurs at `:499, :917, :1033,
  :1258, :1267, :1277, :1323`; step 4.2b removes `:917`, leaving exactly the 6 the plan expects.
  `if (automaticPageTurnActive) {` is at `:495` and `:1659`; `  if (automaticPageTurnActive &&`
  is at `:1052`.
- **The moved bodies are faithful.** I diffed plan 2.2 against `EpubReaderActivity.cpp:1765-1905`.
  The only differences are the listed renames, the dropped `requestUpdate()` calls (`:1810`,
  `:1875`, `:1892`, which the new activity `addBookmark` restores as one trailing call on every
  path past the guard, as A4 requires), the guard/arm/lock/progress split that A5 specifies, and
  the `saveDisabled_` comment reworded from "this activity" to "the owning activity". The rollback's
  `refreshPageFlag(epub, &section, spineIndex)` reads the same live `section->currentPage` and spine
  that `updateBookmarkFlag()` read.
- **The APIs exist with the constness the signatures need.** `ReaderUtils::showMessage(const
  GfxRenderer&, const char*)` (`ReaderUtils.h:233`), so `load`'s `const GfxRenderer&` compiles.
  `Section::estimatedTotalPages() const` (`Section.h:106`) and `getVisibleTextOffsetForPage(...)
  const` (`:162`) work through `const Section*`. `getTextFromSectionFile()` is non-const (`:121`),
  so `toggle` needs `Section&`, which it takes. `Epub::calculateProgress(...) const` and
  `getPath() const` are at `Epub.h:81, 56`. `SavedProgressPosition` is in
  `lib/ProgressMapper/ProgressMapper.h:28`. `BOOKMARK_MESSAGE_DURATION_MS` is at
  `ReaderUtils.h:21`. `GfxRenderer`, `Section` and `Epub` are all `class`, so the forward
  declarations match.
- **The new names do not clash.** `bookmarks` and `autoTurn` are not existing members of
  `EpubReaderActivity`, `ReaderActivity` or `Activity` (checked with `grep -nw`). No file outside
  `EpubReaderActivity.{h,cpp}` references any of the moved members.
- **Both host suites compile and pass exactly as the plan writes them.** I extracted plan lines
  76-130, 154-179, 713-773 and 797-833 into a scratch tree and built them with clang++
  `-std=c++20 -Wall -Wextra -pedantic` (the `crosspoint_test_common` flags, `test/CMakeLists.txt:42-46`)
  against googletest v1.17 sources. Output: `[  PASSED  ] 6 tests.` and `[  PASSED  ] 7 tests.`,
  which matches 1.4 and 3.4, with no warnings from the new code. The clamp and epsilon cases
  discriminate: an unclamped or epsilon-free implementation fails them. The wrap test
  (`MAX - 99` → `4899`/`4900`) yields elapsed times of 4999 and 5000 on the host.
- **The test CMake shape matches its models** (`test/bookmark_save_action/CMakeLists.txt`,
  `test/return_stack/CMakeLists.txt`). Task 0's `78:` and `128:` anchors are correct. `build/` is
  gitignored (`.gitignore:13`).
- **The FILES lock is complete.** Every path a step creates or edits is on a column-0 `FILES:` line
  (plan `:10-14`): `src/util/BookmarkMatch.h`, `test/bookmark_match/`, both `ReaderBookmarks`
  files, `src/activities/reader/AutoPageTurn.h`, `test/auto_page_turn/`,
  `EpubReaderActivity.{h,cpp}`, and `test/CMakeLists.txt`. The whole-tree format run in Task 5.3
  only rewrites these files, since `main` passes the CI format check. Build logs and `build/test`
  are outside the repo or ignored.
- **The `$PIO` wrapper exists** (`.../3749f345-.../scratchpad/pio-locked.sh`: a mkdir lock around
  `/Volumes/stein/.platformio/penv/bin/pio`).
- **Spec coverage is complete.** A1: follow-up issue text and "merge precondition" are in the PR
  body. A2: the rejected passage controller is recorded. A3/A5: plain member, two-call toggle.
  A7: `lastPageTurnTime` stays on the activity. A8/A9: the pure headers. A10: the hand-off lines,
  with `test/CMakeLists.txt` kept uncommitted and restored in 5.2. A11: `"ERS"` tags unchanged.
  Every call site in the spec's list is covered, including the `:381-383` comment. Tasks 2 and 4
  have no failing host test, but the spec sanctions that ("ReaderBookmarks itself is not
  host-built"), and the plan uses the `pio run` build as their gate (2.5, 4.3).

## Findings

No BLOCKERs and no MAJORs.

### MINOR 1 — `clang-format-fix -g` before `git add` never formats the new files

- **Claim:** 1.5, 2.5 and 3.5 format the work before committing it.
- **Problem:** `-g` passes `--modified` to `git ls-files` (`bin/clang-format-fix:21-24, 49`), and
  that option never lists untracked files. So none of the new files are formatted before their
  commit. The plan's own code is not fully clang-format-clean. I ran clang-format 21.1.8 with the
  repo's `.clang-format` on the extracted blocks: plan lines 407-411 (the nested ternary in
  `toggle`) get reflowed. Task 2's commit therefore ships unformatted. Task 5.3 catches it with an
  extra `style:` commit, but that commit also breaks the `--color-moved` reading of the one hunk.
  The squashed result is fine.
- **Evidence:** `bin/clang-format-fix:22-24` ("-g scopes formatting to tracked files currently
  modified"). Project memory "Format and check gates fail open" says the same.
- **Fix:** in 1.5, 2.5 and 3.5, `git add` the new paths first, then run `./bin/clang-format-fix -g`
  (staged files count as modified), then re-`git add`. Or paste the plan's ternary already in
  clang-format's layout.

### MINOR 2 — stale includes left in `EpubReaderActivity.cpp`

- **Claim:** the spec says "the plan checks each include" (spec `:194-196`).
- **Problem:** the plan removes `BookmarkFile.h` and `BookmarkUtil.h` but leaves two includes whose
  only users move out:
  - `#include "BookmarkEntry.h"` (`EpubReaderActivity.cpp:28`). Its only uses are `:104`, `:1830`,
    `:1852` and `:1902`, and all of them move in Task 2.
  - `#include <iterator>` (`:21`). Its only use is `std::size` at `:916`, which goes away in
    Task 4.
- **Fix:** add both lines to the 2.4a and 4.2a deletions, and add `BookmarkEntry` to the 2.4l
  leftover grep.

### MINOR 3 — two spec test cases are only partly covered

- **Duration per option:** the spec (`:221-222`) asks for "each option's duration and
  `pagesPerMinute`". The plan checks `pagesPerMinute` for every option but checks the duration,
  through `due`, only for option 4 (plan `:749-754`).
- **Fallback to the range:** the spec (`:219`) asks that "computed fields matching spine but not
  page count falls back to the range". Plan `:127` covers the negative side, with the percentage
  outside the range. The positive assertion at `:129` varies the spine instead of the page count,
  so it never shows an in-range percentage matching when spine and page match but the count does
  not.
- **Fix:**
  - In `EachOptionTurnsAtItsRate`, add `EXPECT_FALSE(turn.due(60000 / RATES[option] - 1, 0));
    EXPECT_TRUE(turn.due(60000 / RATES[option], 0));`.
  - In `PartialComputedMatchFallsBackToTheRange`, add `EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(0.45f,
    SPINE, PAGE_COUNT + 1, PAGE), SPINE, PAGE, PAGE_COUNT, RANGE));`.
  - The pass counts in 1.4 and 3.4 stay 6 and 7.

### MINOR 4 — small literal-execution gaps

- **`PIO` is never assigned.** Plan `:23-25` defines `PIO` in prose, but the commands at 2.5 and 4.3
  expand `$PIO`, and every Bash call starts a fresh shell. Run literally, the command becomes
  `run -e x4pro` and fails. Fix: write the command as `PIO=/private/tmp/.../pio-locked.sh; "$PIO" run
  -e x4pro ...`, or spell out the full path.
- **The log goes to `/tmp`.** 2.5 and 4.3 write `/tmp/claude-pio-110.log`, then say in parentheses
  "use the scratchpad instead". Fix: give the scratchpad path in the command itself.
- **No expected count for `lastPageTurnTime`.** 4.2g lists the occurrences it expects but no
  number. Fix: say the count is **9**: the `loop` deferral, the `due(...)` argument, the `:615`
  guard, `toggleAutoPageTurn`, and the five `pageTurn` writes.

### MINOR 5 — the commit count differs from the spec without a spec edit

- **Claim:** the spec's Goal (`:18-20`) says "one PR of two commits".
- **Problem:** the plan produces four commits, plus perhaps a `style:` commit. It gives a sound
  reason (each task must leave a committable tree), and squash-merge removes any effect on `main`.
  The spec was not updated, so a later reviewer checking commits against the spec will see a
  mismatch.
- **Fix:** add a line to the spec's "Changes from review" section, or state it in the PR body. No
  human decision is needed.

VERDICT: CLEAR
