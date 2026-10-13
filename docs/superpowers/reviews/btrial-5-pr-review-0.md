Tier: heavy

# PR review 0: `TagPickerActivity` on the chip grid (btrial-5)

- **Range:** `origin/batch/20261012-tag-chip-picker-2l3m...origin/feature/tag-picker-grid`. The
  batch head and the merge base are both `46b096bc`, so the base has not moved and no sync is due.
- **Code commits:** `a34ed7fe`, `d02f3516`, `0f4a4748` and `64ce4f14`.
- **Worker replies:** none. No empty reply commits are on the branch; this is the first PR review.
- **Read against:**
  - the brief (`hpipe brief --task t5`);
  - the change's spec (`docs/superpowers/specs/2026-10-12-btrial-5-design.md`) and plan
    (`docs/superpowers/plans/2026-10-12-btrial-5-plan.md`);
  - the batch spec's §3, §4 and §6 (`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`).

## What I ran

- **Host suite:** `cmake --build build/test && ctest --test-dir build/test -j 8`. 1748 of 1748
  pass, which is the 1745 baseline minus the 7 `TagRows.*` tests plus 10 new ones.
  `ctest -N | grep -c "TagRows\."` prints `0`.
- **Firmware:** `pio run -e x4pro` succeeds with RAM 66,228 B (unchanged) and Flash 5,560,938 B
  (2,828 B under the 5,563,766 B baseline). These match the plan's trial build.
  - My build was incremental. The worker's full log, `build/btrial-5-pio.log`, compiles
    `TagPickerActivity.cpp.o`, `TagChipGridActivity.cpp.o` and both callers (`:550-561`).
  - Its only `warning:` is third-party (`.pio/libdeps/x4pro/WebSockets/...:573`). None is under
    `src/` or our own `lib/`.
- **Format:** `./bin/clang-format-fix` over the whole tree, then `git status --short`, prints
  nothing.
- **Removals:** `git grep -nw -e STR_TAG_NEW -e TagRowMapping -e 'TagRows::' -- src lib test`
  prints nothing.
  - `STR_TAG_NEW` remains only in markdown under `docs/`.
  - None of the 32 translation YAMLs defines it.
- **Plan fidelity:** I pulled the plan's two code blocks out mechanically and diffed them against the
  committed files. `TagPickerActivity.h` and `.cpp` are byte-identical to the plan.
- **Discoveries:** both of the spec's deferred follow-ups are filed (`hpipe discoveries`):
  - **t5/x1:** the dead `activeTags()`, and the stale comments and user docs;
  - **t5/x2:** the "palette full" message shown for a failed palette write.

## Intent

**Acceptance criteria. Every one is met.**

- **Subclass and chips: met.**
  - `TagPickerActivity final : public TagChipGridActivity` (`TagPickerActivity.h:23`).
  - `buildChips` emits **+ New tag** (`Kind::NewTag`) and then `activeIdsByName()`, names only, with
    the reserve sized first (`TagPickerActivity.cpp:58-74`).
  - `chipState` is `TagChips::pickerState` against `MAX_TAGS_PER_PASSAGE` (`:76-79`).
- **Header: met.**
  - `headerTitle()` formats `Tags n/8` with `snprintf` into `title_` (`:46-50`).
  - `drawChrome` records the band and the rect `drawHeaderWithAction` returns (`:52-56`). The theme
    leaves the action unpainted (`BaseTheme.h:226-232`), so the picker's chip is the only thing
    drawn there.
  - `buildHeaderExtras` draws Done as an Action chip with `ACTION_DONE = ACTION_USER` and
    `InputTouch` only, padded to the band by `bandHitPadding` (`:81-87`). `onEnter` registers it
    with `app.on` after the base `onEnter` (`:27-31`), as Highlights does.
  - **Long-press on Done does nothing.** I confirmed this in the SDK: a long-press release calls
    `findTouch(..., InputLongPress)`, and that kind disables the touch fallback
    (`FreeInkUICore.h:1171-1176`, `:1248-1249`).
  - **Done under the retire popup does nothing.** `onDoneEvent` checks `retireConfirmationOpen()`
    (`:171-175`), and the base's popup consumes input first anyway (`TagChipGridActivity.cpp:205`).
  - **The SDK's edge snap is real.** `button.h` passes the padded rect through
    `ensureMinTouchRect`, which snaps any edge within 12 px of the screen edge out to it, with
    `minTouchSize = 0` too (`FreeInkUI.cpp:836-843`). So device check 5's "a tap just above the
    band" holds.
- **Disabled, flash and repaint: met.**
  - `activateIndex` recomputes `pickerState`, so a tap and a Confirm take the same path.
  - **Disabled** posts `STR_TAG_LIMIT_PER_HIGHLIGHT` and changes nothing (`:95-102`).
  - **A toggle** calls `app.clearTapFlash()` inside the dispatch, then flips the id under
    `RenderLock` (`:107-113`). `route()` checks `flashSuppressed_` before arming the flash
    (`FreeInkApp.h:739`).
  - **A removed tag repaints white,** because Inactive is a white fill
    (`TagChipView.cpp`, `draw`, the Inactive case).
- **Create: met.**
  - The outcome is `pickerState(Kind::Tag, ...)` (`:126-139`):
    - Inactive: apply it, under the lock.
    - Disabled: show the limit message.
    - Active: nothing, because the name deduped to a tag already applied.
  - Every path that returns an id ends in `rebuildChips(ChipKey{Tag, id})`, so the grid lands on the
    new chip's page.
  - `addTagName` never returns an inactive id. A retired name is revived (`TagPalette.cpp:45-49`),
    and the larger `activeCount()` makes `addTagName` persist it (`StudyStore.cpp:253-262`). So a
    created chip always exists and always counts.
- **Open, exits and order: met.**
  - **Open:** the constructor filters the selection through
    `selectedInOrder(palette().activeIds(), ...)` (`:22-25`).
  - **Commit:** `commitAndFinish` uses the same helper (`:177-183`), so the result is in allocation
    order.
  - **Home:** reaches `onHome` → commit through the base's popup-guarded `handleHomeGesture`
    (`TagChipGridActivity.cpp:143-148`).
  - **Back:** the base's cancelled result (`:136-141`).
  - **Callers and result type:** both callers and `TagSelectionResult` have no diff.
- **Removals: met.**
  - `tags_`, `rowItems_`, the held-Confirm retire, the picker's own popup and its `render` copy are
    gone.
  - So are `TagRowMapping.h` and `TagRowsTest.cpp`, with its `CMakeLists.txt` line.
  - `STR_TAG_NEW` is gone, with no remaining code use.
- **Verification: met,** as listed under "What I ran". The submit summary itself is not on the branch,
  so I could not check that it lists the §6 device checks.

**Spec requirements.**

- **Problems 1-7:** each is addressed in code.
- **Assumptions A1-A22:** each holds against the source.
  - **A14, palette edits without `RenderLock`:** the picker's render path never reads the palette.
    `chipState` reads the chip and `selectedIds_`, and the title reads `selectedIds_`. Labels are
    copied on the loop task. The base already retires without the lock on the same argument
    (`TagChipGridActivity.cpp:195-202`).
  - **The create handler's timing:** it runs on the loop task after the picker is current again,
    with no lock held. No `onEnter` runs in between (`ActivityManager.cpp:115-132`).
- **Interaction budget:** 64 chips plus Done is 65, under 96.

**Scope.**

- **No reduction:** both halves of every brief item are implemented, including the cap message on
  create and the landing page after it.
- **No expansion:**
  - The `TagChipRow.h` helpers and the `HeaderLayoutTest` case are in the spec's file table and the
    task's declared files.
  - `HeaderLayout`, `BaseTheme` and `TagChipGridActivity` are untouched.
  - Stale comments outside the picker are deferred to discovery t5/x1, as the spec's Non-goals say.

**Tests.**

- `TagChipSelectedInOrder.*` pin the behaviour the commit order depends on:
  - display order and tap order do not leak;
  - retired, unknown and `UNLABELLED` ids are dropped;
  - duplicates are collapsed;
  - an 8-of-48 selection comes back in order.
- `TagChipBandHitPadding.*` pin the Lyra and Classic paddings and the clamp.
- `HeaderLayout.BandHitPaddingSpansTheBandForEveryLayout` checks the brief's "anywhere in the header
  band's height" against the real `HeaderLayout::layout` output for all 16 layouts. It asserts the 14
  that have an action, so it cannot pass vacuously.
- The activity itself has no host target, the same as `TagFilterActivity`. The spec says so, and
  every decision it makes goes through a tested pure function: `pickerState`, `selectedInOrder`,
  `bandHitPadding` and `reselect`.

**Plan divergence.** None in code. The one divergence the plan names is the toggle comment, which is
explained in the plan's closing section and in commit `21c48f83`.

## Quality

- **Mirrors existing patterns:**
  - **Class shape:** follows `TagFilterActivity`. `onEnter` calls the base, then `rebuildChips`; the
    one render-read member is written under `RenderLock` and carries the same comment;
    `activateIndex` guards on `retireConfirmationOpen()`.
  - **Second action:** a static trampoline registered with `app.on`, as in
    `HighlightsActivity.cpp:40-43`.
  - **`drawChrome` calling `headerTitle()`:** `PublicationsActivity.cpp:39-49` and
    `MeetingsActivity.cpp:54-60` do the same.
  - **The pill's hit padding:** a tested helper doing what `EpubReaderMenuActivity.cpp:450-464`
    does inline.
- **No duplication.**
  - Nothing in `src/` or `lib/` already maps a selection onto palette order.
  - `rawIdsOf` is new to `TagChipRowTest.cpp`, whose other helpers build counts and layouts.
- **Naming:** `selectedInOrder` and `bandHitPadding` sit with their `TagChips` siblings. The test
  suites follow `TagChip<Helper>`.
- **Comments:** each kept comment states a reason the code does not show:
  - the class comment's allocation-order and Home-for-buttons notes;
  - the cross-call contract for `title_`, `headerBand_` and `doneRect_`;
  - the toggle's flash clear;
  - the dedupe case;
  - the reworded `reportAddTagFailure` precedence note.

  `TagPalette::add` checks the name before the full palette (`TagPalette.cpp:43-52`), so that note
  is now accurate. There is no dead or commented-out code.
- **Error handling:** follows the established shape. Every message is posted through
  `ReaderUtils::showMessage` and drawn by the base render after `displayBuffer`
  (`TagChipGridActivity.cpp:282`), which the old picker's render copy never did. The create
  switch's unreachable `Action` case has no `default`, so the compiler keeps checking that the
  switch is exhaustive.
- **Resources:**
  - Nothing new on the render path's heap. Labels are copied on the loop task, and the render-task
    state is 64 bytes inside the activity object.
  - Transient vectors are created only at open, rebuild and commit.
  - The net firmware size goes down.

## Findings

None. I found no BLOCKER, no MAJOR and no MINOR worth the implementer's or the tester's time.

VERDICT: CLEAR
