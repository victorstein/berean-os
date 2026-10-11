# berean-o-5 — run the SD card at 40 MHz: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** the X4 Pro's SD card is clocked at 40 MHz high speed where it can hold it. The mount
falls back to 20 MHz where it can't, and the boot log prints the clock the card really runs at.

**Architecture:**
- **The code:** all of it changes in one SDK file, `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp`.
  - A file-scope `constexpr` schedule gives each of the four mount attempts its clock.
  - A `constexpr` predicate decides which attempts may mount on a partial init.
  - Each attempt restores the probing clock and the 1-bit bus first.
  - One `Serial.printf` reports the result.
- **The comments:** two comments and the SDK doc are corrected in the same SDK commit.
- **The landing:** that commit is fast-forwarded onto the fork `victorstein/freeink-sdk` branch
  `berean`, and the bereanOS PR bumps the `freeink-sdk` gitlink.

**Tech stack:** ESP-IDF 5.5.2 SDMMC host and `sdmmc_cmd` (prebuilt in
`framework-arduinoespressif32-libs` 3.3.7), Arduino-ESP32 3.3.7, PlatformIO 6.1.19, GCC 14.2
(`xtensa-esp32s3-elf`).

**Spec:** `docs/superpowers/specs/2026-10-11-berean-o-5-design.md`, cited below as "spec" plus a
section or assumption (A1–A12).

**Model:** the code follows the file's own idiom.
- The `k`-prefixed constants follow `SdmmcBlockDevice.h:42-43`.
- The `if (Serial) Serial.printf("[%lu] [SD] …")` lines follow `SdmmcBlockDevice.cpp:102-103`.
- The `sizeof / sizeof[0]` count follows `freeink-sdk/libs/hardware/AudioManager/src/AudioManager.cpp:84`.
- The `constexpr` + `static_assert` test follows `lib/Serialization/SdPaths.h:36-43`.

FILES: freeink-sdk, freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp
FILES: freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h, freeink-sdk/docs/xteink-x4pro-support.md

---

## How this plan was checked

Every code block below was applied, in order, to the current `SdmmcBlockDevice.cpp` (gitlink
`67f7e012`) by a script that requires each "replace this" block to match exactly once. Each
resulting stage was then compiled with the real toolchain and headers:

- `xtensa-esp32s3-elf-g++ -std=gnu++2a -fsyntax-only -Wall -Wextra`;
- the `qio_opi` sdkconfig, the Arduino core, SdFat from `.pio/libdeps/x4pro`, and
  `-DFREEINK_DEVICE_X4PRO=1 -DUSE_BLOCK_DEVICE_INTERFACE=1`.

The results:

| Stage | Result |
|---|---|
| After Task 1 | compiles, no warnings |
| After Task 2 | compiles, no warnings |
| Task 3 red | fails with exactly the two static-assertion errors shown in Task 3 |
| Task 3 green | compiles, no warnings |
| After Task 4 | compiles, no warnings, and is byte-identical to the final file this plan describes |

The real `pio run` is still the gate in every task. This check only means the blocks are known to
match and compile.

## Rules for the implementer

- **Re-read before editing.** Re-read every file before you edit it. A sibling task may have
  changed it since this plan was written. If a "replace this" block no longer matches exactly,
  stop and report; do not improvise a merge.
- **No bootstrap after Task 1 starts.** Do not run `./bin/bootstrap` or `git submodule update`
  once Task 1 begins. Both check the submodule out at the recorded gitlink, which would move it off
  the local SDK commits.
- **`pio` is not on `PATH`.** Use `/Volumes/stein/.platformio/penv/bin/pio`. Run builds one at a
  time, never two `pio run`s at once.
- **Sibling worktrees share the packages.** They all use `~/.platformio/packages`, and a sibling's
  build can rewrite `framework-arduinoespressif32/idf_component.yml` under yours. So a `FAILED`
  with no `error:` line in `SdmmcBlockDevice.cpp` is environmental, not a code result.
  - Re-run the same command once.
  - If it fails again, read the unfiltered tail (`… 2>&1 | tail -40`) before concluding anything
    about the code.
- **`SCRATCH` must be set in every shell call that uses it.** Shell variables do not survive from
  one tool call to the next. Each block that uses `$SCRATCH` starts with
  `: "${SCRATCH:?set SCRATCH to the session scratchpad}"`, so it stops rather than writing to `/`.
  Prefix the block with `SCRATCH=<this session's scratchpad directory>` when you run it.
- **Rebase conflicts stop the plan.** If any `git rebase` stops on a conflict, run
  `git rebase --abort` and report. A conflict on `freeink-sdk` means `main` moved the gitlink. The
  SDK commit would then have to move onto `main`'s gitlink, and that needs a fresh decision.
- **No direct `clang-format`.** Never invoke or probe `clang-format` directly; only
  `./bin/clang-format-fix`. It does not reach files inside `freeink-sdk/` (spec §7.2), so every SDK
  edit below is already formatted: 2-space indent, at most 120 columns.
- **Comments.** No comment may restate the code. The comments in the blocks below are the only
  ones to add.
- **SDK commits.** The SDK commits in Tasks 1–4 are local work-in-progress commits on a local
  branch inside `freeink-sdk/`. Task 6 squashes them into the one commit spec A9 calls for. Nothing
  is pushed to the fork before Task 8.

---

### Task 0: Start from a clean, current tree

**Files:** none edited.

- [ ] **Step 1: Confirm the branch, remotes and a clean tree**

Run:

```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/perf-r2-sd-40mhz
git branch --show-current
git status --short
git -C freeink-sdk rev-parse HEAD
git -C freeink-sdk status --short
```

Expected:
- the branch is `perf/r2-sd-40mhz`;
- both `git status` outputs are empty;
- the SDK HEAD is `67f7e012e3554106062bc52a3131523339a8ac4e`.

If the SDK HEAD differs, stop and report.

- [ ] **Step 2: Rebase onto the latest `origin/main`**

Run:

```bash
git fetch origin main
git rebase origin/main
git ls-tree HEAD freeink-sdk
```

Expected: the rebase succeeds (the branch carries only this bead's docs), and the gitlink still
reads `67f7e012e3554106062bc52a3131523339a8ac4e`.
- **If the rebase stops on a conflict,** run `git rebase --abort` and report.
- **If `origin/main` moved the gitlink,** stop and report: the SDK work must then start from the
  new gitlink.

- [ ] **Step 3: Put the SDK on a local work branch**

Run:

```bash
git -C freeink-sdk switch -c perf/r2-sd-40mhz
git -C freeink-sdk log --oneline -1
```

Expected: `67f7e01 fix: confirm X4 Pro VBUS detect on GPIO21`.

- [ ] **Step 4: Confirm the baseline build is green before any edit**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "error:|static assertion|SUCCESS|FAILED" | tail -5
```

Expected: a line containing `x4pro` and `SUCCESS`. The first build in a worktree can take 3–4
minutes.

---

### Task 1: Drive the mount clock from a schedule, with today's behaviour (refactor, green)

Spec §7.1 step 1. Each attempt now takes its clock from `kAttemptClockKhz` and its partial-init
rule from `acceptsPartialInit`. Both are set to today's behaviour: four attempts at
`SDMMC_FREQ_DEFAULT`, and every attempt tolerant. So nothing the device does changes.

**Files:**
- Modify: `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp` (`:15-17`, `:24-25`,
  `:84`, `:91-95`)

- [ ] **Step 1: Add the schedule and the predicate**

In `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp`, replace this:

```cpp
namespace freeink {

bool SdmmcBlockDevice::begin(
```

with this:

```cpp
namespace freeink {

namespace {

constexpr int kAttemptClockKhz[] = {SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT};
constexpr int kMountAttempts = sizeof(kAttemptClockKhz) / sizeof(kAttemptClockKhz[0]);

constexpr bool acceptsPartialInit(int /*requestedKhz*/) { return true; }

}  // namespace

bool SdmmcBlockDevice::begin(
```

- [ ] **Step 2: Remove the one-off clock assignment**

Replace this:

```cpp
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz
```

with this:

```cpp
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
```

- [ ] **Step 3: Bound the loop by the schedule**

Replace this:

```cpp
  for (int attempt = 0; attempt < 4; attempt++) {
```

with this:

```cpp
  for (int attempt = 0; attempt < kMountAttempts; attempt++) {
```

- [ ] **Step 4: Set the clock per attempt, and gate the partial-init fallthrough**

Replace this:

```cpp
    esp_err_t e = sdmmc_card_init(&host, card);
    if (e != ESP_OK && card->csd.capacity == 0) {
      mountErr = e;  // failed before CSD — nothing to read; power-cycle and retry
      continue;
    }
```

with this:

```cpp
    host.max_freq_khz = kAttemptClockKhz[attempt];
    esp_err_t e = sdmmc_card_init(&host, card);
    if (e != ESP_OK && (!acceptsPartialInit(host.max_freq_khz) || card->csd.capacity == 0)) {
      mountErr = e;  // no CSD, or a high-speed attempt: retry rather than trust a read
      continue;
    }
```

- [ ] **Step 5: Build**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "error:|warning:.*SdmmcBlockDevice|static assertion|SUCCESS|FAILED" | tail -5
```

Expected: a line containing `x4pro` and `SUCCESS`, with no `SdmmcBlockDevice` warnings.

- [ ] **Step 6: Commit (local SDK work-in-progress commit)**

```bash
git -C freeink-sdk add libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp
git -C freeink-sdk commit -m "refactor: drive the SDMMC mount clock from a per-attempt schedule"
```

---

### Task 2: Restore the probing clock per attempt, and log the real clock (green)

Spec A2, A6 and §5. This is still at today's clock, because the schedule is unchanged.

The spec's order (§7.1 step 3) adds A2 and A6 after the clock values flip. This plan adds them
first, so that no commit, even a local work-in-progress one, ever asks for 40 MHz without the
per-attempt reset. Task 3 still runs red on values, then green.

No host test is possible here (spec A12). The proof is the build now, and the boot log on the
device later (Task 10).

**Files:**
- Modify: `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp` (the retry loop and
  the success path)

- [ ] **Step 1: Add the reset and the bookkeeping at the top of the loop**

This step alone does not compile. It declares `esp_err_t e` at the top of the loop body, and
Task 1's `esp_err_t e = sdmmc_card_init(…)` is still in the same scope until Step 2 replaces it.
The result is `error: redeclaration of 'esp_err_t e'`. Build only at Step 4.

Replace this:

```cpp
  // block I/O works, and GPIO5 is left in the exact LOW state that read succeeded under.
  esp_err_t mountErr = ESP_FAIL;
  for (int attempt = 0; attempt < kMountAttempts; attempt++) {
    if (sdPwr >= 0) {
```

with this:

```cpp
  // block I/O works, and GPIO5 is left in the exact LOW state that read succeeded under.
  // sdmmc_card_init never lowers the clock or the bus width, so each attempt first
  // restores the 400 kHz probing clock and 1-bit bus that slot init left, and only a
  // 20 MHz attempt may fall through to that read after a partial init.
  esp_err_t mountErr = ESP_FAIL;
  esp_err_t lastFailure = ESP_OK;
  esp_err_t initErr = ESP_OK;
  int mountedAttempt = -1;
  for (int attempt = 0; attempt < kMountAttempts; attempt++) {
    esp_err_t e = sdmmc_host_set_card_clk(host.slot, SDMMC_FREQ_PROBING);
    if (e == ESP_OK) e = sdmmc_host_set_bus_width(host.slot, 1);
    if (e != ESP_OK) {
      mountErr = lastFailure = e;
      continue;
    }
    if (sdPwr >= 0) {
```

- [ ] **Step 2: Keep the init result, the last failure, and the successful attempt**

Replace this:

```cpp
    host.max_freq_khz = kAttemptClockKhz[attempt];
    esp_err_t e = sdmmc_card_init(&host, card);
    if (e != ESP_OK && (!acceptsPartialInit(host.max_freq_khz) || card->csd.capacity == 0)) {
      mountErr = e;  // no CSD, or a high-speed attempt: retry rather than trust a read
      continue;
    }
    // CSD is valid (capacity known); prove real block I/O before committing.
    e = sdmmc_read_sectors(card, _dmaBuffer, 0, 1);
    mountErr = e;
    if (e == ESP_OK) break;
  }
```

with this:

```cpp
    host.max_freq_khz = kAttemptClockKhz[attempt];
    initErr = sdmmc_card_init(&host, card);
    if (initErr != ESP_OK && (!acceptsPartialInit(host.max_freq_khz) || card->csd.capacity == 0)) {
      mountErr = lastFailure = initErr;  // no CSD, or a high-speed attempt: retry rather than trust a read
      continue;
    }
    // CSD is valid (capacity known); prove real block I/O before committing.
    e = sdmmc_read_sectors(card, _dmaBuffer, 0, 1);
    mountErr = e;
    if (e == ESP_OK) {
      mountedAttempt = attempt;
      break;
    }
    lastFailure = e;
  }
```

- [ ] **Step 3: Print the clock line on success**

Replace this:

```cpp
    sdmmc_host_deinit();
    return false;
  }
  _card = card;
  return true;
}
```

with this:

```cpp
    sdmmc_host_deinit();
    return false;
  }
  // The host's real dividers, not card->real_freq_khz: a partial init can stop before the
  // step that sets that field, which then reads 0 for a card left at the probing clock.
  int clockKhz = 0;
  if (sdmmc_host_get_real_freq(host.slot, &clockKhz) != ESP_OK) clockKhz = card->real_freq_khz;
  const bool retried = mountedAttempt > 0;
  const bool partialInit = initErr != ESP_OK;
  if (Serial)
    Serial.printf("[%lu] [SD] SDMMC clock %d kHz (requested %d kHz, attempt %d/%d%s%s%s%s)\n", millis(), clockKhz,
                  host.max_freq_khz, mountedAttempt + 1, kMountAttempts, retried ? ", after " : "",
                  retried ? esp_err_to_name(lastFailure) : "", partialInit ? ", partial init " : "",
                  partialInit ? esp_err_to_name(initErr) : "");
  _card = card;
  return true;
}
```

Why these invariants hold, for the reader checking A6:
- **`lastFailure` is never `ESP_OK` when `retried` is true.** Every path that ends an attempt
  without `break` assigns it: the reset failure, the skipped init, and the failed read.
- **`initErr` is the successful attempt's own init result.** It is assigned once per attempt that
  gets past the reset, and the loop breaks right after the read that succeeds.
- **`host.max_freq_khz` still holds the successful attempt's request.** It is not touched after the
  `break`.

- [ ] **Step 4: Build**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "error:|warning:.*SdmmcBlockDevice|static assertion|SUCCESS|FAILED" | tail -5
```

Expected: a line containing `x4pro` and `SUCCESS`, with no `SdmmcBlockDevice` warnings.

- [ ] **Step 5: Commit (local SDK work-in-progress commit)**

```bash
git -C freeink-sdk add libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp
git -C freeink-sdk commit -m "fix: restore the SDMMC probing clock per mount attempt and log the real clock"
```

---

### Task 3: Ask for high speed, under compile-time checks (red, then green)

Spec A1, A3, A5 and A11, and §7.1 steps 2–3.

**Files:**
- Modify: `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp` (the anonymous
  namespace added in Task 1)

- [ ] **Step 1: Write the failing checks**

Replace this:

```cpp
constexpr bool acceptsPartialInit(int /*requestedKhz*/) { return true; }

}  // namespace
```

with this:

```cpp
constexpr bool acceptsPartialInit(int /*requestedKhz*/) { return true; }

constexpr bool noAttemptAboveHighSpeed() {
  for (int khz : kAttemptClockKhz) {
    if (khz > SDMMC_FREQ_HIGHSPEED) return false;
  }
  return true;
}

static_assert(kMountAttempts == 4, "the mount keeps its budget of four power-cycled attempts");
static_assert(kAttemptClockKhz[0] == SDMMC_FREQ_HIGHSPEED, "the first attempt asks for high speed");
static_assert(kAttemptClockKhz[kMountAttempts - 1] == SDMMC_FREQ_DEFAULT,
              "a card that can't hold high speed still gets a 20 MHz attempt");
static_assert(noAttemptAboveHighSpeed(), "the ESP32-S3 has no UHS-I rates above high speed");
static_assert(!acceptsPartialInit(SDMMC_FREQ_HIGHSPEED), "a high-speed attempt mounts only on a clean init");
static_assert(acceptsPartialInit(SDMMC_FREQ_DEFAULT), "a 20 MHz attempt keeps today's partial-init tolerance");

}  // namespace
```

- [ ] **Step 2: Run the build and watch it fail on values**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "static assertion failed|FAILED" | sed 's|.*/||'
```

Expected, exactly these two assertion failures and a failed environment:

```
SdmmcBlockDevice.cpp:<line>:<col>: error: static assertion failed: the first attempt asks for high speed
SdmmcBlockDevice.cpp:<line>:<col>: error: static assertion failed: a high-speed attempt mounts only on a clean init
```

They are followed by a line containing `x4pro` and `FAILED`.

Read the result in this order:

1. **`FAILED` with no `static assertion failed` line at all** is environmental (see "Sibling
   worktrees share the packages"). Re-run once, then read
   `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tail -40` before going further.
2. **A different set of `SdmmcBlockDevice.cpp` errors means a check is wrong.** For example, an
   error about an undeclared name, or the 4-entry, last-entry, no-UHS or 20 MHz-tolerance
   assertion. Stop and fix the check before going on.
3. **Do not commit this state.**

- [ ] **Step 3: Set the schedule and the predicate to the spec's values**

Replace this:

```cpp
constexpr int kAttemptClockKhz[] = {SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT, SDMMC_FREQ_DEFAULT};
constexpr int kMountAttempts = sizeof(kAttemptClockKhz) / sizeof(kAttemptClockKhz[0]);

constexpr bool acceptsPartialInit(int /*requestedKhz*/) { return true; }
```

with this:

```cpp
// Two high-speed attempts keep one mount-sequencing retry at 40 MHz before the last two fall
// back to 20 MHz, for a card whose link can't hold high speed on this wiring.
constexpr int kAttemptClockKhz[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_DEFAULT,
                                    SDMMC_FREQ_DEFAULT};
constexpr int kMountAttempts = sizeof(kAttemptClockKhz) / sizeof(kAttemptClockKhz[0]);

// Whether an attempt may still validate a sector-0 read after sdmmc_card_init failed past the
// CSD. At high speed that read would run either at the 400 kHz probing clock a failed CMD6
// left, or at 40 MHz on a link ESP-IDF's own SCR check just rejected.
constexpr bool acceptsPartialInit(int requestedKhz) { return requestedKhz <= SDMMC_FREQ_DEFAULT; }
```

- [ ] **Step 4: Run the build and watch it pass**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "error:|warning:.*SdmmcBlockDevice|static assertion|SUCCESS|FAILED" | tail -5
```

Expected: a line containing `x4pro` and `SUCCESS`, with no assertion failures and no
`SdmmcBlockDevice` warnings.

- [ ] **Step 5: Commit (local SDK work-in-progress commit)**

```bash
git -C freeink-sdk add libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp
git -C freeink-sdk commit -m "perf: request SDMMC high speed with a 20 MHz fallback"
```

---

### Task 4: Correct the comments and the SDK doc

Spec A8. These are comment and doc changes only. Build once at the end, because Step 1 touches a
`.cpp`.

**Files:**
- Modify: `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp` (the comment above
  `SDMMC_HOST_DEFAULT()`)
- Modify: `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1445-1449`
- Modify: `freeink-sdk/docs/xteink-x4pro-support.md:217-219`, and after `:243`

- [ ] **Step 1: The host-config comment in `SdmmcBlockDevice.cpp`**

Replace this:

```cpp
  // Host config matches the OEM (recovered from app1's mountSD via Ghidra): full
  // default capability flags (0x37) with the actual width selected via slot.width
  // only, and the data clock at 40 MHz. The read timeouts we chased earlier were a
  // mount-sequencing problem, not a clock-margin one — see the retry loop below.
```

with this:

```cpp
  // Host config matches the OEM (recovered from app1's mountSD via Ghidra): full
  // default capability flags (0x37) with the actual width selected via slot.width
  // only. The OEM claims a 40 MHz data clock; here each mount attempt sets
  // host.max_freq_khz from kAttemptClockKhz, and ESP-IDF switches the card to high
  // speed (40 MHz) only if it supports it, negotiating 20 MHz otherwise. At 20 MHz the
  // read timeouts we chased earlier were a mount-sequencing problem, not a
  // clock-margin one — see the retry loop below.
```

- [ ] **Step 2: The X4 Pro profile comment in `BoardConfig.h`**

In `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`, replace this:

```cpp
    // SD is native SDMMC, NOT SPI: the card the OEM reads is silent to SPI-mode CMD0.
    // CONFIRMED on hardware: 1-bit, slot 1, CLK=41 CMD=42 DAT0=40, internal pull-ups, 40 MHz.
    // D1/D2/D3 are UNUSED in 1-bit. Mounts reliably via SdmmcBlockDevice, which power-cycles
```

with this:

```cpp
    // SD is native SDMMC, NOT SPI: the card the OEM reads is silent to SPI-mode CMD0.
    // CONFIRMED on hardware: 1-bit, slot 1, CLK=41 CMD=42 DAT0=40, internal pull-ups. The mount
    // requests 40 MHz high speed and the boot log's "[SD] SDMMC clock" line prints the clock the
    // card actually runs at.
    // D1/D2/D3 are UNUSED in 1-bit. Mounts reliably via SdmmcBlockDevice, which power-cycles
```

- [ ] **Step 3: The SD section of the SDK doc**

In `freeink-sdk/docs/xteink-x4pro-support.md`, replace this:

```markdown
**SDMMC, not SPI — CONFIRMED WORKING on hardware.** The slot is driven as **native
SDMMC** (`esp_driver_sdmmc`); SPI-mode CMD0 is silent, which is why an SPI card path
never worked. 1-bit mode, slot 1, internal pull-ups, 40 MHz.
```

with this:

```markdown
**SDMMC, not SPI — CONFIRMED WORKING on hardware.** The slot is driven as **native
SDMMC** (`esp_driver_sdmmc`); SPI-mode CMD0 is silent, which is why an SPI card path
never worked. 1-bit mode, slot 1, internal pull-ups. The mount requests 40 MHz high
speed (`SDMMC_FREQ_HIGHSPEED`); ESP-IDF negotiates 20 MHz for a card without high
speed, and the boot log's `[SD] SDMMC clock` line prints the clock the card actually
runs at.
```

- [ ] **Step 4: The mount-sequence addition in the SDK doc**

In the same file, replace this:

```markdown
2. **Leave GPIO5 LOW.** Driving it HIGH after init (an earlier misreading of the
   OEM) breaks every subsequent read with `0x107`. It must stay in the LOW state the
   validated read succeeded under.
```

with this:

```markdown
2. **Leave GPIO5 LOW.** Driving it HIGH after init (an earlier misreading of the
   OEM) breaks every subsequent read with `0x107`. It must stay in the LOW state the
   validated read succeeded under.

`SdmmcBlockDevice::begin` adds one rule of its own. `sdmmc_card_init` never lowers the
clock or the bus width, so each attempt first restores the 400 kHz probing clock and
1-bit bus that slot init left. The first two attempts request 40 MHz high speed and
mount only on a clean init; the last two run at 20 MHz and keep the original
tolerance of a partial init followed by a good sector-0 read. The `[SD] SDMMC clock`
line names the attempt, the last failure before it, and any partial init.
```

- [ ] **Step 5: Check that no "40 MHz" SD claim survives unqualified**

Run:

```bash
grep -n "40 MHz" freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp \
  freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h freeink-sdk/docs/xteink-x4pro-support.md
```

Expected:
- every hit describes the request, the fallback or the OEM's claim;
- no line asserts that the card runs at 40 MHz;
- `BoardConfig.h` shows only the new "requests 40 MHz high speed" line for the SD clock. Other
  "40 MHz" hits there, if any, are SPI clocks and stay as they are.

- [ ] **Step 6: Build**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "error:|warning:.*SdmmcBlockDevice|static assertion|SUCCESS|FAILED" | tail -5
```

Expected: a line containing `x4pro` and `SUCCESS`.

- [ ] **Step 7: Commit (local SDK work-in-progress commit)**

```bash
git -C freeink-sdk add libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp \
  libs/hardware/BoardConfig/include/BoardConfig.h docs/xteink-x4pro-support.md
git -C freeink-sdk commit -m "docs: say what clock the X4 Pro SD mount requests and logs"
```

---

### Task 5: Full verification on the host

Spec §7.2.

**Files:** none edited.

- [ ] **Step 1: The release build**

Run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release 2>&1 | grep -E "error:|static assertion|SUCCESS|FAILED" | tail -5
```

Expected: a line containing `x4pro-gh_release` and `SUCCESS`.

- [ ] **Step 2: The host test suite, once (no host code changed)**

Run:

```bash
cmake -S test -B build/test
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j
```

Expected: `100% tests passed`.
- **A first-configure failure in several suites at once** is the known gtest build race. Re-run
  `cmake --build build/test -j` unchanged.
- **Any other failure** existed before this change, because no host code changed. Record it, and
  check it against `main` before reporting.

- [ ] **Step 3: The formatter over the whole tree**

Run:

```bash
./bin/clang-format-fix
git status --short
```

Expected: the only entry is ` M freeink-sdk`, the moved submodule. No bereanOS source file is
modified.

- [ ] **Step 4: Confirm the SDK diff is exactly the planned one**

Run:

```bash
git -C freeink-sdk diff --stat 67f7e012e3554106062bc52a3131523339a8ac4e HEAD
```

Expected: three files changed:
- `docs/xteink-x4pro-support.md`
- `libs/hardware/BoardConfig/include/BoardConfig.h`
- `libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp`

---

### Task 6: Squash the SDK work into the one commit spec A9 names

**Files:** none edited (history only, inside `freeink-sdk/`).

- [ ] **Step 1: Squash onto `67f7e012`**

Write the message to the session scratchpad (any file outside the repo), then run the commit.
The `&&` chain matters. If the message file can't be written, the reset must not run, or the SDK
would be left at `67f7e012` with the whole diff staged and no commit. If that happens anyway,
`git -C freeink-sdk commit -F <message file>` once the file exists restores it.

The message carries no assistant trailer, matching the fork's previous commit (`67f7e01`, whose
body has none).

Run, with `SCRATCH=<this session's scratchpad directory>` prefixed:

```bash
: "${SCRATCH:?set SCRATCH to the session scratchpad}"
cat > "$SCRATCH/sdk-commit-msg.txt" <<'EOF' &&
perf: clock the SDMMC card at 40 MHz high speed

The mount asked for SDMMC_FREQ_DEFAULT, which ESP-IDF 5.5 defines as
20 MHz, while its comment claimed 40. Each mount attempt now takes its
clock from a schedule: two high-speed attempts, then two at 20 MHz for a
card whose link can't hold 40 MHz. A high-speed attempt mounts only on a
clean init, each attempt first restores the probing clock and 1-bit bus,
and the boot log prints the clock the card really runs at (bereanOS
berean-o-5).
EOF
git -C freeink-sdk reset --soft 67f7e012e3554106062bc52a3131523339a8ac4e &&
git -C freeink-sdk commit -F "$SCRATCH/sdk-commit-msg.txt" &&
git -C freeink-sdk log --oneline -2
```

Expected: two lines. The new `perf: clock the SDMMC card at 40 MHz high speed` is on top of
`67f7e01 fix: confirm X4 Pro VBUS detect on GPIO21`.

- [ ] **Step 2: Prove the squash changed nothing**

Run:

```bash
git -C freeink-sdk diff --stat 67f7e012e3554106062bc52a3131523339a8ac4e HEAD
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "SUCCESS|FAILED" | tail -2
```

Expected: the same three files as Task 5 Step 4, and `SUCCESS`. The build is incremental and
quick, because no file content changed.

---

### Task 7: Decision D1 — the owner's approval to push to the fork

Spec §8, D1. Pushing to `berean` needs the owner's approval (`.claude/agents/hal-dev.md`, "pushed
to `berean` only with the owner's approval").

**Files:** none edited.

- [ ] **Step 1: Surface the decision as the last action of the turn**

Run, replacing nothing (the command is complete; `SDK_SHA` is read from the tree):

```bash
SDK_SHA=$(git -C freeink-sdk rev-parse HEAD)
hpipe decide --task t5 \
  --question "berean-o-5 needs one SDK commit (${SDK_SHA}, 'perf: clock the SDMMC card at 40 MHz high speed') fast-forwarded onto victorstein/freeink-sdk branch berean (67f7e01..${SDK_SHA:0:7}), so the bereanOS PR's gitlink resolves in CI. hal-dev.md requires the owner's approval for any push to berean." \
  --recommend "Approve the fast-forward push, exactly as #185 did (310ec61..67f7e01): git -C freeink-sdk push https://github.com/victorstein/freeink-sdk.git HEAD:refs/heads/berean, no force, nothing to Free-Ink. The commit is build-verified for x4pro and x4pro-gh_release, and it touches only SdmmcBlockDevice.cpp plus two comment/doc corrections."
```

Then **end the turn**. Do not push, open a PR, or continue until the answer arrives in the pane.

- [ ] **Step 2: Act on the answer**
  - **Approved:** continue with Task 8.
  - **Declined, or approved with conditions:** follow the answer. If it rules out the push,
    report and stop.

---

### Task 8: Push the SDK commit to the fork

**Files:** none edited.

- [ ] **Step 1: Confirm the fork's `berean` is still at `67f7e012`**

Run:

```bash
git ls-remote https://github.com/victorstein/freeink-sdk.git refs/heads/berean
```

Expected: `67f7e012e3554106062bc52a3131523339a8ac4e	refs/heads/berean`. If it moved, stop and
report. The commit must then be rebased onto the new tip, and that needs a fresh decision.

- [ ] **Step 2: Fast-forward push**

Run:

```bash
git -C freeink-sdk push https://github.com/victorstein/freeink-sdk.git HEAD:refs/heads/berean
```

Expected: `67f7e01..<new sha>  HEAD -> berean`, and no `forced update`.

- [ ] **Step 3: Confirm the fork holds it**

Run:

```bash
git ls-remote https://github.com/victorstein/freeink-sdk.git refs/heads/berean
git -C freeink-sdk rev-parse HEAD
```

Expected: the two SHAs are equal.

---

### Task 9: Bump the gitlink in bereanOS

Spec A10.

**Files:**
- Modify: `freeink-sdk` (the gitlink)

- [ ] **Step 1: Commit the gitlink**

The trailer is the implementing session's own attribution link. Use the URL your session's
attribution instructions give for commit messages ("End git commit messages with:
Claude-Session: …"), not the planning session's. Implementers run in their own sessions.

Run, with `SESSION_LINK=<that URL>` prefixed:

```bash
: "${SESSION_LINK:?set SESSION_LINK to the Claude-Session URL of this session}"
SDK_SHA=$(git -C freeink-sdk rev-parse HEAD)
git add freeink-sdk
git commit -m "perf: run the SD card at 40 MHz high speed" -m "Bumps freeink-sdk 67f7e01..${SDK_SHA:0:7} on the fork's berean branch: the
SDMMC mount now requests 40 MHz high speed with a 20 MHz fallback, and
the boot log prints the clock the card actually runs at (berean-o-5)." -m "Claude-Session: ${SESSION_LINK}"
git show --stat HEAD | tail -3
git ls-tree HEAD freeink-sdk
```

Expected:
- the commit changes only `freeink-sdk`;
- `ls-tree` shows the new SHA;
- `git status --short` is empty.

- [ ] **Step 2: Bring the branch up to date, and decide the device comparison (MAJOR 1 of review 2)**

Run:

```bash
BEFORE_REBASE=$(git rev-parse HEAD)
git fetch origin main
git rebase origin/main
git log --oneline 478cf1ed..origin/main -- src lib
git diff --stat "$BEFORE_REBASE" HEAD -- . ':(exclude)docs'
```

**If the rebase stops on a conflict,** run `git rebase --abort` and report. The SDK commit is
already on the fork (Task 8). So a `freeink-sdk` conflict means `main` moved the gitlink: the SDK
commit has to be rebased onto `main`'s gitlink and pushed again, and that needs a fresh decision.

Expected: the rebase succeeds. Then record which case applies, for the PR body (Task 10):
- **Case BASELINE:** the `git log` prints nothing. The device figures are compared with the
  2026-10-11 baseline.
- **Case A/B:** the `git log` prints one or more commits. The owner compares two builds of this
  branch, with the gitlink at `67f7e012` (A) and as pushed (B).

As of this plan, `origin/main` already carries `106dfc4b` (#255, Bible search and Publications
enter with a FAST refresh) under `src/`, so expect Case A/B.

Also record whether the final `git diff --stat` printed anything. It lists what this rebase
brought outside `docs/`, and Step 3 depends on it.

- [ ] **Step 3: Build after the rebase, only if it brought code**

If Step 2's `git diff --stat` printed any file, run:

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "error:|static assertion|SUCCESS|FAILED" | tail -5
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release 2>&1 | grep -E "error:|static assertion|SUCCESS|FAILED" | tail -5
./bin/clang-format-fix && git status --short
```

Expected: both builds `SUCCESS`, and `git status --short` empty. If the rebase brought only docs,
skip this step and say so in the PR body.

---

### Task 10: Push and open the PR

**Files:** none edited.

- [ ] **Step 1: Push the branch**

Run:

```bash
git push --force-with-lease origin HEAD
```

`--force-with-lease` is needed because Task 0 and Task 9 rebased a branch that was already
pushed with the docs commits. It refuses if someone else pushed to this branch in the meantime.

- [ ] **Step 2: Write the PR body**

Write the template with a **quoted** heredoc, so nothing in it is expanded. Write the two
comparison paragraphs the same way. Then fill the six `@…@` tokens with the script below. Four
are read from the tree. The other two come from this session: the host-test result from Task 5
Step 2, and the session's own attribution link.

The device-check step 4 marker drops the spec's "(~1507 ms)" (spec §7.3 step 4). #255, already on
`main`, paints Bible search's Opening frame with a FAST refresh, so that duration no longer holds.
The `Wait complete:  8179_DRF` line itself still marks the end of the frame.

```bash
: "${SCRATCH:?set SCRATCH to the session scratchpad}"
cat > "$SCRATCH/pr-body.template.md" <<'EOF'
## What this changes

The X4 Pro's SD card has been running at 20 MHz, not the 40 MHz the code comments claimed. `SdmmcBlockDevice::begin` set `host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz`, but ESP-IDF 5.5 defines `SDMMC_FREQ_DEFAULT` as 20000 kHz and `SDMMC_FREQ_HIGHSPEED` as 40000 kHz. On a 1-bit bus that caps every SD read at about 2.5 MB/s raw instead of about 5 MB/s.

In `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp`, each of the four mount attempts now takes its clock from a schedule.
- **Attempts 1–2 request 40 MHz high speed.** ESP-IDF switches the card only if it supports high speed. A card without it gets 20 MHz from ESP-IDF itself, on the first attempt.
- **A 40 MHz attempt mounts only on a clean init.** If init fails, it does not fall through to the sector-0 read. That read would otherwise run at the 400 kHz probing clock a failed switch left, or at 40 MHz on a link ESP-IDF's own SCR check had just rejected.
- **Attempts 3–4 run at 20 MHz** under today's rule, unchanged.
- **Every attempt first restores the 400 kHz probing clock and the 1-bit bus.** `sdmmc_card_init` never lowers either, so retries used to identify the card at the raised clock.
- **The boot log prints, once:** `[SD] SDMMC clock <real> kHz (requested <requested> kHz, attempt <n>/4[, after <last error>][, partial init <error>])`. The value is read from the host's real dividers.

The same SDK commit corrects the three comments that claimed a 40 MHz clock (`SdmmcBlockDevice.cpp`, `BoardConfig.h`, `docs/xteink-x4pro-support.md`).

**Memory:** there is no new allocation. The schedule is 16 bytes of `constexpr` data, and the success path adds three locals. The high-speed switch makes ESP-IDF allocate a transient 64-byte DMA buffer, freed in the same call.

The design is in `docs/superpowers/specs/2026-10-11-berean-o-5-design.md`.

## SDK fork commit and gitlink

- **The fork:** `victorstein/freeink-sdk` branch `berean`, fast-forwarded `67f7e01..@SDK_SHORT@`. The full SHA is `@SDK_SHA@`, `perf: clock the SDMMC card at 40 MHz high speed`. Nothing goes to Free-Ink.
- **This PR:** it bumps the `freeink-sdk` gitlink to that commit. `.gitmodules` is unchanged.

**For existing clones:** run `git submodule sync freeink-sdk && git submodule update --init freeink-sdk` once.

## Verification

- **Builds:** `pio run -e x4pro` and `pio run -e x4pro-gh_release` both succeed.
- **The compile-time tests:** `static_assert`s next to the schedule check six things:
  - there are 4 attempts;
  - the first is 40 MHz;
  - the last is 20 MHz;
  - none is above high speed;
  - a 40 MHz attempt never accepts a partial init;
  - a 20 MHz attempt still does.

  They were run red first. With today's values in place, only "the first attempt asks for high speed" and "a high-speed attempt mounts only on a clean init" failed. Then they went green.
- **Host tests:** @HOST_TESTS@
- **Format:** `./bin/clang-format-fix` over the whole tree leaves no diff. It does not reach SDK files, which were formatted by hand to the file's style.
- **Not tested on a device:** the 20 MHz fallback, the per-attempt reset and the partial-init marker. No card that triggers them on demand was available. They are verified by reading the code, the compile-time checks and the log format.

## Device checks for the owner

@COMPARISON@

Flash an `x4pro` dev build of this branch (`@BUILD_SHA@`), by cable or by the SD side-load in `CLAUDE.md` "Flashing". Start a capture that survives resets, **before** the device boots:

    while :; do cat /dev/cu.usbmodem* 2>/dev/null; sleep 0.1; done >> serial.log

1. **Boot.** Find the one `[SD] SDMMC clock` line.
   - **Pass:** `SDMMC clock 40000 kHz (requested 40000 kHz, attempt 1/4)`.
   - **Pass, to note:** `attempt 2/4, after <error>` at 40000 kHz. Record the error.
   - **Report:** any `20000` or `400` line, or any `partial init`, with its `after` reason if it has one.
   - **Capture gap, not a result:** neither the clock line nor `[SD] SDMMC card mounted` is in the log. With the loop running, reboot and look again.
2. **Open the Bible.** Compare `[BMC] Loaded cache data` (against `[EBP] Loading ePub`) with the baseline's 276 ms, and `[READER] Book loaded in` with 505 ms. Expect a small drop, about 30 ms at most: the spine block is about 159 KB, and the rest is per-entry overhead.
3. **Go to a chapter not yet built.** Time the span from `[ERS] Cache not found, building...` through `[SCT] Streamed temp HTML to …` to `[EHP] Time to parse and build pages`. Compare it with 1.2–1.8 s, and note the stream part on its own, which is the most SD-bound share.
4. **Open Bible search, first after boot.** Time the span from the `Wait complete:  8179_DRF` line that ends the Opening frame to `[BSEARCH] 1189 verse documents`. Compare it with 2.2 s; a later reopen compares with ~500 ms.
5. **Sleep and wake, a no-regression check.** `[SLP] Study pick … ms` (baseline 445 ms) and `[HOMEV] Verse scan … ms` (405 ms) should not get worse. Both read about 44 KB, so they move by about 10 ms at most. Once R22 lands, a same-day wake logs no `Verse scan`.
6. **A day of use.** Copy `/.berean/` off the card first: nothing lowers the clock after mount. The log must show:
   - no `[ERR]` line;
   - no `[SD]` line other than the clock and mount lines;
   - no `[SCT] Retrying stream`.

   If any appears, reflash the previous build and restore `/.berean/` if needed.
7. **An older card**, if one is at hand: record its clock line, including any `after` or `partial init` reason, and whether it boots.

## For the orchestrator

No lines for `test/CMakeLists.txt` or `src/main.cpp`. This change touches neither.

@SESSION_LINK@

Refs berean-o-5
EOF

cat > "$SCRATCH/comparison-baseline.md" <<'EOF'
**What to compare against:** `git log 478cf1ed..origin/main -- src lib` was empty when this branch was rebased. So this build differs from the 2026-10-11 baseline (`1ba1cd34`, `docs/superpowers/notes/responsiveness-baseline.md`) only by the SD clock. Compare against the baseline figures below.
EOF

cat > "$SCRATCH/comparison-ab.md" <<'EOF'
**What to compare against:** `main` gained code under `src/` or `lib/` after the baseline. So compare two builds of this branch, and treat the baseline only as context:
1. **A:** `git -C freeink-sdk checkout 67f7e012`, then `pio run -e x4pro` and flash. Run the steps below.
2. **B:** `git submodule update freeink-sdk`, then `pio run -e x4pro` and flash. Run them again.

Each figure is compared B against A.
EOF
```

Then fill the tokens. Set the four values first, each prefixed to the same call:
- `CASE` is `baseline` or `ab`, from Task 9 Step 2.
- `HOST_TESTS` is the host-test bullet's text.
  - If Task 5 Step 2 passed, it is exactly the text in this block:

    ```text
    they pass. No host code changed, and no host test reaches `freeink-sdk/`.
    ```

  - If Task 5 recorded a pre-existing failure, name the failing tests and say they also fail on
    `main`.
- `SESSION_LINK` is this session's own attribution URL for PR descriptions.
- `SCRATCH` is the session scratchpad, as before.

```bash
: "${SCRATCH:?set SCRATCH to the session scratchpad}"
: "${CASE:?set CASE to baseline or ab}"
: "${HOST_TESTS:?set HOST_TESTS to the host-test bullet text}"
: "${SESSION_LINK:?set SESSION_LINK to the attribution URL of this session}"
python3 -I - "$SCRATCH" "$CASE" "$(git -C freeink-sdk rev-parse HEAD)" "$(git rev-parse --short HEAD)" \
  "$HOST_TESTS" "$SESSION_LINK" <<'EOF'
import pathlib, sys
scratch, case, sdk_sha, build_sha, host_tests, session_link = sys.argv[1:7]
assert case in ("baseline", "ab"), case
d = pathlib.Path(scratch)
body = (d / "pr-body.template.md").read_text()
comparison = (d / ("comparison-baseline.md" if case == "baseline" else "comparison-ab.md")).read_text().rstrip("\n")
for token, value in (("@SDK_SHA@", sdk_sha), ("@SDK_SHORT@", sdk_sha[:7]), ("@BUILD_SHA@", build_sha),
                     ("@COMPARISON@", comparison), ("@HOST_TESTS@", host_tests),
                     ("@SESSION_LINK@", session_link)):
    assert body.count(token) == 1, token
    body = body.replace(token, value)
assert "@" not in body.replace("https://", ""), "an unfilled token remains"
(d / "pr-body.md").write_text(body)
EOF
grep -n "67f7e01\.\.\|What to compare against\|dev build of this branch\|Host tests:\|claude.ai/code" "$SCRATCH/pr-body.md"
```

Expected: five lines:
1. the real short SDK SHA after `67f7e01..`;
2. the chosen comparison paragraph;
3. the bereanOS short SHA;
4. the host-test bullet;
5. this session's link.

- [ ] **Step 3: Open the PR**

Run:

```bash
: "${SCRATCH:?set SCRATCH to the session scratchpad}"
gh pr create --repo victorstein/berean-os --base main --head perf/r2-sd-40mhz \
  --title "perf: run the SD card at 40 MHz high speed" --body-file "$SCRATCH/pr-body.md"
```

Expected: the PR URL. The title is conventional, lowercase and imperative, and targets `main`.

- [ ] **Step 4: Watch CI, then confirm it from two sources**

`gh pr checks` collapses check runs that share a name. A stale FAILURE beside a later SUCCESS
shows only green, and that has already passed a failed PR on this codebase
(`docs/contributing/development-workflow.md:121-132`). So the watch is followed by the two
commands that doc gives.

Run:

```bash
PR=$(gh pr view perf/r2-sd-40mhz --repo victorstein/berean-os --json number -q .number)
gh pr checks "$PR" --repo victorstein/berean-os --watch
gh pr view "$PR" --repo victorstein/berean-os --json statusCheckRollup
gh run list --repo victorstein/berean-os --commit "$(gh pr view "$PR" --repo victorstein/berean-os --json headRefOid -q .headRefOid)"
```

Expected:
- `statusCheckRollup` lists every check with `"conclusion": "SUCCESS"`;
- `gh run list` shows every run for the head commit `completed` / `success`.

In particular, the build job's recursive submodule checkout (`ci.yml:46`) fetches the new SDK SHA
from the fork.
- **"No checks reported", or a run still `in_progress`,** means not started or not finished, not
  passed. Wait and run the last two commands again.
- **If a check fails,** read its log, fix the cause, push, and repeat this step.

---

## Self-review against the spec

| Spec item | Where |
|---|---|
| A1, high-speed request; ESP-IDF negotiates down | Task 3 Step 3 (schedule) |
| A2, probing clock and 1-bit bus per attempt, before the GPIO5 pulse | Task 2 Step 1 |
| A3, the four-entry schedule, `kMountAttempts` with the `sizeof` idiom | Task 1 Step 1, Task 3 Step 3 |
| A4, no dependence on the card leaving HS | needs no code; the 20 MHz entries take ESP-IDF's early return |
| A5, clean init only at 40 MHz, today's rule at 20 MHz | Task 1 Step 4 (gate), Task 3 Step 3 (predicate) |
| A6, the clock line, its fields and its suffixes; the real-divider read | Task 2 Steps 2–3 |
| A7, the failure line and `SDMMC card mounted` untouched | no step edits them; Task 5 Step 4 confirms the diff |
| A8, the comments and the doc | Task 4 |
| A9, one SDK commit, owner-approved fast-forward | Tasks 6–8 |
| A10, the gitlink-only bereanOS change; rebase; the `src lib` re-check | Task 0 Step 2, Task 9 |
| A11, the six `static_assert`s, red on values then green | Task 3 |
| A12, no host test; the host suite run once | Task 5 Step 2 |
| §7.2, both builds and the formatter | Tasks 1–5, Task 9 Step 3 |
| §7.3–7.4, the device checks and expectations, with the A/B rule | Task 10 Step 2 |
| D1 | Task 7 |

## Review log

- **Pass 0 of the plan review:** `docs/superpowers/reviews/berean-o-5-plan-review-0.md`, CLEAR
  with six minors, all fixed inline:
  1. **Builds broken by siblings.** A rule says sibling worktrees share `~/.platformio/packages`.
     Task 3 Step 2 now treats a `FAILED` with no assertion line as environmental: re-run, then
     read the unfiltered tail.
  2. **`$SCRATCH` lost between calls.** Every block that uses `$SCRATCH` starts with a `:?` guard,
     and the `mktemp -d` fallback is gone. Task 6's write, reset and commit are chained with `&&`,
     and the recovery is stated. Both shells were checked: an unset or unwritable `SCRATCH` stops
     before the reset.
  3. **CI watch.** Task 10 Step 4 adds `statusCheckRollup` and `gh run list` for the head commit,
     per `docs/contributing/development-workflow.md:121-132`. "No checks reported" means wait.
  4. **Task 2 Step 1.** It now says it doesn't compile alone (the `esp_err_t e` redeclaration), and
     to build only at Step 4.
  5. **Rebase conflicts.** A rule, plus Task 0 Step 2 and Task 9 Step 2, say to abort and report.
     A `freeink-sdk` conflict needs a fresh decision.
  6. **PR-body text.**
     - The host-test bullet and the session link are now tokens (`@HOST_TESTS@`,
       `@SESSION_LINK@`), and the Task 9 trailer uses `SESSION_LINK` too.
     - Step 4's marker drops "(~1507 ms)", because #255 made that frame a FAST refresh.
     - The two "after reason" clauses are restored in device steps 1 and 7.
     - The PR-body step was re-run end to end in bash and zsh.

