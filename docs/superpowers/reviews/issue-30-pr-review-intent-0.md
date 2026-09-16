# PR #44 — intent review (pass 0)

**Date:** 2026-09-16
**Branch:** `fix/30-launcher-wake-refresh`
**PR:** #44, `fix: paint the launcher cleanly on the first frame after a wake`
**Reviewed against:** issue #30 (brief + acceptance criteria),
`docs/superpowers/specs/2026-09-16-issue-30-design.md`,
`docs/superpowers/plans/2026-09-16-issue-30-plan.md`
**Scope of this pass:** intent only — completeness against the issue and the spec,
silent scope reduction, scope expansion, whether the tests exercise behaviour, and
undeclared divergence from the plan. Code quality is a later pass and is not judged
here.

## What the PR actually changes

Four source files and two test files; the rest of the 2,084 added lines are the
workflow documents (research, spec, plan, three prior reviews), which this repo
commits alongside the change.

- `src/activities/launcher/LauncherRefresh.h:15-17` — new include-free
  `constexpr bool launcherNeedsCleanPaint(bool, bool)`.
- `src/activities/launcher/LauncherActivity.h:25-26,94-98` — constructor parameter
  defaulted to `false`, held as `const bool`, plus the `firstRenderDone` latch.
- `src/activities/launcher/LauncherActivity.cpp:8,24,475-480` — `<HalDisplay.h>`,
  the helper include, and the paint tail that asks the helper for its mode, logs
  it, paints and latches.
- `src/activities/ActivityManager.cpp:247-255` — `(void)cleanInitialRefresh;`
  removed, flag forwarded into the constructor, comment rewritten to cover only
  `initialMenuItem`.
- `test/CMakeLists.txt:111`, `test/launcher_refresh/` — the four-case truth table.

## Acceptance criteria, one by one

**AC1 — the launcher takes the flag and uses `HALF_REFRESH` for its first render,
`FAST_REFRESH` otherwise.** Met. `LauncherActivity.h:25-26` takes it,
`LauncherActivity.h:97` holds it `const`, and `LauncherActivity.cpp:475-480` maps
`launcherNeedsCleanPaint(cleanInitialRefresh, firstRenderDone)` to
`HalDisplay::HALF_REFRESH` / `HalDisplay::FAST_REFRESH` and latches after the
paint. `render` has no early return before that tail
(`LauncherActivity.cpp:452-481`), so the latch cannot be skipped once a paint
happens, and `LauncherActivity.cpp:479` is the file's only `displayBuffer` call.

**AC2 — `goHome` passes the flag through, and the comment at
`ActivityManager.cpp:248-251` is updated.** Met.
`ActivityManager.cpp:254` forwards it; the only remaining
`(void)` is `initialMenuItem`. The rewritten comment's factual claim checks out:
`onGoHome` has 22 call sites (`src/activities/Activity.h:69` plus 21 uses) and no
call site anywhere in `src/` passes a non-`NONE` `HomeMenuItem`, so "No caller
passes anything but `HomeMenuItem::NONE`" is accurate and the old "~20 call sites
pass it" miscount is gone.

**AC3 — `initialMenuItem` stays ignored, that half of the comment stays
accurate.** Met, as above.

**AC4 — `pio run` succeeds.** Verified independently, not taken on trust:
`~/.platformio/penv/bin/pio run` → `x4pro SUCCESS`, RAM 19.5%, flash 81.2%.

**Constraints.** `goHome`'s signature is unchanged
(`ActivityManager.h:93`). `HomeActivity` is untouched — it is not in the changed
file list. The sleep path is untouched; `src/main.cpp` is not in the diff, and the
`freeink-sdk` submodule pointer is unmoved.

## Spec requirements

Every lettered assumption that produces code is implemented, including the ones
that are easy to skip:

- **A1** `HALF_REFRESH`, not `FULL_REFRESH` — `LauncherActivity.cpp:476`.
- **A2 / A6** consumed on the first render only, latch set unconditionally after
  the paint — `LauncherActivity.cpp:480`, outside any branch.
- **A3, the spec's named copy-paste risk** — **not** violated. There is no
  `requestUpdate()` after the paint; the only `requestUpdate()` on the entry path
  is the deferred one in `onEnter` (`LauncherActivity.cpp:73`), and the others are
  input and result callbacks (`:517,524,551,555`). `HomeActivity`'s follow-up
  refresh was correctly not copied.
- **A4** constructor parameter defaulted `false`, `const bool` member — the four
  other `goHome` routes (`src/main.cpp:566`, `ActivityManager.cpp:81,114`,
  `Activity.cpp:13`) keep today's behaviour with no edit.
- **A8** `LOG_DBG` polarity — `LauncherActivity.cpp:477-478` prints
  `firstRenderDone ? 0 : 1` for `firstPaint`, the negation the spec requires, read
  before the latch. This is the exact bug spec review pass 0 caught once already;
  it is right here.
- **A9** `#include <HalDisplay.h>` added directly — `LauncherActivity.cpp:8`.
- **A10** include-free helper plus host suite — `LauncherRefresh.h` has zero
  includes, and `test/launcher_refresh/CMakeLists.txt:10-12` adds only
  `${REPO_ROOT}/src`, matching `test/return_stack`.

The PR's central technical claim was checked against the pinned submodule rather
than accepted: on SSD1677 with `fadingFix` persisted (`turnOff == true`), the
first-paint promotion block is skipped (`Ssd1677Driver.cpp:428`), but any
non-`Fast` mode writes both the BW and RED planes from the same frame buffer
(`Ssd1677Driver.cpp:463-465`), i.e. an absolute paint. So requesting `HALF` is
what makes that configuration clean, and a `FAST` request there would stay
differential. The stated reason for the change holds.

## Tests

The suite builds and passes as claimed: `ctest` reports `100% tests passed out of
547`, the four new `LauncherRefresh` tests among them (nos. 544-547).

The four cases pin the polarity of the two arguments, which is the failure mode
this change is actually exposed to, and `test/launcher_refresh/LauncherRefreshTest.cpp:6-13`
says so. The spec is explicit and honest about what this does not buy: the
`? HALF_REFRESH : FAST_REFRESH` mapping at the call site is not under test, and
neither the suite nor the firmware build can show whether the panel still ghosts
(spec, A10 and Testing). That trade — an include-free helper modelled on
`ReturnStack.h`, at the cost of the mapping's coverage — was ratified by the human
during plan review pass 0 and is not re-opened here.

The PR body carries the hardware checks as **outstanding**, with the exact serial
expectations per entry route, and does not let a green suite stand in for them.
That is the right hand-back for this change.

## Scope

**No silent reduction found.** Both omissions are declared in the PR body under
"Deferred, deliberately", both are spec Non-goals reached through review rather
than convenience, and both require editing `src/main.cpp`, which the issue's own
brief scopes out ("Scope is the launcher and the manager"). Deferring them is
legitimate:

- `src/main.cpp:454-457`'s stale comment is documentation, not behaviour.
- `BootResume::Silent` at `src/main.cpp:566` is a reboot route, not a wake from
  sleep, so it is outside the issue's stated symptom even though it shares the
  mechanism.

**No scope expansion found.** The helper header, the host suite and the `LOG_DBG`
are each traceable to a ratified spec assumption (A10, A8) rather than invented
during implementation. The documents added under `docs/superpowers/` are this
repo's established workflow output, already present for prior issues.

**No undeclared divergence from the plan.** The committed code is byte-identical
to the plan's step 1-4 snippets, the four commits are the plan's four commits with
its commit messages, and the plan's optional step 6 `style: clang-format` commit
is absent because the formatter had nothing to change — consistent with the plan's
own prediction, not a skipped step.

---

## Findings

### MINOR 1 — a spec deliverable aimed at issue #30 was not delivered, and the PR does not say it is still owed

Spec, Open questions 2, requires the non-reproducibility note to reach "the issue
and the PR description, not just this file", and plan step 7c gives the exact
`gh issue comment 30` body, gated on the human's approval (correctly — CLAUDE.md,
Git workflow rule 2).

The PR description carries it, in its opening paragraph. Issue #30 does not:
`gh issue view 30` reports `comments: 0`, and the issue body is unedited. Plan
step 7c's fallback — "If approval is withheld, say so in the hand-back so the next
person knows the note is still owed" — is also unmet: the PR body says nothing
about it.

Not a scope objection; the requirement was deliberate, and it exists precisely so
that a later optical verification on a default unit is not read as a failed fix.
Fixable inline: post the comment with the user's approval, or add one line to the
PR body recording that it is outstanding.

### MINOR 2 — the two deferrals have no tracked home that survives merge

The PR closes #30. Both deferred items are described only in the PR body and in
`docs/superpowers/specs/2026-09-16-issue-30-design.md` (Non-goals, A7). No open
issue covers either one — `gh issue list` shows nothing for the `main.cpp:454-457`
comment or the `BootResume::Silent` route, and neither the PR body nor the spec
names a follow-up issue number.

The second of the two is a one-token behaviour gap (`src/main.cpp:566` reaches the
launcher in the same stale-frame state and passes no flag), and once #30 closes it
lives only in a merged spec document. Fixable inline: file the follow-up with the
user's approval and cite its number in the PR body, or say in the PR body that the
follow-up is not yet filed.

---

## Verification performed for this review

- `cmake -S test -B build/test && cmake --build build/test && ctest` →
  `100% tests passed out of 547`, including the four new ones.
- `~/.platformio/penv/bin/pio run` → `x4pro SUCCESS` (AC4, verified rather than
  taken from the PR body).
- `pio check` and `./bin/clang-format-fix` were not re-run; no finding here depends
  on them.
- Working tree left clean (`git status --short` empty apart from this review file);
  `build/` is gitignored.

Neither the build nor the suite can close the hardware check, and this review does
not claim to. That remains the human's, as the PR says.

## Verdict

Both findings are record-keeping around a change whose behaviour is complete and
correct. Every acceptance criterion is met, every code-producing spec assumption is
implemented — including A3, the one the spec flagged as most likely to be got
wrong — nothing was quietly dropped, and nothing was added that was not asked for.
Neither MINOR reverses a decision, changes scope, or needs a judgment only the
human can make.

MAJORS: 0
MINORS: 2

VERDICT: CLEAR
