Tier: heavy

# Plan review 0: berean-o-18 (Elegir temas, the Predicación topic picker)

Reviewed: `docs/superpowers/plans/2026-10-15-berean-o-18-plan.md` at `4a07605d`, against
`docs/superpowers/specs/2026-10-15-berean-o-18-design.md`, its review
(`docs/superpowers/reviews/berean-o-18-spec-review-0.md`), the brief (`hpipe brief --task t5`),
`CLAUDE.md` and `.claude/agents/ui-dev.md`. I read the plan and the code it depends on. I did not
build anything or apply the plan.

| Severity | Count |
|---|---|
| BLOCKER | 0 |
| MAJOR | 0 |
| MINOR | 3 |

The plan is a faithful transcription of the spec. Every assumption from A1 to A13 maps to code in
step 1 or step 3. The old text that steps 2 and 3 replace matches the current files exactly. I
found nothing that would ship a defect.

## What was checked and holds

- **The file lock.** Plan lines 7-8 are two `FILES:` lines at column 0, outside any fence.
  Together they list exactly the four files that steps 1-3 create or edit. hpipe's parser accepts
  comma-separated lists (its `test/gating.test.ts:122`). No step edits any other tracked file:
  - Step 0 only `touch`es a file.
  - Step 6 writes to `build/`, which is gitignored (`.gitignore:13`).
- **No push, PR or `hpipe submit`.** The plan stops at step 7 ("The work ends here", :504).
- **Spec coverage, item by item.**
  - **A1:** `loop()` at plan :163-171. Back is checked only while `!retireConfirmationOpen()`, so
    the popup still gets Back. `TagChipGridActivity::handleCustomInput` gives it first claim
    (`TagChipGridActivity.cpp:204-212`).
  - **A2:** the picker's own `topicsFile`, loaded in `onEnter`.
  - **A3:** `seedIds` comes from `STUDY.palette().activeIds()` and `topicsFile.tags()`.
    `selectedIds` is assigned under `RenderLock`.
  - **A4 and A6:** `buildChips` has no `NewTag` chip. `chipState` is a plain Active or Inactive,
    with no `pickerState` cap.
  - **A5:** `onHome` calls `saveAndReturn()` before `goHome()`.
    - The order is correct. `finish()` → `popActivity()` sets Pop (`Activity.cpp:24`). `goHome()`
      then sets a Replace over it (`ActivityManager.cpp:144-151`).
    - The reverse order would `reset()` the pending Launcher (`ActivityManager.cpp:267-273`).
  - **A7 and A13:** `saveAndReturn` skips the save on the four-iterator `sameIds`. After a result
    other than `Ok`, it returns `selectedInOrder(activeIdsByName, topicsFile.tags())`.
    `PreachingTopicsFile::save` leaves `tags_` unchanged on every result but `Ok`
    (`PreachingTopicsFile.cpp:63-97`).
  - **A8 and A9:** `onTopicPickerResult` uses the four-iterator `is_permutation` against
    `session.topics()`. A result that is not a `TagSelectionResult` counts as a change. The ring
    lands on `listCount()`, which is the Elegir row (`PreachingActivity.cpp:102-105`). That is
    the same unlocked `moveRingTo` that `onTopicResult` uses (:277-281).
  - **A10, A11 and d1:**
    - `ensureStudyOpen` calls `canResumePublication`, never `resumePublication()`, and times the
      open with `millis()`.
    - `openTopicPicker` checks `hasBible()` first. On OOM it follows the `pushTopic` pattern.
  - **A12:** `headerTitle()` returns `tr(STR_PREACHING_CHOOSE_TOPICS)`, which exists
    (`english.yaml:488`, `spanish.yaml:435`).
- **Threads.**
  - `selectedIds` is written only under `RenderLock`: in `onEnter`, `activateIndex` and
    `onTagRetired`.
  - `chipState` reads it on the render task without the lock, which is the base's contract
    (`TagChipGridActivity.h:43-45`).
  - `seedIds` and `topicsFile` are touched only on the loop task.
  - `ActivityManager::requestUpdate()` is deferred to the end of the manager's loop
    (`ActivityManager.cpp:295-304`). So `onEnter`'s SD reads between `TagChipGridActivity::onEnter()`
    and `rebuildChips()` cannot leave an empty grid painted.
- **Error and fallback paths no test reaches, traced by hand.** In each case, Mis temas either
  matches disk or is reloaded from it:
  - **`preaching.json` latched:** `tags()` is empty in both the picker and the session, so the two
    compare equal and nothing reloads.
  - **Only `tags.json` latched:** at worst, one extra harmless reload.
  - **A retired pick after a refused save:** `STUDY`'s palette no longer lists the tag, so the
    result differs from `session.topics()`, and the reload drops it.
  - **A retired unpicked tag:** nothing changes, and nothing needs to.
- **The batch base moved, but not under this plan.** `origin/batch/20261015-predicaci-n-193x` now
  carries T6's Home card (berean-o-19).
  - The plan's four files are untouched.
  - The new Home card enters Predicación through `activityManager.goToPreaching()`
    (`LauncherActivity.cpp:409` on the batch), which is still a replace. So d1's "no reader beneath
    Predicación" still holds.
  - The only drift is in a citation. The spec's `LauncherActivity.cpp:376` (`drawNext`) is now
    about 14 lines lower. Merging the base before implementation is enough.

## MINOR

### m1. Sleep while the picker is open silently discards the picks

**Claim.** Picks are saved only from the swipe and from Home (plan :163-171, :218-223). The spec
lists no other exit (§Data and control flow).

**Problem.** Sleep, from the power button or the inactivity timeout, leaves through
`activityManager.goToSleep()` (`src/main.cpp:257-260`), which is a replace. The replace runs the
picker's `onExit` and destroys it without calling `saveAndReturn()`, so a set of toggles the owner
made is lost with no toast. Neither the spec nor the plan mentions this path, and no device check
covers it.

`TagPickerActivity` behaves the same way: it drops its selection on sleep. So this is consistent
with the repo, not a regression. But here the picks are a persisted setting rather than an edit
the owner expects to confirm, and pressing power to sleep is a natural way to "put it down".

**Fix (inline).** Add one line to the plan's step 7 hand-off list: sleeping from the picker
discards unsaved picks, as `TagPickerActivity` does. This tells the owner on the device check.

Do not save from `onExit` in this task. That would run SD I/O inside the manager's `RenderLock`
(`ActivityManager.cpp:142-146`), and the spec does not ask for it. If the owner wants it, it
becomes a follow-up via `hpipe discover`.

### m2. Step 4's "no new warnings" check can miss a new warning and invent one

**Claim** (plan :55-63, :434-444): both builds' `warning:` lines are filtered with
`grep -i preaching` and compared with a plain `diff`.

**Problem.** The check can miss a new warning:

- `gcc` attributes a warning to the file where it occurs.
- A warning the new translation unit triggers inside a shared header (`TagChipRow.h`,
  `TagChipView.h`, `UiListActivity.h`) carries that header's path.
- The `preaching` filter drops it. The acceptance line "no new warnings" would then pass on a build
  that has one.

The check can also invent a new warning:

- The `warning:` lines carry `line:col`.
- Step 3 inserts about 40 lines into `PreachingActivity.cpp` above line 208.
- Any warning already below the seam then shifts and diffs as "new". Step 4 tells the implementer
  to "fix it in place", which is a change to code outside the task.

**Fix (inline).** In both the baseline and step 4:

- Drop the `grep -i preaching` filter. Each build is incremental and recompiles only the touched
  translation units, so the log already contains nothing else.
- Strip the positions before comparing:
  `grep -E "warning:" … | sed -E 's/:[0-9]+:[0-9]+:/:/' | sort -u`.

### m3. Step 5's "do not commit that file" leaves a dirty tree that step 6 then rejects

**Claim** (plan :463-464): if `./bin/clang-format-fix` reformats a file outside `FILES:`, "do not
commit that file. Report it."

**Problem.** The reformatted file stays modified in the worktree. Step 6 then requires
`git status --short` to be clean (:486). The worktree is also what the implement phase pushes from.
An implementer who follows the plan to the letter would either stall at step 6 or carry an
unrelated format change along.

**Fix (inline).** After "Report it", add: restore it with `git checkout -- <file>`, and file the
drift with `hpipe discover --task t5`.

VERDICT: CLEAR
