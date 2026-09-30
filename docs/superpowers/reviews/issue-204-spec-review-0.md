Tier: standard

# Review 0: issue #204 design (cover mastheads)

Spec: `docs/superpowers/specs/2026-09-30-issue-204-design.md`. Research:
`docs/superpowers/research/2026-09-30-issue-204-research.md`. Issue: `gh issue view 204 --repo victorstein/berean-os`.
Base checked: worktree HEAD `a1772953` (code identical to `0275a5bd`).

## What was verified and holds

- **Row budget (A2, T4).** I compiled the real headers on the host (`c++ -std=gnu++2a -Isrc`, including
  `components/themes/lyra/LyraTheme.h`, `components/CoverBandGeometry.h` and
  `activities/reader/NumberGridLayout.h`). Output:
  `thumb=783`, `h=120 H=667 7x10 cell=59`, `h=155 H=632 7x10 cell=56`, `h=156 H=631 7x9 cell=61`,
  and Classic (`verticalSpacing 10`) `H=665 7x10 cell=59`. So a 120 band keeps 7 × 10 on both themes,
  and 155 is the largest height that still does, as the spec says.
- **Thumbnail sharing (A1).** The launcher's Bible tile is `left = marginLeft + topPadding`,
  `width = right - left` (`LauncherActivity.cpp:258-260`), and it asks for
  `CoverBand::thumbPathFor(biblePath, bibleTile.w, bibleTile.h, …)` (`:107`). The X4 Pro has no
  viewable insets (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:675`). The reader's
  `Epub` is built with the same cache directory (`EpubReaderActivity.cpp:146`,
  `makeUniqueNoThrow<Epub>(bookPath, sdpaths::CROSSPOINT_DIR)`) as `CoverThumb::pathFor`
  (`CoverThumb.cpp:32`), so `epub->getThumbBmpPath(783)` names the launcher's file. The thumbnail
  converters fill the box (`crop = true` by default, `JpegToBmpConverter.h:10`, `.cpp:582-583`), so a
  thumbnail is at least 469 wide. The fits-at-470 edge case is the same one the launcher already has.
- **Opaque plate (A4).** `fui::header` fills its rect before drawing anything (`header.h:63-66`). The
  default popup style is solid white (`FreeInkUI.cpp:63-73`), and nothing under `src/components`
  overrides `tokens.popup` (`grep -rn "\.popup" src/components` finds only OptionPopup's own props).
  The battery is anchored to the band's top `batteryBarHeight` strip (`BaseTheme.cpp:369-384`), which
  is 44 on Lyra and 20 on Classic, so it always sits inside a 44- or 45-row plate.
- **Refresh plumbing (A8).** `requestUpdate()` only sets a flag, and the render task is notified after
  the loop pass ends (`ActivityManager.cpp:284-293`, `:167-172`). Setting `halfRefreshPending` after
  `UiListActivity::onEnter`'s `requestUpdate()` (`UiListActivity.cpp:26`) therefore still lands before
  the first render. Meetings and Publications are only constructed at `LauncherActivity.cpp:449,454`
  (grep over `src`). `BibleNavigationActivity` does not override `render`, so the base-class change
  reaches it. `level` defaults to `Book` (`BibleNavigationActivity.h:66`), so
  `enterAtPosition → enterLevel(Book)` on entry never counts as "leaving Chapter/Verse".
- **Transient buffers (A9).** `blitCropped` holds two row buffers of `(width+3)/4` and `getRowBytes()`
  bytes (`CoverBand.cpp:32-33`). Both are a few hundred bytes for a thumbnail about 470–590 wide.

## Findings

### MAJOR 1: A6 keeps an empty 76 px band on the grid, where its own rationale does not apply and the issue says otherwise

- **Claim (A6):** "The band is reserved whenever the screen (or grid level) has a masthead, whether or
  not a cover is found. With no cover … the compact header is drawn at its usual place … and the rest of
  the band stays paper." The reason given is that Meetings sizes its card thumbnails from the layout
  (`MeetingsActivity.h:66-68`), and that a download on the screen can bring a missing cover.
- **Problem:** That reason only holds for Meetings. On the grid, the cover is resolved once in
  `onEnter` from `Storage.exists` (A7) and cannot change during the visit, because nothing on the grid
  downloads or generates. Reserving the band there buys no stability. It leaves a compact header at
  y = 5..49, then 76 px of blank paper, then the grid. The issue says the opposite: "Screens without a
  cover (Settings, Tags) keep the compact header from #194". This fallback is also not rare. The grid is
  built for any Bible-shaped EPUB (`EpubReaderActivity.cpp:719-721`,
  `epub->getBibleBookNavSpineIndex() >= 0`), not only the one the launcher resolved and made
  `thumb_783` for (`LauncherActivity.cpp:90-108`). A second Bible opened from Publications or the file
  browser, or a Bible whose cover is missing or narrower than 0.6, gets the blank strip every time.
- **Evidence:** `BibleNavigationActivity::onEnter` (`:54-73`) is the only place the spec resolves the
  cover. `enterLevel` (`:275-290`) and `drawChrome` (`:653-672`) never touch SD.
- **Fix:** On the grid, define `hasMasthead()` as `level != Level::Book && !mastheadCover.empty()`, so
  a Bible without a usable thumbnail keeps today's compact layout at every level. A mid-stream read
  failure can still use A6's paper fallback, since that case is transient and rare. Keep A6 as written
  for Meetings, where the rationale is real. Publications can go either way, because it has no
  thumbnail-size coupling (its row thumbnails are a fixed `THUMB_HEIGHT = 44`,
  `PublicationsActivity.h:35-38`). Say which, and why. Update the Error-handling table and the grid
  line of device check 1 to match.

### MINOR 1: T4 does not pin the shipped metric, so "a host test pins this" is overstated

- **Claim (A2):** "A host test pins this (T4), so the band never needs to shrink at 120." In Risks, a
  table that omits `.mastheadHeight` silently reads 0, and the answer is a runtime `LOG_ERR` in
  `Masthead::bandRect`.
- **Problem:** As written, T4 passes Lyra's numbers into the test as literals ("with Lyra's `topPadding 5`,
  `verticalSpacing 8` and `mastheadHeight 120`"). Raising `LyraMetrics::values.mastheadHeight` to 160,
  or dropping it to 0, would still pass. But the theme headers can be built on the host: my probe
  above included `LyraTheme.h` (which includes only `BaseTheme.h`, and that includes only std headers,
  `BaseTheme.h:3-7`) and `static_assert`ed `LyraMetrics::values.headerHeight == 44` and
  `BaseMetrics::values.topPadding == 5` without error.
- **Fix:** Have T4 read `LyraMetrics::values` and `BaseMetrics::values` directly, for both themes,
  since Classic is user-selectable (`SettingsList.h:280`). Add
  `static_assert(values.mastheadHeight > 0)` next to each table. That turns the "silently reads 0" risk
  into a build failure instead of a runtime log. (Separately, Risks says the two tables are the only
  construction sites. `TextSettingsActivity.h:97` also value-initialises one, but it is overwritten
  from `getMetrics()` at `.cpp:64`, so it is harmless.)

### MINOR 2: A single paint can stream the masthead up to 9 times

- **Claim (A9):** "Each draw streams the 783-row thumbnail." `Masthead::draw` logs one duration per draw.
- **Problem:** `UiListActivity::render` calls `clearScreen → drawChrome → renderUi` again on every
  rebuild pass, up to 8 extra times (`UiListActivity.cpp:163-167`). On Publications, a list with
  wrapped labels, one selection move can read the whole thumbnail up to 9 times. A per-draw time will
  understate the per-paint cost the device check is supposed to judge.
- **Fix:** Either log the masthead time once per paint, summed across passes, or skip the band on
  rebuild passes and draw it once after the loop. Content starts at `contentTop`, so nothing
  FreeInkUI draws overlaps the band. Pick one and state it in A9.

### MINOR 3: Publications' masthead path is written outside the render lock

- **Claim:** Meetings resolves the cover "before the lock, and swaps it in with them
  (`:133-135`)". For Publications, `refresh()` "picks and generates the masthead cover".
- **Problem:** Publications' `refresh()` runs on the loop task from `deleteEntry`, through the
  confirm-popup callback (`PublicationsActivity.cpp:205-216`), and from the search result handler
  (`:240-244`). Neither holds `RenderLock`. A new `std::string` member there is read by `drawChrome` on
  the render task. `entries_` already has this exposure (`:43`), but the spec should not add to it.
- **Fix:** Resolve the path into a local and assign it under `RenderLock lock(*this)`, the way
  `MeetingsActivity::refresh` does (`:141-144`).

### MINOR 4: The cover pick stops at the first candidate even if it yields no cover

- **Claim (A7):** Meetings takes "the first card with a publication". Publications takes "the first
  `RECENT_BOOKS` entry whose path is in `entries_`", else the first listed entry.
- **Problem:** `CoverThumb::pathFor` returns empty when there is no cover image, an unsupported
  format, or a failed generation (`CoverThumb.cpp:42-45`). `crop.fits` rejects a cover narrower than
  the band (`CoverBandGeometry.h:36`). With the pick as written, a Watchtower with an unusable cover
  gives Meetings a blank band while the workbook next to it has a good cover. The same happens on
  Publications when the most recent book has no usable cover.
- **Fix:** Walk the same candidate order and take the first whose thumbnail resolves (and, if cheap,
  whose `CoverThumb::sizeOf` width is at least the band width). Cap the walk so one visit generates at
  most one new `thumb_783`.

### MINOR 5: "Lifts BibleSearchActivity's field into the base" contradicts the change table

- **Claim (A8):** "This lifts `BibleSearchActivity`'s field (`.h:116`, `.cpp:1019,1035`) into the
  base."
- **Problem:** `BibleSearchActivity.{h,cpp}` is not in the Changed table. Its own
  `std::atomic<bool> fullRefreshPending` (`BibleSearchActivity.h:116`) and its full `render` override
  (`.cpp:1014-1038`) would stay, next to a base field with the same meaning and a different name.
  "Lifts" reads as a move, so an implementer may edit that file, or may not.
- **Fix:** Say "mirrors" and leave `BibleSearchActivity` untouched, or list it in the table and move
  it onto the base field. The first keeps the change on the three screens.

### MINOR 6: The most common exit from the masthead is missing from the device checks

- **Claim (A8, Risks):** Chapter/Verse → Book takes a HALF because "a FAST refresh is differential, so
  the masthead could ghost". Leaving for the reader is "a paint those screens own".
- **Problem:** Picking a chapter or verse, which is the grid's main purpose, goes to the reader's
  `navigateTo` (`EpubReaderActivity.cpp:730-732`). The reader then paints on its page cadence, FAST
  unless `pagesUntilFullRefresh <= 1` (`ReaderUtils.h:165-166`). That is the same art-to-paper
  transition the spec protects against inside the grid. Leaving it out of scope is defensible: the
  launcher already goes from cover art to the reader on FAST for Resume (`LauncherActivity.cpp:422`,
  `allowFastInitialRefresh=true`). But device check 3 does not test it.
- **Fix:** Add to device check 3: "Pick a chapter from the grid. The reader page shows no masthead
  ghost." Name the one-line follow-up (set a clean paint in the grid's result handler at
  `EpubReaderActivity.cpp:726`) in case it does.

VERDICT: CLEAR
