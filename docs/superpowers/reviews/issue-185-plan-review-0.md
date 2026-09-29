Tier: heavy

# Issue #185 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-29-issue-185-plan.md`.
Spec: `docs/superpowers/specs/2026-09-29-issue-185-design.md`.

## What I checked, and what held

- **Spec to step coverage.** A1 and A11 map to step 5.2. A2, A3 and A4 map to 1.2-1.3 and 4.1-4.2. A4's
  feed maps to 4.3, the `isUsbConnected()` order in §4.2 to 4.4, and A5 needs no change. A6 and A7
  map to 2.1-2.2 and 3.1. A8 is covered by 2.1's `OtherResetIsOtherEvenWithUsb`. A9 maps to 4.3,
  A10 to 3.1, A12 to 6.1, A13 to 6.2 and A14 to 7.1. §4.6's verification maps to 7.3, §6.2 to Tasks
  1-2, §6.3's build to 7.2, and §6.3's device steps go in the PR body (7.4). Goal 4's `:149`, `:259`
  and `:277-279` doc corrections and the `BoardConfig.h:1358-1361` header amendment are all in 5.2.
  Nothing in the spec is unmapped.
- **Every "old text" block matches the tree**, character for character. I checked them all:
  - `HalGPIO.cpp:137-141`, `:158-161`, `:347-351` and `:353-373`.
  - `HalGPIO.h:48-49` and `:65-66`.
  - `bin/bootstrap:21-22`. The plan says `:20-21`, but the quoted text is right.
  - `hal-dev.md` "freeink-sdk is a submodule".
  - `BoardConfig.h:1361-1362` and `:1400`.
  - `xteink-x4pro-support.md:149-150`, `:259-260` and `:277-279`.
  - The `#rtc--usb--battery` anchor exists (heading at `xteink-x4pro-support.md:262`).
- **Types and names stay consistent across steps.** These keep the same spelling and signature from
  2.2 through 3.1, 4.1 and 4.4:
  - `input::StableLevel::{begin,update,stable}`
  - `input::{ResetKind,WakeCause,WakeupClass}` and
    `classifyWakeup(ResetKind, WakeCause, bool, bool)`
  - `usbPresence`, `readUsbDetectPin() const` and `x4ProUsbDetect() const`

  `isUsbConnected() const` only calls `usbPresence.stable() const`. The mutating `update()` is
  called only from non-const `begin()` and `update()`.
- **The tests are right against the implementation.** I traced all six `StableLevel` cases through
  1.3's `update()`, and each asserts what the spec's §6.2 bullets require. I traced all eight
  `classifyWakeup` cases through 2.2. With `usbCanWakeFromOff = true` the body is identical to
  `HalGPIO.cpp:359-372`, row for row, so other boards are unchanged. The only other difference is
  the A6 carve-out.
- **The ESP-IDF API is what the plan assumes.** Checked in the installed framework:
  - `esp_reset_reason_t esp_reset_reason(void)` is declared at
    `framework-arduinoespressif32-libs/esp32s3/include/esp_system/include/esp_system.h:85`.
  - `esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void)` is declared at
    `.../esp_hw_support/include/esp_sleep.h:705`, and is not deprecated.

  `toResetKind` and `toWakeCause` have `default:`. `toWakeupReason` covers every enumerator, so
  `-Wswitch` stays quiet.
- **Call order.** `gpio.begin()` runs before both `getWakeupReason()` callers (`main.cpp:370`, then
  `:375`). `HalDisplay.cpp:29` is reached later, from `setupDisplayAndFonts`. So the seed in 4.2
  precedes every read. `usbDetect` has no other consumer: `grep -rn usbDetect freeink-sdk/libs src
  lib`, outside `BoardConfig.h`, finds only `HalGPIO.cpp:347`, `:350`.
- **Every commit leaves the tree committable.**
  - Tasks 1-2 add unregistered test directories, which don't affect the build.
  - Task 4 is inert until `usbDetect >= 0`.
  - Task 5 leaves the gitlink unstaged until Task 6.
- **Preconditions hold today.** `git ls-remote` of the fork's `berean`, over both SSH and HTTPS,
  returns `310ec61506fc915836db7799a2e7f4fc135a570d`. `git submodule status` shows the same SHA.
  `git rev-parse --abbrev-ref @{u}` is `origin/fix/185-x4pro-charging-indicator`, so the bare
  `git push` in 7.3 and 7.4 has an upstream.
- **FILES lines** are at column 0, outside fences, and repo-relative. They name no globs. They cover
  every committed path: the two headers, both test directories, `HalGPIO.{h,cpp}`, `freeink-sdk/`,
  `.gitmodules`, `bin/bootstrap` and `.claude/agents/hal-dev.md`. On `test/CMakeLists.txt`, see
  MINOR 1.

## Findings

### MAJOR 1: the plan tells the implementer subagent to push and open the PR, which its brief forbids

**Claim.** Steps 5.3, 7.3 and 7.4 push to two remotes and open the PR, all written as ordinary plan
steps.

**Problem.** The pipeline hands this plan verbatim to an implementer subagent whose brief says
"Commit, but do not push: pushing is the worker's, once it has verified your work"
(`stein.pipeline/prompts/implement.md`, "The implementer's brief"). The worker then runs "the tests
and the typecheck yourself", and only when they are green does it "push, and open the PR" (same
file, step 4).

An implementer that follows the plan literally breaks its brief three ways:

1. It pushes the SDK commit to the fork (5.3), which is an external, non-reversible fast-forward.
2. It pushes the bereanOS branch (7.3).
3. It opens the PR (7.4).

All three happen before the worker has verified anything. An implementer that follows its brief
instead stops at 5.3. Tasks 6-7 then run against an unpushed SDK commit, and 7.3's fresh clone
cannot resolve the gitlink.

Step 7.4 also restores `test/CMakeLists.txt` *before* the worker's own verification run. The worker
therefore re-runs the host suites without `stable_level` and `wakeup_classifier` registered, and the
two new suites silently drop out of its green.

**Evidence.**
- `2026-09-29-issue-185-plan.md:712` (`git -C freeink-sdk push ...`)
- `:841` (`git push`)
- `:873-879` (restore, push, open PR)
- the implement prompt's step 4 and its implementer's brief

**Fix (inline, no decision needed).** Split the plan at the push boundary:

- **Implementer, Tasks 1-7.2.** Step 5.3 becomes "commit in `freeink-sdk`, do not push." Step 6.3
  commits the gitlink to the local SDK commit, which builds fine because PlatformIO compiles the
  checked-out submodule. Step 7.2 runs the full local verification.
- **A new "Worker, after verification" section**, in this order:
  1. Re-run the host suites with the two `add_subdirectory` lines still appended.
  2. Push the fork: the 5.3 push and its `ls-remote` check, with the same fast-forward precondition
     as 5.1, re-checked.
  3. Push the branch.
  4. Run the fresh-clone and stale-config checks (7.3).
  5. Restore `test/CMakeLists.txt`, push, and open the PR (7.4).

The owner's push approval (spec A11) is unchanged. Only who runs the command moves.

### MINOR 1: `test/CMakeLists.txt` is edited by steps 1.1, 2.1 and 7.4 but is not on a `FILES:` line

**Claim.** Every touched file must be declared.

**Problem.** This one is edited locally, never committed, and restored before the PR. That is
deliberate, per `hal-dev.md` "Shared files", and `plan.md:27-31` says so. Declaring it would
serialise this task behind every sibling that commits an `add_subdirectory` line. The lock exists to
prevent a committed collision, and none is possible here: `planDeclaredFiles` and `filesOverlap`
(`stein.pipeline/src/lib/gating.ts:22-60`) are a scheduling heuristic, and the backstop is the
PR-level conflict check, where this file never appears. I therefore rank it MINOR, not BLOCKER.

**Fix.** Add a column-0 prose line under the FILES block. It should state that `test/CMakeLists.txt`
is deliberately undeclared, because it is edited locally and never committed, so a later review
does not read it as an omission.

### MINOR 2: the `-Wswitch` / warning checks in 3.2 and 4.5 cannot fail

**Claim.** `pio run ... 2>&1 | grep -n 'HalGPIO.cpp.*warning'` "must print nothing".

**Problem.** Each step runs `pio run` first and the grep run second. The second run is incremental,
so it doesn't recompile `HalGPIO.cpp`, and its output never contains that file's warnings. The check
passes whatever the code does.

**Evidence.** `plan.md:439-445` and `:594-595`.

**Fix.** Run the build once and capture it:
`~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee "$TMPDIR/pio-185.log"`. Then run
`grep -n 'HalGPIO.*warning' "$TMPDIR/pio-185.log"`. This also halves the build time.

### MINOR 3: half of 7.3's stale-config check is vacuous

**Claim.** After a stale `submodule.freeink-sdk.url` is set, `./bin/bootstrap` is checked by
`git config --get submodule.freeink-sdk.url` and by `git -C freeink-sdk remote get-url origin`.

**Problem.** Step 6.1's `git submodule sync --recursive` already set the submodule's `origin` to
the fork. 7.3 resets only the superproject's config key, so `remote get-url origin` prints the fork
URL whether or not `bootstrap` syncs. The first assertion does discriminate: without the new line,
`update --init` leaves the Free-Ink value. So A13 is tested, but the assertion the spec names in
§4.6 is the one that can't fail.

**Evidence.** `plan.md:745` and `:858-862`. Spec §4.6, "The stale-config case".

**Fix.** Before `./bin/bootstrap`, also run
`git -C freeink-sdk remote set-url origin https://github.com/Free-Ink/freeink-sdk.git`. Both
assertions then fail without the sync line.

### MINOR 4: the fork push precedes any build against the edited SDK

**Claim.** 5.3 pushes. The first build with `usbDetect = 21` is 6.3.

**Problem.** A typo in the 5.2 `BoardConfig.h` edit would reach the fork before any compile. The fork
is fast-forward only (spec A11), so the only repair is a second "fix the fix" commit on `berean`.

**Evidence.** `plan.md:704-718`, then `:773`.

**Fix.** Run `~/.platformio/penv/bin/pio run -e x4pro` at the end of 5.2, before the commit. After
MAJOR 1 the push moves to the worker section anyway, but keep the build before the SDK commit.

### MINOR 5: the expected red messages are GCC's, but the host compiler is AppleClang

**Evidence.** `plan.md:133-134` and `:275-276` expect "`No such file or directory`". `c++ --version`
here reports `Apple clang version 21.0.0`, which prints "`'Input/StableLevel.h' file not found`".

**Fix.** Expect "the build fails on the missing `Input/StableLevel.h` include (`file not found` or
`No such file or directory`)". Do the same for `WakeupClassifier.h`.

### MINOR 6: the fork push URL silently differs from the spec

**Evidence.** Spec A11 (`spec.md:273`) gives `https://github.com/victorstein/freeink-sdk.git`. The
plan (`plan.md:712`) uses `git@github.com:victorstein/freeink-sdk.git`. SSH is a sound choice:
`git ls-remote` over SSH works here, and `origin` is SSH. Both forms name the fork explicitly, which
is the property the spec wanted.

**Fix.** Add one sentence saying the SSH form was chosen because `origin` authenticates over SSH.
The `"do not retry against any other URL"` rule then reads as intended, not as a divergence.

### MINOR 7: the hal-dev.md sentence drops "pushed only with owner approval"

**Evidence.** Spec A14 (`spec.md:310-311`) requires the sentence to include "pushed only with owner
approval". The plan's text at `plan.md:804-808` says "Approved SDK changes go on the bereanOS fork".
That covers approval of the change, not of the push.

**Fix.** Append ", pushed to `berean` only with the owner's approval" after "bereanOS PR".

VERDICT: CLEAR
