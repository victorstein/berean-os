# Issue #112 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-26-issue-112-design.md` against issue #112 (body and
owner comment) and `docs/superpowers/research/2026-09-26-issue-112-research.md`, on
`fix/112-release-serial-logging` at `f385f08c`.

## What holds up

I checked these against the tree and the installed packages. They are correct.

- The release env sets `-DENABLE_SERIAL_LOG` and `-DLOG_LEVEL=1 ; …` at `platformio.ini:187-188`. The
  dev env's are at `:169-170`, and the base has `-std=gnu++2a` at `:40`. The issue's `:190-191` and
  `main.cpp:617-631` have drifted. The spec's `:187-188` and `:618-633` are the current lines.
- The macro gate works as described. `LOG_LEVEL` defaults to 0 (`Logging.h:30-32`), is read only
  inside `#ifdef ENABLE_SERIAL_LOG` (`:44-61`), and all three macros are empty otherwise (`:62-66`).
  A-1's point that `LOG_LEVEL=0` is inert is correct.
- A-2's unreachability chain holds. The only `Serial.begin` in `src`, `lib` or `freeink-sdk/libs` is
  `src/main.cpp:349`, inside the `#ifdef` at `:342-353`. `grep -rn "Serial\.begin" src lib
  freeink-sdk` found only that line and the `MySerialImpl::begin` wrapper at `Logging.h:78`. In the
  framework, `HWCDC::HWCDC()` allocates nothing (`HWCDC.cpp:260-264`). `rx_queue` is created only
  from `begin()` (`:311-313`). `available()` returns `-1` when `rx_queue == NULL` (`:562-565`). The
  core calls `Serial.begin()` only under `ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE`
  (`main.cpp:99-100`), or from `printBeforeSetupInfo` (`chip-debug-report.cpp:294`). That function
  is reached only when `ARDUHAL_LOG_LEVEL >= DEBUG` or `shouldPrintChipDebugReport()` returns true
  (`main.cpp:43-60`). `CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL=1` (framework-libs `sdkconfig:639`, and the
  generated `sdkconfig.x4pro-gh_release:639`). `platformio.ini` sets no `CORE_DEBUG_LEVEL`.
- The X4 Pro uses the Serial log transport. `BoardConfig.h:314-321` falls through to
  `FREEINK_LOG_TRANSPORT_SERIAL`, so the `esp_rom_printf` branch at `Logging.cpp:65-69` is not a
  second output path.
- A-4's crash-report chain holds. The ring buffer's only writer is `logPrintf` (`Logging.cpp:22,75`).
  On Xtensa the backtrace wrapper returns before it fills `panicStack` (`HalSystem.cpp:47-49`).
  `clearPanic()` clears the logs on every non-panic boot (`HalSystem.cpp:88-89,128`), so stale
  pre-OTA logs cannot leak into a later report. "Last logs" really will be empty.
- A-2's reason for not wrapping the handler holds. `src/main.cpp` is on the list in
  `.claude/agents/hal-dev.md:21-26`. The issue's second option ("If it isn't wanted, drop
  `ENABLE_SERIAL_LOG` from the release env") does not ask for the guard. Offering the guard in the
  PR, without applying it, is a defensible reading.
- c40e92e4 did raise the release level from 0 to 1 (`git show c40e92e4 -- platformio.ini`).
  `CLAUDE.md -> AGENTS.md` is a symlink.

## Findings

### MAJOR 1: G4's document inventory misses two user-facing sites, one of which A-8's edit would contradict

**Claim.** G4 says "No user-facing document tells someone on the OTA build to do something that no
longer works." A-8 covers this with one sentence at `USER_GUIDE.md:390`. R §7 lists that as the only
user-facing site.

**Problem.** Two more places tell OTA users to rely on release serial output:

1. `USER_GUIDE.md:408-409`, in the same "Serial logs" section: *"Release builds log at a lower level
   than development builds, so a reproduction with the serial log is worth far more from a build
   flashed over USB than from an OTA image."* After A-1, release builds do not log at all. If A-8
   adds its sentence at `:390` ("the OTA build is silent on serial") and leaves `:408`, the section
   would say two different things 18 lines apart. G4 would fail, and the spec's own edit would
   create the contradiction.
2. `.github/ISSUE_TEMPLATE/bug_report.yml:56-57`: *"For a serial log, run `python3
   scripts/debugging_monitor.py`."* This is the form every field reporter fills in, and nearly all of
   them run the OTA build. After this change the instruction returns nothing.

**Evidence.** `git grep -n -i "debugging_monitor\|LOG_LEVEL\|serial log" -- ':!docs/superpowers'
':!src' ':!lib' ':!freeink-sdk'` returns both lines. Neither appears in the spec or in R §7.
`README.md:130` and `docs/contributing/testing-debugging.md:34` also match, but both come right after
a `pio run -t upload` of the dev env, so they stay true.

**Fix.** Add both to A-8 and the Architecture table:
- `USER_GUIDE.md:408-409`: replace with "OTA (release) builds produce no serial log; flash a
  development build over USB to capture one." Or fold it into A-8's `:390` sentence and delete
  `:408-409`.
- `bug_report.yml:57`: "For a serial log, flash a development build (see the User Guide) and run
  `python3 scripts/debugging_monitor.py`; OTA builds do not log over serial."

These are inline edits inside G4's existing scope, so this finding does not change the decision.

### MINOR 1: The tester step and data-flow step 5 say the port carries nothing, but the ROM and IDF secondary console still do

**Claim.** Data flow step 5: "The host sees the CDC device but receives no application output."
Human tester step 1: "`cat /dev/cu.usbmodem*` shows no application output." Step 4 forces a panic.

**Problem.** The release sdkconfig routes the ROM and IDF console to USB-Serial/JTAG as a secondary
console. On a reset the port can still show the ROM boot banner. IDF or Arduino-core error logs can
still appear there too, since `ARDUHAL_LOG_LEVEL` is ERROR. The forced panic in tester step 4 will
print the Guru Meditation and backtrace to the port. A tester who reads "shows nothing" literally
will fail a build that is correct. The owner's decision covers the app's `LOG_*` path, not the IDF
console, so no scope change is needed.

**Evidence.** `sdkconfig.x4pro-gh_release` (generated by the release build, gitignored per
`.gitignore:33`) has `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` (`:2255`),
`CONFIG_ESP_ROM_CONSOLE_OUTPUT_SECONDARY=y` (`:539`), `CONFIG_BOOT_ROM_LOG_ALWAYS_ON=y` (`:544`), and
`CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL_ERROR=y` (`:634`).

**Fix.** Make tester step 1 say: no `[<ms>] [ERR|INF|DBG] [<origin>]` lines appear. The ROM banner,
IDF `E (…)` lines, and a panic backtrace may still appear on the secondary console. Reword data-flow
step 5 to match.

### MINOR 2: A-3 removes a 250 ms gap after `holdPowerRails()` that every X4 Pro boot has had so far

**Claim.** A-3 says the delay exists only for USB enumeration, and removing it is purely a gain.

**Problem.** The code comment supports that intent. Still, on this device the delay sits between
`BoardConfig::holdPowerRails()` (`main.cpp:338`) and the first peripheral bring-up (`gpio.begin()`,
`powerManager.begin()`, `halTiltSensor.begin()`, then `Storage.begin()`, at `main.cpp:368-402`). Both
envs have carried `ENABLE_SERIAL_LOG` since the X4 Pro port (`git log -S ENABLE_SERIAL_LOG --
platformio.ini`: bbca4886, 27369bdd). So every X4 Pro build that has ever booted had 250 ms of rail
settle time there, and the release build becomes the only one without it. The dev env keeps the
delay (N1), so no dev test would show a hidden dependency on it. I found no evidence that such a
dependency exists. This is an untested timing change, not a known bug.

**Evidence.** `src/main.cpp:337-353` and the setup order after it. The delay came from a525606d, a
C3 fix that predates the X4 Pro latch code.

**Fix.** Add a human-tester step: from a full power-off (the latch) and from a deep-sleep wake, cold
boot the release build several times. Confirm that touch, the Home key and the SD card come up each
time. If any of them fails, restore a settle delay outside the `#ifdef`, which is a `main.cpp`
change for the orchestrator.

### MINOR 3: A-5 describes `isCDC_Connected()` as a side-effect-free register read

**Claim.** A-5: "`if (Serial && …)` still calls `HWCDC::isCDC_Connected()` each loop … a cheap
register read and has no side effects."

**Problem.** When USB is plugged in, the function writes registers. On first call it enables the
`SERIAL_IN_EMPTY` interrupt mask, and on every call it runs `usb_serial_jtag_ll_txfifo_flush()`. It
returns true only once `connected` is set, and `connected` is set by the HWCDC ISR. `begin()`
installs that ISR, so after this change `connected` never becomes true and the flush runs on every
loop while a cable is attached. That is still harmless in practice, and plugged-in-but-unopened
already behaved this way. The stated rationale is wrong, though, and an implementer who trusts it
could reason badly about the port's behaviour.

**Evidence.** Framework `cores/esp32/HWCDC.cpp:161-187` and `:271-273`.

**Fix.** Reword it: "cheap, and harmless. With USB attached it re-arms an interrupt mask once and
flushes the empty TX FIFO each loop." The conclusion stays the same.

### MINOR 4: Churn left a dangling cross-reference and a wrong line count

**Claim.** A-1: "a reader may take `LOG_LEVEL=0` to mean 'errors still log'. It does not; A-7's doc
wording says so." Architecture: "three lines of build configuration and three lines of
documentation."

**Problem.** A-7 is the `.clangd` edit and contains no such wording. No planned edit says that
`LOG_LEVEL=0` without the flag logs nothing. A-6 leaves `AGENTS.md:173-174` unchanged. And
`Logging.h:16`, which says "0 = ERR only", is exactly what invites that misreading. The line count
does not match the table either: 2 lines in `platformio.ini`, 1 in `.clangd`, 1 in `AGENTS.md` and 1
in `USER_GUIDE.md` (more after MAJOR 1).

**Evidence.** Spec lines 70-71 against A-7 at lines 135-139. Table at lines 164-169.

**Fix.** Either drop the mitigation claim from A-1, or name a real place for it. The cheapest is to
keep a trailing comment on the new line, `-DLOG_LEVEL=0 ; inert without ENABLE_SERIAL_LOG`, and
revise A-1's "no trailing comment" to match. Correct the count to match the final table.

## Verdict

The core design is sound. Removing the flag does what the owner decided, and the handler's
unreachability is backed by the framework source. MAJOR 1 is an omission inside G4's own scope and
can be fixed inline, with no change of decision or scope. The MINORs are accuracy and test-checklist
fixes.

VERDICT: CLEAR
