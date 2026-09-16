# Adversarial review — issue #38 implementation plan, pass 0

**Date:** 2026-09-16
**Target:** `docs/superpowers/plans/2026-09-16-issue-38-plan.md`
**Against:** `docs/superpowers/specs/2026-09-16-issue-38-design.md`
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/refactor-38-remove-qr-display` at `697fbdc3`

Every numeric claim in the plan was re-measured on this worktree before being
accepted or attacked. **All of them hold**, which is worth stating plainly
because it is most of the plan's surface area:

| Plan claim | Measured |
| --- | --- |
| completeness grep returns 13 lines | 13 ✓ (`EpubReaderMenuActivity.h:26`, `.cpp:77`, `EpubReaderActivity.cpp:39,828,832`, `QrDisplayActivity.cpp` ×6, `.h` ×2) |
| those 13 are exactly the lines steps 1-4 delete | 1+1+3+6+2 = 13 ✓, so 13 → 0 is arithmetically closed |
| `QrUtils\|qrcode\.h` in `src` = 12, → 10 after | 12 ✓; 6 in `src/util/QrUtils.{h,cpp}`, 4 in `CrossPointWebServerActivity.cpp:20,445,463,483`, 2 in `QrDisplayActivity.cpp:9,41` ✓ |
| `./scripts/i18n_orphans.sh` = 22 | 22 ✓ |
| `EpubReaderMenuActivity.cpp:77` is the `push_back` | ✓ |
| `EpubReaderActivity.cpp:39` include, `:828-839` case, prior case ends `:826-827` | ✓ all four exact |
| `EpubReaderMenuActivity.h:26` = `DISPLAY_QR,`; comment at `:53-56`; `MAX_MENU_ITEMS` at `:57` | ✓, and the quoted "before" comment text is byte-identical |
| 11 unconditional + 5 conditional rows = 16, not the comment's 18 | ✓ counted in `buildMenuItems` (`EpubReaderMenuActivity.cpp:44-81`) |
| `USER_GUIDE.md:140` is the bullet; `:225` is the web server's | ✓ |
| no `-Wall`/`-Werror` in `platformio.ini` | `grep -cE '\-Wall|\-Werror'` → 0 ✓ |
| no `build_src_filter` | ✓ |
| `getTextFromSectionFile` survives at `EpubReaderActivity.cpp:1778` | ✓ |
| host suite = 543 tests | `ctest -N` → 543 ✓ |
| `ninja` absent; `build/test` uses the default generator | `CMAKE_GENERATOR:INTERNAL=Unix Makefiles` ✓ |
| 31 of 32 translation YAMLs define `STR_DISPLAY_QR` | ✓ |

Two structural claims also check out and are load-bearing:

- **The TDD argument is sound, not an excuse.** `test/CMakeLists.txt` has zero
  occurrences of `src`; the only firmware sources any host target compiles are
  `src/network/{MeetingFilename,WolWeekScan,MeetingWeekTable,PubMediaJson}.cpp`
  and `src/util/StringUtils.cpp` (`test/*/CMakeLists.txt`). Nothing under
  `src/activities/` is host-compiled, and `buildMenuItems` depends on
  `Frontlight`, `StrId` and `freeink::ui::ListItem`. A host test for this
  deletion would be a shim testing a shim. Accepted.
- **The gate-2 ordering rationale is correct.** `scripts/gen_i18n.py:1001` calls
  `main(strip_unused=True)` when run as the PlatformIO `pre:` script
  (`platformio.ini:131`), and `scripts/i18n_orphans.sh` greps `src lib` including
  the generated `lib/I18n/I18nKeys.h`, which today still carries
  `STR_DISPLAY_QR` at `:420`. Running the orphan gate before `pio run` really
  would report 22 and read as "no orphan added". The plan's "build first" is
  load-bearing and stated.

What follows is what the plan gets wrong. Two MAJOR, seven MINOR, no BLOCKER.

---

## MAJOR 1 — Spec assumption A4's only action, "open a follow-up issue", maps to no step and no hand-back line

**Claim.** The plan asserts it carries the spec whole: "Everything you need is
below, in full" (plan:9-10), and its "What you must NOT do" section
(plan:501-520) enumerates the spec's decisions so that "reversing one silently"
cannot happen.

**Problem.** A4 is the only spec assumption that attaches a *deliverable* to
this change rather than a prohibition, and it is the one assumption the plan
never mentions. `grep -n "follow-up\|follow up\|A4"` over the plan returns
nothing. A1, A5, A6 and A7 are each cited by name; A4 is not. Executed
literally, the plan closes #38 with the latent unbounded write recorded nowhere
outside two superpowers documents that no issue tracker sees.

**Evidence.**
- Spec `2026-09-16-issue-38-design.md:148-157` — A4: "The latent unbounded write
  in `ricmoo/QRCode` is recorded, not fixed. … *Decision:* out of scope; **open a
  follow-up issue** rather than widen this one."
- Spec `:71-73` (Non-goals) — "Fixing the version ladder or the unbounded write
  in the library. … See A4." So the non-goal is explicitly conditional on A4's
  follow-up existing.
- Plan — no occurrence of `A4`, `follow-up` or `follow up`. Plan:506-519 cites
  "Spec A1" twice, "Spec A6", and A5/A7 by content.

**Fix.** Add a numbered item to "Hand-back" (plan:549-576), between items 3 and
4, and do *not* ask the implementer to file it — `CLAUDE.md` ("Git workflow",
rule 2) forbids remote writes without explicit approval:

> N. **A follow-up issue is owed and you must not open it.** Spec A4 leaves a
> known unbounded write in the vendored QR library: `bb_appendBits`
> (`.pio/libdeps/x4pro/QRCode/src/qrcode.c:209-215`) writes without consulting
> `BitBucket::capacityBytes`, and the version ladder at
> `src/util/QrUtils.cpp:28-32` uses thresholds that are not byte-mode
> capacities. After this change every surviving caller passes 20-50 bytes
> (`CrossPointWebServerActivity.cpp:445,463,483`), so nothing overflows today.
> Hand the human a title and body for the issue and let them file it.

---

## MAJOR 2 — Step 3's "Before" check pairs a command with an expectation that command cannot produce, and the plan's own halt rule turns that into a stop

**Claim.** Step 3 (plan:249-255): "**Before (both files must exist):**
`ls -l src/activities/reader/QrDisplayActivity.h src/activities/reader/QrDisplayActivity.cpp`
— Expect 20 and 47 lines respectively."

**Problem.** `ls -l` prints byte counts, not line counts. The implementer runs
the given command and sees `500` and `1516`. Nothing on screen says 20 or 47.
The plan opens with "Do not improvise: if a step's 'expect' line does not match
what you see, stop and say so rather than adjusting the code until it does"
(plan:10-11) — so a literal executor, which is exactly the reader this plan
declares ("You have no context for this codebase beyond the spec and this
file", plan:9), halts at step 3 of 6. A less literal one shrugs and proceeds,
which is the habit the plan is trying to suppress everywhere else.

**Evidence.**
```
$ ls -l src/activities/reader/QrDisplayActivity.h src/activities/reader/QrDisplayActivity.cpp
-rw-r--r--@ 1 stein staff 1516 Sep 16 01:46 src/activities/reader/QrDisplayActivity.cpp
-rw-r--r--@ 1 stein staff  500 Sep 16 01:46 src/activities/reader/QrDisplayActivity.h
$ wc -l src/activities/reader/QrDisplayActivity.h src/activities/reader/QrDisplayActivity.cpp
      20 src/activities/reader/QrDisplayActivity.h
      47 src/activities/reader/QrDisplayActivity.cpp
```
The 20/47 figures are right — they come from the spec's table
(`2026-09-16-issue-38-design.md:196-197`, "delete (20 lines)" / "delete (47
lines)"). Only the command is wrong.

**Fix.** Replace the command at plan:252 with

```sh
wc -l src/activities/reader/QrDisplayActivity.h src/activities/reader/QrDisplayActivity.cpp
```

and keep the expectation as "20 and 47 lines respectively" (note `wc -l` prints
`.h` first given that argument order, matching the stated order).

---

## MINOR 1 — Step 0's baseline commit is one commit stale, and the plan committed itself past it

**Claim.** Plan:99-102: run `git log --oneline -1`; "Expect, on `9aec9349`: 22
orphans, 13 QR code references, 12 `QrUtils`/`qrcode.h` references." Hand-back
repeats it: "Measured on this worktree at `9aec9349`" (plan:526).

**Problem.** The plan itself is `697fbdc3 docs: implementation plan for removing
Show page as QR (#38)`, so `git log --oneline -1` prints `697fbdc3`, never
`9aec9349`. Same class of spurious halt as MAJOR 2, one notch lower because the
"Expect" line binds the three counts, not the SHA — and all three counts are
unchanged at `697fbdc3` (re-measured: 22 / 13 / 12).

**Evidence.** `git log --oneline -1` → `697fbdc3 docs: implementation plan for
removing Show page as QR (#38)`; plan:102, plan:526.

**Fix.** Plan:102 → "Expect, at `697fbdc3` or any later docs-only commit on this
branch: **22** orphans, **13** QR code references, **12** `QrUtils`/`qrcode.h`
references. The counts are the check; the SHA only dates them." Same
substitution at plan:526.

---

## MINOR 2 — "Seven edits across five files" undercounts both halves

**Claim.** Plan:23-24: "Seven edits across five files, in an order where every
intermediate commit compiles."

**Problem.** Six paths are touched and eight changes are made. Four files are
edited (`EpubReaderMenuActivity.cpp` ×1, `EpubReaderActivity.cpp` ×2,
`EpubReaderMenuActivity.h` ×2, `USER_GUIDE.md` ×1) and two are deleted
(`QrDisplayActivity.{h,cpp}`). The spec's own table lists eight rows. A reader
who uses the count as a diff-size sanity check at review time will think a file
snuck in.

**Evidence.** Spec `2026-09-16-issue-38-design.md:195-203` (eight rows, six
paths); plan steps 1-5 (Step 2 has Edit A and Edit B, Step 4 has Edit A and Edit
B, Step 3 removes two files).

**Fix.** Plan:23 → "Eight changes across six files (four edited, two deleted)".

---

## MINOR 3 — Gate 1 pipes `pio run` through `tee`, which discards the build's exit status — the exact hazard the plan flags eleven lines later

**Claim.** Plan:410: `~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/issue38-build.log`.

**Problem.** In bash without `pipefail` the pipeline's status is `tee`'s, so a
failed build exits 0. The plan knows this failure mode — plan:423-424 warns
"Do not pipe to `wc -l`; the script is `set -euo pipefail` and the pipe discards
its exit status" — and then does the same thing to the one gate that matters
most. CI guards it explicitly (`.github/workflows/ci.yml`: `set -euo pipefail`
before `pio run -e ${{ matrix.env }} | tee pio.log`). Partly self-correcting,
because `grep -E "^(RAM|Flash):"` on the next line would print nothing on a
failed build — but that is a coincidence, not a gate.

**Evidence.** Plan:410 vs plan:423-424; `.github/workflows/ci.yml` build job,
`set -euo pipefail` immediately before the identical `tee`.

**Fix.** Plan:410 → 

```sh
set -o pipefail
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/issue38-build.log
```

---

## MINOR 4 — `pio check` is the only gate with no stated expectation, in a plan built entirely on stated expectations

**Claim.** Plan:446-447 gives the command and nothing else. Every other gate in
Step 6 says what to expect: "23 lines", "nothing", "exactly 10 lines", "543
tests, 100% passing".

**Problem.** An implementer who sees `pio check` emit its usual wall of
environment noise has no criterion, and a reader of the hand-back gets "pio
check results" (plan item 4, plan:566) with no baseline to compare against.
This is the gate the project's own memory singles out as the one people assume
`pio run` covers and it does not.

**Evidence.** Measured on this worktree at `697fbdc3`, before any edit:

```
Checking x4pro > cppcheck (board: esp32-s3-devkitc1-n16r8; ...)
No defects found
========================= [PASSED] Took 11.98 seconds =========================
x4pro          cppcheck  PASSED    00:00:11.981
EXIT=0
```

`pio check` resolves `default_envs = x4pro` (`platformio.ini:2`) with
`check_tool = cppcheck` and `--enable=all` (`platformio.ini:20,24`).

**Fix.** Append to plan:447: `# expect: "No defects found" / [PASSED], exit 0
(measured clean at 697fbdc3, 12 s)`. Worth one further line, because it is the
nearest defect class to this change: deleting a `case` *body* while leaving the
label and a trailing `break;` produces two consecutive breaks and trips
`[low:style] duplicateBreak`, which `--fail-on-defect low` turns into a CI
failure. Step 2 deletes the whole block including both breaks, so it is clean —
but the implementer should know why that gate is in the list.

---

## MINOR 5 — The spec's collision fallback for A5 is not carried into Step 4

**Claim.** Step 4 Edit B (plan:308-330) instructs the comment correction
unconditionally.

**Problem.** The spec makes it conditional: "A5 deliberately edits a comment in
the shared header; **if that collides, drop A5 rather than the deletion**"
(spec `:379-383`, Risks). Two other tasks are editing
`EpubReaderMenuActivity.{h,cpp}` — one owns the orientation rows, one the
translation YAMLs — and the plan states elsewhere that it must not touch them
(plan:516-517). The escape hatch for the one edit that *is* in contested
territory did not make the trip.

**Evidence.** Spec `:379-383`; plan:308-330 has no conditional; plan:516-517
acknowledges the other tasks in the same files.

**Fix.** Add after plan:330: "If this hunk conflicts with another task's edit to
the same comment when the branches meet, **drop Edit B and keep everything
else** — the comment correction is A5, a passing fix, and the deletion does not
depend on it (spec, Risks)."

---

## MINOR 6 — The plan says the spec "has been amended to match" both corrections; only one of the two was

**Claim.** Plan:27-28: "The spec cleared review, but two of its commands do not
work as written. This plan uses the corrected forms; **the spec has been amended
to match**."

**Problem.** True for correction 1 — the spec's Testing strategy carries the
filtered grep at `:316-317` and explains the exclusion at `:332-337`. False for
correction 2: the spec still prescribes `-G Ninja`, the form the plan proves
fails on this machine.

**Evidence.** Spec `:326`:

```sh
cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test
```

against plan:44-53 ("`-G Ninja` fails on this machine. Ninja is not installed").
Confirmed: `command -v ninja` → nothing; `build/test/CMakeCache.txt` has
`CMAKE_GENERATOR:INTERNAL=Unix Makefiles`.

Low impact — the plan supersedes the spec for the implementer and its own form
is right (verified working) — but the statement is false about a document still
in the pipeline, and the next reviewer will re-derive it.

**Fix.** Either amend spec `:326` to
`cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test -j 8 && ctest --test-dir build/test --output-on-failure -j 8`,
adding a line that CI keeps `-G Ninja` because its runner installs it
(`.github/workflows/ci.yml`, unit-tests job: `apt-get install -y cmake
ninja-build`), or soften plan:28 to "correction 1 has been applied to the spec;
correction 2 is local to this machine and the spec is left alone."

---

## MINOR 7 — Bare `-j` on `cmake --build` is the condition the plan blames for the flake it then tells you to ignore

**Claim.** Plan:453: `cmake --build build/test -j`, followed by plan:460-475
documenting a `gtest_discover_tests` 5-second-timeout failure "losing the race
on first launch under `-j`" and instructing "run it again before investigating".

**Problem.** With the Unix Makefiles generator — which is what this tree is
configured with — bare `-j` means *unbounded* parallelism, so the plan
reproduces its own documented flake's precondition and then normalises retrying
through it. A retry instruction next to a gate is corrosive: the one thing this
gate must not do is teach the implementer that a red result is routine.

**Evidence.** `build/test/CMakeCache.txt`: `CMAKE_GENERATOR:INTERNAL=Unix
Makefiles`; plan:453 vs plan:460-475. CI does not hit it because it builds with
a bare `cmake --build build/test` (`.github/workflows/ci.yml`, unit-tests job).
Impact is small here: `build/test` is already configured and warm, and a pure
deletion touching nothing under `test/` relinks no test binary, so
`gtest_discover_tests` should not re-run at all.

**Fix.** Plan:453 → `cmake --build build/test -j 8`, and reword plan:460 from
"run it again before investigating" to "this is a known `gtest_discover_tests`
launch-timeout flake on a *cold* tree; `build/test` is already warm and a
deletion under `src/` relinks no test binary, so it should not appear. If it
does, rerun once — if it survives a warm rerun, stop and report."

---

## Checked and found sound (not findings)

Recorded so the next pass does not re-derive them.

- **Every spec "What goes" row maps to a step.** Spec `:195-203` → Step 1
  (`.cpp:77`), Step 2 A/B (`EpubReaderActivity.cpp:39`, `:828-839`), Step 3
  (both files), Step 4 A/B (`.h:26`, `.h:53-56`), Step 5 (`USER_GUIDE.md:140`).
  All eight. Every spec gate maps to a Step 6 numbered gate, and all four CI
  gating jobs (`build`, `clang-format`, `cppcheck`, `unit-tests`) are covered.
- **Step ordering is required and correct.** Step 3 must follow Step 2 or
  `EpubReaderActivity.cpp:39` loses its header; Step 4 must follow Steps 1 and 2
  or the enumerator still has a producer and a consumer. The plan's order is the
  only valid one and each step's "why it still compiles" paragraph is accurate.
- **No naming or signature drift**, because nothing is introduced. Every
  identifier in the plan (`MenuAction::DISPLAY_QR`, `StrId::STR_DISPLAY_QR`,
  `QrDisplayActivity`, `MAX_MENU_ITEMS`, `getTextFromSectionFile`) was checked
  against the tree and every quoted code block is byte-identical to what is
  there, including the 12-line `case` body and the four-line `MAX_MENU_ITEMS`
  comment.
- **The A5 arithmetic is right in both directions.** 11 unconditional rows today
  (SELECT_CHAPTER, TOGGLE_BOOKMARK, TEXT_SETTINGS, NIGHT_MODE, ROTATE_SCREEN,
  AUTO_PAGE_TURN, GO_TO_PERCENT, SCREENSHOT, DISPLAY_QR, GO_HOME, DELETE_CACHE)
  + 5 conditional = 16; the comment says 13/18; after the deletion 10/15, which
  is what the replacement text says.
- **The orphan prediction of 23 is mechanically justified**, not assumed — see
  the gen_i18n `strip_unused=True` chain above.
- **`git rm` in Step 3 stages its own deletion**, so the commit at plan:277 with
  no `git add` is correct.
- **The whole-tree formatter at gate 6 will not smuggle in unrelated churn.**
  Ran `clang-format --dry-run -Werror` over exactly the wrapper's file set
  (`bin/clang-format-fix:42-50`): zero diagnostics, the tree is format-clean at
  `697fbdc3`, so `git commit -am "style: clang-format"` has nothing unrelated to
  pick up. (`style:` is outside `CLAUDE.md`'s type list but has two precedents
  in the last 300 commits, and PRs are squash-merged so only the PR title reaches
  release-please. Not worth changing.)
- **`ctest … -j` with no value parses fine** under this machine's ctest 4.4.2,
  and matches what CI runs verbatim.
- **No stale artifact is left behind by the `USER_GUIDE.md` edit.**
  `scripts/generate_userguide_epub.py` writes `CrossPoint_User_Guide.epub`, but
  that file is untracked, absent from the worktree, and invoked by nothing in
  the build. No regeneration step is owed.
- **Nothing outside `src/`, `USER_GUIDE.md` and the superpowers documents
  mentions the feature.** Repo-wide grep for
  `DISPLAY_QR|Show page as QR|QrDisplayActivity` (excluding `.git`, `.pio`,
  `build`, `.cache`, `translations`) returns exactly the files the plan touches
  plus the four #38 documents. `test/` has zero references to
  `EpubReaderMenuActivity`, `QrUtils` or `QrDisplay`.
- **The "About TDD here" argument is accepted on the merits**, on the evidence
  above: the host suite compiles no `src/activities/` translation unit, and the
  grep gates genuinely close the one direction the compiler cannot
  (case removed, enumerator surviving). 13 → 0 is a complete cover of the
  reference set, which is a stronger assertion than most unit tests carry.

---

## Verdict rationale

No BLOCKER. Neither MAJOR reverses a spec decision, changes scope, or needs a
judgment only the human can make: MAJOR 1 is a missing hand-back line whose
content is fully specified by A4, and MAJOR 2 is a one-word command swap
(`ls -l` → `wc -l`) with the expected values already correct. All nine findings
are fixable inline by the implementer or the plan's author without re-opening
the spec.

Counts: 0 BLOCKER, 2 MAJOR, 7 MINOR.

VERDICT: CLEAR
