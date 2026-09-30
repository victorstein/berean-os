# Issue #228 research — hide whole-Bible progress in the reader status bar

Branch `feature/228-status-bar-no-bible-progress`, based on `e3e5c94e` (release 1.28.1).
Every line number below was read on this branch on 2026-09-30. Where the issue's
line numbers drift, the current one is given.

## Which files own the behaviour

| Concern | Location |
|---|---|
| Setting defaults | `src/CrossPointSettings.h:265-267` — `statusBarChapterPageCount = 1`, `statusBarBookProgressPercentage = 1`, `statusBarProgressBar = HIDE_PROGRESS` |
| Bar-mode enum | `src/CrossPointSettings.h:93-98` — `BOOK_PROGRESS = 0`, `CHAPTER_PROGRESS = 1`, `HIDE_PROGRESS = 2` |
| Resolved spec | `CrossPointSettings::StatusBarSpec`, `src/CrossPointSettings.h:411-432`; filled by `statusBarSpec()` at `src/CrossPointSettings.cpp:246-260` |
| Caller that knows the book | `EpubReaderActivity::renderStatusBar`, `src/activities/reader/EpubReaderActivity.cpp:1796-1826` |
| Drawing | `BaseTheme::drawStatusBar`, `src/components/themes/BaseTheme.cpp:576-630` (declared non-virtual, `BaseTheme.h:239-242`) |
| Layout reservation | `UITheme::getStatusBarHeight` / `getProgressBarHeight`, `src/components/UITheme.cpp:132-147` |
| User docs | `USER_GUIDE.md:379-381` ("Customise status bar — … chapter page count, book percentage, progress bar style …") |

`BaseTheme::drawStatusBar` is the only definition — no theme overrides it
(`grep -rn drawStatusBar src/components` returns only `BaseTheme.{h,cpp}`).

## Current control flow

1. `renderStatusBar` (`EpubReaderActivity.cpp:1797-1800`) computes
   `currentPage`, `pageCount`, `sectionChapterProg`, and
   `bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100`.
2. It reads `SETTINGS.statusBarSpec()` (`:1804`) but only for `titleMode`.
3. It calls `GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, bookmarked, isBuilding)` (`:1824-1825`). No spec is passed.
4. `BaseTheme::drawStatusBar` re-reads `SETTINGS.statusBarSpec()` itself (`BaseTheme.cpp:583`), then:
   - Text (`:594-610`): when `showBookProgressPercent && showChapterPageCount` it prints
     `"%s%d/%d  %.0f%%"`; percent only → `"%.0f%%"`; page count only → `"%s%d/%d"`.
     The `~` estimate prefix applies to the page count only.
   - Bar (`:614-630`): when `showsProgressBar()`, width is `bookProgress` for
     `BOOK_PROGRESS`, else `currentPage / pageCount * 100` (the chapter branch).
5. The text lane's visibility (`StatusBarSpec::textLaneVisible`, `CrossPointSettings.h:428-431`)
   and the reserved height (`UITheme.cpp:132-147`) both read the spec from `SETTINGS`,
   independently of the draw call.

### Consequences for the change

- **Bar mode swap is layout-neutral.** `BOOK_PROGRESS → CHAPTER_PROGRESS` keeps
  `showsProgressBar()` true and `progressBarHeightPx` unchanged (it depends only on
  thickness, `CrossPointSettings.cpp:257-258`), so `getStatusBarHeight` is unaffected.
- **Dropping the percent can change `textLaneVisible`.** If the *only* text item on
  is the book percentage, a Bible-adjusted spec would report the text lane hidden,
  but `UITheme::getStatusBarHeight` still reserves it from the unadjusted `SETTINGS`
  spec. Result: an empty reserved text lane in a Bible, not a layout break. The spec
  has to decide whether that is acceptable or whether the reservation should follow.
  (The reader's page margins read `getStatusBarHeight` at `EpubReaderActivity.cpp:1136-1140`,
  so making the reservation Bible-aware would change the section cache's viewport — see
  CLAUDE.md "Viewport dimensions change".)
- **The theme currently has no way to receive an adjusted spec.** It reads
  `SETTINGS` directly. Honouring the issue's "one place" rule means either a new
  parameter on `drawStatusBar` (a spec, or a flag), or the caller zeroing
  `bookProgress` — the latter would still print `0%`, so it does not work alone.
- The Settings preview (`StatusBarSettingsActivity.cpp:279`) also calls
  `drawStatusBar` and is not a Bible context; it must keep today's output.

## How "is this a Bible" is decided

`epub->getBibleBookNavSpineIndex() >= 0` (`lib/Epub/Epub.h:91`, `Epub.cpp:980`).
Already used this way in the same activity at `EpubReaderActivity.cpp:274`, `:857`,
`:938`, and by `readerMenuTitle()` at `:1788`.

## Nearest existing example

`3591e3fe feat: compact touch metrics and drop Bible progress indicators (#197)`
removed the other Bible progress indicators, and its reader-menu rule is the
pattern to mirror:

- `src/activities/reader/ReaderMenuModel.h` — a header-only decision, "Free of
  StrId, HAL and Arduino so the host suite can exercise it" (`:28-30`), taking an
  `Inputs` struct with `bool isBible` (`:38`), with the rule
  `if (!in.isBible) m.addRow(A::GO_TO_PERCENT);` (`:93-94`).
- Its host test: `test/ui_layout/ReaderMenuModelTest.cpp`, registered in
  `test/ui_layout/CMakeLists.txt:52-65` with `${REPO_ROOT}/src` as the only extra
  include and `crosspoint_test_common` + `GTest::gtest_main`.
- Same shape: `test/launcher_bible/CMakeLists.txt` (header-only
  `src/activities/launcher/LauncherBible.h`, "no activity constructed").

### Host-test constraint

`CrossPointSettings.h` includes `ArduinoJson.h`, `PersistableStore.h` and
`SdPaths.h` (`CrossPointSettings.h:2-5`), and no host test includes it
(`grep -rln CrossPointSettings.h test` is empty). So the decision function cannot
take or return `CrossPointSettings::StatusBarSpec` directly and stay host-testable
the way `ReaderMenuModel.h` is. It needs plain inputs (`bool isBible`,
`bool showBookPercent`, `uint8_t barMode`) and plain outputs, with the three bar-mode
values restated or moved to a firmware-free header. That choice belongs to the spec.

## Out of scope, confirmed

- `EpubReaderActivity.cpp:768-773` — the initial-percent ("Go to %") path; not a status-bar indicator.
- `EpubReaderActivity.cpp:1944` — another `calculateProgress` use; not in `renderStatusBar`.

## Tool versions

- `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19`
- Platform: `pioarduino/platform-espressif32` release `55.03.37` (`platformio.ini:15`)
- `cmake --version` → `cmake version 4.4.2`
- googletest `v1.17.0` via FetchContent (`test/CMakeLists.txt:15-17`)

## Tier

Stays `light`. The change is inside the `ui` surface (reader activity, theme, a
header-only rule) plus `USER_GUIDE.md`; no stored format, setting or cross-surface
contract moves. One shared-file append is needed — a new test registration in
`test/CMakeLists.txt` or `test/ui_layout/CMakeLists.txt` — which `ui-dev.md` says to
report in the PR body rather than edit.
