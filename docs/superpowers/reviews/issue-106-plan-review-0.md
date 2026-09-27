Tier: heavy

# Issue #106 plan review 0

Reviewed: `docs/superpowers/plans/2026-09-27-issue-106-plan.md` against
`docs/superpowers/specs/2026-09-27-issue-106-design.md`, at `30de6dcd`.

## What was checked, and how

- **Every edit script was dry-run in plan order** (Steps 4-13) against a `git archive HEAD` copy
  of `src lib scripts platformio.ini CLAUDE.md AGENTS.md` in the scratchpad, never against the
  worktree. All ten content assertions matched, and all ten scripts printed `ok`. No code line
  moved between `24e3e8db` and `HEAD` (`git diff --stat 24e3e8db HEAD -- src lib scripts` is empty).
- **The Step 1 test and the Step 2 module were extracted verbatim and run.** Without
  `repo_warnings.py` the run errors (red). With it, all 10 cases pass, and the whole
  `scripts/tests` directory passes (36 tests), under Python 3.14. CI uses 3.12 (`ci.yml:152-157`);
  nothing in the module is version-sensitive.
- **The per-step fixes cover the spike's warning set.** `spike.w` holds 62 unique lines: 61 repo
  lines plus the WebSockets deprecation. Every repo line maps to exactly one of Steps 4-12. The
  A5 sites number 11 in `lib/` plus 32 in `src/`, which is 43: 41 `-Wunused-variable` other than
  `contentTop`, plus 2 `-Wunused-but-set-variable`.
- **The A8 declaration order was checked.** For `ChapterHtmlSlimParser.h`, the members are
  `:27-31` then `:50-62`. For `ParagraphStreamer`, `targetTextNode` comes before `steps` and
  `stepCount`. The reordered lists match.
- **The A12 guard was checked.** At `HalGPIO.cpp:44-113`, nothing in the anonymous namespace is
  used outside `#if FREEINK_MCU_C3`.
- **The A14 restructure was checked.** `a14.log` shows `x4pro-gh_release` succeeding with no
  `maybe-uninitialized` line, and flash at 5,412,338 B.
- **The env-var overrides the census relies on hold.** `PLATFORMIO_BUILD_CACHE_DIR` and
  `PLATFORMIO_EXTRA_SCRIPTS` are both `sysenvvar` options (`platformio/project/options.py:205,818`).
  An env value replaces a single-valued option, and is appended to a multiple-valued one
  (`platformio/project/config.py:292-304`).
- **Every file an edit script opens appears on a `FILES:` line**, with the exception in BLOCKER 1.

Spec to plan mapping:

| Spec item | Plan step |
|---|---|
| A2, A10, C1 | Steps 1-2 |
| A1, A3, A4, C2 | Step 3 |
| A5, A16 | Steps 4-5 |
| A6, A13 | Step 6 |
| A7 | Step 7 |
| A8 | Step 8 |
| A9 | Step 9 |
| A12 | Step 10 |
| A14 | Step 11 |
| A15 | Step 12 |
| C3, C4, testing 2 and 3 | Step 13 |
| Testing 4 and 5 | Step 14 |
| Goal 4, A11, testing 6 | Step 15 |

The gaps are the findings below.

## BLOCKER 1 — the `CLAUDE.md` edit lands in `AGENTS.md`, which is neither locked nor committed

- **Claim.** Step 13b adds the C3 line to `CLAUDE.md`, and Step 13's commit carries it with the
  flag (plan `:945-952`, `:972`). `CLAUDE.md` is declared on `FILES:` (plan `:11`).
- **Problem.** `CLAUDE.md` is a symlink to `AGENTS.md`. The script's `open("CLAUDE.md", "w")`
  follows the link and rewrites `AGENTS.md`. That causes three failures:
  - **The file lock is wrong.** `AGENTS.md` is not on any `FILES:` line, so a touched file is
    missing from the lock.
  - **The flag commit is incomplete.** `git add platformio.ini CLAUDE.md` (plan `:972`) stages the
    unchanged symlink, not its target. The `-Wall` commit lands without the C3 line, which breaks
    C3 and C4's "the flag lands with its documentation".
  - **The line lands in the wrong commit.** Step 14's `git add -A … CLAUDE.md` (`:989`) has the
    same miss. The "no new modifications" expectation at `:999` then fails. If the implementer
    follows `:1002-1006`, `git commit -am "style: format"` sweeps the doc line into a formatting
    commit.
- **Evidence.**
  - `git ls-files -s CLAUDE.md AGENTS.md` gives `120000 … CLAUDE.md` (a symlink) and
    `100644 … AGENTS.md`.
  - `ls -la CLAUDE.md` gives `CLAUDE.md -> AGENTS.md`.
  - On the dry-run copy, the Step 13b `CLAUDE.md` edit wrote into `AGENTS.md:195`. With
    `AGENTS.md` absent, the same edit failed with `FileNotFoundError: 'CLAUDE.md'`.
  - In a scratch repo with the same symlink, appending through `CLAUDE.md` and then running
    `git add CLAUDE.md` left `git status --short` at ` M AGENTS.md`, unstaged.
  - The anchor text is at `AGENTS.md:192`.
- **Fix.**
  - Add `AGENTS.md` to the `FILES:` line at plan `:11`, beside `CLAUDE.md` or in place of it.
  - Edit `AGENTS.md` by name in the Step 13b script.
  - Change the Step 13 commit to `git add platformio.ini AGENTS.md`, and add `AGENTS.md` to
    Step 14's `git add` list.
  - Add one sentence saying that `CLAUDE.md` is a symlink to `AGENTS.md`, so the doc change shows
    up in the diff as `AGENTS.md`.

## MAJOR 1 — Step 3's scoping proof compiles nothing, so the PR's compile lines cannot be produced

- **Claim.** `touch src/main.cpp lib/expat/xmlparse.c`, followed by a verbose release build, prints
  both compile lines, and the counts come out `1` then `0` (plan `:342-351`). Spec testing step 2
  requires that "the PR also shows one owned and one not-owned compile line from `pio run -v`",
  and PR body item 3 (`:1028-1029`) depends on it.
- **Problem.** SCons does not rebuild a target just because its source was touched.
  - SCons decides by content, and PlatformIO never overrides the decider.
  - Just before this build, the red census built `.pio/build/x4pro-gh_release` with identical
    flags, under the same `PLATFORMIO_EXTRA_SCRIPTS`.
  - The release env's flags do not vary per commit. `git_branch.py:85` injects the SHA into
    `BEREAN_VERSION` for `x4pro` only.
  - So the verbose build is up to date: no compile lines, both counts `0`, and the fallback
    `grep 'xmlparse\.c\.o'` finds nothing either.
  - Deleting the objects alone is not enough. The verbose build uses the ini's
    `build_cache_dir = .cache` (`platformio.ini:3`). If that cache holds a matching object, the
    object is retrieved from the cache and not compiled, and no command line is printed.
- **Evidence.**
  - `grep -rn Decider …/site-packages/platformio/` finds nothing.
  - SCons 4.8.1 falls back to the default environment's decider
    (`SCons/Environment.py:1116-1119`), which is `content`/`MD5` (`:1642-1646`).
  - `platformio/builder/main.py:145` calls `env.CacheDir("$BUILD_CACHE_DIR")`.
  - An empirical check with PlatformIO's own `tool-scons/scons.py`: build, then `touch` the
    source, then build again. The second run printed `scons: '.' is up to date.`
- **Fix.** Replace the `touch` line so the two objects are deleted and the cache is bypassed:

  ```bash
  rm -f .pio/build/x4pro-gh_release/src/main.cpp.o
  find .pio/build/x4pro-gh_release -name 'xmlparse.c.o' -delete
  CACHE="$(mktemp -d)"
  PLATFORMIO_BUILD_CACHE_DIR="$CACHE" PLATFORMIO_EXTRA_SCRIPTS="pre:$PWD/scripts/enable_repo_warnings.py" <pio-locked.sh> run -e x4pro-gh_release -v > "$SCRATCH/verbose.log" 2>&1
  rm -rf "$CACHE"
  ```

  Keep the two `grep -c` checks. The `xmlparse` grep can then take its path from the `find`
  output.

## MINOR 1 — the Step 15 claim about release-please is false for a `build:` title

- **Claim.** "The PR title is conventional, so release-please picks it up … `build: enable -Wall
  for repo sources and fix its warnings`" (plan `:1019-1020`).
- **Problem.** `build` is a `hidden: true` changelog section in `release-please-config.json`. A
  squash with this title gets no changelog entry and cuts no release. The releasable types here
  are feat, fix, perf, refactor and docs. A behaviour-preserving change may reasonably ship with
  no release, but the sentence tells the implementer the opposite.
- **Fix.** Either keep `build:` and reword the sentence to "valid for the title lint;
  intentionally non-releasing", or use `fix:` if these changes should appear in the next release
  notes. The branch name is `fix/106-…`.

## MINOR 2 — `$SCRATCH` does not survive between shell invocations

- **Problem.** Step 0 sets `SCRATCH=…` once (plan `:67-68`), and every later step calls
  `"$SCRATCH/census.sh"` or `"$SCRATCH/step-build.sh"`. An agent implementer's shell state does not
  persist between calls. In a fresh shell these expand to `/census.sh` and fail with
  "No such file".
- **Fix.** State in the ground rules that every command block begins with
  `SCRATCH=<scratchpad dir>`, or write the two helpers to a fixed absolute path and call them by it.

## MINOR 3 — the Step 5 site count is wrong

- **Problem.** The text says "This is 33 sites" (plan `:484`). Its own breakdown,
  23 + 4 + 3 + 1 + 1, is 32, and the `SITES` list has 32 entries. `spike.w` agrees: 23 reader
  locals, 43 A5 sites in total.
- **Fix.** Change the number to 32.

## Not findings

- **Steps 4-12 do not run a red build first.** They rely on Step 3's red census, which reproduces
  every warning they fix, and each step's check recompiles the edited TUs. That is an adequate
  failing-test-first shape for a warning cleanup.
- **Every commit leaves the tree building.** The middleware stays unregistered until Step 13, as
  C4 requires.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
