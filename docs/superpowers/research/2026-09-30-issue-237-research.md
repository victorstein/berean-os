# Issue #237 — Home spacing: research

Branch `fix/237-home-spacing`, based on `58546aaf` (release 1.29.2). Every figure
below was read from the file cited or computed from the font tables by the command
shown; nothing is from memory.

## Who owns the behaviour

| Concern | Owner |
|---|---|
| All Home geometry, renderer-free | `src/activities/launcher/HomeLayout.h` (`compute`, `:97-154`) |
| Feeding it the insets, metrics and line heights | `LauncherActivity::computeLayout`, `src/activities/launcher/LauncherActivity.cpp:86-98` |
| Drawing the hero, plate header and buttons | `LauncherActivity::drawHero`, `LauncherActivity.cpp:277-299` |
| Drawing the week strip | `LauncherActivity::drawMeetings`, `LauncherActivity.cpp:357-377` |
| The strip's data (day, today, meeting) | `buildWeekStrip`, `src/activities/network/MeetingWeekView.h:25` / `.cpp:15` — data only, no geometry; nothing in it needs to change |
| Host tests | `test/home_layout/HomeLayoutTest.cpp` (built by `test/home_layout/CMakeLists.txt`, registered at `test/CMakeLists.txt:129`) |

The Meetings screen draws its own strip full-width with `cellWidth = band.width / 7`
(`src/activities/network/MeetingsActivity.cpp:324-325`); it is not the cramped one
and is out of scope. `STRIP_CELL` is used only by `HomeLayout.h:137` and
`LauncherActivity.cpp:359-360`.

## Current control flow

`onEnter` → `computeLayout()` (`LauncherActivity.cpp:77`) measures
`getLineHeight` for SMALL, UI_10, serif 12 and serif 14, reads
`getOrientedViewableTRBL`, and calls `HomeLayout::compute(screenW, screenH, insets,
metrics, lines)` (`:92-97`). The layout is computed once per entry and cached in
`layout`; the cover thumbnail is then generated at exactly `layout.hero`'s size
(`:129`, `CoverBand::thumbPathFor`).

Inside `compute` (`HomeLayout.h:97-154`):

- `top = insets.top + metrics.topPadding`; `bottom = screenH - insets.bottom - metrics.topPadding` (`:100-101`).
- Fixed sections: `recentHeight + verseCardHeight + meetingsHeight + iconRowHeight` (`:102-103`).
- **The hero absorbs the remainder**: `heroHeight = bottom - top - fixed - 4 * gap` (`:104`), with `gap = metrics.verticalSpacing`.
- The hero box comes from `MastheadLayout::band(...)` (`:109`; `src/components/MastheadLayout.h:22-27`), whose `y = marginTop + topPadding`. That is the only space above the hero.
- The plate is pinned to the hero's foot (`:113-114`). `plateHeight = headerHeight + 1 + (ui10 + 2*PAD) + PAD` (`:93-95`): header, rule, button row, bottom pad — **no term between the header and the buttons**.
- `buttonRow.y = bottomOf(plateHeader)` (`:117`) — the Continue / Go to buttons start on the header's last row. This is complaint 2.
- Recent, verse card, meetings card and icon row stack below the hero with `gap` between each (`:122-152`); the icon row ends exactly at `bottom`.

### The numbers, Lyra (the default theme) at 480×800

Inputs as the host test pins them (`HomeLayoutTest.cpp:15-20`): insets `{9,3,3,3}`,
lines `{small 23, ui10 24, serif12 34, serif14 40}`. Lyra metrics
(`src/components/themes/lyra/LyraTheme.h:11-18`): `topPadding 5`, `headerHeight 44`,
`verticalSpacing 8`, `listRowHeight 40`. Base (`src/components/themes/BaseTheme.h:134-141`):
`topPadding 5`, `headerHeight 45`, `verticalSpacing 10`, `listRowHeight 30`.

| Box | Lyra | Source |
|---|---|---|
| hero.y | 14 (9 + 5) | test `:38` |
| hero.height | 258 (Base 280) | test `:55-56` |
| plate.height | 93 → art above plate 165 ≥ `MIN_HERO_ART` 96 | test `:50`, `:62` |
| recent | 143 = 23 + 3 × 40 | test `:48` |
| verse card | 172 | test `:46` |
| meetings | 94 | test `:45` |
| icon row | 79 | test `:44` |
| strip width | 182 = 7 × 26 | test `:111` |
| meetings title width | 218 | test `:112` |

So complaint 1 is literal: the hero's outline starts 5 px under the viewable top
(`topPadding`). Complaint 2 is literal: 0 px between header and buttons.

### "Take the space from the empty area under Recent"

Recent's height is fixed at `lines.small + RECENT_SLOTS * listRowHeight`
(`HomeLayout.h:89-91`) whether or not all three slots are filled;
`drawRecent` draws only `recentCount` rows (`LauncherActivity.cpp:305-313`), and
`recentCount` comes from `PlacesDoc::pickRecent(..., RECENT_SLOTS)`
(`LauncherActivity.cpp:147`, `src/util/PlacesDoc.h:69`), which can return fewer
than three. Without a photo the "empty area" is either those unfilled slots or
the per-row slack (a 40 px row holding a 24 px UI_10 line). Mechanically, though,
**any new vertical term in `compute` is paid by the hero** (`:104`) unless a fixed
section shrinks by the same amount. With 165 px of art against a 96 px floor
there is room either way; which section gives the pixels is a spec decision, and
the acceptance criterion says it must not be the icon row.

## The week strip

`drawMeetings` (`LauncherActivity.cpp:355-377`) lays seven `STRIP_CELL` = 26 px
cells edge to edge from `layout.strip.x`, and centres the letter (SMALL) and the
day number (UI_10 regular) in a box widened by `PAD` on each side
(`:365-369`). The selected day ("today") is a filled rounded rect at
`cellBox.x + 1 .. width - 2` (`:363-364`) — 1 px from each neighbour's cell.

Digit advance, UI_10 regular (`ubuntu_10_regular.h`, glyph table from `:1485`,
`advanceX` is 12.4 fixed-point per `lib/EpdFont/EpdFontData.h:126,134`): every
digit `0`–`9` is `188/16 = 11.75 px`. Command:

```
python3 - <<'EOF'   # parse { w, h, advanceX, ... }, // <char> rows
...
digits regular {'0': 11.75, ..., '9': 11.75}
28 width 23.5
Meetings 93.0625      # UI_10 bold
Reuniones 103.75      # UI_10 bold, Spanish STR_MEETINGS
EOF
```

A two-digit day is 23.5 px in a 26 px cell, so adjacent numbers sit ~2.5 px apart
— "282930". The criterion (≥ one digit width between numbers) needs a cell of at
least 2 × 11.75 + 11.75 ≈ 36 px, i.e. a strip ≈ 7 × 36 = 252 px. At the current
card width (464) that leaves the title column `212 − 8 − 56 = 148 px`
(same arithmetic as `HomeLayout.h:137-140`), which still holds the widest
translated title, "Reuniones" at ~104 px (`lib/I18n/translations/spanish.yaml:9`;
only English and Spanish define `STR_MEETINGS`). `drawTextIn` truncates the
percent line to its box (`LauncherActivity.cpp:59`), so a narrower column
degrades rather than overflows.

**Measuring rather than hardcoding.** `HomeLayout` is renderer-free by design
(`HomeLayout.h:8-11`) and already receives measured font data through
`LineHeights`, filled from `renderer.getLineHeight` in `computeLayout`
(`LauncherActivity.cpp:92-94`). A digit width would arrive the same way, from
`renderer.getTextWidth(UI_10_FONT_ID, ...)` (`lib/GfxRenderer/GfxRenderer.cpp:600`).
That is the existing seam; no new mechanism is needed. The host test pins
`LINES` by hand from the font's `advanceY` (`HomeLayoutTest.cpp:18-20`) and
would pin a digit width the same way (12 px rounded, or 11 truncated — the spec
must say which rounding `getTextWidth` yields; it accumulates 12.4 fixed-point
advances, `GfxRenderer.cpp:625` onward).

## Installed tools

```
$ ~/.platformio/penv/bin/pio --version
PlatformIO Core, version 6.1.19
$ cmake --version
cmake version 4.4.2
```

- ESP32 platform: pioarduino `55.03.37` (`platformio.ini:15`).
- googletest `v1.17.0`, fetched by `test/CMakeLists.txt:14-18`.
- `pio` is not on `PATH`; `./bin/bootstrap` sets up a fresh worktree.

## Nearest existing example

- **This exact file, one change ago:** `829e7f71 feat: redesign Home for a reference Bible (#223)` is the only commit touching `HomeLayout.h` and `HomeLayoutTest.cpp`; the fix is a geometry tweak to it and its pinned numbers.
- **A spacing constant added to a pure layout header plus its host test:** `1fd966f5 feat: add toast padding and line cap (#116)` — `src/components/ToastLayout.h` + `test/posted_message/ToastLayoutTest.cpp`, 38 lines, same shape.
- **Measured font metrics into a renderer-free layout:** `HomeLayout::LineHeights` itself (`HomeLayout.h:36-41`, filled at `LauncherActivity.cpp:92-94`).

## Scope and tier

One surface (`ui`): `HomeLayout.h`, `LauncherActivity.cpp`, `HomeLayoutTest.cpp`.
No store, no format, no i18n key, no shared append-point file (`test/CMakeLists.txt`
already registers `home_layout`). The tier stays **standard**.

## Open for the spec

1. Which fixed section yields the pixels for the top gap and the header→button gap — unfilled Recent slots cannot be reclaimed at layout time as things stand, because `computeLayout()` runs before `resolveTargets()` sets `recentCount` (`LauncherActivity.cpp:78-79`, `:147`) and the thumbnail is sized from the hero in between. So the choice is the hero (has ~69 px of slack over `MIN_HERO_ART`) or Recent's row height/slack.
2. Size of each gap: an existing token (`PAD`, `verticalSpacing`) rather than a new literal.
3. Rounding of the measured digit width, and whether the strip cell derives from it (`2 × digit + digit`) or from a larger letter width if a translation's weekday initial is wider.
