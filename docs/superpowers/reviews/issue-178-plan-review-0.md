Tier: heavy

# Issue #178 — plan review 0

Plan: `docs/superpowers/plans/2026-09-27-issue-178-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-178-design.md`
Checked against the worktree at `125eefc4` (code identical to `main` at `4b1697a5`).

## Summary

The plan is sound. Every spec requirement maps to a task, in the spec's commit order
(3 → 2 → 4 → 1 → 5). I checked each quoted "replace this" block against the source.
They match verbatim, and every include anchor exists:

- `WolWeekScan.cpp:20-55,88-99,118-127`
- `StudySleepPick.h:147-213`
- `MeetingsActivity.cpp:105-140,156-201`
- `LauncherActivity.cpp:177-180`
- `MeetingDownloadActivity.cpp:24-25,127-136,189-190,249-257,286-287`
- `BibleDownloadActivity.cpp:33-34,64-65,214-222,244-245`
- `CatalogSearchActivity.cpp:35-36,59-64,137-138,388-389,458-470,553`
- `PublicationsActivity.{h,cpp}`
- `SettingsList.h:250-258`
- `MeetingWeekPrefetch.cpp:26-33`
- `catalog-index.yml:10-14`
- `file-formats.md:547-548`

The `CivilDate.h` bodies in Task 1.2 match `WolWeekScan.cpp`, with only `inline`
added and the parameters made `const`. The global names (`daysFromCivil`,
`civilFromDays`, `isValidCivilDate`, `addDays`, `isoWeekday`, `localDateFromUtc`)
clash with nothing in `src`, `lib`, `test` or `freeink-sdk`. The only other copies are
`study_sleep::`-scoped ones, which Task 1.5 deletes.

The type and signature chain holds from task to task:

- `CivilDate` keeps `uint16_t year`, the same as `HalClock::Date`
  (`lib/HalClock.h:30-34`).
- `formatDateLine` goes from `string_view` to `const char* const (&)[7]`, and Task
  2.3 correctly predicts the compile failure.
- `buildWeekHeader` renames `utcToday` to `localToday`, and
  `buildWeekStrip(const CivilDate&, const CivilDate*, …)` already takes the pointer.
- `readLocalDate(CivilDate&, bool&)` is used the same way at all four sites.

The test arithmetic checks out:

- ProgressThrottle: the wrap gives 4,352 ms, and the 5 s boundary lands at 10,999.
- Leap and clamp cases, and 2026-02-30 → "Monday 2 Mar" before the fold.
- Test counts: 6, then 8.
- `String keys: 475` is 477 minus 2.

The `FILES:` lines (plan:10-24) sit at column 0, outside any fence, and cover every
file a task edits. That includes `LocalDate.{h,cpp}`, `WeekdayNames.h`, both YAMLs,
`test/CMakeLists.txt`, the two new test directories as `/` prefixes,
`catalog-index.yml` and `docs/file-formats.md`. `StudySleepPick.h`'s header-only
suite already has `${REPO_ROOT}/src` (`test/study_sleep_pick/CMakeLists.txt:10-13`),
so its CMake file needs no edit. No host target compiles `MeetingWeekPrefetch.cpp`
or anything else that would now need `LocalDate.cpp`.

Tasks without a failing test are the ones the spec explicitly exempts: firmware-only
wiring (A3.4, A3.6, item 1, `readLocalDate`), pure moves that existing suites keep
covering (Task 1.3 under `WolWeekScanTest`), or docs and YAML edits. Every commit
builds and passes on its own.

No BLOCKER or MAJOR. Four MINORs, all fixable inline.

## MINOR

### m1 — The `STR_WEEKDAYS` grep in Task 2.5 runs before regeneration and will not print "nothing"

- **Claim:** the check "`grep -rn "STR_WEEKDAYS" src lib test` prints nothing"
  (plan:822-823) comes before the `gen_i18n.py` run (plan:824-825).
- **Problem:** `lib/I18n/I18nKeys.h` (and `I18nStrings.*`) are generated,
  gitignored and present in the worktree. Until `gen_i18n.py` or `pio run`
  regenerates them, the grep matches the stale enum entries. An implementer who
  follows the plan literally sees a failing check and may go hunting for a
  reference that does not exist.
- **Evidence:** at HEAD, `grep -rn STR_WEEKDAYS src lib test` hits
  `lib/I18n/I18nKeys.h:543` (`STR_WEEKDAYS_NARROW`) and `:598` (`STR_WEEKDAYS`), plus
  the two source uses.
- **Fix:** run `gen_i18n.py` first and grep afterwards, or exclude the generated
  files: `grep -rn "STR_WEEKDAYS" src lib test --exclude='I18nKeys.h' --exclude='I18nStrings.*'`.

### m2 — Commits 2 and 3 run the formatter before `git add` of their new files

- **Claim:** the conventions say to format "after `git add` of new files, before each
  commit" (plan:35-36). Task 1.11 does this (plan:586).
- **Problem:** Task 2.6 step 3 (plan:831) runs `./bin/clang-format-fix`, then
  `git add -A …`. Task 3.6 step 2 (plan:1113) does the same. The wrapper skips
  untracked files (project memory, "Format and check gates fail open"). So the new
  `src/util/WeekdayNames.h` (commit 2) and `src/util/LocalDate.{h,cpp}` (commit 3)
  are committed without being formatted. The final gate at plan:1281 catches it only
  after the fact, as a diff in a new commit rather than in the commit that added the
  files.
- **Evidence:** plan:831 and plan:1113, against plan:35-36 and plan:586.
- **Fix:** use Task 1.11's form in both places:
  `git add -A <paths> && ./bin/clang-format-fix && git add -A <paths>`.

### m3 — `docs/file-formats.md` gets prose, not the checklist the spec asks for

- **Claim:** spec §4b says "**Steps**, written as a checklist in both places"
  (spec:382).
- **Problem:** Task 3.5 puts a numbered checklist in the workflow header
  (plan:1066-1079). In `docs/file-formats.md` it adds a single paragraph that
  paraphrases the steps and points to the workflow (plan:1096-1104). The content is
  all there, but the form differs from the spec's.
- **Evidence:** plan:1091-1105 against spec:382-397.
- **Fix:** render the four steps in `file-formats.md` as a numbered list, using the
  same wording as the workflow comment. Alternatively, note in the plan that the docs
  point to the workflow's checklist on purpose, so there is one copy.

### m4 — `CivilDateTest` leaves out two of the spec's listed cases

- **Claim:** the spec's testing table lists `isoWeekday` for 2026-09-28 = 1 and
  2026-09-27 = 7, and `localDateFromUtc` shifting −1/0/+1 (spec:487).
- **Problem:** Task 1.1 tests only the epoch weekday (plan:113) and the 0 and +1
  shifts (plan:121-127). The −1 shift is reached only through
  `localDateOrUtc(haveTime = true)` in Task 3.1 (plan:855-859). Nothing is left
  untested, because `WolWeekScanTest` already asserts all of these
  (`test/wol_week_scan/WolWeekScanTest.cpp:207-208,269-277`) and now exercises the
  same header. But the new suite does not match what the spec says it contains.
- **Fix:** add the two weekday assertions to `TheEpochWasAThursday` (renaming it to
  cover known weekdays), plus one direct −1 case, e.g.
  `localDateFromUtc(civil(2026, 9, 28), 2, 0, 24, local)` → `2026-09-27`. The
  alternative is to note that `WolWeekScanTest` is the suite that covers them.

VERDICT: CLEAR
