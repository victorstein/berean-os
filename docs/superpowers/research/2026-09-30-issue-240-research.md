# Issue #240 — research: tag chip grid and chip padding

Branch `fix/240-tag-chip-grid`, based on `58546aaf` (release 1.29.2). Every claim below cites a
line read in this worktree or a command run in it.

## Who owns the behaviour

| Concern | Owner |
|---|---|
| Chip counting, two-line wrap, hit-rect tiling (pure, host-testable) | `src/activities/reader/TagChipRow.h` (namespace `TagChips`) |
| Building chip candidates, measuring and drawing the header chips | `src/activities/reader/HighlightsActivity.cpp:129-159` (`rebuildChips`), `:708-790` (`buildChipRow`) |
| The **…** screen | `src/activities/reader/TagFilterActivity.{h,cpp}` |
| Row indices of that screen | `src/activities/reader/TagRowMapping.h:25-42` (`FilterRows`) |
| Result type returned to Highlights | `src/activities/ActivityResult.h:86` (`TagSelectionResult`) |
| Host tests | `test/tag_rows/TagChipRowTest.cpp` (built by `test/tag_rows/CMakeLists.txt:1-5` into `TagRowsTest`) |

`TagFilterActivity` is constructed only by `HighlightsActivity::openTagFilter`
(`HighlightsActivity.cpp:210-212`); the other hits for its name in `src/` are comments
(`StudyStore.h:115`, `PassageLinksActivity.h:24`, `TagRowMapping.h:25`).

## Current control flow

**Header chips.** `rebuildRowItems()` ends in `rebuildChips()` (`HighlightsActivity.cpp:126`).
`rebuildChips` (`:129-159`):

- counts over the chapter when `spineFilter_` is set, else over every passage
  (`:134-136`, `TagChips::countIn` / `TagChips::count`) — this is #226's "Tags here" rule;
- pushes `All N`, then each active tag with a non-zero count (a zero-count tag is kept only if it
  is the active filter, `:150-153`), then `Unlabelled N` only if non-zero or active (`:156-158`);
- stops at `MAX_CHIPS + 1` = 25 candidates (`:139`, `:153`), labels formatted `"%s %u"` into a
  40-byte `ChipEntry::label` (`HighlightsActivity.h:106-110`).

`buildChipRow` (`:708-790`) measures every label bold (`:714-722`), with:

- `padX = theme.spaceMd` (`:716`),
- `chipHeight = lineHeight(smallText.font) + 2 * theme.spaceSm` (`:717`),
- `gap = theme.spaceSm` (`:718`),
- `lineWidth = body.width - 2 * listInset` (`:727-728`),

then `TagChips::layout` wraps them into at most `MAX_LINES = 2` lines (`TagChipRow.h:84`,
`:107-156`), replacing a tail with the ellipsis chip when they do not fit. The selected chip gets
`StateSelected` with an inverted pill style (`:739-749`, `:761-768`); if the selected chip was
pushed past the row, the ellipsis is inverted instead (`:751-755`, `:761`). Each chip is a
`fui::button` with `props.minTouchSize = 0` and `hitPadding` splitting the gap between neighbours
(`:772-777`, `TagChipRow.h:171-182`). So today a chip's **tap height is `chipHeight` plus at most
one `gap`**, not `minTouchSize`.

Tapping a chip → `selectChip` (`:187-208`) recomputes `visibleIndices_` and rebuilds under
`RenderLock`. Tapping the ellipsis → `onMoreEvent` → `openTagFilter` (`:176-185`, `:210`). Confirm on
ring position 0 also opens it (`:265-270`).

**The … screen.** `TagFilterActivity` is a `UiListActivity` (`TagFilterActivity.h:25`) that
snapshots `STUDY.activeTags()` (`TagFilterActivity.cpp:16`, `:39`) and lists `All`, `Unlabelled`,
then **every** active tag — no counts, no zero-count filtering, no scope (`:43-58`). It pages
through `syncListViewport` + `screen.list` (`:60-66`). Tapping a row → `activateIndex`
(`:162-177`) returns an empty `TagSelectionResult` for All, `{UNLABELLED}` for Unlabelled, or the
tag's id. Back and Home both return a cancelled result (`:69-81`; the header comment at
`TagFilterActivity.h:30-34` explains why both exits must set one). **Long-press a tag row retires
it** after an `OptionPopup` confirmation (`:83-129`), refused while `STUDY.saveDisabled()`
(`:94-99`). It is one of two retire entry points; the other is `TagPickerActivity.cpp:253-271`.

On return, Highlights applies the pick, calls `dropRetiredFilter()`, and **always** rebuilds rows
and chips, cancel included, because a retirement may have changed them
(`HighlightsActivity.cpp:213-238`).

## Limits the grid must live within

- **Palette size:** `TagPalette::MAX_ACTIVE_TAGS = 200` (`lib/StudyStore/StudyStore/TagPalette.h:38`),
  names up to 24 bytes (`:39`). The comment at `TagFilterActivity.h:62` says the cap is 100; that is
  `HighlightDoc::MAX_TAGS` (`lib/Epub/Epub/HighlightDoc.h:24`), the old per-book store, so the comment
  is stale. A grid can have up to 202 candidates (All, Unlabelled, 200 tags).
- **Interaction table:** `UiAppHost::MAX_INTERACTIONS = 96` (`src/components/UiAppHost.h:35`),
  double-buffered; past it, hits are dropped and the element is silently untappable
  (`UiAppHost.h:28-31`, overflow logged at `UiAppHost.cpp:24`; buffer logic
  `freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUICore.h:1014-1022`). `TagChipRow.h:85-87` still says
  "64-interaction table", which is stale; the real cap is 96. **A single grid frame cannot hold 202
  chips**, so the grid needs pages or scrolling past one screen, and a per-frame chip cap under 96
  less the chrome.
- **Touch size:** FreeInkUI's `minTouchSize` defaults to 44 (`FreeInkUICore.h:638`) and becomes
  `bodyLineHeight + 14` when that is larger (`freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:117`).
  `spaceSm` is `max(4, bodyLineHeight / 6)` (`:118`); `spaceMd = 8`, `spaceLg = 16`
  (`FreeInkUICore.h:635-636`). Tokens come from `uiThemeTokens` (`src/components/UIThemeTokens.h:16`),
  and `listInset` is 0 on BaseTheme and 20 on Lyra (`src/components/themes/BaseTheme.h:145`,
  `src/components/themes/lyra/LyraTheme.h:23`). The small font's actual line height on the device is
  **not measured here**; whether today's `chipHeight` is under 44 px has to be computed from the
  theme at runtime, not assumed.
- **Render-task stack:** `TagChips::Layout` is filled into a member, not returned, because by value
  it would be ~400 B on the render task's stack (`TagChipRow.h:105-106`,
  `HighlightsActivity.h:202-204`). A grid layout of up to 202 placements must follow the same rule,
  or be sized per page.

## Nearest existing examples

1. **The wrap itself** — `TagChips::layout` (`TagChipRow.h:107-156`) already does greedy
   left-to-right wrapping with fixed per-chip widths; the grid is the same algorithm without
   `MAX_LINES`/ellipsis, split into pages by lines that fit the body height. Its tests
   (`TagChipRowTest.cpp:181-330`: `TagChipLayout.*`, `TagChipGeometry.HitRectsNeverIntersect`,
   `NeighboursShareTheGapExactly`) are the model for the grid's host tests.
2. **A paged grid within the interaction cap** — `NumberGridLayout.h` (`MAX_CELLS = 70` sized
   against the interaction cap, `:15-18`; `pageCount`/`pageStartFor`, `:79-115`) and
   `BookGridLayout.h` (variable-width labels, `MAX_PAGES` kept small for the stack, `:25-30`), both
   tested in `test/number_grid`. Paging is driven by a vertical swipe consumed in
   `BibleNavigationActivity::handleCustomInput` (`BibleNavigationActivity.cpp:417-424`) and by
   `ButtonNavigator` page steps (`:405-415`).
3. **Chip drawing** — `HighlightsActivity::buildChipRow` (`:708-790`) is the only chip renderer;
   the grid should share its measure/style code rather than copy it.

## Implications for the design (facts, not decisions)

- The grid must show **counts**, and counts depend on Highlights' scope (`spineFilter_`,
  `computeVisibleIndices`). `TagFilterActivity` has neither today, so Highlights has to hand it the
  counted candidates (or the scope), rather than the filter screen re-deriving them.
- Today the header row hides zero-count tags and a zero Unlabelled (`:150-158`), while the filter
  list offers every active tag and always Unlabelled (`TagFilterActivity.cpp:43-58`). "Filtering
  behaves exactly as today" has to pick which of those sets the grid shows; the spec must decide it.
- Long-press retire lives only on this screen and in the picker; replacing the list with a grid
  must keep it (and keep `saveDisabled` refusal and the always-rebuild on return).
- Raising `chipHeight` to `minTouchSize` makes the header's two-line band taller, which takes
  rows from the passage list below it.

## Tooling actually installed

```
$ ~/.platformio/penv/bin/pio --version
PlatformIO Core, version 6.1.19
$ cmake --version | head -1
cmake version 4.4.2
$ grep -n GIT_TAG test/CMakeLists.txt
17:  GIT_TAG v1.17.0          # googletest
$ git submodule status freeink-sdk
 67f7e012e3554106062bc52a3131523339a8ac4e freeink-sdk (67f7e01)
```

## Sibling tasks in this run

`hpipe show` for t1–t4 lists their files: `EpubReaderActivity`, `EpubReaderMenuActivity`,
`ReaderMenuSheetLayout.h`, `BibleNavigationActivity`, `launcher/`, `MeetingWeekView`, `Masthead`,
`CoverBand`, `ReaderMenuModel.h`, `ActivityManager`. None of them touches `TagChipRow.h`,
`HighlightsActivity`, `TagFilterActivity`, `TagRowMapping.h` or `test/tag_rows/`.

## Tier

Stays `standard`: the change is on the `ui` surface only, with no on-disk format, no store and no
shared contract. `TagFilterActivity`'s constructor and `TagSelectionResult` are used only by
`HighlightsActivity`. New tests fit the existing `test/tag_rows` executable. A new source file there
means editing `test/tag_rows/CMakeLists.txt`, not the shared `test/CMakeLists.txt`.
