# Plan review pass 0 — issue #30, the launcher's clean refresh on wake

**Reviewed:** `docs/superpowers/plans/2026-09-16-issue-30-plan.md`
**Against:** `docs/superpowers/specs/2026-09-16-issue-30-design.md` (cleared, spec review pass 0)
**Date:** 2026-09-16

Everything in this review was checked against the tree at `cdde5082`, and the three code edits were
applied to scratch copies outside the worktree, compiled and run through `clang-format 21.1.8`. The
worktree is unchanged apart from this file.

## What holds up

Verified, not assumed:

- Every anchor line the plan names is the line it claims. `LauncherActivity.h:25-26` is the
  constructor, `:92` is `bool hasResume = false;` and `:93` is the last line of the file.
  `LauncherActivity.cpp:7-8` are `<HalClock.h>` / `<HalStorage.h>`, `:22-23` are the two quoted
  activity includes, `:39` is `constexpr const char* MODULE = "LAUNCH";` and `:473` is
  `renderer.displayBuffer();`. `ActivityManager.cpp:247-255` is `goHome` verbatim as quoted.
  `test/CMakeLists.txt` really ends at line 110 with `add_subdirectory(catalog_stamp)`.
  `.gitignore:13` is `build`. `platformio.ini:188` is `-DLOG_LEVEL=1`, and `Logging.h:57-60` gates
  `LOG_DBG` on `LOG_LEVEL >= 2`, so the "ships as nothing" claim is right.
- The step-1 helper compiles and behaves. I built the plan's exact `LauncherRefresh.h` against the
  plan's exact include set (`src`, `test/stubs`, `REPO_ROOT`, `REPO_ROOT/lib`) with
  `-Wall -Wextra -pedantic -std=c++20`; it compiles clean, and all four cases of the truth table pass
  as `static_assert`s. `<HalDisplay.h>` resolves to `test/stubs/HalDisplay.h:18-21` exactly as the
  plan predicts — `${REPO_ROOT}/lib` is on the path but `lib/hal` is not, and there is no
  `lib/HalDisplay.h`.
- Step 3's include insertions survive `IncludeBlocks: Regroup` (`.clang-format:152-170`) untouched:
  the formatter left both the angle block and the quoted block exactly where the plan puts them.
- Step 2's header edit and step 4's `goHome` replacement are byte-for-byte what `clang-format`
  produces — no diff on either.
- Step 4's verification gate is exact. After the replacement, `grep -n cleanInitialRefresh
  src/activities/ActivityManager.cpp` returns precisely lines 247 and 254, which is the "exactly two
  hits" the plan asks for.
- `LauncherActivity` has exactly one construction site outside its own directory
  (`ActivityManager.cpp:254`), so step 2's "the default argument keeps every existing construction
  site compiling" is true rather than hopeful.
- A1–A9 and A11 each land in a step, with A3 (no `requestUpdate()`), A6 (unconditional latch) and A8
  (the `firstPaint` polarity) called out by name at the exact place they can go wrong
  (`plan.md:343-355`). Signatures are consistent across steps: `launcherRefreshMode(bool, bool) ->
  HalDisplay::RefreshMode` is identical in the test (`plan.md:122-136`), the header
  (`plan.md:209-212`) and the call site (`plan.md:335`).

Two things I went looking for and did **not** find: no placeholder or "adjust as needed" step; and no
step that says what to do without showing the code.

---

## BLOCKER 1 — the plan reverses spec A10, and the spec still says the opposite

**Claim.** `plan.md:37-78` opens with "Change to the spec: A10 is amended, not followed" and adds
three new files: `src/activities/launcher/LauncherRefresh.h`, `test/launcher_refresh/*`, and a line
in `test/CMakeLists.txt`.

**Problem.** The spec says the opposite in three places, and none of them is amended:

- `spec.md:169-175` (A10) — "no host test is added … Testing a two-boolean ternary would mean
  extracting it into a free function the reference implementation does not have — inventing a
  pattern for one line."
- `spec.md:187-188` (Architecture) — "Three edits, two files plus one header. **No new class, no new
  file**, no allocation."
- `spec.md:323` (Testing) — "**Host.** None, per A10." And `spec.md:86` lists "A host-test harness
  for activities" as a Non-goal.

This is not an oversight the plan papered over — it is a decision that spec review pass 0 attacked
and explicitly affirmed: "A3, A2, A4, A6, A9, A10, A11 — attacked and sound as written. **A10 in
particular** … extracting a two-boolean ternary into a free function to test it would invent a
pattern the reference implementation does not have. The build plus A8's serial line is the right
verification budget for this change."
(`docs/superpowers/reviews/issue-30-spec-review-0.md:222-226`).

**Evidence.** The plan's counter-evidence is correct — I checked all three legs and they hold:

1. `test/return_stack/CMakeLists.txt:5-7` puts `${REPO_ROOT}/src` on a suite's include path and
   compiles `src/activities/reader/ReturnStack.h` with no activity, no renderer and no
   `HalStorage`. A10's premise ("none constructs an activity, because that needs the renderer…")
   is a non-sequitur: nothing here needs to construct one.
2. `test/stubs/HalDisplay.h:18-26` already declares `HalDisplay::RefreshMode { FULL_REFRESH,
   HALF_REFRESH, FAST_REFRESH }` in the same order as `lib/hal/HalDisplay.h:14-18`, and
   `test/pagination/CMakeLists.txt:26` is the established way to reach it. No stub work is needed.
3. `src/activities/reader/ReaderUtils.h:131` is an `inline bool` free function in an activity
   directory, so "a free-function header beside an activity" is a pattern the repo already has.

So the technical reversal is **sound**, and I would not ask for it to be undone on the merits. That
is not what makes this a blocker. What makes it a blocker is that the plan and the cleared spec now
contradict each other on scope, and `plan.md:8` tells the implementer to "Read the spec first". An
implementer who reads `spec.md:187` ("No new file") and `spec.md:323` ("Host. None") and then
`plan.md:105-173` has to decide which document wins — a scope call the pipeline reserves for the
human, especially since the previous review ratified A10 as written.

**Concrete fix (human decision, then a documentation edit):** ratify the amendment, then make the
two documents agree in the same change. Rewrite `spec.md:169-175` (A10) to state the new decision
and the three pieces of evidence above; change `spec.md:187-188` to "Three edits, two files, one new
header, plus one host suite"; change `spec.md:323` to name `test/launcher_refresh`; and drop or
re-word the Non-goal at `spec.md:86`, which after this change is only true of an *activity* harness.
Leave `plan.md:37-78` in place as the rationale record. If the human instead declines the amendment,
the plan collapses to the spec's inline ternary at `spec.md:215` and steps 0, 1 and 5 are deleted.

---

## MAJOR 1 — the spec's one explicit "carry this outward" requirement has no step

**Claim.** `spec.md:420-424`, under Open question 2, is an imperative: "**Carry this into the issue
and the PR description**, not just this file: #30's stated symptom ('so the sleep screen ghosts') was
written from code archaeology and is not reproducible on a default unit. Without that sentence in the
issue, someone later verifies optically, sees no change, and concludes the fix did nothing." The spec
review made the same demand independently ("it belongs in the issue too, so nobody later 'verifies'
it optically and concludes the fix did nothing").

**Problem.** No step in the plan writes that sentence anywhere durable. Step 7 (`plan.md:513-518`)
does carry the substance — "state the limit explicitly … on a default unit there is no visible
difference before and after" — but it carries it into the *hand-back message*, which is exactly the
"just this file" the spec is warning against. The issue and the PR body are the two places the spec
names, and the plan mentions neither. Sweep the whole plan for "PR", "issue #30" or "gh issue": the
only occurrences are the branch name and the spec/issue reference in the header block.

**Evidence.** `plan.md:490-518` is the entirety of step 7; it is addressed to the person reading the
hand-back, not to the issue tracker. `spec.md:420` is unambiguous that the hand-back is not enough.

**Concrete fix:** add to step 7, as a numbered deliverable rather than prose, the exact text to post
and where. Something like:

> **7c. Record the non-reproducibility.** Post this as a comment on issue #30 (and reuse it verbatim
> as the first paragraph of the PR description when the user approves a PR):
>
> > #30's stated symptom — "so the sleep screen ghosts" — was written from code archaeology and is
> > not reproducible on a default unit: all three X4 Pro panel drivers already force the first paint
> > after a wake to be non-differential. This change is a correctness fix (a parameter that stated an
> > intent and discarded it) plus the one configuration where the ghost is real: an SSD1677 batch
> > with `fadingFix` persisted to 1 through the web settings API. Verifying optically on a default
> > unit will show no difference, and that is the expected result.
>
> `gh issue comment 30 --repo victorstein/berean-os` needs user approval first
> (CLAUDE.md, Git workflow rule 2); if approval is withheld, say so in the hand-back.

---

## MINOR 1 — `LauncherRefresh.h` and the `LOG_DBG` call as written are not formatter-clean

**Claim.** `plan.md:8-9` promises "full file contents … exact". `plan.md:209-212` and
`plan.md:336-337` are not what `clang-format` leaves.

**Problem.** Step 1 and step 3 commit source the formatter will rewrite in step 6, so the tree is
format-dirty across three commits and step 6 grows a `style: clang-format` commit that a correct plan
would not need. (CI only checks the final tree, so nothing breaks — this is tidiness, not
correctness.) Note the spec has the same defect and asserts otherwise at `spec.md:212`: "both
statements fit the 120-column limit in `.clang-format:133`, so this is what the formatter will
leave." The second statement does not.

**Evidence.** `.venv/bin/clang-format --version` → 21.1.8, `-style=file:.clang-format`:

```
-constexpr HalDisplay::RefreshMode launcherRefreshMode(const bool cleanInitialRefresh,
-                                                      const bool firstRenderDone) {
+constexpr HalDisplay::RefreshMode launcherRefreshMode(const bool cleanInitialRefresh, const bool firstRenderDone) {
```

```
-  LOG_DBG(MODULE, "Paint: clean=%d firstPaint=%d mode=%s", cleanInitialRefresh ? 1 : 0,
-          firstRenderDone ? 0 : 1, mode == HalDisplay::HALF_REFRESH ? "HALF" : "FAST");
+  LOG_DBG(MODULE, "Paint: clean=%d firstPaint=%d mode=%s", cleanInitialRefresh ? 1 : 0, firstRenderDone ? 0 : 1,
+          mode == HalDisplay::HALF_REFRESH ? "HALF" : "FAST");
```

The signature is 115 columns joined and the `LOG_DBG` first line is 110, both inside `ColumnLimit:
120` (`.clang-format:133`).

**Concrete fix:** paste the `+` forms above into `plan.md:209-212` and `plan.md:336-337`. Make the
same two corrections to `spec.md:215-217` so the spec's own snippet is accurate.

---

## MINOR 2 — `git commit` with no message argument, in four steps

**Claim.** `plan.md:232`, `:289`, `:365` and `:427` are a bare `git commit`, with the message in a
following fenced block. Step 6 (`plan.md:481`) uses `-m`.

**Problem.** "Could an implementer execute this literally?" — not here. A bare `git commit` opens
`$EDITOR`; in the non-interactive shell an implementing agent runs, that either blocks until timeout
or aborts with "Aborting commit due to empty commit message". Four of the plan's five commits are
written that way and the fifth is not, so the plan is also internally inconsistent about it.

**Concrete fix:** make all five the same shape, e.g.

```bash
git commit -F - <<'EOF'
test: pin the launcher's first-paint refresh mode

...
EOF
```

---

## MINOR 3 — `pio check` is never run, and CI gates on it

**Claim.** Step 6 (`plan.md:456-486`) runs `~/.platformio/penv/bin/pio run` and
`./bin/clang-format-fix`. Step 7's "what you can claim" list (`plan.md:492-494`) is "the host suite
passes, `pio run` succeeds, the formatter is clean."

**Problem.** `.github/workflows/ci.yml` has a `cppcheck` job running `pio check --fail-on-defect low
--fail-on-defect medium --fail-on-defect high`, and it is one of the four jobs `test-status` requires.
`pio check` is a separate analyser that `pio run` does not cover, and this change adds a new header
to `src/`, which cppcheck will analyse. CLAUDE.md's testing checklist item 2 asks for it "when
relevant". The risk of a defect here is genuinely low — a `constexpr` one-liner — but the plan
neither runs it nor says why it is skipped, so the implementer cannot honestly claim CI-green.

**Concrete fix:** add `~/.platformio/penv/bin/pio check --fail-on-defect low --fail-on-defect medium
--fail-on-defect high` to step 6 after `pio run` (note that the bare `pio` is not on PATH here
either), and add it to step 7's claimable list.

---

## MINOR 4 — step 6's failure triage names an error that cannot occur

**Claim.** `plan.md:467` lists, among the likely `pio run` failures, "the member declared after the
initialiser that names it."

**Problem.** That is not an error in C++. Declaration position relative to the mem-initializer list
is irrelevant; only the *order of entries within* the list versus declaration order can produce
`-Wreorder`, and with exactly one initialised member (`cleanInitialRefresh`) it cannot fire. I
compiled step 2's header edit with the member appended after `hasResume`, exactly as
`plan.md:268-276` writes it, and it is clean. An implementer chasing a build failure will waste time
on this hint.

**Concrete fix:** replace that third item with the real third-most-likely cause — a missing
`add_subdirectory(launcher_refresh)` or a `LauncherRefresh.h` path typo, both of which surface as
"file not found" rather than a link error.

---

## Not findings, recorded so the next reader does not re-derive them

- **Step 1's red is a compile error, not a failing assertion.** That is the only red available for a
  header that does not exist yet, and the plan quotes the exact expected message
  (`plan.md:186`). Fine.
- **Steps 2–4 commit without a build.** `plan.md:78` declares this and it matches CLAUDE.md's
  explicit rule ("build once after the last code edit … do not repeat a target that already passed").
  Deliberate, and consistent with the repo.
- **Step 5 is a no-op by construction** — the host suite compiles none of the files steps 2–4 touch,
  so it cannot move. It costs seconds and proves the tree still configures, so it is harmless.
- **`const bool` member and non-assignability.** `LauncherActivity` is only ever
  `std::make_unique`'d into a `std::unique_ptr<Activity>` (`ActivityManager.cpp:254`), never
  assigned. A4 is safe.
- **`mode` is still used when `LOG_DBG` vanishes** at `LOG_LEVEL=1`, because
  `renderer.displayBuffer(mode)` consumes it. No unused-variable warning in the release env, and
  `platformio.ini` sets no `-Werror` anyway.

---

BLOCKER 1 is what gates this. It does not say the plan is wrong — the A10 reversal is well evidenced
and I verified every leg of it — it says the plan and a cleared spec now disagree about scope, after a
prior review affirmed the spec's side, and only the human can settle that. Ratify it and amend the
spec's A10, Architecture and Testing sections, and MAJOR 1 plus the four MINORs are all inline edits
to the plan.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
