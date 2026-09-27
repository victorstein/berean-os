Tier: heavy

# Issue #115 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-115-design.md` at `eb0e3233`, against
`gh issue view 115 --repo victorstein/berean-os` and
`docs/superpowers/research/2026-09-27-issue-115-research.md`. Every citation below is to
a line I read in this worktree.

Most of the spec checks out. The UTC-week and local-today split (A3), dating the week
from the entry's key (A4), the Memorial single card (A5), the settings persistence (A10),
the calendar test vectors, and all the `file:line` references I checked are accurate. I
also traced the render path: `requestUpdate()` is deferred to the end of the manager's
loop (`ActivityManager.cpp:284-293`), so a slow `refresh()` in `onEnter` or in the result
handler does not race the render task. There is one real blocker, in the sizing that the
issue's main feature (the covers) depends on.

---

## BLOCKER 1: the cover box is sized at 0.6 × h, but thumbnails are wider than that, so no cover ever draws

**Claim (A8, spec:165-166).** "The cover width is `ceil(0.6 × h)`, because
`generateThumbBmp` emits `height * 0.6` wide (`Epub.cpp:706`) and `drawCoverNative`
refuses anything larger than its box." A7 (spec:145-146) keeps the never-rescale rule.

**Problem.** `0.6 × h` is the *target* width that `generateThumbBmp` passes in. It is not
the width the file ends up with. The 1-bit thumbnail converters run in crop mode, which
scales the image so it *covers* the target box, and then write the scaled image at full
size with no crop step. So a cover with aspect ratio `a > 0.6` comes out `a·h` wide
and `h` tall. A cover narrower than 0.6 comes out `0.6h` wide and taller than `h`.
`drawCoverNative` rejects both (`width > boxWidth || height > boxHeight → return 0`). Only
a cover whose aspect is exactly 0.6 would draw. Every real Watchtower or workbook cover
would fall back to the grey placeholder, and the covers are the first feature the issue
title names.

**Evidence.**
- `lib/Epub/Epub.cpp:706-709`: `THUMB_TARGET_WIDTH = height * 0.6` is passed to
  `jpegFileTo1BitBmpStreamWithSize`, and the PNG branch does the same at `:741-744`.
- `lib/JpegToBmpConverter/JpegToBmpConverter.cpp:731-733`: `...WithSize(...)` calls
  `jpegFileToBmpStreamInternal(..., true /*oneBit*/, true /*crop*/)`.
- `JpegToBmpConverter.cpp:582-589`: when `crop` is set, `scale = max(scaleToFitWidth,
  scaleToFitHeight)` and `outWidth = srcWidth * scale`. The header is then written with
  `outWidth, outHeight` (`:609-616`, `writeBmpHeader1bit(bmpOut, outWidth, outHeight)`).
  `grep -n crop JpegToBmpConverter.cpp` finds no other use, so no later crop exists.
- `lib/PngToBmpConverter/PngToBmpConverter.cpp:848-850` and `:588-594`: the same `crop=true`
  and `max` scaling.
- `src/activities/launcher/LauncherActivity.cpp:54-58` already records this: "Covers run
  roughly 0.6-0.75 wide-to-tall". `coverFillHeight` (`:303-305`) exists because a
  thumbnail generated at height `h` is wider than `0.6h`.
- `LauncherActivity.cpp:370`: `if (... width > boxWidth || height > boxHeight) return 0;`.

**Fix.** Size the cover column from what was actually generated, not from the target
ratio:
- In `refresh()`, after `CoverThumb::pathFor`, read the BMP header (`Bitmap::parseHeaders`,
  as `drawCoverNative` already does) and store the thumbnail's real width and height on
  the `Card`.
- Set `BookCardProps::coverSize` from that stored size. Both cards share one `h` (A8), so
  the widths differ only by aspect ratio.
- As an alternative, reserve a fixed column of `ceil(0.8 × h)` so every aspect up to 0.8
  fits. Then let `drawNative` centre the image, and state that a cover narrower than 0.6
  still falls back to the placeholder.

With either fix, drop the "0.6" rationale from A8. Add a device check that a real cover
draws: item 2 of the device list only covers the placeholder.

---

## MAJOR 1: A2's "same expression as the reader" parity claim is false for `pageCount == 0`, and the reader writes that value in a normal study flow

**Claim (A1 spec:50-51, A2 spec:61-66, device test 1 spec:493-494).** The card "cannot
drift from the reader's number". A page count of 0 gives a chapter fraction of 0. The bar
"must match the reader menu's 'Book x%'".

**Problem.**
- The reader's menu does not compute `calculateProgress(spine, 0)` when the page count is
  0. It guards the whole computation with `section->estimatedTotalPages() > 0` and
  otherwise shows 0% (`EpubReaderActivity.cpp:296-302`).
- The reader's menu number also comes from the live, re-laid-out section, not from the
  saved count. The two only coincide when the file's count is non-zero and the layout is
  unchanged.
- The reader writes `pageCount = 0` on purpose. Its destructor saves the return-stack
  origin as `saveProgress(origin->spineIndex, origin->pageNumber, 0)`
  (`EpubReaderActivity.cpp:163-166`). The stack is pushed on every in-book link or
  footnote jump (`:1701-1703`), so this happens whenever someone follows a footnote in
  the Watchtower and then leaves the reader.
- In that state the card shows the percentage at the *start* of the article. The reader
  menu shows the real position. The gap is up to one spine item's share of the issue, and
  device test 1 would fail in exactly the flow this screen serves.

**Fix.** Pick one and state it in A2:
- (a) Keep the approximation, remove "cannot drift" and "same expression", and word
  device test 1 as "matches, except after a footnote return, where the card shows the
  start of the chapter".
- (b) Get the fraction a different way when `pageCount == 0 && page > 0`. That needs a
  section-cache read, which adds format coupling and is probably not worth it.

Option (a) is the honest, cheap one. The decision does not need the human either way.

---

## MINOR 1: the out-of-range spine reasoning is wrong, and `roundPercent` casts before it clamps

**Claim (spec:345-347).** For a spine index at or beyond the spine count, `calculateProgress`
returns a value because `getCumulativeSize` gives 0, and "the result is clamped".
`roundPercent` is `clamp(int(fraction * 100 + 0.5), 0, 100)` (spec:309).

**Problem.**
- With `spine == spineCount`, `curChapterSize = 0 - prevChapterSize` wraps around, because
  it is `size_t` arithmetic (`Epub.cpp:928-929`). The result is about `1.8e19 × fraction`.
- Converting a float that large to `int` is undefined behaviour, and it happens before
  the clamp.
- The reader never saves `spine == spineCount` (its render returns at `:1039-1041` before
  the save at `:1304-1306`). A stale or corrupt `progress.bin` still can.

**Fix.**
- Clamp the fraction to `[0, 1]` in the float domain before multiplying and casting.
- Treat `spine >= epub.getSpineItemsCount()` as 100%, or as unparsed.
- Add both cases to the `roundPercent` and `readBookProgressPercent` tests.

## MINOR 2: the worker's `pio run` cannot pass while the YAML keys are handed to the orchestrator

**Claim.**
- Spec:388-400 puts every new `STR_*` key in the PR body for the orchestrator to apply.
- Spec:398-400 says the keys "must land in the same change".
- Spec:489 has the worker run `pio run`.

**Problem.** `StrId::STR_MEETING_WEEK_RANGE` and the rest do not exist until the YAML
changes, so the worker's build fails at compile time. This is an internal contradiction.
Issue #113's spec resolved the same tension by adding the keys in-branch
(`docs/superpowers/specs/2026-09-26-issue-113-design.md:40`).

**Fix.** Follow the #113 precedent: append the keys in-branch and also list them in the
PR. Otherwise, say explicitly that the build check runs after the orchestrator applies
the lines.

## MINOR 3: the Refresh row can silently disappear if its reserved height is smaller than the list's drawn row

**Claim (A8, spec:161).** The Refresh row is reserved from `metrics.listWithSubtitleRowHeight`.

**Problem.**
- `syncListViewport` is not called (A9, spec:186-187), so on this touch board
  `props.rowHeight` is left at the theme's `rowHeight`, not the metric. Compare
  `UiListActivity.cpp:124-136`, which overrides it only for non-touch hardware.
- The subtitle row can also grow when text wraps (`list.h:366-376`).
- `list()` skips any row that does not fit the remaining band (`list.h:444-446`), and a
  skipped row registers no hit. Refresh would vanish, with no way to tap it.

**Fix.** Set `props.rowHeight` to the same value the layout reserved, and set
`subtitleText.maxLines = 1`, or lay the Refresh band out with `takeBottom` using that
height. Add "Refresh visible and tappable" to device test 2.

## MINOR 4: `bookCard` details the spec leaves implicit

- **No bar.** `bookCard` always draws a progress bar while `progressMax > 0`, and the
  default is 100 (`book-card.h:21, 133-140`). The three "no bar" rows of A6 must set
  `progressMax = 0`.
- **Bar on a selected card.** The selected style inverts the card
  (`defaultListRowStyles`). The bar's fill is fixed solid black (`book-card.h:137-138`),
  so on a selected card the filled part disappears into the background. Use
  `selectionIndicator = BookCardSelectionIndicator::CoverFrame` (`book-card.h:55-57, 79-88`),
  which frames the cover and leaves the card uninverted.
- **Text styles.** `Screen::bookCard` does not theme the text styles
  (`FreeInkApp.h:373-379`). Set `titleText` and `metaText` from `screen.theme()`
  explicitly.

## MINOR 5: selection and swipe edge cases from ignoring the viewport

- **Stale selection.** `listCount()` can shrink from 3 to 2 across a `refresh()`, for
  example when a resolve returns the Memorial week. `nav.selected` can be left at 2, which
  is out of range, so nothing is highlighted and Confirm is ignored
  (`UiListActivity.cpp:53-54`). Clamp `nav.selected` at the end of `refresh()`.
- **Blank refreshes on swipe.** `nav.visibleRows` stays at 1 when the viewport is never
  synced (`list.h:148`). A vertical swipe then moves `top` and calls `requestUpdate()`
  (`UiListActivity.cpp:90-101`). That is a full e-ink refresh with nothing changed, not
  "changes nothing" (spec:186-187). Consume Up and Down swipes in `handleCustomInput()`,
  or keep `visibleRows = listCount()`.

## MINOR 6: local "today" combines an uncached date with a time that can be up to 10 s old

`getDate` reads the RTC fresh (`HalClock.cpp:42-52`). `getTime` returns a cached value for
up to `CLOCK_POLL_MS = 10000` (`HalClock.h:17`, `HalClock.cpp:17-22`). Within about 10 s of
UTC midnight the pair can straddle the day boundary, so `localDateFromUtc` is off by a day
for that window. The impact is cosmetic and brief. Either state that it is accepted, or
call `getTime` before `getDate` and re-read the date when the hour is 0. A combined
`getDateTime` belongs to hal-dev and is out of scope here.

## MINOR 7: `isoWeekFromKey` and `mondayOfIsoWeek` do not reject week 53 in a 52-week year

`"2025-53"` passes a `01..53` range check. `mondayOfIsoWeek({2025, 53})` then yields
2025-12-29, which is the Monday of 2026/W01, so a wrong week gets dated without any
error. The spec's rule "False for anything `meetingWeekKey` would not produce"
(spec:282) requires a year-aware check. Validate by round-tripping through
`isoWeekFromUtcDate`, and add `"2025-53"` to the rejection tests (spec:468).

## MINOR 8: `readBookProgressPercent` in `src/util` would depend on `src/activities/network`

The spec puts the parser (`parseProgressBytes`, `chapterFraction`, `roundPercent`) in
`src/activities/network/MeetingWeekView` (spec:304-310), but its only firmware caller is
`src/util/BookCacheUtils.cpp` (spec:339-343). That makes a util depend on an activity
module. Move the three progress functions into a util-level pure header, such as
`src/util/BookProgress.h`, next to their caller, and host-test them there. The strip and
range helpers stay in `MeetingWeekView`.

## MINOR 9: A10 (System, not Reader or a new tab) is sound and does not need escalation

The issue offers "Reader or a new Meetings category", and the spec picks neither. The
precedent is decisive: `publicationLanguage` and `meetingPrefetch` already sit in
`STR_CAT_SYSTEM` (`SettingsList.h:380-383`). A fifth tab means changes at
`SettingsActivity.cpp:34-35, 51-72`, which is more than two rows justify. The deviation
is labelled and listed for the PR (spec:513). Keep it as it is.

---

BLOCKER 1 invalidates the layout premise behind the issue's main feature. The fix is
mechanical, but A8 and the data flow (cover size known only after `refresh()`) have to
be rewritten before planning.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
