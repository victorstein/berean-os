Tier: standard

# Issue #202 spec review 0

Reviewed: `docs/superpowers/specs/2026-09-30-issue-202-design.md` against issue #202 (`gh issue view 202 --repo victorstein/berean-os`)
and `docs/superpowers/research/2026-09-30-issue-202-research.md`. HEAD is `7855a9ec`; `git diff 67eaaf1e HEAD --stat` touches
only the two docs, so every `:line` the spec cites at `67eaaf1e` still holds in the worktree.

## What was verified and holds

- Every line citation I checked is correct: `LauncherActivity.h:60,63`; `LauncherActivity.cpp:58,63-64,118-119,143-144,259-261,
  329-372,339,341-342,348,354-355,379-401,383,385-388,393,395-398,400,403-415,461-465`; `SleepActivity.cpp:602-639,624`;
  `MeetingsActivity.cpp:227,231,374`; `PublicationsActivity.cpp:80`; `book-card.h:41`; `Epub.cpp:668,670`; `test/CMakeLists.txt:126`.
- **A1/A2 hold.** The `crop` formulas in the spec match `LauncherActivity.cpp:339,348,354-355` term for term, and `thumbHeightFor`
  is `coverFillHeight` (`:260`) verbatim, so the launcher asks for the same `thumb_<h>.bmp` and `APP_STATE.bibleCoverPath`
  (`:122-125`) does not change. The spec requires `fits` to be decided before the clamp ("when false, offsets are 0"), which
  also keeps `std::clamp(…, 0, H - h)` away from its `lo > hi` UB case.
- **The const renderer API works.** `drawPixel`, `maskRoundedRectOutsideCorners`, both `fillRoundedRect` overloads, `drawLine`
  and `fillRect` are all `const` (`GfxRenderer.h:233-256`, `:243`), so `CoverBand::draw(const GfxRenderer&, …)` compiles, the
  same way `CoverThumb::drawNative` does (`CoverThumb.h:20`).
- **`Rect{rect.x, rect.y, rect.w, rect.h}` compiles.** `Rect` has an `explicit` four-int constructor (`BaseTheme.h:17`), and
  direct-list-init is allowed with it; `LauncherActivity.cpp:459` already does this.
- **`cornerRadius = 0` is safe.** The spec skips the mask at radius 0, and `fillRoundedRect` degrades to a plain fill when
  `maxRadius <= 0` (`GfxRenderer.cpp:1286-1290`).
- **A7 is stronger than the spec says.** `SleepActivity::drawBibleCover` also uses a different fit rule
  (`width < pageWidth || height <= 0`, `SleepActivity.cpp:615`, with no height check), so migrating it would change two rules.
  Leaving it out is correct.
- **The host test is feasible.** `test/CMakeLists.txt:4` sets C++20, so `constexpr` `std::clamp`/`std::max` from `<algorithm>`
  work, and `test/launcher_refresh/CMakeLists.txt` is the right template.
- Nothing outside the launcher references the symbols being deleted. `grep -rln` over `src lib test` for `coverFillHeight`,
  `BOOK_TITLE_BAND`, `MAGAZINE_MASTHEAD_BAND`, `drawCoverFilling` and `NARROWEST_COVER_ASPECT` finds only
  `LauncherActivity.{h,cpp}`.

## Findings

### MAJOR 1: `draw`'s contract says "drawn nothing" on failure, but A5 keeps a partial blit and justifies it with a false premise

**Claim.** The API comment (spec lines 105-107) says `draw` returns "False, having drawn nothing, when the cover is missing,
unreadable, or smaller than the band". A5 (lines 194-196) and the error table (line 177) keep a partial blit on a mid-stream
`readNextRow` failure, because "the caller's fallback already paints over it".

**Problem.** These contradict each other, since a cover that fails mid-stream is "unreadable". The premise behind A5 is also
false: the launcher's fallback does **not** paint over the partial cover. `drawTile` (`LauncherActivity.cpp:417-452`) draws a
rounded-rect *outline* (`:422`), an icon through `drawTileArt` (`:314-321`, which is passed an empty cover path from `:386`,
so it only draws the 32 px icon), and text. It never fills the tile. Today a truncated or corrupt thumbnail therefore leaves
dithered cover rows under the icon and label. That is tolerable while one private caller exists. Once this becomes a shared
component whose documented contract is "drawn nothing", Home and the mastheads will rely on that contract, and it will be
false.

**Evidence.** `LauncherActivity.cpp:358` (`return false` inside the row loop, after earlier rows were drawn at `:368`);
`:385-388` (the fallback is `drawTile`); `:421-422` (outline only); no `fillRect` or `fillRoundedRect` in `drawTile`.

**Fix.** Make the contract true without any buffer: on a mid-stream read failure, `draw` calls
`renderer.fillRect(band.x, band.y, band.width, band.height, false)` before returning false. `fillRect` is const
(`GfxRenderer.h:243`), and the band is known before the loop starts. This changes nothing in the success path, so the
"renders the same" criterion is untouched. On the error path it removes garbage, and the acceptance criteria do not cover
that path. Update A5 and the error-table row to match. If the author prefers to keep the partial blit, the minimum fix is
to correct the API comment ("may have drawn part of the band when a read fails mid-stream; the caller must clear it") and
strike the false "fallback paints over it" rationale. Either way, no human judgement is needed.

### MINOR 1: The hand-computed test literals will be wrong for any width divisible by 3, and a "fix" to match them breaks A2

**Claim.** Testing strategy (lines 226-228): the `thumbHeightFor` cases and "a characterisation case per launcher tile …
with the expected offsets computed by hand from the pre-refactor formula and written as literals".

**Problem.** `static_cast<int>(static_cast<float>(w) / 0.6f)` is one less than the exact `w / 0.6` for every `w` divisible
by 3 in the tile range, because `0.6f` is slightly above 0.6. If someone works the literal out by hand (`240 / 0.6 = 400`),
the test fails against correct code (399). The obvious "fix" is to change the formula, and that changes the requested
`thumb_<h>.bmp`, which A2 exists to forbid.

**Evidence.** A single-precision emulation (`struct.pack('f', …)` in Python) over `w = 100..480` lists `117→194`, `120→199`,
`240→399`, `276→459`, `288→479`, `462→769`, `480→799`, and every other multiple of 3 in the ranges 117-153, 231-306 and
462-480.

**Fix.** In the testing strategy, require the literals to be computed with float semantics (for example, `240 → 399`), and
add one deliberate multiple-of-3 width to the `thumbHeightFor` cases so the truncation is pinned.

### MINOR 2: The characterisation inputs are unspecified, which leaves "per launcher tile at the X4 Pro's portrait size" to invention

**Claim.** Line 227: "A characterisation case per launcher tile at the X4 Pro's portrait size".

**Problem.** The crop depends on the tile size (from `computeLayout`, `:267-311`: theme metrics plus the X4 Pro's
`ViewableInsets`), on the plate height (font line heights, `:383`, which a host test cannot see), and on the cover's BMP
size (which depends on the source cover's aspect ratio). The spec pins none of these. An implementer will pick arbitrary
numbers, and a test whose expected values come from the same formula over invented inputs only checks the formula against
itself.

**Evidence.** `tileTextHeight` calls `renderer.getLineHeight` (`:263-265`). The X4 Pro profile takes the default insets
`{9, 3, 3, 3}` (`BoardConfig.h:625-630`; `XTEINK_X4_PRO` at `:1364` does not override them). `topPadding = 5`
(`BaseTheme.h:129`). Those give a Bible tile width of `480 - 3 - 3 - 2·5 = 464` and a Meetings width of `(464 - 10)/2 = 227`.
The heights and plate sizes need a device log.

**Fix.** Name the inputs in the spec or the plan: tile `w`/`h` and plate height per tile (log them once from a dev build,
or derive them from the metrics above plus the measured line heights), and the real thumbnail dimensions of the NWT and
Watchtower covers on the test card. Record the source of each number next to the literal.

### MINOR 3: `plateRect` has no caller, so the launcher keeps a second source of truth for the plate position

**Claim.** `plateRect` is "for the caller to draw its label into" (lines 110-111). The launcher edit (lines 138-141) then
calls `drawCenteredIn(…, plateTop + TILE_PADDING, …)`, using `plateTop` from today's code (`:395`), which is no longer
computed anywhere in the new `drawCoverTile` as written.

**Problem.** The only consumer ignores the API added for it, so `plateRect` ships untested by any real call site, and the
plate geometry is computed twice (inside `draw`, and again in the launcher).

**Fix.** In the launcher edit, take the caption top from `CoverBand::plateRect(band, style).y + TILE_PADDING`, and add a host
assertion or a note that `plateRect(band, style).y == band.y + band.height - plateHeight`. `plateRect` can be `constexpr`
arithmetic in the geometry header if `Rect` is kept out of it, or it can stay in `CoverBand` and be exercised by the
launcher only.

VERDICT: CLEAR
