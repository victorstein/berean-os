Tier: standard

# btrial-3 PR review 0: header action seam

Range: `origin/batch/20261012-tag-chip-picker-2l3m...origin/feature/header-action-seam`
(head `1564ac07`, equal to the worktree HEAD). Code changes: `src/components/HeaderLayout.h` (new),
`src/components/themes/BaseTheme.h`, `src/components/themes/BaseTheme.cpp`,
`test/ui_layout/CMakeLists.txt`, `test/ui_layout/HeaderLayoutTest.cpp` (new). The rest of the diff
is the research, spec, plan and their reviews.

Worker replies: `git log origin/batch/20261012-tag-chip-picker-2l3m..origin/feature/header-action-seam`
holds no empty reply commits; this is the first PR review.

Verified here, not taken from the worker:

- Host: `HeaderLayoutTest` built in a scratch CMake tree, 16/16 pass.
- Firmware: `pio run -e x4pro` succeeds (RAM 66,220 B, Flash 5,559,490 B). `BaseTheme.cpp.o` was
  recompiled with an empty build cache (the shared `.cache` otherwise serves the object), and it
  produced no warning. The only warning in that log is the existing
  `WebSocketsClient.cpp:573` deprecation in libdeps.
- Format: `./bin/clang-format-fix` (the whole tree) exits 0 and `git status --short` stays empty.

## Intent

**Acceptance criteria, one by one.**

- *Signature exists and returns the action's rect.* `BaseTheme.h:232` matches the brief and the
  parent spec exactly: public, `const`, non-virtual (spec A9). `BaseTheme.cpp:291-294` returns
  `drawHeaderBand(..., nullptr, actionLabel)`, and `:415` builds the `Rect` in field order
  (`BaseTheme.h:10-17`).
- *`drawHeader` and its call sites unchanged; code shared, not duplicated.* The diff touches no
  call site. Only three `src` files change, and `grep -rn "GUI.drawHeader(" src` still finds 27
  calls (the "28" counts the `UiAppHelpers.h:42` comment, spec Non-goals). `drawHeader` keeps its
  `virtual` and default argument (`BaseTheme.h:224-225`) and delegates to the private
  `drawHeaderBand` (`BaseTheme.cpp:287-289`, `BaseTheme.h:260-262`). I compared the
  `actionWidth == 0` path against the base's inline code (`BaseTheme.cpp:346-362`, `:384-388` at
  the base). The reserves, the detached `titleOffsetY` (band-relative, using `band.height`) and the
  battery rect (inset 12 detached or `headerSidePadding` otherwise, `{x, band.y, reserve,
  batteryBarHeight}`) are reproduced exactly at `HeaderLayout.h:59-74`. `titleCentred` reaches
  only the action branch (`:96`). The one new step on the `drawHeader` path is an unconditional
  `lineHeight(FONT_TITLE)` call (`BaseTheme.cpp:373`). It is a table lookup and changes no output.
- *Lyra: on the title line just left of the battery reserve; the right reserve grows by `w +
  spaceMd`.* `HeaderLayout.h:87-88` sets `actionRight = right - S - R - M`. `:86` and `:94` give
  `rightReserve = R + M + w + M`. Pinned by `LyraPutsTheActionOnTheTitleLineLeftOfTheBattery`
  (`HeaderLayoutTest.cpp:247`) with the percent on and off.
- *Classic: lower-right corner below the strip; the title stays centred and gives up the action's
  width on both sides.* The corner placement is `:80`, `:83` and `:100-101`. The predicate
  `batteryBarHeight < band.height` is the one `manualRightLabel` already uses (`BaseTheme.cpp:357`,
  A11). Centring uses equal growth on both sides (`:96`). Covered by `:261`, `:272`, `:283` and
  `:293`. **Narrowed by spec A4, and the change says so.** The title gives up `max(0, w - R)` per
  side, not a further full `w` (`HeaderLayout.h:89-93`). The action shares the battery's columns at
  a different height, so the literal reading would cost up to 128 px of a 480 px title line for no
  gain in clearance. The non-overlap the parent's A5 actually requires holds and is tested
  (`:219`, `:283`). The spec records this narrowing (A4, "Acceptance mapping"), spec review 0
  accepted it as MINOR 1, and the plan's hand-back requires the submit summary to cite it
  (`plan:1006`). I agree with the reading. This is not a finding.
- *Battery left or detached: at the title line's right edge.* `:85` with no branch taken. Covered
  by `BatteryLeftOrDetachedPutsTheActionAtTheTitleLineRightEdge` (`:310`) and
  `DetachedActionCentresOnTheTitleLineBelowTheStrip` (`:322`).
- *Every combination, percent on and off, overlaps neither the battery nor the title; a long title
  truncates.* `allLayouts` (`HeaderLayoutTest.cpp:103-115`) covers 2 themes × 4 battery layouts ×
  percent on/off, each at a "Done"-sized width and a floor-sized width (the Corner `w <= R` case).
  `ActionNeverOverlapsTheBattery` and `ActionNeverOverlapsTheTitle` check every one. `titleBox`
  (`:198-202`) matches how `fui::header` actually narrows the title rect
  (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/header.h:72-78`: side-padding inset,
  `leftReserve` shifts x, both reserves shrink the width). The title clears the action by
  `spaceMd`, and truncation follows because `fui::header` lays the title out in that narrowed rect.
- *Build, format, device check in the summary.* Build and format are verified above. The device
  procedure is in the spec's Testing strategy (both themes, Hide Battery % Never then Always, plus
  German under Classic), and the plan's hand-back (`plan:1007`) requires the submit summary to
  carry it. I cannot see the submit summary from the branch.

**Spec requirements beyond the acceptance list.**

- The action is measured in the uiScale small font, bold, *before* the `FONT_SMALL` rebind (A1):
  `BaseTheme.cpp:312-320` comes ahead of the `setFont` at `:324`. It mirrors
  `TagChipView.cpp:57-72` term for term: `smallText` with `bold = true`,
  `measureText(style.font, ...)`, `lineHeight(smallText.font)` and `chipHeight`'s
  `max(lh + 2·spaceMd, minTouch)`. The theme does not include `TagChipView`.
- A null or empty label skips measurement and returns an all-zero `Rect` (A7):
  `BaseTheme.cpp:314` and `HeaderLayout.h:76`.
- The theme does not paint the action (A3). `drawHeaderBand` draws nothing new.
- Height is `min(B.height - 8, room)` and the inset is 4, matching FreeInkUI's trailing button
  (`header.h:105`, `:122-124`). Under Corner the action is bottom-aligned, and under Detached it is
  centred on the title line, then clamped (`HeaderLayout.h:98-106`). The worked values match the
  spec: Lyra `y = 9, h = 36` and Classic `y = 25, h = 25` (`HeaderLayoutTest.cpp:257-258`,
  `:266-268`).
- The returned rect is recomputed every call. `ActionMovesWithTheBatteryPercentOnTheTitleLine`
  (`:341`) checks that it moves with the percent text.
- No heap allocation. `Inputs`, `Result` and two `Box`es are small stack PODs, as the spec
  budgeted.

**Scope.** There is no expansion. No call site, FreeInkUI file, token, i18n string or
`UiListActivity` changes, and the picker's use of the seam is left to `feature/tag-picker-grid`.
The Classic corner-height contract on the draw side (A6) was filed for the picker as discovery
`t3/x1`, and the header comment carries it to the consumer (`BaseTheme.h:226-231`).

**Plan divergence.** None. The final `HeaderLayout.h` is byte-identical to the plan's Task 3 code
block. `drawHeaderBand` matches Task 4's code except for clang-format's line wrapping of the
`fui::batteryIndicator` call. The test file and the CMake block follow Tasks 1-3, and both
plan-review MINORs were applied (`plan:1009-1014`).

**Tests exercise behaviour.** Placement is asserted through properties (no intersection, the
`spaceMd` clearance, staying inside the band above the rule, equal centring growth) across every
combination, plus literal values for the two real themes. T7 calls the real
`TagChips::chipHeight` instead of re-deriving it. The `legacy()` oracle (`:127-145`) transcribes
the pre-change inline arithmetic. Because it is an independent copy,
`ClassicAndLyraReservesWithoutAnAction` (`:167`) also pins literal values, so T1 does not merely
restate `layout`.

## Quality

- **Pattern.** It mirrors `drawToast` + `src/components/ToastLayout.h` + `ToastLayoutTest`: a
  renderer-free, `constexpr`, plain-`int` namespace header with a host test, and a theme method
  that measures, calls the layout and draws. The naming (`HeaderLayout::Box`, `layout`,
  `actionWidth`) sits alongside `ToastLayout::Bounds` and `bounds`. The test block copies the
  `ui_layout` CMake shape, plus the `lib/StudyStore` include and the `ArduinoJson` link that
  `TagChipRow.h` needs, as `test/tag_rows` already does.
- **One way, not two.** The battery placement and reserve logic moved into `HeaderLayout` instead
  of being forked, so `drawHeader` and the overload share one body and one layout function. The
  `12` inset that the old code inlined is now the named `DETACHED_BATTERY_INSET`, and the SDK's
  `4`/`-8` trailing-button geometry is `ACTION_INSET`, with its source cited.
- **Comments.** They explain why, not what: measuring before the rebind, the corner sharing
  columns, equal growth keeping the centre, and the detached clearance carried over from the old
  body. None paraphrases the next line, and there is no dead or commented-out code.
- **Error handling.** It matches the spec's shape: no logging on a per-frame draw path, an empty
  rect for an empty label, and a non-positive height for a room too small (exercised by
  `DetachedStripAsTallAsTheBandLeavesNoRoom`, `:335`).
- **Resources and style.** No `std::string` and no heap allocation. Constants are `constexpr` and
  the header has `#pragma once`. `int16_t` casts happen only at the FreeInkUI boundary, as before.

## Findings

None. I found no BLOCKER, MAJOR or MINOR. The one departure from the brief's wording (A4, the
Classic title giving up only `max(0, w - R)` per side) is already flagged in the spec and accepted
in spec review. It is geometrically sound, and the submit summary is already required to cite it
for the owner.

VERDICT: CLEAR
