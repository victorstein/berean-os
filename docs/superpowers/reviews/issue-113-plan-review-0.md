# Plan review 0: issue #113

Plan: `docs/superpowers/plans/2026-09-26-issue-113-plan.md`
Spec: `docs/superpowers/specs/2026-09-26-issue-113-design.md`
Reviewed at `8135ab80`.

## Method

I did more than read the plan. I applied every edit in Tasks 1 to 4 literally, as exact-text replacements, to a scratch copy of `HEAD` outside the worktree. After each edit I ran the generator and the Task 5 greps.

| Step | Plan expects | Observed |
|---|---|---|
| Baseline | `String keys: 475` | 475, exit 0 |
| 1b red | 1 missing, `STR_SD_CARD_ERROR`, exit 1 | same |
| 1d green | 476, exit 0 | same |
| 2b red | 4 missing (the four LIST/MANIFEST keys) | same, same order |
| 2d green | 480 | 480 |
| 3b red | 2 missing, `DELETE` then `DIR_CREATE` | same |
| 3d green | 482 | 482 |
| 4b red | 4 missing, the `STR_FONT_FILE_*` set | same, same order |
| 4d green | 486 | 486 |
| 4e grep | 8 lines, one `%s` each | 8 lines (`english.yaml:368-371`, `spanish.yaml:351-354`), one `%s` each |
| 5.1 greps | no output | no output |
| 5.2 fallback grep | no output | no output. The filter is live: the unfiltered `-v` output does print `INFO: '...' missing in ES, ...` lines for other keys, so the grep can match. |

Every replace target in the plan occurs exactly once in the file it names. The one exception is `errorMessage_ = "Invalid font manifest";`, which occurs twice. The plan says so and handles both (`plan:144`). All the YAML anchors exist verbatim: `english.yaml:224`, `:360`; `spanish.yaml:215`, `:343`. After the edits, each of the four `char message[128]` declarations sits in its own `if { ... return; }` block (`FontDownloadActivity.cpp:351-395`), so none of them collide.

## Spec coverage

| Spec requirement | Plan step |
|---|---|
| 11 keys, English byte-identical (Goal, key table `spec:82-94`) | 1c, 2c, 3c, 4c. I checked every English value against the literals at `FontDownloadActivity.cpp:92-432` and `main.cpp:405` character for character. |
| Spanish values (`spec:84-94`, A4, A7) | 1c, 2c, 3c, 4c. All 11 match the spec table exactly, including the shortened A4 forms, which `plan:345` protects. |
| 12 call sites, with `:113` and `:148` sharing one key (A2) | 2a (5 sites), 3a (2), 4a (4), 1a (1) |
| `snprintf` into `char[128]`, `file.name` only as an argument (A3) | 4a |
| Keys placed after `STR_FONT_INSTALL_FAILED`, and SD next to `STR_SD_CARD` (`spec:80`) | 2c/3c/4c chain from the `STR_FONT_INSTALL_FAILED` anchor; 1c |
| Locking unchanged (Non-goals) | `plan:132`, `:195`, `:249` |
| SD error goes through `tr()` but renders in English (A5) | 1a, and the PR body in Task 6 |
| A1 shared-file edits disclosed | Task 6 PR body |
| Testing steps 1–7 | Red/green in every task, 5.1, 5.2, 4e, 5.3, 5.4, 5.5 |
| Device checks | Task 6 PR body, "Not verified" |

Nothing in the spec is missing from the plan. Key names, the `tr()` / `snprintf` signatures and the buffer size are the same in every step. The plan contains no placeholders. `<scratchpad>` at `plan:405` is a runtime path for the agent to fill in, not a gap in the design.

## FILES lock

`plan:6-7` sit at column 0, outside any code fence, and list repo-relative paths: `english.yaml`, `spanish.yaml`, `FontDownloadActivity.cpp`, `main.cpp`. These are the only tracked files any step edits. The conditional `#include <cstdio>` fallback at `plan:309` edits `FontDownloadActivity.cpp`, which is already on the list. If the Task 5.5 `clang-format-fix` run changes anything, it can only touch those same files. The generated `I18n*` files are gitignored.

## Findings

### MINOR 1: `git push` in Task 6 is rejected if Task 0's rebase rewrote the branch

- **Claim.** Task 6 pushes with a bare `git push` (`plan:399-402`).
- **Problem.** The branch is already published: `HEAD` and `origin/fix/113-i18n-font-download-sd-error` are both `8135ab80`. Task 0 runs `git rebase origin/main` (`plan:40`). Today that is a no-op, because `origin/main` (`2f303f6f`) is already an ancestor of `HEAD`. If `main` moves before implementation starts, though, the rebase rewrites the four pushed docs commits. A plain `git push` is then rejected as non-fast-forward, and a literal-minded implementer stalls at the last step.
- **Evidence.** `git rev-parse HEAD origin/fix/113-i18n-font-download-sd-error` prints the same SHA twice. `plan:40` rebases, and `plan:401` pushes without a lease.
- **Fix.** In Task 6 step 1, add: "If Task 0's rebase moved any commits, push with `git push --force-with-lease`." Separately, CLAUDE.md's Git rule 2 needs explicit user approval before any push or PR. Earlier pipeline plans handle this the same way (`2026-09-19-issue-63-plan.md:768`), so I assume the pipeline's own gate covers it and am not ranking it as a finding.

No BLOCKER or MAJOR findings. The plan can be executed literally, I reproduced every expected count, and each commit leaves the generator green. The tree is never committed in the red state (`plan:22`).

VERDICT: CLEAR
