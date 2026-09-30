Tiers: t1 standard, t2 standard, t3 standard, t4 heavy, t5 standard

# Whole-branch review 0: berean-os-20260930-bible-only-tags-from-any-publication-235-xhyr

Reviewed on `main` at `5be58731` (after #236, #242, #243, #245, #248). Host tests: `cmake --build build/test && ctest --test-dir build/test`, 1553/1553 pass. No firmware build was run; each PR recorded a clean `pio run -e x4pro`, and nothing merged after #248 touches source.

## Seams checked, found sound

- **#235, #239 and #238 in `EpubReaderActivity.cpp` compose.**
  - The non-Bible **Highlights** and **Tag** entries dispatch on `tagTarget()` first (`EpubReaderActivity.cpp:866-880`) and go to `openBibleTags()` (`:372-375`), which is a `goToReader` handoff. The reader is replaced, so #239's `reopenReaderMenu()` never needs to run on that path, and nothing depends on it.
  - Inside the Bible, the Tags intent routes to `openHighlights(CancelTo::Page)` (`:1069-1071`), so cancelling from a Bible → Tags jump returns to the Bible page, as Home → Tags does. The menu's own Highlights and Tags here use `CancelTo::Menu` (`:879`, `:883`), so they reopen over the page.
  - Every sub-screen that #239 names reopens through `reopenReaderMenu()`: Go to (`:966`), Search (`:983`), Footnotes (`:818`), Bookmarks, via `progressChangeResultHandler` (`:772`), Highlights and Tags here (`:438`). Text settings (`:832`) and Go to % (`:859`) also do.
  - `reopenReaderMenu()` (`:306-326`) re-renders before `openReaderMenu()` reads `chapterPassageCount` and `captureLeftPlace()` (`:280-286`). That keeps "Tags here (n)" and the Recent chips current after the round trip.
  - #238's `navCache` is a reader member (`EpubReaderActivity.h:48-50`). The page re-render does not touch it, and a non-Bible → Bible handoff builds a new reader, so a stale cache cannot cross books.
  - Home's Go to and the sheet's Go to share one path: `ReaderEntryIntent::Route::ChapterGrid` goes to `openChapterPicker(CancelTo::Page)` (`:1062-1065`), and the menu goes to `openChapterPicker(CancelTo::Menu)` (`:807`).
- **#235 and #237 in the launcher compose.** `resolveTargets()` calls `BibleFinder::find()` (`LauncherActivity.cpp:114`), and #237's layout changes (`:93-101`, `:254`, `:326-330`) do not touch it. There is one Bible lookup: the reader's `bibleReachableForTags()` (`EpubReaderActivity.cpp:350-365`) uses the same `BibleFinder::find(openPath)`, and no other lookup is left in `src/`. Recents are loaded at boot (`main.cpp:420`), which `find`'s recents step needs.
- **Tag rule.** `ReaderMenuModel::tagTarget` (`ReaderMenuModel.h:40-44`) is the only rule. `build()` uses it (`:87`), the activity uses it (`EpubReaderActivity.cpp:367-370`), and `openHighlightPassage` refuses outside the Bible (`:392-396`). The long-press path does the same (`:539-542`). Host tests cover Bible and non-Bible, with and without a reachable Bible (`test/ui_layout/ReaderMenuModelTest.cpp:177-221`).
- **#240, #226 and #235 counts.** The chip row and the grid both come from `TagChipView::buildEntries` (`TagChipView.cpp:14-44`), with the same scope. For Tags here, `HighlightsActivity.cpp:132` and `:177` both pass `computeVisibleIndices(std::nullopt)` into `countIn`, so the header and the grid agree. A non-Bible entry reaches this screen only by way of the Bible reader, so `STUDY.passages()` is always the Bible's.
- **Book-name sources have not drifted.** The cached table (`BibleBookIndex::names`) is filled by `joinToc` and `setAbbreviations(page.labels)` (`BibleNavigationActivity.cpp:144-145`). That is the same pair `BibleBookNameTable::load()` runs (`BibleBookNameTable.cpp:40-42`), so Recent chips read the same abbreviations from either source (`EpubReaderActivity.cpp:1002-1012`). `BibleReference` sources nothing new.
- **#238 memory.** `static_assert(sizeof(BibleNavCache) > 4096)` is at `BibleBookIndex.h:28`. The cache is allocated with `makeUniqueNoThrow` and checked for null (`EpubReaderActivity.cpp:949-956`), and a null cache degrades to no caching (`BibleNavigationActivity.cpp:116`, `:209`).
- **Acceptance criteria met on `main`:**
  - #237: the week-strip cell is `twoDigits + digit`, measured (`HomeLayout.h:89-91`, `:156`, `LauncherActivity.cpp:96-97`). There is no 480/800 in `HomeLayout.h` or `LauncherActivity.cpp`. The top and button gaps are pinned by tests (`test/home_layout/HomeLayoutTest.cpp:68-198`). The hero paying the 16 px is recorded decision A1 (`docs/superpowers/specs/2026-09-30-issue-237-design.md:34`).
  - #240: `chipHeight ≥ minTouchSize` (`TagChipRow.h:191-193`, `TagChipView.cpp:61-62`), and 24 chips fit on one page (`TagChipRowTest.cpp:454`).
  - #238: the device timing table is pending, merged that way by the owner's choice. Not a finding.
- **Leftovers.** The removed `STR_*` references (`STR_TAG_FILTER_ALL`, `STR_TAG_UNLABELLED`, `STR_BIBLE`, and others) are all still used. No translation key is orphaned.

## Findings

### MAJOR 1: USER_GUIDE.md contradicts #235 and #239, and describes #240's grid as a list

None of the five PRs touched `USER_GUIDE.md`, and none of the specs planned an update. On `main`, the guide states the opposite of two owner decisions:

- **#239.** `USER_GUIDE.md:196-198` says that after backing out of **Go to** or another menu screen, "the same sheet appears on a blank screen instead of over the page". It now appears over the page. Only night mode, snapshot OOM and rotation still clear it (`EpubReaderActivity.cpp:309-313`).
- **#235.**
  - `USER_GUIDE.md:176` ("**Tag** — start a passage selection") and `:185` ("**Highlights** — browse what you have marked in this publication") are wrong outside the Bible, where both entries open the Bible's tag list.
  - `:232-233` ("Tags are global: a tag you create in one publication is offered in every other") and `:238` and `:155` (long-press a word to start a selection) present tagging as something any publication supports. Tags are now Bible-only.
  - `:257` ("lists this publication's highlights") needs the same correction.
- **#240.** `USER_GUIDE.md:266-270` calls the **…** screen "the full tag list". It is now a chip grid, with counts, that shows every tag on one screen.

This is a documentation fix only. It needs no owner judgment and reverses nothing.

### MINOR 1: stale comment in TagChipRow.h

`src/activities/reader/TagChipRow.h:99` explains zero-count chips as "tags of other publications or chapters". Since #235, no publication other than the Bible carries tags. The zeros are now tags with no passages in this chapter, or none at all.

TRAILER
BLOCKERS: 0
MAJORS: 1
VERDICT: CLEAR
