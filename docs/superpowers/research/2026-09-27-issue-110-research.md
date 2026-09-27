# Issue #110 — research: splitting the largest hand-written files

Task `t8`, run `berean-os-20260927-enhancements-batch-3-review-follow-ups-w-v6ku`, tier `heavy`.
Branch `refactor/110-split-large-files`, based on `9a307341` (release 1.19.0).

## 1. Current sizes (re-measured; the issue's numbers are stale)

`wc -l` on this branch:

| File | Issue said | Now |
|---|---|---|
| `src/activities/reader/EpubReaderActivity.cpp` | 1,930 LOC, 50 includes | **1,952** lines, **52** `#include`s (`grep -c '#include'`) |
| `src/activities/reader/EpubReaderActivity.h` | — | 187 |
| `src/network/CrossPointWebServer.cpp` | 1,875 | **1,883** |
| `src/network/CrossPointWebServer.h` | — | 141 |
| `src/network/WebDAVHandler.cpp` | 783 | 783 |
| `src/activities/network/WifiSelectionActivity.cpp` | 1,246 | 1,247 |
| `src/main.cpp` `setup()` | 264 lines, `:337-601` | **`:340-606`** (267 lines incl. braces); file is 784 lines |

Last touches: `EpubReaderActivity.cpp` by #161, #159, #153; `CrossPointWebServer.cpp` by #159, #142;
`main.cpp` by #159 (`git log -3 -- <file>`).

## 2. The issue asks for, in order

1. Reader: the bookmark and passage controller, then auto page turn.
2. Web server: route groups (files, settings, fonts).
3. `setup()`: named init stages.

`WifiSelectionActivity` (meeting prefetch) is named in the problem statement but not in "What it
needs". The prefetch is already mostly delegated: `WifiSelectionActivity.cpp:20` includes
`network/MeetingWeekPrefetch.h`, and `prefetchMeetingWeekIfDue()` (`:639`) calls
`MeetingWeekPrefetch::due(...)` (`:647`) with a `Hooks` struct (`:656-658`). Treated as out of scope.

## 3. EpubReaderActivity — who owns what today

### 3a. Bookmarks

State, all in `EpubReaderActivity.h`:
`showBookmarkMessage` `:41`, `currentPageBookmarked` `:42`, `BookmarkToast` enum + `bookmarkToast`
`:47-48`, `bookmarksSaveDisabled` `:55`, `cachedBookmarks` `:56`, `bookmarkMessageTime` `:58`;
methods `loadCachedBookmarks` `:131`, `addBookmark` `:132`, `bookmarkToastString` `:133`,
`updateBookmarkFlag` `:134`.

Free helpers in the `.cpp` anonymous namespace (`:57-160`): `initialBookmarkCacheCapacity` `:59`,
`bookmarkProgressEpsilon` `:60`, `struct ProgressRange` `:86-89`, `getPageProgressRange` `:91-102`
(needs `Epub::calculateProgress`), `bookmarkMatchesProgress` `:104-114` — **pure** given a
`ProgressRange` (compares `BookmarkEntry` fields and floats only).

Method bodies: `bookmarkToastString` `:1765-1778`, `loadCachedBookmarks` `:1780-1799`,
`addBookmark` `:1801-1893`, `updateBookmarkFlag` `:1895-1905`.

Call sites outside those bodies (`grep -n` for each member):
- `loadBook` `:244` — `loadCachedBookmarks()`.
- `openReaderMenu` `:317` — passes `!cachedBookmarks.empty()` to the menu.
- `loop` `:521-523` — expires the toast after `ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS`;
  `:531`, `:557-558` — long-press / Confirm toggles via `addBookmark()` (guarded by `!showBookmarkMessage`).
- `onReaderMenuConfirm` `:717` — `loadCachedBookmarks()` after the bookmarks list returns; `:890` — `addBookmark()`.
- `renderBook` `:1271` — `updateBookmarkFlag()`; `:1318-1319` — draws the toast.
- `renderStatusBar` `:1683` — passes `currentPageBookmarked` to `GUI.drawStatusBar`.

What `addBookmark` reaches into on the activity: `section` (`estimatedTotalPages`, `currentPage`,
`pageCount`, `getTextFromSectionFile`, `getVisibleTextOffsetForPage`), `epub` (`getPath`, progress),
`currentSpineIndex`, `currentPageVisibleOffset`, `getCurrentPosition()` (`:1928`), `RenderLock`,
`requestUpdate()`, `millis()`. `loadCachedBookmarks` additionally uses `renderer` for
`ReaderUtils::showMessage`. So a bookmark controller needs either a narrow "current page" input per
call (spine, page, page count, visible offset, page text, saved progress) or a back-reference.

Persistence already lives outside the activity: `BookmarkFile::load/save` (`src/util/BookmarkFile.h`,
included at `EpubReaderActivity.cpp:25`), whose save rule is host-tested in
`test/bookmark_save_action/` and document format in `test/bookmark_doc/`.

### 3b. Passages / highlights

`highlightsLoaded` (`.h:68`; written `:251` from `STUDY.openPublication`, cleared `:262`, read
`:983` in `recordDocumentRead` and `:1435` in `renderContents`), `pendingSelectionAnchorX/Y`
(`.h:73-74`), `openHighlightPassageAt` `:344-350`, `openHighlightPassage` `:352-378`,
`openHighlights` `:380-400`. Callers: `loop` `:490`, `:541`, `:565`; menu `:846`, `:850`.

These are not state-heavy: both `open*` functions are `startActivityForResult(...)` calls whose
result lambdas call `requestUpdate()` / `navigateTo(...)` — protected `Activity` members. The
persisted passage data is already in `STUDY` (`study/StudyStore.h`, included `:52`) and
`HighlightOverlay` (`HighlightOverlay.h`, a pure namespace). A separate "passage controller" object
would have to call back into the activity for every operation; there is little state to move.

### 3c. Auto page turn

State: `lastPageTurnTime` `.h:31`, `pageTurnDuration` `:32`, `automaticPageTurnActive` `:40`;
`PAGE_TURN_RATES` `.cpp:58`; `toggleAutoPageTurn` `:915-936`.

It is threaded through the file, not contained:
- `openReaderMenu` `:323` → `toggleAutoPageTurn(menu.pageTurnOption)`.
- `loop` `:495-518` — cancel on Confirm/Back/menu gesture, defer while `RenderLock::peek()`, fire
  `pageTurn(true)` when `millis() - lastPageTurnTime >= pageTurnDuration`.
- `loop` `:615` — **`lastPageTurnTime` doubles as the manual-turn debounce**
  (`turnGuardActive = RenderLock::peek() || (millis() - lastPageTurnTime) < kMinManualTurnGapMs`),
  and `pageTurn` writes it on every successful turn (`:947, :955, :961, :967, :975`). It is not
  auto-turn-only state.
- `renderBook` `:1033` (build error), `:1052-1058` (bottom margin when the status bar is hidden),
  `:1258`, `:1267`, `:1277` (disable on empty chapter / out-of-bounds / page load failure);
  `onEndOfBookRendered` `:1323`.
- `renderStatusBar` `:1659-1664` — title shows the rate `60 * 1000 / pageTurnDuration`.
- `toggleAutoPageTurn` itself resets the section under `RenderLock` (`:925-935`) when the status
  bar is hidden, touching `section`, `cachedSpineIndex`, `cachedChapterTotalPageCount`,
  `nextPageNumber`, `rememberCurrentContentOffset()`.

The extractable core is small: the rate table, the duration arithmetic, the active flag and the
"is it due" test. `lastPageTurnTime` must stay shared with the manual-turn guard.

### 3d. Other concerns still in the file
Loading `loadBook` `:180-281`; menu `openReaderMenu` `:294-328`, `onReaderMenuConfirm` `:715-894`
(180 lines); `loop` `:402-678` (277 lines); `renderBook` `:1027-1321`; `renderContents`
`:1378-1627`; navigation `:1687-1763`; screenshot info `:1907-1926`; read-folder move helpers `:116-158`.

## 4. CrossPointWebServer — route layout

All routes are registered in `begin()` (`:90-227`) at `:139-178`, as `server->on(path, method,
[this]{ handleX(); })`:
- Files: `/`, `/files`, `/api/status`, `/api/files`, `/download`, `/upload`, `/mkdir`, `/rename`,
  `/move`, `/delete` (`:139-160`). Handlers `:360-1128` (`handleUpload` `:637-788` uses file-scope
  statics `uploadStartTime/totalWriteTime/writeCount` `:614-616` and `flushUploadBuffer` `:618`).
- Migration report `/migration` `:145` → `:1854-1883`.
- Settings `/settings`, `/api/settings` GET/POST (`:163-165`) → `:1130-1309`.
- Fonts `/fonts`, `/api/fonts`, `/api/fonts/upload`, `/api/fonts/delete` (`:168-171`) → `:1649-1852`,
  with `FontUploadState fontUpload` as a class member (`.h:117-130`).
- Wi-Fi `/api/wifi*` (`:174-176`) → `:1311-1453`.
- WebSocket upload `:1455-1647`.

Every handler is a member of `CrossPointWebServer` and reads `server` (a `unique_ptr<WebServer>`,
`.h:76`).

**Nearest existing example of a route group split out:** `WebDAVHandler`
(`src/network/WebDAVHandler.h:6`) subclasses the Arduino `RequestHandler`, owns its own streaming
state (`_putFile` etc., `:16-19`), and is registered once with `server->addHandler(...)` via a raw
nothrow `new` because `WebServer` takes ownership (`CrossPointWebServer.cpp:181-192`).

Surface note: `src/network` belongs to `net-dev` (`.claude/agents/` description), not `ui`.

## 5. `setup()` — control flow (`src/main.cpp:340-606`)

1. Power rails, `t1`, early Serial with the 250 ms USB stall (`:341-354`).
2. `HalSystem::begin`, panic flag, silent-reboot magic read-and-clear (`:356-367`).
3. HAL begin: gpio, power, tilt, clock; wake reason (`:369-374`).
4. Recovery chord latch (`:376-393`).
5. Device log (`:395-399`).
6. SD init; on failure fonts + error screen and **early `return`** (`:401-408`).
7. `checkPanic`, load SETTINGS/APP_STATE/RECENT_BOOKS/WIFI_STORE, language, theme, frontlight (`:410-427`).
8. Wake-reason switch — may `startDeepSleep` (`:429-452`).
9. Boot-resume decision → `setupDisplayAndFonts` (`:457-470`).
10. Study migration with a throttled progress painter using function-static state (`:472-510`).
11. Boot presentation switch (Silent / SplashlessWake / Splash) (`:512-548`).
12. Activity routing (`:553-580`).
13. Silent-resume first-paint wait and input absorb (`:582-603`); `allowSleepAt` (`:605`).

Local state threaded between stages: `rebootedFromPanic`, `isSilentReboot`, `snapshotTarget`,
`wakeupReason`, `recoveryFirmwareMode`, `resume`, `allowFastInitialReaderRefresh`,
`needsWakeRefresh`. A split into named stages needs a small struct or return values to carry them;
the early return at step 6 must survive.

`setupDisplayAndFonts` (`:294-338`) is the existing precedent for a named init helper in this file.

**`src/main.cpp` is a shared file:** `.claude/agents/ui-dev.md` ("Shared files — report, do not
edit") lists it with `test/CMakeLists.txt` and the translation YAMLs as append points owned by the
orchestrator.

## 6. Nearest existing examples of this kind of change

- **Sub-object owned by a reader activity:** `EndOfBookOptions` (`src/activities/reader/EndOfBookOptions.h`,
  219-line `.cpp`), held by `ReaderActivity` as `std::unique_ptr<EndOfBookOptions>` with an
  `std::atomic<bool>` ready flag (`ReaderActivity.h:17-18`), exposing a small `Action` enum the
  activity acts on. Constructed with `GfxRenderer&`.
- **Pure logic pulled out of an activity with a host test:** `MeetingWeekView`
  (`src/activities/network/MeetingWeekView.{h,cpp}`, 37/60 lines) from #169, tested in
  `test/meeting_week_view/` whose `CMakeLists.txt:6` compiles the `src/activities/...` `.cpp` directly.
- **Pure namespace used by the reader:** `HighlightOverlay` (`HighlightOverlay.h`), no activity access.
- **Route group outside the server class:** `WebDAVHandler` (§4).
- **Named init helper:** `setupDisplayAndFonts` (§5).

No test suite builds an `Activity` or `CrossPointWebServer`: `grep -rn 'src/activities\|src/network\|main.cpp' test/*/CMakeLists.txt`
lists only pure units (`MeetingFilename`, `WolWeekScan`, `MeetingWeekView`, `MeetingPrefetchPlan`,
`MeetingWeekTable`, `PubMediaJson`). So only pure extracted pieces (e.g. `bookmarkMatchesProgress`,
the auto-turn rate arithmetic) can take a host test without new infrastructure.

## 7. Toolchain actually installed

- `pio --version` → `PlatformIO Core, version 6.1.19` (`/Volumes/stein/.platformio/penv/bin/pio`).
- Platform `pioarduino/platform-espressif32` `55.03.37` (`platformio.ini:15`); Arduino core
  `framework-arduinoespressif32` `3.3.7` (its `package.json`).
- `xtensa-esp-elf-g++ (crosstool-NG esp-14.2.0_20251107) 14.2.0`.
- `.venv/bin/clang-format --version` → `21.1.8` (used by `bin/clang-format-fix:3`).
- `cmake --version` → `4.4.2` (host tests).
- `platformio.ini` has no `build_src_filter` (`grep` returns nothing), so PlatformIO's default
  compiles every file under `src/`; a new `.cpp` beside the reader is picked up with no config change.
- The added host test directory would need an `add_subdirectory(...)` line in `test/CMakeLists.txt`
  (e.g. `:127-128` for the bookmark suites) — an orchestrator-applied line per `ui-dev.md`.

## 8. Findings that shape the spec

1. **Scope crosses surfaces.** Reader extractions are `ui`. Web-server route groups are `net`
   (`src/network`). `setup()` stages are in `src/main.cpp`, which `ui-dev.md` forbids a surface
   worker to edit. Tier is already `heavy` (the maximum), so there is nothing to raise; the
   cross-surface parts are a scope decision for the spec phase. Current lean: do the two reader
   extractions on this branch and leave the web server and `setup()` as named follow-ups — the
   batch brief allows stopping at a clean point.
2. **"Passage controller" has little state to own** (§3b). The bookmark side has real state and a
   rollback rule (§3a) and is the natural first extraction; passage entry points are thin wrappers
   over `startActivityForResult`.
3. **Auto page turn shares `lastPageTurnTime` with the manual-turn debounce** (`:615`), so an
   extraction must leave that timestamp readable by `loop`/`pageTurn` or keep it on the activity.
4. **Threading:** bookmark reads of `section` take `RenderLock` (`:1817-1821`); `renderBook` and
   `renderStatusBar` run on the render task and read `currentPageBookmarked`,
   `automaticPageTurnActive`, `pageTurnDuration`. An extracted object must be accessed from the same
   places under the same locking, not add new cross-task access.
5. **Sibling task:** `t9 fix/106-enable-wall` is queued in this run (`hpipe status`). Enabling
   `-Wall` may touch the same files; the file lock only applies from implement onward.
