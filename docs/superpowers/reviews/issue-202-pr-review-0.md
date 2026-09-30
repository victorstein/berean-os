Tier: standard

# PR #209 review 0: reusable CoverBand component (issue #202)

Reviewed `gh pr diff 209` (head `60465a22`) against issue #202, the spec
`docs/superpowers/specs/2026-09-30-issue-202-design.md` and the plan
`docs/superpowers/plans/2026-09-30-issue-202-plan.md`.

Verification run by the reviewer:

- Host test, with `add_subdirectory(cover_band)` applied locally after `launcher_refresh`
  (`test/CMakeLists.txt:126`), then restored: `CoverBandGeometryTest` 16/16 passed.
- `pio run -e x4pro`: SUCCESS, RAM 19.9%, flash 84.0%, with no warnings from `CoverBand*` or `LauncherActivity`.
- The code blocks in plan Steps 2 and 3 match `src/components/CoverBandGeometry.h` and
  `src/components/CoverBand.cpp` byte for byte.

## Intent

**Acceptance criteria**

- *The launcher renders the same as before.* The success path has the same arithmetic and draw order:
  - `CoverBandGeometry::crop` (`CoverBandGeometry.h:36-42`) reproduces the removed inline
    `xOffset`/`focusRow`/`yOffset` expressions exactly, including the float truncation and the `/ 2`.
  - The row walk, the `value < 3` threshold and the bottom-up row resolution (`CoverBand.cpp:39-54`) are the
    removed `drawCoverFilling` body.
  - The order is mask, plate fill, plate rule (`CoverBand.cpp:64-80`), then the caption and border in the
    launcher (`LauncherActivity.cpp:320-321`). This is the old z-order.
  - `TILE_RADIUS` is 8, so the new `cornerRadius > 0` guard always takes the mask branch for the launcher.
  - `plateRect(...).y` equals the old `rect.y + rect.h - plateHeight`.

  Photo comparison on the device is correctly left to the human tester in the PR body.
- *Host test for crop and focus-band geometry.* Present: `test/cover_band/CoverBandGeometryTest.cpp`. It covers:
  - centring and odd-surplus truncation;
  - the masthead band and the title band being centred in the visible height rather than the band height;
  - clamping at both ends, and declining a cover that is too narrow or too short;
  - an exact fit;
  - `thumbHeightFor` bound by width and bound by height, plus the `240 → 399` single-precision pin that
    protects the `thumb_<h>.bmp` cache key;
  - `plateTop`;
  - one characterisation case per launcher tile, with its inputs derived in the plan's "Derived test inputs".

  I rechecked the Bible case by hand. `int(464/0.6f)=773`, `focusRow=int(773*0.25f)=193`, `visible=258`, and
  `193-129=64`, so it agrees with the test.
- *No new resident buffers.* The same two per-call `makeUniqueNoThrow` row buffers are used
  (`CoverBand.cpp:31-36`) and freed on return. No member, static or band-sized buffer is added.

**Issue "Change" bullets**

- The component lives in `src/components/` with no SDK edit. The blit is clip-aware (it iterates only the band's
  rows and columns), cropped, 1-bit, aimed at a focus band, and takes an optional opaque plate.
- Thumbnails are requested at the band's size through `CoverThumb` (`CoverBand.cpp:88-90`). The result is the
  old `coverFillHeight` unchanged, so the cover cache is not invalidated.
- The launcher uses the component (`LauncherActivity.cpp:107-108,132-133,307-322`). `drawCoverFilling`,
  `coverFillHeight` and the three constants are gone, with no remaining references in `src/`.

**Scope.** There is no expansion. The one behaviour change is on the error path: a mid-stream read failure now
clears the band white (`CoverBand.cpp:40-44`). The spec justifies it (A5, prompted by MAJOR 1 of spec review
0) and the PR body declares it. The fallback `drawTile` only draws an outline, so without the clear a partial
cover would show through under the icon. The spec's non-goals and the PR body both explain why the scope is
where it is:

- `SleepActivity` is not migrated (A7);
- no `coverPainter` adapter is written yet (A6).

The issue offers the painter as an either/or ("a `CoverBand` component … or a FreeInkUI-style painter"). This
is not a silent reduction.

**Plan divergence.** None found. The files match the plan's code, and the hand-off line matches plan Step 7.

## Quality

- **Patterns.** The geometry header follows `ToastLayout.h`: `constexpr`, only `<algorithm>`, and its own POD
  result struct. The draw namespace follows `CoverThumb`: free functions, `const GfxRenderer&`, a
  success-bool fallback. The test CMake file is a copy of `test/launcher_refresh/CMakeLists.txt`. No second way
  of doing something is introduced.
- **Naming and structure.** These are consistent with siblings: `MODULE` tag, `LOG_ERR` + `return false`,
  `makeUniqueNoThrow` with an OOM log. `Rect` (`BaseTheme.h:11-18`) is used at the draw API, and `TileRect` is
  converted once at the call site.
- **Dead code and comments.** `#include <Bitmap.h>` was correctly dropped from the launcher, and
  `<Memory.h>` is still used there (`LauncherActivity.cpp:434`). The moved comments explain why, not what. The
  orphaned "The Bible tile puts its cover beside the label" comment, which sat above `coverFillHeight` and no
  longer described anything, went with it.
- **Edge safety for future callers.** A plate with `cornerRadius == 0` goes through
  `fillRoundedRect`'s `maxRadius <= 0` branch (`GfxRenderer.cpp:1288-1291`), which is a plain fill, so the
  default `Style` is safe.
- **Tests.** The tests use named device-shaped inputs, with sources given for the characterisation literals.
  They are not tautological: the title-band case asserts a value (50) that differs from the naive
  centre-in-band answer (0), and the float-truncation case pins a non-obvious result. The draw path itself is
  left to the device, which the test header states and the issue does not require otherwise.
- **Duplication.** `SleepActivity::drawBibleCover` still holds its own copy of the row walk. This is a
  deliberate, documented deferral (A7), not an oversight in this PR.

## Findings

None. I found nothing that meets the bar for BLOCKER, MAJOR or MINOR.

VERDICT: CLEAR
