# PR #126 intent review, pass 0 (issue #112)

- Branch `fix/112-release-serial-logging` at `6d097861`.
- Checked against:
  - issue #112, including the owner's decision comment;
  - the spec, `docs/superpowers/specs/2026-09-26-issue-112-design.md`;
  - the plan, `docs/superpowers/plans/2026-09-26-issue-112-plan.md`.
- Diff read: `git diff origin/main...HEAD -- ':!docs/superpowers'` (5 files).

## Findings

None at BLOCKER, MAJOR or MINOR.

## Acceptance criteria (issue #112 and the owner's comment)

The owner's comment settles the issue's either/or: "drop serial logging from the release build.
Remove `ENABLE_SERIAL_LOG` from `x4pro-gh_release` and set `LOG_LEVEL=0` so it matches CLAUDE.md;
also correct CLAUDE.md's `-std=c++2a` to `gnu++2a`."

| Criterion | Evidence | Met |
| --- | --- | --- |
| Drop `ENABLE_SERIAL_LOG` from the release env | `platformio.ini:186-188`: the diff removes `-DENABLE_SERIAL_LOG` from the release block only, pinned under `-DBEREAN_VERSION` | yes |
| `LOG_LEVEL=0` | `platformio.ini:187`: `-DLOG_LEVEL=0 ; inert without ENABLE_SERIAL_LOG` | yes |
| CLAUDE.md dialect set to `gnu++2a` | `AGENTS.md:176` (CLAUDE.md is a symlink to it) | yes |
| Build and CLAUDE.md agree on logging | `AGENTS.md:173-174` and `:854-855` were false before this PR and are now true, untouched per spec A-6 | yes |

The issue's "put the `CMD:` handler behind a debug flag" line belongs to the *keep logging* branch,
which the owner did not choose. On the chosen branch, the handler is unreachable at runtime:

- the only `Serial.begin` in `src`/`lib` is `src/main.cpp:350`, inside `#ifdef ENABLE_SERIAL_LOG` at
  `:343`. `lib/Logging/Logging.h:78` is a wrapper, and nothing in release calls it;
- so `logSerial.available() > 0` at `src/main.cpp:625` is never true, which is spec A-2's argument.

The PR body reports a before/after `strings` check, T5 `SCREENSHOT_START` staying at 1, and offers
the compile-out guard as optional hardening. That meets the issue's intent.

## Spec requirements

| Spec item | Implemented |
| --- | --- |
| G1 / A-1: flag removed, `LOG_LEVEL=0` with the trailing comment | `platformio.ini:187`, the exact text from spec :81 |
| G2 / A-2: handler unreachable, not compiled out; guard offered in the PR | the PR body's "Shared-file report" section has the guard, with line numbers updated to `:623-638` |
| G3 / A-6: `AGENTS.md:176` dialect | the exact wording from spec :157 |
| A-7: `.clangd:2` | `-std=gnu++2a` |
| A-8 site 1: `USER_GUIDE.md:390` dev-build qualifier | `USER_GUIDE.md:391-393`. Its "section 16" cross-reference resolves to `## 16. Firmware updates` (`:343`), which covers SD firmware update (`:349`) |
| A-8 site 2: delete the "lower level" paragraph | removed. The file ends with the closing fence and a single `\n` (checked with `od`) |
| A-8 site 3: `bug_report.yml:57` | wording matches spec :176-178, with the block-scalar indentation kept |
| A-4: crash-report trade-off stated in the PR | the "Trade-off to see before merging" section |
| N1: dev env unchanged | no `[env:x4pro]` hunk. The PR reports the dev image still has the `logPrintf` prefix (1) |
| N3/N4/N6: no edits to `main.cpp`, the SDK or `lib/Logging` | outside `docs/superpowers`, the diff touches only the five planned files |
| T1-T6 | reported in the PR's Verification table and bullets |

All of the spec is implemented, not just the easy half. Nothing in scope was dropped. The one
addition beyond the owner's wording, `.clangd`, is spec A-7, which the spec review allowed and
flagged as droppable.

## Tests

Spec A-9 says why this repo has no automated check for build flags. The tests are observable checks
on the binary that go red then green:

- T1 (the `logPrintf` prefix) goes 1 → 0;
- T2 goes 1 → 0;
- T3 guards the dev env.

These test what the change does (no log format string in the release image), not how it does it.
They are sound for a change that is only build configuration.

## Divergence from the plan

Every divergence is explained in the PR's "Where the tree moved under the plan" section:

- **Rebase onto `0e0e7641`.** `main.cpp` line numbers shifted: 342 → 343, and 618-633 → 623-638.
- **Size.** The image shrank by 47,344 B against a planned 45,952 B. The PR attributes the gap to 23
  new `LOG_*` call sites (738 → 761). That is consistent with the ~60 B per call site the plan's own
  figures imply.
- **Host tests.** The plan (Step 6) said not to run them. The PR ran them anyway as a regression
  check (914/914). That is extra verification, not a change in scope.

The three commits match the plan's messages and order (Steps 2, 4 and 5).

VERDICT: CLEAR
