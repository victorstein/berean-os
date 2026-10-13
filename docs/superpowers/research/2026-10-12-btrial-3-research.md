# btrial-3 — research: header action seam (`BaseTheme::drawHeaderWithAction`)

Branch `feature/header-action-seam` at `59c370cf`, which is also the merge base with
`origin/batch/20261012-tag-chip-picker-2l3m` (`git merge-base HEAD origin/batch/...`). Every
`file:line` below was read in this worktree. `grep -rn "drawHeaderWithAction\|HeaderAction" src`
returns nothing: the seam does not exist yet.

## Who owns the behaviour

| Concern | Owner |
|---|---|
| The header draw, used by both themes | `BaseTheme::drawHeader`, `src/components/themes/BaseTheme.cpp:286-398`, declared `virtual` at `BaseTheme.h:224-225` |
| Theme selection | `UITheme::setTheme`: `CLASSIC` → `BaseTheme` (`src/components/UITheme.cpp:29-31`); `LYRA`, `LYRA_3_COVERS`, `ROUNDEDRAFF` → `LyraTheme` (`:36-41`). Default `uiTheme = LYRA` (`src/CrossPointSettings.h:331`) |
| Lyra's overrides | `LyraTheme.h:84-100`. **No `drawHeader` override**: Lyra goes through `BaseTheme::drawHeader` with Lyra metrics |
| Metrics the header reads | `UITheme::getInstance().getMetrics()` (`BaseTheme.cpp:304`, `UITheme.cpp:48-56`), not the class's own `BaseMetrics` — so one body serves both themes |
| Metric → token mapping | `uiThemeTokens` (`src/components/UIThemeTokens.h:12-39`): `headerHeight`, `headerSidePadding`, `headerUnderline`, `headerTitleAlign` |
| Token publication | `refreshSharedUiThemeTokens` (`src/components/UiAppHelpers.h:45-53`) |
| Header layout primitive | `fui::header` (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/header.h:62-163`) |
| Chip measurement to reproduce | `TagChipView::measure` (`src/activities/reader/TagChipView.cpp:66-71`), `metricsFor` (`:57-64`), `TagChips::chipHeight` (`src/activities/reader/TagChipRow.h:191-193`) |
| The future consumer | `UiListActivity::drawChrome` (`src/activities/UiListActivity.cpp:142-147`), overridden by the picker in a sibling task |

## Current control flow of `drawHeader`

`BaseTheme.cpp`, in order:

1. `:292` builds a private `fui::GfxRendererFrame<1>` from `uiScaleSpec()` fonts. The frame holds
   one interaction and is discarded on return, so nothing it could register reaches a screen.
2. `:299` `refreshSharedUiThemeTokens(ui.target)` — the same tokens every `FreeInkApp` screen reads.
3. `:303` **rebinds the `FONT_SMALL` slot to `SMALL_FONT_ID`** for the battery percent and the
   right label. From here on, `tokens.smallText` measured through `ui.target` is
   `SMALL_FONT_ID`, not the uiScale small font.
4. `:308-321` battery reserve: `batteryWidth + 2` (nub), plus, unless `hideBatteryPercentage ==
   HIDE_ALWAYS`, `batteryPercentSpacing` (4, `BaseTheme.h:249`) and the measured width of
   `"<n>%"`. So the reserve changes with the percentage's digit count every render.
5. `:323-339` `props`: `rightLabel = subtitle`; `batteryLeft = headerBatterySide == 1`,
   `batteryDetached = headerBatteryDetached`; `batteryLeavesCornerFree = batteryBarHeight <
   rect.height` (`:335`); `manualRightLabel = subtitle && !detached && !left && cornerFree`
   (`:336`), in which case the component gets no `rightLabel`.
6. `:340-345` title style: `titleText.align = tokens.headerTitleAlign`; `props.centered` is never
   set. Classic centres through the text style, not through `props.centered`.
7. `:346-362` detached: title anchored above the bottom rule via `titleOffsetY`; otherwise
   `reserve = batteryReserve + spaceMd` goes to `leftReserve` (battery left) or `rightReserve`.
8. `:365-369` underline under a titled header, then `fui::header(ui.frame, band, props)`.
9. `:371-388` `fui::batteryIndicator` at `x = band.right() - inset - batteryReserve`
   (right) or `band.x + inset` (left), `y = band.y`, height `batteryBarHeight`; inset is 12 when
   detached, else `headerSidePadding` (`:384`).
10. `:390-397` manual right label: `x = band.right() - headerSidePadding - labelW`,
    `y = band.bottom() - headerUnderline - spaceSm - lineHeight(FONT_SMALL)`.

Inside `fui::header` (`header.h`): content = band inset by `sidePadding` (`:72-73`), minus
`leftReserve`/`rightReserve` (`:74-78`). The title rect is `content` (`:136-137`) and is drawn with
`frame.target().text` (`:158`), which truncates a single line to the rect width
(`FreeInkUIGfxRenderer.h:177-181`) and clamps a centred line to the rect's left edge (`:161`).
A `rightLabel` shrinks the title on both sides only when `props.centered` (`:150-156`), which the
firmware never sets. The built-in `trailingAction` button anchors at `rect.right() - 4 - btnW`
(`:122-125`), on top of where both themes draw the battery, and the header is the reason it
cannot be used: the spec says so (§4 "Header and Done") and the code agrees.

### The two theme combinations that exist

| | Classic (`BaseTheme.h:131-204`) | Lyra (`LyraTheme.h:8-82`) |
|---|---|---|
| `headerHeight` | 45 | 44 |
| `batteryBarHeight` | 20 → corner free (`:335`) | 44 → corner not free |
| `headerTitleAlign` | 1, centred | 0, left |
| `headerBatterySide` / `Detached` | 0 / false | 0 / false |
| `headerSidePadding` | 18 | 18 |
| `headerUnderlineSize` | 0 | 3 |
| `batteryWidth` | 15 | 16 |

Battery-left and detached are reachable only through the code paths; neither theme sets them.

### Token values on this board

`themeTokensForLineHeight` (`freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:110-120`) starts from
the `ThemeTokens` defaults (`FreeInkUICore.h:634-637`: `spaceSm 4`, `spaceMd 8`, `spaceLg 16`,
`minTouchSize 44`) and raises `minTouchSize` to `lineHeight + 14` and `spaceSm` to
`lineHeight / 6` when larger. The body font is `UI_12_FONT_ID` (`src/components/UIScale.h:17`),
Ubuntu 12, whose `advanceY` is 29 (`lib/EpdFont/builtinFonts/ubuntu_12_regular.h`, third field of
the `EpdFontData` initializer; `getLineHeight` returns `advanceY`, `GfxRenderer.cpp:2143-2151`).
29 + 14 = 43 and 29 / 6 = 4, so neither raise fires: **`spaceSm 4`, `spaceMd 8`, `spaceLg 16`,
`minTouchSize 44`.** These are derived, not measured on the device.

## What "measure as `TagChipView::measure` does" means

```cpp
fui::TextStyle style = screen.theme().smallText;   // TagChipView.cpp:67
style.bold = true;
const int textWidth = screen.target().measureText(style.font, label, style).width;
return std::max(textWidth + 2 * metrics.padX, metrics.chipHeight);   // padX = spaceLg (:60)
```

Two facts the spec has to settle that the brief does not spell out:

1. **Font.** The picker's `screen.target()` has `FONT_SMALL` bound to the uiScale small font,
   `UI_10_FONT_ID` (`UIScale.h:16`, bound by `makeUiTarget`, `UiAppHelpers.h:65-71`): Ubuntu 10,
   regular and bold (`src/main.cpp:125-127`). The header frame rebinds that slot to
   `SMALL_FONT_ID` at `BaseTheme.cpp:303`, which is NotoSans 8 **regular only**
   (`src/main.cpp:122-123`, `:321`). A measurement taken through `ui.target` after `:303` would
   be in the wrong font and could not be bold. To match the chip the picker draws, the action must
   be measured in the uiScale small font, bold — before the rebind, or by font id.
2. **Floor.** `measure` floors the width at `chipHeight` = `max(lineHeight + 2·spaceMd,
   minTouchSize)` (`TagChipRow.h:191-193`). With `UI_10` (`advanceY` 24,
   `ubuntu_10_bold.h`/`ubuntu_10_regular.h`) that is `max(24 + 16, 44) = 44`, so the floor binds
   only for labels narrower than 12 px of text. "Done" and its translations are wider, but the
   spec should say whether the floor is reproduced.

## Vertical room in each placement

- **Classic corner.** The strip is 20 px of a 45 px band, so 25 px lie below it with no underline
  (`headerUnderlineSize = 0`). `UI_10` is 24 px tall; a chip at `chipHeight` (44) cannot fit there.
  The spec's own wording ("visual rect"; `hitPadding` grows the hit rect to the full band, §4)
  implies the visual rect is shorter than the touch target. The spec must give the rect's height.
- **Classic title vs the corner.** The title (`UI_12`, 29 px) is centred in 45 px, so it spans
  y 8–37; the manual right label spans y 18–41 (`:393-396`, `SMALL_FONT_ID` 23 px). They overlap
  vertically, which is why the action's width must come out of the title. Today's
  `manualRightLabel` does **not** reserve its width from the title: a long Classic title can run
  under a wide `subtitle`. No current caller hits it — the only subtitled callers are Settings
  (`SettingsActivity.cpp:483-484`, short title, `BEREAN_VERSION`) and Home
  (`LauncherActivity.cpp:258`, `:261`, `STR_BIBLE`) — so it is latent and outside this change,
  which leaves `drawHeader`'s behaviour untouched.
- **Lyra title line.** The battery is centred on the 44 px title line; the action shares that line,
  between the title's right edge and the battery reserve.

## Call sites

`grep -rn "drawHeader(" src lib | grep -v "^src/components/themes/BaseTheme"` prints 28 lines: 27
`GUI.drawHeader(` calls in 24 files, plus one comment (`src/components/UiAppHelpers.h:42`). The
spec's "28 call sites" counts that comment; the substance — leave every caller alone — is
unaffected.

## Nearest existing example

`BaseTheme::drawToast` (`BaseTheme.cpp:503-550`) is the closest precedent: a `BaseTheme` draw whose
geometry was pulled into a renderer-free header, `src/components/ToastLayout.h` ("free of the
renderer and FreeInkUI so it can be host-tested", `:5-6`), with host tests in
`test/posted_message/ToastLayoutTest.cpp` built by `test/posted_message/CMakeLists.txt`.
`components/Masthead` with `test/masthead/MastheadLayoutTest.cpp` follows the same split.
The draw itself needs the renderer and is checked on the device (`ToastLayoutTest.cpp:1-2`).

A pure placement function (inputs: band, battery reserve, metrics, action width, title alignment;
outputs: left/right reserves and the action rect) fits that pattern and makes the acceptance
matrix — three battery layouts × percent on/off × long title — host-testable. A new test can be
added to an existing suite directory's `CMakeLists.txt` (e.g. `test/ui_layout/CMakeLists.txt`,
which already holds several `add_executable` blocks); a new directory would need a line in the
shared `test/CMakeLists.txt` (`:80`, `:102`, `:127` show the `add_subdirectory` form), which
`ui-dev.md` says to report rather than edit.

## Installed versions

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on `PATH` in this shell) |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| Framework | arduino | `platformio.ini:17` |
| `freeink-sdk` (FreeInkUI) | submodule at `7dde134`, 2026-10-11 | `git submodule status`; `git -C freeink-sdk log -1` |
| CMake (host tests) | 4.4.2 | `cmake --version` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:15-17` |

## Tier

`standard` stands. The change is one surface (`src/components/themes`, owned by ui), adds an
overload without changing `drawHeader`'s signature or its callers, needs no store, format or
migration, and the contract it offers the picker is already fixed by the approved spec.
