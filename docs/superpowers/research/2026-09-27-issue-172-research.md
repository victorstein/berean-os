# Issue #172 — research: web-server route groups and `setup()` init stages

Task `t10`, tier `heavy`. All line numbers are against `7c080671` (this branch's base). `origin/main`
has since gained only `faf76d70 chore(main): release 1.19.1 (#173)` (`git log HEAD..origin/main`),
which does not touch either file.

## 1. What the issue asks, and what the batch brief adds

Two behaviour-preserving extractions, as two commits on one branch:

1. Split the **files, settings and fonts** route groups out of `CrossPointWebServer`, with
   `WebDAVHandler` named as the precedent. Every route keeps its path, method, response and
   auth/dot-dir protection, checked against `docs/webserver-endpoints.md`.
2. Break `src/main.cpp` `setup()` into named init stages; shared state in **one struct**, not new
   globals; init order unchanged; the SD-failure early return stays.

Batch brief: `src/main.cpp` is this task's to edit (overriding `net-dev.md`'s "Shared files — report,
do not edit", which the brief outranks for this task), and this task may register its own suites in
`test/CMakeLists.txt`. Zero new warnings whether or not sibling `#106` (`-Wall`, issue still OPEN per
`gh issue view 106`) lands first. If commit 2 balloons, stop after commit 1 and say so.

## 2. Files that own the behaviour

| File | Lines | Role |
|---|---|---|
| `src/network/CrossPointWebServer.cpp` | 1,883 | every route handler, `begin()`/`stop()`/`handleClient()`, WebSocket upload |
| `src/network/CrossPointWebServer.h` | 141 | class, `UploadState`, `FontUploadState`, `FileInfo` |
| `src/network/WebDAVHandler.{h,cpp}` | 43 / 783 | the precedent: a route group outside the class |
| `src/main.cpp` | 784 | `setup()` `:340-606`, `setupDisplayAndFonts` `:294-338` |
| `docs/webserver-endpoints.md` | 473 | the route contract to check before and after |

(`wc -l`.) The only consumer of `CrossPointWebServer` is `CrossPointWebServerActivity.cpp`
(`:263-355`), using `begin`, `isRunning`, `handleClient` (and the destructor). `grep -rn FileInfo src`
finds no use outside the server. `BootResume`, `SILENT_REBOOT_*` and `setupDisplayAndFonts` are
referenced only in `src/main.cpp` (`grep -rn` over `src` excluding it returns nothing).

## 3. Web server — current control flow

### 3a. Registration, and why its order is load-bearing

`begin()` (`:90-227`) registers every route with `server->on(path, method, [this]{ handleX(); })`
(`:139-176`), then `onNotFound` (`:178`), then `collectHeaders` + `addHandler(new WebDAVHandler)`
(`:182-191`), then `server->begin()` (`:194`).

In the installed Arduino core, dispatch is **first match in registration order**:
`WebServer::_addRequestHandler` appends to a linked list (`WebServer.cpp:368-376`), and
`Parsing.cpp:131-137` walks it from `_firstHandler` and stops at the first `canHandle(...)` true. The
not-found callback runs only if no handler handled the request (`WebServer.cpp:890-903`).
`WebDAVHandler::canHandle` returns true for **every** URI for GET, HEAD, PUT, DELETE, OPTIONS,
PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK (`WebDAVHandler.cpp:19-38`). So if any `server->on` GET
route were registered after `addHandler(webDav)`, WebDAV would answer it instead. **Every route-group
registration must stay before the WebDAV `addHandler`.** Order among the `on()` routes themselves
does not change results (paths are distinct, or same path with distinct methods for
`/api/settings` and `/api/wifi`), but keeping it identical costs nothing.

`WebServer::on(uri, method, fn[, ufn])` takes `std::function<void(void)>` (`WebServer.h:151-155`) and
builds a `FunctionRequestHandler` per call; handlers are deleted by `~WebServer`
(`WebServer.cpp:52-65`).

### 3b. Route groups today (code vs `docs/webserver-endpoints.md`)

| Group | Routes (`CrossPointWebServer.cpp`) | Handlers | Docs |
|---|---|---|---|
| core | `GET /` `:139`, `GET /api/status` `:142`, not-found `:178` | `:360-428` | `:20`, `:27` |
| **files** | `GET /files` `:140`, `GET /api/files` `:143`, `GET /download` `:144`, `POST /upload` `:148`, `POST /mkdir` `:151`, `POST /rename` `:154`, `POST /move` `:157`, `POST /delete` `:160` | `:430-1128` | `:21`, `:59-212` |
| migration | `GET /migration` `:145` | `:1854-1883` | `:109` |
| **settings** | `GET /settings` `:163`, `GET /api/settings` `:164`, `POST /api/settings` `:165` | `:1130-1307` | `:22`, `:216-274` |
| **fonts** | `GET /fonts` `:168`, `GET /api/fonts` `:169`, `POST /api/fonts/upload` `:170` (+upload fn), `POST /api/fonts/delete` `:171` | `:1649-1849` | `:23`, `:278-338` |
| wifi | `GET/POST /api/wifi` `:174-175`, `POST /api/wifi/delete` `:176` | `:1311-1452` | `:342-385` |
| WebSocket | port 81 | `:1455-1645` | `:387-429` |

Code registers 21 `on()` routes (`grep -c "server->on("` → `21`); the docs list the same 21 plus WebSocket, WebDAV and UDP. No
discrepancy found. The issue scopes only the three bold groups.

### 3c. What each group reads besides `server`

- **files**: member `upload` (`UploadState`, public, `.h:35-51`) passed into `handleUpload`/
  `handleUploadPost` by the `/upload` lambdas (`:148`); `running` in `handleUpload`'s guard
  (`:644`); file-scope statics `uploadStartTime`, `totalWriteTime`, `writeCount` and
  `flushUploadBuffer` (`:614-635`); `scanFiles`/`isEpubFile` members (`:430-477`); anonymous-namespace
  helpers `normalizeWebPath` (`:55-71`), `isProtectedWebPath` (`:73-75`), `isProtectedWebName`
  (`:77-79`); `sendHtmlContent` (`:355-358`). Counted with
  `awk 'NR>=479&&NR<=1128' | grep -o ... | sort | uniq -c`: 82 `server->`, 8 `isProtectedWebPath`,
  3 `normalizeWebPath`, 2 `running`.
- **settings**: `server`, `sendHtmlContent`, `sdFontSystem` global (`extern` at
  `SdCardFontSystem.h:62`), `SETTINGS`, `getSettingsList`. No member state.
- **fonts**: `server`, `sendHtmlContent`, `sdFontSystem`, member `fontUpload`
  (`FontUploadState`, `.h:121-133`, a 4 KB `std::vector` sized in its constructor).
  `handleFontList` is `const` and `const_cast`s the global (`:1656`).

**Shared across groups:** `sendHtmlContent` is used by core, files, settings and fonts
(`:361,480,1131,1650`). `isProtectedWebPath` is used by files *and* the WebSocket handler (`:1531`),
which the issue leaves in the class. So splitting files out means those helpers need a home both
translation units can see (a small internal header, or the WebDAV way: WebDAV calls
`protectedpath::` directly, `WebDAVHandler.cpp:6,229`).

### 3d. Protection that must survive

Dot-dir/protected-path refusal is per handler, via `protectedpath::` (`lib/ProtectedPath`, host-tested
in `test/protected_path/`): files routes at `:498, 553, 689, 831, 876, 882, 955, 959, 1082`, and
listing filters protected names in `scanFiles` (`:453`). There is no HTTP auth anywhere
(`grep -n "authenticate\|requestAuthentication" src/network/*.cpp` returns nothing), so "auth" in the
issue's constraint has nothing to preserve beyond the protected-path checks. CORS is global via
`enableCORS(true)` (`:135`) and OPTIONS is answered in `handleNotFound` (`:367-370`).

### 3e. Dead code seen on the way

`String formatFileSize(size_t) const` is declared (`.h:90`) with no definition in `src`
(`grep -rn formatFileSize src` hits only the declaration and `FilesPage.html` JS).

## 4. The precedent: `WebDAVHandler`

`class WebDAVHandler : public RequestHandler` (`WebDAVHandler.h:6`) overrides `canHandle`, `canRaw`,
`raw`, `handle` (`:9-12`), owns its streaming state (`_putFile`, `_putPath`, … `:16-19`), and is
registered once with a raw nothrow `new` because `WebServer` owns and deletes handlers
(`CrossPointWebServer.cpp:184-191`). It does its own method dispatch in `handle`.

What it does **not** show: a group of distinct `path + method` routes, or multipart upload. The files
and fonts groups use the two-callback `on(uri, method, fn, ufn)` form (`:148, :170`); the core routes
those through `FunctionRequestHandler::canUpload/upload` (`Parsing.cpp:355-356, 500-501`). A
`RequestHandler` subclass for a route group would have to re-implement per-path matching and the
upload hooks, which is a behaviour surface the current `on()` form gets from the core for free. A
group that keeps calling `server.on(...)` itself would inherit exactly today's dispatch. This is the
main design choice for the spec.

## 5. `setup()` — current control flow (`src/main.cpp:340-606`)

Line numbers re-measured with `grep -n` against this base; the #110 research note's ranges for
the same function are 2–6 lines early.

1. Power rails, `t1 = millis()` (global `:132`), early Serial with the 250 ms USB stall (`:341-356`).
2. `HalSystem::begin`; `rebootedFromPanic` (`:358-361`); silent-reboot magic **read-and-clear**
   → `isSilentReboot`, `snapshotTarget` bounded to `<= SILENT_REBOOT_TARGET_READER` (`:363-369`).
3. `gpio`, `powerManager`, `halTiltSensor`, `halClock` begin; `wakeupReason` (`:371-376`).
4. Recovery chord latch → `recoveryFirmwareMode` (`:378-395`).
5. Device log (`:397-401`).
6. `Storage.begin()`; on failure `setupDisplayAndFonts(isSilentReboot)`, SD error screen, **`return`**
   (`:403-410`).
7. `checkPanic`; load `SETTINGS`, `APP_STATE`, `RECENT_BOOKS`, `WIFI_STORE`; language, theme,
   `ButtonNavigator`; frontlight (`:412-429`).
8. Wake-reason switch: may `startDeepSleep` (`:431-456`, calls at `:436, :448`); sets global
   `wakePowerReleasePending` (`:438`); then the version log (`:458-459`).
9. `resume` resolution (`:467-470`), refresh flags declared (`:471-472`), `setupDisplayAndFonts`
   (`:474`).
10. Study migration with a throttled painter; the painter is a captureless lambda converted to a
    function pointer (`MigrationProgress::onStep`), so its state lives in function-local `static`s
    (`:487-488`) (`:476-516`).
11. Presentation switch Silent / SplashlessWake / Splash; SplashlessWake may set
    `allowFastInitialReaderRefresh` (`:541`) or `needsWakeRefresh` (`:548`) (`:518-554`).
12. Activity routing (`:559-586`); Back-held read at `:575`.
13. Silent-resume first-paint wait and input absorb (`:588-603`); global `allowSleepAt` (`:605`).

State carried between stages — exactly the issue's list: `rebootedFromPanic`, `isSilentReboot`,
`snapshotTarget`, `wakeupReason`, `recoveryFirmwareMode`, `resume`, `allowFastInitialReaderRefresh`,
`needsWakeRefresh`. `restoreLightOn` (`:428`) and `isSleepWake` (`:467`) are single-use locals.
Existing globals written by `setup()` and read by `loop()` — `t1`, `wakePowerReleasePending`,
`allowSleepAt` — are pre-existing and stay; the issue forbids only *new* globals.

`startDeepSleep` is not declared `[[noreturn]]` (`HalPowerManager.h:43`), so the compiler does not
know step 8 ends the boot; a stage split must not add code that would run between it and the next
line.

**Pure pieces** (inputs → value, no hardware): the silent-reboot decode (step 2, given the two RTC
words), the `resume` resolution (step 9, given `isSilentReboot`, `isSleepWake`,
`APP_STATE.showBootScreen`), and the routing choice (step 12, given the flags, `openEpubPath`
emptiness, `lastSleepFromReader`, Back held, `readerActivityLoadCount`). These are the host-testable
candidates.

## 6. Nearest existing examples of this kind of change

- **Named init helper in `main.cpp`:** `setupDisplayAndFonts` (`:294-338`), a plain free function.
- **Route group outside the server class:** `WebDAVHandler` (§4).
- **Last extraction in this series:** PR #171 (`7c080671`), spec
  `docs/superpowers/specs/2026-09-27-issue-110-design.md`. Pure pieces became header-only units with
  host tests modelled on existing ones (`AutoPageTurn.h` on `ReturnStack.h`, `BookmarkMatch.h` on
  `BookmarkSaveAction.h`), e.g. `test/auto_page_turn/CMakeLists.txt` (include `${REPO_ROOT}/src`, link
  `crosspoint_test_common` + `GTest::gtest_main`).
- **Host tests and `String`:** `test/stubs/Arduino.h:9-29` defines a minimal `String` carrying only what
  `PersistableStore` and the Storage fake need. Arduino-`String` helpers such as `normalizeWebPath`
  (`startsWith`, `endsWith`, `substring`) are therefore not host-testable without growing the stub,
  which the issue's "build no new test infrastructure" rules out. `protectedpath::` is already tested
  (`test/protected_path/`). No suite builds `CrossPointWebServer` or `main.cpp`.

## 7. Toolchain actually installed

- `pio --version` → `PlatformIO Core, version 6.1.19`.
- Platform `pioarduino/platform-espressif32` `55.03.37` (`platformio.ini:15`); Arduino core
  `framework-arduinoespressif32` `3.3.7` (its `package.json`) — the `WebServer`/`RequestHandler`
  sources cited above are from this install.
- `links2004/WebSockets @ 2.7.3`, `bblanchon/ArduinoJson @ 7.4.2` (`platformio.ini` `lib_deps`).
- `cmake --version` → `4.4.2`; host compiler `Apple clang version 21.0.0`.
- Build flags enable `-Wformat` only (`platformio.ini:72-73`); `grep -n "Wall\|Wextra" platformio.ini`
  returns nothing, so `-Wall` from `#106` is not yet on this base.
- Baseline build and host tests: see §8.

## 8. Baseline

`pio-locked.sh run -e x4pro` on `7c080671`: `SUCCESS` in 2 min 47 s.
`RAM: 19.9% (used 65172 bytes from 327680 bytes)`, `Flash: 83.8% (used 5490786 bytes from 6553600 bytes)`.

249 `warning:` lines (`grep -c "warning:"`). By file: 201 `managed_components/espressif__esp-sr/...`,
15 + 15 ESP-IDF `usb/{hub,ext_port}.c`, 11 + 1 `espressif__rmaker_common`, 3 other `esp-sr`,
2 `.pio/libdeps/x4pro/WebSockets/src/WebSocketsClient.cpp`, 1
`src/activities/network/CrossPointWebServerActivity.cpp`. **Zero** in `src/main.cpp` or
`src/network/` (`grep "warning:" | grep -c "src/main.cpp\|src/network/"` → `0`). The acceptance
check is therefore: no warning line naming a file this task creates or edits.

Host tests: `cmake -S test -B build/test && cmake --build build/test -j8 && ctest --test-dir
build/test -j8` → `100% tests passed out of 1120`.

## 9. Findings that shape the spec

1. **Registration order is a correctness constraint**, not style (§3a): group registrations must run
   before `addHandler(WebDAVHandler)`.
2. **Group shape is the key decision.** A `RequestHandler` subclass (the literal WebDAV precedent)
   would re-implement path matching and upload dispatch; a group object that registers its own
   `server.on(...)` routes keeps the core's dispatch unchanged (§4). The latter is the lower-risk
   reading of "behaviour-preserving"; the spec must argue it against the issue naming WebDAV.
3. **Helpers are shared** between files, core and WebSocket (§3c); they need one home.
4. **Upload state lifetime**: `upload` and `fontUpload` are 4 KB-buffered members allocated with the
   server object; whichever object owns them must be constructed at the same point so heap timing
   does not change.
5. **`setup()` stage state** is exactly the eight values the issue lists (§5); three pure decisions
   are host-testable without new infrastructure; Arduino-`String` web helpers are not.
6. **Tier**: stays `heavy`. The work is within `net` plus `main.cpp`, which the brief assigns to this
   task; no contract, format or migration is touched. Nothing to raise.
