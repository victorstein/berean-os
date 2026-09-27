# Issue #115 research: a week view for the Meetings screen

Branch `feat/115-meetings-week-view`, based on `af119732`. Every claim below cites a
line I read in this worktree, or a command and its output.

## 1. Which files own this today

| Concern | File |
|---|---|
| The screen | `src/activities/network/MeetingsActivity.{h,cpp}` (61 + 155 lines) |
| Week → issue data | `src/network/MeetingWeekTable.h` (entry = `key`, `watchtower`, `workbook` strings, `:16-20`), loaded through `MeetingWeekCache::load` |
| Issue → file on the card | `MeetingLibrary::findPublication(pub, issue)`, `src/network/MeetingLibrary.h:23` (returns `""` when absent) |
| ISO week | `IsoWeek {year, week}` and `isoWeekFromUtcDate`, `src/network/WolWeekScan.h:18-28`; implementation `WolWeekScan.cpp:41-60` |
| Cover thumbnails | `LauncherActivity::coverThumbFor` (static, `LauncherActivity.cpp:271-289`) and `drawCoverNative` (const member, `:357-376`) |
| Reader "Book x%" | `EpubReaderActivity::openReaderMenu`, `EpubReaderActivity.cpp:296-303`, displayed at `EpubReaderMenuActivity.cpp:182-187` |
| Settings | `src/CrossPointSettings.h` (fields), `src/SettingsList.h` (menu + persistence keys), `src/activities/settings/SettingsActivity.cpp` (tabs) |

## 2. Current control flow of `MeetingsActivity`

- It is a `UiListActivity` (`MeetingsActivity.h:23`) with a fixed `std::array<Row, 2>`
  plus a 3-slot `ListItem` array (`:53-54`): Watchtower, Workbook, Refresh.
- `onEnter()` → `refresh()` (`.cpp:32-35`). `refresh()` gets the UTC date from
  `halClock.getDate`, computes the ISO week, loads the whole `MeetingWeekTable`,
  finds this week's entry or falls back to `table.newest()`, and sets
  `resolvePending_` if the week is missing and no resolve has been tried this visit
  (`:37-60`).
- Per row it fills `issue`, `path` (`findPublication`), `label` ("name  issue") and a
  status `subtitle`: Open / Download / Workbook unavailable / Week unknown (`:62-81`).
  The "Workbook unavailable" branch is exactly the Memorial-week case: an entry
  exists but its `workbook` is empty (`:76-77`).
- `loop()` consumes `resolvePending_` once: `requestUpdateAndWait()` so the stale
  rows are on the panel, then `startDownload(false)` (`:84-95`).
- `buildScreen()` sets content margins under the header, then emits one
  `fui::list` with `ACTION_ROW` and `InputTouch` (`:97-128`).
- `activateIndex()`: index 2 → forced resolve; a row with a path →
  `activityManager.goToReader(path)`; anything else → non-forced resolve
  (`:130-146`).
- `startDownload()` launches `MeetingDownloadActivity` for result and runs
  `refresh(); requestUpdate();` when it returns (`:148-155`).

No cover, date, week strip or progress is drawn anywhere on this screen today.

## 3. The open question: can progress come from `progress.bin` + `book.bin` without opening the EPUB?

**Yes. The cheap path exists, and it reproduces the reader menu's number exactly.**

What the reader menu computes (`EpubReaderActivity.cpp:296-303`):

```
chapterProgress = section->currentPage / section->estimatedTotalPages()
bookProgress    = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100
percent         = clampPercent(int(bookProgress + 0.5))
```

What `progress.bin` persists (`EpubReaderUtils.h:14-37`, written from
`EpubReaderActivity.cpp:1304-1306`): little-endian `u16 spineIndex`, `u16 page`,
`u16 pageCount`, optional `u32 visibleTextOffset`, 6 or 10 bytes. The saved
`pageCount` is `section->estimatedTotalPages()` and the saved page is
`section->currentPage`, which are the same two inputs the menu uses. The reader also
accepts a legacy 4-byte file with no page count (`EpubReaderActivity.cpp:213-232`),
and treats `page == 0xFFFF` as a stale sentinel meaning 0 (`:218-221`).

What `calculateProgress` needs (`lib/Epub/Epub.cpp:924-933`): only
`getBookSize()` and `getCumulativeSpineItemSize(i)`. Both read the
`BookMetadataCache`'s in-RAM `cumulativeSizes` vector (`Epub.cpp:822-827, 888-893`).

What loading that vector costs (`BookMetadataCache.cpp:460-499`): it opens
`<cache>/book.bin` (`:15`, `:461`), checks `BOOK_CACHE_VERSION` (currently `10`,
`:14`), reads five metadata strings, seeks once, and reads `spineCount` spine entries
sequentially. It never touches the zip. `BookMetadataCache` is constructible
directly from a cache path (`BookMetadataCache.h:100-101`) and its `load()` is public
(`:119`).

The cache path is `cacheDir + "/epub_" + std::hash(filepath)`, computed in `Epub`'s
constructor with no I/O (`lib/Epub/Epub.h:46-49`).

So a card's percentage costs:
- `Epub(path, CROSSPOINT_DIR).getCachePath()`, which does no I/O;
- `BookMetadataCache(cachePath).load()`: one file, a few hundred bytes to a few KB;
- one read of at most 10 bytes from `progress.bin`.

`Epub::load(false, true)` would also work, since the cache-hit path returns at
`Epub.cpp:374-410` without the zip. But it constructs a `CssParser` as well (`:367`),
which the percentage does not need. Using `BookMetadataCache` directly avoids that.

Consequences the spec has to settle:
- **Downloaded, never opened, so no `book.bin`.** `BookMetadataCache::load()` fails.
  That still means no bar, but for a new reason. Note that `coverThumbFor` *builds*
  `book.bin` when it generates a missing thumbnail (`epub.load(true, true)`,
  `LauncherActivity.cpp:283`), so after the first thumbnail pass the metadata exists
  and only `progress.bin` is missing.
- **`book.bin` present, no `progress.bin`.** The publication has not been read. Show
  0%, or no bar; the spec decides.
- **Version mismatch.** `load()` returns false (`:467-472`). That is safe: no bar.
- The percentage arithmetic (prev/cur cumulative sizes, page/pageCount → percent) is
  pure and can be host-tested without `HalStorage`. There is also an in-memory
  Storage fake (`test/stubs/HalStorageFake.{h,cpp}`) if the file parsing is to be
  tested too.

The issue's fallback, "no bar until opened this session", is **not needed**.

## 4. Cover thumbnails: what the extraction has to carry

- `coverThumbFor(bookPath, height, generatedAny)` is `static` and uses no launcher
  state beyond the `MODULE` log tag (`LauncherActivity.cpp:271-289`). Thumbnails are
  cached per height at `<cache>/thumb_<height>.bmp` (`Epub.cpp:668`). The generated
  width is `height * 0.6` (`:706`).
- `drawCoverNative` uses `renderer` and the launcher-local constant `TILE_RADIUS`
  (`:357-376`, `:53`). It deliberately **refuses** to draw a bitmap larger than its
  box rather than rescale it, because the thumbnails are dithered 1-bit
  (`:261-266`, `:367-370`).
- Because thumbnails are keyed by height, a card height different from the launcher
  tile's (`coverFillHeight`, `:303-305`) generates **new** thumbnails on the first
  visit. When `book.bin` is missing that includes a full `epub.load(true, true)`,
  which is a zip parse. The only generation site today is the launcher's `onEnter`;
  the Meetings screen would become a second one.
- Callers: `coverThumbFor` at `:135` and `:159-160`; `drawCoverNative` at `:381`.
  Last touched in `c6314b4f` (#161).

## 5. Rendering building blocks that already exist

- FreeInkUI ships a `bookCard` widget (cover, title, author, meta, progress bar,
  `coverPainter` hook) in
  `freeink-sdk/libs/ui/FreeInkUI/include/components/media/book-card.h`, and
  `progressBar` in `.../controls/progress-bar.h`. `grep -rn "bookCard(" src` finds
  **no** caller in the firmware yet.
- `UiScreen` exposes `screen.frame()` and `screen.target()` for widgets and custom
  hits; `src/components/UiSliderDialog.h:43-70` is a firmware example
  (`screen.frame().hit(...)`, `fui::slider(screen.frame(), ...)`).
- The UI draw target is `GfxRendererTarget`, which draws immediately into the
  `GfxRenderer` (`FreeInkUIGfxRenderer.h:31`). So a `coverPainter` can call the
  same `renderer.drawBitmap` path `drawCoverNative` uses.
- Month names: `STR_MONTHS_SHORT` holds space-separated abbreviations
  ("ene feb … dic" in `spanish.yaml:381`), and it is consumed by
  `catalog::formatIndexDate` (`CatalogSearchActivity.cpp:94`). There are **no**
  full month-name or weekday strings, so "22 al 28 de septiembre" and the strip's
  day letters need new `STR_*` keys.
- There is no inverse of `isoWeekFromUtcDate` (ISO week → Monday date). The
  Hinnant `daysFromCivil` it uses is file-local (`WolWeekScan.cpp:20-29`). A
  Monday-of-week helper needs `daysFromCivil` plus its inverse `civilFromDays`, and
  it belongs beside it as pure code.

## 6. "Today" is a UTC date. This matters for the strip

`HalClock::getDate` returns the RTC's **UTC** calendar date (`lib/hal/HalClock.h:36-41`,
`HalClock.cpp:42-52`). A user UTC offset exists (`CrossPointSettings::clockUtcOffsetQ`,
quarter-hours biased by 48, `CrossPointSettings.h:259-262`), but only `formatTime`
applies it (`HalClock.cpp:54-66`). Every current week computation feeds the UTC date
straight in: `MeetingsActivity.cpp:40`, `LauncherActivity.cpp:178`,
`MeetingDownloadActivity.cpp:129`, `MeetingWeekPrefetch.cpp:31`.

At UTC−6, from 18:00 local the UTC date is already tomorrow. An inverted "today"
cell taken from `getDate` would sit one day ahead every evening, and on Sunday
evening the whole header would move to next week. The spec must either derive the
local date (UTC date plus `getTime` plus `clockUtcOffsetQ`) for the strip and the
week, or accept the offset knowingly. Changing the week key for the cache is a
wider behaviour change than this issue and would touch the downloader and prefetch.

## 7. Settings: how two new enums would land

- Enum settings are `uint8_t` fields with a struct-initialiser default
  (`CrossPointSettings.h:242-346`). Their labels are assigned **by enum value**,
  because they persist as ordinals (`SettingsList.h:231-248`).
- They persist automatically through `toJson`/`fromJson`, keyed on the
  `SettingInfo` key (`CrossPointSettings.cpp:63-84`). A missing key takes the field
  default, and an out-of-range value is clamped back to the default
  (`:163-169`). `FORMAT_VERSION` stays `1` (`CrossPointSettings.h:428`), so adding
  keys needs no version bump. The same list feeds the web settings API.
- Categories are a fixed four-tab array, Display / Reader / Controls / System
  (`SettingsActivity.cpp:34-35`), dispatched in `rebuildSettingsLists` (`:51-72`).
  A new "Meetings" category would mean a fifth tab plus a new vector and switch
  arm. The existing meeting settings, `publicationLanguage` and `meetingPrefetch`,
  already sit in `STR_CAT_SYSTEM` (`SettingsList.h:380-383`). That is the nearest
  precedent, and a lighter one than either Reader or a new tab.
- The brief says `SettingsList.h`, `CrossPointSettings.h` and
  `lib/I18n/translations/*.yaml` are shared with the sleep-screen task in this
  batch. The ui-dev guide says the YAML files are report-don't-edit append points
  (`.claude/agents/ui-dev.md`, "Shared files"). 32 translation files exist; English
  is the fallback for missing keys.

## 8. Nearest existing examples to mirror

- **Pure helper with a host suite:** `isoWeekFromUtcDate` and its tests,
  `test/wol_week_scan/WolWeekScanTest.cpp:113-128`, including a
  year-boundary case: 2027-01-01 → 2026/W53. The suite's `CMakeLists.txt`
  compiles `src/network/WolWeekScan.cpp` directly against `crosspoint_test_common`
  and `GTest::gtest_main`. `test/month_name_map` is the same shape. Registration is
  one `add_subdirectory` line in `test/CMakeLists.txt:82-89`, which is a shared file
  to report, not edit.
- **Launcher logic extracted for host tests:** `test/launcher_refresh`,
  `test/launcher_bible`.
- **Meetings-related setting:** `meetingPrefetch` (`CrossPointSettings.h:339-340`,
  `SettingsList.h:382-383`).
- **Toast (#116) and WifiSession (#109):** both are present
  (`src/components/ToastLayout.h`, `src/network/WifiSession.{h,cpp}`). This screen
  has no failure path that needs a toast beyond what `MeetingDownloadActivity`
  already reports.

## 9. Installed tool versions

```
$ /Volumes/stein/.platformio/penv/bin/pio --version
PlatformIO Core, version 6.1.19
$ cmake --version
cmake version 4.4.2
```

- Platform: pioarduino `platform-espressif32` 55.03.37 (`platformio.ini:15`),
  `framework = arduino` (`:17`)
- ArduinoJson 7.4.2 (`platformio.ini:154`; the host tests pin `v7.4.2`,
  `test/CMakeLists.txt:31`)
- googletest v1.17.0 (`test/CMakeLists.txt:17`)

## 10. Scope and tier

The work touches `src/activities` (ui), `src/CrossPointSettings.h` and
`src/SettingsList.h` (data-dev's surface), translations, and one new pure helper
next to `WolWeekScan`. It needs no new on-disk format. `progress.bin` and `book.bin`
are only read, and the two settings are new keys in an existing JSON store with no
version change. `heavy` already covers the cross-surface settings change and the
shared-helper extraction, so the tier stays as it is.

## 11. Heap notes for the device checklist

- The progress read allocates a `BookMetadataCache`, a `std::vector<uint32_t>` of
  `spineCount` entries, and five short strings, once per card. It is freed at scope
  exit.
- Thumbnail drawing opens one BMP per card through `Bitmap` and `drawBitmap`, the
  same path the launcher already takes for its tiles. Generation, on the first visit
  only, is the expensive step: JPEG/PNG decode, and possibly a `book.bin` build.
- The issue already asks for a serial heap reading on entering and leaving the
  screen.
