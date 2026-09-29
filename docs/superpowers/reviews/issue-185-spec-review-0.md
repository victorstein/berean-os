Tier: heavy

# Issue #185 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-29-issue-185-design.md` against issue #185 (`gh issue view 185`),
`docs/superpowers/research/2026-09-29-issue-185-research.md`, the tree at `39d29ba7`, the
`freeink-sdk` submodule at `310ec61`, and the installed PlatformIO packages.

D1 and D2 are taken as settled. The findings below are about how the spec applies them.

## What holds

I checked these claims and found them correct:

- All the `HalGPIO.cpp` line citations: `:137-139`, `:158-160`, `:293-298`, `:334-351`, `:353-373`.
- The `main.cpp` citations: `:370`, `:375`, `:380-393`, `:436-464`, `:791-795`, `:822`.
- `HalDisplay.cpp:9`, `:29-32`, `BaseTheme.cpp:74/:370` and `LyraTheme.cpp:27`.
- `BatteryMonitor.cpp:194-201`, and `FreeInkDisplay.cpp:191-198`: the constructor's `EPD_CS 21` really is unused on the S3.
- `usbDetect` has no consumer outside `HalGPIO.cpp:347/:350`. `grep -rn usbDetect` over the whole submodule finds only `BoardConfig.h` and one line of the M5PaperS3 docs.
- `ACTIVE` defaults to `XTEINK_X4_PRO` under `FREEINK_DEVICE_X4PRO` (`BoardConfig.h:1519-1520`, `:1532`), so `isX4Pro()` is already true when `gpio.begin()` runs.
- `ESP_RST_USB` and `ESP_RST_JTAG` exist (`esp_system.h:36-37`), and `INPUT_PULLDOWN` exists (`esp32-hal-gpio.h:53`).
- `LOG_DBG` is live at `LOG_LEVEL=2` (`Logging.h:57-58`, `platformio.ini:174`).
- `git ls-remote https://github.com/victorstein/freeink-sdk.git` returns `310ec615… refs/heads/berean`. The fork is `PUBLIC`, `isFork=true`, and its parent is Free-Ink.
- The sleep path holds only named rail pins (`PowerManager.cpp:61-69`, `:93-96`). It never touches GPIO21, so configuring the pin once in `begin()` survives every wake.
- The §4.4 carve-out gives exactly the §4.5 table. The rows keep their order, and `usbConnected && usbCanWakeFromOff` in both `PowerOn` rows is logically sound.
- `AfterFlash` and `Other` take the same boot path and the same resync (`main.cpp:457-461`, `HalDisplay.cpp:30-31`). A8 therefore holds.

## Findings

### MAJOR 1: the serial-log verification cannot be observed in most of the states it names

**Claim.** §6.1 A9 says the per-edge `LOG_DBG` is how the issue's "logs the detect value on a USB edge" is met. A10 says the boot `LOG_INF` "is how §6.3 reads the classification on the device". §6.3 steps 1, 2, 4, 5 and 6 each expect a serial line.

**Problem.** The X4 Pro's only serial link is the native USB port whose power this change detects.

- Step 2 expects `USB disconnected` to be logged once. That line is emitted after the cable is out, so there is no transport to carry it.
- Step 4 expects "no spurious edge lines" during five minutes on battery. There is no serial on battery.
- Step 1's `USB connected` fires about 300 ms after VBUS rises, before the host has re-enumerated the port and the monitor has reopened it.
- Steps 5 and 6 read a line logged at `main.cpp:375`, a few hundred ms after a cold boot or a deep-sleep wake. The port has just (re)appeared at that point.

**Evidence.**

- `logPrintf` drops output when no host holds the port: `if (logSerial) { logSerial.print(buf); }` (`lib/Logging/Logging.cpp`, the non-ROM-printf branch).
- `scripts/debugging_monitor.py:461-466` opens the port once and never reconnects. A `SerialException` returns.
- The bench already hit this. The review log says the probe held its SD write until `Storage.ready()` and appended the `BOOT reset= cause=` line to the SD file (spec lines 405-406). Every §7 result was read from `/usb-probe.txt` over File Transfer, not from serial.
- A10's line is also emitted twice per boot, because `getWakeupReason()` is called at both `main.cpp:375` and `HalDisplay.cpp:29`.

**Fix.** Rewrite §6.3 so that each step is judged by what can actually be observed.

- Steps 1, 2 and 3: the bolt appears and clears. That is the acceptance criterion.
- Step 4: no bolt flicker, by eye.
- Steps 5 and 6: the device wakes or boots and stays up. It does not drop back into sleep.

Keep A9 and A10 as best-effort development aids. Plug-edge lines are visible only when a monitor is attached after enumeration; unplug and on-battery lines are never visible. If the owner wants a record of an edge, the probe's SD-append pattern is the proven route, but it is not needed to meet the acceptance criteria. Also drop "within about a third of a second" from step 2; MINOR 5 covers why.

### MAJOR 2: after the repoint, `bin/bootstrap` and every pipeline worktree still fetch from Free-Ink

**Claim.** In the §4.6 consumer table, the `bin/bootstrap` row says it "fetches the gitlink SHA from the fork". The existing-clones row puts the needed `git submodule sync` in a PR-description note.

**Problem.** `git submodule update --init` does not overwrite a `submodule.<name>.url` that is already set in the repo config. Worktrees share one config: the primary checkout's `.git/config`, which holds the Free-Ink URL.

After merge, every new herdr worktree runs `./bin/bootstrap`, as the project memory "Fresh worktree build bootstrap" instructs. Each one would then try to fetch the fork-only `berean` commit from `Free-Ink/freeink-sdk`, which has no ref for it. The fresh-clone check in §4.6 cannot catch this, because a fresh clone has no stale config. Only clones that already exist fail, and every pipeline worktree is one of those.

Whether GitHub serves a fork-only SHA through the parent's URL is undocumented behaviour. At best it works by accident.

There is a second hazard: `git submodule sync` from a worktree still based on old `main` writes the Free-Ink URL back into the shared config.

**Evidence.**

- `git rev-parse --git-common-dir` gives `/Volumes/stein/Documents/development/personal/berean-os/.git`.
- `git config --show-origin --get submodule.freeink-sdk.url` gives `file:/Volumes/stein/Documents/development/personal/berean-os/.git/config https://github.com/Free-Ink/freeink-sdk.git`.
- `bin/bootstrap:21` is only `git submodule update --init --recursive --depth 1`, with no sync.
- `git -C freeink-sdk config --get remote.origin.url` gives the Free-Ink URL.

**Fix.** Add `git submodule sync --recursive` immediately before the `update` at `bin/bootstrap:21`. This is safe in both directions:

- The fork's history contains every upstream commit up to and past `310ec61` (fork `main` is `a1cf5b4`), so the fork URL also serves old gitlinks.
- Each worktree syncs to its own `.gitmodules` on each bootstrap, so a sync from an old-base worktree cannot leave a new one broken.

Correct the bootstrap row of the table to match, and keep the PR-description note for clones that are not bootstrapped.

### MAJOR 3: A2's reason for keeping the pull mode out of the profile is factually wrong

**Claim.** A2 says that adding a pull-mode field to `BoardProfile` "would change the struct every profile initialises positionally, which is a far wider SDK change than this issue needs".

**Problem.** The struct's own convention is to append fields with defaults, precisely so that existing profiles do not change. The labelled assumption rests on a false premise. The spec then encodes the pull mode twice: once in a profile comment ("read it with INPUT_PULLDOWN") and once as code keyed on `isX4Pro()`. That is the kind of split source of truth the owner rejected for the pin in D2.

**Evidence.** In `BoardConfig.h:654-675`, four trailing members carry defaults, each with a comment such as "Defaulted so existing profiles need no change" (`mic`, `sensors`, `uiScale`, `power`, `displayControllerVariant`, `viewableInsets`). `XTEINK_X4_PRO` sets all of them explicitly up to `{9, 7, 3, 7}` (`:1474`), so a new trailing default would touch only that one profile, and only if it opted in.

**Fix.** No owner decision is needed. Keep the HAL placement, but state the real reasons:

- The debounce and the wake exemption are X4 Pro HAL behaviour keyed on `isX4Pro()` anyway, so the pull mode sits beside them.
- Keeping the fork's diff to one data value plus docs keeps future upstream merges into `berean` trivial. §3 already makes that concern out of scope.

Trim the profile comment to what the SDK owns: pin, polarity and bench provenance. Say "the HAL reads it with a pull-down", so the comment does not read as a policy the SDK enforces.

### MINOR 1: the verification never establishes the fact A6 hedges on

**Claim.** A6 says that whether a USB plug alone cold-boots a fully-off X4 Pro "is not established", and that if it does, the boot runs as `PowerButton`, which is not a new misclassification.

**Problem.** The last acceptance criterion asks that `getWakeupReason()` behaviour "is checked, so a USB-plug boot is no longer misclassified". §6.3 has no plug-without-press step, so the spec never finds out whether the case exists. §4.4 also says a press "can't be told apart from a plug". That is true of reset reason and wake cause, but it leaves out the Power GPIO level at boot, which is the one other discriminator.

**Evidence.** §6.3 steps 1-7 contain no plug-into-fully-off case. `verifyPowerButtonWakeup` short-circuits on the X4 Pro (`HalGPIO.cpp:296`), which suggests a short press can be released before it is sampled.

**Fix.** Add a §6.3 step: from fully off, plug USB without pressing anything, and record whether the device boots. Add one sentence to A6 rejecting the button-level discriminator: a quick press can be released before `begin()` samples GPIO3, and a pressed boot misread as a plug would send a deliberate press back to sleep.

### MINOR 2: the SDK push step runs before the sync that retargets `origin`

**Claim.** A11 makes the SDK commit and pushes it "to the fork's `berean` only". A12's `git submodule sync` comes afterwards.

**Problem.** When A11 runs, the submodule's `origin` is still Free-Ink. The step fails closed today: `gh api repos/Free-Ink/freeink-sdk --jq .permissions` shows `"push":false`. Even so, "never pushed to Free-Ink" is a hard owner constraint, and it should not depend on missing permissions.

**Fix.** Give the literal command: `git -C freeink-sdk push https://github.com/victorstein/freeink-sdk.git HEAD:refs/heads/berean`. Alternatively, order the `.gitmodules` edit and `git submodule sync` first, then assert that `git -C freeink-sdk remote get-url origin` is the fork URL before pushing.

### MINOR 3: `begin()` calls `pinMode` without the `usbDetect >= 0` guard

**Claim.** §4.3: `begin()` (X4 Pro) runs `pinMode(BoardConfig::ACTIVE.usbDetect, INPUT_PULLDOWN)`. §4.2 puts the X4 Pro branch of `isUsbConnected()` before the `usbDetect < 0` guard.

**Problem.** Both paths assume the profile value is set. A future profile edit, or a merged upstream `berean` that resets it, would call `pinMode(-1)` and `digitalRead(-1)`.

**Fix.** Condition the X4 Pro set-up in `begin()` and in `update()`, and the X4 Pro branch in `isUsbConnected()`, on `BoardConfig::isX4Pro() && BoardConfig::ACTIVE.usbDetect >= 0`. Otherwise fall through to the existing `return false`.

### MINOR 4: the pin-free argument misses the doc's earlier "INT=21" note

**Claim.** §4.1 says GPIO21 "appears nowhere else in the X4 Pro profile" and is free.

**Problem.** That is true of the profile. But the findings doc that A11 edits says, at `xteink-x4pro-support.md:149`, "Prior notes had INT=21 (app0), then INT=4/RST=10 — both wrong". The bench settles that GPIO21 is free, but a reader of the corrected doc will see GPIO21 named twice with two roles.

**Fix.** Add a clause to A11 item 2: the app0 "INT=21" reading at `:149` was GPIO21 seen as an input, now identified as VBUS sense.

### MINOR 5: the latency and timing claims ignore the idle delay and the e-ink refresh

**Claim.** §4.3 says "Latency is at most one extra loop (~10 ms)". §6.3 step 2 says the bolt "clears within about a third of a second".

**Problem.** Once the device has been idle, the loop delays 50 ms (`main.cpp:816-819`), not 10 ms. The redraw that clears the bolt is an e-ink refresh on top of that: FAST is about 500 ms and HALF about 1720 ms (`hal-dev.md`, "Where things live").

**Fix.** Say "one extra loop (10-50 ms)", and in step 2 say "the bolt clears on the next refresh after the ~300 ms edge".

### MINOR 6: churn leftovers from the draft

**Problem.**

- §7 says "Classification per the draft §7.3: **S1**". The S1-S4 shapes and §7.3 were removed, as the review log records.
- §7 says "Test 3 (power bank) was not run". §1 and A6 cite "§7, test 4". But the §7 table rows are not numbered, so these references do not resolve.
- §4.1 cites `HalGPIO.h:10` for `EPD_CS 21`. It is at `HalGPIO.h:11`; `:10` is `EPD_MOSI`.

**Fix.** Number the §7 table rows as tests 1-6, with the power bank as the unrun test 3 and the Power press from fully off as test 4. Replace the "draft §7.3: S1" sentence with its meaning: "one pin, active HIGH, on both cable and charger". Fix the citation to `:11`.

### MINOR 7: `hal-dev.md` still describes the SDK as upstream-only

**Problem.** `.claude/agents/hal-dev.md:28-30` says "freeink-sdk is a submodule … If a change genuinely belongs upstream, **stop and ask.**" After D2, SDK corrections have a sanctioned home on the fork's `berean`. The next hal-dev agent will escalate a question the owner has already answered.

**Fix.** Add a line to §4.6 updating that paragraph: SDK corrections go to `victorstein/freeink-sdk` on `berean`, pushed only with owner approval. `hal-dev.md` is not one of the shared append points (`hal-dev.md:21-26`), so it can be edited in this PR.

## Summary

The core design is sound and faithful to D1 and D2:

- GPIO21 is read through the profile.
- A pull-down is applied once in `begin()`.
- A header-only two-sample filter is fed from `update()`.
- The wake table is extracted into a pure host-tested classifier, with a single X4 Pro carve-out that keeps today's `POWERON` → `PowerButton`.

I found nothing that reverses a decision, changes scope or needs the owner's judgement. The three MAJORs can be fixed inline:

1. A verification plan that relies on serial lines the hardware cannot deliver.
2. A bootstrap and worktree seam that breaks every existing checkout after the repoint.
3. A false premise under A2.

VERDICT: CLEAR
