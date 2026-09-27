Tier: heavy

# PR #180 code-quality review 0

Scope: `git diff main...HEAD` over `src/`, `lib/`, `test/`, `.github/`, `docs/file-formats.md`.
Spec and plan docs were not re-reviewed.

## Summary

The refactor does what it says. The Hinnant date arithmetic is now in one place
(`src/util/CivilDate.h`), and the old copies in `WolWeekScan.cpp` and
`StudySleepPick.h` are gone. The three hand-copied repaint throttles in
Catalog/Bible/Meeting are now one `ProgressThrottle`. `SdFirmwareUpdateActivity`'s
`lastRenderedPercent` (`src/activities/settings/SdFirmwareUpdateActivity.cpp:229`) is a
different rule (repaint on any percent change), so leaving it out was correct.
The cover path goes through the existing `CoverThumb::pathFor` (`src/util/CoverThumb.h:12`),
the language label is built in one place, and weekday names have one source. The Browse
row copies the Search row exactly and keeps the row-index constants in step
(`PublicationsActivity.h:53-56`). Error handling keeps the existing shapes: early
`return false`, null or empty output on failure, and `LOG_ERR` + `fail()` in the
downloader. There is no dead or commented-out code, and the new comments explain why,
not what.

Nothing blocks. The findings below are about loose ends from the fold.

## MAJOR

### M1. The new `test/civil_date` suite copies tests that already exist in `test/wol_week_scan`

This PR exists to remove duplication, and it adds a second copy of a test suite.
The calendar helpers moved from `WolWeekScan.cpp` to `CivilDate.h`, but their tests
stayed in `test/wol_week_scan/WolWeekScanTest.cpp`. `test/civil_date/CivilDateTest.cpp`
then wrote them again:

| New in `test/civil_date/CivilDateTest.cpp` | Already in `test/wol_week_scan/WolWeekScanTest.cpp` |
|---|---|
| `IsoWeekdayRunsMondayToSundayFromAThursdayEpoch` (:48) | `CivilCalendar.IsoWeekdayRunsMondayToSunday` (:206), same three dates |
| `AddDaysCrossesAYearAndALeapDay` (:64), including the impossible-date case | `CivilCalendar.AddDaysCrossesMonthYearAndLeapDay` (:218), `AddDaysGivesAnEmptyDateForAnImpossibleOne` (:225) |
| `LocalDateShiftsBackForwardOrNotAtAll` (:54) | `LocalDate.ShiftsByTheClockOffset` (:263), which also covers Nepal +5:45 and a year boundary |
| `LocalDateClampsACorruptOffsetToUtcPlusFourteen` (:70) | `LocalDate.ClampsAnOffsetPastUtcPlus14` (:281) |
| `LocalDateRefusesAnImpossibleReading` (:78) | `LocalDate.RejectsAnImpossibleTimeOrDate` (:288), identical inputs |

The two helpers are also duplicated. `civil()` (`CivilDateTest.cpp:15`) and
`WolWeekScanTest.cpp`'s `civil()` and `expectDate()` do the same job, and they check
dates in different ways: a string compare against field-by-field `EXPECT_EQ`. Only four
cases are new: `RejectsDatesThatDoNotExist`, `TheDayCountStartsAtTheEpochAndRoundTrips`,
and the two `localDateOrUtc` tests.

Fix inline: move the `CivilCalendar.*` and `LocalDate.*` tests from `WolWeekScanTest.cpp`
into `CivilDateTest.cpp`, which now owns that code. Delete the five duplicates listed
above, keep the four new cases, and use one date helper. `WolWeekScanTest.cpp` keeps
`IsoWeek.*` and `IsoWeekMonday.*`, whose code still lives in `WolWeekScan.cpp`. This is
mechanical and needs no human decision.

## MINOR

### m1. `readLocalDate`'s `shifted` output is ignored at three of its four call sites

`readLocalDate(CivilDate&, bool& shifted)` (`src/util/LocalDate.h:8`) makes every caller
declare a throwaway `bool todayIsLocal = false;`. Three callers never read it:

- `src/activities/launcher/LauncherActivity.cpp:179`
- `src/activities/network/MeetingDownloadActivity.cpp:126`
- `src/network/MeetingWeekPrefetch.cpp:27`

Only `MeetingsActivity.cpp:137` reads it. A reader of those three sites sees a
named-but-unused variable that looks like a missing check. Two ways to fix it:

- Default the parameter to a pointer (`bool* shifted = nullptr`).
- Add a one-argument overload for the ISO-week callers.

Either keeps the "UTC fallback" rule inside `LocalDate.cpp`.

### m2. `isoWeekFromUtcDate` now receives local dates, but its name still says UTC

`src/network/WolWeekScan.h:30`: the comment was changed to "for a calendar date", but the
name was not. All four production call sites now pass `readLocalDate` output. The
`gmtime_r` detail in the comment explains why the name says UTC, but a reader at a call
site sees `isoWeekFromUtcDate(today…)` where `today` is local. Either rename it
(`isoWeekFromDate`) or leave the name and add a comment on why. A rename only touches
`WolWeekScan.*`, `MeetingPrefetchPlan.cpp:8` and the test file.

### m3. `copyInitial` hand-decodes the UTF-8 lead byte, which `lib/Utf8` already does, and handles invalid bytes differently

`src/activities/network/MeetingWeekView.cpp:43` inlines `lead < 0x80 ? 1 : (lead >> 5) ==
0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4`. That is `utf8CodepointLen` (`lib/Utf8/Utf8.cpp:74-80`)
except for the fallback: the lib treats an invalid lead byte as length 1, while
`copyInitial` treats it as 4. `utf8CodepointLen` is not in `Utf8.h`, so using it means
exporting it and linking `Utf8.cpp` into `test/meeting_week_view`. Inputs are always
translated weekday names, so this has no effect today. Either export the lib function,
or add one line saying why a private copy exists.

### m4. Weekday-name plumbing: a literal 7 and a hand-unrolled array

- `src/SettingsList.h:253`: `for (size_t i = 0; i < 7; ++i)`. The bound should be
  `std::size(WEEKDAY_NAME_IDS)`, so the loop follows the table instead of repeating its
  length.
- `src/activities/boot_sleep/StudySleepScreen.cpp:194-197`: seven hand-written
  `I18N.get(WEEKDAY_NAME_IDS[n])` initialisers. A short loop into a
  `const char* weekdayNames[7]` says the same thing, and can't drift from the table
  order.

Neither changes behaviour.

## Checked and fine

- `ProgressThrottle` takes `uint32_t` time, and a comment says why (the host has to wrap
  like `millis()` does). The wrap test (`ProgressThrottleTest.cpp:39-43`) tests exactly
  that; it is a well-chosen test, not just present. `MeetingDownloadActivity::scanWeek`
  now reuses `ProgressThrottle::MIN_UPDATE_MS`, so no constant is left orphaned.
- `formatDateLine` taking `const char* const (&)[7]` keeps `StudySleepPick.h` host-pure.
  The pointer array is the right boundary, not a second way of looking names up.
- `WeekdayNames.h` has a `static_assert` tying the table to the meeting-day enum's layout.
  It enforces the "index = setting − MONDAY" contract that `SettingsList.h` relies on.
- `copyWordAt` still has users (month names, `MeetingWeekView.cpp:61-62`). It is not left
  dead by removing `STR_WEEKDAYS_NARROW`.
- The v1-retirement checklist appears in both `.github/workflows/catalog-index.yml` and
  `docs/file-formats.md`. The copy is deliberate and each points to the other.
- New suites are registered in `test/CMakeLists.txt:137-138`, with the same
  header-only CMake shape as `test/study_sleep_pick`.

VERDICT: CLEAR
