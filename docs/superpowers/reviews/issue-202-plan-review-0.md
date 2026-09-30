Tier: standard

# Issue #202 plan review 0

Plan: `docs/superpowers/plans/2026-09-30-issue-202-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-202-design.md`
Tree: `dac7cfaa` (line numbers in `src/` match the plan's `67eaaf1e` citations; nothing on this branch touches `src/`).

## Summary

The plan is sound. Every spec requirement maps to a step. The code blocks are complete, and they compile against the
real APIs. The arithmetic literals are right under float semantics. An implementer with no other context could run it
as written. I found no BLOCKER and no MAJOR, and one MINOR.

## Findings

### MINOR 1: the red state is a missing header, not the stub the spec asks for

- **Claim.** Step 1 (plan `:210-219`) expects the red state to be `fatal error: 'components/CoverBandGeometry.h' file not
  found`.
- **Problem.** The spec says to "write `test/cover_band/CoverBandGeometryTest.cpp` against a stub header, see it fail,
  then fill in `CoverBandGeometry.h`" (spec `:225-226`). A failed include only shows that the file is missing. It does
  not show that the 16 assertions can fail, for example that `thumbHeightFor(240, 100) == 399` would catch a changed
  formula. That is the property the characterisation cases exist for (spec `:237-240`, `:249-250`). The risk is low,
  because Step 2's literals are all checked here (see "Verified" below).
- **Fix.** In Step 1, create `src/components/CoverBandGeometry.h` as a stub. It should have the same namespace, the
  three constants and `Crop`, with `crop` returning `Crop{false, 0, 0}` and `thumbHeightFor`/`plateTop` returning `0`.
  The expected red state then becomes assertion failures. Step 2 then replaces the stub's bodies. The FILES lines
  already cover the header, so nothing else changes. The other option is to add one sentence recording the deviation
  from the spec.

## Verified (not findings)

- **FILES coverage.** Steps 1-6 touch `src/components/CoverBandGeometry.h`, `CoverBand.h`, `CoverBand.cpp`,
  `src/activities/launcher/LauncherActivity.{h,cpp}` and `test/cover_band/{CMakeLists.txt,CoverBandGeometryTest.cpp}`.
  All of them are on the column-0 `FILES:` lines (plan `:7-9`). The `test/cover_band/` entry is a directory prefix,
  not a glob. `test/CMakeLists.txt` is edited only in the working tree and restored before every commit
  (plan `:11-13`, `:295`, `:627`). That is the shared-file rule in `.claude/agents/ui-dev.md:22-27`, and it follows the
  precedent in `docs/superpowers/plans/2026-09-29-issue-194-plan.md:23-25`, which also leaves it off FILES.
- **Arithmetic.** I compiled the Step 2 header verbatim with `-std=c++20 -Wall -Wextra -pedantic` and checked it with
  `static_assert`. `thumbHeightFor(240,100)==399` holds, as does `(464,321)==773`, `(227,160)==378` and
  `(100,400)==400`. Bible `crop(579,773,464,321,258,0.25f)` gives `{57,64}` and Meetings gives `xOffset 28`.
  `ClampsSoTheBandNeverRunsOffTheCoversFoot` gives 100 and `TitleBandIsCentred…` gives 50. A runtime (non-constexpr)
  `(float)240/0.6f` also truncates to 399. There are 16 `TEST`s, which matches the `[ PASSED ] 16 tests` expectation
  (plan `:289`).
- **Derived inputs.** Lyra's `topPadding = 5` and `headerHeight = 44` are at `LyraTheme.h:11,13`, and `uiTheme = LYRA`
  at `CrossPointSettings.h:331`. The viewable insets `9/3/3/3` are at `BoardConfig.h:626-630`. `advanceY` is 23 for
  `notosans_8_regular.h:3663` and 24 for `ubuntu_10_regular.h:3719`. With those, `computeLayout`
  (`LauncherActivity.cpp:281-307`) gives Bible `{8,59,464,321}` and Meetings `{8,390,227,160}`, as the plan states.
- **Pixel identity of the success path.** Step 3's `blitCropped` is `LauncherActivity.cpp:331-371` with only the
  offsets factored out. It has the same buffers, the same `isTopDown` resolution, the same `value < 3` threshold and
  the same column walk. `draw` then masks with the default `Color::White` (`GfxRenderer.h:242`, as `:393`), fills the
  plate with the same radius and corner flags (`:396-397`), and draws the same rule (`:398`). `plateRect().y` equals
  the old `plateTop` (`:395`). Step 5f keeps the caption and border calls unchanged (`:399-400`), and the fallback is
  identical (`:385-388`).
- **API fit.** Every renderer call `CoverBand` makes is `const` (`GfxRenderer.h:233-234,242-243,256`), so
  `const GfxRenderer&` works. `Rect`'s constructor is `explicit` (`BaseTheme.h:17`), but every construction in the
  plan is direct-list-initialised (`Rect{...}`, `const Rect band{...}`), so it compiles. `CoverBand::Style` is an
  aggregate with default member initialisers, so `{focusBand, TILE_RADIUS, plateHeight}` is valid. `fillRoundedRect`
  with radius 0 falls back to a plain fill (`GfxRenderer.cpp:1288-1291`), so `cornerRadius = 0` is safe for future
  callers. `CoverThumb::pathFor(const std::string&, int, bool&)` (`CoverThumb.h:12`) matches `thumbPathFor`.
- **Launcher leftovers.** `<Bitmap.h>` has no user left in the launcher after 5e, so Step 5a is right to delete it.
  `<Memory.h>` is still used at `:513` and is correctly kept. `MODULE` is still used. Outside `LauncherActivity.{h,cpp}`,
  there are no references to the deleted names in `src lib test docs/*.md USER_GUIDE.md`, so the 5h grep expectations
  hold.
- **Steps 3-5 have no failing test.** This is by the spec's design. The draw path needs the renderer, and the spec
  gives on-device photographs as its verification (spec `:257-260`). Steps 4 and 5 are declared as one commit
  (plan `:473-474`), so no commit leaves a broken tree.
- **Push.** The branch already tracks `origin/refactor/202-cover-band` and `push.autoSetupRemote=true` is set, so a
  bare `git push` in Step 7 works.

VERDICT: CLEAR
