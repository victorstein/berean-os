Tier: heavy

# Issue #115 spec review, pass 1

Reviewed: `docs/superpowers/specs/2026-09-27-issue-115-design.md` at `8b916fe5`, against
`gh issue view 115 --repo victorstein/berean-os`, the research note, and pass 0
(`docs/superpowers/reviews/issue-115-spec-review-0.md`). Every citation is to a line I read
in this worktree, or to command output quoted here.

## What I re-verified and found sound

- **Pass 0 BLOCKER 1 fix (measured cover size).** The crop-mode scaling is as stated:
  `JpegToBmpConverter.cpp:582-589` takes the larger fit ratio and writes `outWidth × outHeight`,
  and `PngToBmpConverter.cpp:588-594` does the same. `generateThumbBmp` passes `height * 0.6`
  only as a target (`Epub.cpp:706-709`, and the same in the PNG branch). Reading the real size
  back with `Bitmap::parseHeaders` and passing it as `coverSize` holds for a portrait cover.
  `bookCard` builds `coverRect` from `coverSize`, clamped to the content box
  (`book-card.h:68-70`). The float truncation in `outHeight = int(srcH * scale)` can give
  `h - 1`, which still fits.
- **Pass 0 MAJOR 1 and MINORs 1, 2, 4, 5, 6, 7, 8.** All applied correctly:
  - The footnote-return case is `EpubReaderActivity.cpp:163-166`.
  - The `size_t` wrap is at `Epub.cpp:929-930`, one line below the cited `928-929`. The guard
    and the float clamp are right.
  - `bookCard`'s `progressMax`, `CoverFrame` and unthemed-props claims match
    `book-card.h:37, 55-57, 133-140` and `FreeInkApp.h:373-379`.
  - The swipe path is `UiListActivity.cpp:84, 90-101`, and `handleCustomInput` runs first.
  - `RenderLock` in `refresh()` is safe on both call paths. `onEnter` and the result handler
    both run with the lock released (`ActivityManager.cpp:125-127, 152-153`).
  - `"2025-53"` is refused by the round trip.
- **Calendar test vectors.** I checked them with Python's `isocalendar`:
  - 2026-09-21 is W39 Monday, 2026-09-27 is weekday 7, and 2026-09-28 is weekday 1.
  - `fromisocalendar(2026,1,1)` = 2025-12-29 and `fromisocalendar(2026,53,1)` = 2026-12-28.
  - 2027-01-03 is still 2026/W53.
- **Progress path.** A1 matches the code:
  - `Epub::load(false, true)` returns at `Epub.cpp:409` without touching the zip, and returns
    false at `:413-415`.
  - `CssParser`'s constructor only stores a string (`CssParser.h:38`).
  - `Epub` is a few strings and `unique_ptr`s (`Epub.h:16-32`), so it is fine on the stack,
    as it already is in `LauncherActivity.cpp:274`.
- **A10 settings.**
  - `SettingInfo::Enum` takes `std::vector<StrId>` by value (`SettingsActivity.h:80`), so
    passing one vector twice by copy works.
  - `STR_NOT_SET` exists (`english.yaml:238`).
  - The clamp is at `CrossPointSettings.cpp:163-169`, and there is 4 KB of save budget
    (`CrossPointSettings.h:~427`).
- **A3 citations.** All four UTC week sites are as cited: `LauncherActivity.cpp:176-178`,
  `MeetingDownloadActivity.cpp:127-129` and `MeetingWeekPrefetch.cpp:29-31`.

Two findings are real defects in the revised layout. One of them is a pass-0 fix that was
only half applied. Neither needs the human, so both can be fixed inline.

---

## MAJOR 1: the Refresh band is still shorter than the row `list()` lays out on the Classic theme, so Refresh disappears (pass 0 MINOR 3, half-applied)

**Claim (A8 spec:214-217, A9 spec:278-283).** `refreshH` comes from
`metrics.listWithSubtitleRowHeight`, the band is `takeBottom(refreshH)`, and
`props.rowHeight = refreshH` with a single-line subtitle, "so the row height must equal the
band".

**Problem.** Setting `rowHeight` equal to the band is not enough. `list()` grows a subtitle row
to `labelLh + subLh` whenever the row height is smaller than that, and then drops any row
taller than its band. On the Classic theme the metric is smaller than the two fonts' line
heights, so Refresh is laid out 3 px too tall and never drawn. It also registers no hit, so it
cannot be tapped. Lyra (60 px) happens to fit, which is why the fix looks right on the default
theme. Classic is a user-selectable theme (`CrossPointSettings.h:217-220`).

**Evidence.**
- `list.h:436-446`, the subtitle branch:
  - `basePad = rowH - labelLh - subLh`;
  - `needed = labelLh * labelLines + subH + (basePad > 0 ? basePad : 0)`;
  - `if (needed > rowH) itemH = needed;`
  - then `if (cursorY + itemH > rowArea.bottom() ...) break;`
- Fonts: `uiScaleSpec()` binds body = `UI_12_FONT_ID` and small = `UI_10_FONT_ID`
  (`UIScale.h:16-17`). These are `ubuntu_12_regular` and `ubuntu_10_regular`
  (`main.cpp:123-131, 330-331`). `getLineHeight` returns `advanceY`
  (`GfxRenderer.cpp:2141-2148`), which is **29** and **24**: the fifth field of each
  `EpdFontData` initialiser at the tail of `lib/EpdFont/builtinFonts/ubuntu_12_regular.h` and
  `ubuntu_10_regular.h` (field order in `EpdFontData.h:176-181`). So `labelLh + subLh = 53`.
- `BaseTheme.h:134`: `.listWithSubtitleRowHeight = 50`, and 53 > 50, so the row is dropped.
  `LyraTheme.h:19`: 60, which fits.

**Fix.**
- Size the band from the fonts, never below what `list()` will lay out:
  `refreshH = max(metrics.listWithSubtitleRowHeight, lineHeight(body) + lineHeight(small))`.
  Take the line heights from the same `screen.target().lineHeight(FONT_BODY / FONT_SMALL)`
  the list uses.
- Keep `props.rowHeight = refreshH` and the single-line subtitle.
- Device test 2: check that Refresh is visible and tappable **under both Classic and Lyra**.

---

## MAJOR 2: a card with no usable thumbnail gets `coverSize = {0, 0}`, so it draws no placeholder and has no selection indicator

**Claim.** In the A6 table (spec:173-175), the "Download", "No entry" and failed-thumbnail rows
show the "placeholder", which is "`bookCard`'s own dithered light-grey cover fill, drawn when
the `coverPainter` returns false" (spec:181-183). Error handling (spec:584-585) says "Size
zeroed, and the placeholder cover". Device test 2 (spec:669-670) expects "the placeholder".

**Problem.** The Architecture section makes the size zero exactly when the placeholder is
wanted. The `Card` holds `coverWidth`/`coverHeight`, "0 when there is no usable thumbnail"
(spec:492-493), and `buildScreen` sets `coverSize = {coverWidth, coverHeight}` (spec:517). With
a 0×0 `coverSize`:
- The placeholder fill is drawn into an empty rect and does nothing.
- The text column starts at the card's left padding, so these cards do not line up with a
  covered card.
- `CoverFrame` selection strokes a 6×6 box around a point, so a selected "Download" or
  "Week unknown" card shows essentially no selection.

In the no-entry state both cards are like this, so stepping with Left/Right shows no visible
selection at all.

**Evidence.**
- `book-card.h:68-70`: `coverW`/`coverH` come straight from `props.coverSize`, and `coverRect`
  is built from them.
- `book-card.h:75-76`: `if (!coverDrawn) fill(coverRect, Paint::dither(LightGray))`.
- `FreeInkUIGfxRenderer.h:70-72`: `fill` does `if (rect.empty()) return;`.
- `book-card.h:79-88`: the `CoverFrame` rect is `coverRect` grown by `selectedCoverFrameGap`
  (3) on each side.
- `book-card.h:89`: `content.x = coverRect.right() + props.gap`.

**Fix.** Separate the placeholder box from the measured size.
- Add a layout constant for the placeholder, for example `placeholderW = h * 3 / 4` with a
  `static constexpr` aspect, and `placeholderH = h`.
- In `buildScreen`, set `coverSize` to the measured size when the thumbnail is usable, and to
  `{placeholderW, h}` otherwise. `coverPainter` still returns false for those cards, so
  `bookCard` fills the grey box and `CoverFrame` has something to frame.
- Update the Card field comment and the Error handling rows to match.
- Device test 2 then checks that the "Download" card shows a grey box of cover height and
  that its selection frame is visible.

---

## MINOR 1: A8 guards a cover that is too tall but not one that is too wide

**Claim (spec:239-242, 506-507).** Only "a thumbnail taller than the layout's `h`" is zeroed.

**Problem.** The width is measured and passed through unbounded. A landscape or square cover
image comes out `a·h` wide with `a ≥ 1`. `bookCard` clamps `coverW` to `content.width`
(`book-card.h:68`), so `drawNative`'s box becomes narrower than the bitmap and the draw is
refused. The text column, `rect.right() - padding.right - (coverRect.right() + gap)`
(`book-card.h:89-90`), then shrinks to zero or goes negative, and the title and status
vanish. The spec's "JW covers are not that narrow" argument only covers the other direction.

**Fix.** In `refresh()`, also treat `coverWidth > cardContentWidth / 2`, or any
fraction that leaves a readable text column, as unusable. That card then gets the MAJOR 2
placeholder, and a `LOG_DBG` records the size.

## MINOR 2: the range bands must not collapse, or the card height and thumbnail height follow the clock and the settings

**Claim.**
- A8 (spec:218-219) splits the card height for two cards "so the Memorial week does not
  generate a second thumbnail size".
- A11 (spec:323) says "Both unset means ... no legend line".
- A4 (spec:149) and Error handling (spec:580-583) say there is "no range line and no strip"
  in some states.

**Problem.** The spec does not say whether `computeLayout()`, which runs in `onEnter()` before
`refresh()` knows any of these states, reserves the range, strip and legend bands
unconditionally. If `buildScreen` omits a band and the cards take the freed space, the
drawn card is taller than the `h` the thumbnail was generated at. If `computeLayout` omits
it, toggling a meeting-day setting, or losing the clock, changes `h` and generates yet
another `thumb_<h>.bmp`. That is the cost A8 set out to avoid for the Memorial week. The
two layout passes (`computeLayout` and `buildScreen`'s `takeTop`/`takeBottom`) also have to
agree, and nothing states that they must.

**Fix.** State the following in A8:
- The range, strip and legend bands are always reserved at fixed heights, and left blank
  when their content is absent.
- `buildScreen` places the cards from `Layout`'s rects only, and never from `screen.body()`,
  so `h` depends on nothing but the theme and the panel.

## MINOR 3: `formatWeekRange` feeds `string_view` words into translator `%s` formats

**Claim (A12 spec:329-347, spec:431-434).** Month names come from `wordAt(STR_MONTHS_LONG, i)`
as a `std::string_view`. The formats are translator strings with `%s`.

**Problem.** A `wordAt` view points into the middle of the translated string and is not
null-terminated. Passing `.data()` to a `%s` prints the rest of the list: "Week of 21–27
September October November December". `CLAUDE.md`, "`std::string_view` and null
termination", names exactly this. The precedent avoids it only because it controls the
format and uses `%.*s` (`CatalogStamp.cpp:82`), which a translator's `%s` format cannot. The
strip letters from `STR_WEEKDAYS_NARROW` have the same problem when they reach `drawText`
or `text()`.

**Fix.**
- Specify that each word is copied into a small `char[]` (`snprintf(buf, n, "%.*s", ...)`)
  before it is passed to the translated format or to any text draw.
- Add a host test where the month is not the last word: a September range must not contain
  "October".

## MINOR 4: the out-of-range spine guard's test points at the wrong device item

**Claim (spec:650-651).** "It is covered on device (item 8)."

**Problem.** Device item 8 (spec:681-683) covers swipes and selection clamping. No device
item stages a `progress.bin` whose spine is at or beyond the spine count, so the pass 0
MINOR 1 guard is tested nowhere. This is a cross-reference broken by churn.

**Fix.** Add a device item: write a 6-byte `progress.bin` with spine `0xFFFF` into the
publication's cache directory through File Transfer, then check that the card shows "Open"
with no bar. Alternatively, say plainly that the guard is untested.

## MINOR 5: #114 in the same batch builds a second UTC-to-local date helper and weekday string

**Claim (A3, spec:125-132, 397-407; A14 spec:358-359).** `localDateFromUtc`, `civilFromDays`
and `isoWeekday` go into `WolWeekScan`. The spec notes that #114 shares the YAML files.

**Problem.** The #114 spec and plan on `feat/114-study-sleep-screen` add their own
`study_sleep::daysFromCivil`, `civilFromDays`, `CivilDate` and a quarter-hour shift, clamped
to 104 exactly as here:
- `git show feat/114-study-sleep-screen:docs/superpowers/specs/2026-09-27-issue-114-design.md`, A12, lines 198-207;
- the plan, lines 575-653.

They also add `STR_WEEKDAYS`, full names with Sunday first (spec A14, line 219), next to this
spec's `STR_MONDAY` … `STR_SUNDAY`. The namespaces differ, so nothing fails to link. But the
batch lands two implementations of one conversion, one of them in a file this spec says the
UI cannot reach. The implement phases are serialised (A14), so whichever runs second can
reuse the first.

**Fix.** Add a line to A3/A14: if #114 has merged when `implement` starts, reuse its helper, or
move both onto the one in `WolWeekScan`. Otherwise list the duplicate as a follow-up in the PR.
Record also that `STR_WEEKDAYS` and `STR_MONDAY`… overlap in content.

---

Two MAJORs and five MINORs. None reverses a decision, changes scope, or needs the human. The
MAJORs are mechanical layout corrections, one of them finishing a pass 0 fix, and all seven
can be applied inline before planning.

VERDICT: CLEAR
