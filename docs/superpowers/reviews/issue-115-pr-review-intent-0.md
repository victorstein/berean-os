Tier: heavy

# Issue #115: PR #169 intent review, pass 0

Reviewed: `gh pr diff 169` (head `99b8a8b9`, base `b566b730`), issue #115, spec
`docs/superpowers/specs/2026-09-27-issue-115-design.md`, plan
`docs/superpowers/plans/2026-09-27-issue-115-plan.md`.

## Summary

The PR does what the issue and the spec ask for. Every acceptance item in the issue is
implemented or is a deviation the spec states and the PR body lists again. The screen code
matches the plan's Task 12 text. I extracted the plan's `MeetingsActivity.h`/`.cpp` blocks
and diffed them against the branch: the header is identical, and the `.cpp` differs only by
two lines that clang-format re-wrapped. I found no silent scope cut and no scope creep. The
tests check behaviour (dates, strip mapping, byte parsing, rounding) and do not restate the
implementation. Nothing here blocks the merge. There are two minor findings.

## Findings

### MINOR 1: Two small test items from the spec are missing

- The spec's Testing section (spec:690-692) asks for `formatWeekRange` over the same month,
  two months and **two years**, each with the English **and** Spanish formats, and for
  `wordAt` to return the **first** and last word.
- `test/meeting_week_view/MeetingWeekViewTest.cpp:90-98` tests two years in Spanish only
  (`civil(2026, 12, 28)` at `:96`). `:112-118` tests word indices 2 and 6 but not index 0.
- Both paths are covered in substance. The English two-year case runs the same
  `twoMonthFormat` branch that `:92` tests in English, and index 0 is the first iteration of
  `catalog::wordAt`, which `test/catalog_stamp` already covers. The risk is negligible.
- **Fix inline (optional):** add an English assertion for `civil(2026, 12, 28)`
  ("Week of 28 December – 3 January") and a `copyWordAt(..., 0, ...)` → `"L"` check.

### MINOR 2: The settings location departs from the issue's two options (stated, surfaced)

- The issue says the new settings go "in the Reader or a new Meetings category of
  `SettingsList.h`". The branch puts both in `STR_CAT_SYSTEM`
  (`src/SettingsList.h:397-400`), next to `meetingPrefetch`.
- This is not silent. Spec A10 (spec:337-346) argues for it and asks the review to attack
  it. Spec review 0 found it sound (MINOR 9). The PR body repeats it as behaviour difference 3.
- It is cheap to reverse and the existing meeting settings already live in System, so it is
  not a scope change that needs the human before merge. The human should still see it on
  the PR, and the PR shows it.
- **No fix needed.**

## Checked and sound

**Issue items:**
- **Header** (issue item 1). `headerTitle()` is `STR_MEETINGS` = "Reuniones". The range
  comes from `buildWeekHeader` → `mondayOfIsoWeek` → `formatWeekRange`
  (`MeetingsActivity.cpp:156-201`). The Spanish format is "Semana del %u al %u de %s", as
  the issue asks.
- **Week strip** (item 2). The strip starts on Monday. Today is inverted only when the local
  date falls inside the shown week (`src/activities/network/MeetingWeekView.cpp:14-26`). The two
  settings put dots on their days, and the legend appears only when some day is set
  (`MeetingsActivity.cpp:196`, `drawLegend`).
- **Cards** (item 3):
  - The order is Watchtower first (`buildCards`).
  - The cover comes from the shared `CoverThumb` helper. It was extracted, not copied, and
    the launcher calls through it at its three former call sites
    (`LauncherActivity.cpp:136-137, 162, 216`).
  - Each card shows the title and issue, and a bar with an "N% read" label.
  - Tapping keeps the old actions (`activateIndex`).
  - The Memorial week gives one card, because an empty issue is skipped (`buildCards`, the
    `issue.empty()` continue).
- **"Unavailable" status**. It is no longer shown: the card is simply absent (spec A5), and
  PR behaviour difference 1 says so.
- **Refresh** (item 4). It stays the last item, laid out in a reserved band with
  `rowHeight` equal to the band, so `list()` cannot drop it (spec A8/A9 pass-1 MAJOR 1).
- **Progress and the issue's open question**:
  - `readBookProgressPercent` (`src/util/BookCacheUtils.cpp:32-55`) uses
    `Epub::load(false, true)`. I read `lib/Epub/Epub.cpp:361-413`: with a cached `book.bin`
    and `skipLoadingCss`, it returns before any zip access, and it never builds.
  - The parse rules mirror the reader (`EpubReaderActivity.cpp:213-232`): 4, 6 or 10 bytes,
    and the `UINT16_MAX` sentinel.
  - The arithmetic is the reader menu's (`EpubReaderActivity.cpp:296-302`), and it reuses
    `calculateProgress`.
  - The out-of-range spine guard and the float clamp are present (spec A2).
  - The one known divergence is the footnote return with `pageCount == 0`. The spec states
    it and PR behaviour difference 4 lists it.
- **Settings**:
  - Two enums with values 0 = Not set and 1..7 = Monday..Sunday, defaulting to Not set
    (`CrossPointSettings.h`).
  - Labels are assigned by enum value and go through `tr()`/`StrId`.
  - Persistence is automatic, with no format bump.
- **Out of scope, respected**. There is no study-article line and no reminder.

**Spec:**
- **A3**. The week stays the UTC ISO week, and only "today" is local through
  `localDateFromUtc`, which clamps the offset as `HalClock` does.
- **A4**. The header dates the week of the entry on screen, from its key through
  `isoWeekFromKey` (`MeetingsActivity.cpp:164`). If there is no entry it uses the current
  week. If there is neither, it draws the title only.
- **A8**:
  - Every band is reserved in `computeLayout`, and the cards are sized for two.
  - Cover sizes are measured with `CoverThumb::sizeOf`.
  - The placeholder box is 0.75 × h, and a cover that is too tall or too wide is refused,
    with `LOG_DBG` on both paths (plan review 0, MINOR 5 applied).
  - The spec says `takeBottom`, but the code uses absolute rects plus a content margin set
    to the Refresh band. That matches the spec's own rule that "`buildScreen` places the
    cards and Refresh from `Layout`'s rects only" (spec:249-252). It is the plan's mechanism
    and gives the same result.
- **A9**:
  - The cover is framed with `CoverFrame`, with themed title and meta text.
  - `progressMax = 0` is set on cards without a bar. I checked that this hides the bar in
    `book-card.h:133`, and that a painter returning `false` gives the grey fill
    (`book-card.h:71-75`).
  - Vertical swipes are consumed.
  - The selection is clamped under the `RenderLock` in which cards and header are swapped
    in (`MeetingsActivity.cpp:144-153`). That is plan review 0, MAJOR, applied.
- **A12/A14**:
  - `STR_MONTHS_LONG` and the public `catalog::wordAt`/`copyOut` came in with #163. The
    plan states the reuse (plan:23-36).
  - Every translated word is copied out and terminated before it reaches `%s` or a text
    draw.
  - The YAML keys are appended in the branch, and `test/CMakeLists.txt` is untouched in the
    PR diff, with its lines given in the PR body.
- **#114 overlap**. #114 has merged (`STR_WEEKDAYS` is on main, `english.yaml:481`). The
  branch keeps its own date helpers and lists folding them as a follow-up. This departs
  from A14's "if merged, reuse or fold". The plan states the departure (plan:48-52), plan
  review 0 raised it as MINOR 2, and the PR body repeats it.

**Tests:**
- The host suites cover the issue's two host items: a week range that spans a month and a
  year, Monday of W01/W39/W53 with `2025/W53` refused, and today and both settings mapped
  onto the strip, including same-day, unset and out-of-range values.
- `BookProgressTest` checks every parse size, the sentinel, and the float clamp (including
  ±1e19 and NaN).
- The device items (heap on entry and exit, real covers, the Memorial week) are rightly
  left to the human's checklist in the PR body.

VERDICT: CLEAR
