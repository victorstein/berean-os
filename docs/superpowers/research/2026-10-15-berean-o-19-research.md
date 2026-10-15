# Research — berean-o-19 (T6): the Predicación Home card and the cover guard

Run `berean-os-20261015-predicaci-n-193x`, task `t6`, branch `feature/preaching-home-card`
(cut from `batch/20261015-predicaci-n-193x`, HEAD `61bb11f9`). Source spec:
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §2, D3 (`:51`), Goal 6 (`:29`).

## 1. Who owns the behaviour

Only four source files reference `HomeLayout` / `HomeTargets`
(`grep -rln "HomeLayout\|HomeTargets" src test lib`):

| File | Role |
|---|---|
| `src/activities/launcher/HomeLayout.h` | Pure `constexpr` geometry: `compute()` places every section; free of the renderer so the host suite pins it against the real theme tables (`:8-11`). |
| `src/activities/launcher/HomeTargets.h` | Pure routing: `Target`, `Action`, `route()`, `isActive()`, `ring()`. |
| `src/activities/launcher/LauncherActivity.{h,cpp}` | Measures line heights, calls `compute`, draws, maps taps and the Left/Right ring to targets, dispatches actions. |
| `src/components/MastheadLayout.h:20` | Comment only (the hero uses the masthead band). |

Tests: `test/home_layout/HomeLayoutTest.cpp` and `HomeTargetsTest.cpp`, both in the
`HomeLayoutTest` executable (`test/home_layout/CMakeLists.txt:5-9`), include dirs `src` and
`lib/StudyStore` only. No CMake change is needed to extend those two files.

What T3 already landed on this branch (so T6 only consumes it):
- `ActivityManager::goToPreaching(const PreachingOrigin& origin = {})`
  (`src/activities/ActivityManager.h:88`, `.cpp:232-239`): `makeUniqueNoThrow<PreachingActivity>`,
  null-checked with `LOG_ERR`, then `replaceActivity`. Default origin is `Kind::None`
  (`src/activities/preaching/PreachingOrigin.h:7-13`), i.e. "from Home".
- `STR_PREACHING` / `STR_PREACHING_SUBTITLE` in `english.yaml:484-485` and `spanish.yaml:431-432`
  ("Predicación", "Preguntas de la Biblia · tus temas").

## 2. Current control flow

**Layout** (`HomeLayout.h:113-174`). `gap = metrics.verticalSpacing`; `top = insets.top +
topPadding + PAD`; `bottom = screenHeight - insets.bottom - topPadding`; `fixed = recent + verse +
meetings + iconRow` (`:118-119`); `heroHeight = bottom - top - fixed - 4 * gap` (`:120`). Then it
stacks downward: hero (+ plate inside its foot) → recent → verse card → meetings (`:154-164`) →
four icon tiles (`:166-172`), each section followed by `gap`. `Layout` (`:58-82`) has no
`preaching` boxes.

**Measured numbers** (the host suite's `LINES{23,24,34,40}`, `INSETS{9,3,3,3}`, 480×800,
`HomeLayoutTest.cpp:15-20`; theme metrics `LyraTheme.h:11-18`, `BaseTheme.h:134-141`):

| | Lyra | Base |
|---|---|---|
| `verticalSpacing` (gap) | 8 | 10 |
| `topPadding` / `headerHeight` / `listRowHeight` | 5 / 44 / 40 | 5 / 45 / 30 |
| top, bottom | 22, 792 | 22, 792 |
| fixed = 143/113 + 172 + 94 + 79 | 488 | 458 |
| hero today = 770 − fixed − 4·gap | **250** | **272** (`HomeLayoutTest.cpp:64-65`) |
| plate (`plateHeight`, `:109-111`) | 101 | 102 (`:59-60`) |

With a 63 px card (`PAD + max(ICON 32, ui10 24 + small 23) + PAD = 8 + 47 + 8`) and a fifth gap:
Lyra 250 − 63 − 8 = **179**, Base 272 − 63 − 10 = **199**; artwork above the plate 179 − 101 = **78**,
199 − 102 = **97**. Matches the brief.

**Targets** (`HomeTargets.h`). `Target` enum order (`:12-25`) *is* the ring order: `ring()` walks
`0..COUNT` and keeps active targets (`:84-91`). Inserting `Preaching` between `Meetings` and `Tags`
in the enum is therefore the whole ring change. `Tags`/`Search` route to `DownloadBible` without a
Bible (`:67-70`); `Action` (`:27`) has no `OpenPreaching`. `State` (`:29-35`) needs no new field.

**Launcher** (`LauncherActivity.cpp`):
- `computeLayout()` (`:88-103`) feeds measured line heights into `compute`.
- `boxFor(target)` (`:194-223`) maps every target to a box — a switch over `Target`, so a new
  enumerator must get a case (missing → `-Wswitch` under the per-source `-Wall`).
- Tap: `loop()` iterates every target, `boxFor` + `isActive` + `contains` (`:463-475`).
  Confirm: `activate(ring[position])` (`:493`). Both go through `activate()` (`:380-401`), a switch
  over `Action`; adding `OpenPreaching` there is what makes "tap and Confirm both open it".
- `OpenSettings` → `activityManager.goToSettings()` (`:397-399`) is the direct analogue of the
  wanted `goToPreaching()` (replace, no result callback). Meetings/Publications instead use
  `startActivityForResult` (`:434-443`).
- `render()` (`:360-378`) draws hero, recent, verse, meetings, then the four icon tiles.
- `drawMeetings()` (`:316-352`) is the nearest drawing example: `drawRoundedRect` with a 3 px
  border when selected, `drawIcon` at `meetingsIcon`, `UI_10_FONT_ID` bold title via `drawText`,
  `SMALL_FONT_ID` secondary line via `drawTextIn` (which truncates, `:61-66`).

**Cover crop** (`src/components/CoverBand.cpp:21-31,145`): `blitCropped` calls
`CoverBandGeometry::crop(width, height, band.width, band.height, band.height - style.plateHeight,
focusBand)`; the launcher passes `BOOK_TITLE_BAND` and `layout.plate.height`
(`LauncherActivity.cpp:248`). `crop()` (`CoverBandGeometry.h:34-41`):
`yOffset = clamp(int(coverHeight * focus) - visibleHeight / 2, 0, coverHeight - bandHeight)`;
`fits` is false when the cover is narrower than the band.

For the 773-row thumbnail (the NWT cover is 535×773 there, spec `:89`; 535 ≥ 464 so it fits):
focus row = int(773 × 0.25) = 193.
- Lyra: visible 78 → yOffset = 193 − 39 = 154; visible rows 154–231. Holds 167–214 (13 above, 17 below).
- Base: visible 97 → yOffset = 193 − 48 = 145; visible rows 145–241. Holds 167–214.

The "visible window" the guard must test is `[yOffset, yOffset + visibleHeight)`; the band itself
extends further but the plate covers that part.

**The thumbnail does not change.** `thumbHeightFor(464, h) = max(h, int(464 / 0.6)) = 773` for any
hero ≤ 773 (`CoverBandGeometry.h:46-48`), and that value is the cache key of `thumb_<h>.bmp`
(`:44-45`, `CoverBand.cpp:162`). So neither the hero shrink nor the sleep screen's
`APP_STATE.bibleCoverPath` (`LauncherActivity.cpp:121-128`) invalidates anything on SD.
`CoverBandCache` keys a slot on the band `Rect` and style (`CoverBandCache.h:27,58-60`); a smaller
hero is just a miss on first paint and needs fewer bytes than the 16,240 B it is sized for (`:19`).

## 3. Findings the spec phase must act on

1. **`MIN_HERO_ART = 96` (`HomeLayout.h:27-28`) contradicts the settled 78 px.** Its only use is
   `TheHeroKeepsItsArtAboveThePlate` (`HomeLayoutTest.cpp:86-92`), which would fail for Lyra
   (78 < 96). Base (97) still passes. The spec (§2, D3) chose 78 px knowingly, and the cover guard
   is the stronger, content-based check, so the constant must drop to ≤ 78 or that test must be
   replaced by the guard. Not a decision: D3 settles the number; this is test plumbing in the
   owned file.
2. **Tests whose expectations move:** `TheHeroTakesTheRemainder` (`:63-66`, 250/272 → 179/199);
   `TheGapsLeaveTheSectionsBelowTheHeroInPlace` (`:79-84`, `bottomOf(hero)` today 272 = 14 + 258 /
   294 = 14 + 280 → 201 = 14 + 187 / 221 = 14 + 207); `SectionsStackWithoutOverlap` (`:101-110`,
   icons now follow the preaching card); `TheRingWalksActiveTargetsInReadingOrder` (`HomeTargetsTest.cpp:93-104`,
   Preaching after Meetings in all three rings); `WithNoBibleTheReaderTargetsDownloadIt` (`:75-83`)
   and `MeetingsPublicationsAndSettingsNeedNoBible` (`:85-91`) are the models for the new route
   tests. `TheLastSectionEndsAtTheViewableFoot` (`:94-99`) and `TheHeroSharesTheMastheadThumbnail`
   (`:44-50`, 773) stay true unchanged.
3. **Icon format is rotated.** `src/components/icons/*.h` 32×32 icons are 128-byte
   `static const uint8_t XIcon[]` arrays with a `// size: 32x32` comment (`folder.h`, `library.h`),
   1 bpp MSB-first, bit 0 = ink. `GfxRenderer::drawIcon` (`lib/GfxRenderer/GfxRenderer.cpp:1372-1387`)
   plots bit `(row, col)` at screen `(x + size − 1 − row, y + col)`, so the stored bitmap is the
   screen image rotated. Decoding `FolderIcon` through that mapping (a short Python script over
   `folder.h`) draws an upright folder, confirming it. `listIcons.h`'s `icon_*_32_bits` from
   `gen_icons.py` are **not** this layout (different bytes for the same folder) and are drawn by a
   different path, so the generator's output cannot be pasted in as-is.
4. **Icon tooling.** `freeink-sdk/libs/assets/Icons/tools/gen_icons.py` rasterises Lucide SVGs with
   `rsvg-convert` (present: `/opt/homebrew/bin/rsvg-convert`) and Pillow (absent from both system
   `python3` and `~/.platformio/penv/bin/python`: `ModuleNotFoundError: No module named 'PIL'`).
   Lucide SVGs are in `freeink-sdk/libs/assets/Icons/lucide/icons/`; candidates that fit
   "preaching": `megaphone`, `messages-square`, `speech`, `message-circle`, `book-open-text`. The spec
   names no icon; pick one in the spec phase. `scripts/convert_icon.py` targets the `drawIcon` layout (it
   rotates 90°, `:32`) but needs `cairosvg` and Pillow (`:9,17`), and only flattens alpha for raster
   input (`:24-30`); a one-off conversion (rsvg-convert on white → threshold → rotate) avoids both.
5. **Subtitle fits and renders.** Text column = `left + PAD + ICON + PAD` to `left + width − PAD`
   → 464 − 56 = 408 px wide at `SMALL_FONT_ID`; `drawTextIn` truncates anyway. `·` (U+00B7) is in
   `notosans_8_regular`'s `0xA0–0x17F` interval (`lib/EpdFont/builtinFonts/notosans_8_regular.h:2481`).

## 4. Tool versions (installed)

- `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19` (not on PATH).
- `cmake --version` → `cmake version 4.4.2`.
- Host suite baseline (configured into the scratchpad, `cmake -S test -B <dir>`, then
  `cmake --build <dir> --target HomeLayoutTest`): `[==========] 42 tests from 3 test suites ran.
  [  PASSED  ] 42 tests.`

## 5. Nearest existing example

`a5c55b14 fix: give Home breathing room and space the week-strip days (#242)` and
`829e7f71 feat: redesign Home for a reference Bible (#223)` are the last two changes to
`HomeLayout.h` (`git log -- src/activities/launcher/HomeLayout.h`). The Meetings card is the model
for the new card end to end: `meetingsHeight` (`HomeLayout.h:95-97`) and its boxes (`:154-163`),
`Target::Meetings` / `Action::OpenMeetings` (`HomeTargets.h:19,65-66`), `boxFor` (`:209-210`),
`drawMeetings` (`:316-321` for the icon/title part), and its tests (`HomeLayoutTest.cpp:52-61,
138-151`; `HomeTargetsTest.cpp:85-91`). The routing mirrors `Tags`/`Search`; the dispatch mirrors
`OpenSettings`.

## 6. Tier

`standard` holds. Everything stays in `src/activities/launcher/`, `test/home_layout/` and one new
icon header; no store, format, contract or other surface is touched (`goToPreaching` and the
strings already exist from T3).
