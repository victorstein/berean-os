Tier: heavy

# PR #174 code-quality review 0 (issue #172)

Scope: the code in `gh pr diff 174`, which is `src/network/{FileRoutes,FontRoutes,SettingsRoutes,WebRouteUtils}.*`,
`src/network/CrossPointWebServer.{h,cpp}`, `src/main.cpp`, `src/boot/BootDecisions.h` and
`test/boot_decisions/`. I compared against base `7c080671`. The planning docs are out of scope.

## What I checked and found sound

- **One pattern across the three groups.** `FileRoutes`, `SettingsRoutes` and `FontRoutes` share one shape:
  a `registerRoutes(WebServer&)` (`FileRoutes.cpp:40-53`, `SettingsRoutes.cpp:15-19`, `FontRoutes.cpp:14-21`),
  handlers that take `WebServer&`, and lambdas that capture `[this, &server]`. The PR body explains why this
  departs from `WebDAVHandler`'s `RequestHandler` subclassing. The reason holds: `server.on()` keeps the core's
  URI and method matching and its multipart hooks. The registration-order constraint relative to WebDAV is
  documented where it applies (`CrossPointWebServer.cpp`, just before the three `registerRoutes` calls). That
  comment explains why, so it is not noise.
- **Move-only.** `git diff --color-moved` against `7c080671` shows the handler bodies moved unchanged apart
  from the `server->`/`server.` and `webroutes::` edits the PR lists. `CrossPointWebServer.cpp` has been
  pruned of the includes it no longer uses: `FsHelpers`, `ProtectedPath`, `string_view`, the three page
  headers, `CrossPointSettings`, `FontInstaller`, `SdCardFontSystem` and `SettingsList`. The includes that
  remain are still used, for example `clearBookCache` at `:622,691` and `resetTaskWatchdog`. The dead
  `formatFileSize` declaration is gone. No commented-out code was added.
- **No new duplication.** `sendHtmlContent` and `isProtectedWebPath` exist once, in `WebRouteUtils.cpp:9-16`.
  Helpers used only by the file routes (`normalizeWebPath`, `isProtectedWebName`) stay in `FileRoutes.cpp`'s
  anonymous namespace (`:16-37`). `WebDAVHandler::isProtectedPath` (`WebDAVHandler.cpp:749-750`) still wraps
  the same `protectedpath::isProtectedPath` call. That wrapper predates this PR, which did not add it, so it
  is not a finding.
- **Boot stages match the file.** The new stage functions are `static`, like their neighbours in `main.cpp`
  (`:148`, `:220`, `:227`). `BootContext` (`main.cpp:329-338`) replaces the locals that `setup()` used to
  share, and adds no globals. The error handling keeps the established shape: `mountStorage` logs with
  `LOG_ERR`, shows the SD error screen and returns false, and `setup()` returns early (`main.cpp:403-413`,
  `:630`). Comments moved with the code they explain, and the one that now names a function was updated
  (`// Splash skipped: routeFirstActivity() picks...`).
- **Pure header matches precedent.** `BootDecisions.h` follows the repo's header-only pattern for host-tested
  decisions (`src/util/BookProgress.h`, `src/activities/reader/AutoPageTurn.h`). The test CMake mirrors its
  siblings.
- **The tests are well designed.** `resumeReaderInputs()` (`BootDecisionsTest.cpp:7-16`) builds a baseline
  in which every input leads to `ResumeReader`, and each `ChooseBootRoute` case changes only the input it is
  named for. That tests each guard in isolation. The precedence tests (`RecoveryBeatsPanicAndSilentReboot`,
  `PanicBeatsSilentReboot`) and the stale-state tests (`SilentHomeTargetIgnoresStaleReaderState`,
  `ColdBootIgnoresStaleSplashFlag`) pin the orderings that the original comments warned about. The
  `decodeSilentReboot` tests cover both ends of the uninitialised-RTC range (`2`, `0xFFFFFFFF`).

## Findings

**MINOR 1: the call site builds `BootRouteInputs` positionally with six bools.**
`main.cpp:566-568` passes `{boot.recoveryFirmwareMode, boot.rebootedFromPanic, boot.resume,
boot.snapshotTarget, APP_STATE.openEpubPath.empty(), APP_STATE.lastSleepFromReader, backHeld,
APP_STATE.readerActivityLoadCount > 0}`. Six of those eight fields are `bool`. Swapping two of them, such as
`backHeld` and `lastSleepFromReader`, compiles silently and inverts a boot route, and the host test cannot
catch it because the test builds its own inputs. The test already labels each field with `/*name=*/`
comments (`BootDecisionsTest.cpp:8-15`), which shows the risk was noticed. The repo already uses C++20
designated initializers for this kind of struct (`EpubReaderActivity.cpp:674`, `:1719`; `BaseTheme.h:124`).
Fix inline: use `{.recoveryFirmwareMode = ..., .rebootedFromPanic = ..., ...}` at the call site (and
optionally in `resumeReaderInputs()`). The compiler then checks the field order.

**MINOR 2: the new shared helper, and one sibling's state struct, differ from the other groups.**
- `webroutes::sendHtmlContent` takes `WebServer*` (`WebRouteUtils.h:10`), while every route-group handler
  takes `WebServer&`. So three of its four callers write `&server` (`FileRoutes.cpp:106`,
  `SettingsRoutes.cpp:21`, `FontRoutes.cpp:24`).
- `FileRoutes::UploadState` is declared `public` (`FileRoutes.h:19-36`) but is used only by private members.
  The sibling `FontRoutes::FontUploadState` is private (`FontRoutes.h:14-28`).

Both carry over from where the code came from. `sendHtmlContent` was a file-static taking a pointer, and
`UploadState` was public on `CrossPointWebServer`. Now that each is a new interface, it can match its
siblings. Fix inline: take `WebServer&` (the core route becomes `*server`), and move `UploadState` into the
`private:` section.

No BLOCKER or MAJOR findings. The refactor mirrors the patterns it names and removes what it made dead. Its
tests pin the decisions that the old inline comments were guarding.

VERDICT: CLEAR
