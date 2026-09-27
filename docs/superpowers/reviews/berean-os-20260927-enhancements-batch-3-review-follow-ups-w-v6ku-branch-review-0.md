Tiers: t1 heavy, t2 heavy, t3 standard, t4 heavy, t5 heavy, t6 heavy, t7 heavy, t8 heavy, t9 heavy, t10 heavy

# Whole-branch review: enhancements batch 3 (run v6ku)

Reviewed `main` at `4b1697a5` (release 1.19.3). That commit includes PRs #153, #155, #159, #161, #163, #167, #169, #171, #174 and #176, plus #149, #151, #164 and #165.

## Build and test (merged tree)

- `pio run -e x4pro` via `pio-locked.sh`: **SUCCESS**. It was a full compile: 532 objects, 255 of them from `src/` or `lib/`.
  - RAM: 19.9% (65,172 of 327,680 B).
  - Flash: 83.8% (5,494,910 of 6,553,600 B).
- Warnings: **zero from `src/` or `lib/`**. The one warning in the log is in a third-party library: `.pio/libdeps/x4pro/WebSockets/src/WebSocketsClient.cpp:573` (`-Wdeprecated-declarations`).
- i18n (`gen_i18n.py`): 477 keys, 476 used, **1 unused**. The unused key is `STR_HIGHLIGHTS_TOO_LARGE`, which already had no caller at `dc97292e`, before the run. The run added no orphaned keys.
  - Every key the run added to `english.yaml` is also in `spanish.yaml`.
  - Each Spanish format string takes its arguments in the same order as the English one.
- Host tests: `ctest --test-dir build/test` passes **1139/1139**. The first build hit the known gtest race, and a rebuild fixed it.
- Script tests: `python3 -m unittest discover -s scripts/tests` passes **37/37**.
- CI on `27bcfa42` and `be41881e`: green. Release 1.19.3 published its firmware.

## Seams checked and found sound

- **`publication::Result::NoEpubEdition` (#163) against its callers:**
  - `failureMessage` handles every enumerator (`src/network/PublicationDownloader.cpp:260-277`).
  - The `switch` in `BibleDownloadActivity`, added by #161, lists every enumerator explicitly, including `NoEpubEdition` (`src/activities/network/BibleDownloadActivity.cpp:252-279`).
  - `MeetingDownloadActivity.cpp:293-306` and `CatalogSearchActivity.cpp:409-424` compare against `Result` values and fall through to `failureMessage`.
  - No `switch` is left non-exhaustive.
- **Register-on-open after the #171 extraction:** the #161 block survived the bookmarks move intact, at `src/activities/reader/EpubReaderActivity.cpp:231-240`.
  - `ReaderBookmarks` (`src/activities/reader/ReaderBookmarks.cpp`) reproduces the load latch, the toggle with rollback, and the toast.
  - It still draws through `GUI.drawToast` (`EpubReaderActivity.cpp:1281-1282`), as #153 set it up.
  - Nothing was lost or duplicated.
- **#153 toast against the new screens:**
  - `BibleDownloadActivity` posts "Download cancelled" through `PostedMessage::post` (`:259`) and draws the queue after `displayBuffer` (`:401-402`), like the launcher (`LauncherActivity.cpp:496-497`) and `UiListActivity.cpp:167-168`.
  - Neither #167 nor #169 routes an outcome message through `drawPopup`.
- **Web routes after the #174 split:**
  - Every route that existed before the split is registered once:
    - `FileRoutes.cpp:41-53`
    - `FontRoutes.cpp:15-20`
    - `SettingsRoutes.cpp:14-17`
    - `CrossPointWebServer.cpp:104-119`
  - Dot-dir protection is intact. The pre-split file made 12 protection checks (`isProtectedWebPath` or `isProtectedWebName`). The same 12 are now at `FileRoutes.cpp:79,124,179,315,457,502,508,581,585,708` and `CrossPointWebServer.cpp:588`.
  - The settings API is generic over `getSettingsList()` (`SettingsRoutes.cpp:25-116`). `sleepScreen` therefore exposes STUDY (value 8), and `midweekMeetingDay`/`weekendMeetingDay` are exposed. A POST is range-checked against `enumValues.size()` (`SettingsRoutes.cpp:149-160`).
- **`-Wall` middleware (#176) against the #174 files:**
  - `is_repo_source` accepts everything under `src/` (`scripts/repo_warnings.py:29-31`). That covers `src/network/*Routes*` and `src/main.cpp`, which includes `src/boot/BootDecisions.h`.
  - The script sits in `[base]`'s `extra_scripts` (`platformio.ini:131-138`), so both envs get it.
  - #176 merged after #174, so its census covered the split files.
- **The #174 `setup()` split** keeps the init order. `chooseBootRoute` (`src/boot/BootDecisions.h:57-73`) reproduces the old `if`/`else` chain branch for branch. The SD-failure early return remains (`src/main.cpp:633`).
- **WifiSession (#155):**
  - Every network screen holds one:
    - `BibleDownloadActivity.cpp:53`
    - `CatalogSearchActivity.cpp:73`
    - `MeetingDownloadActivity.cpp:39`
    - `ClockSyncActivity.cpp:20`
    - `FontDownloadActivity.cpp:40`
    - `OtaUpdateActivity.cpp:77`
    - Settings → Wi-Fi networks (`SettingsActivity.cpp:308-311`), as decided
  - On this touch board the teardown calls `silentRestart()`, which stops Wi-Fi without rebooting (`src/main.cpp:147-167`).
  - #169's Meetings screen reaches Wi-Fi only through `MeetingDownloadActivity`.
- **Persisted formats:**
  - `STUDY = 8` is appended after `TRANSPARENT_CUSTOM` (`src/CrossPointSettings.h:64`). An older build clamps it to the field default through the generic enum clamp (`src/CrossPointSettings.cpp:169`).
  - The meeting-day keys are new, so an older build ignores them.
  - The `CrossPointState` ring loads as empty from an older file (`src/CrossPointState.cpp:108-119`), and its worst case stays under `SAVE_BUDGET` 2048.
  - `recent.json` stays at v1. The budget shrank, and both directions load.
  - The catalog v2 index is published beside v1, not in place of it. The stamp compares the version (`lib/Catalog/Catalog/CatalogStamp.cpp`).
  - The `catalog` release now carries both `catalog-{S,E}.v2.txt.gz` files and their probe caches, from the dispatched run on 2026-09-27. The "no index until the first run" gap that #163 flagged is closed.
- **Shared date words:** #163, #167 and #169 share `catalog::wordAt`/`copyOut` (`lib/Catalog/Catalog/CatalogLabel.cpp`, `MeetingWeekView.cpp:28-31`, `StudySleepPick.h:208-209`). There are not three separate list parsers.
- **Cover thumbnails:** #169 moved the launcher's helper into `src/util/CoverThumb.{h,cpp}`. The launcher (`LauncherActivity.cpp:137,162,331`) and the Meetings screen (`MeetingsActivity.cpp:238-242,385`) both call it. There are not two copies (but see MINOR 3).

## Findings

### MINOR 1: `USER_GUIDE.md` contradicts the code shipped in #161, #167 and #169

- **Bible tile, §4:** `USER_GUIDE.md:98-99` says that with no Bible, the tile "opens the file browser instead". Since #161, the tile opens `BibleDownloadActivity`'s download offer (`src/activities/launcher/LauncherActivity.cpp:523-539`), and the file browser is only its "Choose a file" button (`BibleDownloadActivity.cpp:118-126`). `USER_GUIDE.md:246` (§12) repeats the old route.
- **Sleep screen:** the Sleep screen list (`USER_GUIDE.md:264`) and the §14 mode table (`:321-330`) leave out **Study**, which is live at `src/SettingsList.h` (`sleepScreenValues[STUDY]`) and `src/CrossPointSettings.h:64`.
- **System settings:** the System tab (`USER_GUIDE.md:307-317`) leaves out **Midweek meeting day** and **Weekend meeting day** (`src/SettingsList.h:397-400`).
- **Meetings, §10:** `USER_GUIDE.md:210-211` still describes the Meetings tile as "lists the same two publications", not the week card #169 shipped.

All three PRs changed user-visible behaviour and none of them touched the guide. Only docs are affected; the code is correct.

### MINOR 2: Weekday names are translated three times, in two different orders

- `STR_WEEKDAYS` starts on Sunday, with full names (`lib/I18n/translations/english.yaml:481`, from #167).
- `STR_WEEKDAYS_NARROW` starts on Monday, with letters (`:425`, from #169).
- `STR_MONDAY`…`STR_SUNDAY` are seven separate keys (`:430-436`, from #169).

`STR_WEEKDAYS` and the seven single keys carry the same words, so a translator has to keep 14 entries in step. The Spanish file already differs in capitalisation between them (`spanish.yaml:423` is lowercase; `STR_MONDAY` at `:375` reads "Lunes").

The two lists also start on different days, Sunday against Monday. `StudySleepPick.h:177-180` and `MeetingsActivity.cpp:199` each index in their own order. That is correct today, but a future caller that picks the wrong list is off by one day with no warning.

### MINOR 3: Duplicated helpers the run introduced or left unfolded

- **Two civil-date implementations.**
  - #167 added `study_sleep::CivilDate`, `daysFromCivil`, `civilFromDays` and `weekdayFromDays` (`src/activities/boot_sleep/StudySleepPick.h:147-180`). Its own comment says it duplicates the file-local copy in `src/network/WolWeekScan.cpp`.
  - #169 then added a second `CivilDate`, `isoWeekday`, `addDays` and `localDateFromUtc` (`src/network/WolWeekScan.h:30-50`).
  - The UTC-offset day shift is written twice, at `StudySleepPick.h:198-204` and `WolWeekScan.cpp:120-125`, including the same `> 104` clamp.
  - #169's PR body acknowledges this as a follow-up.
- **A third cover-thumbnail copy.** #169 extracted `CoverThumb`, but `PublicationsActivity::loadThumb` (`src/activities/catalog/PublicationsActivity.cpp:81-100`) still does `getThumbBmpPath`, `load(true, true)`, `generateThumbBmp` and `parseHeaders` by hand, which `CoverThumb::pathFor`/`sizeOf` now cover.
- **The publication-language label** is written twice: `CatalogSearchActivity.cpp:62-63` (#163) and `BibleDownloadActivity.cpp:64-65` (#161).
- **Two copies of the download progress pipeline.** `BibleDownloadActivity` copies `MeetingDownloadActivity`'s input pumping in the progress hook and its repaint throttle (`BibleDownloadActivity.cpp:199-222`).

None of these copies has diverged yet. They are debt, not bugs.

### MINOR 4: Follow-ups named in merged PR bodies are not tracked anywhere

The repository has **no open issues** (`gh api repos/victorstein/berean-os/issues?state=open` returns 0), but merged PRs defer these:

- Fold the two civil-date implementations (#169 "Follow-ups").
- Move "this week" to local time for the cache key, the launcher, the downloader and the prefetch together (#169, spec A3).
- Retire the v1 catalog index (#163: "Follow-up to file").
- Nothing reachable opens the file browser once a Bible is on the card. The only entries are `BibleDownloadActivity.cpp:124`, reached only with no Bible, and `BmpViewerActivity.cpp:260` (#159 "Follow-ups"). A sideloaded book outside the download folder and `/` is unreachable on the device.
- `USER_GUIDE.md` documents "Back to file browser" and "Remove read books from recents", which no `SettingInfo` exposes (#159). They are still at `USER_GUIDE.md:305,311`.

Without issues, these exist only in squash-commit prose.

### MINOR 5: An `AGENTS.md`/`CLAUDE.md` citation broken during the run

- `AGENTS.md:71` cites `src/MappedInputManager.cpp:266,301` for the Back swipe.
  - Those lines were correct at `dc97292e`.
  - #159 deleted 73 lines from the file, and `wasBackGesture` is now at `:222`, with its `wasReleased` use at `:257`.
  - This came from the PR whose item 9 was fixing stale citations in this file.
- Several neighbouring citations were already stale before the run and are still wrong:
  - `platformio.ini:162,167`: the board is at `:166` and PSRAM at `:171`.
  - `HalGPIO.cpp:166`: `hasHomeKey` is at `:247`.
  - `ReaderUtils.h:129`: `isTouchMenuTap` is at `:140`.
  - `release-publish.yml:106`: the repository guard is at `:102`.

## Verdict rationale

No BLOCKER and no MAJOR. Every cross-PR seam in the brief holds:

- `Result` exhaustiveness
- register-on-open after the bookmarks extraction
- toast routing
- route registration and dot-dir protection after the split
- `-Wall` coverage of the new files
- rollback safety of every changed persisted format

The merged tree builds with no repo warnings, and every suite passes. The findings are docs drift, duplicated helpers and untracked follow-ups. None of them reverses a decision, changes scope or needs the user to decide.

BLOCKERS: 0
MAJORS: 0
MINORS: 5
VERDICT: CLEAR
