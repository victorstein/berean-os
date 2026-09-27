Tier: heavy

# PR #169 review: code quality, pass 0

Scope: `gh pr diff 169` (source, tests and translations; the planning documents were not
reviewed for quality). The branch contains `origin/main` (`b566b730`). Citations are to the
checked-out head `faaf709e`.

## Summary

The work is sound. The extraction mirrors what was already there:

- `CoverThumb::pathFor` and `CoverThumb::drawNative` are the launcher's
  `coverThumbFor` and `drawCoverNative` moved without behaviour changes, with the rationale
  comments carried across (`src/util/CoverThumb.cpp:19-28`).
- The launcher calls through the new helpers (`LauncherActivity.cpp:137,162,331`).
- The pure view model (`MeetingWeekView.h`) follows the `LauncherRefresh.h` precedent: global
  free functions beside the activity, with a host suite.
- Translated words go through the existing `catalog::wordAt`/`copyOut` from #163. They are not
  re-implemented.
- The settings enum follows the `PUBLICATION_LANGUAGE` shape and its "persisted by ordinal,
  append" rule (`CrossPointSettings.h:72-83,355-357`).
- Error handling matches the house shape. Failures log and fall back to the placeholder cover,
  no bar or a blank band, and none of them fails silently: `MeetingsActivity.cpp:127,165-182,
  243-255`, `BookCacheUtils.cpp:43-55`.
- `bookCard` is used for the first time in the repo. That is a justified new use of an existing
  FreeInkUI component, not a second way to do something the repo already does.

The tests are designed around the risks, not just present:

- The Monday of every ISO week from 2025 to 2027 is checked by round trip, and `2025/W53` is
  refused (`WolWeekScanTest.cpp:238-261`).
- An unterminated-view check proves that `%s` does not run on into "October"
  (`MeetingWeekViewTest.cpp:84`).
- The float clamp is tested at ±1e19 and NaN (`BookProgressTest.cpp:80-87`).
- The key parser's rejection table covers every shape `meetingWeekKey` would not produce.

No BLOCKER or MAJOR findings. The MINORs follow, most significant first.

## MINOR 1: A third Hinnant calendar and a second copy of the UTC→local day shift

- **Evidence**:
  - `WolWeekScan.cpp:32-49` adds `civilFromDays`, and `WolWeekScan.h:30-34` adds a global
    `CivilDate` with a `uint16_t` year.
  - Both already exist on `main` as `study_sleep::civilFromDays` and `study_sleep::CivilDate`
    (`StudySleepPick.h:147-175`), where the year is `int32_t`.
  - `localDateFromUtc` (`WolWeekScan.cpp:118-126`) repeats `formatDateLine`'s clamp to 104, its
    quarter-hour minute arithmetic and its ±1 day shift line for line
    (`StudySleepPick.h:201-206`).
  - `StudySleepPick.h:158-159` justifies its own copy by the network copy being file-local. This
    PR makes that copy public, so the justification no longer holds.
- **Why only MINOR**: the plan states the deferral (plan:48-52), plan review 0 raised it
  (MINOR 2), and the PR body lists it as a follow-up. Both copies are pure and host-tested, so
  the risk is drift, not a present defect. Folding it here would reverse a reviewed decision.
- **Fix**: no follow-up issue exists yet (`gh issue list` has no match). File one before merge
  that names both implementations. The fold has one natural shape: `formatDateLine` calls
  `localDateFromUtc`, and `study_sleep` drops its `CivilDate`, `daysFromCivil` and
  `civilFromDays`.

## MINOR 2: `parseProgressBytes` is a second parser for `progress.bin`, and the reader does not use it

- **Evidence**:
  - `src/util/BookProgress.h:15-24` encodes the 4/6/10-byte layout and the `UINT16_MAX`
    sentinel.
  - The reader still parses the same bytes inline (`EpubReaderActivity.cpp:213-232`), and the
    header says so: "The layout and its quirks are EpubReaderActivity's" (`BookProgress.h:6-7`).
  - A future change to the format, such as a fifth size, now has to be made in two places. Only
    one of them is under a host test.
- **Fix**: point `EpubReaderActivity::onEnter`'s parse at `parseProgressBytes`. The reader
  still needs the text offset from the 10-byte form, so add it to `SavedProgress` or have the
  reader read it separately. That is a reader change that needs a device check, so it is
  reasonable to add it to the MINOR 1 follow-up issue rather than make it here. At minimum, put
  a comment at `EpubReaderActivity.cpp:213` that points at `BookProgress.h`, so the next format
  change finds both parsers.

## MINOR 3: Three includes in `LauncherActivity.cpp` are dead after the extraction

- **Evidence**:
  - `#include <Epub.h>` (`LauncherActivity.cpp:4`), `#include <FsHelpers.h>` (`:5`) and
    `#include <SdPaths.h>` (`:13`) no longer have any user in the file.
  - `Epub`, `FsHelpers::` and `sdpaths::` appeared only in the removed `coverThumbFor`. On
    `main` their only uses were `LauncherActivity.cpp:272,274`. The one remaining "Epub" match,
    at `:236`, is inside a comment.
- **Fix**: remove the three includes. `Bitmap.h` (`:3`) stays, because `drawCoverFilling` still
  uses it (`:350`).

## MINOR 4: The range buffer size is written as a bare 96 in two unlinked places

- **Evidence**: `MeetingsActivity.h:95` has `char rangeLine[96]`, and `MeetingWeekView.cpp:10`
  has `MAX_RANGE_BYTES = 96`. The first is the output capacity and the second is the
  intermediate `snprintf` buffer (`:52`). If someone later raises one of them alone, a longer
  translation either fails to fit or fails for no visible reason. `letters[7][8]`
  (`MeetingsActivity.h:98`) is the same kind of magic number.
- **Fix**: move `MAX_RANGE_BYTES`, and a `MAX_WEEKDAY_LETTER_BYTES`, into `MeetingWeekView.h`
  as `inline constexpr`. Size `WeekHeader`'s arrays from them, and use `week_.strip.size()` or a
  named 7 for the first dimension.

## MINOR 5: The branch that reads the cover size tests `coverPath.empty()` twice

- **Evidence**: `MeetingsActivity.cpp:242-256` is
  `if (!empty && !sizeOf(...)) {log} else if (!empty) {fit check}`. The empty-path case is
  handled by falling through both conditions, so the reader has to work out that "empty path
  means placeholder".
- **Fix**: an early `if (!card.coverPath.empty()) { if (!sizeOf) log; else fit check; }` block,
  or a small `bool fitCover(Card&)` helper. This is cosmetic.

## Checked and not raised

- **Copy-then-move of `meetingDayValues`** (`SettingsList.h:395-400`). Evaluation order inside
  a braced initialiser list is guaranteed left to right, and the comment says why the copy comes
  first. It is correct as written.
- **`rectOf`** (`MeetingsActivity.cpp:39`). No shared `fui::Rect` factory exists in `src/`,
  and the other eight sites build `fui::Rect{static_cast<int16_t>…}` inline. A file-local
  helper is proportionate.
- **Swipes swallowed in `handleCustomInput`** (`MeetingsActivity.cpp:282-287`). This is the
  base's documented hook (`UiListActivity.h:56`), and other list activities override it the same
  way.
- **`drawRefresh` sets `rowHeight` and `selectedIndex` itself instead of calling
  `syncListViewport`**. The one-row list is placed in a fixed band, so there is no viewport to
  sync. The comment at `MeetingsActivity.cpp:401-403` explains the choice.
- **No new `test/CMakeLists.txt` lines in the diff**. This is the orchestrator hand-off, and the
  PR body gives the lines.

VERDICT: CLEAR
