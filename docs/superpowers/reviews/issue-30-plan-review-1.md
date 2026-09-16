# Plan review pass 1 — issue #30, the launcher's clean refresh on wake

**Reviewed:** `docs/superpowers/plans/2026-09-16-issue-30-plan.md`
**Against:** `docs/superpowers/specs/2026-09-16-issue-30-design.md` (rewritten at `5faf597f`)
**Prior:** `docs/superpowers/reviews/issue-30-plan-review-0.md` (`VERDICT: BLOCKER`, 1/1/4)
**Date:** 2026-09-16

Everything below was checked against the worktree at `5faf597f`. I applied the plan's three source
edits to copies under the scratchpad and ran them through `.venv/bin/clang-format 21.1.8`; I created
`test/launcher_refresh/` and `src/activities/launcher/LauncherRefresh.h` in the worktree, configured
and built the real host suite, ran it, and then deleted all three plus `build/`. `git status --short`
is empty apart from this file.

## The six pass-0 findings, checked one at a time

| Pass 0 | Status |
|---|---|
| BLOCKER 1 — plan reversed spec A10 while the spec said the opposite | **Applied.** All four spec sites are rewritten: A10 (`spec.md:170-204`), Architecture (`spec.md:215` "Three edits to existing files, one new header, and one new host suite"), Testing (`spec.md:385` "**Host.** `test/launcher_refresh`, per A10"), Non-goal (`spec.md:85-86`, now scoped to a harness "that constructs an activity"). `grep -in "no host test\|no new file\|Host.*None"` over the spec returns only the two historical narrations at `:171` and `:490`. The documents agree. |
| MAJOR 1 — the "not reproducible" sentence reached neither issue nor PR | **Applied.** `plan.md:513-546` is step 7c, with the verbatim `gh issue comment 30` body, the PR-description reuse instruction, and the CLAUDE.md rule-2 approval gate including the "say so in the hand-back if approval is withheld" branch. |
| MINOR 1 — snippets not formatter-clean | **Half applied.** The three source snippets are now byte-exact formatter output (verified below). The *new test file* is not. See MINOR 1. |
| MINOR 2 — bare `git commit` | **Applied.** All five commits (`plan.md:207,264,342,402,466`) are `git commit -F - <<'EOF'`. |
| MINOR 3 — `pio check` never run | **Applied.** `plan.md:450` runs the exact command `.github/workflows/ci.yml:95` runs, and `plan.md:481-483` adds it to the claimable list. |
| MINOR 4 — impossible C++ error in triage | **Applied.** `plan.md:443-444` now names a missing/mistyped `LauncherRefresh.h` include, which does surface as "file not found". |

## What I verified rather than assumed

- **Every line anchor the plan names is the line it claims.** `LauncherActivity.h:25-26` is the
  constructor, `:92` is `bool hasResume = false;`, `:93` the closing `};` (the file is 93 lines).
  `LauncherActivity.cpp:7-8` are `<HalClock.h>` / `<HalStorage.h>`, `:22-23` the two quoted activity
  includes, `:39` `constexpr const char* MODULE = "LAUNCH";`, `:473` `renderer.displayBuffer();`.
  `ActivityManager.cpp:247-255` is `goHome` verbatim as quoted. `test/CMakeLists.txt` ends at line
  110 with `add_subdirectory(catalog_stamp)`. `platformio.ini:170` is `-DLOG_LEVEL=2` and `:188` is
  `-DLOG_LEVEL=1`; `Logging.h:57-60` gates `LOG_DBG` on `LOG_LEVEL >= 2`, so "ships as nothing" holds.
  `GfxRenderer.h:189` is `void displayBuffer(HalDisplay::RefreshMode = HalDisplay::FAST_REFRESH)`.
  `HomeActivity.cpp:306,308-311` is the reference paint plus the `requestUpdate()` A3 forbids.
- **All three source edits are byte-for-byte what `clang-format 21.1.8` leaves.** I applied
  `plan.md:236-237`, `:244-251`, `:283-285`, `:293-295`, `:309-315` and `:375-383` to copies of the
  real files and diffed against the formatter: no change in any of the three. `IncludeBlocks:
  Regroup` (`.clang-format:152`) leaves both inserted includes exactly where the plan puts them.
- **Step 4's verification gate is exact.** After the replacement,
  `grep -n cleanInitialRefresh src/activities/ActivityManager.cpp` returns precisely lines 247 and
  254 — the two hits `plan.md:395-396` predicts — and no `(void)cleanInitialRefresh`.
- **Step 0's baseline is genuinely green.** `cmake -S test -B build/test && cmake --build build/test
  && ctest -j` → `100% tests passed out of 547` (543 pre-existing plus the 4 new). An implementer will
  not be halted by `plan.md:71-72`.
- **Step 1's red is the exact message quoted.** With only the test and CMake files present, the build
  fails with `fatal error: 'activities/launcher/LauncherRefresh.h' file not found` — character for
  character `plan.md:159`.
- **Step 1's green is the exact result quoted.** After adding the plan's `LauncherRefresh.h`,
  `./build/test/launcher_refresh/LauncherRefreshTest` prints `[  PASSED  ] 4 tests.` The binary path
  in `plan.md:38,194` is right (no `CMAKE_RUNTIME_OUTPUT_DIRECTORY` is set anywhere under `test/`, so
  each suite lands in its own subdirectory), and `crosspoint_test_common`
  (`test/CMakeLists.txt:37-46`) supplies `-Wall -Wextra -pedantic` — the header and test compile
  warning-free under them.
- **The rest of the tree is already formatter-clean**, so step 6's "expect no changes" would be true
  but for MINOR 1: `clang-format --dry-run -Werror` over every file `bin/clang-format-fix` selects
  exits 0.
- **Spec coverage is complete.** A1→step 3 (`HALF_REFRESH`), A2→step 3, A3→`plan.md:319-324` by name,
  A4→step 2, A5→step 4, A6→`plan.md:328-329` by name, A7→step 7d, A8→`plan.md:325-327` with the
  polarity spelled out, A9→step 3, A10→step 1, A11→nothing to do. Spec Testing's five serial checks
  are `plan.md:498-509` items 1–5; the optical caveat is item 6 plus 7b. Open question 2's "carry
  this into the issue and the PR description" is step 7c. Both Non-goals that survive as live
  temptations (`main.cpp:453-456`, `BootResume::Silent` at `main.cpp:566`) are step 7d.
- **Signatures are consistent across every appearance.** `constexpr bool launcherNeedsCleanPaint(bool,
  bool)` is identical in the test (`plan.md:99`), the header (`plan.md:181`), the call site
  (`plan.md:309`) and both spec copies (`spec.md:175,238`). Member names `cleanInitialRefresh` /
  `firstRenderDone` are identical in step 2 and step 3. `goHome`'s signature is untouched, so
  `ActivityManager.h:93`'s default arguments and `Activity::onGoHome`'s call sites need no edit.
- **No placeholders, no "adjust as needed", no step that says what to do without showing it.** Every
  edit is a quoted before and a quoted after.

---

## MINOR 1 — pass-0 MINOR 1 is only half applied: the new test file is not formatter-clean

**Claim.** `plan.md:12-14`: "Every code block has been run through `clang-format 21.1.8` with this
repo's `.clang-format` and is what the formatter leaves, so no step commits source that step 6 will
rewrite." `plan.md:570-571` repeats it as the fix for pass-0 MINOR 1, and `plan.md:461-462` acts on it
("Every snippet in this plan is already formatter-clean, so expect no changes"). `plan.md:6` claims
"all six findings applied".

**Problem.** The claim is true of the three source snippets and false of
`test/launcher_refresh/LauncherRefreshTest.cpp` (`plan.md:84-116`). `.clang-format:80` is
`AllowShortFunctionsOnASingleLine: All`, so the two single-statement `TEST` bodies collapse onto one
line each. Step 1 therefore commits source that step 6 rewrites — exactly the condition
`plan.md:13-14` says does not occur — and step 6 grows the `style: clang-format` commit that
`plan.md:461-462` says to expect not to need. Harm is bounded (one extra commit; CI only checks the
final tree), but the plan's headline verification claim is the thing an implementer trusts, and this
was a named pass-0 fix reported as done.

**Evidence.** `.venv/bin/clang-format --style=file:.clang-format` on `plan.md:84-116` verbatim:

```
-TEST(LauncherRefresh, FirstPaintOfAFlaggedEntryNeedsACleanPaint) {
-  EXPECT_TRUE(launcherNeedsCleanPaint(true, false));
-}
+TEST(LauncherRefresh, FirstPaintOfAFlaggedEntryNeedsACleanPaint) { EXPECT_TRUE(launcherNeedsCleanPaint(true, false)); }

-TEST(LauncherRefresh, LaterPaintsOfAFlaggedEntryDoNot) {
-  EXPECT_FALSE(launcherNeedsCleanPaint(true, true));
-}
+TEST(LauncherRefresh, LaterPaintsOfAFlaggedEntryDoNot) { EXPECT_FALSE(launcherNeedsCleanPaint(true, true)); }
```

Both joined forms are 119 and 109 columns, inside `ColumnLimit: 120` (`.clang-format:133`). The
single-line form is the repo's own house style under this config: `grep -c "^TEST(.*) { .*; }$" `
over `test/` finds 29, e.g. `test/ota_version/OtaVersionTest.cpp:28` and
`test/tag_palette/TagPaletteTest.cpp:36`. The other two tests
(`AnUnflaggedEntryNeverDoes`, `ResolvesAtCompileTime`) have multi-statement bodies and are unchanged.

**Concrete fix.** Replace those two blocks in `plan.md:98-104` with the `+` forms above, and leave
`plan.md:12-14`, `:461-462` and `:570-571` as they stand — they become true.

---

## MINOR 2 — the serial line the hand-back tells the tester to look for is not the line the firmware prints

**Claim.** `plan.md:499-500`: "Expect `[DBG] LAUNCH Paint: clean=1 firstPaint=1 mode=HALF`". Same
string in the spec at `spec.md:402-403`.

**Problem.** `LOG_DBG` routes through `logPrintf`, whose prefix is
`snprintf(c, sizeof(buf), "[%lu] [%s] [%s] ", ms, level, origin)`
(`lib/Logging/Logging.cpp:47`). The device emits
`[123456] [DBG] [LAUNCH] Paint: clean=1 firstPaint=1 mode=HALF` — a millisecond field the plan omits
and brackets around `LAUNCH` the plan drops. `scripts/debugging_monitor.py:307` rewrites only the
leading `^\[\d+\]` to a wall-clock stamp; it does not touch `[DBG] [LAUNCH]`. A tester who greps or
diffs against the quoted string finds nothing and has no way to tell "the log format differs" from
"the flag never arrived", which is the one thing steps 7b.1–7b.4 exist to distinguish.

**Evidence.** `lib/Logging/Logging.cpp:76-90` (`logPrintf`), `lib/Logging/Logging.h:57-58`
(`LOG_DBG` → `logPrintf("DBG", origin, format "\n", ...)`), `scripts/debugging_monitor.py:307`.

**Concrete fix.** In `plan.md:499-500` and `plan.md:502,504,506`, quote the real shape once and match
on the stable tail thereafter, e.g. "Expect `[DBG] [LAUNCH] Paint: clean=1 firstPaint=1 mode=HALF`
(the monitor prefixes a wall-clock stamp); grep for `Paint:`." Mirror it in `spec.md:402-403,407,410,413`.

---

## MINOR 3 — the spec and the plan give different comment text for the same two members

**Claim.** `plan.md:10` tells the implementer "Read the spec first", and `plan.md:11-12` says the plan
is the exact source. Both documents then print the member declaration block.

**Problem.** They do not match. `spec.md:254-258`:

```cpp
  // Set when the panel is still showing a frame this activity did not draw --
  // the sleep screen, after a wake with no Quick Resume frame. The first paint
  // must then be non-differential; every later one is an ordinary fast refresh.
```

`plan.md:246-248`:

```cpp
  // Set by the wake path when the panel is still showing a frame this activity
  // did not draw: the sleep screen, after a wake with no Quick Resume frame.
  // Consumed by the first paint only -- see LauncherRefresh.h.
```

Nothing of substance turns on it, and the plan wins by its own precedence rule, but an implementer
reading both has to notice the plan is authoritative for a block the spec also presents as code.
(`LauncherRefresh.h` is byte-identical in both, `spec.md:224-240` vs `plan.md:167-183`, so this is the
only divergence.)

**Concrete fix.** Paste `plan.md:246-248` over `spec.md:254-256`, or drop the comment from the spec
snippet and add "comment text is in the implementation plan", matching how `spec.md:299` already
defers the test file.

---

## MINOR 4 — three line citations in step 7 are off by one

**Claim.** `plan.md:501` cites `LauncherActivity.cpp:124,136,219` for the `LAUNCH` lines that are not
`Paint:`. `plan.md:551` cites `src/main.cpp:453-456` for the stale comment A7 defers.

**Problem.** `LauncherActivity.cpp:219` is `return {};`; the `LOG_DBG(MODULE, "No cover thumbnail for
%s", ...)` is at `:218`. `src/main.cpp:453` is a blank line; the comment runs `:454-457`. (`:124` and
`:136` are correct — the meeting-publication `LOG_INF` and the generated-thumbnail `LOG_INF`.) Both
slips are inherited from `spec.md:405` and `spec.md:74,151`, and the spec carries a third of the same
kind at `spec.md:328`, which cites `LauncherActivity.cpp:544,548` for the button-press
`requestUpdate()` calls that are at `:543` and `:547`. The repo's evidence rule
(CLAUDE.md, Agent rules) is a file *and a line* that has been read, and pass-0 spec review MINOR 4
already swept this spec once for exactly this class of slip.

**Evidence.** `sed -n '217,221p' src/activities/launcher/LauncherActivity.cpp`,
`sed -n '451,458p' src/main.cpp`, `sed -n '541,549p' src/activities/launcher/LauncherActivity.cpp`.

**Concrete fix.** `plan.md:501` → `:124,136,218`; `plan.md:551` → `src/main.cpp:454-457`; and the
matching corrections at `spec.md:405`, `spec.md:74,151` and `spec.md:328`.

---

## Not findings, recorded so pass 2 does not re-derive them

- **Steps 2, 3, 4 and 6 have no automated test and the plan says so up front** (`plan.md:54-55`).
  With A10's host-testable unit already carved out, what is left is an activity constructor, an
  include and a `render` tail, none of which is reachable without the activity harness
  `spec.md:85-86` rules out. The gate is the compiler plus step 7's serial checks. Correct as
  declared.
- **Steps 2–4 commit without a build.** `plan.md:258,336` say so deliberately, and it is CLAUDE.md's
  own rule ("build once after the last code edit … do not repeat a target that already passed").
- **Step 5 cannot fail.** The host suite compiles none of the files steps 2–4 touch. It costs seconds
  and re-proves the tree configures; harmless.
- **`const bool` member and non-assignability.** `LauncherActivity` has exactly one construction site
  (`ActivityManager.cpp:254`, `std::make_unique` into a `std::unique_ptr<Activity>`) and is never
  assigned, so A4's `const` member is safe and step 2's default argument really does keep the tree
  building.
- **No unused-variable warning when `LOG_DBG` vanishes** at `LOG_LEVEL=1`: `cleanPaint` feeds `mode`
  and `mode` feeds `renderer.displayBuffer(mode)`.
- **`bin/clang-format-fix` reaches the new files at step 6** because they are tracked by then —
  `git ls-files --exclude-standard` (`bin/clang-format-fix:44`) lists tracked files only, and steps 1
  and 3 commit them first. Had step 6 run before those commits it would have skipped them silently.
- **Step 6's conditional `style: clang-format` commit is the right shape**: `git add -A src test`
  covers both trees the plan writes to, and the plan tells the implementer to check `git status
  --short` against `.gitignore` first.
- **A10's recorded cost is real and correctly recorded.** The `? HALF_REFRESH : FAST_REFRESH` mapping
  at `plan.md:310` is not covered by any test, which `spec.md:194-195` states in those words. That is
  a settled trade, not a gap.

---

The plan is executable literally. I ran the only two steps that have an automated gate — step 0's
baseline and step 1's red-then-green — against the real tree and got exactly the output the plan
quotes, and I ran the other three edits through the formatter and got no diff. Every spec assumption
lands in a numbered step, every signature is identical at every appearance, and the two documents no
longer disagree about scope. The four findings are text corrections to the plan (and, for two of
them, the same correction to the spec); none reverses a decision, changes scope, or needs the human.

BLOCKERS: 0
MAJORS: 0
MINORS: 4

VERDICT: CLEAR
