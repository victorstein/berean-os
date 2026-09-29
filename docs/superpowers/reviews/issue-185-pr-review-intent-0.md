Tier: heavy

# PR #190 intent review, pass 0 (issue #185)

This pass reviews PR #190 (`fix/185-x4pro-charging-indicator`, 5 code commits `16ee12aa..e7bb98d6`
on top of the spec and plan commits) against issue #185 and the spec
`docs/superpowers/specs/2026-09-29-issue-185-design.md`. It also checks for divergence from the plan
`docs/superpowers/plans/2026-09-29-issue-185-plan.md`. The SDK side was read with
`git -C freeink-sdk show 67f7e012`.

The owner's decisions are settled and are not re-litigated here: D1 (GPIO21, active HIGH, pull-down,
debounce, and the Power-press wake result) and D2 (the `victorstein/freeink-sdk` fork on `berean`,
with `.gitmodules` repointed and `usbDetect = 21` in the profile).

## Summary

The PR does what the issue asks, on the owner-confirmed signal only, and the spec's harder half is
implemented, not just its easy half:

- the wake-classification carve-out;
- the bootstrap sync;
- the stale-doc and GT911 corrections in the SDK.

The code matches the plan's "new text" blocks line for line. There is no silent scope reduction and
no scope expansion. CI is green on every job (`gh pr checks 190`), including the `x4pro` build, which
proves the new gitlink resolves through the fork. There are no BLOCKERs and no MAJORs.

## Acceptance criteria (issue #185)

| Criterion | Status | Evidence |
|---|---|---|
| `isUsbConnected()` returns true on X4 Pro when USB power is present, from the owner-confirmed signal | Met | Profile `usbDetect = 21` (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1401`, SDK commit `67f7e012`). X4 Pro branch returns the filtered level (`lib/hal/HalGPIO.cpp:362-364`). Pin set up with `INPUT_PULLDOWN` in `begin()` (`lib/hal/HalGPIO.cpp:143-148`). Guard is `isX4Pro() && usbDetect >= 0` (`lib/hal/HalGPIO.cpp:373`). |
| Bolt appears and clears on plug and unplug | Met in code; device check pending | `update()` feeds the filter before the unchanged edge latch (`lib/hal/HalGPIO.cpp:167-175`). That latch drives `main.cpp:793` `requestUpdate()`. The themes re-read `isUsbConnected()` (`BaseTheme.cpp:74`, `:370`; `LyraTheme.cpp:27`) and now get the same filtered value (spec A5). Device steps 1-4 are in the PR body. |
| Stale "GPIO10 is a candidate" comment and "not conclusively identified" doc note corrected | Met | SDK commit `67f7e012` replaces the `BoardConfig.h` line and the doc bullet. It also does the two extras the spec asked for: the reversed GT911 INT/RST line in `docs/xteink-x4pro-support.md` and the old "INT=21" note. It amends the "battery/VBUS pins" PENDING header too (spec A11 item 1). |
| `getWakeupReason()` checked with real USB detection; a USB-plug boot is no longer misclassified or newly misclassified | Met as the spec scopes it (see MINOR 1) | Table moved into `input::classifyWakeup` (`lib/Input/Input/WakeupClassifier.h:14-29`). Row order is preserved. The X4 Pro passes `usbCanWakeFromOff = false` (`lib/hal/HalGPIO.cpp:423-424`), so a bench-observed `POWERON` Power press with USB attached stays `PowerButton` and is not sent to sleep by `main.cpp:446-455`. |

The issue's constraint, "do not guess the pin", is honoured. The probe, the owner's bench results
(spec §7) and D1 all come before the implementation commits, and the only pin used is the one that
was confirmed.

## Spec requirements

Every spec assumption with a code or repo consequence is implemented as written:

- **A1 / A11 — the pin and the SDK corrections.** `usbDetect = 21` is the single source of truth.
  There is no HAL pin constant (`grep -rn usbDetect src lib` finds only `HalGPIO.cpp:146,365,371,373`).
  The comment matches spec §4.1. The SDK commit has no assistant trailer, as §4.6 requires.
- **A2 / A3 — how the X4 Pro reads it.** The pull-down is in `HalGPIO`, keyed on `isX4Pro()`. It is
  configured once in `begin()`, which runs before the first `getWakeupReason()` (`main.cpp:370`,
  then `:375`).
- **A4 — the debounce.** `StableLevel` is header-only in `lib/Input/Input/StableLevel.h`, with exactly
  the specified interface. The X4 Pro path cannot call `pinMode(-1)`.
- **A7 — the classifier.** The ESP-IDF reads stay in `HalGPIO`, and the enum mapping is as specified
  (`lib/hal/HalGPIO.cpp:377-414`). The public signature and both callers are untouched
  (`main.cpp:375`, `HalDisplay.cpp:29`).
- **A9 / A10 — logging.** The edge `LOG_DBG` is inside the latch's `if` (`HalGPIO.cpp:172-174`), and
  the boot `LOG_INF` is in `getWakeupReason()` (`HalGPIO.cpp:425-426`).
- **A12 — the submodule repoint.** `.gitmodules` now reads `url = https://github.com/victorstein/freeink-sdk.git`
  and `branch = berean`, and the gitlink moves `310ec615 -> 67f7e012`.
- **A13 — bootstrap.** `bin/bootstrap` runs `git submodule sync --recursive` before `update`.
- **A14 — agent docs.** The `hal-dev.md` sentence matches the plan-review wording.

Non-goals (spec §3) are respected. Other boards' paths are byte-for-byte the old logic:

- The generic read is only extracted into `readUsbDetectPin()`.
- `classifyWakeup` with `usbCanWakeFromOff = true` reduces to the old table.
- There is no charge-complete state, no gauge inference and no JTAG fallback.
- There is no change to `main.cpp` boot policy.
- `test/CMakeLists.txt` is not in the diff. Its two lines are in the PR body for the orchestrator,
  per `hal-dev.md` "Shared files".

## Tests

The tests exercise behaviour rather than restating the implementation:

- **`StableLevelTest`** feeds sample sequences and asserts the observable level. That covers glitch
  rejection, alternation, and the two bench edge sequences from spec §7.
- **`WakeupClassifierTest`** pins each row of the spec §4.5 table, with `usbCanWakeFromOff` both ways
  where it matters. It includes the X4 Pro regression row, `PowerOnWithUsbIsPowerButtonOnX4Pro`.

The known gap is spec-acknowledged (§6.2), and the PR does not hide it. The enum mapping and
`isUsbConnected()` have no host test, because no host target compiles `HalGPIO.cpp`. They are left
to device steps 1-8.

## Plan divergence

None that affects the result:

- Tasks 1-7 produce the plan's exact file contents and commit messages.
- The worker steps W.1-W.4 are reported as done in the PR body: the fast-forward push
  `310ec61..67f7e01`, the fresh-clone check, the stale-config check, and restoring
  `test/CMakeLists.txt`.
- The PR body carries every item W.4 requires, and `Closes #185` is its last line.

## Findings

### MINOR 1 — the "no longer misclassified" half of the wake criterion rests on a device step

Issue #185's fourth criterion asks that a USB-plug boot be "no longer misclassified or newly
misclassified".

The PR makes sure nothing is *newly* misclassified. The X4 Pro keeps `POWERON -> PowerButton` whatever
the USB state (`WakeupClassifier.h:20-21`, `HalGPIO.cpp:424`). That is the same classification as
today, and on this board `verifyPowerButtonWakeup` returns true unconditionally (`HalGPIO.cpp:311-313`).

If plugging USB into a fully-off X4 Pro does cold-boot it, though, that boot is still classified
`PowerButton` rather than `AfterUSBPower`. It therefore stays up rather than going back to sleep.
Spec A6 argues that this is unavoidable, because a Power press and a plug produce the same
reset/cause pair (bench test 4), and that it is the owner-sanctioned choice under D1. Whether the
case happens at all is left to device check 7 in the PR body.

This is a correct, documented scoping, not a hidden reduction. When check 7 runs, the owner should
record its result on the issue, so the criterion is closed on evidence rather than by assumption. No
code change is needed.

VERDICT: CLEAR
