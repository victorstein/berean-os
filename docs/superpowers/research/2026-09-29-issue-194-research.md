# Issue #194 research — compact metrics and no Bible progress indicators

Branch `feature/194-compact-metrics-no-progress`, base `a8461e94` (release 1.20.1). Every claim
below cites a line read on this branch or a command run on 2026-09-29.

## Installed tools and packages

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| ESP32 platform | pioarduino `55.03.37` | `platformio.ini:15` |
| CMake (host tests) | 4.4.2 | `cmake --version` |
| freeink-sdk gitlink | `67f7e01` "fix: confirm X4 Pro VBUS detect on GPIO21" | `git submodule status` |
| UI body font | Ubuntu 12, `advanceY` 29 | `lib/EpdFont/builtinFonts/ubuntu_12_regular.h` tail (`69, 29, 24, -5`); `GfxRenderer::getLineHeight` returns `advanceY` (`GfxRenderer.cpp:2141-2149`) |
| UI small font | Ubuntu 10, `advanceY` 24 | `ubuntu_10_regular.h` tail (`69, 24, 20, -4`) |

`uiScaleSpec()` pins small = `UI_10`, body = `UI_12`, title = `UI_12` (`src/components/UIScale.h:14-24`).

## Part A — where the metrics come from

### Theme selection and the metrics struct

- Lyra is the theme on this device: every non-Classic value, including out-of-range ones, resolves
  to `LyraTheme` + `LyraMetrics::values` (`src/components/UITheme.cpp:36-43`).
- `LyraMetrics::values` (`src/components/themes/lyra/LyraTheme.h:9-77`): `topPadding 5` (`:11`),
  `batteryBarHeight 40` (`:12`), `headerHeight 84` (`:13`), `verticalSpacing 16` (`:14`),
  `listRowHeight 40` (`:18`), `listWithSubtitleRowHeight 60` (`:19`), `headerBatteryDetached true`
  (`:32`), `tabBarHeight 40` (`:34`).
- Classic (`BaseMetrics::values`, `src/components/themes/BaseTheme.h:124-148`) already uses the
  shape the issue asks for: `batteryBarHeight 20`, `headerHeight 45`, `headerBatteryDetached false`.
- **Existing touch-only metric override:** `UITheme::getMetrics()` copies the theme's metrics and,
  when `gpio.hasTouch()`, sets `buttonHintsHeight = 0` (`UITheme.cpp:48-61`). It re-derives if the
  touch flag flips after static construction (`:49-52`). This is the one place metrics already
  differ by touch.

### The header band

`BaseTheme::drawHeader` (`src/components/themes/BaseTheme.cpp:285-395`) has both layouts:

- **Detached (Lyra today):** the title is pinned to the bottom of the band,
  `titleTop = band.height - headerUnderline - spaceMd - titleLineHeight` (`:343-351`), and the
  battery sits in a separate top strip.
- **Shared line (Classic):** the title gets a `rightReserve` (or `leftReserve`) of
  `batteryReserve + spaceMd` (`:352-359`), and a subtitle is drawn manually in the lower-right
  corner (`:333-336`, `:387-394`).
- In **both** paths the battery is drawn in `Rect{batteryX, band.y, batteryReserve,
  batteryBarHeight}` (`:381-385`), and the FreeInkUI component centres the glyph vertically in
  that rect (`freeink-sdk/libs/ui/FreeInkUI/include/components/bars/battery-indicator.h:77-78`).
  So on a shared line the battery is vertically centred with the title only if
  `batteryBarHeight == headerHeight`; with Classic's 20/45 it sits in the top 20 px.
- The header component vertically centres a title in its content rect
  (`components/controls/header.h:136-144`).
- `uiThemeTokens()` copies `metrics.headerHeight` into the FreeInkUI `headerHeight` token
  (`src/components/UIThemeTokens.h:31-34`), so fui-drawn headers follow the same value.

### Screens that hand-offset from the header

`grep -rn headerHeight src` finds **64** references in **38** files (the issue's "58" is out of
date). Apart from `drawHeader(...)` calls themselves, every content offset is
`[safe.y +] topPadding + headerHeight [+ verticalSpacing | tabBarHeight | subHeaderHeight()]`, so
they follow the metric automatically. The ones that add something of their own:

| File:line | What it adds |
|---|---|
| `src/activities/launcher/LauncherActivity.cpp:303` | `max(topPadding + headerHeight, marginTop) + TILE_GAP`; the tiles re-flow from `available` (`:310-316`), the Bible tile absorbing the remainder. |
| `src/activities/network/MeetingsActivity.cpp:64` | `+ gap` |
| `src/activities/reader/BibleNavigationActivity.cpp:404, 570` | `+ subHeaderHeight()` (the Bible grids) |
| `src/activities/network/CrossPointWebServerActivity.cpp:393-419` | `+ tabBarHeight + verticalSpacing * 2` |
| `src/activities/network/WifiSelectionActivity.cpp:964, 1018` | `+ tabBarHeight + ...` |
| `src/components/UiSliderDialog.h:36` | `+ verticalSpacing * 4` |
| `src/activities/util/KeyboardEntryActivity.cpp:401, 698` | `+ verticalSpacing + ...` |
| `src/activities/settings/TextSettingsActivity.cpp:65`, `src/activities/home/CrashActivity.cpp:40` | `+ verticalSpacing` |
| `src/components/UITheme.cpp:69` | `getNumberOfItemsPerPage` reserves `headerHeight + verticalSpacing` |

`LyraTheme.cpp:21`'s `topHintButtonY = 345` is **not** header-relative. It positions the side
button hints (`LyraTheme.cpp:196-221`), and `LyraTheme::drawSideButtonHints` returns immediately
when `gpio.hasTouch()` (`:173-176`), so it never draws on the X4 Pro.

`verticalSpacing` is referenced **87** times in `src/` (`grep -rn verticalSpacing src | wc -l`).
Changing Lyra's value moves every one of them, not only the list spacer.

### List row height on touch

- `themeTokensForLineHeight` sets `rowHeight = lineHeight * 2 + 8`
  (`freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:114`). With the body line height of 29 that
  is **66 px**. `minTouchSize` stays 44 because `lineHeight + 14 = 43` does not exceed the default
  (`FreeInkUI.cpp:117`; default `44` at `FreeInkUICore.h:637`).
- `UiListActivity::syncListViewport` (`src/activities/UiListActivity.cpp:123-138`) and its tab
  twin `UiTabListActivity::syncTabListViewport` (`src/activities/UiTabListActivity.cpp:73-90`)
  both start from `screen.theme().rowHeight`. Only the **non-touch** branch substitutes
  `listRowHeight` / `listWithSubtitleRowHeight` and writes `props.rowHeight`. On touch,
  `props.rowHeight` stays 0 and `FreeInkApp::list` falls back to `theme_.rowHeight`
  (`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkApp.h:283-284`).
- `list()` grows an individual row only for wrapped text (`components/lists/list.h:366-420`): a
  subtitle row grows when its label wraps, and a label-only row grows only when
  `labelLh * maxLines > rowH`. So a smaller fixed `rowH` for one-line rows leaves subtitle rows
  sized by their content.
- Rows per page are `(body.height + rowGap) / (rowHeight + rowGap)` (`FreeInkUI.cpp:853-859`).
- **The theme's `rowHeight` token is not list-only.** It also sizes buttons and sliders:
  `UiSliderDialog.h:50`, `UiAppHelpers.h:140`, `BibleSearchActivity.cpp:757, 803`,
  `BibleDownloadActivity.cpp:317, 352`, `FrontlightPanelActivity.cpp:184-226`,
  `WifiSelectionActivity.cpp:1038`, and inside the SDK `FreeInkApp.h:115, 206, 217, 306, 319, 347`.
  Lowering the token in `uiThemeTokens()` would shrink all of those. The narrower lever is the two
  `sync*Viewport` functions, which already set `props.rowHeight` per list.
- `WifiSelectionActivity.cpp:1065` has its own copy of the `screen.theme().rowHeight` start.

### Measured baseline: rows per page (touch, portrait 480×800)

The X4 Pro profile sets no `viewableInsets`, so they default to `{}`
(`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:675`; the `XTEINK_X4_PRO` block at
`:1364` does not assign them), and `buttonHintsHeight` is 0 on touch (`UITheme.cpp:54-56`).

| Screen | Above the list today | Body | Rows at 66 px |
|---|---|---|---|
| Settings | `topPadding 5 + headerHeight 84` (`SettingsActivity.cpp:438`) + tab band `max(40, 24 + 10) = 40` (`UiTabListActivity.cpp:149-151`) + `verticalSpacing 16` (`:187`) = 145 | 655 | **9** |
| Reader menu | `5 + 84` (`EpubReaderMenuActivity.cpp:176`) + progress band `tabBarHeight 40` (`:188`) + `verticalSpacing 16` (`:191`) = 145 | 655 | **9** |

For reference, a 44 px header, an 8 px spacer and 50 px rows would give Settings
`800 - (5+44+40+8) = 703 → 14` rows, and the reader menu without its progress band
`800 - (5+44+8) = 743 → 14` rows. Both clear the issue's +30% bar (≥ 12).

## Part B — the progress indicators

### Home: "N of 1189 chapters read"

- `LauncherActivity::applyChaptersReadSubtitle` (`src/activities/launcher/LauncherActivity.cpp:84-98`)
  loads `ChapterCompletionFile` for `study::BIBLE_PUB_KEY` and overwrites `bibleSubtitle` with
  `tr(STR_BIBLE_CHAPTERS_READ)`. It is declared at `LauncherActivity.h:89` and called only at
  `LauncherActivity.cpp:135`, after `bibleSubtitle = bibleTitleFor(...)` (`:134`). Removing the
  call leaves the tile showing the Bible's title.
- Its only reason to include `study/ChapterCompletionFile.h` is this function
  (`LauncherActivity.cpp:41`; `grep ChapterCompletion src/activities/launcher/` finds nothing else).

### Reader menu: progress band and title

- `EpubReaderMenuActivity::buildScreen` (`src/activities/reader/EpubReaderMenuActivity.cpp:172-224`)
  builds `progressLine` from `STR_CHAPTER_PREFIX`, `currentPage/totalPages`, `STR_PAGES_SEPARATOR`
  and `STR_BOOK_PREFIX` + `bookProgressPercent` (`:182-187`), draws it in a `tabBarHeight` band
  (`:188-190`) and adds a `verticalSpacing` spacer (`:191`).
- `currentPage`, `totalPages`, `bookProgressPercent` are constructor arguments stored as members
  (`EpubReaderMenuActivity.h:33-34, 85-87`; `.cpp:18-29`) and used nowhere else in the menu.
- The only construction site is `EpubReaderActivity::openReaderMenu`
  (`src/activities/reader/EpubReaderActivity.cpp:257-289`). It computes those three values
  (`:259-267`) solely to pass them in, and passes `epub->getTitle()` as the title (`:278`). It
  already knows `isBible = epub->getBibleBookNavSpineIndex() >= 0` (`:276`).
- `clampPercent` is still used elsewhere in the reader (`EpubReaderActivity.cpp:647, 800`), so it
  stays.
- The title is drawn by `drawChrome` → `GUI.drawHeader(..., title.c_str())` (`:226-234`).

**The nearest existing "current reference" string** is the status bar's chapter-title mode,
`EpubReaderActivity::renderStatusBar` (`EpubReaderActivity.cpp:1628-1641`): the TOC item covering
`currentSpineIndex` (`getTocIndexForSpineIndex` / `getTocItem`), which for a Bible is the book
("Exodo"), plus `' ' + bibleChapterNumber` when `bibleChapterNumberSpine == currentSpineIndex`.
Two caveats the spec has to handle:

1. `bibleChapterNumber` is filled by `resolveBibleChapterNumber()` (`:1592-1610`), called once
   per section load (`:1173`), using `SpineHtmlStream::WhenMissing::Fail`. Its own comment
   (`:1601-1605`) says that when a finished layout cache meant the chapter HTML was never read,
   **the number stays unknown (-1)**. In that case the status bar shows only the book name, and a
   menu title built the same way would read "Isaiah" rather than "Isaiah 40".
2. The status bar only builds this string when `titleMode == CHAPTER_TITLE`; the menu would need
   it regardless of that setting.

`PassageSelectActivity.cpp:242` builds a verse-level label the same way (`toc.title + " " + verse`).

### Study sleep screen: chapter-progress strip

In `src/activities/boot_sleep/StudySleepScreen.cpp`:

- `ScreenContent::progress` (`:86`), `loadCompletion` (`:232-249`), `progressHeight` (`:251-253`)
  and `drawProgress` (`:255-289`), with constants `STRIP_MAX_BAR`, `STRIP_MIN_BAR`,
  `STRIP_BAR_GAP`, `STRIP_CAPTION_GAP` (`:54-57`).
- `drawScreen` adds `SECTION_GAP * 2 + progressHeight` to `chromeHeight` (`:319`) and draws the
  strip at `:361-364`. `render` allocates the record and loads it at `:396-401`.
- The passage already gets "whatever height is left": `fitPassage` is given
  `areaBottom - viewTop - chromeHeight` (`:325-328`) and picks the largest serif size that fits.
  Dropping the `:319` term hands the strip's height to the passage with no further change.
- `#include "StudyStore/ChapterCompletion.h"` (`:30`) exists only for the strip.

### Meetings — keep

`MeetingsActivity.cpp:248-251` formats `STR_MEETING_PROGRESS` from `readBookProgressPercent`, and
`:356-358` feeds the card's progress bar. Neither reads the chapter-completion store. Untouched.

### The chapter-completion store — out of scope, still written

`ChapterCompletion` is referenced by `src/study/StudyStore.{h,cpp}`,
`src/study/ChapterCompletionFile.{h,cpp}`, `lib/StudyStore/StudyStore/ChapterCompletion.{h,cpp}`,
`lib/Serialization/PersistableStore.h` and the host tests `test/chapter_completion/` and
`test/storage_io/ChapterCompletionFileTest.cpp`. After this issue only the launcher and sleep
screen stop reading it; the writers stay, per the issue.

## Translation keys that become unused

| Key | Only code use | Present in |
|---|---|---|
| `STR_BIBLE_CHAPTERS_READ` | `LauncherActivity.cpp:95` | `english.yaml:10` only |
| `STR_BOOKS_FINISHED` | `StudySleepScreen.cpp:284` | `english.yaml:482`, `spanish.yaml:425` |
| `STR_CHAPTER_PREFIX` | `EpubReaderMenuActivity.cpp:184` | **all 32** `lib/I18n/translations/*.yaml` |
| `STR_PAGES_SEPARATOR` | `EpubReaderMenuActivity.cpp:185` | **all 32** |
| `STR_BOOK_PREFIX` | `EpubReaderMenuActivity.cpp:187` | **all 32** |

(`grep -rn "\b<KEY>\b" src lib test scripts docs`, excluding generated headers.) Remaining hits
are only in older docs under `docs/superpowers/`.

The issue says to remove keys "from `english.yaml` and `spanish.yaml`". For the last three keys
that is not enough: `scripts/gen_i18n.py:232-237` warns for every language that has a key English
lacks, so removing them from English alone would print a warning for 30 files. Today the generator
prints no such warning (`python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` → "Total:
477 | Used in code: 476 | Never used: 1", no WARNING lines). The repo's precedent is to delete a
dropped key from every language file: `70041d90 refactor: drop 22 unreferenced translation keys
(#57)` and `321c6ecd refactor: remove unreachable fork screens ... (#146)` each touch
`french.yaml` among the others (`git log --stat -- lib/I18n/translations/french.yaml`).

`.claude/agents/ui-dev.md` lists `lib/I18n/translations/*.yaml` as a shared append point to
"report, do not edit". The issue explicitly asks for the removal, so the spec should decide
whether the ui work edits them or hands the exact deletions to the orchestrator.

## USER_GUIDE.md passages that describe these screens

- `USER_GUIDE.md:103` — Home: "**Bible** — opens your Bible and shows how many chapters you have
  read."
- `USER_GUIDE.md:371` — Study sleep screen: "... its first tag, and your Bible reading progress".
- `USER_GUIDE.md:150-160` — reader menu section. It lists items and does not mention the progress
  band, so the new title may deserve one line.
- `USER_GUIDE.md:429` (`completion/` in the `.berean/` table) and `:449` ("deleting `.berean/`
  clears ... Bible reading progress") describe the store, which this issue keeps. They are still
  accurate.

## Host tests

`test/` has no suite covering `ThemeMetrics`, `UiListActivity`, `StudySleepScreen` drawing or the
reader menu. Nearby pure-helper suites: `test/study_sleep_pick/StudySleepFitTest.cpp` (the passage
fit the strip competes with), `test/launcher_bible/`, `test/launcher_refresh/`,
`test/number_grid/` (the Bible grid layout), `test/book_progress/` (links `src/` only,
`test/book_progress/CMakeLists.txt`). A row-height or header-layout rule pulled into a pure
function would be the natural thing to test; as the code stands, none of the Part A values pass
through a testable helper.

## Nearest existing examples

- **Touch-specific metric:** `UITheme::getMetrics()` zeroing `buttonHintsHeight` on touch
  (`UITheme.cpp:48-61`).
- **Per-list row height set from our side without touching the SDK:** the non-touch branch of
  `UiListActivity::syncListViewport` (`UiListActivity.cpp:125-136`) and
  `UiTabListActivity::syncTabListViewport` (`:77-86`), which write `props.rowHeight` from
  `ThemeMetrics`. `EndOfBookOptions.cpp:184` does the same unconditionally.
- **Shared-line header with the battery inline:** Classic's `BaseMetrics::values`
  (`headerBatteryDetached = false`) and the `else` branch of `BaseTheme::drawHeader`
  (`BaseTheme.cpp:352-359`).
- **Current-reference string:** `EpubReaderActivity::renderStatusBar` chapter-title mode
  (`EpubReaderActivity.cpp:1628-1641`).
- **Dropping translation keys:** `70041d90` (#57), removing from every language file.

## Tier

The work stays inside the `ui` surface (`src/activities`, `src/components`) plus the i18n YAML and
`USER_GUIDE.md` the issue names. It changes no on-disk format, no store and no SDK code, so the
`standard` tier holds.
