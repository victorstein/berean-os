Tier: heavy

# PR #171 code-quality review 0 (issue #110)

Scope: the code in `src/` and `test/`, from `9927c3f3..HEAD` (commits `a581d3b7`, `36094e42`, `bcb43b3d`, `e8352265`). The branch sits on an older `main`, so `git diff main...HEAD` also shows unrelated main-side changes. I reviewed the PR's own commits only.

## Pattern fit

- **`BookmarkMatch.h` fits its siblings.**
  - It is a pure, header-only rule under `src/util/`, next to `BookmarkSaveAction.h`.
  - It includes `"../BookmarkEntry.h"` the same way `BookmarkFile.h:8` and `BookmarkDoc.h:9` do.
  - Its test directory copies the `test/bookmark_save_action` layout.
- **`AutoPageTurn.h` follows the reader's pure-header pattern.**
  - Like `ReturnStack.h`, `BookGridLayout.h` and `TagRowMapping.h`, it lives in `src/activities/reader/` and is included from a test as `activities/reader/...` (`test/return_stack/ReturnStackTest.cpp:3`).
  - `test/auto_page_turn/CMakeLists.txt` is a line-for-line copy of `test/return_stack/CMakeLists.txt`.
- **`ReaderBookmarks` follows `EndOfBookOptions`.** It is a plain member controller with no heap indirection.
  - The comment on the `activities/Activity.h` include (`ReaderBookmarks.cpp:521-524` in the diff) copies `EndOfBookOptions.cpp:10-13`. It explains a real include-order constraint and is not narration.
- **Member naming:**
  - `ReaderBookmarks` uses trailing-underscore members, and `EndOfBookOptions.h:65-67` does not. The repo already has both styles in the same directory (`HighlightsActivity.h:134-142`, `PublicationsActivity.h:75-79`, `StudySleepPick.h:132-137` use the suffix).
  - CLAUDE.md forbids only a prefix.
  - This is not a finding.
- **Error handling is unchanged in shape.** `LOG_ERR` plus toast plus rollback moved verbatim. `git diff --color-moved` shows only the planned renames:
  - `cachedBookmarks` → `cachedBookmarks_`
  - `bookmarksSaveDisabled` → `saveDisabled_`
  - `updateBookmarkFlag` → `refreshPageFlag`
  - `section->` → `section.`
- **Nothing is duplicated.**
  - `ProgressRange`, `bookmarkMatchesProgress` and `getPageProgressRange` now exist once each.
  - The old anonymous-namespace copies and the `PAGE_TURN_RATES`, `initialBookmarkCacheCapacity` and `bookmarkProgressEpsilon` constants are gone from `EpubReaderActivity.cpp`.
  - The now-unused `<iterator>`, `BookmarkFile.h`, `BookmarkEntry.h` and `BookmarkUtil.h` includes were removed from the reader.
- **The test registrations are handed off, not missing.**
  - `auto_page_turn` and `bookmark_match` are not yet in `test/CMakeLists.txt`.
  - The PR body lists the two hand-off lines, and the anchor lines exist: `return_stack` at `test/CMakeLists.txt:78`, `bookmark_doc` at `:128`.
  - That is the documented hpipe convention for shared files.

## Test design

- **`BookmarkMatchTest` pins behaviour, not implementation.** It checks:
  - an exact computed match outside the range;
  - both epsilon edges, from both sides (`BOOKMARK_PROGRESS_EPSILON / 2` and `* 2`);
  - clamping at both ends of the book;
  - the re-paginated-chapter case, with negative and positive assertions.
- **`AutoPageTurnTest` does not derive its expectations from the code under test.** It hard-codes the intervals and rates instead of computing them from `RATES`. It also derives the wrap test from `std::numeric_limits<unsigned long>::max()`, so the test really wraps on a 64-bit host.

## Findings

**MINOR 1: `DueExactlyAtTheInterval` repeats a case the loop above it already covers.**
- `test/auto_page_turn/AutoPageTurnTest.cpp:836-841` starts option 4 and asserts `due(1000+4999)` is false and `due(1000+5000)` is true.
- `EachOptionIsDueExactlyAtItsInterval` (`:824-834`) already makes exactly those two assertions for option 4, with `LAST_TURN = 1000` and an interval of 5000.
- The extra test adds no coverage and leaves two places to update if the table changes.
- Fix: delete it.

**MINOR 2: `AutoPageTurn::pagesPerMinute()` divides by zero if it is called before a successful `start()`.**
- `src/activities/reader/AutoPageTurn.h:38` computes `60 * 1000 / durationMs_`, and `durationMs_` starts at `0` (`:42`).
- The only caller is `EpubReaderActivity.cpp:1616-1617`, and it checks `autoTurn.active()` first. The old code had the same hazard with `pageTurnDuration = 0UL`, so no behaviour changes.
- But this is now a public method on a type whose purpose is to be the tested boundary, and nothing in its interface says it requires `active()`.
- Fix: return `0` when `!active_`, or add a one-line precondition comment. Either is cheap.

No BLOCKER or MAJOR findings. The extraction reuses the existing patterns (`EndOfBookOptions`, `BookmarkSaveAction.h`, `ReturnStack.h`) rather than adding a second way to do the same thing. It leaves no dead or commented-out code. The comments that moved with the code are the load-bearing "why" comments: the rollback ordering, the transition-only toast, and the per-book save latch.

VERDICT: CLEAR
