Tier: standard

# berean-o-5 spec review, pass 2

- **Spec:** `docs/superpowers/specs/2026-10-11-berean-o-5-design.md` at `f4f1abca` (its "Pass 1",
  answering `berean-o-5-spec-review-0.md`).
- **Checked against:** the brief (`hpipe brief --task t5`); the research note
  `docs/superpowers/research/2026-10-11-berean-o-5-research.md`; the ESP-IDF 5.5.2 source (`IDF/`) and
  the headers and libraries that link (`LIBS/`); `freeink-sdk/` at `67f7e012`; `src/`, `lib/`,
  `platformio.ini` and the workflows in this worktree; the baseline note on `origin/main`; the ESP-IDF
  v5.5.2 ESP32-S3 SDMMC host docs; and the specs of the sibling tasks in this run, read in their own
  worktrees.

Path abbreviations follow the spec's. `W/` = `/Volumes/stein/.herdr/worktrees/berean-os/`.

## Summary

Pass 0's BLOCKER is fixed correctly, and the fix runs consistently through §1.1, Goal 2, A5, §5,
§6, A6's table and A11. I found no new correctness hole in the clock, reset or acceptance logic. What
is left is one MAJOR in the device-check protocol and seven MINORs. None of them reverses a decision,
changes scope or needs the owner's judgment, so all can be fixed inline.

## Findings

### MAJOR 1 — the §7.3 before/after comparisons are confounded by sibling tasks that target the same figures

**Claim.**
- A10: "The branch is rebased onto `origin/main` (`478cf1ed`, docs and a release only, R preamble)
  before the PR opens."
- §7.3: "Flash an `x4pro` dev build of the PR branch". Steps 2, 4 and 5 then compare its figures with
  the baseline taken on `1ba1cd34`.

**Problem.** Three tasks in this run are changing exactly those figures right now. If any of them
merges before this PR is rebased and flashed, the steps below measure the sibling's change, not the
clock:

| §7.3 step | Baseline compared | Sibling that changes it |
|---|---|---|
| 2 | `BMC Loaded cache data` 276 ms, `Book loaded in` 505 ms | **berean-o-3 (R12)** rewrites the cold `BookMetadataCache::load` and the Home → Bible path |
| 4, the later reopen | ~500 ms | **berean-o-6 (R11)**: "well under 100 ms (from ~500 ms)" |
| 5 | `HOMEV Verse scan` 405 ms | **berean-o-7 (R22)**: a same-day wake "logs no `Verse scan`", so the line step 5 reads is gone |

- The landing order is plausible. This task cannot open its PR until D1 is answered and the fork is
  pushed (§4.7, A9), while the siblings are already in spec-review or plan-review.
- Once that happens, A10's "docs and a release only" is false. Nothing in the spec tells the
  implementer to re-check it, or the owner what to compare against.
- §7.4 makes the device run the only evidence of a speed-up ("only the owner's device run gives a
  figure"). A confounded run would credit the SD clock with R12's or R11's gain, in the PR body and in
  the roadmap's re-ranking.

**Evidence.**
- `hpipe show --task t3|t6|t7`: branches `perf/r12-bible-open` (epub, spec-review),
  `perf/r11-search-index-resident` (data, spec-review) and `perf/r22-verse-once-per-sleep` (ui,
  spec-review).
- `W/perf-r12-bible-open/docs/superpowers/specs/2026-10-11-berean-o-3-design.md:11-16` targets
  `Book loaded in` and `BookMetadataCache::load` 274–278 ms. Its `:44` covers the cold open, the very
  case §7.3 step 2 measures.
- `W/perf-r11-search-index-resident/docs/superpowers/specs/2026-10-11-berean-o-6-design.md:44-49`.
- `W/perf-r22-verse-once-per-sleep/docs/superpowers/specs/2026-10-11-berean-o-7-design.md:30-33`.
- `git ls-remote origin refs/heads/main` → `478cf1ed` today, so A10's premise holds only for now.

**Fix.**
- In A10, re-check the premise at rebase time with `git log --oneline 478cf1ed..origin/main --
  src lib`.
- In §7.3, state an A/B rule: the measured build differs from its comparator by the gitlink alone.
  - If nothing under `src/` or `lib/` has moved since `478cf1ed`, compare against the baseline as
    written.
  - Otherwise the owner flashes two builds of the same base: the PR branch, and the PR branch with
    the gitlink back at `67f7e012`. The step 2–5 figures are compared between those two. The baseline
    figures become context.
- Say in step 5 that after R22 the wake `Verse scan` line may be absent. Then only `Study pick`
  remains.

### MINOR 1 — A6's "every outcome prints differently" fails for a 20 MHz partial init, and §5 under-specifies `lastFailure`

**Claim.**
- A6: "With A5 in place, every outcome prints differently."
- The table row `20000 … requested 20000 … attempt 3/4 … after …` means "the loop fell back, with the
  reason".

**Problem.**
- A 20 MHz attempt keeps the partial-init rule. Its init can fail after the frequency step, at the
  SCR check (`IDF/sdmmc/sdmmc_init.c:163`, then `:169`). The host is then at 20 MHz, and the sector-0
  read decides.
- On success the line prints `20000 kHz (requested 20000 kHz, attempt 3/4, after <attempt 2's
  error>)`. That is the same line as a clean 20 MHz fallback.
- The successful attempt's own init error is lost. `mountErr` takes the read result
  (`SdmmcBlockDevice.cpp:97-98`), and §5 step 4.5 records nothing on that branch.
- So "20 MHz on a link that just failed ESP-IDF's check at 20 MHz" cannot be told from "20 MHz, link
  fine". This is the residue of pass 0's A6 point, now on the 20 MHz side.
- Separately, §5 step 4.5 says only "record the error" on the A5 and `capacity == 0` branches. It
  names `lastFailure` only for the A2 and read failures. A6 says `lastFailure` covers init errors too,
  so the two sections disagree.
- Grading is unaffected, because §7.3 step 1 already reports any `20000` line as a finding.

**Fix.**
- Keep the successful attempt's init result. When it is not `ESP_OK` (by A5, only possible on a
  20 MHz attempt), append `, partial init <esp_err_to_name>` to the line, and add that row to A6's
  table.
- Otherwise, scope the claim to "every 40 MHz outcome".
- In §5 step 4.5, say that every "record the error" sets both `mountErr` and `lastFailure`.

### MINOR 2 — A5's cost line understates what a marginal HS card pays

**Claim.** A5: "A card whose HS attempts both fail reaches 20 MHz two attempts later than today… about
2 × 200 ms of GPIO5 delays, paid only on such a card." §6: "There is no new `return false`."

**Problem.**
1. Such a card gets two 20 MHz attempts instead of today's four. A card that today needs a third or
   fourth power-cycled attempt can now end on the SD error screen. The power-cycled retry exists for
   exactly that (`SdmmcBlockDevice.cpp:78-82`). §6's sentence is true of the code, but more cards now
   reach the existing `return false`. A3 makes this trade knowingly ("The total stays four"), but
   nothing states the consequence.
2. The cost is paid on **every wake**, not once. Wake is a reset, and the baseline measures it "from
   reset" (baseline `:51`; `esp_deep_sleep_start`, `PowerManager.cpp:96`).
3. 2 × 200 ms is a floor. Each failed HS attempt also re-runs identification at 400 kHz after the
   pulse (A2).

**Fix.** State all three in A5's cost bullet and in A3's "The total stays four". No design change is
needed. A3's rejected alternatives already lay out the trade.

### MINOR 3 — the capture instruction cannot capture "from boot" across a reset, and the capture-gap remedy points the wrong way

**Claim.**
- §7.3: "Capture serial from boot with `cat /dev/cu.usbmodem* > serial.log`."
- Step 1, on a capture gap: "Re-capture from a cold boot."

**Problem.**
- The glob is expanded once, when `cat` starts, and `cat` exits when the USB device goes away. A
  single `cat` cannot be running when a re-enumerated node appears.
- `src/main.cpp:343-347` records that a cold boot races USB enumeration, while a warm reboot "worked …
  because USB was already enumerated". So a cold boot is the *worst* case for catching a line printed
  about 0.5 s after reset (250 ms at `:348`, then at least one 200 ms pulse).
- The baseline itself has one clean capture from reset (baseline `:51`).

**Fix.**
- Give a reconnecting capture: `while :; do cat /dev/cu.usbmodem* 2>/dev/null; sleep 0.1; done >>
  serial.log`, started before the boot.
- Replace "from a cold boot" with "with that loop running, from any boot".

### MINOR 4 — steps 3 and 4 name no start line for the spans they time

**Claim.**
- Step 3: "Compare the section build span from the `[SCT]` timestamps."
- Step 4: "Compare the open, ending at `[BSEARCH] 1189 verse documents`."

**Problem.**
- The brief requires "the exact log line that proves the after figure". Neither span has a named
  start line, and step 3 has no named end either.
- No log line reports a section build's duration. `[EHP] Time to parse and build pages`
  (`ChapterHtmlSlimParser.cpp:1647`) covers the parse only, not the HTML stream
  (`Section.cpp:279-315`).
- `BibleSearchStore` logs only the end of the open (`BibleSearchStore.cpp:198`).
- The baseline does not record its start points either (baseline `:47-48`).

**Fix.**
- Name the pairs. For step 3, for example: the build's first `[SCT]` line (`Streamed temp HTML to …`,
  `Section.cpp:315`, or `Reusing cached HTML`, `:273`) to `[EHP] Time to parse and build pages`.
- For step 4, name the line that marks entry to search.
- Because the baseline lacks those start points, these two are measured the same way on both builds
  of MAJOR 1's A/B.

### MINOR 5 — §7.4 sets no expectation for the book-load figures, which are mostly overhead-bound

**Claim.** §7.4 explains why the sleep and wake figures barely move. It says nothing comparable for
step 2.

**Problem.**
- The sibling R12 research measured the spine block of `book.bin` at 159,467 bytes. At 1-bit 20 MHz
  that is about 64 ms of the 276 ms `BMC Loaded cache data`, so 40 MHz can save at most about 32 ms
  there, roughly 12%.
- The rest is per-entry overhead: four mutex-guarded `HalFile` reads per spine entry.
- A small drop in step 2 is therefore expected. Without a stated ceiling, the owner could read it as
  the change failing. That is the same reasoning the spec accepted for pass 0's MINOR 3.

**Evidence.** `W/perf-r12-bible-open/docs/superpowers/research/2026-10-11-berean-o-3-research.md:130`;
`W/perf-r12-bible-open/docs/superpowers/specs/2026-10-11-berean-o-3-design.md:16`; the spec's own
2.5 MB/s figure (§1).

**Fix.** Add a §7.4 bullet: `BMC Loaded cache data` and `Book loaded in` should drop by about 30 ms at
most, and the largest SD-bound shares are the section build's HTML stream and the search index read.

### MINOR 6 — step 6's error list misattributes one line and misses most of the write traffic

**Claim.** Step 6: "The atomic writer's errors are … (`PersistableStore.cpp:77`, `:85`, `:98`, `:118`).
`writeSectors` itself fails silently …, so writes at 40 MHz only show up there."

**Problem.**
- `:98` is in `writeDocToFile`, the non-atomic writer (`PersistableStore.cpp:93-101`), not the atomic
  one.
- "Only show up there" is false. Most SD writes are not `PersistableStore` saves: section page
  serialisation, temp HTML streaming and the section rename log under `[SCT]`
  (`Section.cpp:89`, `:95`, `:311`, `:625`).
- A transient SD error during the HTML stream is retried and logged only at DBG,
  `Retrying stream (attempt N)` (`Section.cpp:276-283`, whose own comment calls it "Retry logic for SD
  card timing issues"). That is exactly the signal a marginal 40 MHz link would leave.
- Nothing lowers the clock after mount (`SdmmcBlockDevice.cpp:130-156`). So a runtime error has no
  fallback, and a failed write in the middle of a FAT update is not covered by the atomic writer.

**Fix.**
- Grade step 6 on any `[ERR]` line, any `[SD]` line other than the clock and mount lines, and any
  `[SCT] Retrying stream`.
- Correct the `:98` attribution.
- Ask the owner to copy `/.berean/` off the card before the day of use, and to reflash the previous
  build if any of those lines appear.

### MINOR 7 — small slips introduced by the revision

- **A2 and the review log, "true on every board sharing the class".**
  - On a 4-bit board, slot init holds D3 as a plain GPIO driven high until the 4-bit width switch
    hands it to the peripheral (`IDF/esp_driver_sdmmc/src/sdmmc_host.c:766-779`, `:996`).
  - `sdmmc_host_set_bus_width(slot, 1)` only writes the width register (`:991-992`), so it doesn't
    restore that.
  - The internal pull-up keeps D3 high anyway (`SdmmcBlockDevice.cpp:37`, `sdmmc_host.c:735-738`,
    `:1266`), so this is probably harmless. Still, the claim is not literally true.
  - Scope it to "the clock and the bus-width register", or name the D3 difference.
- **Goal 2 and §4.4, "the last two attempts run today's 20 MHz path unchanged" / "change nothing".**
  A2 changes where they start: they now identify at 400 kHz, where today's attempts 2–4 may identify
  at 20 MHz. A2 itself says so ("This fixes that at the same time"). Say "today's acceptance rule,
  unchanged".
- **Review log, "§8's closing claim".** That claim is §6's closing paragraph. §8 is Decisions, and
  pass 0's numbering was the same.

## Verified and sound

Each of these was re-read or re-run for this pass, and each holds:

- **Pass 0's BLOCKER fix.**
  - A 20 MHz request takes the early return and sends no CMD6 (`IDF/sdmmc/sdmmc_sd.c:489-494`). So a
    CMD6 error can only occur on attempts 1–2, and A5 sends those to the next attempt.
  - A clean init at 40 MHz has passed `sdmmc_check_scr`, the last step before `return ESP_OK`
    (`sdmmc_init.c:169`, `:175`).
  - `400 … requested 40000` cannot print: a clean init always runs `sdmmc_init_host_frequency` at
    20000 or 40000 (`sdmmc_common.c:247-248`; `sdmmc_sd.c:489-494`, `:531`;
    `sdmmc_common.c:188-190`).
  - §1.1, Goal 2, A5, §5 step 4.5, §6 rows 2–3 and A11 asserts 5–6 all state the same rule.
- **A1.** NOT_SUPPORTED comes from `sdmmc_sd.c:238-241` and `:304-306` and becomes 20 MHz plus OK
  (`sdmmc_common.c:188-190`). `MIN(host, SDR104)` is at `sdmmc_sd.c:531`. Divider 4 is 40 MHz
  (`sdmmc_host.c:255-257`). `sdmmc_fix_host_flags` rejects only SDR50/DDR50/SDR104 on a non-UHS SoC
  (`sdmmc_common.c:379-384`), so 40000 passes.
- **A2's calls.**
  - Declared at `LIBS/…/driver/sdmmc_host.h:116` and `:141`, the same order as slot init
    (`sdmmc_host.c:822-832`).
  - A clock-update command skips the card-detect check (`sdmmc_host.c:438-441`).
  - Nothing in `IDF/sdmmc/*.c` lowers the clock (`grep` finds only `sdmmc_common.c:247-248`).
- **A4.** The 20 MHz attempt uses the early return and host divider 8 (`sdmmc_host.c:258-260`),
  whatever mode the card is in. SD high-speed timing is specified from 0 to 50 MHz.
- **A6's reader and seed.** `sdmmc_host_get_real_freq` writes only after `SLOT_CHECK` and the NULL
  check (`sdmmc_host.c:354-367`). `real_freq_khz` is set only at `sdmmc_common.c:268`, after the
  `memset` at `sdmmc_init.c:49`.
- **A6's `esp_err_to_name` table is in the variant that links.**
  - The board is `"memory_type": "qio_opi"` (`esp32-s3-devkitc1-n16r8.json:5`).
  - `LIBS/qio_opi/include/sdkconfig.h:945` has `CONFIG_ESP_ERR_TO_NAME_LOOKUP 1`.
  - That variant and `LIBS/lib/libsdmmc.a` were rebuilt locally on Oct 11 by the
    `custom_sdkconfig` hybrid compile (`platformio.ini:94-116`). Its `CONFIG_ESP_TIMER_TASK_STACK_SIZE`
    is 4096, the custom value, where `dio_opi` keeps the stock 8192.
  - So the code that links is compiled from `IDF/`, which makes the spec's "source matches what
    links" stronger than R states.
- **A6 visibility.** The pre-mount `LOG_INF` (`src/main.cpp:399`) goes through `if (logSerial)`
  (`lib/Logging/Logging.cpp:71`, `Logging.h:35`, default transport `BoardConfig.h:319-320`). The HWCDC
  arming and connect logic is at `HWCDC.cpp:86-95`, `:134` and `:181-188`.
- **A3 and A11.** The `int` element type matches `sdmmc_host_t::max_freq_khz`
  (`sd_protocol_types.h:214`). The `sizeof` idiom is at `AudioManager.cpp:84` and
  `LilyGoT5S3LgfxConfig.cpp:176`. The file-scope `static_assert` model is at `RecoveryBoot.cpp:55` and
  `SdPaths.h:36-43`. The red step fails on asserts 2 and 5 only, so it fails on values, not names.
- **Single caller.** `src/main.cpp:406` → `lib/hal/HalStorage.cpp:26`. That is the only
  `Storage.begin`/`SDCard.begin` in `src lib`.
- **The three "40 MHz" SD claims.** They are the only ones: `SdmmcBlockDevice.cpp:22`, `:25`,
  `BoardConfig.h:1446` and `xteink-x4pro-support.md:219`. The other 40 MHz hits in the SDK are SPI
  clocks.
- **Fork state.** `ls-remote` of the fork's `berean` gives `67f7e012…`, which equals the gitlink. The
  clone is shallow, and its `origin` is the fork. `.gitmodules` has `branch = berean`. CI checks out
  submodules at `ci.yml:46,71,94,131`.
- **Formatter scope.** `bin/clang-format-fix:49` walks `git ls-files`, which lists `freeink-sdk` as a
  single entry.

VERDICT: CLEAR
