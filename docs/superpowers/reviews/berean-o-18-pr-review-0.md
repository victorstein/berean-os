Tier: heavy

# PR review 0: Elegir temas, the Predicación topic picker (berean-o-18, t5)

Range: `origin/batch/20261015-predicaci-n-193x...origin/feature/preaching-topic-picker` at `29b5d306`.
Code under review: `src/activities/preaching/PreachingTopicPickerActivity.{h,cpp}` (new) and
`src/activities/preaching/PreachingActivity.{h,cpp}`. The rest of the diff is the research, spec,
plan and their reviews. The branch log has no empty reply commits, so no finding was rejected.

Checked against the `hpipe brief --task t5` brief, the change's spec
(`docs/superpowers/specs/2026-10-15-berean-o-18-design.md`), its plan
(`docs/superpowers/plans/2026-10-15-berean-o-18-plan.md`), and parent spec §3 and §7.

The batch has moved since this branch was cut: show mode (berean-o-17) landed. It touches
`PreachingTopicActivity.*` and adds `PreachingShow*`, but none of this change's files.
`git merge-tree` merges the two cleanly.

## Intent

**Acceptance criteria.**

- **"＋ Elegir temas" opens a multi-select grid of the active tags, with the picks ticked and no
  tag creation.**
  - `openTopicPicker` (`PreachingActivity.cpp:212-226`) pushes the picker with
    `startActivityForResult`.
  - `buildChips` (`PreachingTopicPickerActivity.cpp:59-69`) makes one `Kind::Tag` chip for each
    entry in `STUDY.palette().activeIdsByName()`, with no `NewTag` chip.
  - `chipState` (`:71-73`) shows `Active` for a pick.
  - The seed is `selectedInOrder(activeIds, topicsFile.tags())` (`:34`), so the current picks
    open ticked. Met.
- **The left-edge swipe saves through `PreachingTopicsFile` and returns. A failed or latched save
  shows the popup and keeps the previous selection.**
  - `loop()` (`:43-51`) takes `wasReleased(Back)` before the base loop. That is the left-edge
    swipe (`MappedInputManager.cpp:222-226, 264-265`). It does so only while the retire
    confirmation is closed.
  - `saveAndReturn` (`:105-121`) calls `topicsFile.save(picks)`.
    - On any result but `Ok`, it posts `STR_PREACHING_SAVE_FAILED` and returns the on-disk picks.
    - The store refuses while latched and leaves `tags()` and the file untouched on failure
      (`PreachingTopicsFile.cpp:63-97`).
  - Met.
- **Mis temas shows the new picks in `activeIdsByName` order, with fresh counts.**
  - `onTopicPickerResult` (`PreachingActivity.cpp:239-247`) calls `onTopicsChanged()` when the
    returned ids differ, as a set, from `session.topics()`. That reloads the file and palette,
    recounts and rebuilds under one `RenderLock` (`:249-261`).
  - `PreachingSession::ensureTopics` rebuilds `activeTopics` in `activeIdsByName` order
    (`PreachingSession.cpp:114-125`).
  - The counts pass runs only on a change, as the brief asks. Met.
- **The build passes with no new warnings, and clang-format is clean.**
  - I did not rerun `pio run`, because another worktree's `pio run -e x4pro` was in progress.
  - The worker's build artefacts postdate the last code commit (03:50:37):
    `PreachingTopicPickerActivity.cpp.o` and `PreachingActivity.cpp.o` at 03:51:17, and
    `firmware.bin` at 03:51:41.
  - The worker's `build.log` and `verify-build.log` both end in `SUCCESS`.
  - The tree is clean after the format run, so the wrapper changed nothing.
  - **The warning baseline was empty.** `warnings-before.txt` is 0 bytes, while
    `warnings-after.txt` holds one line: a `-Wdeprecated-declarations` in
    `.pio/libdeps/x4pro/WebSockets/src/WebSocketsClient.cpp`.
    - That is a third-party library recompiled by the clean build, not code this change reaches.
      So the conclusion "no new warnings from this change" holds.
    - The plan's before/after diff did not literally pass, though.
  - Met.

**Brief items beyond the acceptance list.**

- **Retire.** The base's long-press retire is kept. `onTagRetired` (`:93-96`) drops the id from
  the selection under `RenderLock`. That differs from the seed, so the save prunes it, and the
  result differs from Mis temas, so Mis temas reloads.
- **Home.** `onHome` (`:98-103`) calls `saveAndReturn()` and then `goHome()`.
  - The order is correct. `finish()` sets a pending Pop, and `replaceActivity` then overwrites it
    with Replace (`ActivityManager.cpp:185-191`). The reverse order would reset the pending Home
    (`:262-268`).
  - The brief says Home "goes Home, as the other chip grids do". Neither sibling actually goes
    Home: TagPicker commits and returns, and TagFilter cancels.
  - Spec A5 settles this by going Home after saving, so picks are never silently dropped. That is
    a documented reading that does what the brief literally asks, not a scope change.
- **Don't write over a refused file.** The store's latch handles this.
  - A13's no-change guard (`:107`) also means a latched store raises no toast on a visit that
    changed nothing.

**Spec requirements.**

- **d1, opening `STUDY` on the Bible.** `ensureStudyOpen` (`PreachingActivity.cpp:228-237`)
  matches the spec:
  - it uses `canResumePublication`, not `isOpen()`;
  - it times the open and logs it;
  - it logs the save latch;
  - it does not call `resumePublication()`.
- **The reopen is safe.** Its safety argument rests on Predicación being entered only by a
  replace. `ActivityManager.cpp:233` is the only construction site, and `goToPreaching` is a
  replace (`:232-239`).
- **The d1 hazard (an empty palette written over `tags.json`) is closed.**
  - After a fresh boot, a `NotFound` palette gives no chips, so nothing can be retired.
  - A `Failed` palette latches `saveDisabled_`, which the base checks before showing the retire
    confirmation (`TagChipGridActivity.cpp:174-181`).
- **The other assumptions are each implemented as written:**
  - A7: the on-disk picks after a failed save;
  - A8: the four-iterator `is_permutation` at both sites;
  - A9: `moveRingTo(listCount())` lands on the Elegir row, which is `rowItems`' last entry
    (`PreachingActivity.cpp:106-109`);
  - A11: no Bible, no picker;
  - A12: the header title.
- **No silent scope reduction or expansion.** Only the four files in the plan's `FILES:` lines
  changed.

**Plan divergence.** None. The three code commits reproduce the plan's steps 1–3 verbatim. The
plan's optional style and fix commits were not needed.

**Tests.** No host test was added.
- The brief asks for tests only of extracted logic. None was extracted:
  - ordering is `TagChips::selectedInOrder`;
  - the store is T2's and already tested;
  - each change check is a single library call.
- So the absence is in scope, not a gap.
- The behaviour that matters is the change detection and the "result after a failed save" path.
  It is covered only by the owner's device checks 3, 3a, 5 and 5a (spec §Testing).
- Host suites were not rerun here. No test or library file changed.

## Quality

- **Mirrors the model.** The picker is `TagPickerActivity` with the New tag chip, the cap and the
  Done pill removed:
  - `isSelected`, `buildChips`, the toggle under `RenderLock` and `onTagRetired` match
    `TagPickerActivity.cpp:33-35, 58-74, 95-120, 170-173`;
  - `saveAndReturn` ends like `commitAndFinish` (`:181-187`).
- **`openTopicPicker` follows `pushTopic`.** It uses `makeUniqueNoThrow`, a null check with
  `LOG_ERR`, `reportOpenFailure` and the same lambda form (`PreachingActivity.cpp:298-307`).
- **The one new mechanism has no exact precedent.** That mechanism is intercepting Back in
  `loop()` ahead of a `final` `onBackButton`.
  - The cited `MeetingsActivity::loop` precedent wraps the base loop, but not Back.
  - It is still the least invasive option: the base is out of scope, and its contract ("both exits
    MUST set a result", `TagChipGridActivity.h:58-62`) is honoured.
  - The interception yields to the retire popup through `retireConfirmationOpen()`, so the popup
    still dismisses on Back.
- **Threads.**
  - `selectedIds` is written only under `RenderLock`: in the seed, the toggle and the retire. The
    render task reads it only from `chipState`.
  - `seedIds` and `topicsFile` are loop-task only.
  - The result handler runs with the lock free (`ActivityManager.cpp:121-128`), and
    `onTopicsChanged` takes its own lock.
- **No extra empty-grid refresh.** `onEnter` does SD reads after the base `onEnter` has called
  `requestUpdate()`, but that request is deferred to the end of the manager's loop
  (`ActivityManager.cpp:295-304, 168-174`). So no empty grid is pushed to the panel first.
- **Error handling is the established shape:**
  - `LOG_ERR` plus a posted toast;
  - no abort path;
  - no bare `new`;
  - no `writeDocToFile`;
  - `.reserve()` before the `push_back` loop;
  - every user-facing string through `tr()`.
- **Comments are about "why" only.** Examples are the four-iterator note, the Home ordering, the
  `final` Back, and why `STUDY` is opened. None restates the line after it. There is no dead or
  commented-out code.
- **Small duplication.** The four-iterator set comparison is written twice: once as `sameIds` in
  the picker and once inline in `PreachingActivity` (MINOR 1).

## Findings

**MINOR 1: the four-iterator change check is duplicated, and only one copy explains itself.**
- `PreachingTopicPickerActivity.cpp:20-24` defines `sameIds`, with the comment on why the
  three-iterator form is wrong (spec review M3).
- `PreachingActivity.cpp:242` writes the same `std::is_permutation(…4 iterators…)` inline,
  without the comment.
- The inline site is the one that decides whether the counts pass runs. Someone tidying it to the
  three-iterator form would bring back the bug that device check 3a exists to catch: dropping the
  last pick in name order reads as unchanged.
- Fix: carry the one-line reason to the inline site as well. An alternative is a shared helper,
  but `TagChipRow.h`, where `selectedInOrder` lives, is outside this task's files.

**MINOR 2: sleep drops unsaved picks, and that gap is only in the plan, not in the owner's checks.**
- Plan step 7 records it: `goToSleep` is a replace that runs only `onExit`, so a power-button or
  timeout sleep with the picker open discards the changes without a message.
- It also says to ask the owner to confirm this.
- `TagPickerActivity` behaves the same way, so this is not a defect.
- But spec §Testing's checks 1–8 do not list it. A tester working from the spec will not see it.
  Make sure the hand-off states it as a known, unverified behaviour.

VERDICT: CLEAR
