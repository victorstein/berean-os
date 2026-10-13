Tier: standard

# btrial-3 plan review 0: header action seam

Plan: `docs/superpowers/plans/2026-10-12-btrial-3-plan.md`
Spec: `docs/superpowers/specs/2026-10-12-btrial-3-design.md`
Code read at the worktree HEAD (`1692b180`, `src/` unchanged since `59c370cf`).

## What was checked

- **Spec coverage.** Every Placement rule maps to code in Task 3 Step 3 (`plan:698-727`):
  TitleLine/Corner/Edge/Detached slot predicates (`plan:698-703`, the same
  `batteryBarHeight < rect.height` predicate as `BaseTheme.cpp:335`, A11), growth
  `w + M` / `max(0, w - R)` (`plan:706-713`, A4), equal growth on both sides for centred titles
  (`plan:716`, A5), `h = min(B.height - 8, room)` (`plan:718`, A6), Corner bottom-aligned,
  Detached centred on the absolute title line then clamped into the room (`plan:719-726`). Data
  flow step 2 (measure bold in the uiScale small font *before* the rebind, A1) is at `plan:850-859`,
  ahead of the `setFont` at `plan:863`. Error handling: null/`""` label skips measurement and
  returns `Rect{}` (`plan:853`, `:696`, `:954`); a null title still places the action (layout does
  not read the title). T1-T7 each have a test; T7 calls the real `TagChips::chipHeight`
  (`plan:99`, `:113`). The device check is carried into the hand-back (`plan:1013`).
- **`drawHeader` unchanged — the wiring no host test sees.** T1's `legacy()` (`plan:269-284`) was
  checked line by line against `BaseTheme.cpp:346-362` and `:381-388`: detached `titleOffsetY`
  uses `band.height`, reserve is `batteryReserve + spaceMd` on the battery's side only when not
  detached, battery inset is 12 detached / `headerSidePadding` otherwise, rect is
  `{x, band.y, batteryReserve, batteryBarHeight}`. The new body's `Inputs` (`plan:907-918`) feed
  exactly the values the old body used (`tokens.headerUnderline`, `tokens.headerSidePadding`,
  `tokens.spaceMd`, `lineHeight(FONT_TITLE)`, `metrics.batteryBarHeight`, `headerBatterySide == 1`,
  `headerBatteryDetached`), and `titleCentred` only feeds the action branch, so the no-action path
  is the old arithmetic. The only new work on that path is an unconditional
  `lineHeight(FONT_TITLE)` call, which is a table lookup on a font that always exists.
- **The title model in T3.** `titleBox` (`plan:460-463`) matches what `fui::header` actually does
  with the reserves (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/header.h:76-82`,
  `:127-129`): side padding inset, then `leftReserve` shifts x, both shrink width, `titleOffsetY`
  shifts y. So "never overlaps the title" is tested against the real rect, not a guess.
- **Measurement matches the chip.** `plan:854-858` mirrors `TagChipView.cpp:57-71`
  (`smallText` with `bold = true`, `measureText(style.font, ...)`, `lineHeight(smallText.font)`);
  `smallText.font` is slot 0 = `FONT_SMALL` (`FreeInkUIGfxRenderer.h:33`, `FreeInkUICore.h:1447`),
  and `measureText` honours `bold` (`FreeInkUIGfxRenderer.h:236`).
- **Return type.** `Rect` is `{x, y, width, height}` with an explicit 4-int constructor
  (`BaseTheme.h:10-17`); `Rect{placed.action.x, placed.action.y, placed.action.width,
  placed.action.height}` (`plan:954`) is in field order.
- **No override hides the split.** No theme overrides `drawHeader` (`grep drawHeader
  src/components/themes` hits only `BaseTheme.h:224` and `BaseTheme.cpp:286`), so a non-virtual
  `drawHeaderWithAction` reaching the private body is the same header on every theme (A9).
- **Red/green counts.** Before Task 3's implementation the action is all-zero: the battery-overlap,
  centring and no-room tests pass vacuously and the other nine fail, so "9 failed, 7 passed"
  (`plan:614`) and "16 tests" (`plan:738`) are right.
- **File lock.** `plan:13-14` lists all five files any step edits (`HeaderLayout.h`,
  `BaseTheme.h`, `BaseTheme.cpp`, `test/ui_layout/CMakeLists.txt`, `HeaderLayoutTest.cpp`), at
  column 0, outside code blocks, repo-relative. The only other writes are `build/test` and
  `build/pio-btrial-3.log`, under the gitignored `build` (`.gitignore:13`).
- **No push, PR or `hpipe submit` step.** The header forbids them (`plan:3`); the hand-back only
  describes what the orchestrating worker does afterwards (`plan:1010`).

No BLOCKER or MAJOR found.

## MINOR

### MINOR 1 — Task 4 Step 4's `git diff --stat` expectation is wrong for the commit state

- **Claim.** `plan:962`: "`git diff --stat` shows only the five files in this plan's `FILES:` lines."
- **Problem.** Tasks 1-3 have already committed `HeaderLayout.h`, `HeaderLayoutTest.cpp` and the
  CMake block (`plan:166`, `:429`, `:743`), so at Task 4 Step 4 the working-tree diff holds only
  `BaseTheme.h` and `BaseTheme.cpp`. An implementer who takes "the five files" literally may go
  looking for three missing files.
- **Fix.** Reword to: "`git diff --stat` shows only `src/components/themes/BaseTheme.h` and
  `src/components/themes/BaseTheme.cpp`; `git diff --stat main...HEAD` plus those two stays within
  the `FILES:` lines."

### MINOR 2 — Task 4 Step 6 calls itself the whole `ui_layout` suite but reruns one target Task 4 cannot change

- **Claim.** `plan:976-980`: "Run the whole `ui_layout` host suite again", then
  `ctest ... -R HeaderLayout`.
- **Problem.** The command filters to `HeaderLayout` only, and Task 4 edits nothing that
  `HeaderLayoutTest` compiles (it reaches neither `BaseTheme.h` nor `BaseTheme.cpp`; the plan says
  so at `plan:751`). It is a re-run of a target that already passed in Task 3 Step 4, which the
  repo's testing checklist says not to do, and its title overstates what it covers.
- **Fix.** Delete the step. If a whole-suite run is wanted as a final gate, make it
  `ctest --test-dir build/test -R "HeaderLayout|ListRowHeight|BibleReference" --output-on-failure`
  (or no `-R`) and call it what it is.

## Verdict

The plan implements the spec as written, including the A4 narrowing (which the hand-back flags,
`plan:1012`), and the one path no host test reaches, `BaseTheme`'s wiring, transcribes the old
arithmetic exactly. The two MINORs are wording fixes, to be made inline.

VERDICT: CLEAR
