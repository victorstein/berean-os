Tier: heavy

# Issue #172 — spec review 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-172-design.md` against `gh issue view 172 --repo
victorstein/berean-os` and `docs/superpowers/research/2026-09-27-issue-172-research.md`, at `186092b9`
(only docs changed since the base `7c080671`: `git diff 7c080671 --stat` lists just the two docs files).
`origin/main` has gained only `faf76d70` (release 1.19.1), which touches `platformio.ini`'s version
line and nothing in `src/` or `test/`.

## What was attacked and held

These are the design's real choices. Each was checked against source, and none produced a finding.

- **W1/W5 dispatch.** In the installed core (`framework-arduinoespressif32` `package.json` →
  `"version": "3.3.7"`), `_addRequestHandler` appends (`WebServer.cpp:368-376`). `Parsing.cpp:131-137`
  takes the first handler whose `canHandle` is true. `Uri::canHandle` is `_uri == requestUri`
  (`Uri.h:25`), and `FunctionRequestHandler::canHandle` checks the method first
  (`detail/RequestHandlersImpl.h:83-89`). `WebDAVHandler::canHandle` returns true for 11 methods on
  every URI (`WebDAVHandler.cpp:19-38`). So "every group registration before `addHandler`" is the
  right invariant. Reordering the `on()` routes among themselves is also inert, because no
  `(method, path)` pair repeats (`CrossPointWebServer.cpp:139-176`).
- **W2/W3 allocation and lifetime.** `upload` and `fontUpload` are by-value members
  (`CrossPointWebServer.h:35-51, 121-133`). Moving them into by-value group members keeps them
  inside the one `makeUniqueNoThrow<CrossPointWebServer>()` block
  (`CrossPointWebServerActivity.cpp:263`). `~CrossPointWebServer` calls `stop()` (`:88`), and
  `stop()` resets `server` (`:284`). Every failure path in `begin()` also resets it (`:127-130`,
  `:186-189`, `:197-203`). `~WebServer` deletes its handlers (`WebServer.cpp:52-65`). No lambda can
  run after its group is gone.
  - Capture size: `[this, &server]` is two pointers, 8 bytes on the 32-bit Xtensa. That still fits
    libstdc++'s `std::function` local buffer, which is sized to a member-function pointer (8 bytes on
    this target). So the claim that no allocation is added holds, provided the capture stays two
    pointers.
- **W4.** `running` is a plain `bool` (`.h:76`), so `const bool&` binds. The guard at `:644` really
  is unreachable: `handleClient` returns early when `!running` (`:297`), and `stop()` clears `running`
  synchronously before tearing down (`:252`).
- **W9.** Outside the class, nothing uses `UploadState`, `FontUploadState`, `FileInfo` or `.upload`.
  `grep -rn "\.upload\b\|->upload\b\|UploadState\|FileInfo\|FontUploadState" src` hits only CSS and JS
  in `html/*.html`, apart from the server's own two files.
- **S7, the eager Back read.** Every link in the chain is `const` and has no side effects:
  - `MappedInputManager::isPressed` → `mapButton` (`MappedInputManager.cpp:57-63`, `:272`);
  - → `HalGPIO::isPressed` (`HalGPIO.cpp:178-183`);
  - → `NavKeyGestures::suppressState` (`NavKeyGestures.cpp:52-57`);
  - → `InputManager::isPressed`, which is `return currentState & (1 << buttonIndex);`
    (`freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:472`). The spec's chain stops one
    link short of this, but it is pure too.

  The only `mutable` members in `MappedInputManager.h:120-122` belong to `getHeldTime`, which is not
  on this path. The S7 fallback is not needed.
- **S2/S3/S4/S8, the stage split.** The eight `BootContext` fields are exactly the issue's list, and
  every S3 line range matches `src/main.cpp:340-606`. The SD-failure `return` (`:409`) still skips
  `checkPanic`, the stores, migration and `allowSleepAt`. Both `startDeepSleep` calls (`:436`, `:448`)
  stay inside the switch, with nothing added after them.
- **S6, the silent-reboot constants.** `SILENT_REBOOT_*` and `BootResume` are referenced only in
  `src/main.cpp`: a `grep` over `src`, `lib` and `test` finds nothing else, and `SilentRestart.h`
  declares only the two functions.
- **S6, the route choice.** `goHome()` in the Silent branch and `goHome(needsWakeRefresh)` in the Home
  branch differ only by argument (`ActivityManager.h:90`, default `false`). Keeping `SilentHome` and
  `Home` as separate routes preserves that difference.

## Findings

### MINOR 1 — The route count is 21, not 20

- **Claim.** Testing step 2 says "the sorted sets must be equal (20 routes)". Research §3b says "Code
  registers 20 `on()` routes".
- **Problem.** The mechanical pre/post check is anchored to the wrong number. An implementer who
  gets 21 will go looking for a duplicate or a stray route.
- **Evidence.** `grep -c "server->on(" src/network/CrossPointWebServer.cpp` → `21`. By group:
  - core: 2
  - files: 8
  - `/migration`: 1
  - settings: 3
  - fonts: 4
  - Wi-Fi: 3 (`GET /api/wifi`, `POST /api/wifi`, `POST /api/wifi/delete`, `:174-176`)
- **Fix.** Change "20 routes" to "21 routes" in Testing step 2 and in research §3b.

### MINOR 2 — W7 and Testing 3 undercount the non-moved edits

- **Claim.** In W7, handler bodies change "only `server->` → `server.` and the helper namespace
  prefix". Testing 3 says the only non-moved lines are "declarations, registration,
  `server->`/`server.` and helper prefixes".
- **Problem.** Some body edits are not in either list, so the PR's move-only statement would be false
  as written. A reviewer relying on `--color-moved` will see them as unexplained edits.
- **Evidence.** The body edits that fall outside both lists:
  - `handleFileListData` streams from inside a `scanFiles` lambda that captures `[this, &output,
    &doc, seenFirst]` and calls `server->sendContent` (`:511-530`). Once `server` is a handler
    parameter, not a member, that capture list must change to capture `server`.
  - The three moved `sendHtmlContent(server.get(), …)` calls (`:480`, `:1131`, `:1650`) become
    `&server`, because the helper takes `WebServer*` (`:355`).
  - The W4 guard at `:644` changes shape.
- **Fix.** In W7 and Testing 3, list all three edits:
  - the capture-list change at `:511`;
  - `server.get()` → `&server` at the moved `sendHtmlContent` call sites (or give the helper a
    `WebServer&` parameter and say so);
  - the `:644` guard change.

### MINOR 3 — W6's "required shared home" is true for only two of the four helpers

- **Claim.** In W6, the four helpers "are used by core, files, settings, fonts and the WebSocket
  handler …, so one home both sides include is required."
- **Problem.** Only `sendHtmlContent` and `isProtectedWebPath` cross groups. `normalizeWebPath` and
  `isProtectedWebName` are files-only. Moving those two out of an anonymous namespace into an
  exported `webroutes::` header gives them external linkage, and the stated reason does not cover
  that.
- **Evidence.**
  - `normalizeWebPath` and `isProtectedWebName` are used only at `:453`, `:860`, `:876`, `:942` and
    `:943`, all of them files handlers.
  - The WebSocket handler normalises its path inline (`:1522-1525`) and calls only
    `isProtectedWebPath` (`:1531`).
- **Fix.** Pick one:
  - Keep `normalizeWebPath` and `isProtectedWebName` in `FileRoutes.cpp`'s anonymous namespace, and
    put only `sendHtmlContent` and `isProtectedWebPath` in `WebRouteUtils`.
  - Keep all four together, and reword the rationale to "two are shared; the other two co-locate
    with their `protectedpath::` sibling".

  Either is behaviour-free.

### MINOR 4 — `BootContext` has no stated default for `wakeupReason`

- **Claim.** S2 gives `BootContext` "the defaults the locals have today (`false`, `0`, `Splash`)".
- **Problem.** That covers seven of the eight fields. `wakeupReason` is a `HalGPIO::WakeupReason`
  (`HalGPIO.h:132`) that today is only ever initialised from `gpio.getWakeupReason()` (`:376`). A
  field with no initialiser would be indeterminate until `beginInputAndPower` runs.
- **Evidence.** `src/main.cpp:376`; `lib/hal/HalGPIO.h:132` (`enum class WakeupReason { PowerButton,
  AfterFlash, AfterUSBPower, Other };`).
- **Fix.** In S2, state `HalGPIO::WakeupReason wakeupReason = HalGPIO::WakeupReason::Other;` so that
  every field has a defined default.

## Summary

The design's load-bearing decisions all hold against the installed core and the code:
- route groups that call `server.on()` rather than subclassing `RequestHandler`;
- every group registration before WebDAV;
- by-value groups with no allocation added;
- the `running` reference guard;
- the stage table and its SD early return;
- the eager Back read.

The four findings are documentation-accuracy fixes. They can be applied inline and reverse no
decision.

VERDICT: CLEAR
