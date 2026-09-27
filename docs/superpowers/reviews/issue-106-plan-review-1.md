Tier: heavy

# Issue #106 plan review 1

Reviewed: `docs/superpowers/plans/2026-09-27-issue-106-plan.md` against
`docs/superpowers/specs/2026-09-27-issue-106-design.md`, at `f10809cb`.

## Review 0 follow-up

Every plan-review-0 finding is applied, and applied correctly.

- **BLOCKER 1 (the `CLAUDE.md` symlink).** Fixed.
  - `AGENTS.md` is on the `FILES:` lock (plan `:11`).
  - Step 13b edits `AGENTS.md` by name (`:975-982`).
  - Step 13 stages `AGENTS.md` (`:1003`), and so does Step 14 (`:1021`).
  - The symlink is explained at `:959-961`, and the Step 0 preflight diff covers `AGENTS.md` (`:58`).
  - In the dry run below, the edit landed in `AGENTS.md`, and `CLAUDE.md` stayed a symlink.
- **MAJOR 1 (the scoping proof).** Fixed at `:349-366`.
  - The plan deletes both objects and builds with a fresh `PLATFORMIO_BUILD_CACHE_DIR`.
  - It also adds a check that the expat compile line exists.
  - The object paths are real: `.pio/build/x4pro-gh_release/src/main.cpp.o` and
    `.pio/build/x4pro-gh_release/lib21a/expat/xmlparse.c.o` both exist in this worktree.
  - The verbose compile-line form `-o .pio/build/<env>/…/X.o` is confirmed against an existing
    verbose log (`feat-114…/pio_task8_verbose.log`). So `grep -F -- "-o $EXPAT_OBJ"` matches the
    compile line and not the archive or link lines.
- **MINOR 1 (the PR title).** Fixed. `fix` is a visible "Bug Fixes" section in
  `release-please-config.json`, and `build` is hidden.
- **MINOR 2 (`$SCRATCH` between shells).** Fixed. Every block that uses `$SCRATCH` sets it.
- **MINOR 3 (the Step 5 count).** Fixed. The plan says 32, and the list has 32 entries.

## What was checked, and how

- **All ten edit scripts were dry-run in plan order.** They ran against a `git archive HEAD` copy
  of `src lib scripts test platformio.ini AGENTS.md CLAUDE.md` in the scratchpad, never against
  the worktree. Every content assertion matched, and every script printed `ok`.
  - `git diff --stat 24e3e8db HEAD -- src lib scripts platformio.ini AGENTS.md` is empty, so the
    plan's line numbers still hold.
  - I read the resulting diff hunk by hunk. It matches the spec (A5-A15).
- **The Step 1 test and Step 2 module were extracted verbatim and run.** Without
  `repo_warnings.py`, the run errors, which is red. With it, the 10 tests pass, and the whole
  `scripts/tests` suite passes (36 tests).
  - Extra probes all behave as the spec's error-handling section requires:
    - `D:\proj\Lib\Expat\x.c` with `ntpath` gives `False`: normcase applies before the
      vendored check.
    - `/work/p/src/../lib/expat/a.c` gives `False`: normpath applies first.
    - A UNC path against a `D:` project gives `False`, with no exception.
    - A trailing-slash `project_dir` still classifies correctly.
- **`[[maybe_unused]]` on a set-but-unused local was probed with
  `xtensa-esp32s3-elf-g++ -std=gnu++2a -Wall -Os`.** This is the `modeName` shape at
  `CrossPointWebServerActivity.cpp:133`. The attribute suppresses `-Wunused-but-set-variable`,
  and an unannotated control in the same probe still warns.
- **The Step 12 `static_assert` form was probed with the same toolchain** under
  `-Wall -Wformat`, against a `nonnull(1)`-attributed `softAP` and the real
  `constexpr const char* AP_PASSWORD = nullptr` (`CrossPointWebServerActivity.cpp:28`). It
  compiles clean, and `apStarted` is read at `:207` of the edited file, so `const bool` is valid.
- **The framework's flag files contain no `-Wall`**
  (`framework-arduinoespressif32-libs/esp32s3/flags/{c,cpp,S}_flags`). So the Step 3 `0` for the
  expat line is meaningful, and not masked by a framework-wide `-Wall`.
- **Both envs inherit the new `extra_scripts` entry.** Neither `[env:x4pro]` nor
  `[env:x4pro-gh_release]` overrides `extra_scripts` (`platformio.ini:131`, `:163-164`,
  `:181-182`).
- **The Step 13 "1 total line" expectation holds for `x4pro`.** The `x4pro` baseline
  (`base-x4pro.w`) contains only the WebSockets deprecation plus the `-Wnonnull` that Step 12
  fixes. A global-`-Wall` `x4pro` log (`wall-x4pro.log`) has no repo warning that is absent from
  the release spike set. So no dev-only site is missing from Steps 4-12.
- **No module name in `scripts/` shadows a stdlib module** (Step 3's `sys.path.insert(0, …)`).
- **The `FILES:` lock is complete.** Every path opened by a Step 1-13 script or staged by a
  commit appears on a column-0 `FILES:` line (plan `:10-18`). Step 14's `git add -A` only
  re-stages paths that are already locked, and `build/` is ignored (`.gitignore:13`).

Spec to plan mapping. This is unchanged from review 0 and still complete:

| Spec item | Plan step |
|---|---|
| A2, A10, C1 | 1-2 |
| A1, A3, A4, C2 | 3 |
| A5, A16 | 4-5 |
| A6, A13 | 6 |
| A7 | 7 |
| A8 | 8 |
| A9 | 9 |
| A12 | 10 |
| A14 | 11 |
| A15 | 12 |
| C3, C4, testing 2-3 | 13 |
| testing 4-5 | 14 |
| Goal 4, A11, testing 6 | 15 |

## MAJOR 1 — Step 14's host-test configure fails on this machine: Ninja is not installed

- **Claim.** Step 14 configures the host tests with
  `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release` (plan `:1016`). That is
  CI's command (`ci.yml:145`).
- **Problem.** CI installs `ninja-build` first (`ci.yml:136`). This host does not have it on
  `PATH`, so the configure fails, and so do the build, ctest and the warning count that depend
  on it. An implementer running the plan literally stops at Step 14 with nothing to compare.
  The spec's command has no generator at all (spec `:357`).
- **Evidence.**
  - `which ninja` finds nothing, and there is none in `/opt/homebrew/bin` or `/usr/local/bin`.
  - A scratch configure with `-G Ninja` fails:
    `CMake Error: CMake was unable to find a build program corresponding to "Ninja".  CMAKE_MAKE_PROGRAM is not set.`
  - A Ninja binary does exist at `/Volumes/stein/.platformio/packages/tool-ninja/ninja`.
- **Fix.** Either drop `-G Ninja` and use the default Makefiles generator, as the spec does, or
  keep it and point CMake at PlatformIO's Ninja:

  ```bash
  cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_MAKE_PROGRAM=/Volumes/stein/.platformio/packages/tool-ninja/ninja
  ```

  Also add one line: on a fresh configure, the first `cmake --build build/test` can fail several
  suites while gtest races itself, and re-running the same command succeeds (project memory,
  "Fresh worktree build bootstrap"). Otherwise `| tail -3` hides a spurious failure. This is a
  mechanical fix and does not change any decision.

## MINOR 1 — the Step 11 fallback edits `Section.h`, but the Step 11 commit does not stage it

- **Problem.**
  - The fallback's `Section.h` pragma must bracket
    `Section::buildReachedVisibleTextOffset` in `lib/Epub/Epub/Section.h` (plan `:873-874`).
  - The commit's `git add` names only `ReaderBookmarks.cpp` and `EpubReaderActivity.cpp`
    (`:881`).
  - If the fallback is used, `Section.h` stays unstaged through Step 13 (`:1003`). Step 14's
    `git add -A … lib` then sweeps it into the "style: format" commit (`:1021`, `:1035`).
- **Fix.** Add "and `lib/Epub/Epub/Section.h` if the fallback touched it" to the Step 11
  `git add`.

## MINOR 2 — the Step 0 heredocs sit inside a list item, indented by three spaces

- **Problem.** The `census.sh` and `step-build.sh` blocks (plan `:70-96`, `:100-117`) are
  fenced inside numbered list items, so every raw line starts with three spaces. If the block is
  copied from the raw markdown rather than the rendered view, the terminator becomes `   EOF`.
  That does not close `<<'EOF'`, so the heredoc runs to end of input and swallows the
  `chmod +x` line. The later `"$SCRATCH/census.sh"` calls then fail with "Permission denied".
  Every other script in the plan is fenced at column 0.
- **Fix.** Move the two blocks out of the list, so they are fenced at column 0, or add a
  sentence saying the three-space indent must be removed.

## MINOR 3 — Step 15 opens a PR without showing the command

- **Problem.** Step 15 says "Push and open the PR" and gives only `git push` (`:1047-1049`).
  The title and body contents are specified, but the command is not. The repo's rules require
  two things here:
  - `--repo victorstein/berean-os` whenever `gh` may resolve another default (`CLAUDE.md`,
    "Repository context");
  - a base of `main` (`CLAUDE.md`, Git workflow rule 1).
- **Fix.** Add
  `gh pr create --repo victorstein/berean-os --base main --title "fix: enable -Wall for repo sources and fix its warnings" --body-file "$SCRATCH/pr-body.md"`,
  with the body written to that file first.

## MINOR 4 — one of the spec's not-owned cases is replaced rather than carried over

- **Problem.** The spec lists `../packages/framework-arduinoespressif32/cores/esp32/main.cpp`,
  joined to `<project_dir>`, as a not-owned case (spec `:332`). The plan tests an unrelated
  absolute path instead (plan `:179-181`). So the `..`-normalisation route (`normpath` before
  `commonpath`) has no test. The current module handles it correctly (probe above).
- **Fix.** Add `self.assertFalse(posix_owned("../packages/framework-arduinoespressif32/cores/esp32/main.cpp"))`
  to `test_framework_outside_the_project_is_not_owned`.

## Not findings

- **Steps 4-12 still have no per-step red build.** Review 0 accepted Step 3's red census as the
  failing test for the whole cleanup, and that reasoning still holds.
- **Step 15's `git push` works as written.** The branch already tracks
  `origin/fix/106-enable-wall`, and `push.autoSetupRemote` is `true`. Pushing and opening the PR
  is the pipeline's implement phase, not an unapproved action by the implementer.
- **The `platformio.ini` version bump on `origin/main` (`faf76d70`) does not conflict.** It
  touches the `[berean]` version block, not `extra_scripts`, and the Step 0 preflight already
  says to continue without merging.

VERDICT: CLEAR
