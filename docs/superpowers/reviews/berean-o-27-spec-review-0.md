Tier: light

# berean-o-27 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-11-22-berean-o-27-design.md` against the brief (`hpipe brief --task t3`)
and `docs/superpowers/research/2026-11-22-berean-o-27-research.md`, at `8f8fd9a8`.

## What I verified

- All five sites read as the spec describes: `PublicationsActivity.cpp:97-102`, `HomeVerseDoc.cpp:133`,
  `PreachingSession.cpp:122-127`, and `GridMarks.h:21-23` (which includes only `<cstdint>` among the standard headers, `:6`).
- The cited models exist and have the claimed shape: `PassageLinksActivity.cpp:6-7,28-30`
  (`reserve` + `std::transform` + `back_inserter`), `BookPathIndex.cpp:7-8,25-26` (`std::copy_if` + lambda),
  `EndOfBookOptions.cpp:99-101`, `CatalogSearchActivity.cpp:400-403` (a reason, then `// cppcheck-suppress <id>`).
- `PreachingSession.h:45` documents the `activeIdsByName` order. `GridMarksTest.cpp:59-65` covers `clear()`.
  `HomeVerseDocTest.cpp:173-179` covers the `doc["v"] | 0` path, including a missing `v`.
- `platformio.ini:24` has `--inline-suppr` and `--suppress=unmatchedSuppression`. `ci.yml:76` matches.
- A2/A3 hold. The lambdas return `std::string` by value, and `back_inserter` then calls `push_back(T&&)`. That is one
  copy plus a non-allocating move, the same allocation count as the loop's `push_back(const T&)`. `reserve` is unchanged.
- **I applied the spec's five changes exactly as written to scratch copies of the four files and ran cppcheck on them.**
  The method is in M1. On the base files, the bundled `Cppcheck 2.11` with pio's exact per-file flags and defines, minus
  `--includes-file`, reports exactly CI's five lines: `PublicationsActivity.cpp:99`, `:102`, `HomeVerseDoc.cpp:133`,
  `PreachingSession.cpp:126`, `GridMarks.h:22` (rc 3). On the changed files it reports nothing (rc 0), so there are
  no new findings either. A full `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
  on a project copy with the changes applied also prints `No defects found`, exit 0. The design is sound.

## MAJOR

### M1 — The red→green test is not red locally: `pio check` already passes on the unchanged base

**Claim.** Testing strategy: the check "exits 1 with `Total 0 0 5` before the change (research note) and must exit 0
after it. This is the test that fails first." The Problem section also says local cppcheck "reports the same five, so a
local run reproduces CI exactly". It cites the research note's "The local check reproduces CI exactly" paragraph.

**Problem.** That is not true in this worktree now. The planned red step will not go red, so the green step proves
nothing. An implementer following the spec gets "exit 0 after" even for an incomplete fix. Either they are confused by the
missing red, or they ship on a check that never exercised the findings.

**Evidence.** I ran it on the clean worktree at `8f8fd9a8`, which has no source changes from `6fb52ff5`:

    $ ~/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high
    No defects found
    ========================= [PASSED] Took 16.09 seconds =========================
    exit 0

`pio check -v` shows the four files are analysed (`Checking .../PublicationsActivity.cpp ...` and the others). The
findings depend on include resolution. pio runs one cppcheck per file, with `--includes-file=<tmp>`. With that file, the
types resolve (`JsonVariantConst::operator|`, the element types of `recents`, `entries_` and `byName`), and cppcheck
stays silent. With the same command minus `--includes-file`, all five appear exactly as CI prints them. CI's cppcheck job
evidently runs with includes unresolved. The `platformio.ini:21-23` comment says the same about a fresh CI checkout. That
also explains the ubuntu-24.04 green versus ubuntu-26.04 red better than "not established" does. A built worktree, or a
copy where `pio check` installs the libdeps, resolves them and goes green. I saw the same on a scratch project copy,
whose base was also `No defects found`. Whatever state the research run saw, it is not reproducible here now.

**Fix (inline in the spec).** Rewrite Testing's red→green as follows:
1. **Red→green that reproduces CI.** Run `pio check -v -e x4pro` once and take the cppcheck command line it prints for
   one `src/` file. Drop `--includes-file=...` and `--verbose`, and run it with the four touched files as arguments. Note
   that pio's printed line is not shell-safe: `-D` values contain spaces and parentheses, and zsh globs them. Rebuild the
   argument list rather than pasting it. Before the change, this prints the five findings with rc 3. After the change, it
   prints nothing with rc 0.
2. `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` must still exit 0. It is a
   regression guard with includes resolved, not the red test.
3. The CI cppcheck job on the PR is the final gate.

As the brief requires, the PR notes must say that local `pio check` does not reproduce the findings. Also correct the
Problem section's "a local run reproduces CI exactly", and the research note's matching paragraph.

## MINOR

### m1 — Research's root-cause paragraph should name include resolution, not leave it open

The research note says why the 24.04 run was green "is not established" and that it "does not matter for this fix". The
second half stands. The first is now answerable from M1's evidence: unresolved includes produce the findings, and
resolved includes hide them. Record that in one line so that the next time the cppcheck job "suddenly" fails, nobody
chases a cppcheck version again. Not a scope change, and nothing in `platformio.ini` or `ci.yml` needs to move.

VERDICT: CLEAR
