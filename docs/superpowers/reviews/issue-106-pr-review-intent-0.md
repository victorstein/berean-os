Tier: heavy

# PR #176 review (intent): `-Wall` on repo sources, warnings at zero

- **PR:** #176, `fix/106-enable-wall` at `4c7ee16f`
- **Checked against:** issue #106, `docs/superpowers/specs/2026-09-27-issue-106-design.md` (the spec),
  `docs/superpowers/plans/2026-09-27-issue-106-plan.md` (the plan)
- **Evidence read:** `gh pr diff 176`, the tree at HEAD, and the implementation-run artefacts in
  `/private/tmp/claude-501/berean-os-issue-106/`: `census-{red,green,landed,verify}-*.{w,log}`,
  `scoping-{main,expat}.txt`, `implement-report.md`. I also re-ran
  `python3 -m unittest discover -s scripts/tests`, which gave 37 tests, OK.

## Issue acceptance criteria

| Issue #106 item | Status | Evidence |
|---|---|---|
| 1. `format(printf, 3, 4)` on `logPrintf`, plus the fallout fixed | Met by #142, before this PR | `lib/Logging/Logging.h:42` carries the attribute. `platformio.ini:71-73` carries `-Wformat`. With `-Wall`, which implies `-Wformat`, both green censuses show no `-Wformat` line from repo code (`census-verify-*.w`, one line each, the WebSockets deprecation) |
| 2a. `-Wall` on the `x4pro` build | Met, in both envs | `platformio.ini:137` registers `pre:scripts/enable_repo_warnings.py` in `[base] extra_scripts`, which both envs inherit. `scoping-main.txt` has `-Wall` on the `src/main.cpp` compile line (count 1). `scoping-expat.txt` does not have it on `xmlparse.c` (count 0), and that compile line exists |
| 2b. "possibly `-Wformat=2`" | Deferred, and the deferral is stated | The spec lists it under Non-goals. The PR lists it under Follow-ups. The issue's wording makes it optional |
| 2c. "warnings are zero before the flag lands" | Met | Red release census: 61 repo lines (`census-red-x4pro-gh_release.w`). The census at `4c7ee16f`, with the flag registered, has 0 repo lines in both envs (`census-landed-*.w`, `census-verify-*.w`). The `extra_scripts` line lands in the last commit, `4c7ee16f`, after the green census. That satisfies spec C4 |

## Spec requirements

Spec Goals 1-4, assumptions A1-A16 and conditions C1-C4 are each implemented at the
level the spec asks for:

- **A1, A2, C1.** `scripts/repo_warnings.py:14-31` implements the pure rule, with `VENDORED_LIBS = ("expat", "miniz", "uzlib")` at `:11`. The drive-safe normalisation follows the spec's four steps, in order: `normcase(normpath)`, then the `splitdrive` check, then `commonpath` inside `try`, then the prefix rule.
- **A3, A4, C2.** In `scripts/enable_repo_warnings.py`:
  - `:24` and `:33` add the `-isystem` for FreeInkUI.
  - `:27-30` add exactly `-Wall` per owned node.
  - The docstring at `:1-13` gives the "why not `build_flags`" reason and the sign-compare note.
  - `-Wformat` is unchanged at `platformio.ini:73`.
- **C3.** `AGENTS.md:195-197` holds the line directly after the flags code block. `CLAUDE.md` is a symlink to `AGENTS.md`, so this is the spec's `CLAUDE.md` line. The plan applied that correction after plan review 0, BLOCKER 1.
- **A5 and A6.** Every `-Wunused-*` site in `census-red-x4pro-gh_release.w` gets `[[maybe_unused]]` in the diff. `contentTop` is deleted from `FontDownloadActivity.cpp`.
- **A7.** Explicit cases, with no `default:`:
  - `BibleDownloadActivity.cpp:275` adds `NoEpubEdition` to the group that already calls `fail`.
  - `WifiSelectionActivity.cpp:977` adds `PASSWORD_ENTRY`.
  - `EpubReaderActivity.cpp:789-793` adds `AUTO_PAGE_TURN` and `ROTATE_SCREEN`, with the reason.
- **A8.** The init lists are reordered and no declaration moved: `ChapterHtmlSlimParser.h:160-172` and `ProgressMapper.cpp:656-661`.
- **A9.** `ChapterHtmlSlimParser.cpp:925-926` adds parentheses that match the existing precedence.
- **A12.** `HalGPIO.cpp:44` and `:115` put the NVS keys, the three helpers and the function under one guard. `X3GPIO::*` stays outside it. The guard matches the caller at `:118-119`.
- **A13.** `FontDownloadActivity.cpp:130-131` and `:136-137` hoist named `JsonArray` locals.
- **A14, primary form, fallback not needed.** Both rewrites keep the value of every branch:
  - At `EpubReaderActivity.cpp:1047-1052`: the explicit offset first; otherwise `cachedVisibleTextOffset` only when no page jump, anchor or spine change is pending; otherwise `nullopt`.
  - At `ReaderBookmarks.cpp:125-128`: `visibleOffset` if it has a value; otherwise the page lookup when the page is in range; otherwise `nullopt`.
  - The `Section.h` pragma fallback was not used, and the PR says so.
- **A15.** `CrossPointWebServerActivity.cpp:203-205` has the `static_assert` and one `softAP` call. The call is the old `else` branch, and `AP_PASSWORD` is `nullptr`.
- **A16.** `src/main.cpp` gets a single `[[maybe_unused]]`.
- **Goal 3 (behaviour-preserving).** RAM is unchanged in both envs. Flash grows by +44 B (release) and +16 B (`x4pro`). Both are inside the spec's ±64 B budget and are reported in the PR.
- **Goal 4 (what was excluded or suppressed).** The PR's "What third-party code is excluded or suppressed" section lists every item the spec requires, `-Wsign-compare` included.

No requirement is only half-done. I found no silent scope reduction. The deferrals are
`-Wextra`, `-Wformat=2`, a CI gate and `-Wsign-compare`. All four are spec non-goals, and the PR
lists all four as follow-ups. I found no scope expansion either: every source edit maps to a line
in the red census. `AGENTS.md` is the one documentation change, which C3 requires. The
`docs/superpowers/*` files are this pipeline's normal artefacts.

## Tests

`scripts/tests/test_repo_warnings.py` exercises the ownership rule as behaviour, through
the real `posixpath` and `ntpath` modules:

- owned sources, and vendored or third-party sources;
- prefix traps (`lib/expatfoo`, `srcx`, `libx`, and a sibling project directory);
- `..` normalisation;
- Windows case and slash normalisation, and a cross-drive path that must not raise.

The tests do not copy the implementation's logic. For example, the cross-drive case would fail
if the function called `relpath` directly. The middleware glue has no unit test. That is
appropriate: it cannot run without SCons, and the census, which runs the red build and then the
landed build, plus the verbose scoping proof, is its acceptance test, as spec testing step 2
intends.

## Plan divergences

- **`src/network/FileRoutes.cpp`.** This file is outside the plan's `FILES:` lock. PR #174 moved
  three of the plan's log-only locals out of `CrossPointWebServer.cpp` and into it. The PR body
  explains this under "Deviation from the plan". The edits are the same text, and all three sites
  appear in the red census (`FileRoutes.cpp:370,387,388`).
- **`Section.h`.** It is in the plan's `FILES:` list but was not touched, because the fallback
  was not needed. The PR says so.
- **Census baseline.** The plan's Step 0 baselines were measured before #174 merged. The PR's
  "before" figures come from the red census instead, on the same tree as the fixes. That is the
  more honest comparison, and the table header in the PR says how "Before" was measured.

## Findings

### MINOR 1: the PR describes the scoping proof instead of showing the two compile lines

The plan's PR-body requirement 3 (`docs/superpowers/plans/2026-09-27-issue-106-plan.md:1089-1090`)
and spec testing step 2 ("The PR also shows one owned and one not-owned compile line from
`pio run -v`") both ask for the compile lines themselves. The PR body only says that the `main.cpp`
line includes `-Wall` and the `xmlparse.c` line does not. The PR does not explain why it departs
from the plan here.

The claim itself is verified. `scoping-main.txt` contains `-Wall` once, and `scoping-expat.txt`
is a compile line that does not contain it. Each raw line is about 36 KB of include paths, so
pasting them whole would not help a reader.

Fix inline: paste each line trimmed to its flags, from `CCFLAGS` through `-Wall` or the end of the
warning flags, or add one sentence saying the lines were summarised because of their length.
Neither option changes scope.

VERDICT: CLEAR
