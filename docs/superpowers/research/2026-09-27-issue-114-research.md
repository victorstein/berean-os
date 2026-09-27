# Issue #114 — study sleep screen: research

Branch `feat/114-study-sleep-screen` at `af119732`, which is `origin/main` (`git log HEAD..origin/main`
was empty on 2026-09-27). Every `file:line` below was read on that commit.

## 1. Where the issue's citations have drifted

| Issue says | What is there |
|---|---|
| `renderDefaultSleepScreen` at `SleepActivity.cpp:635-656` | `src/activities/boot_sleep/SleepActivity.cpp:637-660` |
| `recentSleepImages` at `CrossPointState.h:22-24` | `src/CrossPointState.h:23-25` |
| `SLEEP_SCREEN_MODE` at `CrossPointSettings.h:53-63` | `src/CrossPointSettings.h:55-65` |
| Stream passages "through the existing `HalFileReader` path (`src/study/PassageFile.cpp:16-47`)" | There is no `HalFileReader` type in `lib/` or `src/`. `PassageFile.cpp` is 39 lines; `load` (`:18-24`) goes through `PersistableStoreBase::loadAdopting` with `readDocFromFileStreamed`, which is `BufferedFileReader` (512 B, `lib/Serialization/PersistableStore.cpp:16,154`) feeding a private `JsonFileReader` adapter (`PersistableStore.cpp:22-32`) into a plain `deserializeJson` with **no filter**. |
| `ChapterCompletion::readCountInBook()` | It **does** exist, contrary to the batch note: declared `lib/StudyStore/StudyStore/ChapterCompletion.h:42`, defined `ChapterCompletion.cpp:77-81` (loops `isRead` over `canonicalChapterCount(book)`), tested at `test/chapter_completion/ChapterCompletionTest.cpp:82-83`. `readCount()` is `:41` / `.cpp:71-75`. No new StudyStore API is needed for the progress strip. |
| Title for a non-Bible passage "from `PubKeyRegistry`" | `PubKeyRegistry` stores only path → `{symbol, issue, language}` (`src/study/PubKeyRegistry.h:12-31`, keys `s`/`i`/`l` at `PubKeyRegistry.cpp:36-38`). It holds **no title**. There is no pubkey → title lookup anywhere in `src/`. See §7. |
| Date from `HalClock::getDate` "with the `clockUtcOffsetQ` offset applied" | `getDate` returns UTC **year/month/day only** (`lib/hal/HalClock.h:30-41`, `HalClock.cpp:42-52`). Shifting a date by an offset needs the UTC hour and minute too. See §5. |

## 2. Who owns the behaviour today

**Entry.** `enterDeepSleep` (`src/main.cpp:253-291`) sets `APP_STATE.lastSleepFromReader`, clears
`showBootScreen`, saves `APP_STATE` atomically (`:265`), sets `deepSleepInProgress`, then
`activityManager.goToSleep(fromTimeout)` (`:271`), which runs `SleepActivity::onEnter`. After it
returns: Quick Resume frame handling (`:273-278`), WiFi off, `display.deepSleep()`, deep sleep.

**`SleepActivity::onEnter`** (`SleepActivity.cpp:490-550`):

1. Clears display inversion (`:498`).
2. `QUICK_RESUME` (or timeout + `QUICK_RESUME_AFTER_TIMEOUT`) → `renderLastScreenSleepScreen` (`:500-507`).
3. `TRANSPARENT_CUSTOM` → popup preserving the frame, release SD font caches, overlay (`:509-523`).
4. Otherwise draws the "Entering sleep" popup (`:526-532`), then `switch (SETTINGS.sleepScreen)`
   (`:534-549`): `BLANK`, `CUSTOM`, `COVER`, `COVER_CUSTOM`; **`default:` →
   `renderDefaultSleepScreen`** (`:547-548`), which is where `DARK`, `LIGHT` and any unknown value land.

A new `STUDY` case slots into that switch. Nothing else is needed there, because `default:` already
supplies the fallback target.

**`renderDefaultSleepScreen`** (`:637-660`): `clearScreen`, `drawBibleCover()` or
`berean_mark::draw(...,120)` centred (`:645-647`), "Berean" bold `UI_10` at `pageHeight-90`, "Sleeping"
`SMALL_FONT` at `pageHeight-65`, `invertScreen()` unless `LIGHT` (`:652-654`),
`displayBuffer(HALF_REFRESH)`. It is 1-bit only, with no grayscale pass.

**Settings enum.** `SLEEP_SCREEN_MODE` has `DARK=0 … TRANSPARENT_CUSTOM=7, SLEEP_SCREEN_MODE_COUNT`
(`CrossPointSettings.h:55-65`). Its default is `sleepScreen = DARK` (`:242`). The labels are a
vector sized `SLEEP_SCREEN_MODE_COUNT`, indexed by enum value (`SettingsList.h:233-241`), registered
as `SettingInfo::Enum(STR_SLEEP_SCREEN, …, "sleepScreen", STR_CAT_DISPLAY)` (`:256-257`).

**Compatibility is already handled by the loader.** `CrossPointSettings.cpp:166-169` clamps every ENUM
to `info.enumValues.size()` and falls back to the field default. So:
- an older build reading `sleepScreen = 8` loads it as `DARK`, and nothing fails;
- this build reading values 0–7 keeps their meaning, provided `STUDY` is appended as `= 8` before `_COUNT`.

The web settings page hardcodes only `SLEEP_SCREEN_MODE = { QUICK_RESUME: 6 }`
(`src/network/html/SettingsPage.html:316-318`). The option list itself comes from the device, so an
appended value shows up there with no HTML change.

Other readers of `sleepScreen`: `src/main.cpp:259` (Quick Resume check only).

## 3. The nearest existing example: the "no immediate repeats" random picker

`selectRandomSleepFile` (`SleepActivity.cpp:414-450`) is the model for the pick:

- Iterates a directory with `dir.openNextFile()` via `findNextValidSleepImage` (`:386-412`), counting valid files.
- Picks `random(fileCount)`, re-rolls up to 20 times while `isRecentSleepIndex` over a window of
  `min(recentFill, fileCount-1)` (`:430-436`). That window is what lets the only candidate repeat.
- `rewindDirectory` and walks to the chosen index (`:438-441`).
- `pushRecentSleepIndex` then `APP_STATE.saveToFileAtomic()` (`:447-448`). **The ring is persisted
  from inside the sleep path**, after `main.cpp:265` already saved once.

`random(n)` is Arduino-ESP32 3.3.7's `WMath.cpp:52-62`: `esp_random() % howbig` when hardware RNG is in
use. That is `package.json` `"version": "3.3.7"` under
`~/.platformio/packages/framework-arduinoespressif32`. No `randomSeed`/`esp_random` call exists
elsewhere in `src/`/`lib/` (grep empty).

**The ring.** `CrossPointState` (`src/CrossPointState.h:15-47`, `.cpp:9-28,30-44`):
`SLEEP_RECENT_COUNT = 16`. There are two parallel `uint16_t[16]` rings (`recentSleepImages`,
`recentOverlaySleepImages`) with `pos`/`fill`, and free helpers `isRecentIndex`/`pushRecentIndex`
(`.cpp:9-26`) that walk newest-first over `min(checkCount, fill)`. It serialises as arrays plus
scalars (`.cpp:46-61`). `fromJson` clamps array length, pos and fill (`.cpp:71-94`).
`FORMAT_VERSION = 1`, `SAVE_BUDGET = 2048` against a stated ~1,190 B worst case (`.h:33-35`).
`fromJson` refuses an unknown `v` (`.cpp:64-68`). An added key needs no bump: an older build
ignores unknown keys, which is how `bibleCoverPath` coexists. Because the rings hold `uint16_t`
**array positions**, the issue's "key on something stable" means a third ring whose element is a
hash of `pubkey + u`, not a reuse of `recentSleepImages`. `CrossPointState` has no host test (no
`test/*state*`), so ring logic that needs testing has to live in a pure header.

## 4. The study stores the screen reads — and the rename hazard

All three store loaders the UI normally uses go through `PersistableStoreBase::loadAdopting`, which
**renames** a `<path>.tmp` into place when the primary is missing (`PersistableStore.h:84-115`,
comment at `:107-114`):

- `PassageFile::load` → `loadAdopting(…, readDocFromFileStreamed, …)` (`src/study/PassageFile.cpp:18-24`)
- `ChapterCompletionFile::load` → `loadAdopting(…, readDocFromFileChecked, …)` (`src/study/ChapterCompletionFile.cpp:18-26`); the launcher calls this (`LauncherActivity.cpp:83-97`)
- `TagPaletteFile::load` → `loadAdopting(…, readDocFromFileChecked, …)` (`src/study/TagPaletteFile.cpp:18-23`)

The brief says "never write to the store from this path". A `.tmp` promotion is a write, so the sleep
path should not call these. It can instead call the **non-adopting** readers directly —
`PersistableStoreBase::readDocFromFileStreamed` / `readDocFromFileChecked` (public,
`PersistableStore.h:77-81`, both returning `DocReadStatus` `Ok|Missing|Unreadable|ParseError`,
`lib/Serialization/DocReadStatus.h:8-13`) — and hand the result to the pure `study::*::fromJson`. That
needs no change under `src/study/` or `lib/StudyStore/`. The one gap is that neither reader accepts a
`DeserializationOption::Filter`: `readDocFromFileStreamed` calls a bare `deserializeJson(doc, reader)`
(`PersistableStore.cpp:156`), and `JsonFileReader` is file-local. See §7.

**Passage file shape** (`lib/StudyStore/StudyStore/PassageDoc.cpp:190-216`): `{"v":1|2,"p":[{…}]}`. Each
row carries `u` start unit (compact string), `e`, `f`, `d`, `s`, `x` snippet, `r` reference, optional
`g`, `t` tag-id array, and `k` links. The files are `/.berean/passages/<pubkey>.json`
(`SdPaths.h:30`, `PassageFile.cpp:16`), so **the pubkey is the filename stem** and needs no second
lookup. Caps: `MAX_SNIPPET_BYTES = 120`, `MAX_REFERENCE_BYTES = 48`, `MAX_TAGS_PER_PASSAGE = 8`,
`SAVE_BYTE_BUDGET = 200000` per file (`PassageDoc.h:30-34`). `fromJson` rejects a future `v`
(`PassageDoc.cpp:222-`) and re-truncates `x`/`r` with `utf8SafeSummary` (`:237-238`). The user's real
store is 63 passages (`PassageDoc.h:37-38`). A filter must keep `u` as well as `x`/`r`/`t`, because
`u` is half of the stable ring key the issue asks for.

**Nothing iterates `/.berean/passages/` today.** Every `PassageFile::load` caller works on one known
pubkey (`src/study/StudyStore.cpp:49`, `MigrationRunner.cpp:278,380`). Cross-publication enumeration
is new. Its shape comes from `findNextValidSleepImage`'s `openNextFile` loop (§3) and
`src/util/NextBookFinder.cpp:54`.

A per-element streaming alternative already exists: `lib/JsonParser/StreamingJsonParser.h:6-30`, a
SAX-style parser with function-pointer callbacks and a fixed 512 B token buffer, used by
`src/network/PubMediaJson.h`. It would hold one passage at a time instead of one filtered file.

**Tags.** `/.berean/tags.json` (`SdPaths.h:23`). `TagPalette::name(TagId)` returns the name for any
id ever allocated, active or retired, and empty if unknown (`TagPalette.h:53-54`). `UNLABELLED = 0`
(`:31`). `MAX_TAG_NAME_BYTES = 24` (`:39`). The file is small (<2 KB, `TagPaletteFile.h:13-15`), so
`readDocFromFileChecked` is safe for it.

**Completion.** `/.berean/completion/bible.json` (`SdPaths.h:32`, `ChapterCompletionFile.cpp:16`,
key `study::BIBLE_PUB_KEY`). `BIBLE_BOOK_COUNT = 66`, `canonicalChapterCount(book)`,
`CANONICAL_CHAPTER_TOTAL = 1189` (`ChapterCompletion.h:19-26`). The launcher's subtitle
(`LauncherActivity.cpp:83-97`) loads the record, returns silently unless `Loaded|RecoveredFromTemp`,
and formats `tr(STR_BIBLE_CHAPTERS_READ)` with `readCount()` and `CANONICAL_CHAPTER_TOTAL`. That is
the count to reuse. A book is "finished" when `readCountInBook(b) == canonicalChapterCount(b)`.

## 5. Clock and date line

- `HalClock::getDate` gives UTC Y/M/D and returns false with no RTC or a stopped oscillator. That is
  "the only trustworthy 'never set' signal" (`HalClock.h:30-41`, `.cpp:42-52`). It is the right
  gate for "leave the date out".
- `getTime` gives UTC hour/minute, **cached for 10 s** and falling back to the cache on a read error
  (`HalClock.cpp:15-40`). `formatTime` applies the offset `(q-48)*15` minutes, clamped at `q ≤ 104`,
  wrapping only within the day (`:54-79`). **No existing code shifts a date across midnight.**
- The SDK's `Rtc::DateTime` has year…second and `weekday` 0=Sunday (`freeink-sdk/libs/hardware/Rtc/include/Rtc.h:19-27`),
  but `HalClock` exposes neither the full `DateTime` nor the weekday.
- Pure date helpers: `daysFromCivil` is file-local in `src/network/WolWeekScan.cpp` (`:22-29`), and
  `isoWeekFromUtcDate` is public (`WolWeekScan.h:28`, tested in `test/wol_week_scan`). Its consumers
  call `getDate` with no offset (`LauncherActivity.cpp:178`, `MeetingsActivity.cpp:40`,
  `MeetingWeekPrefetch.cpp:31`).
- Options that stay inside the `ui` surface: call `getDate` and `getTime` back to back, then shift
  the civil date by `floor((h*60+m+off)/1440)` days in a pure helper that derives the weekday from
  days-since-epoch. The two reads are not atomic; across UTC midnight that can put the date off by
  a day for one render. A single-read `HalClock` API would be a `hal` surface change.
- Localised names: `STR_MONTHS_SHORT` already exists as a space-separated word list
  (`english.yaml:439` "Jan Feb …", `spanish.yaml:381` "ene feb …"), consumed by
  `catalog::formatIndexDate(isoDate, monthsShort, out, size)` via `wordAt`
  (`lib/Catalog/Catalog/CatalogStamp.cpp:65-85`, test `test/catalog_stamp/CatalogStampTest.cpp:84-87`).
  **That word-list pattern is the model** for a new weekday list. No weekday strings exist yet (grep
  of `english.yaml` for day names: none).

## 6. Rendering facts

- The largest built-in serif is `NOTOSERIF_18_FONT_ID` (`src/fontIds.h:7`), with an italic face
  (`src/main.cpp:86-91`), registered at `:323` inside `#ifndef OMIT_FONTS`. No build env sets
  `OMIT_FONTS` (`grep OMIT platformio.ini`: none).
- `GfxRenderer::wrappedText(fontId, text, maxWidth, maxLines, style)` returns lines
  (`lib/GfxRenderer/GfxRenderer.h:299-303`). `drawCenteredText` (`:280`) and `drawRoundedRect`
  (`:239-241`) cover the pill. `fillRectDither(…, Color)` with `LightGray`/`DarkGray` (`:27,244`) is
  the 1-bit way to get "grey". Text drawing is `bool black` only (`:283`), so a "grey" quote mark on
  the 1-bit sleep path means dithering or a grayscale pass, not a colour argument.
- `berean_mark::draw(renderer, x, y, size)` (`src/activities/boot_sleep/BereanMark.h:15`).
- `releaseSdFontCachesForDecode` (`SleepActivity.cpp:480-486`) already logs `ESP.getFreeHeap()`
  with `PRIu32`. That is the specifier to reuse for the issue's heap-before/after check under `-Wformat`.

## 7. Open points the spec has to settle

1. **Publication title for a reference-less passage.** Neither `PubKeyRegistry` nor any other store
   maps pubkey → title. The issue's fallback therefore needs one of: showing the pubkey's symbol,
   a reverse registry walk plus reading the EPUB/`RecentBooks` title (heavy on the sleep path), or
   leaving the line out. This is a scope decision.
2. **Filtered streaming read.** The filter needs either a new filtered reader in `lib/Serialization`
   (not in my read-only list, but shared) or `StreamingJsonParser` (§4). Both avoid `src/study`.
3. **Ring key.** Hash `pubkey + "/" + u` (the compact unit string) into a `uint16_t` or `uint32_t`
   ring appended to `CrossPointState` as new JSON keys. No `FORMAT_VERSION` bump is needed (§3), and
   the budget headroom is ~850 B.
4. **Grey quote mark**: dithered fill, or accept black. **Invert**: `renderDefaultSleepScreen`
   inverts for every mode but `LIGHT`, and the issue asks for a light background.

## 8. Tool and package versions (installed, not remembered)

| | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `pio --version` |
| Platform | pioarduino platform-espressif32 55.03.37 | `platformio.ini:15` |
| Arduino-ESP32 core | 3.3.7 | `framework-arduinoespressif32/package.json:3` |
| ArduinoJson | 7.4.2 (device and host tests) | `platformio.ini:154`; `test/CMakeLists.txt:28-31`; `version.hpp:7` in an installed copy. `DeserializationOption::Filter` is present (`Deserialization/Filter.hpp:13`). |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:14-17` |
| CMake | 4.4.2 | `cmake --version` |

This worktree has no `.pio/libdeps` yet, so the ArduinoJson source above was read from a sibling
worktree's install of the same pinned version.

## 9. Host-test model

Pure header beside the activity, with no activity constructed, and `${REPO_ROOT}/src` as the only
extra include: `test/launcher_bible/CMakeLists.txt` (testing `src/activities/launcher/LauncherBible.h`)
and `test/return_stack` (`src/activities/reader/ReturnStack.h`). The sampler, ring skip and date
formatting fit that shape, as a new `src/activities/boot_sleep/*.h` plus `test/<name>/`. Per
`.claude/agents/ui-dev.md`, `test/CMakeLists.txt` (`add_subdirectory`) and the translation YAMLs are
shared append points: the line goes in the PR body for the orchestrator.

## 10. Tier

It stays `heavy`. This finding adds nothing that requires a higher tier (heavy is the top). The
work touches settings (`CrossPointSettings.h`, `SettingsList.h`) and a persisted `CrossPointState`
field, both anticipated in the brief. The open points in §7 are decisions, not a surface change,
unless the spec chooses a `HalClock` or `lib/Serialization` API.
