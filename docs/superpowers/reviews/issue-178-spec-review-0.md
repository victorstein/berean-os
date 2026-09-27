Tier: heavy

# Issue #178 spec review 0

Spec: `docs/superpowers/specs/2026-09-27-issue-178-design.md` (the spec).
Research: `docs/superpowers/research/2026-09-27-issue-178-research.md` (the note).
Issue: `gh issue view 178 --repo victorstein/berean-os`, body plus the item 5 comment.
Checked at `f1e336d1`. Its `src/`, `lib/`, `test/`, `scripts/` and `.github/` trees match `4b1697a5`.

## Summary

The design is sound. I checked every labelled assumption against the code. All five items are
covered, and the batch-brief constraints hold:

- No change to the reader gesture. The file browser gets a touch entry.
- Item 4b is written down only; v1 is not stopped.
- The weekday keys are removed from both YAMLs that carry them. No other YAML has them:
  `grep -ln 'STR_MONDAY\|STR_WEEKDAYS' lib/I18n/translations/*.yaml` lists only `english.yaml` and
  `spanish.yaml`.
- Unused keys stay at 1. The baseline `gen_i18n.py` run gives `String keys: 477` and
  `Unused keys: 1`.
- There is one commit per item.

I found one MAJOR: a planned test cannot check what it claims to check. There are also several
MINOR seam and accuracy problems. None of them reverses a decision or changes scope.

---

## MAJOR

### M1. `ProgressThrottle`'s "millis() wrap" host test cannot show device behaviour with `unsigned long`

**Claim.** §3.4 declares `unsigned long lastMs_` and `unsigned long MIN_UPDATE_MS`, and takes
`nowMs` as `unsigned long`. A3.7 says "`nowMs - lastMs_` stays `unsigned long` arithmetic, so
`millis()` wrap behaves as today". The testing table lists a `millis() wrap` case in the new
`ProgressThrottleTest`.

**Problem.** `unsigned long` is 32 bits on the device and 64 bits on the host. The subtraction
therefore wraps at different values in the two places:

- Take `lastMs_ = 0xFFFFF000` and `nowMs = 0x100`, which is a real wrap.
- On the device the difference is 4,352 ms, so there is no repaint.
- On the host the difference is about 1.8e19, so it repaints.

The planned test has two outcomes, both bad:

- Written to device semantics, it fails on the host.
- Written to host semantics, it passes but proves nothing about the device.

That makes A3.7, the one behaviour claim the new suite exists to protect, untestable as specified.

**Evidence.**

- Device: `framework-arduinoespressif32/cores/esp32/esp32-hal.h:141` declares
  `unsigned long millis();`. Xtensa LX7 is ILP32, so that is 32 bits.
- Host: a `sizeof(unsigned long)` probe compiled with the host `c++` prints `8`.
- The host stub already models the device width: `test/stubs/Arduino.h:20` has
  `inline uint32_t millis() { return 0; }`.

**Fix.** Declare `lastMs_`, `MIN_UPDATE_MS` and the `nowMs` parameter as `uint32_t`, and pass
`static_cast<uint32_t>(millis())` at the three call sites. On the device this changes nothing,
because `unsigned long` is already 32 bits there. The host test then exercises the wrap the device
actually does. Update A3.7 to say that is the reason.

---

## MINOR

### m1. The commit 3 → commit 2 seam in `formatDateLine`, and three `StudySleepPickTest` cases the spec does not mention

**Claim.** Commit order is 3, then 2, and "each builds and passes the host suites on its own". §3.1
makes `formatDateLine` use `isoWeekday` in commit 3, and says to see item 2 for the name source.
The test table says `StudySleepPickTest` only changes its arrays, its Spanish expectation and one
new Feb 30 case, with "all offset tests unchanged".

**Problem.**

1. In commit 3, `STR_WEEKDAYS` is still the Sunday-first list (`english.yaml:481`), and
   `isoWeekday` is Monday = 1 (`WolWeekScan.h:36`). The spec does not say how commit 3 maps one to
   the other. `isoWeekday(local) - 1` would print "Saturday 27 Sep" for a Sunday. The mapping that
   preserves behaviour is `isoWeekday(local) % 7`.
2. `TheEpochWasAThursday` calls `study_sleep::daysFromCivil` and `study_sleep::weekdayFromDays`
   (`test/study_sleep_pick/StudySleepPickTest.cpp:234-235`). §3.1 deletes both, so commit 3 does
   not compile until that test is removed or moved.
3. `AMissingNameGivesNoLine` passes `""` as the weekday list (`StudySleepPickTest.cpp:268`). That
   cannot be expressed with the new `const char* const (&)[7]` parameter. It has to become an array
   with a null or empty entry. The `line()` helper at `:224` changes signature as well.

**Fix.** Pick one of these:

- State the commit 3 mapping (`% 7` into the still Sunday-first list).
- Move the whole `formatDateLine` rewrite into commit 2.

Then add to the test table:

- Delete `TheEpochWasAThursday`. `CivilDateTest`'s `isoWeekday(1970-01-01) == 4` replaces it.
- Rewrite `AMissingNameGivesNoLine` to use an array with an empty or null name.
- Change `line()` to take the array.

### m2. A5.1's length claim is false

**Claim.** "About 100 characters, shorter than `STR_CONFIRM_DELETE_PUBLICATION`."

**Problem.** Both new strings are longer than that key, not shorter. Measured with Python `len()`:

| String | Characters |
|---|---|
| New English | 99 |
| English `STR_CONFIRM_DELETE_PUBLICATION` (`english.yaml:447`) | 97 |
| New Spanish | 107 (108 bytes) |
| Spanish `STR_CONFIRM_DELETE_PUBLICATION` (`spanish.yaml:389`) | 102 |

The strings still fit, but for a different reason than the spec gives. The dialog height is
computed from its content: `fui::optionDialogHeight(target, props, width)`
(`src/components/OptionPopup.h`, in `processRender` where `height` is computed), so a longer title
makes the dialog taller rather than clipping it.

**Fix.** Rewrite A5.1 to say the popup sizes to its title, and cite `optionDialogHeight`. Device
check 4 should cover the Spanish UI, which has the longest string.

A related wording point: the prompt now says "Retire…", but its button stays `STR_DELETE`
(`TagFilterActivity.cpp:104`, `TagPickerActivity.cpp:250`). "Delete" can be read as "delete the
tag", and the prompt says no highlight is deleted, so this is defensible. The non-goal should still
state that reading explicitly, so the reviewer of the PR does not reopen it.

### m3. Two errors in the 4b retirement checklist

**Claim.** Step 2 is
`gh release delete-asset catalog catalog-S.txt catalog-S.txt.gz catalog-E.txt catalog-E.txt.gz`.
Step 3 removes "the v1 write (`:399-403`)".

**Problem.** The step 2 command fails as written, because `gh release delete-asset` takes exactly
one asset:

- `gh release delete-asset --help` shows `USAGE gh release delete-asset <tag> <asset-name>`.
- Running it with four names gives `accepts 2 arg(s), received 4`. That is gh 2.96.0.

Step 3 is incomplete. The v1 index is also *rendered* at `scripts/build_catalog_index.py:392`
(`legacy = render_index(rows, LEGACY_FORMAT_VERSION, …)`). Removing only `:399-403` leaves dead code
and an unused `LEGACY_FORMAT_VERSION`. The v1 description in the module docstring (`:28`) and in
`render_index` (`:172`) would also go stale.

This checklist is the durable artifact 4b produces, so a wrong command there costs the future
maintainer who runs it.

**Fix.**

- Step 2: `for a in catalog-S.txt catalog-S.txt.gz catalog-E.txt catalog-E.txt.gz; do gh release delete-asset catalog "$a" --yes --repo victorstein/berean-os; done`.
- Step 3: add the `legacy` render at `:392`, `LEGACY_FORMAT_VERSION`, and the v1 wording in the
  docstrings at `:28` and `:172`.

### m4. A3.5 leaves open a question the code already answers

**Claim.** If `CrossPointSettings.h` cannot see `StrId` without a new include, `langNameId` goes in
a new `src/util/PublicationLanguage.h` instead. "The implementer checks which."

**Problem.** It cannot see `StrId`. Its includes are `ArduinoJson.h`, `Epub/ReaderRenderSpec.h`,
`PersistableStore.h`, `SdPaths.h` and `<cstdint>` (`src/CrossPointSettings.h:2-7`). Their own
includes are:

- `ReaderRenderSpec.h`: only `<cstdint>`.
- `SdPaths.h`: only `<string_view>`.
- `PersistableStore.h`: `Arduino.h`, `ArduinoJson.h`, `Logging.h`, `<mutex>`, `<string>` and four
  Serialization headers.

None of these is I18n. By the spec's own rule, the helper goes in `PublicationLanguage.h`, and the
lead sentence "Beside its sibling `langWritten`" is wrong.

**Fix.** Decide now: use `src/util/PublicationLanguage.h`, including `<I18nKeys.h>` and
`CrossPointSettings.h`. Reword the §3.3 heading and its lead sentence to match.

### m5. A4.2's fallback also changes the strip highlight, and that change is not labelled

**Claim.** When `getTime` fails, `readLocalDate` returns the UTC date. `MeetingsActivity` passes
"the same local date … to `buildWeekHeader` as today".

**Problem.** Today, when `getTime` fails, `buildWeekHeader` highlights no day: `haveLocalToday`
stays false (`MeetingsActivity.cpp:187-194`). After the change it highlights the UTC date. A
highlighted day is a user-visible claim about what today is, and on the wrong side of the offset
window it would be wrong. `readLocalDate(CivilDate&)` gives the caller no way to tell a local
result from a fallback.

The case is rare: the RTC read has to fail inside `getTime` with nothing cached, right after
`getDate` succeeded (`HalClock.cpp:15-52`).

**Fix.** Pick one of these:

- Label it as A4.6 and accept it.
- Have `readLocalDate` also return whether the date was shifted (for example `bool* local`), and
  pass `nullptr` as today to `buildWeekHeader` when it was not. That keeps today's behaviour.

### m6. The settings loop drops the by-enum-value assignment its own comment requires

**Claim.** §2 says `SettingsList.h:250-258` "fills `meetingDayValues[i + 1] = WEEKDAY_NAME_IDS[i]`".

**Problem.** The block this replaces carries the rule "Assign these labels by enum value so a
reordered menu or enum cannot silently swap their behavior" (`SettingsList.h:231-232`). A bare
`i + 1` assumes `MEETING_DAY_MONDAY == 1` and that the days are contiguous, and nothing checks
either.

**Fix.** Write it as `meetingDayValues[CrossPointSettings::MEETING_DAY_MONDAY + i]`, and add
`static_assert(MEETING_DAY_SUNDAY - MEETING_DAY_MONDAY == 6)` in `WeekdayNames.h` or at the loop.
That static_assert pins A2.1's "index = meeting-day setting − 1" at compile time.

### m7. Small inaccuracies

- §3.1 says "the four test targets that compile `WolWeekScan.cpp`". There are five:
  `wol_week_scan`, `meeting_week_view`, `meeting_filename`, `month_name_map` and
  `meeting_week_table` (`grep -l WolWeekScan.cpp test/*/CMakeLists.txt`). All five already have
  `${REPO_ROOT}/src` on their include path, so `#include "util/CivilDate.h"` resolves unchanged.
  The conclusion holds; only the count is wrong.
- A4 keeps the name `isoWeekFromUtcDate` and explains why in the spec. The header comment still
  reads "ISO-8601 week-based year and week number for a UTC calendar date" (`WolWeekScan.h:26`).
  Once the function is fed local dates, the implementer should reword it to "a calendar date", or
  the comment will contradict its four callers.

---

## Assumptions checked and found sound

- **A1.2 / A1.3.**
  - `goToFileBrowser` replaces the stack (`ActivityManager.cpp:204-206`).
  - An empty path becomes `/` (`FileBrowserActivity.cpp:33`).
  - At the root, Back calls `onGoHome()` (`:392`).
  - Publications already leaves by replacing the stack: `activateIndex` → `goToReader`
    (`PublicationsActivity.cpp:190`). So a Browse row that does the same is proven on this path.
  - `entryIndexForRow` and `listCount()` both use `FIRST_BOOK_ROW` (`.cpp:31-35`, `.h:53`).
- **A2.1.**
  - `enumValues` is `std::vector<StrId>` (`SettingsActivity.h:33`).
  - The base settings list is a function-local static (`SettingsList.h:229-230`).
  - The meeting-day ordinals are 1 to 7 (`CrossPointSettings.h:74-82`).
  - Keeping the seven day keys as the single source is the only choice that does not reach into
    settings plumbing.
- **A2.2.** "L M M J V S D" follows directly from the issue's instruction to "derive the short
  forms from it". The spec labels it as a visible change. It is not a scope question.
- **A2.4.** `gen_i18n.py` scans `src` and `lib` only (`scripts/gen_i18n.py:831`), so the
  "no stale `STR_WEEKDAYS` spelling" rule matters there, not in `test/`. The baseline is
  `Unused keys: 1`, as the note says.
- **A3.1 / A3.2.**
  - `WolWeekScan.cpp:49-55` validates by a round trip.
  - `formatDateLine` checks only ranges (`StudySleepPick.h:198`).
  - `ClockReading::year` is `uint16_t` (`:184`).
- **A3.3.** `CoverThumb::pathFor` (`CoverThumb.cpp:29-47`) matches `loadThumb:82-92`, apart from the
  `LOG_DBG` and the height guard. The `generatedAny` popup logic in `refresh()` is unaffected.
- **§3.4 predicate.** It is identical in all three copies (`MeetingDownloadActivity.cpp:249-257`,
  `BibleDownloadActivity.cpp:214-222`, `CatalogSearchActivity.cpp:463-469`). Nothing outside the
  predicate and the resets reads `lastRenderedPercent` or `lastProgressUpdateMs`.
  `SdFirmwareUpdateActivity` has its own, different field.
- **A4.1.** The four sites are exhaustive. `grep -rn 'getDate(\|isoWeekFromUtcDate' src` finds
  only them, plus `MeetingPrefetchPlan.cpp:8`, which is pure week arithmetic, and the sleep screen.
- **A4.3.** The 10-second `getTime` cache is confirmed (`HalClock.cpp:18-23`), and `getDate` reads
  the RTC uncached (`:42-52`).
- **A4.5.**
  - `gh release view v1.17.4` gives `publishedAt 2026-09-27T10:37:54Z`.
  - `git tag --contains 0a88a616` starts at `v1.17.4`.
  - 2026-09-27 plus 13 weeks is Sunday 2026-12-27, so the first Monday run (cron `0 5 * * 1`,
    `catalog-index.yml:20`) is 2026-12-28.
  - Dropping `--out-v1` is enough to stop v1, because the builder treats it as optional
    (`build_catalog_index.py:401`).
- **Name collisions.** `daysFromCivil`, `civilFromDays`, `isValidCivilDate`, `localDateOrUtc`,
  `readLocalDate`, `ProgressThrottle`, `WEEKDAY_NAME_IDS` and `copyInitial` appear nowhere else in
  `src`, `lib` or `freeink-sdk`, so making them global inline functions is safe.

VERDICT: CLEAR
