# berean-o-5 — run the SD card at 40 MHz: design

Branch `perf/r2-sd-40mhz`, bead `berean-o-5`, run task `t5`, surface `hal`, review tier `standard`.

- **Research:** `docs/superpowers/research/2026-10-11-berean-o-5-research.md`, cited below as R§.
- **Model:** `docs/superpowers/specs/2026-09-29-issue-185-design.md`, the last SDK change that
  landed through the fork and a gitlink bump (PR #190, commit `2822662d`). This spec follows its
  shape: numbered labelled assumptions, the fork mechanics in their own section, device checks
  written for the owner, and a review log.
- **Pass 1:** this pass answers `docs/superpowers/reviews/berean-o-5-spec-review-0.md`. What
  changed is in the review log at the end.

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

### 1.1 What changing the constant alone would break

The mount loop accepts more than a clean init (`SdmmcBlockDevice.cpp:84-100`):

- it retries an attempt only when init failed before the CSD was read (`:92-95`);
- if init failed after the CSD, it still reads sector 0 and mounts if that read works
  (`:96-99`).

With a 40 MHz request, three things break.

1. **A marginal link gets no fallback.** An HS-capable card on a marginal link fails init at the
   post-switch SCR check (`IDF/sdmmc/sdmmc_sd.c:587-603`, called at `IDF/sdmmc/sdmmc_init.c:169`).
   ESP-IDF does not fall back to 20 MHz.
2. **Retries identify at the raised clock.** Nothing lowers the clock between attempts, neither
   `sdmmc_card_init` (`sdmmc_init.c:46-176`) nor the loop. Only slot init sets the probing clock
   (`sdmmc_host.c:822-823`). So attempts 2–4 would identify the card at 40 MHz.
3. **The partial-init rule turns new errors into slow or unverified mounts.** The high-speed
   switch runs after the CSD (`sdmmc_init.c:113` runs before `:143`), so its failures reach the
   sector-0 read:
   - **A CMD6 error at 400 kHz** mounts the card at 400 kHz. Examples are a send error, a busy
     function (`ESP_ERR_INVALID_STATE`) or a bad response version (`ESP_ERR_INVALID_RESPONSE`),
     at `sdmmc_sd.c:209-228`, propagated by `sdmmc_common.c:191-192`. The read runs at the
     probing clock the host still holds. Today that card gets no CMD6 at all and runs at 20 MHz.
   - **A failed SCR check at 40 MHz** leaves the host at 40 MHz, because the frequency step
     (`sdmmc_init.c:163`) runs before the check (`:169`). One sector-0 read that happens to pass
     then mounts the card on a link ESP-IDF has just rejected.

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
2. **No card that mounts today ends up slower, or on a link ESP-IDF rejected:**
   - a card without high speed falls back to 20 MHz inside ESP-IDF (A1);
   - a 40 MHz attempt mounts only after a clean init, with ESP-IDF's SCR check passed (A5);
   - otherwise the loop moves on, and the last two attempts run today's 20 MHz path unchanged
     (A3, A5).
3. Print the clock the card really runs at, once, after the boot mount, on the line that
   `if (Serial)` lines in the SDK already use. When the loop needed a retry, the line also says
   why.
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
- **Changing what a 20 MHz attempt accepts.** The partial-init rule (`:92-99`) stays exactly as
  it is for those attempts (A5).
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
  (`sdmmc_host.c:255-257`). Then the SCR check re-reads SCR at 40 MHz (`sdmmc_init.c:169`,
  `sdmmc_sd.c:587-603`).
- **The card lacks CMD6 or SDR25.** ESP-IDF returns `ESP_ERR_NOT_SUPPORTED` (`sdmmc_sd.c:238-241`,
  `:304-306`). `sdmmc_init_card_hs_mode` turns that into 20 MHz and success
  (`IDF/sdmmc/sdmmc_common.c:188-190`). Init is clean, so A5 accepts it on any attempt.

UHS-I/DDR can't engage: the slot never sets `SDMMC_SLOT_FLAG_UHS1` (`SdmmcBlockDevice.cpp:37`;
R§What ESP-IDF does). The host flags stay `SDMMC_HOST_DEFAULT()`'s 0x37 (`SdmmcBlockDevice.cpp:24`).

### 4.2 Each attempt starts where slot init left the host

**Assumption A2 — before the GPIO5 pulse, every attempt restores the two host settings slot init
sets: the 400 kHz probing clock and a 1-bit bus.** The calls are
`sdmmc_host_set_card_clk(host.slot, SDMMC_FREQ_PROBING)` and
`sdmmc_host_set_bus_width(host.slot, 1)`, in slot init's order (`sdmmc_host.c:822-831`). Both are
declared in `LIBS/include/esp_driver_sdmmc/include/driver/sdmmc_host.h:141` and `:116`, which
`SdmmcBlockDevice.cpp:12` already includes. 400 kHz gives a host divider of 10 and a card divider
of 20 (`sdmmc_host.c:261-263`). The clock call also re-arms the data timeout for the new clock
(`:342`).

- **Why both, before the pulse.** Attempt 1 today powers up and identifies the card under exactly
  what slot init left: 400 kHz and 1-bit (`SdmmcBlockDevice.cpp:40`, `sdmmc_host.c:822-831`).
  Restoring both first makes every later attempt start from that same state, on every board that
  shares this class.
  - On the X4 Pro the width call never changes anything: the slot is 1-bit
    (`BoardConfig.h:1450`) and `sdmmc_init_host_bus_width` only widens the bus past 1 bit
    (`sdmmc_common.c:212-231`).
  - On a 4-bit `FREEINK_SD_SDMMC` board, an attempt that fails after `sdmmc_init_host_bus_width`
    (`sdmmc_init.c:150`) would otherwise leave the next attempt's identification on a 4-bit host.
- **On attempt 1 they change nothing.** The host is already at 400 kHz and 1-bit.
- **It already matters today.** An attempt that fails after `sdmmc_init_host_frequency`
  (`sdmmc_init.c:163`) leaves the bus at 20 MHz for the next identification. This fixes that at
  the same time.
- **On failure** the attempt is spent: the error is recorded (A6's last failure, `mountErr`), and
  the loop continues to the next attempt. If every attempt fails, this ends on today's failure
  path (§6).

### 4.3 Falling back to 20 MHz inside the retry loop

**Assumption A3 — a fixed four-entry schedule: attempts 1–2 at `SDMMC_FREQ_HIGHSPEED`, attempts
3–4 at `SDMMC_FREQ_DEFAULT`.** It is a file-scope `constexpr int` array in `SdmmcBlockDevice.cpp`,
with the loop bound computed from its length:

    constexpr int kAttemptClockKhz[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_HIGHSPEED,
                                        SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT};
    constexpr int kMountAttempts = sizeof(kAttemptClockKhz) / sizeof(kAttemptClockKhz[0]);

- **The element type.** It is `int` because `sdmmc_host_t::max_freq_khz` is an `int`
  (`sd_protocol_types.h:214`).
- **The size idiom.** `sizeof / sizeof[0]` is the SDK's own (`SDK/libs/hardware/AudioManager/src/AudioManager.cpp:84`,
  `SDK/libs/hardware/BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp:176`). It needs no `<iterator>`.
- **Why the .cpp.** `SDMMC_FREQ_*` come from the ESP-IDF headers, which only the .cpp includes
  (`SdmmcBlockDevice.cpp:11-13`). The header includes only `BoardConfig.h` and `SdFat.h`
  (`SdmmcBlockDevice.h:16`, `:22`).
- **The naming.** It follows the SDK's `k`-prefixed constants (`SdmmcBlockDevice.h:42-43`).
- **The total stays four.** Today's budget is `attempt < 4` (`SdmmcBlockDevice.cpp:84`). That
  keeps the time to the SD error screen with no card in the slot as it is.
- **Why two at high speed.** The power-cycled retry exists for a mount-sequencing problem
  (`SdmmcBlockDevice.cpp:22-23`). That conclusion was reached at 20 MHz, the only clock the code
  has run (§1). Giving HS a second attempt keeps one sequencing retry before 20 MHz.
- **Why a fixed schedule.** At 40 MHz a marginal link most likely fails the SCR re-read inside
  `send_scr`, with a CRC or timeout error (`sdmmc_sd.c:594-597`). That is more likely than the
  compare after it (`:599-601`). The old sequencing failures were sector reads with `0x107`,
  which is `ESP_ERR_TIMEOUT` (`SdmmcBlockDevice.cpp:57-60`, `:79-81`; `esp_err.h:31`). Since a
  timeout can mean either, no error code tells "sequencing" from "clock margin", and the schedule
  doesn't try to.

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

### 4.4 A 40 MHz attempt accepts only a clean init

**Assumption A5 — whether an attempt may mount on a partial init depends on the clock it
requested.**

- **A 40 MHz attempt** mounts only if `sdmmc_card_init` returns `ESP_OK` and the sector-0 read
  succeeds. Any init error records the error and goes straight to the next attempt, with no
  sector-0 read.
- **A 20 MHz attempt** keeps today's rule exactly (`SdmmcBlockDevice.cpp:92-99`): retry if init
  failed before the CSD, otherwise read sector 0 and mount on success.

The rule is a file-scope `constexpr` predicate beside the schedule:

    constexpr bool acceptsPartialInit(int requestedKhz) { return requestedKhz <= SDMMC_FREQ_DEFAULT; }

Why:

- **It closes both new paths in §1.1, item 3.**
  - A CMD6 error can now only happen on attempts 1–2, since a 20 MHz request sends no CMD6
    (`sdmmc_sd.c:490-493`). It no longer reaches a 400 kHz read; it falls through to the 20 MHz
    attempts.
  - A failed SCR check at 40 MHz no longer reaches a read that might pass by luck.
- **A mount at 40 MHz now implies a clean init.** The SCR check passed (`sdmmc_init.c:169`, the
  last step before `return ESP_OK` at `:175`) and so did a real block read. That is ESP-IDF's own
  test of a raised clock, plus ours.
- **The 20 MHz attempts change nothing.** They reproduce today's behaviour, including its
  tolerance, which is the reason the tolerance was kept. A card that mounts today through a
  partial init still does, on attempt 3 or 4.
- **The cost.** A card whose HS attempts both fail reaches 20 MHz two attempts later than today
  reached it on attempt 1. That is about 2 × 200 ms of GPIO5 delays (`:87-89`), paid only on such
  a card.

A 400 kHz mount can still come from today's path: a 20 MHz attempt whose init fails after the CSD
but before the frequency step, for example at SSR (`sdmmc_init.c:160`). A6 prints it. It is not
new, and it is not changed here (§3).

### 4.5 The boot log line

**Assumption A6 — one line, printed by `SdmmcBlockDevice::begin` on success, from the host's real
dividers.** It goes just before `_card = card;` (`SdmmcBlockDevice.cpp:110`). Format:

    [<millis>] [SD] SDMMC clock <real> kHz (requested <requested> kHz, attempt <n>/<total>)
    [<millis>] [SD] SDMMC clock <real> kHz (requested <requested> kHz, attempt <n>/<total>, after <last error>)

The fields:

- **`<requested>`** is `host.max_freq_khz`. After the `break` it still holds the successful
  attempt's request.
- **`<n>`** is the successful attempt's 1-based index, kept in a variable outside the `for`. The
  loop variable today is scoped to the loop (`:84`) and doesn't exist at `:110`.
- **`<total>`** is `kMountAttempts`.
- **The `after` suffix** appears only when `<n>` is greater than 1. `<last error>` is
  `esp_err_to_name` of the most recent failed attempt's error. That covers any of the init,
  sector-0 read, or A2 reset errors. `esp_err_to_name` is already used at `:103`, and the build
  keeps its name table (`CONFIG_ESP_ERR_TO_NAME_LOOKUP 1`, `LIBS/qio_opi/include/sdkconfig.h`).
  The last error is a separate `esp_err_t`, because `mountErr` is `ESP_OK` once the read succeeds
  (`:98`).

How the line is built and what it tells apart:

- **The style.** It mirrors the failure line at `SdmmcBlockDevice.cpp:102-103` and the SDK's
  `if (Serial) Serial.printf("[%lu] [TAG] …", millis(), …)` idiom (`SDCardManager.cpp:61`,
  `XteinkDetect.cpp:351-353`). The SDK has no `LOG_*` macros (R§What to log).
- **The value.** `<real>` comes from `sdmmc_host_get_real_freq(host.slot, &khz)` (`sdmmc_host.h:256`,
  `sdmmc_host.c:354-367`), seeded with `card->real_freq_khz`. That call writes its output only
  after its checks pass (`:356-364`), so if it fails the seed is printed instead. On a partial-init
  mount, `card->real_freq_khz` alone would print `0`, because `sdmmc_card_init` zeroes the card
  (`sdmmc_init.c:49`) and only `sdmmc_init_host_frequency` sets it (`sdmmc_common.c:268`). The real
  dividers show the true clock.
- **What the fields tell apart.** With A5 in place, every outcome prints differently:

  | Line | Meaning |
  |---|---|
  | `40000 … requested 40000 … attempt 1/4` | HS, first try |
  | `40000 … requested 40000 … attempt 2/4, after …` | HS after one retry, with the reason |
  | `20000 … requested 40000` | the card refused HS |
  | `20000 … requested 20000 … attempt 3/4` or `4/4`, `after …` | the loop fell back, with the reason |
  | `400 … requested 20000` | today's partial-init path |

  `400 … requested 40000` can't occur, because A5 refuses partial inits at 40 MHz.
- **Once.** `begin` runs once per boot: the only caller is `src/main.cpp:406` via
  `HalStorage.cpp:26` (R§Who owns).
- **Visibility.** `if (Serial)` is `HWCDC::isCDC_Connected()` (`HWCDC.cpp:271-273`). Its first
  call after plug-in arms the IN_EMPTY interrupt and returns false (`HWCDC.cpp:178-188`). `connected`
  becomes true only once the host has drained data (`:86-95`, `:134`).
  - The X4 Pro logs through `if (logSerial)` (`lib/Logging/Logging.cpp:71`, the default transport
    at `BoardConfig.h:319-320`, `logSerial` = `Serial` at `lib/Logging/Logging.h:35`).
  - So `LOG_INF("MAIN", "Device: %s", …)` at `src/main.cpp:399`, just before `mountStorage`
    (`:635`), has already armed it.
  - The clock line prints after at least one GPIO5 pulse (200 ms, `SdmmcBlockDevice.cpp:87-89`).
  - So the line is expected in a capture taken from boot. Like every SDK line, it is not gated by
    `LOG_LEVEL`. In `x4pro-gh_release` it prints only if a CDC host is connected, as `SDMMC card
    mounted` already does.

**Assumption A7 — the failure line and `SDMMC card mounted` stay as they are**
(`SdmmcBlockDevice.cpp:102-103`, `SDCardManager.cpp:61`). The brief asks for the clock once after
mount. The mount line comes from a different class, which can't read the opaque card
(`SdmmcBlockDevice.h:45-48`) without a new accessor.

### 4.6 Comments and the SDK doc

**Assumption A8 — the three wrong "40 MHz" claims are corrected in the same SDK commit, and the
retry-loop comment's 20 MHz conclusion is scoped.**

- **`SdmmcBlockDevice.cpp:20-25`.** The OEM claim stays an OEM claim, and the code comment says
  what the code now does:
  - the host asks for `SDMMC_FREQ_HIGHSPEED` (40 MHz), and ESP-IDF negotiates 20 MHz for a card
    without high speed;
  - the retry loop's last two attempts run at 20 MHz;
  - a 40 MHz attempt mounts only on a clean init.
  The trailing `// 40 MHz` on the assignment goes. The constant's name says it.
- **The same comment's "a mount-sequencing problem, not a clock-margin one" (`:22-23`)** becomes
  scoped to 20 MHz, where it was established. At 40 MHz the design treats clock margin as live.
- **The retry-loop comment (`:78-82`)** gains one sentence: each attempt restores the probing
  clock and 1-bit bus, and only 20 MHz attempts fall through to the sector-0 read on a partial
  init.
- **`BoardConfig.h:1446`.** "40 MHz" leaves the CONFIRMED list, because it was never confirmed:
  the code ran 20 MHz. A separate clause says the mount requests 40 MHz high speed, and the boot
  log prints the clock the card actually runs at.
- **`SDK/docs/xteink-x4pro-support.md:219`.** The same correction, plus one sentence in the
  mount-sequence list (`:232-243`) on the per-attempt reset, the 20 MHz fallback, and the
  clean-init rule at 40 MHz.

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
4. **`mountedAttempt` is unset, and `lastFailure` is `ESP_OK`.** For each attempt `i` in
   `0 .. kMountAttempts-1`:
   1. **Restore the probing clock and the 1-bit bus (A2). On error, record it in `mountErr` and
      `lastFailure`, and go to the next attempt.**
   2. GPIO5 HIGH 80 ms, then LOW 120 ms (`:85-90`). Unchanged.
   3. **`host.max_freq_khz = kAttemptClockKhz[i]`.**
   4. `sdmmc_card_init(&host, card)`. On HS-capable cards with `i < 2`, it switches the card to
      SDR25 and the host to 40 MHz. Otherwise it runs at 20 MHz.
   5. **If init failed and `!acceptsPartialInit(host.max_freq_khz)`, record the error and go to
      the next attempt (A5).** Otherwise, unchanged: if init failed and `capacity == 0`, record
      it and retry. Otherwise read sector 0. On success set `mountedAttempt = i` and stop; on
      failure **record the error in `lastFailure` too** (`:92-99`).
5. On failure: the unchanged failure line and cleanup (`:101-109`).
6. On success: **query the real clock and print the A6 line**, then `_card = card` (`:110`).

`SDCardManager::begin` then mounts the volume and prints `SDMMC card mounted`
(`SDCardManager.cpp:54-61`). That is unchanged.

**Resources.**

- No new allocation in the SDK. `kAttemptClockKhz` is 16 bytes of `constexpr` data. Two
  `esp_err_t`/`int` locals hold the attempt index and the last failure.
- The HS switch makes ESP-IDF allocate one transient 64-byte `MALLOC_CAP_DMA` buffer, freed in the
  same call (`sdmmc_sd.c:245`, `:316`; `sd_protocol_types.h:108`).
- The printed line uses no buffer of ours. It is a `Serial.printf` with integer and
  `esp_err_to_name` string arguments, like `:102-103`.
- Boot gains two CMD6 exchanges with a 64-byte reply each (`sdmmc_sd.c:251`, `:308`), and a
  deselect, CSD read and reselect (`:511-528`), all at 400 kHz. The SCR re-read is not new: it
  runs for every SD memory card today (`sdmmc_init.c:169`), and now runs at 40 MHz instead of
  20. Not measured.

## 6. Error handling

| Event | Behaviour |
|---|---|
| Card without HS | ESP-IDF selects 20 MHz and init is clean (A1). Prints `SDMMC clock 20000 kHz (requested 40000 kHz, attempt 1/4)`. |
| HS-capable card, link fails the SCR re-read at 40 MHz | Init fails on attempt 1. A5 skips the read and goes to attempt 2, which restores 400 kHz and 1-bit (A2) and tries HS again. Attempts 3–4 run today's 20 MHz path. A success prints `requested 20000 kHz, attempt 3/4, after <error>`. |
| A CMD6 exchange errors on attempt 1 or 2 | The same as the row above: init fails, A5 skips the read, and the loop moves on to attempts 3–4 at 20 MHz, which send no CMD6. |
| HS init clean, sector-0 read fails at 40 MHz | Unchanged rule: the attempt fails and the loop retries. |
| A 20 MHz attempt fails init after the CSD | Unchanged: the sector-0 read runs. If it succeeds, the card mounts at whatever clock init reached, and A6 prints it, `400 … requested 20000` included. |
| A2's reset calls fail | That attempt is spent and its error recorded. If every attempt fails, today's `SDMMC mount failed after retries: <err>` prints (`:101-103`), `begin` returns false, and `src/main.cpp:406-410` shows the SD error screen. |
| `sdmmc_host_get_real_freq` fails | The line prints `card->real_freq_khz` instead (A6). The mount is unaffected. |
| No card | Unchanged: four attempts and the SD error screen. Each attempt adds the two A2 calls. |

There is no new `return false`. Every outcome ends on today's success path at a clock ESP-IDF
validated, on today's 20 MHz partial-init path, or on today's failure path.

## 7. Testing strategy

### 7.1 Compile-time checks, red then green

**Assumption A11 — the schedule's and the predicate's invariants are `static_assert`s next to
them, and they are this change's red-then-green test.**

- **The model.** `lib/Serialization/SdPaths.h:36-43`: a `constexpr` definition with
  `static_assert`s checking it, compiled into every build. The SDK uses file-scope
  `static_assert` in a `.cpp` too (`SDK/libs/hardware/RecoveryBoot/src/RecoveryBoot.cpp:55`).
- **The asserts:**
  1. there are 4 entries, today's budget (A3);
  2. the first entry is `SDMMC_FREQ_HIGHSPEED` (A1);
  3. the last entry is `SDMMC_FREQ_DEFAULT`, so a marginal card always gets a 20 MHz attempt
     (A3);
  4. no entry exceeds `SDMMC_FREQ_HIGHSPEED`, so the schedule can't ask for UHS rates the S3
     lacks (§3);
  5. `!acceptsPartialInit(SDMMC_FREQ_HIGHSPEED)` (A5);
  6. `acceptsPartialInit(SDMMC_FREQ_DEFAULT)` (A5).

The order makes each red a real failure of the assert, not a missing name:

1. **Refactor, green.** Add `kAttemptClockKhz` as four `SDMMC_FREQ_DEFAULT` entries,
   `kMountAttempts`, and `acceptsPartialInit` returning `true`. Today's behaviour: every attempt
   at 20 MHz, every attempt tolerant. Drive the loop from them. `pio run -e x4pro` compiles.
2. **Asserts, red.** Add asserts 1–6. Assert 2 fails (the first entry is `SDMMC_FREQ_DEFAULT`) and
   assert 5 fails (`acceptsPartialInit` is always true). The build stops on those two.
3. **Behaviour, green.** Set the schedule to A3's values and the predicate to
   `requestedKhz <= SDMMC_FREQ_DEFAULT`. The build compiles. Then add A2's reset and A6's line.

**Assumption A12 — no host test.**

- No host test compiles anything under `freeink-sdk/` (R§Tooling: the only mention in `test/` is
  a comment, `test/pagination/CMakeLists.txt:10`). There is no host stub for the ESP-IDF SDMMC
  driver.
- #185's host tests covered logic that lived in bereanOS (`lib/Input`), not in the SDK (R§Nearest
  examples).
- The rest of the behaviour is ordering of ESP-IDF calls (A2, A5's branch, A6). A test of it would
  need a fake SDMMC driver and a new path from `test/CMakeLists.txt` into the SDK. That is a new
  pattern, which this spec does not invent. The pure parts, the schedule and the predicate, are
  covered by A11.

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
"Flashing". Capture serial from boot with `cat /dev/cu.usbmodem* > serial.log`. Then:

1. **Boot.** Read the one `[SD] SDMMC clock` line and grade it with A6's table:
   - **Pass:** `SDMMC clock 40000 kHz (requested 40000 kHz, attempt 1/4)`.
   - **Pass, to note:** `SDMMC clock 40000 kHz (requested 40000 kHz, attempt 2/4, after <error>)`.
     That is a sequencing retry at 40 MHz. Record the error.
   - **Finding to report:** any `20000` or `400` line, with its `after` reason if it has one.
   - **Capture gap, not a result:** neither the clock line nor `[SD] SDMMC card mounted` is in the
     log. Re-capture from a cold boot.
2. **Open the Bible.** Compare `[BMC] Loaded cache data` (against `Loading ePub`) with 276 ms, and
   `[READER] Book loaded in` with 505 ms (`BookMetadataCache.cpp:497`, `Epub.cpp:362`,
   `ReaderActivity.cpp:60`).
3. **Go to a chapter not yet built.** Compare the section build span from the `[SCT]` timestamps
   with 1.2–1.8 s.
4. **Open Bible search, first after boot.** Compare the open, ending at
   `[BSEARCH] 1189 verse documents` (`BibleSearchStore.cpp:198`), with 2.2 s. A later reopen
   compares with ~500 ms.
5. **Sleep and wake, a no-regression check.** `[SLP] Study pick … ms` against 445 ms and
   `[HOMEV] Verse scan … ms` against 405 ms (`StudySleepScreen.cpp:254`, `HomeVerse.cpp:98`)
   should not get worse. §7.4 explains why they barely move.
6. **A day of use.** None of these in the log:
   - `SDMMC mount failed`;
   - SdFat or `sdmmc_read_sectors` read errors;
   - store write failures. The atomic writer's errors are `[PERSIST] Failed to open … for write`,
     `Failed to write …` and `Failed to rename … into place` (`lib/Serialization/PersistableStore.cpp:77`,
     `:85`, `:98`, `:118`). `writeSectors` itself fails silently (`SdmmcBlockDevice.cpp:144-156`),
     so writes at 40 MHz only show up there.
7. **An older card**, if one is at hand: record its clock line, including any `after` reason, and
   whether it boots.

### 7.4 What only the device can show

- Whether the owner's card switches to HS (§7.3 step 1).
- The real speed-up. It is under 2×, because per-command overhead and the bounce-buffer `memcpy`
  don't scale with the clock (R§Mechanism).
- **The sleep and wake figures barely move.** Both read 43,864 bytes (baseline note `:75-76`).
  That is about 17.5 ms of raw transfer at 1-bit 20 MHz and about 8.8 ms at 40 MHz, so they
  should change by about 10 ms at most. A flat result there is expected, not a failure. They stay
  as no-regression checks because the brief lists verse scans among the costs.
- Whether the fallback path ever runs.

The fallback path (A3), the clean-init rule (A5) and the reset (A2) are verified by reading the
code, the compile checks and the log format. A card that exercises them on demand is not
available. This is stated in the PR, not claimed as tested.

## 8. Decisions

- **D1 — approval to fast-forward the fork's `berean` with the SDK commit (A9).** It is raised
  with `hpipe decide` at implement time, before the push, with the recommendation to push as #185
  did.

## Review log

- **Pass 0:** the first draft.
- **Pass 1** answers `docs/superpowers/reviews/berean-o-5-spec-review-0.md` (BLOCKER, 1 blocker,
  0 majors, 7 minors). Every finding was accepted.
  - **BLOCKER 1, the partial-init rule on HS attempts: fixed.**
    - A5 is reversed for 40 MHz attempts: they accept only a clean init, through the new
      `acceptsPartialInit` predicate. The 20 MHz attempts keep today's rule unchanged.
    - The pass-0 reasoning, "a slow boot for no boot", was wrong: attempts 3–4 mount such a card
      at 20 MHz exactly as today.
    - §1.1 now lists both new paths.
    - Goal 2 states the guarantee.
    - §5 step 4.5, both §6 rows, A6's table and §8's closing claim all follow the rule.
    - A11 asserts the predicate.
  - **MINOR 1, a vacuous red: fixed.** §7.1 now refactors to today's values first, then adds
    asserts that fail on values, then changes the values.
  - **MINOR 2, the loop bound and attempt index: fixed.** `kMountAttempts` is defined with the
    SDK's `sizeof` idiom (A3). The successful attempt's index lives outside the loop (A6, §5).
  - **MINOR 3, the sleep and wake checks: fixed.** §7.3 step 5 is a no-regression check, and §7.4
    gives the ~10 ms ceiling.
  - **MINOR 4, the visibility evidence: fixed.** A6 now rests on the pre-mount `LOG_INF` at
    `src/main.cpp:399` through `Logging.cpp:71`, and on HWCDC's arming logic. §7.3 step 1 names a
    capture gap.
  - **MINOR 5, the device-check grading: fixed, including the optional part.**
    - `attempt 2/4` at 40 MHz is a pass to note.
    - Store write failures are added to step 6.
    - The clock line gains an `after <last error>` suffix whenever the mount took more than one
      attempt (A6).
  - **MINOR 6, the 20 MHz conclusion: fixed.** A8 scopes "not a clock-margin one" to 20 MHz, and
    A3 says so where it relies on it.
  - **MINOR 7, the three slips: fixed.**
    - §5 no longer counts the SCR re-read as new.
    - A3 names `send_scr`'s CRC or timeout as the likely margin failure.
    - A2 now restores the 1-bit bus as well as the clock, mirroring slot init. That makes "every
      attempt starts where slot init left the host" true on every board sharing the class,
      rather than scoping the claim to the X4 Pro.
