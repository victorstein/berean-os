Tier: standard

# Plan review 0 — berean-o-19 (T6): the Predicación Home card and the cover guard

Plan: `docs/superpowers/plans/2026-10-15-berean-o-19-plan.md`
Spec: `docs/superpowers/specs/2026-10-15-berean-o-19-design.md`

## What was checked

- **Spec coverage.** Every Goal and Testing item maps to a step: the height, boxes and fifth gap
  (Step 1.2, spec `:54-77`), the hero numbers (Step 1.1(b)/(c), spec `:85-87`), the removal of
  `MIN_HERO_ART` and the rename to `ThePlateSitsAtTheHerosFoot` (Steps 1.1(d), 1.2(a), spec A5), the
  cover guard with the 773-row assertion (Step 2.1, spec `:216-230`), `Target::Preaching`,
  `Action::OpenPreaching` and the route (Step 3.2, spec `:90-98`), both launcher switches (Step 3.3),
  the icon (Task 4), `drawPreaching`, the include, the `render` call and the class comment (Task 5,
  spec `:100-115`), and the device checks (Step 6.3, spec `:247-251`). Nothing is narrowed or dropped.
- **Arithmetic.** Rechecked against the source: `preachingHeight` = 8 + max(32, 24 + 23) + 8 = 63;
  `textX` = 8 + 8 + 32 + 8 = 56 (`HomeLayout.h:159`), so `preachingTextWidth` = 8 + 464 − 8 − 56 =
  408; heroes 179 / 199; `thumbHeightFor(464, 179)` = max(179, ⌊464 / 0.6⌋) = 773
  (`CoverBandGeometry.h:46-48`); focus row ⌊773 × 0.25⌋ = 193; Lyra window 154–231, Base 145–241
  (`CoverBandGeometry.h:38-39`). The Step 2.2 mutation (`TITLE_LAST_ROW = 240`) does fail Lyra
  (231 < 240) and pass Base (241).
- **Replace anchors.** Every quoted "replace" text exists verbatim: `HomeLayout.h:27-28, 80-81,
  95-97, 118-120, 163-164`; `HomeTargets.h:19-20, 27, 65-66`; `LauncherActivity.cpp:35, 209-210,
  391-393, 364-365`; `LauncherActivity.h:16-18, 56`; `HomeLayoutTest.cpp:54, 63-66, 82, 86-92, 108,
  150-151`; `HomeTargetsTest.cpp:85-91, 93-104`.
- **The icon bytes.** Decoded the plan's 128 bytes through `drawIcon`'s mapping
  (`lib/GfxRenderer/GfxRenderer.cpp:1380-1386`, `(x + size − 1 − row, y + col)`, bit 0 = ink): they
  print two upright speech bubbles, the front one top-left. The Step 4.1 script uses the same mapping.
- **Behaviour no test sees.** Tap dispatch walks `boxFor` for every target
  (`LauncherActivity.cpp:463-475`) and Confirm walks `ring` (`:477-493`); with the `boxFor` and
  `activate` cases in Step 3.3 both reach `goToPreaching()`, which handles its own OOM
  (`ActivityManager.cpp:232-239`). `Target` and `Action` are never persisted (`selected` is a plain
  member, `LauncherActivity.h:62`), so inserting enumerators mid-enum renumbers nothing on disk. The
  ring array is sized by `Target::COUNT` (`LauncherActivity.cpp:477`), so it grows with the enum. The
  thumbnail key stays `thumb_773` and `APP_STATE.bibleCoverPath` is unchanged
  (`LauncherActivity.cpp:121-128`). The draw path, tap and Confirm are on the device list.
- **File lock.** Every source and test file a step edits or creates is on a column-0 `FILES:` line
  (`plan:6-9`). The only other writes are gitignored build output (`build/host`,
  `build/pio-x4pro.log`). No step pushes, opens a PR or runs `hpipe submit`.

## Findings

### MINOR 1 — Step 2.2 proves the wrong half of the guard can fail

- **Claim.** Step 2.2 (`plan:263-271`) shows that the cover guard can go red.
- **Problem.** It mutates `TITLE_LAST_ROW`, so it only exercises
  `EXPECT_GE(crop.yOffset + visible - 1, TITLE_LAST_ROW)`. That is the assertion that does not bind.
  The spec's own derivation (`spec:227-230`) has the first row binding at `visible ≥ 52` and the last
  row only at `visible ≥ 43`. So when the card grows, `EXPECT_LE(crop.yOffset, TITLE_FIRST_ROW)` is
  the line that trips, and no step ever shows it failing. A typo there, such as `EXPECT_GE` or the
  wrong constant, would leave the guard green for every growth that matters, and Step 2.2 would
  still pass.
- **Evidence.** Lyra visible 78 → `yOffset` 154; growth past 26 px → visible ≤ 51 → `yOffset` ≥ 168
  > 167, which fails only the first-row assertion.
- **Fix.** Add a second mutation to Step 2.2: temporarily set `TITLE_FIRST_ROW = 150`. Expect
  `TheCoverStillShowsTheBibleTitle` to fail for Lyra (`yOffset` 154 > 150), while Base passes
  (145). Then restore `167` and re-run to get the passing count. Better still, also show the real
  failure mode once: temporarily add 30 to `preachingHeight`'s return. Lyra visible becomes 48 and
  `yOffset` 169, so the guard fails on the first-row line, along with the exact-height tests. Then
  revert.

### MINOR 2 — Spec A9's rationale for the throwaway script is wrong, though its conclusion holds

- **Claim.** "No repo tool emits `drawIcon`'s rotated layout" (`spec:180`).
- **Problem.** `scripts/convert_icon.py` does. It rotates 90° counter-clockwise (`:31`), packs
  MSB-first with white = 1 (`:41-50`), and writes the `#pragma once` / `// size:` header
  (`:52-54`). PIL's `rotate(90)` maps an upright `(x, y)` to stored `(row = W−1−x, col = y)`, and
  `drawIcon` puts that back on screen at `(x, y)`. The real reasons the plan cannot use it are that
  it needs Pillow and cairosvg, and that its SVG path skips the white flatten. That is the same
  black-background trap spec review 0 MAJOR 1 found in the `sips` route. The plan ships fixed bytes,
  so nothing it builds changes. This matters only to whoever makes the next icon and reads A9 as
  "there is no tool".
- **Fix.** Leave the plan's bytes as they are. When the spec is next touched, say that A9 avoids
  `scripts/convert_icon.py` because it needs Pillow and cairosvg and does not flatten SVG alpha onto
  white, not because no such tool exists.

VERDICT: CLEAR
