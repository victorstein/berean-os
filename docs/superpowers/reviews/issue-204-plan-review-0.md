Tier: standard

# Issue #204 plan review 0

Plan: `docs/superpowers/plans/2026-09-30-issue-204-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-204-design.md` (with its "Amendments" section)
Base checked: `cbad8a66` (tree identical to `0275a5bd` for every file the plan edits)

## What was verified

- **Every replace anchor matches the tree exactly once.** `BaseTheme.h:195` and `LyraTheme.h:78`
  (`.textFieldLineEndOffset = 0};`), `UiListActivity.cpp:168-170`, `UiListActivity.h` `nav`/`buttonNavigator`,
  `BibleNavigationActivity.h:5-8,66,133-134`, `BibleNavigationActivity.cpp:279-280` (the only
  `RenderLock lock;\n    level = next;`), `:458-460`, `:653-672`, `:674-677`, the includes block,
  `MeetingsActivity.cpp:3-7,12-14,53-57,64,145-146,153-154`, `MeetingsActivity.h` `headerTitle`/`week_`,
  `PublicationsActivity.cpp:3-14,37-40,68-75,124-126`, `PublicationsActivity.h` anchors.
- **APIs exist with the signatures used.** `CoverBand::draw`/`Style` (`CoverBand.h:15-27`),
  `CoverBandGeometry::crop/thumbHeightFor/plateTop` (`CoverBandGeometry.h:34-53`),
  `CoverThumb::pathFor/sizeOf` (`CoverThumb.h:11-14`), `Epub(std::string, const std::string&)` and
  `getThumbBmpPath` (`lib/Epub/Epub.h:46,62`), `sdpaths::CROSSPOINT_DIR` (`lib/Serialization/SdPaths.h:11`),
  `HalDisplay::HALF_REFRESH` as an unscoped nested enum (`lib/hal/HalDisplay.h:14-18`),
  `RenderLock(Activity&)` (`src/activities/RenderLock.h:11`), `GUI` (`UITheme.h:49`), `RecentBook`
  (`src/RecentBook.h:9`). No name collides with `halfRefreshPending`, `hasMasthead`, `levelTitle` or
  `mastheadCover` in `src/`.
- **Arithmetic.** `thumbHeightFor(464,120) = thumbHeightFor(464,321) = 773`; Lyra body 658 and Classic
  body 656 both give 7 × 10 with cell 58 ≥ `MIN_CELL` (`NumberGridLayout.h:12-47`); the host test's
  zero bottom inset holds because touch zeroes `buttonHintsHeight` (`UITheme.cpp:54-56`) and
  `getScreenSafeArea` does not apply viewable insets (`UITheme.cpp:83-94`). Step 2.4's "7 tests" count
  is right (1 + 6).
- **Threading.** `requestUpdate` is deferred to the end of the loop pass (`ActivityManager.cpp:284-293`),
  so setting `halfRefreshPending` after `refresh()` in `onEnter` cannot lose the first HALF. `onEnter`,
  result handlers and `OptionPopup` callbacks all run with the render lock released
  (`ActivityManager.cpp:126,159`; `OptionPopup.h:65`), so the new `RenderLock lock(*this)` in
  `PublicationsActivity::refresh` cannot deadlock.
- **Host suite.** `test/masthead/CMakeLists.txt` mirrors `test/cover_band/CMakeLists.txt`;
  `crosspoint_test_common` and `REPO_ROOT` exist (`test/CMakeLists.txt:35-40`); every included header
  (`BaseTheme.h`, `LyraTheme.h`, `CoverBandGeometry.h`, `NumberGridLayout.h`) pulls std headers only.
- **Only two `ThemeMetrics` tables exist** (`BaseTheme.h:127`, `LyraTheme.h:9`), so the new field and
  its `static_assert` break nothing else.
- **FILES lock.** Every file a step edits or commits is on a column-0 `FILES:` line (plan `:8-14`).
  `test/CMakeLists.txt` is edited in the working tree only and restored before each commit, as the
  #202 plan did and its review 0 accepted. That follows the shared-files rule (`.claude/agents/ui-dev.md:22-27`).
- **Spec coverage.** Goals 1–4, A1–A11 and amendments MAJOR 1 and MINOR 1–6 each map to a step:
  metric plus `static_assert` (Task 1), T1–T4 (Task 2), A8 base field (Task 3), `pickCover` with
  one-generation and cap (Tasks 4, 7), grid gating and HALF on Back (Task 5), Meetings always-reserved
  band and heap log (Task 6), Publications conditional masthead under `RenderLock` (Task 7), and the
  device list (8.5). The exception is below.

## Findings

### MAJOR 1: Publications generates the masthead thumbnail with no progress popup

- **Claim.** Step 7.4 calls `Masthead::pickCover` after the row-thumbnail loop has finished, and
  `pickCover` may call `CoverThumb::pathFor` for a missing `thumb_773`.
- **Problem.** That call opens the EPUB and decodes its cover (`CoverThumb.cpp` `pathFor`:
  `epub.load(true, true)`, then `generateThumbBmp`). The spec's Changed table says Publications
  "picks and generates the masthead cover (A7) with the row thumbs, **sharing their popup**
  (`:65-76`)", and no plan step does this. Device check 8.5 #7 expects exactly this generation on
  first entry. The popup exists because generation is slow
  (`PublicationsActivity.cpp:65-66`: "Generating a cover opens the EPUB, so it happens once per book
  and behind a popup"). On a first entry where the row thumbnails are already cached but `thumb_773`
  is not, the screen freezes with no feedback. When a row thumbnail was generated, the popup is
  still in the framebuffer, but it says 100 % while a further generation runs.
- **Evidence.** Plan `:1131-1172` has no `drawPopup` and no `generatedAny` hand-off. Spec `:177`.
- **Fix.** Before calling `pickCover` in Step 7.4, show the popup when the first candidate's
  thumbnail is not cached. In `Masthead`, add
  `bool isCached(const GfxRenderer&, const std::string& bookPath)` (the existing `cachedThumb` at
  `thumbHeight`), then write:
  `if (!coverCandidates.empty() && !Masthead::isCached(renderer, coverCandidates.front()) && !generatedAny) popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));`.
  An equivalent route: give `pickCover` an optional `void (*onGenerate)(void*)` hook. Either way,
  list the new function in Step 4.1's header.

### MINOR 1: The grid's cover lookup dereferences `epub` unguarded and logs a spurious open failure

- **Claim.** Step 5.3 runs `epub->getThumbBmpPath(...)` and then `Masthead::fits(renderer, cover)`.
- **Problem.** (a) The activity treats `epub` as nullable. `loadBooks` guards it
  (`BibleNavigationActivity.cpp:76-78`: `if (!epub) return false;`), and the new line runs before that
  guard. (b) The spec specifies `Storage.exists` first (spec `:84-85,186`). `fits` goes straight to
  `CoverThumb::sizeOf` → `Storage.openFileForRead`, and that prints "Failed to open file for reading"
  (`freeink-sdk/.../SDCardManager.cpp:320-323`). Every grid entry without a cached `thumb_773` (the
  absent-thumbnail case in device check 8.5 #2) therefore logs a false failure.
- **Fix.** In Step 4.2, start `Masthead::fits` with
  `if (coverPath.empty() || !Storage.exists(coverPath.c_str())) return false;`. In Step 5.3, wrap the
  two lines in `if (epub) { ... }`.

### MINOR 2: When the Publications masthead changes on a FAST paint, the old cover ghosts

- **Claim.** A8 requests HALF where "a paint replaces art with paper … a FAST refresh is
  differential, so the masthead could ghost". The plan sets the flag only on entry (7.3) and on the
  grid's Back (5.5).
- **Problem.** `deleteEntry` → `refresh()` (`PublicationsActivity.cpp:217-220`) and the search result
  handler (`:246-249`) can change `mastheadCover_` from art to "" or to a different cover. That is the
  same art-to-paper (or art-over-art) FAST paint that A8 guards against on the grid.
- **Fix.** In Step 7.4, under the lock:
  `if (!mastheadCover_.empty() && mastheadCover_ != mastheadCover) halfRefreshPending.store(true);`
  before the move. Add "delete the masthead's book; no ghost" to device check 8.5 #4.

### MINOR 3: The Publications candidate ordering is pure logic with no failing test first

- **Claim.** Tasks 3–7 run no host test, and the plan says so because they need the renderer (plan `:376`).
- **Problem.** That holds for the paint glue, but not for Step 7.4's candidate list: listed recents
  first, deduplicated, then list order, capped at `MAX_MASTHEAD_CANDIDATES`. That is plain string and
  vector logic, and it carries the spec's A7 and amendment MINOR 4 semantics without a test.
- **Fix.** Move it to a renderer-free
  `MastheadLayout::coverCandidates(const std::vector<std::string>& recentPaths, const std::vector<std::string>& listedPaths, size_t cap)`,
  or a sibling header under `src/components/` added to the `FILES:` line. Add a failing
  `test/masthead/` case first: a recent that is not listed is skipped, duplicates are dropped, and
  the cap holds. Step 7.4 then maps `recents`/`entries_` to paths and calls it.

## Verdict

The plan is literal, the anchors and APIs check out against the tree, the numbers and host tests
match the amended spec, and each commit leaves a building tree. The one unmapped spec item (MAJOR 1)
and the three MINORs can be fixed inline. None reverses a decision or needs the owner.

VERDICT: CLEAR
