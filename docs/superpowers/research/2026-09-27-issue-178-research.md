# Issue #178 — research

Branch `fix/178-batch-3-follow-ups` at `4b1697a5`, equal to `origin/main` after
`git fetch` (`git rev-parse origin/main HEAD` → both `4b1697a55d6e…`). Every
`file:line` below was read at that commit.

## Tools as installed

| Tool | Version | Command |
|---|---|---|
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| Python | 3.14.7 | `python3 --version` |
| CMake | 4.4.2 | `cmake --version` |

Script tests run in CI as `python -m unittest discover -s scripts/tests -v`
(`.github/workflows/ci.yml:163-164`); `scripts/tests/` holds
`test_build_catalog_index.py` and `test_repo_warnings.py`.

**i18n baseline.** `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
reports `String keys: 477`, `Unused keys: 1`; English 477 own, Spanish 419 own /
58 fallback. The one unused key is `STR_HIGHLIGHTS_TOO_LARGE` (a grep of every
`english.yaml` key against `src/` and `lib/`, excluding `lib/I18n`). Item 2 must
leave the count at 1 or lower.

## Issue citations that have drifted

- `src/study/StudySleepPick.h` does not exist. The file is
  `src/activities/boot_sleep/StudySleepPick.h`; the civil-date code is at
  `:147-180` and the day shift at `:201-206`.
- The `WolWeekScan` civil-date code is in the **.cpp**, file-local:
  `src/network/WolWeekScan.cpp:20-55` (`daysFromCivil`, `civilFromDays`,
  `isValidCivilDate`). The header only declares `CivilDate` (`WolWeekScan.h:30-34`)
  and the public functions. The day shift is `WolWeekScan.cpp:118-127`
  (`localDateFromUtc`).
- The download progress throttle has **three** copies, not two — see item 3.

---

## Item 1 — a book in a subfolder cannot be opened

### Who owns it

- `CardBooks::list()` (`src/util/CardBooks.cpp:36-45`) lists the download folder
  then `/`, each through `collect()` (`:19-28`), which reads one directory level
  with `Storage.listFiles(dir, 400)` and keeps `.epub` names only. No recursion.
- `PublicationsActivity::refresh()` (`src/activities/catalog/PublicationsActivity.cpp:44-79`)
  builds its rows from `CardBooks::list()` alone.
- Rows today: `SEARCH_ROW = 0`, `HEADER_ROW = 1`, books from `FIRST_BOOK_ROW = 2`
  (`PublicationsActivity.h:49-51`). Every row→entry conversion goes through
  `entryIndexForRow` (`.cpp:31-35`), which returns -1 below `FIRST_BOOK_ROW`.
  `activateIndex` (`.cpp:183-191`) special-cases `SEARCH_ROW` then opens the entry.
  `listCount()` is `FIRST_BOOK_ROW + entries_.size()` (`.h:53`).
- The launcher reaches Publications through `openPublications()`
  (`src/activities/launcher/LauncherActivity.cpp:547-553`) with
  `startActivityForResult`, so Publications sits on the activity stack.

### Every path into the file browser

`grep -rn goToFileBrowser src`:

| Site | Reachable on the X4 Pro? |
|---|---|
| `ReaderUtils.h:263` | No. Needs `wasReleased(Back)` from a physical button; the reader returns early on `wasBackGesture()` (`:256-258`) and the X4 Pro has no Back button. |
| `BmpViewerActivity.cpp:260` | Only after opening an image, which needs the file browser first. |
| `BibleDownloadActivity.cpp:124` | Only on the Confirm screen shown when no Bible is on the card (the "choose a file" action). |
| `ActivityManager.cpp:210` | `goToReader("")` fallback only. |

The launcher has no menu: it is five fixed tiles, `enum class Tile { Bible,
Meetings, Search, Settings, Resume, COUNT }` (`LauncherActivity.h:37`). "Search"
opens Publications. The issue's "launcher menu" option would mean a new tile and a
layout change (`computeLayout`); a Publications row is the smaller change.

### What the file browser does

- `ActivityManager::goToFileBrowser(path)` (`ActivityManager.cpp:204-206`) calls
  `replaceActivity`, which drops the whole stack (`ActivityManager.h:79`).
- `FileBrowserActivity` lists folders plus `.epub`, `.bmp`, `.png` in `Mode::Books`
  (`FileBrowserActivity.cpp:57-72`), hiding dot-files unless `showHiddenFiles`.
  Tapping a file calls `onSelectBook` → `activityManager.goToReader(path)`
  (`Activity.cpp:15`, `FileBrowserActivity.cpp:328-330`).
- Back: short Back goes up a directory; at `/` in `Mode::Books` it calls
  `onGoHome()` (`FileBrowserActivity.cpp:363-390`). On this device the left-edge
  swipe *is* Back: `MappedInputManager::wasReleased(Back)` returns true on
  `wasBackGesture()` (`src/MappedInputManager.cpp:265`). So the browser is fully
  navigable by touch once it is entered.
- It can also be **pushed** rather than replaced:
  `SdFirmwareUpdateActivity.cpp:28` pushes
  `std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", Mode::PickFirmware)`
  with `startActivityForResult`. In `Mode::Books` the root-level Back still calls
  `onGoHome()`, so a pushed browser returns to the launcher, not to Publications,
  unless that branch changes.

### Nearest example

The Search row itself: a non-book row at a fixed index ahead of the books, with a
`tr()` label and hint (`PublicationsActivity.cpp:144-148`) and a branch in
`activateIndex`. A "Browse the card" row mirrors it and needs one new key
(label, plus a hint if it follows the Search row's two-line shape), and one
constant shift (`HEADER_ROW`/`FIRST_BOOK_ROW` +1) that `entryIndexForRow` already
isolates.

---

## Item 2 — weekday names, three sources

Only `english.yaml` and `spanish.yaml` carry any of these keys
(`grep -n … lib/I18n/translations/*.yaml`); the other 30 languages fall back to English.

| Key | english.yaml | spanish.yaml | Order | Consumer |
|---|---|---|---|---|
| `STR_WEEKDAYS` | `:481` "Sunday Monday …" | `:423` "domingo lunes …" (lowercase) | Sunday first | `StudySleepScreen.cpp:193` → `study_sleep::formatDateLine` indexes it with `weekdayFromDays` (`StudySleepPick.h:177-180`, 0 = Sunday) |
| `STR_WEEKDAYS_NARROW` | `:425` "M T W T F S S" | `:370` "L M X J V S D" | Monday first | `MeetingsActivity.cpp:198-200`, `copyWordAt(…, i, …)` for strip cell i (Monday first, `MeetingWeekView.h:20`) |
| `STR_MONDAY`…`STR_SUNDAY` | `:430-436` | `:375-381` (capitalised) | seven keys | `SettingsList.h:250-258`, `meetingDayValues` for the two meeting-day enum settings |

Constraints a single source has to satisfy:

1. **The settings enum wants a `StrId` per value.** `SettingInfo::enumValues` is
   `std::vector<StrId>` (`SettingsActivity.h:33`). There is a runtime alternative,
   `enumStringValues` (`SettingsActivity.h:34`, used by the font-family setting,
   `SettingsList.h:21-60`), but:
   - `getSettingsList()` builds its base list once, in a function-local
     `static const` (`SettingsList.h:229-230`). Strings resolved there freeze at
     the first call's UI language; the first call is `CrossPointSettings` load
     (`CrossPointSettings.cpp:68,121`).
   - The option popup for an enum with more than two values reads
     `enumValues.data()` directly (`SettingsActivity.cpp:256-258`); only the other
     branch honours `enumStringValues` (`:283-284`).
   - Settings load clamps a persisted value against `enumValues.size()`
     (`CrossPointSettings.cpp:169`), so `enumValues` can never be emptied.
   - The web settings route prefers `enumStringValues` when present
     (`src/network/SettingsRoutes.cpp:68-77`).
   Deriving the settings labels from a list string therefore is not a
   YAML-only change. Keeping `STR_MONDAY`…`STR_SUNDAY` as the source and deriving
   the lists from them is.
2. **Spanish narrow Wednesday is "X"**, not the first letter of "miércoles"
   (`spanish.yaml:370`). A first-letter derivation turns it into "M", colliding
   with Tuesday — a visible change in the meetings strip.
3. **Capitalisation differs.** The sleep screen prints lowercase Spanish
   ("domingo 27 sep", asserted by `test/study_sleep_pick/StudySleepPickTest.cpp:241`);
   the setting values are capitalised ("Domingo"). One source changes one of the two
   unless the derivation also folds case.
4. `formatDateLine` takes the list as a `std::string_view` and resolves the word
   with `catalog::wordAt` (`StudySleepPick.h:194-209`); its tests pass literal lists
   (`StudySleepPickTest.cpp:205-207`). A change of source changes that signature or
   the list's order.

Which of (2) and (3) is kept is a spec decision, not something the code settles.

---

## Item 3 — duplicated helpers

### Civil date (two implementations)

- `src/network/WolWeekScan.cpp:20-55`: `daysFromCivil(int y, unsigned m, unsigned d)`,
  `civilFromDays(int64_t)` → `CivilDate{uint16_t year; uint8_t month; uint8_t day}`
  (`WolWeekScan.h:30-34`), plus `isValidCivilDate` by round trip. Public on top:
  `isoWeekday` (1 = Monday), `addDays`, `mondayOfIsoWeek`, `localDateFromUtc`
  (`WolWeekScan.h:36-51`, `.cpp:88-127`).
- `src/activities/boot_sleep/StudySleepPick.h:147-180`, header-only in
  `namespace study_sleep`: its own `CivilDate{int32_t year; …}`, `daysFromCivil`,
  `civilFromDays`, and `weekdayFromDays` (0 = Sunday). The comment at `:153-154`
  says it copied the network one because that copy is file-local.

Differences that a fold must not smuggle in:
- Year type: `uint16_t` vs `int32_t`.
- Validation: `WolWeekScan` rejects Feb 30 through `isValidCivilDate`;
  `formatDateLine` only range-checks month 1-12 and day 1-31
  (`StudySleepPick.h:198-200`) and would shift Feb 30 through `civilFromDays` into
  March. Using the validating path changes that edge from "prints a March date" to
  "prints nothing". The RTC cannot report Feb 30, so the difference is theoretical,
  but "behaviour must not change" makes it a spec call.
- Weekday numbering: ISO 1..7 Monday-first vs 0..6 Sunday-first.

### UTC-offset day shift (twice)

Identical arithmetic, including the `> 104` clamp:
`StudySleepPick.h:201-206` and `WolWeekScan.cpp:118-127`
(`localMinutes = h*60 + m + (offsetQ - 48) * 15`, shift -1/0/+1).

### Cover thumbnail (third copy)

`PublicationsActivity::loadThumb` (`PublicationsActivity.cpp:81-92`) repeats
`CoverThumb::pathFor` (`src/util/CoverThumb.cpp:29-47`) line for line: same
`Epub(path, CROSSPOINT_DIR)`, `getThumbBmpPath`, `exists`, `load(true, true)`,
`generateThumbBmp`, re-`exists`. The only difference: `pathFor` also `LOG_DBG`s a
book with no cover. The rest of `loadThumb` (`:94-129`) decodes the BMP into a BW1
buffer for a FreeInkUI `BitmapRef`; `CoverThumb` has no such function (its
`drawNative` draws straight to the renderer). The per-pixel "value < 3 is ink"
unpack `packedRow[c/4] >> (6 - (c*2)%8) & 3` also appears in
`LauncherActivity.cpp:381-384` and `SleepActivity.cpp:633-637`, each with a
different crop, so only the unpack is common to all three.

`CoverThumb` is not host-tested (no `test/` suite names it); it depends on `Epub`
and `HalStorage`. The pure part that can be host-tested is the row unpack.

### Publication-language label (twice)

`CatalogSearchActivity.cpp:59-64` (`publicationLanguageName()`, anonymous
namespace) and `BibleDownloadActivity.cpp:64-65` (inline ternary): both
`SETTINGS.publicationLanguage == PUB_LANG_ENGLISH ? tr(STR_LANG_ENGLISH) :
tr(STR_LANG_SPANISH)`. `CrossPointSettings.h:86` already hosts a sibling pure
mapping, `langWritten(uint8_t)` → `"E"`/`"S"`. The settings enum maps the same
values to the same keys (`SettingsList.h:246-248`).

### Download progress throttle (three copies)

Same constants (`STEP_PERCENT = 5`, `MIN_UPDATE_MS = 5000`) and the same predicate
`percent >= 100 || last < 0 || percent >= last + STEP || now - lastMs >= MIN_MS`:

- `MeetingDownloadActivity.cpp:24-25`, `:249-257`
- `BibleDownloadActivity.cpp:33-34`, `:214-222`
- `CatalogSearchActivity.cpp:35-36`, `:463-469` (`throttledProgressRepaint`)

Each keeps `int lastRenderedPercent = -1; unsigned long lastProgressUpdateMs = 0;`
(`MeetingDownloadActivity.h:79-80`, `BibleDownloadActivity.h:74-75`,
`CatalogSearchActivity.h:127-128`) and resets both before a download. The input
pumping above the predicate differs per activity (Catalog handles Back only, no
home gesture: `CatalogSearchActivity.cpp:452-456`), so only the percent and the
throttle decision are common. `SdFirmwareUpdateActivity.cpp:229-232` throttles on
percent change alone and is a different rule.

### Nearest example of this kind of extraction

- `src/activities/network/MeetingWeekView.{h,cpp}` from #169: pure view helpers
  beside their activity, tested by `test/meeting_week_view/` whose
  `CMakeLists.txt` compiles the helper `.cpp` plus `WolWeekScan.cpp` directly.
- `src/util/CoverThumb.{h,cpp}` from #169 (commit `71c8e322`): "the launcher's
  `coverThumbFor` and `drawCoverNative`, moved without behaviour changes into a
  shared helper" — the same fold, applied to two of the three copies.
- `test/study_sleep_pick/CMakeLists.txt`: a header-only helper tested with no
  activity constructed.

Suites touched today: `WolWeekScanTest` (25 `TEST`s), `StudySleepPickTest` (31),
`MeetingWeekViewTest` (11).

---

## Item 4a — "this week" is the UTC ISO week

Four call sites compute the current week from the RTC's UTC date alone:

| Site | Code |
|---|---|
| `MeetingsActivity.cpp:105-109` | `halClock.getDate(today)` → `isoWeekFromUtcDate` |
| `LauncherActivity.cpp:177-180` | `thisWeeksMeetingPublication()`, same pair |
| `MeetingDownloadActivity.cpp:127-133` | `runSequence()`, same pair; failure shows `STR_CLOCK_NOT_SET` |
| `src/network/MeetingWeekPrefetch.cpp:26-33` | `due()`, same pair |

`MeetingsActivity.cpp:185-193` already computes a **local** today with
`localDateFromUtc(*utcToday, hour, minute, SETTINGS.clockUtcOffsetQ, …)` for the
week strip's highlight, and its comment states the split on purpose: "Only today
is local. The week stays the UTC ISO week the launcher, the downloader and the
prefetch use, so the cards always match what they fetch." So the fix has to move
all four together, or the cards and the fetch disagree across the offset window.

The sleep screen's date is local: `StudySleepScreen.cpp:186-194` reads
`getDate` and `getTime` and passes `SETTINGS.clockUtcOffsetQ` to
`formatDateLine`. `HalClock` exposes `getTime(hour, minute)` and `getDate(Date&)`
separately (`lib/hal/HalClock.h:28,41`), two RTC reads; both existing local paths
accept that.

`MeetingWeekPrefetch.cpp` is under `src/network/`, the net surface; the other three
are `src/activities`.

## Item 4b — retiring the v1 catalog index

- The workflow builds and publishes both versions per language
  (`.github/workflows/catalog-index.yml:10-14` header comment; builder args
  `--out-v1 catalog-<lang>.txt --previous-v1 previous-<lang>.txt` at `:89-90`,
  `:109-110`; upload loop `:146-162`, which always includes `catalog-$lang.txt` and
  adds v2 only outside `hold`).
- The plain `.txt` is also the diff baseline: `:62` downloads
  `catalog-$lang.txt` as `previous-$lang.txt`, and `:151-155` says the `.txt` stays
  published "because the next run diffs against it".
- `scripts/build_catalog_index.py:354-355` defines `--out-v1` and
  `--previous-v1`; `scripts/tests/test_build_catalog_index.py` covers the builder.
- Current firmware fetches only v2: `CatalogIndexStore::assetUrl()`
  (`src/network/CatalogIndexStore.cpp:56-62`) → `catalog-<lang>.v2.txt.gz`, with
  a comment that v1 stays at its name for older firmware.
- The first release reading v2 is **v1.17.4**: the only commit that introduced
  `.v2.txt.gz` there is `0a88a616` (#163, 2026-09-27), and
  `git tag --contains 0a88a616` starts at `v1.17.4`. Firmware ≤ v1.17.3 fetches
  `catalog-<lang>.txt.gz` and refuses any version other than 1.
- `docs/file-formats.md:531-562` documents both versions; `:547-548` says v1 "is
  still published unchanged for firmware that predates version 2", and `:555-556`
  says "Firmware since issue #157 reads versions 1 and 2".
- OTA is user-initiated (Settings → check for updates, per `CLAUDE.md`), so there
  is no telemetry for how many devices still run ≤ v1.17.3.

Per the batch brief, 4b is resolved by writing the retirement condition and steps
into the workflow header comment and `docs/file-formats.md`, not by stopping v1.

## Item 5 — the tag-retire confirmation

- `STR_CONFIRM_DELETE_TAG` is `english.yaml:391`, "Delete this tag from every
  highlight in this book?". **`spanish.yaml` has no such key**, so Spanish shows the
  English string today.
- It is shown from **two** places, both of which call `STUDY.retireTag`:
  `TagFilterActivity.cpp:105` (`retireTag` at `:113-129`) and
  `TagPickerActivity.cpp:251` (`retireTag` at `:256-…`).
- `StudyStore.h:98-103`: retire removes the tag from the pickers, keeps it
  resolving for display, drops it from the open publication's passages, leaves
  tag-less passages UNLABELLED, and never removes a passage.
- The popup's action button is `tr(STR_DELETE)` in both call sites
  (`TagFilterActivity.cpp:104`, `TagPickerActivity.cpp:250`); the issue asks only
  for the prompt text.

## Scope and tier

The work spans `src/activities` (ui), `src/network/WolWeekScan` and
`MeetingWeekPrefetch` (net), `src/SettingsList.h` if item 2 touches the settings,
two YAMLs, the catalog workflow and `docs/file-formats.md`. No on-disk format or
persisted value changes: the meeting-day settings persist as ordinals
(`CrossPointSettings.h:74-82`) whatever their labels are. The tier stays `heavy`;
nothing here calls for a migration.
