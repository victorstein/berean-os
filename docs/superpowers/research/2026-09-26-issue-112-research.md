# Release builds keep serial logging — research

Investigated 2026-09-25 on `2f303f6f`, the tip of `main` (`git fetch origin main && git rev-parse
--short origin/main` → `2f303f6f`), for issue #112.

Owner decision, recorded on #112: drop serial logging from `x4pro-gh_release`. Remove
`-DENABLE_SERIAL_LOG`, set `LOG_LEVEL=0`, and correct CLAUDE.md's `-std=c++2a` to `gnu++2a`. This
note checks what that change actually does. It does not revisit the decision.

**Verdict.** Removing `-DENABLE_SERIAL_LOG` does what the owner wants, but not in the way the issue
describes:

1. **The `CMD:` handler is not behind `ENABLE_SERIAL_LOG`.** `src/main.cpp:618-633` runs
   unconditionally and still compiles into the release binary. It goes dead *at runtime* only
   because the one `Serial.begin()` is behind the flag (`src/main.cpp:342-353`). Without that call
   the HWCDC RX queue is never allocated, so `available()` returns `-1` and the `> 0` test never
   passes (§3). The spec has to decide whether "not live" means compiled out or merely unreachable.
2. **Crash reports lose their "Last logs" section.** The RTC ring buffer is filled only by
   `logPrintf`, and `logPrintf` is reached only through the `LOG_*` macros. With the flag gone,
   every macro expands to nothing, the buffer stays empty, and `/crash_report.txt` keeps just the
   version and the panic reason. On this Xtensa board the stack dump is empty already (§4). Upstream
   made the opposite call on purpose in c40e92e4 (§6).
3. **`LOG_LEVEL=0` on its own does nothing** once `ENABLE_SERIAL_LOG` is gone. `LOG_LEVEL` is only
   read inside `#ifdef ENABLE_SERIAL_LOG` (`lib/Logging/Logging.h:44-66`). It is harmless and makes
   the env match CLAUDE.md, but it is not a second guard.
4. **Three documents say otherwise and will need updating:** AGENTS.md (the `c++2a` line), `.clangd`
   (`-std=c++2a`), and USER_GUIDE.md's "Serial logs" section, which tells users of the OTA build to
   run the serial monitor (§7).
5. **Measured:** with the flag removed, the release build still succeeds, adds no warnings, and is
   45,952 bytes smaller. The `SCREENSHOT_START`/`CMD:` strings are still in the binary (§8).

---

## 1. Ownership

| File | Role |
| --- | --- |
| `platformio.ini:178-190` | `[env:x4pro-gh_release]`. Sets `-DENABLE_SERIAL_LOG` (`:187`) and `-DLOG_LEVEL=1 ; Set log level to info for release builds` (`:188`) |
| `platformio.ini:160-176` | `[env:x4pro]` dev env. `-DENABLE_SERIAL_LOG` (`:169`), `-DLOG_LEVEL=2` (`:170`). Not touched by this issue |
| `platformio.ini:40` | `-std=gnu++2a` in `[base] build_flags`; `:75` unflags `-std=gnu++11` |
| `lib/Logging/Logging.h` | Macro gate. `LOG_LEVEL` defaults to 0 (`:30-32`). `LOG_ERR`/`LOG_INF`/`LOG_DBG` are real only under `#ifdef ENABLE_SERIAL_LOG`, and empty otherwise (`:44-66`). `logSerial` is the raw `HWCDC&` (`:34-36`) |
| `lib/Logging/Logging.cpp` | `logPrintf` prints if `logSerial` is connected (`:71-73`), then **always** appends to the RTC ring buffer (`:75`). `getLastLogs` reads the buffer back (`:78-91`) |
| `src/main.cpp:342-353` | The only `Serial.begin(115200)` in `src/`+`lib/` (`git grep -n "Serial.begin\|logSerial.begin\|setRxBufferSize\|HWCDCSerial" -- src lib` → `src/main.cpp:349` plus the `MySerialImpl::begin` wrapper at `Logging.h:78`). It is inside `#ifdef ENABLE_SERIAL_LOG`, next to `setTxTimeoutMs(1)` |
| `src/main.cpp:612-616` | 10 s heap print: `if (Serial && …) LOG_INF(…)`. The `LOG_INF` becomes empty; the `Serial` test and the timestamp stay |
| `src/main.cpp:618-633` | The `CMD:` handler. **No preprocessor guard.** Reads a line with `readStringUntil('\n')`, then on `CMD:SCREENSHOT` writes the 48,000-byte framebuffer (`display.getBufferSize()`) to serial |
| `lib/hal/HalPowerManager.cpp:70-76` | `logSerial.end()` before deep sleep, under `#ifdef ENABLE_SERIAL_LOG`. Stays consistent: nothing to end if nothing began |
| `lib/hal/HalSystem.cpp:131-139` | `getPanicInfo(true)` builds the crash report: version, panic reason, `"Last logs:\n" + getLastLogs()`, stack |
| `lib/hal/HalSystem.cpp:100-119` | `checkPanic()` writes that report to `/crash_report.txt` |
| `src/activities/network/WifiSelectionActivity.cpp:552` | `#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2`. Already off in release |
| `scripts/debugging_monitor.py:339-348` | The only host-side sender of `CMD:` lines (`git grep -n "CMD:" -- scripts tools bin`) |
| `freeink-sdk` (submodule, read-only) | `ENABLE_SERIAL_LOG` gates in `BoardConfig.h:1654`, `MemoryManager.cpp:26`, and seven sites in `PaperMonoDriver.cpp`. `ENABLE_SERIAL_LOG` is a global `build_flags` define, so removing it turns those off in release too. None of them is reachable on the X4 Pro in a way that matters: `holdPowerRails` logs only on a latch/bus collision, and PaperMono is a different panel |

`CLAUDE.md` is a symlink to `AGENTS.md` (`ls -la` → `CLAUDE.md -> AGENTS.md`), so there is one file
to edit.

## 2. Control flow today (release env, `ENABLE_SERIAL_LOG` + `LOG_LEVEL=1`)

1. **Boot.** Arduino `loopTask` calls `printBeforeSetupInfo()` only when `ARDUHAL_LOG_LEVEL >= DEBUG`
   or `shouldPrintChipDebugReport()` is true (`framework-arduinoespressif32/cores/esp32/main.cpp:52-60`).
   The weak default returns `false` (`:43-45`), nothing in `src`/`lib` overrides it (`git grep
   shouldPrintChipDebugReport -- src lib` → empty), and the rebuilt sdkconfig has
   `CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL=1` (`framework-arduinoespressif32-libs/esp32s3/sdkconfig:639`).
   So the core does **not** begin Serial. `main.cpp:99-100` begins it only when
   `ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE`, and `ARDUINO_USB_MODE=1` (`platformio.ini:32`).
2. **`setup()`.** Under `ENABLE_SERIAL_LOG`: `delay(250)`, `Serial.begin(115200)`,
   `setTxTimeoutMs(1)` (`src/main.cpp:342-353`). `HWCDC::begin` allocates the 256-byte RX queue
   (`cores/esp32/HWCDC.cpp:306-315`).
3. **Every `loop()`.** `LOG_*` call `logPrintf`, which prints when a host is connected and always
   writes the RTC ring buffer (`Logging.cpp:71-75`). `logSerial.available()` returns the queue depth
   (`HWCDC.cpp:562-567`). If any byte is waiting, `readStringUntil('\n')` blocks until a newline or
   `Stream::_timeout` runs out, 1000 ms by default (`cores/esp32/Stream.h:62`; nothing in `src/`
   calls `setTimeout` on `logSerial`). This is the stall the issue describes.
4. **Panic → reboot.** `checkPanic()` writes the ring buffer's 16 × 256-byte lines
   (`Logging.cpp:8-9`) into `/crash_report.txt` under "Last logs".

## 3. Control flow after removing `-DENABLE_SERIAL_LOG`

- `setup()` skips the `delay(250)` and `Serial.begin`. That is 250 ms off every cold and warm boot,
  a side effect worth stating in the spec.
- Nothing else begins the HWCDC (steps 1–2 above), so `rx_queue` stays `NULL`.
  `HWCDC::available()` returns `-1` when `rx_queue == NULL` (`HWCDC.cpp:562-565`). `-1 > 0` is false,
  so the handler body never runs and `readStringUntil` is never called. **The handler is unreachable
  but still compiled in**, including `display.getFrameBuffer()`, the two `printf` format strings,
  and the Arduino `String` code.
- `if (Serial && …)` at `main.cpp:612` still calls `HWCDC::isCDC_Connected()` every loop
  (`HWCDC.cpp:271-273`). The body is `LOG_INF` (now empty) plus `lastMemPrint = millis()`. That is
  harmless, but the code no longer does anything useful.
- `LOG_ERR`/`LOG_INF` in release become nothing (`Logging.h:62-66`), and so does their argument
  evaluation. A local whose only use is a `LOG_*` argument turns into an unused variable in the
  release build. §8 records what the build actually reports.
- The USB-Serial/JTAG peripheral itself is fixed-function silicon (`hwids 303A:1001` in
  `platforms/espressif32/boards/esp32-s3-devkitc1-n16r8.json`), so cable flashing with esptool keeps
  working. Only the application's CDC stream goes away.

## 4. The crash-report consequence

- `getLastLogs()` is used only by `HalSystem::getPanicInfo` (`git grep -n getLastLogs -- src lib` →
  `lib/hal/HalSystem.cpp:139`, plus its declaration/definition).
- `addToLogRingBuffer` is called only from `logPrintf` (`Logging.cpp:22,75`). `logPrintf` is called
  only through the macros (`Logging.h:46,52,58`), and those are empty without the flag.
- On the S3 the stack section is empty too: `__wrap_panic_print_backtrace` returns early under
  `#if !__riscv` before filling `panicStack` (`lib/hal/HalSystem.cpp:47-49`). A release crash
  report after this change will therefore hold `CrossPoint version`, `Panic reason`, and two empty
  sections.
- USER_GUIDE.md:388-389 asks users to attach that report to bug reports.

This does not overturn the owner's decision. It is a consequence the spec should state, and the
owner should see it before the PR merges. §6 shows upstream once weighed the same trade-off the
other way.

## 5. Installed versions

| Tool / package | Version | Evidence |
| --- | --- | --- |
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on PATH) |
| platform-espressif32 (pioarduino) | 55.03.37 | `platformio.ini:15`; `~/.platformio/platforms/espressif32/platform.json` `version` |
| framework-arduinoespressif32 | 3.3.7 | `package.json` `version` |
| framework-arduinoespressif32-libs (ESP-IDF) | 5.5.0+sha.87912cd291 | `package.json` `version` |
| toolchain-xtensa-esp-elf (GCC) | 14.2.0+20251107 | `package.json` `version` |
| Board | `esp32-s3-devkitc1-n16r8`, `board_build.mcu = esp32s3` | `platformio.ini:180-181` |

## 6. Nearest existing examples

- **c40e92e4** `fix: dump crash log without usb plugged, bump release log to INFO (#1332)`
  (upstream CrossPoint, 2026-03-06) is the change that put `LOG_LEVEL=1` into the release env
  (`-DLOG_LEVEL=0 ; … error` → `-DLOG_LEVEL=1 ; … info`). In the same commit, upstream made
  `logPrintf` fill the ring buffer even with no USB host connected (`Logging.cpp`, `HalSystem.cpp`).
  Release logging existed *so that crash reports carry logs*. This issue reverses the level change
  and removes the reason for the ring-buffer change.
- **27369bdd** `ci: build one board, and fix the release repo guard` is the most recent
  release-env edit in this fork. It is a `platformio.ini`-only change to the release env, with the
  consumers (`ci.yml`, `release-publish.yml` matrices) moved in the same commit. It is the model for
  scope: flag change plus every file that describes the flag, in one commit.
- **Compile-time gating of a debug-only block** already exists in the tree as
  `#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2` (`WifiSelectionActivity.cpp:552`) and
  `#ifdef ENABLE_SERIAL_LOG` (`main.cpp:342`, `HalPowerManager.cpp:70`). If the spec decides the
  `CMD:` handler must be compiled out rather than just unreachable, wrapping `main.cpp:618-633` in
  `#ifdef ENABLE_SERIAL_LOG` follows this pattern exactly. No new mechanism is needed.

## 7. Documents that describe the flag

| Location | Says | After the change |
| --- | --- | --- |
| `AGENTS.md:173-174` | release is "`LOG_LEVEL=0`, no serial logging" | becomes true |
| `AGENTS.md:176` | `C++20 (`-std=c++2a`)` | wrong: build uses `-std=gnu++2a` (`platformio.ini:40`). Owner asked to fix |
| `AGENTS.md:854-855` | OTA installs a build with `LOG_LEVEL=0` and no serial logging | becomes true |
| `.clangd:2` | `Add: [-std=c++2a]` | same mismatch as AGENTS.md:176. Editor-only, and outside the owner's wording |
| `USER_GUIDE.md:390-398` | "Serial logs. Connect the device over USB and run `debugging_monitor.py`" | false for anyone on an OTA/release build; needs a "dev builds only" qualifier |
| `platformio.ini:188` comment | `; Set log level to info for release builds` | goes away with the line |

Historical plans and reviews under `docs/superpowers/` quote `platformio.ini:187-188` as
`LOG_LEVEL=1` (for example `docs/superpowers/specs/2026-09-17-issue-51-design.md:449-456`). They are
dated records, not live docs, and should stay as they are.

## 8. Build evidence

Two release builds in this worktree, one after the other (`~/.platformio/penv/bin/pio run -e
x4pro-gh_release`). The first is `main` unchanged. The second is a **temporary, uncommitted** edit
that deletes `-DENABLE_SERIAL_LOG` and changes `:188` to `-DLOG_LEVEL=0`. `platformio.ini` was
restored afterwards (`git status --short` shows only this note).

| | Baseline (`2f303f6f`) | Flag removed | Δ |
| --- | --- | --- | --- |
| Exit | 0 | 0 | |
| `firmware.bin` | 5,536,720 B | 5,490,768 B | **−45,952 B** |
| Flash (pio summary) | 5,536,214 B (84.5 %) | 5,490,254 B (83.8 %) | −45,960 B |
| RAM (static) | 65,116 B | 65,116 B | 0 |
| Warnings in `src/` or `lib/` | 0 | 0 | |
| Warnings overall | 248, all from the one-time core rebuild (`managed_components/espressif__esp-sr`, `framework-espidf/components/usb`, `rmaker_common`) plus WebSockets | 1 (`WebSocketsClient.cpp:573`, deprecated `flush()`) | nothing new |

`strings firmware.bin | grep -c <pattern>`:

| Pattern | Baseline | Flag removed | Meaning |
| --- | --- | --- | --- |
| `[%lu] [%s] [%s] ` (logPrintf prefix) | 1 | **0** | `logPrintf` is dropped by the linker: no callers are left |
| `Free: %d bytes` (heap `LOG_INF`) | 1 | **0** | `LOG_INF` compiled out |
| `Dumped panic info` (`LOG_INF` in `checkPanic`) | 1 | **0** | same |
| `SCREENSHOT_START` | 1 | **1** | **the `CMD:` handler is still in the binary** (§3) |
| `CMD:` | 1 | **1** | same |

Caveat on the warning count: neither `platformio.ini` (`grep -n "Wall\|Wextra" platformio.ini` →
empty) nor the framework's build script (`framework-arduinoespressif32-libs/esp32s3/pioarduino-build.py`
has no `-Wall`/`-Wextra`) turns on `-Wall` for project sources, and `-Wunused-variable` is part of
`-Wall`. So "no new warnings" does **not** prove that no local is left used only by a log macro. It
proves only that the compiler was not asked to report them. Such locals cost nothing at `-Os`.

The ~45 KB flash saving comes from the format strings of every `LOG_ERR`/`LOG_INF` in the firmware
and the SDK, plus `logPrintf` and the ring-buffer writer, all leaving `.rodata`/`.text` together.
RAM is unchanged because the ring buffer is `RTC_NOINIT_ATTR` and stays declared.

## 9. Testability

- There is no host test for build flags. `test/` holds 60+ CMake suites for pure logic
  (`ls test`), and nothing parses `platformio.ini` (`grep -n "platformio\|ini" test/CMakeLists.txt`
  → only a version-pin comment at `:25`). The macro gate is plain preprocessor logic, so the
  honest checks are a release build, a symbol/strings check on the release ELF, and the human tester.
- Checks that can actually fail, all measured in §8: on the release binary,
  `strings firmware.bin | grep -c '\[%lu\] \[%s\] \[%s\] '` goes from 1 to 0, and
  `grep -c SCREENSHOT_START` stays at 1 unless the handler is also wrapped in `#ifdef`. On device, a release build should put nothing on `/dev/cu.usbmodem*`, and a panic
  should produce a `/crash_report.txt` with an empty "Last logs".
