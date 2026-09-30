Tier: standard

# PR #219 review 0: cover mastheads on the chapter grid, Meetings and Publications (issue #204)

Reviewed: `gh pr diff 219` (head `bac8d5dc`) against issue #204, the spec
`docs/superpowers/specs/2026-09-30-issue-204-design.md` (including its Amendments), and the plan
`docs/superpowers/plans/2026-09-30-issue-204-plan.md`. The new host suite was compiled standalone
against the checked-in gtest build (`test/masthead/MastheadLayoutTest.cpp` with `-I src`). It
passed 11/11. `test/CMakeLists.txt` is correctly left untouched, and the hand-off line is in the
PR body.

## Intent

**Acceptance criteria (issue #204)**

- *The three screens show their mastheads, with no text drawn over dither.* This is met.
  - `Masthead::draw` (`src/components/Masthead.cpp:78-95`) blits the band through `CoverBand::draw`
    with `plateHeight = headerHeight + 1`. `CoverBand::draw` fills the plate opaque white and draws
    the rule (`src/components/CoverBand.cpp:75-80`). The theme header then goes into `plateHeader`,
    one row below the rule, so its title and battery sit only on the white plate.
  - On a declined or failed blit, `CoverBand` leaves the band as paper: an empty path or a non-fit
    returns before any pixel is drawn, and a mid-stream failure clears the band
    (`CoverBand.cpp:21-43`). The compact header is then drawn instead (`Masthead.cpp:87-90`).
  - Grid: `BibleNavigationActivity.cpp:355-358`. Meetings: `MeetingsActivity.cpp:194-197`.
    Publications: `PublicationsActivity.cpp:66-71`.
- *Selection moves stay FAST; entry from an art-heavy screen may use one HALF.* This is met.
  - `UiListActivity::render` consumes `halfRefreshPending` once per paint and defaults to FAST
    (`UiListActivity.cpp:169`, field `UiListActivity.h:90`), so every other list screen is
    unchanged.
  - Meetings and Publications set the flag in `onEnter`. `ActivityManager::requestUpdate` defers the
    first paint until `onEnter` returns (`ActivityManager.cpp:284-293`, `:166-172`), so the flag is
    always set before the first paint consumes it.
  - The grid sets the flag only on Chapter/Verse → Book while a masthead is up
    (`BibleNavigationActivity.cpp:305`). It is evaluated before `level` changes, so it tests the
    level being left, which is the intent.
  - The band is redrawn with identical pixels on each paint, so a differential FAST refresh of a
    selection move leaves it untouched.
- *Heap is logged on each screen.* This is met. Meetings (`MeetingsActivity.cpp:205-208`) and
  Publications (`PublicationsActivity.cpp:78-81`) log in the grid's existing format after their SD
  work. The grid's existing line (`BibleNavigationActivity.cpp:~74`) covers the third screen.

**Issue "Change" bullets**

- *About 120 px, through `CoverBand`, title and battery on a plate, replacing header plus title row.*
  Done through `mastheadHeight = 120` in both theme tables. Each table has a `static_assert`
  (`BaseTheme.h:197-203`, `LyraTheme.h:75-82`).
- *Settings and Tags keep the compact header.* They are untouched, because the base
  `halfRefreshPending` defaults to false and `drawChrome` is overridden only on the three screens.
- *Stream from SD; PSRAM cache only if visibly slow, never internal SRAM.* The issue makes the cache
  conditional on a device observation. The PR streams, logs each draw's time
  (`Masthead.cpp:83-85`), and names the 7,200 B PSRAM cache as the follow-up in the PR body (device
  check 6). This is the deferral the issue text allows, not a silent reduction. No new resident
  allocation is made. The two transient row buffers belong to `CoverBand`: 464 px / 4 ≈ 116 B, plus
  one BMP row.
- *Grid keeps 7×10; measure; shrink if needed.* This is measured and pinned.
  `ChapterGridKeepsSevenByTenOnBothThemes` (`MastheadLayoutTest.cpp:775-783`) runs
  `NumberGrid::geometryFor` over the real Lyra and Classic metrics, and the model matches
  `buildScreen`. `getScreenSafeArea` does not subtract viewable insets (`UITheme.cpp:83-94`), so
  body = 800 − `contentTop` − `verticalSpacing`, as the test computes. The 146 px ceiling is
  recorded at the field (`BaseTheme.h:120-123`).

**Spec and amendments.** Every amendment is implemented:
- the corrected band `{8, 14, 464, 120}` and `thumb_773`;
- the masthead shown on the grid only at Chapter/Verse with a cached, fitting cover
  (`BibleNavigationActivity.cpp:296`);
- on Publications, the masthead only when a cover resolves; on Meetings, the band always reserved
  (`MeetingsActivity.cpp:217-219`);
- the band drawn once per paint from `drawFooter`, with nothing in `drawChrome` while a masthead
  shows;
- the Publications path assigned under `RenderLock`;
- `pickCover` generating at most one missing thumbnail, with the Publications walk capped at 8;
- the base field mirroring `BibleSearchActivity` without touching it;
- the reader-ghost device check (PR body, device check 4).

Plan review 0's additions are also present: the `isCached` popup, `fits` checking
`Storage.exists` first, the grid's `epub` guard, a HALF on a changed Publications cover, and the
candidate order host-tested.

**Scope.** The verse grid also gets the masthead. That is spec A3, which the PR body explains: it is
the same `NumberGrid` path and title. No other screen, format, translation key or `CoverBand` change
was made.

**Plan divergence.** The code matches the plan's steps: the same function set, the same includes and
the same comments. No unexplained divergence was found.

**Tests.** The eleven tests exercise behaviour:
- the band against the launcher's Bible tile, so the thumbnail is shared;
- the plate against `CoverBandGeometry::plateTop`;
- the 7×10 grid computed through `NumberGrid`, not a restated constant;
- candidate ordering, de-duplication and the cap.

`BothThemesSetTheBandHeight` only restates the table value and overlaps the `static_assert`s. It
is harmless, and it is what makes a changed value visible in the suite.

## Quality

- **Pattern reuse.**
  - `MastheadLayout.h` mirrors `CoverBandGeometry.h`: `constexpr`, std-only, and host-tested the
    same way (`test/masthead/CMakeLists.txt` copies `test/cover_band`).
  - `halfRefreshPending` is the `fullRefreshPending` idiom, lifted into the base with a
    behaviour-neutral default.
  - The heap lines copy the grid's format.
  - Thumbnail generation goes through `CoverThumb::pathFor`, and the cached-path check uses
    `Epub::getThumbBmpPath` the same way `CoverThumb.cpp:33` does.
  - `pickCover` composes `CoverThumb::pathFor(path, thumbHeight)`, which `CoverBand::thumbPathFor`
    also wraps. The compositions are equivalent, and `Masthead::thumbHeight` is needed on its own
    by the grid, so this is not a meaningful duplicate.
- **Naming and structure.** The names are consistent with their siblings:
  - `hasMasthead()` on each screen;
  - `mastheadCover_` with the trailing underscore on the two screens that use it, and
    `mastheadCover` on `BibleNavigationActivity`, whose members carry no suffix;
  - `levelTitle()` extracted cleanly from the old `drawChrome` body.
- **Comments.** They explain why, not what. Examples: the reason the rule row exists
  (`MastheadLayout.h:578-579`), why the grid never generates a thumbnail
  (`BibleNavigationActivity.cpp:282-283`), why `fits` checks existence first
  (`Masthead.cpp:52-53`), and the 146 px ceiling. There is no dead or commented-out code.
- **Error handling.** Failures degrade to the compact header, and `CoverBand` and `CoverThumb` log
  the cause. No writes are made, so there is no partial-file risk. The `[[maybe_unused]]` on
  `startMs` handles the release build's compiled-out `LOG_DBG`.
- **Resources.** There is no new resident allocation. The candidate vectors in
  `PublicationsActivity::refresh` and `MeetingsActivity::refresh` are `reserve`d and live off the
  render path. Nothing new is created as a `std::string` per paint: `Masthead::draw` takes
  `const std::string&`.

## Findings

1. **MINOR** — The Publications loading popup keys only on the first candidate
   (`src/activities/catalog/PublicationsActivity.cpp:101-103`), but `Masthead::pickCover` generates
   the first *missing* thumbnail among the candidates it walks (`src/components/Masthead.cpp:69-72`).
   - How it happens: the first candidate's `thumb_773` is cached but fails `fits`, because the
     cover is narrower than the 0.6 aspect `CoverBandGeometry` assumes. `pickCover` then moves on,
     and if the next candidate is uncached, it opens that EPUB with no popup on screen.
   - It is rare, since most covers clear 0.6, and cosmetic: the device appears to stall briefly.
   - Fix inline: have `pickCover` report or take a callback before it generates, or drop the
     `front()` shortcut and show the popup whenever any candidate is uncached.

VERDICT: CLEAR
