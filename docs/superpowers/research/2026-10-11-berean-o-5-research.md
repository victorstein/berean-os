# berean-o-5 — research: run the SD card at 40 MHz

Branch `perf/r2-sd-40mhz` at `1ba1cd34`. The run's base is `origin/main` `478cf1ed`, which adds only
the responsiveness docs (#253) and the 1.29.9 release (#254). `git diff --stat HEAD origin/main --
freeink-sdk .gitmodules lib/hal` is empty. Every claim below cites a line read in this worktree,
in the installed PlatformIO packages, or a command run here.

ESP-IDF paths are abbreviated:

- `IDF/` = `~/.platformio/packages/framework-espidf/components/`. This is the 5.5.2 source.
- `LIBS/` = `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/`. These are the
  precompiled libraries and headers the firmware actually links (see "Tooling").

## Who owns the behaviour

| Concern | Owner |
|---|---|
| Host config, clock request, mount retry loop | `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp:17-112` (`SdmmcBlockDevice::begin`) |
| The clock request itself | `SdmmcBlockDevice.cpp:24-25`: `host = SDMMC_HOST_DEFAULT(); host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz` |
| Opaque card handle | `SdmmcBlockDevice.h:48`, `void* _card`. `sdmmc_card_t` can't be forward-declared (`:45-47`). |
| Volume mount and the `[SD]` log lines | `freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:19-66`. "SDMMC card mounted" is at `:61`. |
| Board wiring: 1-bit, CLK41 CMD42 D0=40 | `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1445-1450`. Its comment claims "CONFIRMED on hardware: … 40 MHz". |
| SD enable, GPIO5 active-LOW | `BoardConfig.h:1382-1390` |
| SDMMC on, for X4 Pro / de-link / Paper Mono / Murphy M4 | `BoardConfig.h:303-305` |
| HAL entry point | `lib/hal/HalStorage.cpp:26`, `bool HalStorage::begin() { return SDCard.begin(); }` |
| The only call site | `src/main.cpp:406` in `mountStorage()`, called once from `setup()` at `:635` |

The `1-bit` in the brief comes from the board profile's `busWidth = 1` (`BoardConfig.h:1450`).
`platformio.ini:175` is only a comment that says so.

`detachFilesystemForRawAccess()` (`SDCardManager.cpp:68-76`) has no caller in `src/` or `lib/`
(`grep -rn detachFilesystemForRawAccess src lib` finds nothing). So the boot mount is the only
mount.

Nothing logs the SD clock today. `grep -rn -E "max_freq_khz|real_freq_khz|sdmmc_card_print_info|
sdmmc_host_get_real_freq|SDMMC_FREQ_"` over `src lib freeink-sdk test` finds one hit, line 25
itself.

## Current control flow

`SdmmcBlockDevice::begin`:

1. `SDMMC_HOST_DEFAULT()` (`LIBS/include/esp_driver_sdmmc/include/driver/sdmmc_default_configs.h:25-55`)
   gives these flags: `8BIT | 4BIT | 1BIT | DDR | DEINIT_ARG` = BIT(2,1,0,4,5) = **0x37**
   (`sd_protocol_types.h:202-207`). That matches the comment at `:20-21`. Its default
   `.max_freq_khz` is `SDMMC_FREQ_DEFAULT` (`:32`), so line 25 restates that default today.
2. `sdmmc_host_init()`, then `sdmmc_host_init_slot()` (`:39-43`). Slot init sets the **400 kHz
   probing clock** and a 1-bit bus (`IDF/esp_driver_sdmmc/src/sdmmc_host.c:822-829`).
3. `malloc` the card (`:51`), then a 4 KiB DMA bounce buffer in internal RAM (`:70-71`).
4. The retry loop runs up to 4 times (`:84-100`):
   - pulse GPIO5 HIGH for 80 ms, then LOW for 120 ms;
   - call `sdmmc_card_init()`;
   - if that fails **and** `card->csd.capacity == 0`, record the error and retry (`:92-95`);
   - otherwise read sector 0 (`:97`), and stop if the read succeeds.
5. On failure it prints `[SD] SDMMC mount failed after retries: <err>`, frees everything and
   returns false (`:101-109`). On success it publishes `_card` (`:110`).

`sdmmc_card_init()` (`IDF/sdmmc/sdmmc_init.c:46-176`):

- It begins with `memset(card, 0, …)` (`:49`), so `capacity`, `max_freq_khz` and `real_freq_khz`
  restart at 0 on every attempt.
- The steps that matter here run in this order: CSD (`:113`), select (`:122`), SCR (`:130`),
  `sdmmc_init_card_hs_mode` (`:143`), bus width (`:146-151`), SSR (`:160`),
  `sdmmc_init_host_frequency` (`:163`), then `sdmmc_check_scr` (`:169`).
- **No step sets the probing clock.** `grep -n "set_card_clk\|SDMMC_FREQ_PROBING" IDF/sdmmc/*.c`
  finds only `sdmmc_common.c:247-248`, which raises the clock. The 400 kHz clock comes from slot
  init alone.

Today's speed decision (`IDF/sdmmc/sdmmc_sd.c:486-494`):

```c
card->max_freq_khz = SDMMC_FREQ_DEFAULT;
if (card->host.max_freq_khz <= card->max_freq_khz) {
    card->max_freq_khz = card->host.max_freq_khz;
    return ESP_OK;
}
```

At `host.max_freq_khz = 20000` this returns early. The card is **never asked** to switch to
high-speed mode, and the host goes to 20 MHz: host divider 8 from PLL_F160M
(`sdmmc_host.c:258-260`; `SDMMC_CLK_SRC_DEFAULT = SOC_MOD_CLK_PLL_F160M`,
`LIBS/include/soc/esp32s3/include/soc/clk_tree_defs.h:481`).

The constants are in `LIBS/include/sdmmc/include/sd_protocol_types.h:215-216`:
`SDMMC_FREQ_DEFAULT 20000` and `SDMMC_FREQ_HIGHSPEED 40000`. The research note's F16 cites
`components/esp_driver_sdmmc/include/driver/sdmmc_types.h:215-216`. In this install they are in
`sdmmc/include/sd_protocol_types.h`, not there.

## What ESP-IDF 5.5.2 does once `host.max_freq_khz = SDMMC_FREQ_HIGHSPEED`

`sdmmc_enable_hs_mode_and_check` now calls `sdmmc_enter_higher_speed_mode`
(`sdmmc_sd.c:497`). The outcome depends on the card.

| Card | Path | Result |
|---|---|---|
| No CMD6: SD spec < 1.10, or no switch command class | `sdmmc_sd.c:238-241` returns `ESP_ERR_NOT_SUPPORTED` | `sdmmc_init_card_hs_mode` sets `card->max_freq_khz = SDMMC_FREQ_DEFAULT` and returns OK (`sdmmc_common.c:188-190`). **Clean fallback to 20 MHz.** |
| CMD6, but SDR25 (HS) not in the supported mask | `sdmmc_sd.c:302-307` returns `ESP_ERR_NOT_SUPPORTED` | The same clean fallback to 20 MHz. |
| HS-capable | CMD6 switch to SDR25 (`:308`), deselect, re-read CSD, reselect (`:509-528`). Then `card->max_freq_khz = MIN(host.max_freq_khz, SDR104) = 40000` (`:531`). | `sdmmc_init_host_frequency` sets the host divider to 4, which is exactly 40 MHz (`sdmmc_host.c:255-257`). Then `sdmmc_check_scr` re-reads SCR at 40 MHz and compares it (`sdmmc_sd.c:587-603`). |
| HS-capable, but the 40 MHz link is marginal | `sdmmc_check_scr` returns `ESP_ERR_INVALID_RESPONSE`: "got corrupted data after increasing clock frequency" | `sdmmc_card_init` **fails**. ESP-IDF has **no fallback** to 20 MHz. |
| A CMD6 exchange errors, e.g. a timeout or CRC failure at 400 kHz | an error other than NOT_SUPPORTED, returned from `sdmmc_init_card_hs_mode` (`sdmmc_common.c:191-192`) | `sdmmc_card_init` fails **before** the clock is raised. |

UHS-I and DDR50 can't engage, despite the DDR host flag:

- The DDR branch (`sdmmc_sd.c:259`) needs `card->is_uhs1`.
- That needs the card to answer S18A (`sdmmc_init.c:102`).
- The card is only asked for S18A when the slot has `SDMMC_SLOT_FLAG_UHS1`
  (`sdmmc_common.c:47-53`, `sdmmc_host.c:622-632`, `:646`). `SdmmcBlockDevice.cpp:37` adds only
  `INTERNAL_PULLUP`.
- `SOC_SDMMC_UHS_I_SUPPORTED` is not defined for the S3 (`soc_caps.h:573-574` has only
  `USE_GPIO_MATRIX` and `NUM_SLOTS`).

So the only new mode is SDR25 (HS).

## New failure paths in the mount retry loop

The loop's guard (`SdmmcBlockDevice.cpp:92`) retries only when CSD was never read. Every step that
fails *after* CSD falls through to the sector-0 read. Each HS failure above reaches that read in a
different state:

1. **HS switched, SCR check failed.** The host is already at 40 MHz (step `:163` ran before
   `:169`). Sector 0 is read at 40 MHz on the same link that just corrupted SCR, which probably
   fails.
2. **Every retry then identifies the card at the last clock, not 400 kHz.** Nothing in the loop
   or in `sdmmc_card_init` lowers the clock again (see above). Attempts 2–4 send
   CMD0/CMD8/ACMD41 at 40 MHz, against the SD spec's ≤ 400 kHz identification rate.
   - Today this already happens at 20 MHz whenever an attempt fails after `:163`.
   - The change doubles it, and makes it likely exactly when the link is marginal.
   - The likely outcome is four failed attempts, the SD error screen (`src/main.cpp:406-410`),
     and no way to boot. ESP-IDF won't fall back to 20 MHz on its own.
   - Whether the GPIO5 pulse (`:85-89`) truly removes card power, and so resets the card's bus
     speed mode, is not established. The comments call it a power cycle that "gates the card's
     data path" (`:56-60`).
3. **A CMD6 error before the clock rises.** Sector 0 is read at the 400 kHz probing clock, which
   succeeds, and the volume mounts **at 400 kHz** with no error printed.
   - `card->real_freq_khz` stays 0 in this case: it is set only in `sdmmc_init_host_frequency`
     (`sdmmc_common.c:268`), and the card was zeroed at `sdmmc_init.c:49`.
   - A log of `card->real_freq_khz` alone would print 0 here, not 400.
     `sdmmc_host_get_real_freq(slot, &khz)` (`LIBS/include/esp_driver_sdmmc/include/driver/sdmmc_host.h:256`,
     `sdmmc_host.c:354-367`) reads the real dividers, so it shows the true clock.
   - The same partial-init mount can already happen today if SSR or SCR fail after CSD.

The data timeout follows the clock: 100 ms of cycles (`sdmmc_host.c:294-299`, `:342`). No
timeout constant needs to change.

## What to log, and where it shows up

- **The SDK's idiom.** It has no `LOG_*` macros (`grep -rln "LOG_INF\|LOG_DBG" freeink-sdk/libs`
  finds nothing). It logs with `if (Serial) Serial.printf("[%lu] [TAG] …\n", millis(), …)`, for
  example `SDCardManager.cpp:61` and `XteinkDetect.cpp:351-353`. That XTDET line,
  `promoted SSD1677 -> UC8179`, is in the owner's baseline capture (baseline note line 11), so
  this idiom reaches the log on the owner's setup.
- **Where `Serial` comes up.** `Serial.begin` runs at `src/main.cpp:349`, after a 250 ms
  enumeration delay, and before `mountStorage` (`setup()` `:630` → `:635`). `if (Serial)` is
  `HWCDC::isCDC_Connected()` (`framework-arduinoespressif32/cores/esp32/HWCDC.cpp:271-273`).
- **What the card struct offers.** `sdmmc_card_t` has `max_freq_khz` ("maximum frequency
  supported by the card", `sd_protocol_types.h:265`) and `real_freq_khz` ("real working
  frequency configured on the host", `:266`).
- **`sdmmc_card_print_info` is the wrong tool.** It writes to a `FILE*` (`sdmmc_cmd.h:39`), not
  `Serial`. It prints `Speed: N/A` when `real_freq_khz == 0` (`sdmmc_common.c:329-336`).
- **ESP-IDF's own error lines.** The prebuilt IDF compiles logs up to ERROR only:
  `CONFIG_LOG_MAXIMUM_LEVEL 1` in `LIBS/{dio,qio}_opi/include/sdkconfig.h`. So "got corrupted
  data after increasing clock frequency" is in `libsdmmc.a` (`strings`), but its `ESP_LOGD`
  traces are compiled out. Whether those ESP-IDF error lines reach the USB CDC capture is not
  established.
- **Where the clock is known.** `_card` is a `void*` private to `SdmmcBlockDevice`
  (`SdmmcBlockDevice.h:48`), so `SDCardManager` can't read the clock without a new accessor. The
  natural place to log is `SdmmcBlockDevice::begin`, after `:110`, or a value it returns to
  `SDCardManager.cpp:61`.

## The mechanism behind the expected gain

A 1-bit bus at 20 MHz moves 20 Mbit/s, about 2.5 MB/s raw. At 40 MHz it moves about 5 MB/s.

Each `readSectors` call is split into commands of at most 8 sectors (4 KiB,
`SdmmcBlockDevice.h:43`, `.cpp:132-140`), each followed by a `memcpy` out of the DMA bounce
buffer. Per-command overhead, card access latency and the `memcpy` don't scale with the clock.
So the wall-clock gain is under 2× and has to be measured on the device. This note claims no
figure.

The baseline figures this touches (`docs/superpowers/notes/responsiveness-baseline.md`, on `main`
since #253):

| Measurement | Baseline | Source |
|---|---|---|
| `book.bin` load | 276 ms (`BMC Loaded cache data`) | `:69` |
| `Book loaded in` | 505 ms | `:43`, `:71` |
| Section build | 1.2–1.8 s | `:47` |
| First Bible search open | 2.2 s (`BSEARCH 1189 verse documents`) | `:48` |
| Later search opens | ~500 ms | `:49` |
| Verse scan at wake | 405 ms | `:52`, `:75` |
| `Study pick` at sleep | 445 ms | `:53`, `:76` |

## Tooling actually installed

- Platform `espressif32` 55.03.37 (`platformio.ini:15`;
  `~/.platformio/platforms/espressif32/platform.json:21`).
- Arduino core 3.3.7 (`framework-arduinoespressif32/package.json`).
- Prebuilt libraries, which are what links: `LIBS/versions.txt`, `esp-idf: release/v5.5
  87912cd291`. `esp_idf_version.h:14-18` gives 5.5.2.
- ESP-IDF source: `framework-espidf/version.txt` `5.5.2.260206`.
  - `diff -r` of both `sdmmc/include` and `esp_driver_sdmmc/include` against `LIBS/include/…`
    shows them identical.
  - `strings LIBS/lib/libsdmmc.a` contains the `sdmmc_check_scr` error string read above.
  - So the source read here matches the code that links.
- PlatformIO Core 6.1.19, at `~/.platformio/penv/bin/pio`. It is not on `PATH`.
- No host test compiles anything under `freeink-sdk/`. The only mention in `test/` is a comment
  (`test/pagination/CMakeLists.txt:10`). The ESP-IDF SDMMC driver has no host stub.

## Fork and landing mechanics

- `.gitmodules` sets `url = https://github.com/victorstein/freeink-sdk.git` and
  `branch = berean`.
- `git submodule status` gives `67f7e012…`. `git ls-remote origin berean` gives the same SHA, so
  the gitlink is at the tip of `berean`. `main` is at `a1cf5b4b`.
- The submodule clone is **shallow** (`git rev-parse --is-shallow-repository` → `true`). A commit
  on `berean` still works. A push from a shallow clone is a fast-forward of one new commit on
  `67f7e01`.
- Pushing to `berean` needs the owner's approval (`.claude/agents/hal-dev.md`, "freeink-sdk is a
  submodule").

## Nearest existing examples

- **An SDK change plus a gitlink bump:** PR #190, commit `2822662d`. It is the last commit to
  touch `freeink-sdk` (`git log -- freeink-sdk`). It changed a board-profile value in the SDK
  and pushed it to `berean` as a fast-forward, `310ec61..67f7e01`. It bumped the gitlink, and its
  PR body names the fork range, "SDK fork", "For existing clones" and the owner's device checks.
  Its host-tested logic lives in bereanOS (`lib/Input`), not in the SDK.
- **An SDK log line on this device:** `XteinkDetect.cpp:351-353` and `SDCardManager.cpp:61`. Both
  are `if (Serial) Serial.printf("[%lu] [TAG] …", millis(), …)`.

## Implications for the design (facts, not decisions)

- **The one-line change is safe for cards without HS.** ESP-IDF falls back to 20 MHz cleanly.
- **It is not safe for an HS-capable card on a marginal link.** ESP-IDF fails the init. The loop
  then retries identification at 40 MHz, and the device can't mount. The brief's "check the mount
  retry loop for any new failure path" lands here.
  - One remedy is resetting the clock to `SDMMC_FREQ_PROBING` before each attempt.
  - Another is falling back to `SDMMC_FREQ_DEFAULT` for the remaining attempts after an HS
    failure.
  - Both live in `SdmmcBlockDevice.cpp:84-100`.
- **A partial init can mount at 400 kHz without a word.** The log should read the host's real
  clock (`sdmmc_host_get_real_freq`), not only `card->real_freq_khz`.
- **The change reaches more than this device.** `SdmmcBlockDevice` is shared by every
  `FREEINK_SD_SDMMC` board (`BoardConfig.h:304-305`). bereanOS builds only X4 Pro
  (`platformio.ini:164-193`), and `berean` is bereanOS's fork.
- **Neither the clock nor the retry policy can be host-tested without a new pattern.** That would
  mean a pure helper in the SDK plus a bereanOS host test that reaches into `freeink-sdk/`.
  Verification otherwise rests on `pio run` and the owner's boot log.

## Sibling tasks in this run

`hpipe show --task t1…t7`: no other task in run `…-y98z` holds `freeink-sdk`, `.gitmodules` or
`lib/hal`. This task's hold is `freeink-sdk, lib/hal/HalStorage, .gitmodules`. The siblings are
on the `ui`, `epub` and `data` surfaces.

## Tier

It stays **standard**. The change is confined to the `hal` surface's SDK boundary. It changes no
on-disk format, no persisted store and no cross-surface contract, and needs no migration. The
retry-loop hardening is in the same function the brief already names.
