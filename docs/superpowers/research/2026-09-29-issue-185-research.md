# Issue #185 — research

On the X4 Pro the battery icon never shows the charging bolt. This note records how the code behaves
on `main` (`a4e2288d`, freeink-sdk submodule at `310ec61`). Every claim cites a line I read or a
command I ran. Nothing here has been checked on hardware; the detect signal can only be settled on
the bench (see §6).

## 1. Which files own the behaviour

| Concern | File | Lines |
|---|---|---|
| USB presence, per board | `lib/hal/HalGPIO.cpp` | 334-351 (`isUsbConnected`) |
| USB edge latch | `lib/hal/HalGPIO.cpp` | 158-160 (in `update`), 163 (`wasUsbStateChanged`) |
| Boot classification | `lib/hal/HalGPIO.cpp` | 353-373 (`getWakeupReason`) |
| Public API | `lib/hal/HalGPIO.h` | 127 (`isUsbConnected`), 130 (`wasUsbStateChanged`), 132-134 (`WakeupReason`) |
| X4 Pro board profile | `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h` | 1347-1474; `usbDetect` at 1400 |
| `usbDetect` field | same file | 646 (`int8_t usbDetect;`) |
| Gauge charging read | `freeink-sdk/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp` | 183-215 (`readGaugeCharging`) |
| Hardware findings doc | `freeink-sdk/docs/xteink-x4pro-support.md` | 262-279 (RTC / USB / battery) |
| Bolt drawing | `src/components/themes/BaseTheme.cpp` | 74, 370 |
| Bolt drawing (Lyra) | `src/components/themes/lyra/LyraTheme.cpp` | 27 |
| Redraw on USB edge | `src/main.cpp` | 791-795 |
| Boot routing on wake reason | `src/main.cpp` | 375, 380, 436-464, 470 |
| Display resync on wake reason | `lib/hal/HalDisplay.cpp` | 29-32 |

`freeink-sdk` is a git submodule pointing at upstream (`git -C freeink-sdk remote -v` →
`https://github.com/Free-Ink/freeink-sdk.git`). Both files the acceptance criteria want corrected
(`BoardConfig.h:1400` and `xteink-x4pro-support.md:277-279`) live in it. `.claude/agents/hal-dev.md`
says: "Do not edit it to fix something that belongs in the HAL. If a change genuinely belongs
upstream, **stop and ask.**" That is an owner decision, recorded in §7.

## 2. Current control flow

### Charging bolt

1. `HalGPIO::update()` calls `isUsbConnected()` **every loop** and latches an edge:
   `usbStateChanged = (connected != lastUsbConnected)` (`HalGPIO.cpp:158-160`).
2. `main.cpp:793` requests a render when `wasUsbStateChanged()` is true.
3. The themes read `gpio.isUsbConnected()` again at draw time (`BaseTheme.cpp:74, 370`,
   `LyraTheme.cpp:27`) and draw the bolt when it is true.
4. `isUsbConnected()` (`HalGPIO.cpp:334-351`):
   - X3 branch (`deviceIsX3()`): reads BQ27220 `Current()` over I²C, true if > 0 mA. On this
     S3 build `_deviceType` is hard-set to `X4` (`HalGPIO.cpp:137-139`, the `#else` of
     `FREEINK_MCU_C3`), so this branch is never taken.
   - Otherwise: `if (BoardConfig::ACTIVE.usbDetect < 0) return false;` (`:347-349`), else
     `digitalRead(usbDetect) == HIGH` (`:350`).
5. The X4 Pro profile sets `usbDetect = PIN_UNASSIGNED` (`BoardConfig.h:1400`), so step 4 always
   returns false. The bolt never draws, and step 1 never sees an edge.

Note: nothing configures the detect pin. `grep -rn usbDetect freeink-sdk/libs src lib` finds only
`HalGPIO.cpp:347` and `:350` outside `BoardConfig.h`; there is no `pinMode(usbDetect, …)` anywhere.
The M5PaperS3 profile (`BoardConfig.h:1252`, `5, // usbDetect GPIO5, HIGH = USB present`) relies on
the same bare `digitalRead`. Whatever the X4 Pro signal turns out to be, the fix must decide whether
it needs an explicit `pinMode` (pull-up/down, or none on an externally driven line).

### Boot classification

`getWakeupReason()` (`HalGPIO.cpp:353-373`) calls `isUsbConnected()` once and returns:

| Reset reason | Wake cause | USB | Result |
|---|---|---|---|
| `ESP_RST_DEEPSLEEP` | GPIO or EXT1 | any | `PowerButton` |
| `ESP_RST_POWERON` | undefined | no | `PowerButton` |
| `ESP_RST_UNKNOWN` | undefined | yes | `AfterFlash` |
| `ESP_RST_POWERON` | undefined | yes | `AfterUSBPower` |
| anything else | | | `Other` |

Consumers:

- `main.cpp:380` — only `PowerButton` latches the recovery chord.
- `main.cpp:438-445` — `PowerButton` verifies the press duration (X4 Pro short-circuits to true,
  `HalGPIO.cpp:293-297`).
- `main.cpp:446-455` — **`AfterUSBPower` calls `powerManager.startDeepSleep(gpio)`** ("If USB power
  caused a cold boot, go back to sleep"), except on PaperMono.
- `main.cpp:470` — `PowerButton` is treated as a sleep wake for the boot presentation.
- `HalDisplay.cpp:29-32` — `PowerButton`, `AfterFlash`, `Other` request a display resync;
  `AfterUSBPower` does not.

Today, with USB always "absent" on the X4 Pro, a `POWERON` boot is always `PowerButton`, and the
`AfterFlash` / `AfterUSBPower` rows are unreachable. Once detection works, two rows become live:

- `POWERON` + USB → `AfterUSBPower` → immediate deep sleep. That is the intended behaviour for a
  battery-dead device that cold-boots when plugged in. It is also what happens if the owner presses
  Power on a fully-off device while it is on USB — the boot is `POWERON` either way, and this
  function cannot tell the two apart. The C3 X4 lives with the same ambiguity. Whether an X4 Pro
  ever reaches `ESP_RST_POWERON` with a press rather than a plug is a bench question; its normal
  off state is deep sleep (`ESP_RST_DEEPSLEEP` → `PowerButton`, unaffected).
- `UNKNOWN` + USB → `AfterFlash`. On the S3, the installed IDF has distinct `ESP_RST_USB` and
  `ESP_RST_JTAG` reasons (`esp_system.h:36-37`, see §3). A reset from the USB-Serial/JTAG
  peripheral (esptool over the native port) lands in `Other` rather than `AfterFlash`, and `Other`
  proceeds to boot with a resync, same as `AfterFlash`. So a flash reset is not newly misclassified
  in effect. That is inferred from the enum, not observed on the device.

## 3. Installed tool and package versions

Command: read `version` from each `~/.platformio/packages/*/package.json` and
`~/.platformio/platforms/espressif32/platform.json`.

| Package | Version |
|---|---|
| platform espressif32 (pioarduino) | 55.03.37 (`platformio.ini:15`) |
| framework-arduinoespressif32 | 3.3.7 |
| framework-arduinoespressif32-libs (IDF) | 5.5.0+sha.87912cd291 |
| toolchain-xtensa-esp-elf | 14.2.0+20251107 |
| freeink-sdk submodule | `310ec61` |

Board: `esp32-s3-devkitc1-n16r8` (`platformio.ini:166`), `-DFREEINK_DEVICE_X4PRO=1`,
`-DENABLE_SERIAL_LOG`, `-DLOG_LEVEL=2` for the `x4pro` env.

APIs that exist in this install, relevant to option 2 of the issue:

- `HWCDC::isPlugged()` → `usb_serial_jtag_is_connected()`
  (`framework-arduinoespressif32/cores/esp32/HWCDC.h:73-77`). The framework's comment says it is
  timer-based, not the SOF ISR, because the SOF ISR broke esptool uploads (`HWCDC.cpp:143-158`).
- `HWCDC::isConnected()` → `isCDC_Connected()` (`HWCDC.h:79-81`), which reflects an open CDC
  session, not bus power.

Both only see a USB **host** enumerating the native port. A wall charger sends no SOF frames, so
neither can detect it. I have not checked either on the device.

## 4. What the gauge can and cannot say

`readGaugeCharging()` returns `known = false` for the CW2017 (`BatteryMonitor.cpp:194-201`):
"CW2017 has no current register and the X4 Pro has no charger IC on this bus". The driver reads only
SoC (reg 0x04) and VCELL (regs 0x02/0x03) (`BatteryMonitor.cpp:86-87`, `:147-181`). Option 3 in the issue
(infer charging from VCELL/SoC trends) would have to be built from those two readings alone.

## 5. Nearest existing examples

- **Board-profile pin, read by the generic path.** M5PaperS3: `usbDetect = 5`, "HIGH = USB
  present" (`BoardConfig.h:1252`), read by `HalGPIO.cpp:350`. If the bench finds an active-HIGH
  VBUS GPIO, the fix is this shape: one value in the profile, no HAL code.
- **Board-specific branch in `isUsbConnected()`.** The X3 branch (`HalGPIO.cpp:335-346`) infers USB
  from the gauge's current sign, with a two-attempt retry. This is the model if the signal is not a
  plain active-HIGH GPIO (active-LOW, an ADC threshold, or the USB-Serial/JTAG state).
- **Board check idiom.** `BoardConfig::isX4Pro()` (`BoardConfig.h:1620`), already used in HAL at
  `HalGPIO.cpp:293` and in `main.cpp:381, 386`.
- **Host tests.** Boot routing is host-tested as pure functions in `src/boot/BootDecisions.h`
  (`test/boot_decisions/BootDecisionsTest.cpp`). `getWakeupReason()` is not: it calls
  `esp_sleep_get_wakeup_cause()` and `esp_reset_reason()` inline and has no host test
  (`grep -rln 'WakeupReason' test` → nothing). A pure `classifyWakeup(reset, cause, usb)` would be
  the way to test the table in §2 in isolation, following the `BootDecisions.h` pattern.

## 6. Pins the detect signal cannot be

From the X4 Pro profile and the findings doc, these are already claimed:

| GPIO | Role | Source |
|---|---|---|
| 0, 7, 3 | Left, Right, Power buttons | `BoardConfig.h:1396` |
| 1 | master peripheral rail (`power.latch0`) | `BoardConfig.h:1461-1468` (value `{1}` at 1468) |
| 2 | GT911 power enable, active-LOW | `BoardConfig.h:1403-1405`, value at 1425 |
| 4, 10 | GT911 RST, **INT** | `BoardConfig.h:1402`, values at 1414-1415 ("CONFIRMED ON HARDWARE: INT=GPIO10, RST=GPIO4") |
| 5 | SD enable, active-LOW | `BoardConfig.h:1380-1389` |
| 6, 11, 12, 13, 14, 18 | SSD1677 BUSY, MOSI, SCLK, CS, RST, DC | `BoardConfig.h:1377` |
| 8, 9 | frontlight cool, warm | `BoardConfig.h:1439` |
| 19, 20 | USB D−, D+ | `xteink-x4pro-support.md:267` |
| 38, 39 | shared I²C (GT911, RTC, CW2017) | `BoardConfig.h:1412-1413`, `:1453`, `:1459` |
| 40, 41, 42 | SDMMC DAT0, CLK, CMD | `BoardConfig.h:1448` |
| 45 | SD CS in the unused SPI view; boot strap | `BoardConfig.h:1389` |

Also off limits on an N16R8 module: GPIO26-32 (SPI flash) and GPIO33-37 (octal PSRAM). The module
is inferred from the board name `esp32-s3-devkitc1-n16r8`, not read from the part.

That leaves **GPIO15, 16, 17, 21, 43, 44, 46, 47, 48** as candidates for a bench probe. 43/44 are
UART0 TX/RX, unused as a console here because `ARDUINO_USB_CDC_ON_BOOT=1` routes `Serial` to the
native port. 46 is a boot strap. 15-17 are ADC2 channels, which matters if VBUS reaches the MCU
through a divider rather than as a logic level. None of this says which one, if any, carries VBUS.

The "GPIO10 is a candidate" comment at `BoardConfig.h:1400` is out of date: GPIO10 is the GT911 INT
line, confirmed on hardware in the same profile. The findings doc also contradicts itself on the
GT911 pins: `xteink-x4pro-support.md:125` says "RST=GPIO4, INT=GPIO10" (matching the profile), and
`:259` says "INT=GPIO4, RST=GPIO10". Line 259 is the stale one.

## 7. Scope and tier

The work touches `lib/hal/HalGPIO.cpp` and, for the acceptance criteria's comment and doc fixes, the
`freeink-sdk` submodule. The tier is already `heavy`, the highest, so there is no raise to make. Two
owner decisions come before implementation, in this order:

1. **The detect signal** (batch context: raise with `hpipe decide`, with a bench procedure; never
   guess). The spec phase will write a probe (dev build, logs levels of the §6 candidates, and the
   `usb_serial_jtag_is_connected()` state, on plug/unplug over a data cable and a wall charger), and
   ask the owner to run it.
2. **Where the submodule fixes land.** Either upstream to Free-Ink with a submodule bump, or an
   override on the bereanOS side. `hal-dev.md` forbids editing the submodule without asking.
