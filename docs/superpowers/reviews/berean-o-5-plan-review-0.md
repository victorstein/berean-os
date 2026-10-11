Tier: standard

# berean-o-5 plan review 0

Plan: `docs/superpowers/plans/2026-10-11-berean-o-5-plan.md`, reviewed against
`docs/superpowers/specs/2026-10-11-berean-o-5-design.md` (pass 2), on `perf/r2-sd-40mhz` at
`2c478c47`, with the SDK at `67f7e012`.

## What was checked independently

I didn't take the plan's "How this plan was checked" table on trust. I re-ran its claims:

- **Every replace block matches exactly once.** A script applied all 13 "replace this" / "with
  this" pairs, in order, to scratch copies of `SdmmcBlockDevice.cpp`, `BoardConfig.h` and
  `xteink-x4pro-support.md`. Each matched once (plan:157, :184, :199, :213, :270, :302, :340,
  :410, :465, :522, :545, :565, :586).
- **Each stage compiles as the plan says.** I ran `xtensa-esp32s3-elf-g++ -std=gnu++2a
  -fsyntax-only -Wall -Wextra` against the prebuilt esp32s3 flags, `qio_opi` sdkconfig, the
  Arduino core and `.pio/libdeps/x4pro/SdFat`, with `-DFREEINK_DEVICE_X4PRO=1
  -DUSE_BLOCK_DEVICE_INTERFACE=1`:
  - after Task 1: clean;
  - after Task 2: clean;
  - Task 3 red: exactly `static assertion failed: the first attempt asks for high speed` and
    `static assertion failed: a high-speed attempt mounts only on a clean init`;
  - Task 3 green: clean;
  - after Task 4: clean.
  `FREEINK_SD_SDMMC` is on for the X4 Pro (`BoardConfig.h:303-305`), so the asserts really
  compile in.
- **The API calls exist with the types the code assumes.**
  - `sdmmc_host_set_bus_width(int, size_t)`, `sdmmc_host_set_card_clk(int, uint32_t)` and
    `sdmmc_host_get_real_freq(int, int*)` are at `IDF/esp_driver_sdmmc/include/driver/sdmmc_host.h:116`,
    `:141` and `:256`.
  - `int max_freq_khz`, `SDMMC_FREQ_DEFAULT 20000`, `SDMMC_FREQ_HIGHSPEED 40000`,
    `SDMMC_FREQ_PROBING 400` and `int real_freq_khz` are at `sd_protocol_types.h:214-217` and
    `:266`. The prebuilt and source headers are identical over `:200-230`.
  - The hybrid build's own config keeps the error names: `CONFIG_ESP_ERR_TO_NAME_LOOKUP=y`
    (`sdkconfig.x4pro:1739`).
- **The final file meets the spec.** The patched file (scratch copy, lines 17-45 and 110-149)
  implements A1-A6 and A11 exactly:
  - the reset comes before the GPIO5 pulse;
  - `acceptsPartialInit(host.max_freq_khz)` gates the fall-through. `sdmmc_card_init` takes the
    host by `const*`, so the local `host` still holds the requested clock;
  - every non-`break` exit from an attempt assigns `lastFailure`;
  - `initErr` is the successful attempt's own result;
  - the line prints before `_card = card`.
  No edited line exceeds 120 columns. The only long lines are pre-existing ones in
  `BoardConfig.h` (:75, :89), outside the edit.
- **Names and types stay consistent across tasks.** That covers `kAttemptClockKhz`,
  `kMountAttempts`, `acceptsPartialInit`, `noAttemptAboveHighSpeed`, `mountErr`, `lastFailure`,
  `initErr`, `mountedAttempt` and `clockKhz`. Each later replace block quotes the earlier
  block's output verbatim, which the exact-match run confirms.
- **Every spec requirement maps to a step.** I checked the self-review table (plan:1036-1052)
  row by row against the spec and found no gap:
  - Goals 1-5, A1-A12, §7.2, §7.3/§7.4 and D1 each land in the step named.
  - A4 and A7 rightly need no code.
  - The "For existing clones" line matches the bead text verbatim (`hpipe bead show berean-o-5`).
  - The #185 range in D1's recommendation (`310ec61..67f7e01`) matches `2822662d`'s gitlink diff.
  - `hpipe decide --task --question --recommend` matches `hpipe decide --help`.
- **The `FILES:` lines cover every touched file.**
  - plan:33-34 sit at column 0, outside any fence, repo-relative and comma-separated, as sibling
    plans do (`2026-10-11-berean-o-3-plan.md:15-21`).
  - They cover all four touched paths: the `freeink-sdk` gitlink and the three SDK files.
  - Nothing else in the bereanOS tree is edited. `build/test` and `.pio/` are gitignored
    (`.gitignore:13`), and the `$SCRATCH` files are outside the repo.
- **Two deviations from the spec are sound, so neither is a finding.**
  - **A2 and A6 land before the red/green step.** Spec §7.1 step 3 adds them after the values
    flip. The plan's reason holds: no commit asks for 40 MHz without the reset (plan:255-257).
    The red set is unchanged, as verified above.
  - **The SDK doc gains a paragraph after the mount list, not a sentence in it.** The list
    is introduced as "Two non-obvious details" (`xteink-x4pro-support.md:232`), so a third item
    would contradict its own heading.
- **Tasks without a failing test are what the spec sanctions.** Tasks 1, 2 and 4 open without
  one. A11 makes the asserts this change's only red, and A12 rules out a host test. Task 2 says
  so (plan:259-260).

No BLOCKER and no MAJOR. The findings below are MINORs, all fixable inline.

## Findings

### MINOR 1 — a build broken by a sibling worktree would read as a wrong check

- **Claim.** The plan's rule is "Run builds one at a time, never two `pio run`s at once"
  (plan:69-70). Task 3 Step 2 filters the red build to `static assertion failed|FAILED`. It then
  says "A different set of failures means a check is wrong … Stop and fix the check before going
  on" (plan:444, :456-458).
- **Problem.** The race the spec names is not confined to this worktree. Spec §7.2 (:511-512)
  says concurrent runs race on `idf_component.yml`, and that file is in the shared framework
  package, not the worktree.
  - If a sibling's build breaks this one during the CMake or component-manager stage, the filter
    shows only a `FAILED` line.
  - The plan then tells the implementer to "fix" assertions that are correct.
- **Evidence.** All of this was observed during this review:
  - `~/.platformio/packages/framework-arduinoespressif32/idf_component.yml` was rewritten at
    06:03, beside an `idf_component.yml.orig` backup.
  - The whole framework package, and `framework-arduinoespressif32-libs`, were reinstalled at
    05:58-05:59.
  - `pio run -e x4pro` and `idf_component_manager.prepare_components` were running at the same
    time in `perf-r9-cover-band-cache` and `perf-r21-menu-close-restore`.
- **Fix.** Add to "Rules for the implementer":
  - Sibling worktrees share `~/.platformio/packages`.
  - A `FAILED` with no `error:` in `SdmmcBlockDevice.cpp` is environmental. Re-run the same
    command once, and read the unfiltered tail (`… 2>&1 | tail -40`) before concluding anything.
  - In Task 3 Step 2, put that check before "a check is wrong".

### MINOR 2 — `$SCRATCH` does not survive from one shell call to the next

- **Claim.**
  - Task 6 Step 1 writes `"$SCRATCH/sdk-commit-msg.txt"`, then runs `git reset --soft` and
    `git commit -F` (plan:712-725). The instruction to set `SCRATCH` comes after the block
    (plan:728).
  - Task 10 Step 2's first block defaults `SCRATCH=${SCRATCH:-$(mktemp -d)}` (plan:897).
  - The fill block (plan:989-1003) and Step 3's `--body-file "$SCRATCH/pr-body.md"` (plan:1015)
    rely on that value.
- **Problem.** The implementer's Bash tool does not keep environment variables between calls.
  - **Task 6.** If `SCRATCH` is unset, `cat > "/sdk-commit-msg.txt"` fails. With no `&&`,
    `git reset --soft 67f7e012` still runs and the commit then fails. The SDK is left at
    `67f7e012` with the whole diff staged. That is recoverable, but nothing in the plan says how.
  - **Task 10.** If the template block runs as its own call, its `mktemp -d` directory is lost.
    The fill script then reads `./pr-body.template.md` from the repo root and fails.
- **Fix.**
  - Start each of those four blocks with a literal `SCRATCH=<session scratchpad path>`, or
    `: "${SCRATCH:?set SCRATCH to the session scratchpad}"`.
  - Drop the `mktemp -d` fallback.
  - In Task 6, chain `cat … && git -C freeink-sdk reset --soft … && git -C freeink-sdk commit -F …`.

### MINOR 3 — the CI watch relies on the one source the workflow says has lied

- **Claim.** Task 10 Step 4 runs only `gh pr checks --repo victorstein/berean-os --watch`, and
  expects "every check passes" (plan:1024-1030).
- **Problem.** The workflow doc says this command collapses same-named check runs. A stale
  FAILURE beside a later SUCCESS then shows only green, and this "has already shown a failed PR
  as passing on this codebase". It adds that "No checks reported" means not started
  (`docs/contributing/development-workflow.md:121-132`). Run straight after `gh pr create`, the
  watch can hit exactly that case.
- **Fix.** After the watch, run the two commands the doc gives (`:127-128`), and treat
  "no checks reported" as "wait and look again":
  - `gh pr view <n> --json statusCheckRollup`;
  - `gh run list --commit "$(gh pr view <n> --json headRefOid -q .headRefOid)"`.

### MINOR 4 — Task 2 Step 1 alone does not compile

- **Claim.** Task 2 builds only at Step 4 (plan:379-387).
- **Problem.** Step 1 (plan:270-296) adds `esp_err_t e = sdmmc_host_set_card_clk(…)` at the top
  of the loop body. Task 1's `esp_err_t e = sdmmc_card_init(…)` is still in the same scope until
  Step 2 replaces it (plan:302-334). The task ends green, so the commit is sound. But an
  implementer who builds after each edit hits a red the plan does not predict. That red is not
  the kind it warns about, so they may start improvising.
- **Evidence.** The stage after plan:270 only fails with `error: redeclaration of 'esp_err_t e'`.
  The stage after plan:340 compiles clean.
- **Fix.** Add one sentence to Step 1: "This does not compile until Step 2 replaces the second
  `esp_err_t e` declaration; build only at Step 4."

### MINOR 5 — neither rebase step says what to do on a conflict

- **Claim.** Task 0 Step 2 (plan:109-117) and Task 9 Step 2 (plan:843-858) both say "Expected:
  the rebase succeeds".
- **Problem.** Task 0 checks the gitlink only after the rebase. Task 9 rebases after the SDK
  commit has already been pushed to the fork (Task 8).
  - The likeliest stop in Task 9 is a `freeink-sdk` conflict: `main` moved the gitlink.
  - That conflict needs the SDK commit rebased and a fresh D1. The plan gives no stop rule for
    it.
  - The general "stop and report" rule (plan:63-65) covers replace blocks only.
- **Fix.** Add to both steps: "If the rebase stops on a conflict, `git rebase --abort` and
  report. A `freeink-sdk` conflict means the SDK commit must move onto `main`'s gitlink, which
  needs a fresh decision."

### MINOR 6 — PR-body and commit text that can be wrong when filled

- **A hardcoded host-test result.** The template says "**Host tests:** they pass" (plan:935).
  Task 5 Step 2 allows a pre-existing failure to be recorded instead (plan:669-672).
  - **Fix:** make that bullet a fourth `@…@` token, or tell the implementer to edit it when
    Task 5 recorded a failure.
- **A stale duration on the step 4 marker.** Step 4 starts the span at "`Wait complete:
  8179_DRF (~1507 ms)`" (plan:954, from spec §7.3 step 4).
  - `origin/main` already carries #255 (`106dfc4b`), which Task 0's rebase brings in. #255
    paints Bible search's Opening frame with a FAST refresh, about 560 ms by its own PR body.
  - The marker string stays and the duration doesn't. That case is also A/B (below).
  - **Fix:** drop "(~1507 ms)".
- **A hardcoded session link.** The `Claude-Session` trailer (plan:829) and the PR-body link
  (plan:968) are written in by hand. Yet the plan says the tokens "are filled from the tree,
  never by hand" (plan:893-894).
  - Implementers run in their own sessions: sibling #255 ends with
    `session_01BKhtSZjh1TanRUgr3K2qzM`.
  - **Fix:** use the link from the implementing session's own attribution instructions.
- **Two dropped clauses in the device checks.**
  - Step 1's "Report" (plan:950) drops spec §7.3 step 1's "with its `after` reason if it has
    one" (spec:549-550).
  - Step 7 (plan:962) drops "including any `after` or `partial init` reason" (spec:590-591).
  - **Fix:** restore both clauses.

Context for the comparison case, not a finding: `git log --oneline 478cf1ed..origin/main -- src lib`
already prints `106dfc4b`. Task 9 Step 2 will therefore land on Case A/B, which the plan handles
(plan:851-855, :977-983).

VERDICT: CLEAR
