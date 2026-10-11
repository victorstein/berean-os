Tier: standard

# berean-o-5 — PR #257 review, pass 1

PR #257, `perf: run the SD card at 40 MHz high speed`, head `4ac4e6d2`, base `main` at `b2f19ef7`
(the merge-base equals the `origin/main` tip, so the branch is current).

What was read:
- the brief (`hpipe brief --task t5`);
- the spec `docs/superpowers/specs/2026-10-11-berean-o-5-design.md`;
- the plan `docs/superpowers/plans/2026-10-11-berean-o-5-plan.md`;
- the PR body;
- the SDK change itself, `git -C freeink-sdk diff 67f7e012..7dde1349` (3 files, +82/−10);
- the final `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp` in full;
- the ESP-IDF 5.5.2 sources the design rests on (`~/.platformio/packages/framework-espidf/components/`,
  `IDF/` below) and the linked headers (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/include/`,
  `LIBS/` below).

State checked:
- **The fork:** `git ls-remote` puts the fork's `berean` at `7dde1349801a18a8e01ff0d7b422503b50c95f31`.
  Its parent is `67f7e012` (a fast-forward), and that is the SHA the gitlink records.
- **CI:** all eight checks are `SUCCESS` on `4ac4e6d2`, among them `Build x4pro`, `unit-tests`,
  `clang-format` and `cppcheck`.
- **The PR's files:** only this bead's docs and the `freeink-sdk` gitlink.

## Intent

### The brief's work items

- **Set `SDMMC_FREQ_HIGHSPEED` and correct the comment: done.**
  - The one-off `host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz` is gone.
  - The value now comes per attempt from `kAttemptClockKhz` (`SdmmcBlockDevice.cpp:21-22`, `:135`), and
    the first two entries are `SDMMC_FREQ_HIGHSPEED`.
  - The comment at `:50-56` now calls 40 MHz the OEM's claim and states the negotiation.
- **Log the negotiated clock once after mount: done** (`SdmmcBlockDevice.cpp:159-169`).
  - The value comes from the host's real dividers (`sdmmc_host_get_real_freq`, `IDF/esp_driver_sdmmc/src/sdmmc_host.c:354-367`).
    That is the brief's "or the real frequency" option. It is the better choice, since on a
    partial-init mount `card->real_freq_khz` is still the zero that `memset` left
    (`IDF/sdmmc/sdmmc_init.c:49`).
  - It prints once: on success, just before `_card = card` (`:170`). The built objects carry the
    format string (`strings` on both `SdmmcBlockDevice.cpp.o` files and on the release `firmware.bin`).
- **What ESP-IDF does with a card that can't do high speed: answered, and I confirmed it.**
  `sdmmc_enter_higher_speed_mode` returns `ESP_ERR_NOT_SUPPORTED` for a card without CMD6 or SDR25.
  `sdmmc_init_card_hs_mode` turns that into `card->max_freq_khz = SDMMC_FREQ_DEFAULT` and success
  (`IDF/sdmmc/sdmmc_common.c:188-190`). So the card falls back to 20 MHz rather than failing.
- **New failure paths in the retry loop: found and closed.** The spec's §1.1 names three. The code
  closes each one:
  - the 400 kHz mount after a CMD6 error;
  - the mount on a link that just failed `sdmmc_check_scr` (`IDF/sdmmc/sdmmc_sd.c:587-603`);
  - identification at a raised clock on retries.

  The first two are closed by the clean-init gate at `:137`. The third is closed by the
  per-attempt reset at `:123-128`.

### Acceptance criteria

- **The boot log prints the SD clock: met in code.** The other half, "on the owner's card it reads
  40 MHz", is the owner's to see. The PR does not claim it: its device step 1 grades the line, and
  "Not tested on a device" lists what was not exercised.
- **The fork commit and the gitlink bump, with the SHA: met.** The PR body gives the range
  `67f7e01..7dde134`, the full SHA, the commit title, "Nothing goes to Free-Ink", `.gitmodules`
  unchanged, and the brief's "For existing clones" line word for word.
- **The device checks: met.** Each check the brief asks for is in the PR body:
  - boot, step 1;
  - opening the Bible, step 2, against 276 ms and 505 ms;
  - a new chapter build, step 3, against 1.2–1.8 s;
  - search open, step 4, against 2.2 s and ~500 ms;
  - a day of use, step 6;
  - an older card, step 7.

  Every baseline figure matches the baseline note on `docs/252-responsiveness-baseline`. Every
  log marker named exists at the cited line:
  - `EpubReaderActivity.cpp:1286`;
  - `Section.cpp:282` and `:315`;
  - `ChapterHtmlSlimParser.cpp:1647`;
  - `BibleSearchStore.cpp:198`;
  - `BookMetadataCache.cpp:497`;
  - `ReaderActivity.cpp:60`;
  - `HomeVerse.cpp:98`;
  - `EpdBus.cpp:293`;
  - `SDCardManager.cpp:61`.
- **The shared context: met.**
  - The title is `perf: …` and the PR targets `main`.
  - There is a "For the orchestrator" section; nothing is needed for `test/CMakeLists.txt` or
    `src/main.cpp`.
  - The PR has no new heap allocation. Its memory paragraph names ESP-IDF's transient 64-byte DMA
    buffer (`sdmmc_sd.c:245`) and gives the mechanism, not a gain.
  - The last line is `Refs berean-o-5`.
  - The A/B comparison rule is right to apply: `git log 478cf1ed..origin/main -- src lib` prints
    `106dfc4b` (#255).

### Spec requirements

Each assumption maps to code, and none is only half done:

| Spec | Where it is implemented |
|---|---|
| A1, the HS request and ESP-IDF negotiating down | `:21-22`, `:135`. The DDR branch at `sdmmc_sd.c:259` stays unreachable: it needs `is_uhs1`, and `is_uhs1` needs S18R in ACMD41, which is set only for a UHS-I slot (`sdmmc_common.c:47-52`). The S3 has no `SOC_SDMMC_UHS_I_SUPPORTED` (`LIBS/soc/esp32s3/include/soc/soc_caps.h:573-578`). |
| A2, the probing clock and 1-bit bus restored before the GPIO5 pulse | `:123-128`, in slot init's order (`sdmmc_host.c:822-831`). On failure the attempt is spent and its error recorded. |
| A3, the four-entry schedule with the `sizeof` count | `:21-23`. The loop is bounded by `kMountAttempts` (`:122`). |
| A5, a clean init only at 40 MHz, today's rule at 20 MHz | `:28`, `:137`. At 20 MHz the gate reduces exactly to today's `e != ESP_OK && capacity == 0`. |
| A6, the line, its fields and its suffixes | `:161-169`. `lastFailure` is assigned on all three non-`break` exits (`:126`, `:138`, `:148`). `initErr` is the successful attempt's own result. `host.max_freq_khz` is not touched after the `break`. |
| A7, the failure line and `SDMMC card mounted` unchanged | `:152`; `SDCardManager.cpp:61`. |
| A8, the three comments and the SDK doc | `SdmmcBlockDevice.cpp:50-56` and `:115-117`, `BoardConfig.h:1446-1448`, and `xteink-x4pro-support.md:219-222` and `:248-253`. |
| A9 / A10, one fast-forwarded SDK commit and a gitlink-only bereanOS change | Confirmed above. The approval for the push, D1, went through the pipeline (`hpipe show --task t5`: `blocked-on-decision → implement`). |
| A11, six `static_assert`s, red then green | `:37-43`. The claimed red set is right: with four `DEFAULT` entries and an always-true predicate, only asserts 2 and 5 can fail. |

### Scope

- **No reduction.** Every goal in spec §2 is in the code.
- **No expansion.** The spec's non-goals are all respected:
  - no clock field in `BoardConfig::SdmmcPins`;
  - no change to the retry budget, the GPIO5 cycle or the bounce buffer;
  - no change to `lib/hal`;
  - no new bereanOS log line.

### Tests

- **What the asserts cover.** The six asserts pin the schedule's invariants and the predicate's
  value at both clocks.
- **What they don't cover.** They do not exercise the loop's use of them at `:137`.
- **Why that is accepted.** The gap is a recorded decision, not an oversight:
  - spec A12 rejects a fake SDMMC driver as a pattern this repo lacks (no host test reaches
    `freeink-sdk/`);
  - the plan review accepted it;
  - the PR body lists the fallback, the reset and the partial-init marker as untested on a device.

  I don't count it as a finding.

### Divergence from the plan

None that goes unexplained:
- The final file matches Tasks 1–4's blocks.
- The plan's one change of order against the spec is explained in the plan (Task 2): the reset
  and the log line land before the clock values flip.
- The rebase happened at Task 0. The reflog shows `rebase (start): checkout origin/main` at 06:10.
  The builds came after it: the `SdmmcBlockDevice.cpp.o` files are from 06:22–06:23, after the last
  edit to the source (06:22:10). So Task 9's rebase was a no-op, and the PR's "both builds succeed"
  is true of the final tree.

## Quality

- **It mirrors existing patterns.** Every construct already has a precedent in the SDK:
  - the anonymous namespace: `AudioManager.cpp:16`, `RecoveryBoot.cpp:14`;
  - a file-scope `static_assert` in a `.cpp`: `RecoveryBoot.cpp:55`, `BookCatalog.cpp:47`;
  - `k`-prefixed constants: `SdmmcBlockDevice.h:42-43`;
  - the `if (Serial) Serial.printf("[%lu] [SD] …", millis(), …)` idiom: `:152`, `SDCardManager.cpp:61`;
  - the `sizeof / sizeof[0]` count: `AudioManager.cpp:84`.

  The reset uses the same two public calls that slot init uses, rather than a private sequence.
- **Naming is consistent with its siblings:** `kAttemptClockKhz`, `kMountAttempts`,
  `acceptsPartialInit`, `lastFailure`, `initErr`, `mountedAttempt`. All are camelCase or `k`-prefixed,
  like the file.
- **No dead code.** There is no dead or commented-out code. `noAttemptAboveHighSpeed()` exists only
  to feed assert 4, which is its purpose.
- **Comments explain why.** The new comments give reasons: why two HS attempts (`:19-20`), why
  there is no partial init at HS (`:25-27`), why the loop resets each attempt (`:115-117`), and why
  the real dividers are read (`:159-160`). The sentence at `:52-53` that says each attempt sets
  `host.max_freq_khz` restates `:135`. It sits inside a mechanism explanation that earns its place,
  so it isn't worth a change.
- **Error handling has the established shape.** Each failure is recorded in `mountErr` and the loop
  goes on. The all-failed path (`:150-158`) is untouched, and there is no new `return false`.
- **Nothing is duplicated.**
- **The PR body's device checks are carefully specified,** with one imprecision in step 6 (MINOR 1).

## Findings

1. **MINOR: device step 6's pass criterion would fail a healthy run on any card without legacy
   highlights.**
   - **The criterion.** The PR body's step 6 requires "no `[SD]` line other than the clock and mount
     lines" (from spec `docs/superpowers/specs/2026-10-11-berean-o-5-design.md:579`).
   - **Where else `[SD]` prints.** The SDK also prints `[SD] Failed to open directory` and
     `[SD] Path is not a directory`, from `SDCardManager::listFiles`
     (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:165`, `:169`). That function
     opens the path unguarded (`lib/hal/HalStorage.cpp:42-44` passes it straight through).
   - **Why it fires on a healthy boot.** Every boot calls
     `MigrationRunner::pending()` (`src/main.cpp:484`), which calls `legacySources()`
     (`src/study/MigrationRunner.cpp:183`). That lists `/.crosspoint/highlights`
     (`MigrationRunner.cpp:49`, `lib/Serialization/SdPaths.h:19`). On a card without that
     directory, every boot of build A and build B logs the line, whatever the SD clock.
   - **The cost.** The step's remedy is to reflash the previous build, so a benign line would wrongly
     roll back a working change.
   - **The fix is a PR-body edit only, with no SDK change.** Narrow the criterion to
     `[SD] SDMMC …` lines, or name the two directory-listing lines as expected noise.

The SDK change is correct against the ESP-IDF 5.5.2 sources, and it implements every spec
requirement. The one finding is a wording fix to the owner's device check, to be fixed inline.

VERDICT: CLEAR
