# Design — berean-o-19 (T6): the Predicación Home card and the cover guard

Run `berean-os-20261015-predicaci-n-193x`, task `t6`, branch `feature/preaching-home-card`.
Parent spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §2 (`:129-146`), D3
(`:51`), Goal 6 (`:29`). Research: `docs/superpowers/research/2026-10-15-berean-o-19-research.md`.
Modelled on the **Meetings card**: `HomeLayout::meetingsHeight` and its boxes
(`src/activities/launcher/HomeLayout.h:95-97,154-163`), `Target::Meetings`
(`src/activities/launcher/HomeTargets.h:19,65-66`), `LauncherActivity::drawMeetings`
(`src/activities/launcher/LauncherActivity.cpp:316-321`) and its tests
(`test/home_layout/HomeLayoutTest.cpp:52-61,138-151`, `HomeTargetsTest.cpp:85-91`). Routing mirrors
`Tags`/`Search` (`HomeTargets.h:67-70`); dispatch mirrors `OpenSettings`
(`LauncherActivity.cpp:397-399`).

## Problem

T3 added the Predicación screen and `ActivityManager::goToPreaching()`
(`src/activities/ActivityManager.h:88`, `.cpp:232-239`), but nothing on Home opens it. Home has no
spare room: every pixel below the hero is a fixed-height section, and the hero takes the remainder
(`HomeLayout.h:118-120`). A new section shrinks the hero, and the hero's visible artwork is what
shows "LA BIBLIA" on the NWT cover. Nothing today fails if a layout change pushes the title out of
the crop window.

## Goal

1. A 63 px **Predicación** card below Reuniones with a 32×32 icon, `tr(STR_PREACHING)` and
   `tr(STR_PREACHING_SUBTITLE)`, and no counts.
2. `Target::Preaching` sits between `Meetings` and `Tags` in the ring. With a Bible it opens
   `goToPreaching()`; without one it routes to `DownloadBible`. A tap and Confirm both open it.
3. The hero becomes 179 px (Lyra) and 199 px (Base).
4. A host test fails if the crop window ever loses rows 167–214 of the 773-row NWT thumbnail, on
   either theme.

## Non-goals

- Counts, badges or "last topic" state on the card (spec §2: "No counts").
- Any change to `CoverBandGeometry` or `BOOK_TITLE_BAND` (`src/components/CoverBandGeometry.h:19`).
- Strings (T3 owns `STR_PREACHING*`, `lib/I18n/translations/spanish.yaml:431-432`,
  `english.yaml:484-485`), `PreachingActivity`, and `ActivityManager`.
- Restoring Home's selection after coming back from Predicación (see A7).
- A reusable icon-generation tool in the repo (see A9).

## Architecture

Three files change and one is added. All of them are under `src/activities/launcher/`, apart from
the icon:

| File | Change |
|---|---|
| `HomeLayout.h` | `preachingHeight()`, four `Layout` boxes, a fifth gap in `compute`; `MIN_HERO_ART` removed (A5) |
| `HomeTargets.h` | `Target::Preaching` after `Meetings`; `Action::OpenPreaching`; its `route` case |
| `LauncherActivity.{h,cpp}` | `boxFor` case, `activate` case, `drawPreaching()`, the icon include, a `render` call |
| `src/components/icons/preaching.h` (new) | `PreachingIcon`, 32×32, in the format of `folder.h` / `library.h` |

### Layout (`HomeLayout.h`)

```cpp
constexpr int preachingHeight(const LineHeights& lines) {
  return PAD + std::max(ICON, lines.ui10 + lines.small) + PAD;
}
```

`Layout` gains `preaching`, `preachingIcon`, `preachingTitle` and `preachingSubtitle`, declared after
the meetings boxes (`HomeLayout.h:74-80`). In `compute`:

- `fixed` adds `preachingHeight(lines)`. The hero becomes `bottom - top - fixed - 5 * gap`, up from
  four gaps (`:118-120`).
- After the meetings block (`:164`, `y += meetingsHeight(lines) + gap`):
  - `preaching = {left, y, width, preachingHeight(lines)}`
  - `preachingIcon = {left + PAD, y + PAD, ICON, ICON}`
  - `const int preachingTextWidth = left + width - PAD - textX;`
  - `preachingTitle = {textX, y + PAD, preachingTextWidth, lines.ui10}`
  - `preachingSubtitle = {textX, bottomOf(preachingTitle), preachingTextWidth, lines.small}`
  - `y += preachingHeight(lines) + gap`, then the icon tiles as today.

The existing `textX` local (`:159`, `left + PAD + ICON + PAD`) is reused, not redeclared. The
existing `textWidth` local (`:160`) is the meetings card's width beside the strip (162 px), so the
preaching card's full-row width needs its own name, `preachingTextWidth`.

Numbers under the host suite's inputs (`HomeLayoutTest.cpp:15-23`; gaps from `LyraTheme.h:14`,
`BaseTheme.h:137`):

| | Lyra | Base |
|---|---|---|
| `preachingHeight` | 63 | 63 |
| hero | 250 − 63 − 8 = **179** | 272 − 63 − 10 = **199** |
| artwork above the plate (hero − plate 101/102) | 78 | 97 |
| `bottomOf(hero)` | 201 = 14 + 187 | 221 = 14 + 207 |
| `preachingTitle.width` | 464 − 56 = 408 | 408 |

### Targets (`HomeTargets.h`)

- `enum class Target`: `..., Meetings, Preaching, Tags, ...`. `ring()` walks the enum in order
  (`:84-91`), so the enum position *is* the ring position.
- `enum class Action`: append `OpenPreaching`.
- `route`: `case Target::Preaching: return state.hasBible ? Route{Action::OpenPreaching} :
  Route{Action::DownloadBible};`. The intent stays `Kind::None` and the fast refresh stays false,
  the `Route` defaults (`:37-43`).
- `State` is unchanged.

### Launcher (`LauncherActivity.{h,cpp}`)

- `boxFor` (`:194-223`): `case Target::Preaching: return &layout.preaching;`.
- `activate` (`:380-401`): `case HomeTargets::Action::OpenPreaching:
  activityManager.goToPreaching(); return;`, the shape of `OpenSettings` (`:397-399`).
- `drawPreaching()` (declared beside `drawMeetings`, `LauncherActivity.h:56`):
  - `drawRoundedRect` of `layout.preaching`, 3 px when `isSelected(Target::Preaching)`, else 1 px
    (the meetings card's border, `:318`).
  - `drawIcon(PreachingIcon, preachingIcon.x, preachingIcon.y, ICON)`.
  - `drawText(UI_10_FONT_ID, ..., tr(STR_PREACHING), true, BOLD)` at `preachingTitle`, as `:320-321`.
  - `drawTextIn(renderer, SMALL_FONT_ID, layout.preachingSubtitle, tr(STR_PREACHING_SUBTITLE), 0,
    true)`, as the meetings percent line (`:323`). `drawTextIn` truncates to the box (`:61-66`).
- `render` (`:360-369`): `drawPreaching()` after `drawMeetings()`.
- Header include: `#include "components/icons/preaching.h"` beside the other icons (`:33-37`).
- The class comment listing Home's sections (`LauncherActivity.h:16-18`) gains the Predicación card
  after this week's meetings.

### The icon (`src/components/icons/preaching.h`)

A `static const uint8_t PreachingIcon[]` of 128 bytes, with the `#pragma once`, `<cstdint>` and
`// size: 32x32` header of `library.h`. It is 1 bpp, MSB-first, with bit 0 as ink, stored in
`drawIcon`'s rotated layout: bit `(row, col)` lands on screen at `(x + 31 − row, y + col)`
(`lib/GfxRenderer/GfxRenderer.cpp:1380-1386`). The source is Lucide `messages-square`
(`freeink-sdk/libs/assets/Icons/lucide/icons/messages-square.svg`), rasterised at 32 px.

## Data and control flow

```
onEnter → computeLayout (measures lines; compute places preaching) → resolveTargets → render
render  → … drawMeetings → drawPreaching → icon tiles → displayBuffer
tap     → loop: for each Target: boxFor ∧ isActive ∧ contains → activate   (:463-475)
Confirm → loop: ring(state) → activate(ring[position])                       (:477-493)
activate(Preaching) → route: hasBible ? OpenPreaching : DownloadBible
   OpenPreaching  → activityManager.goToPreaching()   (replaces Home; PreachingOrigin{} = from Home)
   DownloadBible  → openBibleDownload()               (unchanged, :422-432)
```

No new state, no SD I/O, no allocation in the launcher. `Layout` grows by four `Box`es of four
`int`s (`src/components/MastheadLayout.h:13-18`), 64 B inside the already heap-allocated
`LauncherActivity`. The icon is 128 B of flash.

The cover path is unaffected. `thumbHeightFor(464, h)` stays 773 for any `h ≤ 773`
(`CoverBandGeometry.h:46-48`), so `thumb_773.bmp` and `APP_STATE.bibleCoverPath`
(`LauncherActivity.cpp:121-128`) keep their values. `CoverBandCache` keys on the band `Rect`
(`src/components/CoverBandCache.h:58-60`), so the smaller hero is one cache miss on its first paint.

## Assumptions (each one reviewable)

- **A1 — The card's icon and title sit at the top, not vertically centred.** The icon is at
  `y + PAD`, and so is the title, as on the meetings card (`HomeLayout.h:155,161`). The card is
  `PAD + 47 + PAD`, so the 32 px icon leaves 15 px under it, beside the subtitle. The two cards
  stacked in one column then share an icon edge and a title baseline.
- **A2 — The subtitle is `SMALL_FONT_ID` regular and the title is `UI_10_FONT_ID` bold.** These
  mirror the meetings title and percent line (`LauncherActivity.cpp:320-323`). The `·` (U+00B7) is
  in `notosans_8_regular`'s `0xA0–0x17F` range
  (`lib/EpdFont/builtinFonts/notosans_8_regular.h:2481`).
- **A3 — No Bible routes to `DownloadBible`, not to an inert card.** The brief and spec §2 say so.
  The card therefore stays in the ring with no Bible, as `Tags` and `Search` do
  (`HomeTargetsTest.cpp:98-103`).
- **A4 — `goToPreaching()` is called with the default origin.** `PreachingOrigin{}` is
  `Kind::None` (`src/activities/preaching/PreachingOrigin.h:8-10`), which T3's screen reads as
  "from Home" (parent spec `:161`).
- **A5 — `MIN_HERO_ART` is removed, and the cover guard replaces its test.** The constant's only
  use is `TheHeroKeepsItsArtAboveThePlate` (`HomeLayoutTest.cpp:86-92`). Lyra's 78 px would fail
  its 96 px floor, and parent-spec D3 settled on 78 px. Lowering the floor to 78 would make it a
  copy of whatever the layout happens to produce. The guard tests what the floor stood in for,
  whether the title is visible. The test keeps its second assertion, that the plate sits at the
  hero's foot (`:90`).
- **A6 — The guard's title rows live in the test, not in `HomeLayout.h`.** Rows 167–214 and the
  535 px width belong to the NWT cover at 773 rows (parent spec `:89`), not to the layout. Layout
  code has no business knowing one publisher's artwork.
- **A7 — Coming back from Predicación selects `Continue`.** `goToPreaching` replaces the launcher
  (`ActivityManager.cpp:238`), and a new `LauncherActivity` starts at `Target::Continue`
  (`LauncherActivity.h:62`). That is what Settings does today.
- **A8 — The icon is Lucide `messages-square`.** The spec names no icon. The card is about
  conversations at the door, built from questions and topics, and two speech bubbles say that
  better than `megaphone` (a broadcast) or `book-open-text` (too close to the Bible's own icon
  vocabulary). The repo's generated list icons are already stock Lucide
  (`src/components/icons/listIcons.manifest:8-10`).
- **A9 — The icon is converted once, by a throwaway script, and only the header is committed.**
  `scripts/convert_icon.py` does emit `drawIcon`'s rotated layout (`img.rotate(90, expand=True)`,
  `:32`), but it imports `cairosvg` and Pillow (`:9,17`), neither installed, and it flattens alpha
  onto white only for raster input (`:24-30`), not for an SVG. `gen_icons.py` writes a different
  layout and also needs Pillow (research §3.3–3.4). The path: `rsvg-convert -w 32 -h 32 -b white`
  to PNG, then macOS `sips -s format bmp` to an uncompressed BMP, which plain Python can read. The
  `-b white` composite matters: without it `sips` writes transparent pixels as RGB 0,0,0 and every
  pixel thresholds to ink (spec review 0, MAJOR 1). `gen_icons.py` composites on white for the
  same reason (`rasterize`, `bg.paste(img, mask=…)`). Pixels with luminance below 110 count as ink
  (`gen_icons.py`'s `THRESHOLD`). The bits are packed into the
  rotated layout. Success is a round trip: the bytes, decoded through `drawIcon`'s mapping, print
  the upright icon. The script stays in the scratchpad.

## Error handling

- `goToPreaching` handles OOM itself: it logs `"OOM: preaching activity"` and returns, and Home
  stays on screen (`ActivityManager.cpp:233-237`). The launcher adds nothing.
- A Bible without the preaching section is `PreachingActivity`'s concern
  (`STR_PREACHING_NO_SECTION`, parent spec `:160-161`). The card opens the screen either way.
- `drawTextIn` truncates a long translated subtitle instead of overflowing (`LauncherActivity.cpp:63`).
- Nothing is written to SD, so the storage discipline does not apply.

## Testing strategy (host, `test/home_layout/`, TDD: each test first, red, then the code)

`HomeLayoutTest.cpp`:
- `FixedSectionHeights` gains `EXPECT_EQ(HomeLayout::preachingHeight(LINES), 63)`.
- `TheHeroTakesTheRemainder`: 250 / 272 → **179 / 199**.
- `TheGapsLeaveTheSectionsBelowTheHeroInPlace`: 258 / 280 → **187 / 207**.
- `SectionsStackWithoutOverlap`: `preaching.y == bottomOf(meetings) + gap` and
  `icons[0].y == bottomOf(preaching) + gap`. The existing meetings→icons line is replaced.
- New `ThePreachingCardHoldsItsIconTitleAndSubtitle`, modelled on `:138-151`:
  - `preaching.height == 63`.
  - `preachingIcon`, `preachingTitle` and `preachingSubtitle` sit inside `preaching`.
  - `preachingTitle.width == 408`.
  - `bottomOf(preachingSubtitle) + PAD <= bottomOf(preaching)`.
  - `bottomOf(preachingIcon) + PAD <= bottomOf(preaching)`.
- `TheHeroKeepsItsArtAboveThePlate` becomes `ThePlateSitsAtTheHerosFoot` and keeps only
  `bottomOf(plate) == bottomOf(hero)` (A5): with the floor gone, the old name no longer says what
  it tests.
- New **`TheCoverStillShowsTheBibleTitle`**, run for both themes:
  - `visible = hero.height - plate.height`.
  - `ASSERT_EQ(thumbHeightFor(hero.width, hero.height), NWT_THUMB_H)` with `NWT_THUMB_H = 773`: the
    title rows were measured on the 773-row thumbnail, so a sizing change must fail here rather
    than crop a differently scaled cover against stale rows.
  - `crop = CoverBandGeometry::crop(NWT_THUMB_W, NWT_THUMB_H, hero.width, hero.height, visible,
    BOOK_TITLE_BAND)`.
  - Expect `crop.fits`, `crop.yOffset <= 167` and `crop.yOffset + visible - 1 >= 214`.
  - Constants: `NWT_THUMB_W = 535`, `NWT_THUMB_H = 773`, `TITLE_FIRST_ROW = 167`,
    `TITLE_LAST_ROW = 214`.
  - Expected windows: Lyra 154–231, Base 145–241.
  - The title fits only while `visible >= 52`. From `193 − ⌊v/2⌋ ≤ 167`, `v ≥ 52`; from
    `193 − ⌊v/2⌋ + v − 1 ≥ 214`, `v ≥ 43`. So the guard trips if the Lyra card grows past 26 px
    (Base 45 px), the point where the title would actually be cut. Any smaller growth is caught
    first by the exact heights in `TheHeroTakesTheRemainder`.

`HomeTargetsTest.cpp`:
- New `PreachingOpensWithABibleAndDownloadsWithout`:
  - With a Bible: `OpenPreaching`, intent `None`, no fast refresh.
  - With no Bible: `DownloadBible`.
  - Active in both cases.
- `TheRingWalksActiveTargetsInReadingOrder`: `Preaching` after `Meetings` in all three rings.
- `TheRingStopsAtItsCapacity` is unchanged; its first two targets are still Continue and GoTo.

Run: configure `test/` with CMake, build the `HomeLayoutTest` target, and run it. The baseline is
42 passing (research §4).

Firmware: `~/.platformio/penv/bin/pio run -e x4pro` with no new warnings. The new `Target` and
`Action` enumerators are covered in `boxFor` and `activate`, so `-Wswitch` stays quiet. Then
`./bin/clang-format-fix` over the whole tree.

**Device (owner, end of batch):**
- The cover still reads "LA BIBLIA" on Lyra and on Base.
- The card shows its icon, title and subtitle, untruncated in Spanish.
- A tap on the card opens Predicación, and so does Confirm with the card selected.
- With no Bible, the card opens the Bible download.

## Changes after spec review 0 (CLEAR)

- **MAJOR 1, the icon recipe:** `rsvg-convert` now takes `-b white`. Without it, `sips` turns the
  transparent background black and the threshold makes a solid square (A9).
- **MINOR, the `textWidth` clash:** the card's text width is a new `preachingTextWidth`, because
  `compute` already declares `textWidth` for the meetings card (Layout).
- **MINOR, the guard's thumbnail height:** the guard asserts the 773-row thumbnail before cropping
  and passes the constant to `crop` (Testing).
- **Plan review 0, MINOR:** A9 no longer claims no repo tool emits the rotated layout;
  `scripts/convert_icon.py` does, and A9 now says why it is not used. The plan's guard check also
  proves the first-row bound fails when the card grows.
- **MINOR, stale names:** `TheHeroKeepsItsArtAboveThePlate` is renamed to
  `ThePlateSitsAtTheHerosFoot`, and the `LauncherActivity.h:16-18` section list gains the card.
