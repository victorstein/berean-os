Tiers: t1 light, t2 standard

# Whole-branch review 0: berean-os-20260930-hide-whole-bible-progress-in-the-status--4a72

Reviewed on `main` at `e3b54c37`, which contains #229 (`d2882dda`, issue #228) and #231
(`66cc071c`, issue #225). Line numbers refer to that commit.

## Seams between the two tasks

- **#225 did not undo #228.** #231 touched only the include, the new `knownChapter` helper
  (`src/activities/reader/EpubReaderActivity.cpp:78-82`) and two call sites: the menu title
  (`:1799`) and the status-bar chapter title (`:1823`). The #228 lines are still intact: the
  `isBible` flag and the `!isBible` argument to `GUI.drawStatusBar` (`:1830-1832`). The theme
  still routes both decisions through `StatusBarProgress::resolve`
  (`src/components/themes/BaseTheme.cpp:586-589`, used at `:598-607` and `:619`).
- **The leading-space edge case is fixed as #225 claims.** Before #231, the status bar called
  `bibleReference(title, chapter)` with no empty-title guard. Now `BibleReference::format`
  adds the space only when both the book and the numbers are non-empty
  (`src/util/BibleReference.cpp`). For a non-Bible EPUB, `currentBibleChapter()` is -1
  (`EpubReaderActivity.cpp:1788-1790`), so `knownChapter` gives chapter 0 and the title is the
  TOC title unchanged. The non-Bible status bar is byte-identical.
- **The "is this a Bible" check.** #225 added no new check. #228 added one inline check at
  `:1830`, which follows the file's existing idiom (MINOR 2).
- **One status-bar consumer outside the reader.** The Settings preview
  (`src/activities/settings/StatusBarSettingsActivity.cpp:279`) relies on the default
  `showWholeBookProgress = true` (`src/components/themes/BaseTheme.h:242`). That is the
  intended "preview unchanged" behaviour. No other theme overrides `drawStatusBar`.

## Requirements against main

**#228**
- Percent hidden and the BOOK bar tracking the chapter in a Bible: `StatusBarProgress.h:19-22`
  and `BaseTheme.cpp:598-607,619-624`. Met.
- Non-Bible output unchanged: `resolve` is the identity when `showWholeBookProgress` is true.
  Met.
- Rule in one place, as a pure function with a host test: `test/ui_layout/StatusBarProgressTest.cpp`
  exhaustively covers the Bible and non-Bible cases for every combination. It is registered at
  `test/ui_layout/CMakeLists.txt:101-114`. Met.
- USER_GUIDE: `USER_GUIDE.md:379-383` states the Bible behaviour, and settings are unchanged.
  This is coherent with `:218`. Met.
- Leave the "Go to %" initial-percent path alone: it is untouched (`EpubReaderActivity.cpp:774-779`).

**No Bible progress UI anywhere.** The remaining `calculateProgress` consumers aren't Bible
progress UI:
- the "Go to %" seed, whose menu entry is hidden in a Bible (`USER_GUIDE.md:191`);
- the screenshot filename (`EpubReaderActivity.cpp:1951`, `src/util/ScreenshotUtil.cpp:32-39`),
  which is a file name, not UI;
- bookmark ranges (`src/activities/reader/ReaderBookmarks.cpp:26-33`);
- `readBookProgressPercent`, whose only callers are the workbook card
  (`src/activities/launcher/LauncherActivity.cpp:194`) and the meeting cards
  (`src/activities/network/MeetingsActivity.cpp:274`).

**#225**
- One formatter with full-name and abbreviated variants: `BibleReference::format` is used at
  `EpubReaderActivity.cpp:1799,1823`, `src/util/PlacesDoc.cpp:60,68`,
  `src/activities/reader/BibleSearchActivity.cpp:530,600` and
  `src/activities/reader/PassageSelectActivity.cpp:238`. `bibleReference` and
  `formatTypedReference` have no remaining references in `src/`, `lib/` or `test/`. There is one
  miss (MINOR 1).
- Name sources are documented in the `BibleReference.h` header comment. Met.
- Cross-surface host test: `ReferenceConsistencyTest` is registered in
  `test/places_doc/CMakeLists.txt:36-66`. Met.
- NWT output unchanged: the diff changes only formatting internals. Met.

**Host tests.** I configured a fresh build in a scratch directory and ran
`StatusBarProgress|BibleReference|ReferenceConsistency|PlacesDoc|TypedReference`:
100% of 70 tests passed.

**Checked and clean.**
- UI text: no new user-facing literals. `STR_UNNAMED` and `STR_AUTO_TURN_ENABLED` are still
  routed through `tr()`.
- No hardcoded 480 or 800 dimensions.
- No stale comments or keys found.

## Findings

### MINOR 1: the Bible navigation verse header still formats "Book C" by hand

`src/activities/reader/BibleNavigationActivity.cpp:677` builds the Verse-level header with
`snprintf(headerTitle, sizeof(headerTitle), "%s %d", bookName, chapter)`. This is an eighth
"Book C" reference builder that #225's inventory (design §1) missed. It isn't user-visible
today:
- it draws the same TOC-joined name, `bookNames.joinToc` at `:110`;
- it guards the empty name itself (`:670,675`);
- the buffer is sized for the name plus the numbers (`BibleNavigationActivity.h:48`).

It still contradicts "one formatting function, used by all call sites". The fix is
`BibleReference::format(headerTitle, sizeof(headerTitle), bookName, {uint16_t(chapter)})`.
Doing that would also drop the hand-rolled empty-book branch.

### MINOR 2: the Bible check is still repeated inline instead of being shared

#228 added `const bool isBible = epub && epub->getBibleBookNavSpineIndex() >= 0;` at
`EpubReaderActivity.cpp:1830`. The same predicate is spelled inline at `:245`, `:280`, `:863`,
`:944`, `:1775` and `:1794`. #228 followed the existing idiom and #225 added none, so nothing
here is a behavioural risk. A private `isBible()` accessor would make this the single shared
check the run intended. This is optional cleanup.

## Summary

The two tasks compose cleanly. #225 preserved #228's status-bar rule and fixed the empty-title
space it claimed to fix. The owner's "no Bible progress UI" rule holds everywhere I checked.
There are two MINOR findings, and neither reverses a decision or needs the owner's judgment.

BLOCKERS: 0
MAJORS: 0
MINORS: 2
VERDICT: CLEAR
