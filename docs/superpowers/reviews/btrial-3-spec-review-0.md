Tier: standard

# btrial-3 spec review 0 — `docs/superpowers/specs/2026-10-12-btrial-3-design.md`

Reviewed at `5ccdcb3d` against the brief (`hpipe brief --task t3`), the parent design
(`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` §4 "Header and Done", D3, A5) and the
research note. `origin/batch/20261012-tag-chip-picker-2l3m` is still at `59c370cf`
(`git log HEAD..origin/batch/...` is empty), so nothing has drifted under the spec.

## What I verified and found sound

- **Current `drawHeader` control flow** (`BaseTheme.cpp:286-398`): frame at `:292`, token refresh at
  `:299`, the `FONT_SMALL` rebind to `SMALL_FONT_ID` at `:303`, the battery reserve at `:308-321`,
  the `batteryLeavesCornerFree`/`manualRightLabel` predicates at `:335-336`, the detached
  `titleOffsetY` at `:350-354`, the reserve at `:356-361`, the battery rect at `:384-388` and the
  manual label at `:390-397`. All match the spec's description.
- **Measuring font (A1).** `GfxRendererFrame`'s constructor binds `FONT_SMALL` to `smallFontId`
  (`FreeInkUIGfxRenderer.h:249`). `uiScaleSpec().smallFontId` is `UI_10_FONT_ID` (`UIScale.h:16`).
  `tokens.smallText.font` is `FONT_SMALL` (`FreeInkUI.cpp:91`), and `fontStyle` maps `bold` to the
  BOLD face (`FreeInkUIGfxRenderer.h:236`). `UI_10` has a bold face (`main.cpp:125-127`), and
  `SMALL_FONT_ID` is NotoSans 8 regular only (`main.cpp:122-123`). So measuring between `:299` and
  `:303` gives the same width `TagChipView::measure` gets from the picker's `makeUiTarget` target
  (`UiAppHelpers.h:65-71`, `TagChipView.cpp:66-71`). A1 is correct.
- **Width rule (A2).** `chipHeight = max(lh + 2·spaceMd, minTouchSize)` (`TagChipRow.h:191-193`).
  The tokens come from `themeTokensForLineHeight(29)` (`FreeInkUI.cpp:110-120`), giving
  `spaceMd 8`, `spaceLg 16` and `minTouchSize 44`. The result is `max(textW + 32, 44)`.
- **Placement arithmetic.**
  - TitleLine: `action.right = B.right − S − R − M`, and the title's right edge is `action.x − M`.
    That is the brief's growth of exactly `w + M`.
  - Corner: the battery is at `[B.y, B.y+20)` (`:388`) and the action at `[B.y+20, B.y+45)`. They
    are disjoint, and the title clears the action by `M`.
  - A5's both-sides growth by `g` keeps the centre that `drawHeader` produces. A centred title is
    drawn centred within the reserved content rect (`header.h:136-137`,
    `FreeInkUIGfxRenderer.h:162-166`), so equal growth on both sides leaves it in place.
- **A10.** The battery label rect is the indicator rect, 20 px tall (`battery-indicator.h:233-236`).
  The NotoSans 8 line is 23 px, so the text is drawn from `rect.y + max(0, −1) = rect.y`. With
  ascender 18 (`notosans_8_regular.h:3663`), digit and `%` ink ends inside the strip, and the
  `%` glyph has `top 13, height 14` (`:1188`).
- **Call-site count.** `grep … | grep -c "GUI.drawHeader("` gives 27, in 24 files, plus the
  `UiAppHelpers.h:42` comment.
- **Test wiring.** `test/ui_layout/CMakeLists.txt:1-14` is the `ListRowHeightTest` block,
  `ui_layout` is already added at `test/CMakeLists.txt:102`, and `ToastLayout.h:5-6` is the cited
  precedent.
- **Returned rect.** `Rect` default-constructs to zeros (`BaseTheme.h:17`).
- **Hit padding.** `hitPadding` is per-edge `Insets` (`button.h:28`, `:37-40`), so the picker can
  grow a corner rect up to the full band.

## Findings

### MAJOR 1: Under Classic, the 25 px corner rect cannot hold the chip's label

**Claim.** A6: "Under Classic the corner leaves 25 px, so the visual rect is 25 px holding
`UI_10`'s 24 px line." The worked values give `h = 25`, `y = 20..45`.

**Problem.** The rect is consumed by `TagChipView::draw` → `fui::button` (parent §4, "draws an
Action chip at the rect"; `TagChipView.cpp:93-106`). The button draws its label into
`rect.inset(Insets{2, 4, 2, 4})` (`button.h:56`), which is 21 px tall. `GfxRendererTarget::text`
then places a 24 px line at `content.y + max(0, (21 − 24) / 2) = rect.y + 2`
(`FreeInkUIGfxRenderer.h:173`). The line therefore overflows the rect by 1 px rather than fitting
in it.

Measured from the bold `UI_10` glyph tables, the baseline falls at `rect.y + 22`, since the
ascender is 20 (`ubuntu_10_bold.h:4139`).

- Capitals (`D`: `top 15, height 15`, `ubuntu_10_bold.h:1725`) occupy rows `rect.y+7..+21`. That
  leaves 7 px above the text and 2 px below it, so "Done" sits visibly low in the pill.
- A descender (`g`: `top 11, height 15`, `:1760`) runs to row `rect.y+25`. It crosses the 1 px
  bottom border at `rect.y+24` and ends one row below the header band, in the list body.

Shipped translations with descenders hit this:

- German "Erledigt" (`german.yaml:84`)
- Norwegian "Ferdig" (`norwegian.yaml:99`)
- Danish "Færdig" (`danish.yaml:87`)
- Lithuanian "Baigta" (`lithuanian.yaml:99`)

All the host tests T1–T7 still pass with this defect: they check rect geometry, not ink. The
device check is written in English ("Done sits …"), so it would pass too. The defect is a shipped
visual break under Classic with a verification plan that cannot see it.

**Fix.**

1. Correct A6. With `fui::button`'s 2 px vertical inset, a 25 px rect gives the label 21 px, so
   descenders cross the border.
2. State it as a contract note on the returned rect: under Corner, the height is the free room
   (25 px), and a consumer drawing a bordered label must not inset it vertically.
3. File it for the picker task with `hpipe discover --task t3`. Since `feature/tag-picker-grid`
   owns the draw, it must draw the Done label so that its ink stays inside a 25 px pill. For
   example, it can centre on the cap height, or clear the button's vertical inset for this chip.
4. Add a step to the device check: Classic, with German selected, "Erledigt" stays inside its pill
   border and nothing inks below the header band.

The seam's geometry itself (bottom-aligned in `[B.y + batteryBarHeight, B.bottom − U]`) is
correct and should stay.

### MINOR 1: A4 narrows a sentence in the brief's acceptance criteria

**Claim.** A4: the Corner reserve is `max(R, w) + M`, and the centred growth is
`g = max(0, w − R)`.

**Problem.** The brief's acceptance bullet reads: "the title stays centred and gives up the
action's width on both sides." Under A4, with the percent shown on Classic, the title gives up
about 20 px per side, not `w`. The figures are `R ≈ 15 + 2 + 4 + 43 = 64` for "100%" (NotoSans 8
advances, `notosans_8_regular.h:1188`, `:1199-1200`) and `w ≈ 52 + 32 = 84` for "Done". A literal
pr-review would flag this.

The spec's reading is the better engineering: it keeps the title clear by `M` and wastes nothing.
It is also labelled. This is a process point, not a defect.

**Fix.** In Testing, map that acceptance bullet onto T3 and T5 by name, and have the implement
summary cite A4 against it. Raise it with `hpipe decide` only if the orchestrator wants the literal
reading.

### MINOR 2: The device check's "battery percentage hidden" names no setting value

**Claim.** Device check: "*Settings → battery percentage* shown and hidden (`hideBatteryPercentage`
…)".

**Problem.** The setting is "Hide Battery %", with the values Never, In Reader and Always
(`SettingsList.h:272-273`, `english.yaml:98`, `:156-157`). The header hides the percent only on
`HIDE_ALWAYS` (`BaseTheme.cpp:308-309`). A tester who picks "In Reader" (the picker opens from the
reader) still sees the percent in the header and never tests the percent-off layout.

**Fix.** Write it as "*Settings → Hide Battery %* = **Never**, then **Always**".

### MINOR 3: The Detached vertical rule mixes band-relative and absolute coordinates

**Claim.** "Detached: centred on the title line, whose top is `B.height − U − M − titleLineHeight`
… then moved down to `B.y + batteryBarHeight` … and clamped to `B.bottom − U`."

**Problem.** The first term is band-relative, copied from `:352`, where it feeds the relative
`titleOffsetY`. The other two terms are absolute. Every real band has `B.y = topPadding = 5`
(`UiListActivity.cpp:146`, `BaseTheme.h:134`, `LyraTheme.h:11`), so implementing the rule as
written puts the action 5 px high.

The Detached room (`roomTop`/`roomBottom` in the height rule) is also never defined, and Detached
is the only slot without it. Neither theme uses Detached, but T2, T3 and T6 must exercise it.

**Fix.**

- Write the title-line top as `B.y + B.height − U − M − titleLineHeight`.
- Define the Detached room as
  `[battery right ? B.y + batteryBarHeight : B.y, B.bottom − U]`.
- Require the host tests to use `B.y = topPadding`, not 0.

### MINOR 4: T7 is a tautology, and T2/T3 skip the Corner `R ≥ w` branch

**Problem.** As written, T7 checks `HeaderLayout::actionWidth` against a formula restated in the
test. It cannot catch drift from `TagChips::chipHeight` (`TagChipRow.h:191-193`), which is the
point of A2.

The Corner reserve `max(R, w) + M` also has two branches, and the planned inputs (real metrics,
"Done"-sized widths) only reach `w > R`. Short labels do reach `w ≤ R`. Catalan "Fet" is
`w ≈ 33 + 32 = 65` (`catalan.yaml:95`) against `R ≈ 64` at 100%. The floor case `w = 44` against
`R = 64` hits the `g = 0` branch.

**Fix.**

- Have T7 call `TagChips::chipHeight`. The header is host-safe: `test/tag_rows/TagChipRowTest.cpp`
  already includes it.
- Add a Corner case with `w ≤ R` to T2, T3 and T5, where `g = 0` and the reserves are those of
  `drawHeader`.

VERDICT: CLEAR
