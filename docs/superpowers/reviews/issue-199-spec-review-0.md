Tier: standard

# Issue #199 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-30-issue-199-design.md`
Research: `docs/superpowers/research/2026-09-30-issue-199-research.md`
Issue: `gh issue view 199 --repo victorstein/berean-os`
Read at `90568304` (worktree HEAD).

## What was verified and holds

These are the claims most likely to be wrong. I checked each against the code and none needs a change:

- **Cap arithmetic (A1, A2, A5).** `geometryFor` (`NumberGridLayout.h:34-41`) with stride 64 gives `cols = 488/64 = 7` at width 480, and `rows = min(fitRows, 70/7 = 10)`. The computed body height is 800 − (5 + 44) − 8 = 743. The inputs are `LyraTheme.h:11-14`; `buildScreen` sets the top margin and adds the spacer at `.cpp:403-407`; `UITheme.cpp:53-56` zeroes the button hints on touch boards. At 743, `fitRows` is 11 and the cap binds at 10. The page counts for 50, 66, 150 and 176 are correct.
- **Interaction budget (A1).** No other code on this screen registers interactions. `UiListActivity.cpp:156-168` draws chrome and footer outside `renderUi()` through `GUI`, and `drawChrome`/`drawFooter` (`BibleNavigationActivity.cpp:537-572`) register no hits. `keyGrid` indexes cells as `uint8_t` (`key-grid.h:58`), so 70 fits.
- **Square cells (A4).** `keyGrid` derives `cellW` and `cellH` from the rect it is given (`key-grid.h:54-55`). A rect of `cols*cell + (cols-1)*gap` therefore gives `cellW == cellH == cell` exactly. `minTouchSize == cell` does not enlarge the hit rects.
- **Book grid side effect (A3).** `BookGridLayout.h:91` reads `NumberGrid::MAX_CELLS`. The assertions that would move are at `BookGridLayoutTest.cpp:73, 84`, and `rows == 8` at `:83` would become 11 without the new `BookGrid::MAX_CELLS`.
- **Threading (A7).** `loadChapters` runs only from `activateIndex` at the Book level (`.cpp:270`) or from `enterAtPosition` before the first level switch (`.cpp:170`). `loadVerses` runs only from `openVerseList`, which is reached from the Book or Chapter level (`.cpp:246-256, 267, 278`). Both write before `enterLevel` publishes under `RenderLock` (`.cpp:218-234`).
- **Book numbering (A8).** The navigator's book index and `Unit.book` come from the same `BibleNav::Scanner` link order. The navigator uses `loadBooks`, `.cpp:65-72`. `Unit.book` comes from `UnitIndexCache::buildBookMap`, `src/study/UnitIndexCache.cpp:259-290`, which maps book `k` to value `k+1`. `take()` and `takeBookNav().targets` share one link set (`BibleNavScanner.h:58-62`).
- **Bookmark data (A9).** `computedSpineIndex` is persisted as `"si"` (`src/util/BookmarkDoc.cpp:16,45`), so loaded bookmarks carry it, not just newly added ones. `visibleTextOffset` and `VerseAnchor.offset` are both consumed as `offsetJump` (`EpubReaderActivity.cpp:683, 735`). `SELECT_CHAPTER` does not clear `bookmarks` (`EpubReaderActivity.cpp:710-727`), and the reader is pushed to `stackActivities` (`ActivityManager.cpp:153`).
- **Marker art (A10).**
  - `BookmarkStatusIcon` (`src/components/icons/bookmark.h`) is row-major with set bit = ink. `BaseTheme.cpp:34-41` draws it with `drawPixel(..., (byte & mask) != 0)`. That matches the BW1 contract in `FreeInkUIGfxRenderer.h:197-209`, so wrapping it in a `BitmapRef` works.
  - `TextStyle.color == White` draws white (`FreeInkUIGfxRenderer.h:134`).
  - `theme.key` is `defaultKeyStyles()` (`FreeInkUI.cpp:100`), with `selected` = black background and white foreground (`:25-26`).
  - `smallText` exists (`FreeInkUICore.h:661`).
- **PSRAM routing (§8).** `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` is set in the installed framework (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2154`). `bookNames` (66 × 48 B) and `bookAbbrev` (66 × 16 B) together are 4,224 B, so the navigator object is already above the threshold.

## Findings

### MAJOR 1: The test plan names the wrong test to rewrite, and the test that actually breaks is not mentioned

**Claim.** §9: "Existing `Psalm119TakesFourPages` (48 per page) is rewritten for 70 per page." It also says "480 × 695 (old fixture) … give 7 × 10".

**Problem.** Raising the cap does not break `Psalm119TakesFourPages`, because it passes the literal `48` to `pageCount` and never reads `MAX_CELLS`. The test that does break is `RowClampFiresInsteadOfOverflowing`, and the spec never mentions it. That test asserts that the row clamp binds at the `PORTRAIT_H = 695` fixture. With the cap at 70 it no longer binds there, and both of its assertions fail. The spec also keeps 695 as a fixture that must give an unclamped 7 × 10, which contradicts that test. The TDD step would start from a red suite the spec does not predict.

**Evidence.**
- `test/number_grid/NumberGridLayoutTest.cpp:89` is `EXPECT_EQ(NumberGrid::pageCount(PSALM_119_VERSES, 48), 4)`. It is pure arithmetic and passes unchanged.
- `NumberGridLayoutTest.cpp:39-47`: `unclampedRows = (695 + 8) / 64 = 10`, and 10 × 7 = 70.
  - `EXPECT_GT(70, MAX_CELLS = 70)` fails.
  - `geometry.rows = min(10, 70/7) = 10`, so `EXPECT_LT(10, 10)` fails.
- The same applies to `LastPageIsPartlyPadded` (`:91-97`) and the other literal-48 paging tests. They stay green, but they no longer describe the device.

**Fix.** In §9, replace the `Psalm119TakesFourPages` bullet with the following:

- Move `RowClampFiresInsteadOfOverflowing` to a fixture where the clamp still binds at 7 columns. The 480 × 743 body works: `unclampedRows = 11`, so the page is 77 cells before the clamp and 70 after. Keep 695 only as an "unclamped, still 7 × 10" case.
- Restate the literal-48 paging tests (`:89-147`) at 70, so the suite describes the shipped page size: Psalm 119 → 3 pages, and 36 cells on the last page.

### MINOR 1: A9 says both `std::vector` and "not a `std::vector`" for the bookmark copy

**Claim.** A9, third bullet: "The constructor copies the three fields into a member `std::vector<GridMarks::BookmarkPosition>`". Two bullets later: "one `makeUniqueNoThrow<GridMarks::BookmarkPosition[]>(n)` … Not a `std::vector`, whose growth aborts on OOM".

**Problem.** This is an internal contradiction, probably left over from an earlier draft. §5 and §6 follow the `unique_ptr` version.

**Evidence.** Spec lines 125 and 129-131; §5 line 232 (`std::unique_ptr<GridMarks::BookmarkPosition[]> bookmarkPositions` + `int bookmarkCount`).

**Fix.** Change the third bullet to "into a member `std::unique_ptr<GridMarks::BookmarkPosition[]>` plus a count". Also skip the allocation when `entries().empty()`, so a zero-length request never reaches the OOM log path.

### MINOR 2: The marker ink rule covers only normal and selected, not pressed or tap-flash

**Claim.** A10: "Ink follows the cell's state, mirroring the Space glyph … black on a normal cell, white on the selected (inverted) cell".

**Problem.** The Space glyph that A10 cites as its model does not use a two-way rule. It resolves the ink through `frame.stateFor(action, value, state)`, which also adds `StateActive` while a finger is down and `StateFocused` during a tap flash. `active` is black background with white foreground. A marker hardcoded to black on a non-selected cell would disappear on the inverted cell for as long as it is pressed.

**Evidence.**
- `key-grid.h:86`: `bp.styles.resolve(frame.stateFor(props.action, key.value, state)).foreground`.
- `FreeInkUICore.h:1034-1043` sets `StateActive` for the pressed slot and `StateFocused` for the tap flash.
- `FreeInkUI.cpp:31-32` gives `active` a black background and white foreground.

**Fix.** Take the marker ink the same way:

```cpp
props.keyStyles.resolve(screen.frame().stateFor(ACTION_ROW, row, baseState)).foreground
```

Here `baseState` includes `StateSelected` for the selected cell. Use it for both the dot's `TextStyle.color` and the bitmap's foreground.

### MINOR 3: The spec does not give `GridMarksTest` the include paths its headers need

**Claim.** §5: `GridMarks.h` includes `<StudyStore/Unit.h>` and `<Epub/VerseAnchors.h>`. §9: add an `add_executable` to `test/number_grid/CMakeLists.txt`.

**Problem.** The existing number_grid targets add only `${REPO_ROOT}/src`, and `crosspoint_test_common` adds only `${REPO_ROOT}` and `${REPO_ROOT}/lib`. Neither header resolves from those roots, because both live one directory deeper: `lib/StudyStore/StudyStore/Unit.h` and `lib/Epub/Epub/VerseAnchors.h`.

**Evidence.**
- `test/number_grid/CMakeLists.txt` (all three targets use `${REPO_ROOT}/src`).
- `test/CMakeLists.txt:37-41`.
- The pattern already in use: `test/unit_type/CMakeLists.txt:6` (`${REPO_ROOT}/lib/StudyStore`) and `test/highlight_doc/CMakeLists.txt:8` (`${REPO_ROOT}/lib/Epub`).

**Fix.** In §9, state that the new target adds `${REPO_ROOT}/src`, `${REPO_ROOT}/lib/StudyStore` and `${REPO_ROOT}/lib/Epub`. Also state that it links no `.cpp`. That holds only if `spanFor` compares `(major, minor)` itself rather than calling `study::operator<` / `orderableByAddress`, which are defined in `Unit.cpp:36`.

### MINOR 4: The heap check has no baseline on `main` and leaves out an expected internal delta

**Claim.**
- §9 Device: "the `BNV` memory line with the grid open, compared against `main` — internal free flat apart from the documented host growth."
- A9: the bookmark copy (< 800 B at typical counts) "lands in internal SRAM".

**Problem.**
- The new `BNV` line does not exist on `main`, so the comparison has nothing to compare against.
- The bookmark copy is live in internal SRAM while the grid is open, so "flat" is wrong by 8 B × n.
- The spec says `LOG_DBG` while "mirroring" `logMemory`, which uses `LOG_INF`. Both print at the dev env's `LOG_LEVEL=2` (`platformio.ini:174`; `Logging.h:51-58`), so only the wording is inconsistent.

**Evidence.**
- There is no heap log in `BibleNavigationActivity.cpp`.
- `main.cpp:660-664` already logs `ESP.getFreeHeap()` every 10 s on both builds, and in this framework that is `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` (`framework-arduinoespressif32/cores/esp32/Esp.cpp:163-165`).
- `src/study/BibleSearchIndexer.cpp:322` shows `logMemory` using `LOG_INF`.

**Fix.**
- Name the baseline as the periodic `MEM` line on both builds, taken with the grid open. Alternatively, put the `BNV` log in its own first commit so that a `main`+log build exists to compare against.
- List the bookmark copy (8 B × bookmark count, internal) in the expected delta, next to the per-host +1 KB.
- Pick `LOG_INF` or `LOG_DBG` and say which.

## Assumptions attacked with no finding

- **A6 per-level bitsets.** These are sound. At `CAPACITY = 176`, `chapterCount` and a normal verse count cannot exceed the bitset, and anything past it is refused rather than written out of bounds.
- **A10 `⌂` → ribbon.** The mockup shows `⌂`, but U+2302 is absent from every built-in font (research §4.3), and the ribbon matches the reader's status bar. The spec labels the substitution explicitly. It does not reverse the owner's direction, so no human decision is needed.
- **A11 book-level dot deferred.** The issue marks it optional (item 3).
- **§10 hand-offs.** `test/number_grid/CMakeLists.txt` is surface-local (`test/CMakeLists.txt:101` only adds the subdirectory), and there is no new UI text.

None of the findings reverses a decision, changes scope, or needs the owner's judgment. All of them can be fixed inline.

VERDICT: CLEAR
