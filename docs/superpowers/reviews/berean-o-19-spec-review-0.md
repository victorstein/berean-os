Tier: standard

# Spec review 0 — berean-o-19 (T6): the Predicación Home card and the cover guard

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-19-design.md` against the brief
(`hpipe brief --task t6`), the research note
(`docs/superpowers/research/2026-10-15-berean-o-19-research.md`) and the parent spec
(`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §2, D3, Goal 6), at branch HEAD
`d54d321c`.

## What I checked and found sound

- **Layout arithmetic.** `compute` today (`src/activities/launcher/HomeLayout.h:115-120`): top 22,
  bottom 792, so 770 px. Lyra fixed is 143 + 172 + 94 + 79 = 488, and Base's is 458
  (`HomeLayoutTest.cpp:53-58`). Gaps are 8 and 10 (`LyraTheme.h:14`, `BaseTheme.h:137`). With
  `preachingHeight = 8 + max(32, 24 + 23) + 8 = 63`:
  - Lyra: 770 − 551 − 5·8 = **179**.
  - Base: 770 − 521 − 5·10 = **199**.
  - `bottomOf(hero)` is 22 + 179 = 201 = 14 + 187, and 22 + 199 = 221 = 14 + 207.
  - The icon row still ends at 792, so `TheLastSectionEndsAtTheViewableFoot` holds unchanged.
  - The text column is (left + 464 − 8) − (left + 48) = 408.
  - The subtitle's foot plus PAD lands exactly on the card's foot: y + 8 + 24 + 23 + 8 = y + 63.
- **The cover guard.** `crop()` (`CoverBandGeometry.h:34-41`) gives focus row
  `int(773 × 0.25) = 193`:
  - Lyra: yOffset = 193 − 39 = 154, so rows 154–231.
  - Base: yOffset = 193 − 48 = 145, so rows 145–241.
  - Neither is clamped, since the upper bound is 773 − 179 = 594.
  - The bounds `v ≥ 52` and `v ≥ 43` derived in the spec are correct, and so are the 26 px and 45 px
    trip points.
  - On the device, `blitCropped` calls the same `crop` with the bitmap's real dimensions and
    `band.height − plateHeight` (`src/components/CoverBand.cpp:21-31`), with
    `style.plateHeight = layout.plate.height` (`LauncherActivity.cpp:248`). The test models the
    device path.
  - The spec says outright that a growth of under 26 px is caught by the exact-height tests rather
    than by the guard. That meets the brief's intent.
- **Routing and ring.** The ring walks the enum in order (`HomeTargets.h:84-91`). The only switches
  over `Target`/`Action` are `route` (`HomeTargets.h:51`), `boxFor` (`LauncherActivity.cpp:195`) and
  `activate` (`:382`), so the spec's cases are complete for `-Wswitch` (grep over `src` and `test`).
  - With no Bible, Preaching is active through `DownloadBible`, so it appears in all three rings.
  - `TheRingStopsAtItsCapacity` stays true.
- **Seams with T3.** `goToPreaching(const PreachingOrigin& = {})` exists, OOM-checked
  (`ActivityManager.h:88`, `.cpp:232-239`). `PreachingSession::open` finds the Bible with the same
  `BibleFinder::find()` the launcher uses (`src/activities/preaching/PreachingSession.cpp:36`), so
  "Home has a Bible" and "Predicación has a Bible" cannot disagree. The strings are at
  `english.yaml:484-485` and `spanish.yaml:431-432`.
- **Fonts.** `SMALL_FONT_ID` is `notosans_8_regular` (`src/main.cpp:122-123`). Its `0xA0–0x17F`
  interval covers `·` (`notosans_8_regular.h:2481`).
- **Icon layout.** I decoded `FolderIcon` through `drawIcon`'s `(x + size−1−row, y + col)` mapping
  (`GfxRenderer.cpp:1372-1387`) and it draws an upright folder. The spec's description of the
  rotated storage is right.
- **A5.** `MIN_HERO_ART` has exactly one use outside docs (`HomeLayoutTest.cpp:89`). Removing it
  rather than lowering it to whatever the layout produces is sound, and D3 settled 78 px.
- **A7.** `finish()` goes through `popActivity` (`Activity.cpp:24`) to `goHome`, which makes a new
  launcher that starts at `Continue` (`LauncherActivity.h:62`), as Settings does today.

## Findings

### MAJOR 1 — A9's icon pipeline, as written, produces a solid black square

**Claim** (spec `:175-181`): `rsvg-convert -w 32 -h 32` to PNG, then `sips -s format bmp`, then
"pixels with luminance below 110 count as ink (`gen_icons.py`'s `THRESHOLD`)".

**Problem.** Lucide strokes are dark on a *transparent* background. `sips` keeps the alpha and
writes a 32-bpp BMP in which every transparent pixel is RGB (0,0,0). A luminance-only threshold then
marks all 1,024 pixels as ink. `gen_icons.py` gets a usable `THRESHOLD = 110` only because it first
composites onto white (`freeink-sdk/libs/assets/Icons/tools/gen_icons.py:30-32,41-44`), and the
spec drops that step. The round trip at the end of A9 would catch the black square, so it would not
ship. But the procedure the spec prescribes fails its own verification, and the implementer has to
rediscover why.

**Evidence.** I ran the spec's exact pipeline on `messages-square.svg` in the scratchpad:

```
hdr 124 w 32 h -32 bpp 32 comp 3
################################   (every row)
ink count 1024 of 1024
```

I then rendered with `rsvg-convert -w 32 -h 32 -b white …` (the `-b, --background-color` option
from `rsvg-convert --help`). `sips` then writes 24-bpp, `comp 0`, top-down (`h -32`), and the same
threshold yields the clean two-bubble icon. Compositing the 32-bpp file's alpha onto white gives
an identical bitmap.

**Fix (inline).** In A9, render with `rsvg-convert -w 32 -h 32 -b white`, or composite alpha onto
white before taking luminance, as `gen_icons.py` does. Also note the BMP that `sips` emits: a
top-down image (negative height) whose row stride is padded to 4 bytes, either 24-bpp `BI_RGB` or
32-bpp `BI_BITFIELDS` with a 124-byte header. The throwaway reader must honour both, or it will
skew rows.

### MINOR 1 — `textWidth` cannot be declared again in `compute`

**Claim** (spec `:70,75`): "`textX = left + PAD + ICON + PAD` and `textWidth = left + width − PAD −
textX`", and "the existing `textX` local (`:159`) is reused, not redeclared".

**Problem.** `textWidth` is already a `const int` in the same scope (`HomeLayout.h:160`,
`out.strip.x − PAD − textX`, which is 162 px). The preaching width (408) can neither reuse it nor
redeclare it. Taken literally, the spec does not compile, and the bare name invites an
implementer to overwrite the meetings width.

**Fix.** Name it, for example `const int preachingTextWidth = left + width − PAD − textX;`, and drop
the restated `textX = …` from the bullet list.

### MINOR 2 — The guard should pin the 773-row premise its title rows depend on

**Claim** (spec `:209-212`): the guard computes
`coverHeight = thumbHeightFor(hero.width, hero.height)` and compares it with constant rows 167–214
"of the 773-row NWT thumbnail".

**Problem.** Rows 167–214 were measured on the cover scaled to 535×773 (parent spec `:89`). If
`thumbHeightFor` or `NARROWEST_COVER_ASPECT` (`CoverBandGeometry.h:13,46-48`) ever changes, the
guard keeps comparing a different thumbnail against rows measured on the old one. It can then pass
while the title is cut. Today only `TheHeroSharesTheMastheadThumbnail` pins 773 (Lyra,
`HomeLayoutTest.cpp:49`), and it lives in a different test.

**Fix.** In the guard, declare `NWT_THUMB_H = 773` beside `NWT_THUMB_W`, and
`ASSERT_EQ(thumbHeightFor(hero.width, hero.height), NWT_THUMB_H)` for each theme before cropping.

### MINOR 3 — Names and comments that A5 and the new card leave stale

**Problem.** After A5, `TheHeroKeepsItsArtAboveThePlate` (`HomeLayoutTest.cpp:86-92`) checks only
that the plate sits at the hero's foot, so its name claims what it no longer tests. The class
comment in `LauncherActivity.h:16-18` lists Home's sections as "this week's meetings, and Tags,
Search, Publications and Settings", which goes stale the moment the card lands. Both files are in
scope.

**Fix.** Rename the test, for example `ThePlateSitsAtTheHerosFoot`. Add "the Predicación card" to the
`LauncherActivity.h` class comment.

## Summary

The geometry, the guard's arithmetic, the routing and the T3 seams all hold against the code. The
one material defect is the icon conversion recipe, which needs the white composite. Fix it inline,
with the three MINORs. None needs another review pass.

VERDICT: CLEAR
