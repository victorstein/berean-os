# Issue #203 research: Home redesign for a reference Bible

Branch `feature/203-home-redesign`, based on `75c65e93` (PR #221, issue #201). Every claim below
was read at that commit in this worktree.

## Dependencies are in

- #201 (places and intents) and #202 (CoverBand) are both `CLOSED`
  (`gh issue view 201|202 --json state`).
- `CoverBand` landed in `c72df634` (PR #209). Places and intents landed in `75c65e93` (PR #221).
  `Masthead`, a second CoverBand consumer, landed in `06e52c84` (PR #219, issue #204).
- `hpipe status` shows every other task in this run (t1–t9, t11) as `done`. No sibling holds
  launcher files.

## Which files own Home today

| Concern | File |
|---|---|
| Home screen | `src/activities/launcher/LauncherActivity.{h,cpp}` (100 + 494 lines) |
| Clean-entry paint rule | `src/activities/launcher/LauncherRefresh.h:15` `launcherNeedsCleanPaint` |
| Bible lookup rules | `src/activities/launcher/LauncherBible.h` (`resolveBible`, `isCdnNamedCopyOf`) |
| Construction | `ActivityManager::goHome` (`src/activities/ActivityManager.cpp:242-244`); `isHomeActivity()` returns true (`LauncherActivity.h:31`) |

## Current control flow (`LauncherActivity.cpp`)

1. `onEnter` (63-70) runs `computeLayout()`, then `resolveTargets()`, then `requestUpdate()`.
   Layout comes first because a cover thumbnail is generated at the tile's drawn size.
2. `resolveTargets` (72-143):
   - Loads `RECENT_BOOKS` and sets the resume strip to `recents[0]`, whatever book that is
     (76-80).
   - Resolves the Bible through registry, then card scan, then recents (90-103).
   - Gets `bibleCoverPath` from `CoverBand::thumbPathFor(biblePath, tile.w, tile.h, generatedAny)`
     (108) and mirrors it into `APP_STATE.bibleCoverPath` for the sleep screen (111-114).
   - Resolves the meeting publication: this week's from `MeetingWeekCache`, preferring the
     Watchtower (148-167), then the registry, then a card scan.
3. `computeLayout` (246-291) places five rects. Four tiles sit between the header and the resume
   strip: Bible (double weight), Meetings | Publications, and Settings. The resume strip sits at the
   foot. Positions come from `getOrientedViewableTRBL` and `metrics.topPadding/headerHeight`, with
   file-local constants `TILE_GAP = 10`, `TILE_PADDING = 8`, `TILE_RADIUS = 8` (50-53).
4. `render` (375-405) draws the theme header (`GUI.drawHeader`, `STR_BEREAN`), then:
   - the Bible with `drawCoverTile` (CoverBand with a plate, `BOOK_TITLE_BAND`);
   - Meetings with `drawCoverTile` (`MAGAZINE_MASTHEAD_BAND`);
   - Publications and Settings with `drawTile` (icon and label);
   - the resume strip.

   It then calls `displayBuffer(HALF_REFRESH)` if `launcherNeedsCleanPaint(cleanInitialRefresh,
   firstRenderDone)`, otherwise `FAST_REFRESH`, followed by `PostedMessage::drawNext` and
   `firstRenderDone = true`.
5. `loop` (463-494) handles input:
   - A tap hit-tests the rects and activates the tile.
   - Next and Previous move `selected` through `ButtonNavigator`.
   - Confirm activates. `mappedInput.update()` is deliberately not called here (464-467).
6. `activate` (407-427):
   - Bible calls `goToReader(biblePath)`, or pushes `BibleDownloadActivity` if there is no Bible.
   - Meetings pushes `MeetingsActivity`.
   - Publications pushes `PublicationsActivity` and re-resolves on return.
   - Settings calls `activityManager.goToSettings()`.
   - Resume calls `goToReader(resumePath, true)`.

Today's Home shows **no Bible progress**: `render` draws only titles and subtitles, and
`bibleSubtitle` is the edition name (`bibleTitleFor`, 195-201). The "chapters read" line the mockup
lists was removed in `3591e3fe` (PR #197, issue #194), per `git log -S "chapters read" -- src/activities/launcher`. This acceptance criterion is about not adding progress back.

## What the redesign plugs into

### Cover hero: `CoverBand` (`src/components/CoverBand.h`)

- `draw(renderer, coverPath, band, Style{focusBand, cornerRadius, plateHeight})` draws the cover at
  1:1, cropped and never scaled, and lays an opaque plate across the band's foot. It returns false
  with nothing drawn if the cover is missing, too small, or unreadable. `plateRect()` gives the
  rectangle the label goes in.
- `thumbPathFor(bookPath, w, h, generatedAny)` returns the thumbnail, generating it when missing.
  The cache key is `thumb_<thumbHeightFor(w,h)>.bmp`, where `thumbHeightFor = max(h, w/0.6)`
  (`CoverBandGeometry.h:46-48`). The hero's **width** therefore picks the cached file.
- `Masthead` says it draws a 464-wide band "at the launcher Bible tile's left edge and width, so it
  asks for the same `thumb_773.bmp` the launcher already caches" (commit `06e52c84` body;
  `464 / 0.6 = 773`).
  - A hero kept 464 px wide at the same left edge reuses that file.
  - A different width generates a new one on first entry, which means opening the EPUB and showing
    a popup.
  - `APP_STATE.bibleCoverPath` then changes too, and the sleep screen follows it.

### Places and intents (#201)

- `PLACES.getPlaces()` (`src/PlacesStore.h:42`) holds up to `MAX_PLACES = 12` places
  (`src/util/PlacesDoc.h:22`), newest first, and loads itself on first use (`PlacesStore.cpp:27-29`,
  `48-51`). Home only reads it. Each `Place` (`src/Place.h`) carries:
  - `unit`
  - `reference`, e.g. "Revelation 21:4", capped at 48 bytes
  - `chapterOnly`
  - `spineIndex`
  - `visibleTextOffset`
- `PlacesDoc::pickRecent(places, onScreen, out, max)` (`PlacesDoc.h:67-69`) is the existing
  "next N, skipping the one on screen" helper, used by the reader menu's Recent chips.
- `ActivityManager::goToReader(path, allowFastInitialRefresh, const ReaderEntryIntent&)`
  (`ActivityManager.h:86`, `.cpp:208-229`) takes an intent.
  - `ReaderEntryIntent` (`src/activities/reader/ReaderEntryIntent.h`) has the kinds `OpenAt`
    (`openAt(const Place&)`), `BookGrid`, `Search` and `Tags`.
  - `route()` maps each kind to Locate, ChapterGrid (or TocList for a non-Bible), Search or
    Highlights. OpenAt and Search become `None` outside the Bible.
- The intent is consumed once in `EpubReaderActivity::onBookLoaded`
  (`EpubReaderActivity.cpp:934-968`):
  - Locate runs `STUDY.locatePlace(unit, spineHint)` and falls back to `STR_LINK_TARGET_NOT_FOUND`.
  - The grid and search open with `CancelTo::Page`.
  - Tags calls `openHighlights()`.
- **No call site passes an intent yet.**
  - Every `goToReader(` caller uses the defaults: `main.cpp:586,600`, `Activity.cpp:15`,
    `ReaderActivity.cpp:100`, `LauncherActivity.cpp:422,431`, `MeetingsActivity.cpp:434` and
    `PublicationsActivity.cpp:236` (from `grep -rn "goToReader(" src`).
  - Home is the first producer.
  - The routing is host-tested in `test/ui_layout/ReaderEntryIntentTest.cpp`.

### "From your tags": the sleep picker

- The pure half is `src/activities/boot_sleep/StudySleepPick.h`. It covers:
  - `Sampler`, a reservoir sample over "fits and not recently shown", with a stale fallback;
  - `passageKey` (FNV-1a over pubkey/start/end);
  - `ageOf` over a recent ring;
  - `rowIsWhole`, `withinPrefilter` and `inSweep`.

  It is host-tested in `test/study_sleep_pick/`.
- The bounded scan is in `StudySleepScreen.cpp`, **entirely inside an anonymous namespace**
  (37-372). Only `study_sleep_screen::render(const GfxRenderer&)` is exported
  (`StudySleepScreen.h`), and it scans, draws and pushes a full frame.
  - `pickPassage` (257-291) counts files up to `MAX_ENTRIES = 512` and starts at a random file.
    Its two sweeps stop at `MAX_TOTAL_BYTES = 262144` parsed.
  - `offerFile` (226-255) streams each file into a PSRAM `JsonDocument`
    (`PsramJsonAllocator::json()`) and skips files over `SAVE_BYTE_BUDGET + 4096`.
  - `offerRow` (189-223) drops rows that are not whole (`"h"`/`"w"`) and gates fit with
    `fitsFloorRung`, which is sized to the **sleep screen's** layout (`measureLayout`, 138-157).
  - Home cannot call any of these without extracting them or adding an export.
- `Candidate` (`StudySleepPick.h:90-96`) holds `text`, `reference[49]`, `tag` and `key`. It has
  **no pubkey and no unit**, so a picked passage cannot become an `OpenAt` intent as it stands.
- A passage opens with OpenAt only inside the Bible (`route()`). Bible passages live in exactly one
  file:
  - The Bible's pubkey is `study::BIBLE_PUB_KEY = "bible"` (`lib/StudyStore/StudyStore/PubKey.h`).
  - Passage files are `<pubkey>.json` under `sdpaths::PASSAGES_DIR = "/.berean/passages"`
    (`lib/Serialization/SdPaths.h:31`; `pubKeyFromFileName`, `StudySleepPick.h:62-67`).
  - So the Bible's passages are `/.berean/passages/bible.json`, and its size is capped by
    `PassageDoc::SAVE_BYTE_BUDGET = 200000` (`PassageDoc.h:38`).
  - A card limited to Bible passages is a one-file scan. The sleep screen's all-publication
    scan would also pick passages Home cannot open.
- The sleep screen records what it shows in `APP_STATE.recentStudySleep`, a ring of
  `SLEEP_RECENT_COUNT = 16` (`CrossPointState.h:15,31-33,53`; `pushRecentStudySleep`,
  `StudySleepScreen.cpp` near 429). Whether Home should share, read or ignore that ring is open.
- **"Once per day in RAM" has no precedent.** Launcher state dies with the activity: activities are
  deleted on exit (CLAUDE.md, "Activity lifecycle"), and `LauncherActivity` holds everything as
  members.
  - A grep for RAM caches that outlive an activity finds only the store singletons (`PLACES`,
    `APP_STATE`, `STUDY`).
  - The day comes from `readLocalDate(CivilDate&, bool& shifted)` (`src/util/LocalDate.h:8`). It
    fails when the clock is unset, and the launcher already handles that
    (`LauncherActivity.cpp:148-154`).
- **"Paint first, then fill."** `MeetingsActivity::loop` (`MeetingsActivity.cpp:285-293`) is the
  nearest precedent. A pending flag makes `loop()` call `requestUpdateAndWait()`, so the cached
  screen lands, and only then do the slow work. `Activity.h:39,42` declare
  `requestUpdate`/`requestUpdateAndWait`.

### Meetings strip

- `buildWeekStrip(monday, localToday, midweekDay, weekendDay)` is in
  `src/activities/network/MeetingWeekView.h:25`. It is pure and host-tested in
  `test/meeting_week_view/`, and `copyInitial`/`formatWeekRange` sit beside it.
- The settings are `SETTINGS.midweekMeetingDay`/`weekendMeetingDay` (`CrossPointSettings.h:356-357`,
  default `MEETING_DAY_NOT_SET`, exposed in `SettingsList.h:392-395`).
- `MeetingsActivity::buildWeekHeader` (180-216) is the complete example:
  - It dates the strip from the cache entry's key, not from today, when the entry is stale.
  - It marks no day when the local time is unknown.
  - It takes the letters from `WEEKDAY_NAME_IDS`.
- `MeetingsActivity::drawStrip` (318-350) draws the strip through FreeInkUI (`screen.target()`,
  `fui::Rect`), not through raw `renderer` calls as the launcher does.
- The weekly %:
  - It comes from `readBookProgressPercent(path)` (`src/util/BookCacheUtils.h:22`) and is formatted
    with `STR_MEETING_PROGRESS` ("%d%% read") (`MeetingsActivity.cpp:274-281`).
  - The workbook path is `MeetingLibrary::findPublication(MeetingPub::Workbook, entry->workbook)`.
  - The launcher's `thisWeeksMeetingPublication` returns the Watchtower in preference to the
    workbook (161-165), so the Workbook % needs its own lookup.

### Icon row targets

| Entry | Existing route |
|---|---|
| Tags | `goToReader(biblePath, false, ReaderEntryIntent::of(Kind::Tags))` → `openHighlights()`. `HighlightsActivity` lists "the ONE open publication" (`HighlightsActivity.h:18`), so this is Bible tags only. |
| Search | `goToReader(biblePath, false, of(Kind::Search))` → `openBibleSearch(CancelTo::Page)` |
| Publications | `PublicationsActivity` via `startActivityForResult` (`LauncherActivity.cpp:453-459`) |
| Settings | `activityManager.goToSettings()` (`LauncherActivity.cpp:461`) |
| Go to… | `of(Kind::BookGrid)` → `openChapterPicker(CancelTo::Page)` |

All four reader intents need `biblePath`. Today a missing Bible only affects the Bible tile, which
offers `BibleDownloadActivity` (429-446).

### Heap logging precedent

`BibleNavigationActivity.cpp:78-81` and `PublicationsActivity.cpp:58-59` log
`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`, `heap_caps_get_largest_free_block(...)` and PSRAM
free at `LOG_INF` when the screen opens. `PsramJsonAllocator::logMemory(tag)` is used around the
sleep pick (`StudySleepScreen.cpp:375,403`).

### Metrics, fonts, strings

- Lyra metrics after #194 (`src/components/themes/lyra/LyraTheme.h`): `topPadding = 5` (11),
  `headerHeight = 44` (13), `listRowHeight = 40` (18), `mastheadHeight = 120` (79).
- The mockup (artifact `4qvfHbNnJ77F2gM5DELQrQ`, "homeProposed") draws, top to bottom at 480×800:
  - a date and battery line;
  - the hero, 470×262, with a 92 px plate holding "BIBLE" and the `Continue · Isaiah 40:31 ›`
    (inverted) and `Go to…` buttons;
  - a "RECENT PLACES" label and three 44 px rows;
  - a 126 px "FROM YOUR TAGS" card: tag pill, serif italic verse, right-aligned reference;
  - a 92 px Meetings card: 46×76 cover, title, week range, "Workbook 42%" with a bar, and a
    7-day strip with dots on meeting days;
  - four 111×88 icon tiles: Tags, Search, Pubs, Settings.

  **It has no theme header.**
- Serif italic faces exist for the verse (`NOTOSERIF_*_FONT_ID` with `EpdFontFamily::ITALIC`,
  `StudySleepScreen.cpp:61-64`).
- `formatDateLine` (`StudySleepPick.h:179-199`) already builds "Tue 29 Sep" from the RTC.
- Existing keys in `lib/I18n/translations/english.yaml`:
  - `STR_BIBLE` (8), `STR_SETTINGS_TITLE` (14), `STR_CONTINUE_READING` (16), `STR_SEARCH` (288)
  - `STR_TAGS` (375), `STR_MEETING_PROGRESS` (421), `STR_PUBLICATIONS` (434), `STR_GO_TO` (477)
  - `STR_RECENT: "RECENT"` (481)

  No key yet for "From your tags" or a "Continue · %s" format. New keys are a hand-off
  (ui-dev.md, "Shared files").

## Installed tool and package versions

- `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19`
- Platform: `pioarduino/platform-espressif32` release `55.03.37` (`platformio.ini:15`), Arduino
  framework (17), `-std=gnu++2a` (40)
- `bblanchon/ArduinoJson @ 7.4.2` (`platformio.ini:155`)
- `cmake --version` → `cmake version 4.4.2`. Host tests are GTest suites registered in
  `test/CMakeLists.txt`, for example `add_subdirectory(launcher_refresh)` (125) and
  `meeting_week_view` (85).

## Nearest existing examples of this kind of change

- **#204 / PR #219** (`06e52c84`) is the closest: a screen redesign on CoverBand. It:
  - split the renderer-free geometry into a header (`src/components/MastheadLayout.h`);
  - host-tested it against the real Lyra and Classic metric tables (`test/masthead/`);
  - left the drawing in a `.cpp` (`Masthead.cpp`);
  - fell back to the compact header when there is no cover.

  A Home layout header, host-tested the same way, would mirror it.
- **`LauncherRefresh.h` + `test/launcher_refresh/`** is the pattern for a pure decision that
  lives beside the launcher: header only, `${REPO_ROOT}/src` as the only include, no activity
  constructed (`test/launcher_refresh/CMakeLists.txt`).
- **`MeetingsActivity`** is the pattern for the week strip and the paint-then-work loop.

## Open points the spec must settle

1. **Resume of non-Bible books.** Today's strip resumes `recents[0]`, which may be a Watchtower or
   any EPUB. The redesign's Continue is the newest *place*, which is Bible-only by construction.
   Is "carry on reading the Watchtower" meant to go only through the Meetings card?
2. **The verse card's scope.** It could scan only `bible.json`, which every pick can open, or
   extend `Candidate` with pubkey and unit and scan everything, which may pick a passage `OpenAt`
   cannot open. Either way the scan has to leave the anonymous namespace, and it needs a fit gate
   sized to the card rather than to the sleep screen.
3. **The daily cache's home and invalidation.** A file-scope static or singleton outliving the
   activity is new here. A cached pick can also go stale when its tag is deleted the same day.
4. **The hero's width.** Keeping it at 464 px reuses `thumb_773.bmp`, which Masthead and the sleep
   screen already depend on. The mockup's 470 px would generate a second thumbnail.
5. **No Bible on the card.** Continue, Go to, Recent, Tags and Search all need `biblePath`, and
   the fallback for each is unspecified.
6. **Rendering layer.** The launcher draws with raw `renderer`/`GUI` calls and Meetings uses
   FreeInkUI. Porting the strip means picking one.

## Tier

`standard` stands.

- The work stays in `src/activities` and `src/components` (ui-dev).
- It reads `/.berean/` without writing to it.
- It adds no store or on-disk format.
- It uses the #201 intent contract as shipped, with no change.

A change to `StudySleepScreen.cpp` for sharing the scan is still ui surface. The only shared files
touched are the i18n YAML and `test/CMakeLists.txt`, and both are hand-offs.
