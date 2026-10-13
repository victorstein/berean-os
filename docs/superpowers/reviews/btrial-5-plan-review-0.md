Tier: heavy

# btrial-5 plan review 0

- **Under review:** `docs/superpowers/plans/2026-10-12-btrial-5-plan.md`, at `21c48f83`.
- **Checked against:**
  - the spec `docs/superpowers/specs/2026-10-12-btrial-5-design.md`;
  - the brief (`hpipe brief --task t5`);
  - the batch spec's §3, §4 and §6.
- **Method.** I read the plan and checked its claims against the code in this worktree and the
  FreeInkUI sources under `freeink-sdk/`. I did not apply or build the plan. Its compile and count
  expectations are left to its own red/green steps.

## What holds

- **Spec coverage is complete.** Every bullet of the brief maps to a step, and so does every spec
  item. The plan's self-review table (`plan:980-1001`) is accurate. Nothing is narrowed:
  - **Open filter:** the constructor (`plan:546`).
  - **Header:** `headerTitle` and `drawChrome` (`:567-577`). Done is drawn as an Action chip with
    `InputTouch` only, `ACTION_DONE = ACTION_USER`, and a band hit pad (`:602-608`). It is
    registered after the base `onEnter` (`:548-552`).
  - **Activation:** `pickerState` is recomputed in `activateIndex` (`:610-635`). A toggle clears
    the flash and writes under `RenderLock`.
  - **Create:** the outcome comes from `pickerState`, and every returned id ends in
    `rebuildChips(ChipKey)` (`:637-668`).
  - **Retire:** `onTagRetired` takes the lock (`:684-687`).
  - **Commit:** allocation order through `activeIds()` (`:697-703`).
  - **Removals:** `rowItems_`, `tags_`, the held-Confirm retire, `TagRowMapping.h`,
    `TagRowsTest.cpp`, its CMake line, and `STR_TAG_NEW`.
  - **Verification:** the device checks are listed unchanged (`:970-971`).
- **The file lock covers every tracked file a step touches** (`plan:44-47`).
  - The listed files: `TagChipRow.h`, `TagChipRowTest.cpp`, `HeaderLayoutTest.cpp`,
    `TagPickerActivity.{h,cpp}`, `english.yaml`, `TagRowMapping.h`, `TagRowsTest.cpp` and
    `test/tag_rows/CMakeLists.txt`.
  - Some untracked paths are written but need no lock entry, because `.gitignore:8-10` and `:13`
    ignore them: the generated `lib/I18n/I18n*` files, `build/btrial-5-pio.log`, and `build/test`.
  - Comma-separated `FILES:` lines are the form earlier plans use
    (`docs/superpowers/plans/2026-09-26-issue-101-plan.md:9-15`).
- **No step pushes, opens a PR or runs `hpipe`** (`plan:4`, `:973`).
- **The behaviour the tests cannot see holds against the code:**
  - **No handler runs on the render task.**
    - `renderUi` calls `app.render()` with an empty snapshot (`src/components/UiAppHost.cpp:17-19`).
    - So neither `onDoneEvent` nor `activateIndex` can run with `RenderLock` already held, where
      re-taking it would deadlock.
  - **The keyboard round trip.**
    - A push keeps the picker and runs no `onExit`/`onEnter` (`src/activities/ActivityManager.cpp:154-160`).
    - The result handler runs unlocked, after the picker is current again (`:115-127`).
  - **Done can be drawn in the header band.**
    - `fui::button` registers and paints at the absolute rect and does not clip to the content
      margin (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/button.h:36-48`).
    - With `minTouchSize = 0`, `ensureMinTouchRect` only edge-snaps (`FreeInkUI.cpp:812-850`).
  - **Done's tap flash cannot collide with a chip.**
    - It is `{ACTION_DONE, 0}`, and the chips use `ACTION_ROW` (`UiListActivity.h:27-28`).
    - `commitAndFinish` clears it anyway.
  - **Nothing outside the render task reads `title_`, `headerBand_` or `doneRect_`.** The only
    callers of `headerTitle()` and `drawChrome()` are render paths (`UiListActivity.cpp:143`,
    `TagChipGridActivity.cpp:270`, `:274`).
  - **The Task 4 deletion is guarded.**
    - It is safe without a second firmware build: `git grep` shows `TagRowMapping.h` is included
      only by `TagPickerActivity.cpp:12` and `TagRowsTest.cpp:3`.
    - Task 3 Step 3 replaces the first wholesale.
    - Task 4 Step 1 stops if any `src/` line remains (`plan:836-839`).
  - **The Step 7 warning grep can trip.** The trial log prints warning paths repo-relative
    (`.pio/libdeps/...`), so a `src/` or `lib/` warning would match `^(src|lib)/`.

No BLOCKER and no MAJOR. Two MINORs follow. Both are fixed inline by the text given, with no
further review pass.

## Findings

### MINOR 1 — User-facing docs made stale by this change are missing from the follow-up discovery

- **Claim.** Task 5 Step 4 hands the worker the discovery's contents (`plan:973-976`): the dead
  `StudyStore::activeTags()`, plus stale comments at `HighlightsActivity.h:30`, `:72-75` and
  `TagPalette.h:19-23`. The spec's Non-goals list the same set (`spec:88-97`).
- **Problem.** Three more passages describe the picker as having no confirm of its own besides
  Home, or as a tick list. After this change it has a header **Done** pill and chips, and the base
  owns the Home override (`TagChipGridActivity.h:33`, `final`). Nothing in the plan or the
  discovery will correct them. They are what the owner reads, and what the next agent reads.
- **Evidence.**
  - `USER_GUIDE.md:64-66`: "Two screens repurpose it because they have no other way to confirm …
    the tag picker uses it to finish."
  - `USER_GUIDE.md:250`: "**Tag** opens the tag picker; tick any number of tags, then tap **Home**
    to apply them."
  - `docs/contributing/touch-and-ui.md:120`: "`handleHomeGesture()` is the only way a screen with
    no other confirm affordance can accept one — `PassageSelectActivity` and `TagPickerActivity`
    both repurpose the Home tap that way."
  - None of these files is on a `FILES:` line, so a discovery is the right route rather than an
    edit here.
- **Fix (inline).** In Task 5 Step 4, extend the discovery list after the `TagPalette.h:19-23`
  bullet with:
  - "the user guide's picker text at `USER_GUIDE.md:64-66` and `:250`: Done in the header, chips,
    and Home still commits;"
  - "`docs/contributing/touch-and-ui.md:120`, which still says `TagPickerActivity` overrides
    `handleHomeGesture()` for want of another confirm."

  Add the same two items to the spec's Non-goals discovery bullet (`spec:90-97`), so the two
  documents agree.

### MINOR 2 — The rewritten `reportAddTagFailure` comment names the wrong function, and claims more than the code does

- **Claim.** Task 3 Step 3 rewrites the comment (`plan:671-673`) as the spec directs
  (`spec:395-400`): "Mirrors TagPalette::add's precedence, name checks before the full palette, so
  it needs no failure code from addTag to say something true."
- **Problem.**
  1. **The picker never calls `addTag`.** It calls `StudyStore::addTagName` (`plan:642`,
     `src/study/StudyStore.h:118`).
     - The only `addTag` left in the tree is the legacy `HighlightDoc::addTag`
       (`lib/Epub/Epub/HighlightDoc.cpp:30`). The spec says that one "no longer exists"
       (`spec:396-397`), but it does.
     - So the rewrite keeps a pointer to the very function it set out to stop citing. A reader who
       follows it lands in the old per-book store.
  2. **"To say something true" fails on one reachable path.** `addTagName` also returns nullopt
     when the palette write fails: it logs, retires the new tag again and returns
     (`src/study/StudyStore.cpp:258-262`).
     - A non-empty name of 24 bytes or fewer then falls to the last branch. The picker shows
       `STR_TAG_PALETTE_FULL` (`plan:678-679`), "This book already has the maximum number of tags"
       (`english.yaml:378`), for an SD write error.
     - `STR_TAG_SAVE_FAILED` (`english.yaml:380`) exists and would be true.
     - This is today's behaviour (`TagPickerActivity.cpp:183-198`), so changing it is out of scope.
       The new comment should not assert that it is correct, though.
- **Fix (inline).**
  - Replace the first sentence of the comment in Task 3 Step 3 (`plan:671-672`) with:
    `// Mirrors TagPalette::add's precedence, name checks before the full palette, so it needs no
    failure code from addTagName. A failed palette write also returns nullopt and reads as full
    here.`
    Keep the second sentence, about the too-long branch, as written.
  - Make the matching edit to the spec's wording at `spec:397-398`.
  - Add one more bullet to the Task 5 Step 4 discovery list: "a failed palette write in + New tag
    shows `STR_TAG_PALETTE_FULL` instead of `STR_TAG_SAVE_FAILED` (`StudyStore.cpp:258-262`), and
    that string still says 'This book' although the palette is global (`english.yaml:378`)."

VERDICT: CLEAR
