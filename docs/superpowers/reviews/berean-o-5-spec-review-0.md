Tier: standard

# berean-o-5 spec review, pass 0

- **Spec:** `docs/superpowers/specs/2026-10-11-berean-o-5-design.md` at `79cdbffc`.
- **Checked against:** the brief (`hpipe brief --task t5`), the research note
  `docs/superpowers/research/2026-10-11-berean-o-5-research.md`, the installed ESP-IDF 5.5.2 source
  (`IDF/`), the headers and libraries that link (`LIBS/`), `freeink-sdk/` at `67f7e012`, the
  ESP-IDF v5.5.2 ESP32-S3 SDMMC host docs, and `git show origin/main:docs/superpowers/notes/responsiveness-baseline.md`.

Path abbreviations follow the spec's.

## Findings

### BLOCKER 1 — A5 lets HS attempts mount on a partial init, which breaks Goal 2 and the §6 guarantee

**Claim.** A5 keeps today's rule that a post-CSD `sdmmc_card_init` failure still gets a sector-0
read and mounts if that read succeeds. Its reasoning is: "Refusing such a mount would trade a slow
boot for no boot, on a card that mounts today." §6 adds: "Every outcome ends in either today's
success path or today's failure path." Goal 2 says "an HS-capable card that fails at 40 MHz gets
20 MHz attempts in our retry loop."

**Problem.** Under A3's schedule, attempts 1–2 request HS, and the partial-init rule applies to
them unchanged. That creates two outcomes that are neither today's success path nor today's
failure path.

1. **CMD6 errors at 400 kHz: the card mounts at 400 kHz, where it mounts at 20 MHz today.**
   - Today `host.max_freq_khz = 20000` takes the early return, so no CMD6 is ever sent
     (`IDF/sdmmc/sdmmc_sd.c:489-493`).
   - Under the spec, attempt 1 sends CMD6. A non-`NOT_SUPPORTED` error is returned from
     `sdmmc_init_card_hs_mode` (`IDF/sdmmc/sdmmc_common.c:191-192`). Possible errors include
     `ESP_ERR_INVALID_STATE` for a busy function and `ESP_ERR_INVALID_RESPONSE` for a bad response
     version (`sdmmc_sd.c:216-228`).
   - The failure comes after CSD (`sdmmc_init.c:113` runs before `:143`), so `capacity != 0`.
   - The sector-0 read then runs at the 400 kHz the host still holds, succeeds, and the loop
     breaks (`SdmmcBlockDevice.cpp:92-99`).
   - The result is the whole session at 400 kHz, 50× slower than that card runs today. It is not
     a "slow boot". The spec accepts this in A5 and §6 row 3.
   - "No boot" is not the alternative. If the HS attempt simply `continue`s, attempts 3–4 request
     20 MHz, send no CMD6, and mount exactly as today.
2. **`sdmmc_check_scr` fails at 40 MHz, the sector-0 read happens to pass, and the card mounts at
   40 MHz on a link ESP-IDF has just rejected.**
   - The SCR check is ESP-IDF's own signal-integrity test after the clock is raised
     (`sdmmc_sd.c:589-592`).
   - It runs after `sdmmc_init_host_frequency` (`sdmmc_init.c:163`, `:169`), so a check failure
     leaves the host at 40 MHz. The sector-0 read then runs at 40 MHz.
   - §6 row 2 assumes that read "probably fails". On an intermittent, data-dependent margin, one
     512-byte read can pass, and then the 20 MHz fallback in Goal 2 never runs.
   - The A6 line would print `SDMMC clock 40000 kHz (requested 40000 kHz, attempt 1/4)`. That is
     the same line as a healthy HS mount, and §7.3 step 1 counts it as a pass. So A6's claim that
     requested and real "tell three things apart" fails in exactly this case.

Both paths are new failure paths in the mount retry loop, which the brief told the spec to find
("Check the mount retry loop in the same file for any new failure path"). The spec found path 1
(R§New failure paths item 3). It then kept the path on a false dichotomy. It did not consider
path 2's success branch at all.

**Evidence.**
- Today's loop: `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp:84-100`.
- Order inside `sdmmc_card_init`: CSD `:113`, HS mode `:143`, host frequency `:163`, SCR check
  `:169` (`IDF/sdmmc/sdmmc_init.c`).
- The early return that keeps today's cards away from CMD6: `IDF/sdmmc/sdmmc_sd.c:489-493`.
- CMD6 error propagation: `IDF/sdmmc/sdmmc_common.c:188-193`.
- A spot check confirmed the linked library matches this source: `strings LIBS/lib/libsdmmc.a`
  contains `select_card (1) returned`, `send_csd returned` and `auto tuning not supported`.

**Fix.** Narrow A5 to the attempts where it reproduces today's behaviour. In effect, this reverses
A5 for HS attempts.

- An attempt whose requested clock is above `SDMMC_FREQ_DEFAULT` accepts only a clean
  `sdmmc_card_init`. On any error it records `mountErr` and `continue`s, so the sector-0 read
  validates only a fully initialised HS card.
- The 20 MHz attempts (3–4) keep today's `capacity == 0` guard and partial-init read unchanged.
  That keeps A5's real justification, no regression against today, where it applies.
- Express the rule as a `constexpr` predicate beside `kAttemptClockKhz`, for example
  `acceptsPartialInit(int requestedKhz)`. Give it `static_assert`s in A11:
  `!acceptsPartialInit(SDMMC_FREQ_HIGHSPEED)` and `acceptsPartialInit(SDMMC_FREQ_DEFAULT)`.
- Rewrite §6 rows 2–3 to match:
  - A CMD6 error on attempts 1–2 falls through to 20 MHz and prints `requested 20000 kHz,
    attempt 3/4`.
  - A 400 kHz mount can then only come from today's 20 MHz partial-init path.
- Correct A6's "tell three things apart" so it holds again.
- Update §5 step 4.5.

### MINOR 1 — A11's "red" is vacuous

**Claim.** "Red: add the asserts referring to `kAttemptClockKhz` before the table exists." The
build fails, then turns green once the table exists.

**Problem.** That red is an undeclared-identifier error. It would fail the same way if every assert
were wrong, so it never shows that an assert can catch a bad schedule.

**Evidence.** §7.1 of the spec, "Red" and "Green".

**Fix.** Red-green it the meaningful way:

1. Introduce the table first with today's behaviour, four `SDMMC_FREQ_DEFAULT` entries. This is a
   pure refactor, and `pio run -e x4pro` is green.
2. Add the asserts. Assert 2, first entry is `SDMMC_FREQ_HIGHSPEED`, fails.
3. Change the entries. The build is green again.

Do the same for the BLOCKER 1 predicate.

### MINOR 2 — the loop bound and the attempt index are underspecified

**Claim.**
- §4.3: "the loop bound derived from its length".
- §5 step 4: "For each attempt `i` in `0 .. kMountAttempts-1`".
- A6 prints `attempt <n>/<total>` after the loop.

**Problem.**
- `kMountAttempts` is never defined.
- Today's loop variable is scoped to the `for` (`SdmmcBlockDevice.cpp:84`), so it doesn't exist at
  `:110`, where A6 prints.

**Fix.**
- Define `constexpr int kMountAttempts = sizeof(kAttemptClockKhz) / sizeof(kAttemptClockKhz[0]);`,
  or name `std::size` and its `<iterator>` include.
- Say that the successful attempt's index is kept outside the loop for the A6 line.
- `host.max_freq_khz` after `break` already holds that attempt's request, so it needs nothing.

### MINOR 3 — the verse-scan and study-pick checks are not SD-bound, and the spec doesn't say so

**Claim.** §1 and §7.3 step 5 list `HOMEV Verse scan` (405 ms) and `SLP Study pick` (445 ms) as
costs this touches, and ask the owner to compare them.

**Problem.** Both lines report 43,864 bytes. At 1-bit 20 MHz that is about 17.5 ms of raw transfer;
at 40 MHz about 8.8 ms. So by the spec's own mechanism (R§Mechanism), these figures move by about
10 ms at most, roughly 2%. A flat result there is expected, but the owner could read it as the change
failing.

**Evidence.**
- Baseline note, `origin/main`, lines 75–76: `Verse scan: 43864 bytes, ..., 405 ms`;
  `Study pick: 1 files, 43864 bytes parsed, ..., 445 ms`.
- Spec §1: 1-bit at 20 MHz is about 2.5 MB/s.

**Fix.** In §7.4, set the expectation that these two are parse-bound and should change by no more
than about 10 ms. Keep them in §7.3 as no-regression checks, not as speed-up checks. The brief
listed them, so they stay.

### MINOR 4 — A6's visibility evidence comes from after the mount

**Claim.** "The idiom's output reaches the owner's capture: the baseline records
`XTDET promoted SSD1677 -> UC8179`."

**Problem.**
- On X4 Pro, XTDET prints after the mount: `setup()` → `beginDisplay` (`src/main.cpp:642`) →
  `setupDisplayAndFonts` (`:475`) → `applyXteinkDisplayController` (`:291`), after `mountStorage`
  (`:635`).
- HWCDC's `operator bool` returns false on its first call until the host has drained something.
  `isCDC_Connected` arms the FIFO and returns false (`HWCDC.cpp:181-188`), and only the IN_EMPTY
  or RX ISR sets `connected` (`HWCDC.cpp:86-95`, `:134`).
- So XTDET doesn't prove the mount-time window. The claim is probably still true, for a different
  reason: `LOG_INF("MAIN", "Device: %s", …)` writes to `Serial` before the mount (`main.cpp:399`).

**Fix.**
- Cite `main.cpp:399` as the pre-mount write.
- Add one sentence to §7.3 step 1: if neither the clock line nor `[SD] SDMMC card mounted` is in
  the capture, that is a capture gap, not a clock result.

### MINOR 5 — the device-check wording would misgrade a pass and leaves writes out

**Claim.** §7.3 step 1: "Any other value is a finding to report, not a pass." Step 6 checks only
for read errors.

**Problem.**
- `SDMMC clock 40000 kHz (requested 40000 kHz, attempt 2/4)` meets the brief's acceptance (the
  clock reads 40 MHz), but the wording grades it as a non-pass. It is a sequencing retry and should
  be noted, not failed.
- Writes run at 40 MHz too. `writeSectors` fails silently in the SDK
  (`SdmmcBlockDevice.cpp:144-156`), and failures surface only as store save errors.
- When the loop falls back, nothing in the capture says why. The research notes that whether
  ESP-IDF's own error lines reach the USB CDC capture "is not established".

**Fix.**
- Say that `attempt 2/4` at 40000 kHz is a pass to note.
- Add store save failures (`LOG_ERR` from the atomic writers) to step 6.
- Optionally, when the successful attempt is not the first, append the previous attempt's
  `esp_err_to_name` to the A6 line. That costs no buffer, and it lets the "older card" check (step
  7) tell sequencing from margin.

### MINOR 6 — A8 leaves a 20 MHz-only conclusion unqualified in the corrected comment

**Claim.** A8 rewrites `SdmmcBlockDevice.cpp:20-25` to say what the code now does. A3's "Why two at
high speed" relies on `:22-23`: "a mount-sequencing problem, not a clock-margin one".

**Problem.** That conclusion was reached with the code running at 20 MHz, the only clock it ever
ran (spec §1). At 40 MHz the design itself treats clock margin as live (A3's fallback). If the
sentence survives the rewrite unqualified, it contradicts the code beside it.

**Fix.** A8 should say the sentence is scoped to "at 20 MHz", or moved to the retry-loop comment
with that qualifier.

### MINOR 7 — three small factual slips

- **§5 "Resources".** "Boot gains … then an SCR re-read." That re-read is not new. `sdmmc_check_scr`
  runs for every SD memory card today, at 20 MHz (`sdmmc_init.c:169`, `is_sdmem`). Only the two
  CMD6 exchanges (`sdmmc_sd.c:251`, `:308`) and CMD7/CMD9/CMD7 (`:511-528`) are added.
- **A3 "Why a fixed schedule".** "A marginal link fails `sdmmc_check_scr` with
  `ESP_ERR_INVALID_RESPONSE`." SCR is a CRC-checked data read, so a marginal link more often fails
  inside `send_scr` with a CRC or timeout error (`sdmmc_sd.c:594-597`) than at the memcmp
  (`:599-601`). The conclusion, that no error code separates sequencing from margin, holds and is
  stronger. Reword it.
- **A2.** "Resetting first makes attempts 2–4 identical to attempt 1." That is true for this
  1-bit board only. Slot init sets both the probing clock and a 1-bit bus
  (`sdmmc_host.c:822-831`). A 4-bit `FREEINK_SD_SDMMC` board whose attempt fails after
  `sdmmc_init_host_bus_width` (`sdmmc_init.c:150`) keeps 4-bit. Either scope the claim to X4 Pro,
  or say why the width reset is left out, given that §3 makes this one policy for every SDMMC
  board.

## Verified and sound

Each of these was re-read or re-run for this review, and each holds:

- **Constants.** `SDMMC_FREQ_DEFAULT 20000`, `HIGHSPEED 40000`, `PROBING 400`
  (`LIBS/include/sdmmc/include/sd_protocol_types.h:215-217`). `max_freq_khz` is an `int` (`:214`).
  The `IDF/` and `LIBS/` include trees are identical (`diff -r` printed nothing for either).
- **A1, both branches.**
  - The early return is at `sdmmc_sd.c:490-493`.
  - `NOT_SUPPORTED` comes from `:238-241` and `:304-306`, and becomes 20 MHz plus OK at
    `sdmmc_common.c:188-190`.
  - `MIN(host, SDR104) = 40000` at `sdmmc_sd.c:531`.
  - Divider 4 is exactly 40 MHz (`sdmmc_host.c:255-257`).
- **UHS and DDR stay off.** `SOC_SDMMC_UHS_I_SUPPORTED` is absent from `soc_caps.h:573-578`. S18A
  is requested only when the slot is UHS1 (`sdmmc_common.c:47-54`, `sdmmc_host.c:646-648`). The DDR
  branch needs `is_uhs1` (`sdmmc_sd.c:259`).
- **A2's call.**
  - `sdmmc_host_set_card_clk` is declared at `sdmmc_host.h:141`.
  - It is the same function slot init uses (`sdmmc_host.c:823`).
  - 400 kHz gives host divider 10 and card divider 20 (`:261-263`).
  - It re-arms the data timeout for the new clock (`:342`).
  - Nothing in `IDF/sdmmc/*.c` lowers the clock (`grep` finds only `sdmmc_common.c:247-248`).
- **A6's reader.** `sdmmc_host_get_real_freq` writes only after `SLOT_CHECK` and the NULL check
  (`sdmmc_host.c:354-367`). The S3 divider getters are consistent with the setter
  (`hal/esp32s3/include/hal/sdmmc_ll.h:161-202`). `card->real_freq_khz` is set only at
  `sdmmc_common.c:268`.
- **Single call site.** `src/main.cpp:406` → `lib/hal/HalStorage.cpp:26`.
  `detachFilesystemForRawAccess` has no caller in `src lib`.
- **Fork state.** `git ls-remote https://github.com/victorstein/freeink-sdk.git berean` →
  `67f7e012e3554106062bc52a3131523339a8ac4e`, which equals `git submodule status`. The clone is
  shallow (`true`). `.gitmodules` points at the fork with `branch = berean`. CI checks out
  submodules at `ci.yml:46,71,94,131`.
- **Baseline figures and log cites.**
  - Every baseline figure matches the note on `origin/main` (`:43,47,48,49,52,53,69,71,75,76`).
  - The log lines exist at the cited places: `BookMetadataCache.cpp:497`, `Epub.cpp:362`,
    `ReaderActivity.cpp:60`, `BibleSearchStore.cpp:198`, `StudySleepScreen.cpp:254`,
    `HomeVerse.cpp:98`.
- **The three 40 MHz claims.** They are at the cited lines: `SdmmcBlockDevice.cpp:22`,
  `BoardConfig.h:1446`, `xteink-x4pro-support.md:219`. A repo-wide grep finds no fourth claim
  about the SD clock.
- **Formatter scope.** `bin/clang-format-fix:49` lists files with `git ls-files`, which never
  reaches SDK sources. The SDK carries no format tooling or CI of its own.
- **ESP-IDF docs.** The v5.5.2 ESP32-S3 SDMMC host docs list High Speed at 40 MHz for 1-line SD and
  route the slots through the GPIO matrix. They state no GPIO-matrix frequency limit.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 0
