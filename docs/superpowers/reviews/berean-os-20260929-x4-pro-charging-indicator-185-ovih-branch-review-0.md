Tiers: t1 heavy, t2 standard, t3 heavy, t4 standard, t5 standard, t6 standard, t7 heavy, t8 heavy, t9 standard, t10 standard, t11 standard, t12 standard, t13 standard

# Whole-branch review: berean-os-20260929-x4-pro-charging-indicator-185-ovih

Range reviewed: `48310b5f~1..93a74464` on `main` (PRs #189, #190, #193, #197, #207, #209, #211,
#212, #213, #217, #219, #221, #223). The review read code on `main` and built and ran the whole
host suite out of tree: 1481 tests pass.

## Findings

### MAJOR 1: "Tags here" shows chip counts for the whole publication while the list shows one chapter (#200 × #205 seam)

#205 (PR #212) replaced the filter row with tag chips. Its acceptance criterion is "The counts
match the passages in the list." #200 (PR #217) then added a chapter-scoped mode to the same screen
(`spineFilter_`, reached from the menu's **Tags here (n)** row, `EpubReaderActivity.cpp:792-794`).

- The list honours the filter: `computeVisibleIndices` narrows to `STUDY.passagesInDocument(*spineFilter_)`
  (`src/activities/reader/HighlightsActivity.cpp:56-62`).
- The chips do not: `rebuildChips` counts `STUDY.passages()`, the whole publication
  (`HighlightsActivity.cpp:132-133`).

In a Tags-here view holding 3 rows, the chips still read `All 37`, `hope 12` and so on. Tags that
have no passage in this chapter still get a chip (`:149` skips only zero *publication* counts), and
tapping one gives an empty list. The menu row's "Tags here (3)" and the chip's "All 37" disagree on
the same screen.

Each PR passed its own review, because #212 predates the filter and #217 did not revisit the chips.

Fix: count over the spine-filtered candidates when `spineFilter_` is set, and add a host test. No
owner decision is needed.

### MAJOR 2: USER_GUIDE.md does not describe the final state

Only #189, #197, #207 and #217 touched `USER_GUIDE.md`. Five merged features that change what the
user sees are missing, or are described in their old form:

- **§4 The launcher** (`USER_GUIDE.md:99-113`) still describes four tiles under a "bereanOS" header
  and a "Continue Reading" tile. #203's Home is not described: the cover hero with
  Continue · reference and Go to, Recent places, "From your tags", the Meetings strip and the icon
  row.
- **§6 The reader menu** (`:150-181`) has no **Recent** row (#201).
- **§8 Browsing what you marked** (`:226-229`) still says "The filter row at the top narrows the list
  to a single tag". #205 replaced that row with tag chips and counts.
- **No section covers verse search**, so typed references ("Isa 40:31" → **Go to …**, #206) are
  undocumented.
- **§17 Where your data lives**: the `.berean/` table (`:436-445`) has no `places.json` (#201).
  `docs/file-formats.md:487` does document it.
- **Progress wording contradicts the owner rule.** `:460` still says deleting `.berean/` clears
  "your highlights, tags and Bible reading progress", but that progress no longer exists (#195).
- **#188's user-visible rules are not stated.** A selection snaps to whole verses (§8). A passage
  too long to fit whole is left out of the Study sleep rotation (§14, `:382-385`). Older tags are
  rebuilt when their publication is next opened.

`docs/file-formats.md` is coherent: PassageDoc v4 and its repair are at `:516-551`, `places.json`
v1 at `:487-510`, and the retired completion file at `:350-370`. Only the user guide lags.

No owner decision is needed.

### MINOR 1: The reader status bar still shows whole-Bible progress by default

`CrossPointSettings.h:266` defaults `statusBarBookProgressPercentage = 1`. In a Bible,
`EpubReaderActivity::renderStatusBar` passes `epub->calculateProgress(...)` (`EpubReaderActivity.cpp:1797`)
and `BaseTheme.cpp:594-606` draws it as "n/m  47%" on every page. The `BOOK_PROGRESS` progress-bar
mode (`BaseTheme.cpp:625`) is a second path to the same indicator.

#194's spec kept the status bar deliberately (`2026-09-29-issue-194-design.md:70`), and the issue
itself scoped the removal to Home, the reader menu and the sleep screen. That makes this a
consistent decision, not a miss. It is, however, the last Bible reading-progress indicator left on
the device, it is on by default, and the stated rule is "no Bible progress UI anywhere".

Raise it with the owner: hide the book percentage and the book progress bar in a Bible, or accept
them as a user setting. Nothing is broken either way, so this is MINOR.

### MINOR 2: Home's "Go to" opens the chapter grid, although the intent is named `BookGrid`

- Home sends `Kind::BookGrid` (`src/activities/launcher/HomeTargets.h:56`).
- `ReaderEntryIntent::route` maps that to `Route::ChapterGrid` (`src/activities/reader/ReaderEntryIntent.h:37-38`).
- `openChapterPicker` (`EpubReaderActivity.cpp:250-279`) passes `currentSpineIndex`, so #187's
  entry opens the chapter grid of the book last read.

#203 says "Go to… (the book grid, through the entry intent)". The behaviour matches #187's owner
decision for Select chapter and costs one Back to reach the book grid. The mismatch is in the name
and the issue text, not in what the device does. Rename the kind (for example `GoTo`), or confirm
the landing with the owner.

### MINOR 3: Three orphaned translation keys

- `STR_BIBLE_SUBTITLE_NONE` (`english.yaml:9`, `spanish.yaml:9`) lost its only caller when #203
  rewrote the launcher.
- `STR_SEARCH_VERSES` (`english.yaml:21`) and `STR_TOGGLE_BOOKMARK` (`english.yaml:218`) lost theirs
  when #200 replaced `buildMenuItems` with `STR_SEARCH` and `STR_MARK`.

All three are still in the other language YAMLs as well. Remove them, and keep them out of comments
too, because `gen_i18n.py` scans comments.

### MINOR 4: Independent "Book C:V" formatters

Five separate formatters build this string:

- `bibleReference` (`src/activities/reader/BibleReference.h:8`)
- `PlacesDoc::formatReference` (`src/util/PlacesDoc.cpp:51`)
- `PlacesDoc::formatChipLabel` (`:62`)
- `formatTypedReference` (`src/activities/reader/TypedReference.cpp:254`)
- the search hit row (`BibleSearchActivity.cpp:599`)

They agree on the grammar but take the book name from different sources. The reader title and
places use the TOC title, search uses `BibleBookNameTable`, and the chips use nav abbreviations. The
chapter also comes from two sources: `currentBibleChapter()` from the verse markers, and
`unit.major`.

Today they give the same result on the NWT. Where the TOC and nav names differ, one place would be
labelled two ways. There is also an edge case: `formatReference` with an empty book name (when no
TOC entry covers the spine, `EpubReaderActivity.cpp:532-537`) stores a reference with a leading
space, " 40:31".

### MINOR 5: A failed places save is only logged

`PlacesStore::record` (`src/PlacesStore.cpp:40-43`) logs an error when `saveToFileAtomic()` fails and
does not tell the UI. CLAUDE.md says a failed write "must report to the UI rather than fail
silently". Data safety holds: the write is atomic and the budget is derived and static_asserted. The
data is also non-critical recent history.

### MINOR 6: Stale cross-reference in MastheadLayout

`src/components/MastheadLayout.h:20-22` says the band matches "the launcher's Bible tile
(`LauncherActivity::computeLayout`)". Since #203 the dependency runs the other way: `HomeLayout.h:107-109`
calls `MastheadLayout::band`. The thumbnail keys still agree.

## Verified sound

- **Reader seams (`EpubReaderActivity.*`):**
  - #187's positioned entry survives #200 and #201 through `openChapterPicker`/`openBibleSearch`, and
    those two helpers are shared by the menu and the entry intents.
  - `CancelTo::Menu` versus `CancelTo::Page` is correct for both paths.
  - #195's `recordDocumentRead` is gone, and every chapter-leave site now records a place instead:
    `pageTurn`, `skipPages`, `navigateTo` and `onExit`. The place is captured under `RenderLock`, and
    the write happens after the lock is released.
  - Nothing records per page.
  - #188's `STUDY.repairTexts()` runs in `loadBook`, off-lock.
- **Reader menu:**
  - The #194 title (`readerMenuTitle`, through `bibleReference`) feeds the #200 sheet.
  - #201's Recent band sits between the tiles and the columns and is layout-tested with the maximum
    row count (`ReaderMenuSheetLayoutTest.cpp:158-200`).
  - Go to % is hidden for a Bible (`ReaderMenuModel.h:92`).
  - The page snapshot uses `makeUniqueNoThrow`, falls back to the cleared sheet on OOM, night mode,
    rotation or a missing framebuffer, and is released in `onExit`.
- **Owner rule elsewhere:**
  - There is no chapters-read, book-% or strip UI on Home, the reader menu or the sleep screen.
  - `ChapterCompletion` is gone from code and tests. `COMPLETION_DIR` stays as a reserved path, as
    `docs/file-formats.md:350-357` documents.
  - Meetings workbook % is kept, as intended.
- **Duplication:**
  - Masthead draws through `CoverBand` (`Masthead.cpp:84`), and the Home hero does as well; the
    launcher keeps no private blit.
  - GridMarks reuses `BibleEntry::chapterRowFor`.
  - HomeVerse reuses the sleep picker's scan, seed, fit and sampler, and caches its pick once per
    day in RAM.
  - The grid keeps 7×10 under the masthead (`MastheadLayoutTest.cpp:96`).
- **Stores:**
  - PassageDoc v4 refuses a future version, keeps `"w"` uncapped but inside the budget, refuses a
    save over budget with a UI report, and keeps whole texts in PSRAM with a justification
    (`PsramJsonAllocator.h`).
  - The repair is bounded, never downgrades a row, and restores every row if the save fails.
  - PlacesStore is a `PersistableStore` with atomic writes, a derived ≤4 KB budget, and a refused
    `v:2` or missing `v` that blocks saves.
  - The acceptance-criteria host tests for #188 and #201 exist and are registered.
- **Mechanics:**
  - No bare `new`, no new `std::function`, and no hardcoded 480 or 800.
  - All UI text goes through `tr()`.
  - Every new test suite is registered in `test/CMakeLists.txt`, and the removed suites
    (`chapter_completion`, `passage_label`) are unregistered.
- **#185:**
  - The submodule points at `victorstein/freeink-sdk` branch `berean`, and the pinned commit is on
    that branch.
  - GPIO21 is debounced by `StableLevel`.
  - `classifyWakeup` keeps an X4 Pro cold boot with USB present classified as PowerButton.
  - The out-of-date GPIO10 comment and the SDK doc are both corrected.

## Trailer

Neither MAJOR reverses a decision, changes scope or needs the owner. Each is a bounded fix: one
chip-count scope and one documentation pass. MINOR 1 and MINOR 2 are questions to put to the owner,
and neither blocks.

VERDICT: CLEAR
