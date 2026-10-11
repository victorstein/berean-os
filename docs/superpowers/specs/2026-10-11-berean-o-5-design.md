# berean-o-5 — run the SD card at 40 MHz: design

Branch `perf/r2-sd-40mhz`, bead `berean-o-5`, run task `t5`, surface `hal`, review tier `standard`.

- **Research:** `docs/superpowers/research/2026-10-11-berean-o-5-research.md`, cited below as R§.
- **Model:** `docs/superpowers/specs/2026-09-29-issue-185-design.md`, the last SDK change that
  landed through the fork and a gitlink bump (PR #190, commit `2822662d`). This spec follows its
  shape: numbered labelled assumptions, the fork mechanics in their own section, and device checks
  written for the owner.

ESP-IDF paths use R's abbreviations:

- `IDF/` = `~/.platformio/packages/framework-espidf/components/`, the 5.5.2 source.
- `LIBS/` = `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/`, which is what
  links.

R§Tooling shows the two match. `SDK/` = `freeink-sdk/`.

## 1. Problem

`SdmmcBlockDevice::begin` asks for the default-speed clock while its comment claims 40 MHz:

    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz

That is `SDK/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp:25`.

- `SDMMC_FREQ_DEFAULT` is `20000` and `SDMMC_FREQ_HIGHSPEED` is `40000`
  (`LIBS/include/sdmmc/include/sd_protocol_types.h:215-216`).
- With the host capped at 20 MHz, ESP-IDF never asks the card to switch to high speed
  (`IDF/sdmmc/sdmmc_sd.c:489-493`). It clocks the bus at 160 MHz / 8 = 20 MHz
  (`IDF/esp_driver_sdmmc/src/sdmmc_host.c:258-260`).
- The bus is 1-bit (`SDK/libs/hardware/BoardConfig/include/BoardConfig.h:1450`). So every SD read
  in the firmware moves at most about 2.5 MB/s raw, against about 5 MB/s at 40 MHz.

Three comments repeat the wrong claim:

- `SdmmcBlockDevice.cpp:20-23`: "the data clock at 40 MHz";
- `BoardConfig.h:1446`: "CONFIRMED on hardware: 1-bit, slot 1, CLK=41 CMD=42 DAT0=40, internal
  pull-ups, 40 MHz";
- `SDK/docs/xteink-x4pro-support.md:219`: "1-bit mode, slot 1, internal pull-ups, 40 MHz".

Nothing logs the clock the card actually runs at (R§Who owns), so no device log could have caught
this.

Changing the constant alone opens a new failure path (R§New failure paths):

- An HS-capable card on a marginal link fails `sdmmc_card_init` at the post-switch SCR check
  (`IDF/sdmmc/sdmmc_sd.c:587-603`). ESP-IDF does not fall back to 20 MHz.
- The mount loop never lowers the clock between attempts. Neither `sdmmc_card_init`
  (`IDF/sdmmc/sdmmc_init.c:46-176`) nor the loop (`SdmmcBlockDevice.cpp:84-100`) sets the probing
  clock; only slot init does (`sdmmc_host.c:822-823`).
- So attempts 2–4 would identify the card at 40 MHz, and likely all fail. The device would then
  stop at the SD error screen (`src/main.cpp:406-410`).

Baseline costs this touches, from the owner's X4 Pro, `x4pro` build of `1ba1cd34`
(`docs/superpowers/notes/responsiveness-baseline.md`):

| Measurement | Baseline | Note line |
|---|---|---|
| `BMC Loaded cache data` after `Loading ePub` | 276 ms | `:69` |
| `Book loaded in` | 505 ms | `:43`, `:71` |
| Section build of a new chapter | 1.2–1.8 s | `:47` |
| First Bible search open (`BSEARCH 1189 verse documents`) | 2.2 s | `:48` |
| Later search opens | ~500 ms | `:49` |
| `HOMEV Verse scan` at wake | 405 ms | `:52`, `:75` |
| `SLP Study pick` at sleep | 445 ms | `:53`, `:76` |

## 2. Goal

1. Ask ESP-IDF for high speed, so a card that supports it runs at 40 MHz.
2. Keep every card that mounts today mounting:
   - a card without high speed falls back to 20 MHz inside ESP-IDF;
   - an HS-capable card that fails at 40 MHz gets 20 MHz attempts in our retry loop.
3. Print the clock the card really runs at, once, after the boot mount, on the line that
   `if (Serial)` lines in the SDK already use.
4. Correct the three comments in §1 so the SDK stops claiming a clock it did not run.
5. Land the SDK change on the fork `victorstein/freeink-sdk` branch `berean`, and bump the gitlink
   in the bereanOS PR.

## 3. Non-goals

- **4-bit bus.** The X4 Pro is wired 1-bit; `BoardConfig.h:1447` says D1–D3 are unused.
- **UHS-I, SDR50, DDR50.** The S3 doesn't support them: `SOC_SDMMC_UHS_I_SUPPORTED` is absent
  from `soc_caps.h:573-574` (R§What ESP-IDF does).
- **A clock field in `BoardConfig::SdmmcPins`.** That would change the struct every board profile
  initialises (`BoardConfig.h:435-443`, the X4 Pro at `:1450`), which is a contract change. The
  shared `SdmmcBlockDevice` gets one policy for every `FREEINK_SD_SDMMC` board
  (`BoardConfig.h:304-305`). bereanOS builds only the X4 Pro (`platformio.ini:164-193`), and
  de-link, Paper Mono and Murphy M4 are not validated here.
- **Changing the retry budget, the GPIO5 power cycle, or the DMA bounce buffer**
  (`SdmmcBlockDevice.cpp:61-76`, `:85-90`).
- **Larger transfers.** `kMaxTransferSectors = 8` (`SdmmcBlockDevice.h:43`) caps each command at
  4 KiB. Raising it is a separate lever with its own memory cost.
- **The USB-MSC remount path.** `detachFilesystemForRawAccess()` has no caller in bereanOS
  (R§Who owns).
- **New log lines in bereanOS `src/` or `lib/hal`.** `lib/hal/HalStorage.cpp:26` is unchanged.
- **A claimed speed-up.** This spec gives the mechanism; only the owner's device run gives a figure.

## 4. Architecture

All code changes are in `SDK/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp`. The comment and
doc corrections touch `BoardConfig.h` and `xteink-x4pro-support.md` in the same SDK commit. The
bereanOS PR changes only the `freeink-sdk` gitlink and adds this bead's docs.

### 4.1 The clock request

**Assumption A1 — the host asks for `SDMMC_FREQ_HIGHSPEED` and lets ESP-IDF negotiate down.**
`host.max_freq_khz` is set per attempt from the schedule in A3. The first entry is
`SDMMC_FREQ_HIGHSPEED`. With `host.max_freq_khz = 40000`, ESP-IDF behaves as follows:

- **The card supports HS.** ESP-IDF sends CMD6 to switch it to SDR25
  (`IDF/sdmmc/sdmmc_sd.c:302-313`). It re-reads the CSD (`:509-528`) and sets
  `card->max_freq_khz = 40000` (`:531`). The host divider becomes 4, which is exactly 40 MHz
  (`sdmmc_host.c:255-257`). Then it re-reads SCR and checks it (`sdmmc_sd.c:587-603`).
- **The card lacks CMD6 or SDR25.** ESP-IDF returns `ESP_ERR_NOT_SUPPORTED` (`sdmmc_sd.c:238-241`,
  `:304-306`). `sdmmc_init_card_hs_mode` turns that into 20 MHz and success
  (`IDF/sdmmc/sdmmc_common.c:188-190`).

UHS-I/DDR can't engage: the slot never sets `SDMMC_SLOT_FLAG_UHS1` (`SdmmcBlockDevice.cpp:37`;
R§What ESP-IDF does). The host flags stay `SDMMC_HOST_DEFAULT()`'s 0x37 (`SdmmcBlockDevice.cpp:24`).

### 4.2 Each attempt starts from the probing clock

**Assumption A2 — every attempt first resets the host to `SDMMC_FREQ_PROBING`, before the GPIO5
pulse.** The call is `sdmmc_host_set_card_clk(host.slot, SDMMC_FREQ_PROBING)`. It is declared in
`LIBS/include/esp_driver_sdmmc/include/driver/sdmmc_host.h:141`, which `SdmmcBlockDevice.cpp:12`
already includes. 400 kHz gives a host divider of 10 and a card divider of 20
(`sdmmc_host.c:261-263`).

- **Why before the pulse.** Attempt 1 today runs the card's power-up and identification under the
  400 kHz clock that slot init left (`SdmmcBlockDevice.cpp:40`, `sdmmc_host.c:822-823`). Resetting
  first makes attempts 2–4 identical to attempt 1, instead of powering the card up under whatever
  clock the last attempt raised.
- **On attempt 1 it changes nothing.** The clock is already 400 kHz.
- **It already matters today.** An attempt that fails after `sdmmc_init_host_frequency`
  (`sdmmc_init.c:163`) leaves the bus at 20 MHz for the next identification. This fixes that at
  the same time.
- **On failure** the attempt is spent: `mountErr` takes the error, and the loop continues to the
  next attempt. A later failure of the same call would end the loop on today's failure path (§6).

### 4.3 Falling back to 20 MHz inside the retry loop

**Assumption A3 — a fixed four-entry schedule: attempts 1–2 at `SDMMC_FREQ_HIGHSPEED`, attempts
3–4 at `SDMMC_FREQ_DEFAULT`.** It is a file-scope `constexpr int` array in `SdmmcBlockDevice.cpp`,
with the loop bound derived from its length:

    constexpr int kAttemptClockKhz[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_HIGHSPEED,
                                        SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT};

- **The element type.** It is `int` because `sdmmc_host_t::max_freq_khz` is an `int`
  (`sd_protocol_types.h:214`).
- **Why the .cpp.** `SDMMC_FREQ_*` come from the ESP-IDF headers, which only the .cpp includes
  (`SdmmcBlockDevice.cpp:11-13`). The header includes only `BoardConfig.h` and `SdFat.h`
  (`SdmmcBlockDevice.h:16`, `:22`).
- **The naming.** It follows the SDK's `k`-prefixed constants (`SdmmcBlockDevice.h:42-43`).
- **The total stays four.** Today's budget is `attempt < 4` (`SdmmcBlockDevice.cpp:84`). That
  keeps the time to the SD error screen with no card in the slot as it is.
- **Why two at high speed.** The power-cycled retry exists for a mount-sequencing problem,
  "not a clock-margin one" (`SdmmcBlockDevice.cpp:22-23`). Giving HS a second attempt keeps one
  sequencing retry before 20 MHz.
- **Why a fixed schedule.** An HS-capable card on a marginal link fails `sdmmc_check_scr` with
  `ESP_ERR_INVALID_RESPONSE`. The old sequencing failures were sector reads with `0x107`, which is
  `ESP_ERR_TIMEOUT` (`SdmmcBlockDevice.cpp:57-60`, `:79-81`; `esp_err.h:31`). A 40 MHz read can also time out on
  a marginal link. So no single error code tells "sequencing" from "clock margin", and the
  schedule doesn't try to.

The rejected alternatives:

- **Four HS attempts, then two at 20 MHz.** That keeps three sequencing retries at HS, but every
  boot of a marginal card pays four failed attempts (~200 ms of GPIO5 delays each, `:87-89`), and
  a boot with no card pays two extra.
- **Switching to 20 MHz on the first HS failure, whatever the error.** It gives sequencing no
  retry at HS.

**Assumption A4 — switching to 20 MHz does not depend on the card leaving HS mode.** The card may
still be in high-speed mode from an earlier attempt, if the GPIO5 pulse doesn't remove its power
(R§New failure paths: unestablished). Even so, ESP-IDF with `host.max_freq_khz = 20000` takes the
early return (`sdmmc_sd.c:490-493`) and clocks the host at 20 MHz. Clocking a high-speed card
below 50 MHz is within its timing. So the 20 MHz attempts don't depend on that unknown.

### 4.4 The partial-init mount stays tolerated

**Assumption A5 — the loop keeps accepting a sector-0 read after a post-CSD `sdmmc_card_init`
failure** (`SdmmcBlockDevice.cpp:92-99`, unchanged).

The comment there explains why: it proves real block I/O before committing. The change adds one
more way to reach that path: a CMD6 exchange that errors at 400 kHz (R§New failure paths, item 3).
The volume then mounts at 400 kHz.

Refusing such a mount would trade a slow boot for no boot, on a card that mounts today. The log
line in A6 prints `400 kHz`, so the owner sees it. A slow mount at 400 kHz is something the device
check in §7.3 would catch, and it goes to `hpipe discover` if it appears. It is not fixed
speculatively here.

### 4.5 The boot log line

**Assumption A6 — one line, printed by `SdmmcBlockDevice::begin` on success, from the host's real
dividers.** It goes just before `_card = card;` (`SdmmcBlockDevice.cpp:110`). Format:

    [<millis>] [SD] SDMMC clock <real> kHz (requested <host.max_freq_khz> kHz, attempt <n>/<total>)

- **The style.** It mirrors the failure line at `SdmmcBlockDevice.cpp:102-103` and the SDK's
  `if (Serial) Serial.printf("[%lu] [TAG] …", millis(), …)` idiom (`SDCardManager.cpp:61`,
  `XteinkDetect.cpp:351-353`). The SDK has no `LOG_*` macros (R§What to log).
- **The value.** `<real>` comes from `sdmmc_host_get_real_freq(host.slot, &khz)` (`sdmmc_host.h:256`,
  `sdmmc_host.c:354-367`), seeded with `card->real_freq_khz`. That call writes its output only
  after its checks pass (`:356-364`), so if it fails the seed is printed instead.
  `card->real_freq_khz` alone would print `0` after a partial init, because `sdmmc_card_init`
  zeroes the card (`sdmmc_init.c:49`) and only `sdmmc_init_host_frequency` sets it
  (`sdmmc_common.c:268`). The real dividers print `400`.
- **Requested beside real.** Together they tell three things apart: the card refused HS (`20000`
  with `requested 40000`), the loop fell back (`requested 20000`, `attempt 3/4` or `4/4`), and a
  partial init (`400`).
- **Once.** `begin` runs once per boot: the only caller is `src/main.cpp:406` via
  `HalStorage.cpp:26` (R§Who owns).
- **Visibility.** The idiom's output reaches the owner's capture: the baseline records
  `XTDET promoted SSD1677 -> UC8179` (baseline note `:11`) from `XteinkDetect.cpp:351-353`. Like
  every SDK line, it is not gated by `LOG_LEVEL`. In `x4pro-gh_release` it prints only if a CDC
  host is connected (`HWCDC.cpp:271-273`), as `SDMMC card mounted` already does.

**Assumption A7 — the failure line and `SDMMC card mounted` stay as they are**
(`SdmmcBlockDevice.cpp:102-103`, `SDCardManager.cpp:61`). The brief asks for the clock once after
mount. The mount line comes from a different class, which can't read the opaque card
(`SdmmcBlockDevice.h:45-48`) without a new accessor.

### 4.6 Comments and the SDK doc

**Assumption A8 — the three wrong "40 MHz" claims are corrected in the same SDK commit.**

- `SdmmcBlockDevice.cpp:20-25`. The OEM claim stays an OEM claim, and the code comment says what
  the code now does:
  - the host asks for `SDMMC_FREQ_HIGHSPEED` (40 MHz);
  - ESP-IDF negotiates 20 MHz for a card without high speed;
  - the retry loop falls back to 20 MHz for the last two attempts.
  The trailing `// 40 MHz` on the assignment goes. The constant's name says it.
- `BoardConfig.h:1446`. "40 MHz" leaves the CONFIRMED list, because it was never confirmed: the
  code ran 20 MHz. A separate clause says the mount requests 40 MHz high speed, and the boot log
  prints the clock the card actually runs at.
- `SDK/docs/xteink-x4pro-support.md:219`. The same correction, plus one sentence in the
  mount-sequence list (`:232-243`) on the per-attempt probing reset and the 20 MHz fallback.

Comments are written for the merged state, per the root `CLAUDE.md` comment rules.

### 4.7 The SDK fork and the gitlink

The fork is already the submodule's remote:

- `.gitmodules` is `url = https://github.com/victorstein/freeink-sdk.git`, `branch = berean`.
- The gitlink `67f7e012` equals `git ls-remote origin berean` (R§Fork).
- CI checks out submodules recursively (`.github/workflows/ci.yml:46`, `:71`, `:94`, `:131`). So
  the new SDK commit must exist on the fork before the bereanOS PR's CI runs.

**Assumption A9 — one SDK commit, fast-forwarded onto `berean`, as #185 did.**

- It is made in `freeink-sdk/` on top of `67f7e012`. The submodule clone is shallow (R§Fork),
  which doesn't stop a one-commit fast-forward.
- Message: `perf: clock the SDMMC card at 40 MHz high speed`.
- Push: `git -C freeink-sdk push https://github.com/victorstein/freeink-sdk.git
  HEAD:refs/heads/berean`. No force; it is never pushed to Free-Ink. This is the #185 push form
  (`2026-09-29-issue-185-design.md` §4.6, A11).
- **The push needs the owner's approval** (`.claude/agents/hal-dev.md`, "pushed to `berean` only
  with the owner's approval"). That is decision D1, raised before the push at implement time.

**Assumption A10 — the bereanOS PR carries the gitlink bump and no other code.** `.gitmodules` is
unchanged. The PR body names:

- the fork commit's SHA and the range `67f7e01..<sha>`;
- the "For existing clones" line from the brief;
- the device checks in §7.3.

The branch is rebased onto `origin/main` (`478cf1ed`, docs and a release only, R preamble) before
the PR opens, so the PR diff is this bead's files alone.

## 5. Data and control flow after the change

`SdmmcBlockDevice::begin`, with the changed steps in **bold**:

1. `host = SDMMC_HOST_DEFAULT()`. **The per-attempt clock comes from `kAttemptClockKhz`**
   (`:24-25`).
2. Slot config and `sdmmc_host_init_slot`, which leaves 400 kHz and a 1-bit bus (`:29-43`).
3. The card struct, the GPIO5 `pinMode`, and the DMA buffer (`:51-76`). Unchanged.
4. For each attempt `i` in `0 .. kMountAttempts-1`:
   1. **`sdmmc_host_set_card_clk(host.slot, SDMMC_FREQ_PROBING)`. On error, record it and go to
      the next attempt.**
   2. GPIO5 HIGH 80 ms, then LOW 120 ms (`:85-90`). Unchanged.
   3. **`host.max_freq_khz = kAttemptClockKhz[i]`.**
   4. `sdmmc_card_init(&host, card)`. On HS-capable cards with `i < 2`, it switches the card to
      SDR25 and the host to 40 MHz. Otherwise it runs at 20 MHz.
   5. Unchanged: if init failed and `capacity == 0`, retry. Otherwise read sector 0, and stop if
      the read succeeds (`:92-99`).
5. On failure: the unchanged failure line and cleanup (`:101-109`).
6. On success: **query the real clock and print the A6 line**, then `_card = card` (`:110`).

`SDCardManager::begin` then mounts the volume and prints `SDMMC card mounted`
(`SDCardManager.cpp:54-61`). That is unchanged.

**Resources.**

- No new allocation in the SDK. `kAttemptClockKhz` is 16 bytes of `constexpr` data.
- The HS switch makes ESP-IDF allocate one transient 64-byte `MALLOC_CAP_DMA` buffer, freed in the
  same call (`sdmmc_sd.c:245`, `:316`; `sd_protocol_types.h:108`).
- The printed line uses no buffer of ours. It is a `Serial.printf` with integer arguments, like
  `:102-103`.
- Boot gains the HS switch commands: one or two CMD6 with a 64-byte reply, CMD7, CMD9, CMD7, then
  an SCR re-read, all but the last at 400 kHz (`sdmmc_sd.c:251`, `:308`, `:511-528`). Not
  measured.

## 6. Error handling

| Event | Behaviour |
|---|---|
| Card without HS | ESP-IDF selects 20 MHz and init succeeds (A1). The line prints `SDMMC clock 20000 kHz (requested 40000 kHz, attempt 1/4)`. |
| HS-capable card, link fails SCR at 40 MHz | Init fails after CSD, and the sector-0 read at 40 MHz probably fails. Attempt 2 resets to 400 kHz (A2) and tries HS again. Attempts 3–4 run at 20 MHz (A3), and a success prints `requested 20000 kHz, attempt 3/4`. |
| A CMD6 exchange errors | Init fails after CSD, and the read runs at 400 kHz. If it succeeds, the volume mounts at 400 kHz and prints `SDMMC clock 400 kHz` (A5). |
| `sdmmc_host_set_card_clk` fails | That attempt is spent and `mountErr` holds the error. If every attempt fails, today's `SDMMC mount failed after retries: <err>` prints (`:101-103`), `begin` returns false, and `src/main.cpp:406-410` shows the SD error screen. |
| `sdmmc_host_get_real_freq` fails | The line prints `card->real_freq_khz` instead (A6). The mount is unaffected. |
| No card | Unchanged: four attempts and the SD error screen. Each attempt adds one probing-clock reset. |

None of these is a new `return false`. Every outcome ends in either today's success path or
today's failure path.

## 7. Testing strategy

### 7.1 Compile-time checks, written first

**Assumption A11 — the schedule's invariants are `static_assert`s next to the table, and they are
this change's red-then-green test.** The model is `lib/Serialization/SdPaths.h:36-43`: a
`constexpr` definition with `static_assert`s checking it, compiled into every build. The SDK uses
file-scope `static_assert` in a `.cpp` too (`SDK/libs/hardware/RecoveryBoot/src/RecoveryBoot.cpp:55`).

The asserts:

1. there are 4 entries (today's budget, A3);
2. the first is `SDMMC_FREQ_HIGHSPEED` (A1);
3. the last is `SDMMC_FREQ_DEFAULT`, so a marginal card always gets a 20 MHz attempt (A3);
4. no entry exceeds `SDMMC_FREQ_HIGHSPEED`, so the schedule can't ask for UHS rates the S3 lacks
   (§3).

**Red:** add the asserts referring to `kAttemptClockKhz` before the table exists. `pio run -e x4pro`
fails to compile `SdmmcBlockDevice.cpp`. **Green:** add the table and the loop changes, and it
compiles.

**Assumption A12 — no host test.**

- No host test compiles anything under `freeink-sdk/` (R§Tooling: the only mention in `test/` is
  a comment, `test/pagination/CMakeLists.txt:10`). There is no host stub for the ESP-IDF SDMMC
  driver.
- #185's host tests covered logic that lived in bereanOS (`lib/Input`), not in the SDK (R§Nearest
  examples).
- The behaviour here is ordering of ESP-IDF calls (A2, A6). A test of it would need a fake SDMMC
  driver and a new path from `test/CMakeLists.txt` into the SDK. That is a new pattern, which this
  spec does not invent. The pure part, the schedule, is covered by A11.

### 7.2 Build and format

- `pio run -e x4pro` and `pio run -e x4pro-gh_release`, run sequentially: concurrent runs race on
  `idf_component.yml`.
- `./bin/clang-format-fix` over the whole tree. It lists files with `git ls-files`
  (`bin/clang-format-fix:49`), which reports the submodule as one entry, `freeink-sdk`. So it
  never reaches SDK sources, and the SDK edit has to match the surrounding style by hand.
- Run the host suite once to confirm nothing in bereanOS moved. No host code changes, so a failure
  there would be pre-existing.

### 7.3 Device checks (owner)

Flash an `x4pro` dev build of the PR branch, by cable or by the side-load in `CLAUDE.md`
"Flashing". Capture serial with `cat /dev/cu.usbmodem* > serial.log`. Then:

1. **Boot.** The log shows one line
   `[SD] SDMMC clock 40000 kHz (requested 40000 kHz, attempt 1/4)`. Any other value is a finding
   to report, not a pass:
   - `20000 … requested 40000` means the card refused HS;
   - `requested 20000` means the loop fell back;
   - `400` means a partial init.
2. **Open the Bible.** Compare `[BMC] Loaded cache data` (against `Loading ePub`) with 276 ms, and
   `[READER] Book loaded in` with 505 ms (`BookMetadataCache.cpp:497`, `Epub.cpp:362`,
   `ReaderActivity.cpp:60`).
3. **Go to a chapter not yet built.** Compare the section build span from the `[SCT]` timestamps
   with 1.2–1.8 s.
4. **Open Bible search, first after boot.** Compare the open, ending at
   `[BSEARCH] 1189 verse documents` (`BibleSearchStore.cpp:198`), with 2.2 s. A later reopen
   compares with ~500 ms.
5. **Sleep and wake.** Compare `[SLP] Study pick … ms` with 445 ms and `[HOMEV] Verse scan … ms`
   with 405 ms (`StudySleepScreen.cpp:254`, `HomeVerse.cpp:98`).
6. **A day of use.** No `SDMMC mount failed`, and no `sdmmc_read_sectors` or SdFat read errors in
   the log.
7. **An older card**, if one is at hand: record its A6 line and whether it boots.

### 7.4 What only the device can show

- Whether the owner's card switches to HS (§7.3 step 1).
- The real speed-up. It is under 2×, because per-command overhead and the bounce-buffer `memcpy`
  don't scale with the clock (R§Mechanism).
- Whether the fallback path ever runs.

The fallback path (A3) and the probing reset (A2) are verified by reading the code, the compile
checks and the log format. A card that exercises them on demand is not available. This is stated
in the PR, not claimed as tested.

## 8. Decisions

- **D1 — approval to fast-forward the fork's `berean` with the SDK commit (A9).** It is raised
  with `hpipe decide` at implement time, before the push, with the recommendation to push as #185
  did.

## Review log

- Pass 0, first draft.
