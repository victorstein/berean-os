# btrial-3 — design: header action seam (`BaseTheme::drawHeaderWithAction`)

Parent design: `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (D3, Non-goals, §4
"Header and Done", A5). Where this document and the parent disagree, the parent wins, and every
place this document narrows it is listed under **Assumptions**. Evidence:
`docs/superpowers/research/2026-10-12-btrial-3-research.md`. Every `file:line` below was read at
`59c370cf`.

**Modelled on:** `BaseTheme::drawToast` (`src/components/themes/BaseTheme.cpp:503-550`) and its
renderer-free geometry `src/components/ToastLayout.h`, host-tested by
`test/posted_message/ToastLayoutTest.cpp`. The same split — the theme measures and draws, a
`constexpr` header decides where — is used here.

## Problem

The tag picker needs a touchable **Done** in its header (parent D3). The header has no slot for
one:

- The theme draws the header through its own `fui::GfxRendererFrame<1>` (`BaseTheme.cpp:292`),
  discarded on return, so nothing it could register reaches the screen.
- FreeInkUI's `trailingAction` anchors at the band's right edge (`header.h:122-125`), where both
  themes put the battery (`BaseTheme.cpp:385-386`).
- The subtitle slot is not a button-height slot: under Classic it is drawn one small-text line
  tall in the corner below the battery strip (`BaseTheme.cpp:335-339`, `:390-397`).

So the caller cannot place a chip itself without knowing the battery's width (which changes with
the percentage, `:308-321`) and which battery layout the theme uses.

## Goal

```cpp
Rect BaseTheme::drawHeaderWithAction(const GfxRenderer& renderer, Rect rect, const char* title,
                                     const char* actionLabel) const;
```

draws the same header `drawHeader` draws, with room made for one action label clear of the title
and the battery, and returns where that action goes. It **does not paint** the action (A3); the
picker draws a touchable chip at the returned rect (parent §4).

## Non-goals

- `drawHeader`'s signature, behaviour and its call sites. `grep -rn "drawHeader(" src lib`
  outside `BaseTheme` lists 27 `GUI.drawHeader(` calls in 24 files plus one comment
  (`src/components/UiAppHelpers.h:42`); the parent's "28" counts the comment. None changes.
- Any use of the seam: `TagPickerActivity::drawChrome`, the Done chip, its action id and hit
  padding belong to `feature/tag-picker-grid`. `UiListActivity::drawChrome`
  (`src/activities/UiListActivity.cpp:142-147`) is unchanged.
- A subtitle together with an action. The overload has no subtitle parameter (parent signature).
- The latent Classic overlap between a long title and a wide `subtitle` in today's `drawHeader`
  (research, "Vertical room"): no caller triggers it, and fixing it would change `drawHeader`.
- Any FreeInkUI (`freeink-sdk`) change, new font, token, metric, i18n string, store or format.

## Architecture

Two pieces, mirroring `drawToast` + `ToastLayout`:

### 1. `src/components/HeaderLayout.h` — pure geometry (new)

Renderer- and FreeInkUI-free, `constexpr`, plain `int`s, so it builds on the host exactly as
`ToastLayout.h` does (`ToastLayout.h:5-6`). It owns every number `drawHeader` computes today from
metrics and measured widths, so that both entry points get them from one place:

```cpp
namespace HeaderLayout {
struct Box { int x; int y; int width; int height; };

// TagChipView::measure's width rule (TagChipView.cpp:66-71, TagChipRow.h:191-193),
// reproduced because the theme must not include reader code.
constexpr int actionWidth(int boldTextWidth, int textLineHeight, int spaceLg, int spaceMd, int minTouchSize);

struct Inputs {
  Box band;                 // the rect drawHeader was given
  int sidePadding;          // tokens.headerSidePadding
  int underline;            // tokens.headerUnderline
  int spaceMd;
  int titleLineHeight;      // lineHeight(FONT_TITLE)
  int batteryReserve;       // measured, BaseTheme.cpp:316-321
  int batteryBarHeight;     // metrics.batteryBarHeight
  bool batteryLeft;         // metrics.headerBatterySide == 1
  bool batteryDetached;     // metrics.headerBatteryDetached
  bool titleCentred;        // tokens.headerTitleAlign == Center
  int actionWidth;          // 0 = no action: drawHeader's layout
};

struct Result {
  int leftReserve;          // -> HeaderProps::leftReserve
  int rightReserve;         // -> HeaderProps::rightReserve
  int titleOffsetY;         // -> HeaderProps::titleOffsetY (detached only, else 0)
  Box battery;              // the rect handed to fui::batteryIndicator
  Box action;               // {0,0,0,0} when actionWidth == 0
};

constexpr Result layout(const Inputs& in);
}
```

With `actionWidth == 0`, `layout` must return exactly what `drawHeader` computes inline today:
the reserve from `:346-362`, the detached `titleOffsetY` from `:350-354`, and the battery rect from
`:384-388`. That equivalence is a host test (T1), and it is what lets `drawHeader` move onto the
module without a behaviour change.

### 2. `BaseTheme` — one private body, two public entry points

`drawHeader`'s body (`BaseTheme.cpp:286-398`) moves into a private, non-virtual

```cpp
Rect drawHeaderBand(const GfxRenderer&, Rect rect, const char* title, const char* subtitle,
                    const char* actionLabel) const;
```

- `drawHeader(r, rect, title, subtitle)` stays `virtual` with the same default argument
  (`BaseTheme.h:224-225`) and calls `drawHeaderBand(r, rect, title, subtitle, nullptr)`, discarding
  the result.
- `drawHeaderWithAction(r, rect, title, actionLabel)` is a new public, **non-virtual** `const`
  member (A9) that returns `drawHeaderBand(r, rect, title, nullptr, actionLabel)`.

Inside the body, the inline reserve, `titleOffsetY` and battery-x arithmetic are replaced by one
`HeaderLayout::layout` call; everything else (`props` styling, underline, `fui::header`,
`fui::batteryIndicator`, the manual right label) is untouched.

## Placement rules

Let `w` be the action width, `g` the reserve growth on the action's side, `S = sidePadding`,
`R = batteryReserve`, `M = spaceMd`, `U = underline`, and `B = band`. The action is always
right-anchored. Slot selection reuses `drawHeader`'s existing predicates (`BaseTheme.cpp:330-336`):

| Slot | When | Action x | Action vertical room | Action-side reserve |
|---|---|---|---|---|
| **TitleLine** | battery right, not detached, `batteryBarHeight >= B.height` (Lyra) | `B.right - S - (R + M) - w` | the band above the underline | `rightReserve = R + M + w + M` (`g = w + M`) |
| **Corner** | battery right, not detached, `batteryBarHeight < B.height` (Classic) | `B.right - S - w` | `[B.y + batteryBarHeight, B.bottom - U]` | `rightReserve = max(R, w) + M` (`g = max(0, w - R)`) |
| **Edge** | battery left, not detached | `B.right - S - w` | the band above the underline | `rightReserve = w + M` (`g = w + M`) |
| **Detached** | `headerBatteryDetached` | `B.right - S - w` | the title line (below), pushed below the battery strip when the battery is on the right | `rightReserve = w + M` (`g = w + M`) |

- **Centred titles** (Classic, `headerTitleAlign = 1`, `BaseTheme.h:153`) also grow the *other*
  reserve by the same `g`, so the title's centre stays exactly where `drawHeader` puts it for the
  same battery state (A5). A left-aligned title (Lyra) grows only the action side.
- **Height.** `h = min(B.height - 8, roomBottom - roomTop)`. `B.height - 8` is FreeInkUI's own
  trailing-button height, inset 4 px top and bottom (`header.h:105`, `:122-124`) (A6).
- **Vertical position.**
  - TitleLine and Edge: `y = B.y + 4` (the SDK trailing button's inset, `header.h:123`), then
    clamped so `y + h <= B.bottom - U`.
  - Corner: bottom-aligned in the room, `y = B.bottom - U - h`, the corner `manualRightLabel`
    pins its label to (`BaseTheme.cpp:393-395`).
  - Detached: centred on the title line, whose top is `B.height - U - M - titleLineHeight`
    (`BaseTheme.cpp:352`), then moved down to `B.y + batteryBarHeight` if the battery is on the
    right and the result would start above it, and clamped to `B.bottom - U`.
- **Width.** `w = max(boldTextWidth + 2·spaceLg, max(textLineHeight + 2·spaceMd, minTouchSize))`,
  `TagChipView::measure` exactly (A2). On this board that is `max(textW + 32, 44)` (research,
  "Token values").

Worked values (derived from metrics and font tables, not measured on the device — research,
"Token values"): Lyra, 44 px band, `U = 3`: the action is 36 px tall at `y = 4`, ending at 40,
above the rule at 41. Classic, 45 px band, `U = 0`, 20 px strip: room 25 px, `h = 25`,
`y = 20..45`.

## Data and control flow

Everything runs on whichever task already calls `drawHeader` — the render task, inside one
`render()` (parent §4). Per call:

1. Build the frame and refresh tokens, unchanged (`BaseTheme.cpp:290-299`).
2. **New, only when `actionLabel` is non-empty, and before the small-font rebind at `:303`:**
   `actionText = tokens.smallText` with `bold = true`; `textW = ui.target.measureText(FONT_SMALL,
   actionLabel, actionText).width`; `lh = ui.target.lineHeight(FONT_SMALL)`;
   `w = HeaderLayout::actionWidth(textW, lh, tokens.spaceLg, tokens.spaceMd, tokens.minTouchSize)`.
   At this point `FONT_SMALL` is `spec.smallFontId = UI_10_FONT_ID` (`BaseTheme.cpp:292`,
   `UIScale.h:16`) — the font the picker's screen measures and draws chips in
   (`UiAppHelpers.h:65-71`, `TagChipView.cpp:66-69`). After `:303` it would be `SMALL_FONT_ID`,
   NotoSans 8 regular with no bold face (`src/main.cpp:122-123`) (A1).
3. Rebind, measure the battery percent and compute `batteryReserve`, unchanged (`:303-321`).
4. `HeaderLayout::layout(inputs)`; apply `leftReserve`, `rightReserve`, `titleOffsetY` to `props`.
5. `fui::header`, then `fui::batteryIndicator` at `result.battery`, then the manual right label
   (only reachable from `drawHeader`, since the overload passes no subtitle) — unchanged.
6. Return `Rect{result.action.x, result.action.y, result.action.width, result.action.height}`.

The result is recomputed every call: the percent text's width changes `R`, which moves the action
under TitleLine and changes `g` under Corner (parent §4, last bullet).

**Resources.** No heap allocation: `Inputs`, `Result` and two `Box`es are under 100 bytes of
stack on the render path, beside the existing `percentText[8]` and `HeaderProps`. One extra
`measureText` and `lineHeight` per call, and only for the overload. No new flash table; the
module is header-only `constexpr`.

## Error handling

- **`actionLabel` null or `""`:** no measurement, `actionWidth = 0`, the header is exactly
  `drawHeader(r, rect, title)`, and the return is `Rect{}` (all zero) — an empty rect the caller
  can test with `width <= 0` (A7).
- **`title` null:** as today, no title and no underline (`BaseTheme.cpp:365`); the action is still
  placed.
- **Font missing:** `GfxRenderer::getLineHeight` logs and returns 0 (`GfxRenderer.cpp:2143-2148`);
  `measureText` then yields 0 and `w` floors at `minTouchSize`. The rect stays valid.
- **Too little room:** reserves larger than the content make the title rect empty, and
  `GfxRendererTarget::text` draws nothing for an empty rect (`FreeInkUIGfxRenderer.h:131`,
  `FreeInkUICore.h:100`). The action is not shrunk or clamped horizontally (A8). A room shorter
  than `h` clamps `h`; a non-positive room yields `height <= 0`, which `Rect` callers treat as
  empty.
- No logging is added: none of these is an error the user can act on, and the header draws every
  frame.

## Assumptions (for the review to attack)

- **A1. Measuring font.** The action is measured in the uiScale small font, bold, before the
  rebind at `BaseTheme.cpp:303`, because the picker draws its chip in that font. Measuring after
  the rebind (as the battery percent is) would size the rect for NotoSans 8 regular and the chip
  text would not fit.
- **A2. Width floor.** `TagChipView::measure`'s `max(…, chipHeight)` floor is reproduced, so the
  returned width equals what `measure` returns for the same label on the picker's screen. It binds
  only below 12 px of text.
- **A3. The theme does not paint the action.** Its frame cannot register a hit (`:292`), so a
  painted label would be an untouchable duplicate under the picker's chip. The band background is
  painted by `fui::header` under the whole rect, so the slot is blank until the picker draws.
- **A4. Corner reserve.** Under Corner the battery (strip, y `0..20`) and the action (y `20..45`)
  share columns at different heights, so the title's right reserve is `max(R, w) + M`, not
  `R + w + M`. The parent's "the title gives up the action's width" is read as "the title's right
  edge clears the action by `M`"; adding the full `w` to a reserve that already clears the battery
  would only waste title width.
- **A5. Centred titles keep `drawHeader`'s centre.** Both reserves grow by the same `g`, so the
  title is centred where `drawHeader` centres it (already offset by the battery reserve), not
  re-centred on the band. A short title on the picker therefore sits where it sits on every other
  Classic screen.
- **A6. Visual height.** No firmware precedent sizes a button inside the header; the nearest is
  FreeInkUI's trailing button (`header.h:105`, `:122-124`): band height minus 8, inset 4. Under
  Classic the corner leaves 25 px, so the visual rect is 25 px holding `UI_10`'s 24 px line
  (`ubuntu_10_regular.h`, `advanceY` 24, ascender 20, descender −4). The touch target is the
  picker's concern (its `hitPadding` grows the hit rect to the band, parent §4).
- **A7. Empty label returns an empty rect** rather than a zero-width rect at some position, so a
  caller cannot register a hit at a meaningless spot.
- **A8. No horizontal clamp.** Every caller draws the header across the screen width
  (`UiListActivity.cpp:146`); on a 480 px portrait band the widest real case (Lyra, `100%`, a
  ~70 px action) leaves the title well over 150 px. A band too narrow for the action is outside
  any caller and is not defended.
- **A9. Non-virtual.** The parent gives the signature without `virtual`, and no theme overrides
  `drawHeader` (`LyraTheme.h:84-100`), so there is nothing to override. `drawHeader` keeps its
  `virtual`.
- **A10. Battery ink stays in its rect.** "Never overlaps the battery" is checked against the rect
  `drawHeader` hands `fui::batteryIndicator` (`:388`). Its percent label is drawn into a line box
  taller than Classic's 20 px strip (NotoSans 8 `advanceY` 23), but text is drawn from the box top
  with the baseline at `y + ascender` (`GfxRenderer.cpp:657`), ascender 18
  (`notosans_8_regular.h`), and `0-9` and `%` have no descenders, so the ink ends inside the strip.
  The device check confirms it.
- **A11. Slot predicates.** Corner vs TitleLine is decided by `batteryBarHeight < rect.height`,
  the predicate `manualRightLabel` already uses (`BaseTheme.cpp:335`), so the action and the
  subtitle agree on where the corner is free.

## Testing strategy

**Host (TDD, first).** `test/ui_layout/HeaderLayoutTest.cpp`, a new `add_executable` block in
`test/ui_layout/CMakeLists.txt` modelled on its `ListRowHeightTest` block (`:1-13`) — no change to
the shared `test/CMakeLists.txt`, since `ui_layout` is already added (`:102`). Inputs use the two
themes' real metrics (`BaseTheme.h:131-204`, `LyraTheme.h:8-82`), the derived tokens, and two
battery reserves: percent off (`batteryWidth + 2`) and on (`+ 4 + ` a `100%` width). Tests:

- **T1 — `drawHeader` unchanged.** With `actionWidth = 0`, for every slot, `layout` returns the
  reserves, `titleOffsetY` and battery rect the current inline code computes (expected values
  written out from `BaseTheme.cpp:346-362`, `:350-354`, `:384-388`), and an all-zero action.
- **T2 — never overlaps the battery.** Every slot × percent on/off: `action ∩ battery = ∅`.
- **T3 — never overlaps the title.** The title rect (`B.x + S + left`, `B.y + titleOffsetY`,
  `B.width − 2S − left − right`, `B.height`) does not intersect the action, and its right edge is
  `<= action.x − M`.
- **T4 — placement.** TitleLine: `action.right == B.right − S − R − M`. Corner and Edge and
  Detached: `action.right == B.right − S`. Corner: `action.y >= B.y + batteryBarHeight` and
  `action.bottom == B.bottom − U`.
- **T5 — centring.** Centred titles: `left − left₀ == right − right₀` against T1's values.
- **T6 — inside the band.** The action lies within `B`, above the underline.
- **T7 — width.** `actionWidth` matches `TagChipView::measure`'s formula, including the floor.

Run: `cmake -S test -B build/test && cmake --build build/test --target HeaderLayoutTest &&
ctest --test-dir build/test -R HeaderLayout --output-on-failure`.

**Build.** `pio run` (x4pro) with no new warnings; `./bin/clang-format-fix` leaves the tree clean.

**Device (owner).** No caller uses the seam in this change, so the owner checks it once the picker
lands, or with a temporary local call. Under **Lyra** and under **Classic**, each with
*Settings → battery percentage* shown and hidden (`hideBatteryPercentage`,
`CrossPointSettings.h:221`, `:324`): Done sits left of the battery on the title line (Lyra) or in
the lower-right corner under the battery (Classic); it touches neither the battery icon nor the
percent; the title truncates with an ellipsis before Done; and every other header (e.g. Settings,
whose `BEREAN_VERSION` subtitle exercises the corner label, `SettingsActivity.cpp:483-484`) looks
as it did.
