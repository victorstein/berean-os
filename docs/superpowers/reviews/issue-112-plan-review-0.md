# Plan review, pass 0: issue #112

- Plan: `docs/superpowers/plans/2026-09-26-issue-112-plan.md`
- Spec: `docs/superpowers/specs/2026-09-26-issue-112-design.md`
- Tree: `fix/112-release-serial-logging` at `e70eb95a`; `origin/fix/112-release-serial-logging` is
  also `e70eb95a`, and `origin/main` (`2f303f6f`) is an ancestor.

## What was checked, and holds

- **Spec coverage.** G1 → Step 2. G2 → A-2's unreachability, observed by T5 in Step 2 and by tester
  step 2 in the PR body. G3 → Step 2 (logging line) and Step 4a (dialect). G4 → Step 5 (all three
  A-8 sites). A-7 → Step 4b. A-4's one-sentence trade-off, A-10's title, the shared-file report and
  all four human-tester steps are in the Step 7 body. T1–T6 are all run (Steps 1, 2, 3, 6). N1–N6
  are respected: no step touches `src/`, `lib/`, `freeink-sdk/`, the dev env or historical docs.
- **Anchors.** Every Step 0 anchor matches the tree: `platformio.ini:187-188`, `:40`,
  `AGENTS.md:176`, `.clangd:2`, `USER_GUIDE.md:391` and `:408-409`, `bug_report.yml:56-57`,
  `src/main.cpp:342`.
- **Find texts are unique.** `BEREAN_VERSION` occurs three times in `platformio.ini`, but the
  three-line block in Step 2 (`:186-188`) occurs once, and it leaves the dev env's `:169` alone.
  The Step 4a, 4b, 5a, 5b and 5c find texts each match once.
- **Step 4a and 5c replacement text** match spec A-6 and A-8 word for word (5c only rewrapped inside
  the `description: |` block, at the correct 8-space indent).
- **The check commands work.** The research build still in `.pio/build/x4pro-gh_release/` is the
  flag-removed image (5,490,768 B). On it the plan's three `strings | grep -c` commands print
  `0`, `0`, `1`, which are Step 2's expected T1/T2/T5. R §8 (`research:165,175-178`) records the
  before values and the −45,952 B delta the plan uses.
- **Red before green.** Step 1 rebuilds the release env because `platformio.ini` has changed since
  that research build, so T1 = 1 is a real red.
- **FILES line** (`plan:14`) is at column 0, outside any fence, repo-relative, and lists all five
  tracked files that any step edits. The only other writes are `.pio/issue112-release.log` and
  `.pio/issue112-pr-body.md`. `.pio` is gitignored (`.gitignore:1`), so they are build scratch and
  not part of the lock.
- **Every commit leaves a working tree.** Step 2 is the only build-affecting commit and is verified
  by a release build before it is made. Steps 4 and 5 are doc-only.

## Findings

### MAJOR 1: after a Step 0 rebase, Step 7's plain `git push` is rejected

- **Claim.** Step 0 tells the implementer to `git rebase origin/main` if `main` has moved
  (`plan:57`). Step 7 then runs a bare `git push` (`plan:392-394`).
- **Problem.** The branch is already published: `origin/fix/112-release-serial-logging` is
  `e70eb95a`, the same as the local HEAD. A rebase rewrites those four research, spec and plan
  commits, so the push is non-fast-forward and is refused. The plan does not say what to do next. An
  implementer with no other context either stalls or picks `--force`, which is the unsafe choice.
- **Evidence.** `git rev-parse HEAD origin/fix/112-release-serial-logging` prints `e70eb95a` twice.
  `plan:57` is the conditional rebase. `plan:392-394` is the unconditional `git push`.
- **Fix.** In Step 7, replace the push with: "If Step 0 rebased, run
  `git push --force-with-lease origin fix/112-release-serial-logging`. Otherwise run `git push`."
  This does not reverse any decision, so it can be fixed inline.

### MINOR 1: Step 7 has no fallback for running outside the pipeline

- **Claim.** Step 7 treats the pipeline's implement phase as the push and PR approval
  (`plan:388-390`).
- **Problem.** That holds inside the pipeline, and it matches the issue-34 precedent. But the
  issue-34 plan also says "If you are executing this plan outside that pipeline, stop here"
  (`2026-09-19-issue-34-plan.md:726-728`), and this plan leaves that out. The issue-63 review marked
  an ungated push as a MAJOR (`2026-09-19-issue-63-plan.md:24`).
- **Fix.** Add the issue-34 sentence to Step 7: outside the pipeline, stop after Step 6 and ask
  before pushing, per CLAUDE.md Git rule 2.

### MINOR 2: Step 5b's post-check assumes text follows the deleted paragraph

- **Claim.** After deleting the paragraph, the plan says to "check that whatever follows the old
  paragraph is still separated from the fence by exactly one blank line" (`plan:304-311`).
- **Problem.** Nothing follows it. `USER_GUIDE.md` has 409 lines, and `:408-409` is the end of the
  file. After the edit, the file should end with the closing fence and one newline. The check as
  written asks the implementer to verify something that does not exist.
- **Fix.** Replace the check with `tail -n 3 USER_GUIDE.md` and expect the `cat …` line, then the
  closing fence as the last line, with a trailing newline and no blank line after it.

### MINOR 3: Step 6's expected commit count will be wrong

- **Claim.** Step 6 expects "four docs/research/spec/review commits plus" the three new ones
  (`plan:369`).
- **Problem.** This plan review, and any commit that applies its findings, lands on the branch
  before implementation. There will be more than four pre-existing docs commits.
- **Fix.** Expect "the pipeline's `docs:` commits, then" the three named commits. Do not state a
  count.

### MINOR 4: Steps 4 and 5 check only after the edit

- **Claim.** Every step starts with a failing check.
- **Problem.** Steps 4 and 5 run their greps only after editing (`plan:242-248`, `:308-310`). Step 0
  does confirm the anchors, so this costs little. Still, no check is shown going red first.
- **Fix.** At the top of Step 4, run `git grep -n "c++2a" -- AGENTS.md .clangd` and expect two hits.
  At the top of Step 5, run `grep -c "Release builds log at a lower level" USER_GUIDE.md` and expect
  `1`.

VERDICT: CLEAR
