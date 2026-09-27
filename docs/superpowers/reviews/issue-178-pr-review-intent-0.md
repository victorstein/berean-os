Tier: heavy

# PR #180 — intent review 0

PR: `fix: reach subfolder books, one weekday source, local meetings week` (branch
`fix/178-batch-3-follow-ups`, head `dab71383`).
Checked against: issue #178 (body and the item 5 comment), the spec
`docs/superpowers/specs/2026-09-27-issue-178-design.md`, the plan
`docs/superpowers/plans/2026-09-27-issue-178-plan.md`, and the batch brief's extra
constraints.

## Findings

None at BLOCKER, MAJOR or MINOR. The rest of this review is the evidence.

## Batch-brief constraints

| Constraint | Evidence | Met |
|---|---|---|
| One commit per item | `faae0067` (3), `1b4f5ad6` (2), `3ad1caa5` (4), `a20d7ac4` (1), `dab71383` (5). This is the spec's dependency order (spec "Commits"). | Yes |
| Item 1 adds a reachable entry and no reading-surface gesture | The only files in `a20d7ac4` are `PublicationsActivity.{h,cpp}`, the two YAMLs and `USER_GUIDE.md`. `ReaderUtils.h` is not touched anywhere in the PR. | Yes |
| Item 3 changes no behaviour | See item 3 below. The one labelled edge (30 February) is disclosed in the spec (A3.1) and in the PR body. | Yes |
| Item 4b keeps publishing v1 and writes the condition down | `3ad1caa5` adds only comment lines to `catalog-index.yml`. The comment is at `.github/workflows/catalog-index.yml:15-34`, and no step, flag or asset list changes. The condition and steps are in `docs/file-formats.md:548-568`. | Yes |
| Item 2 removes the keys from every YAML, and the unused count does not grow | `grep -l WEEKDAYS lib/I18n/translations/*.yaml` finds nothing. I ran `gen_i18n.py` into the scratchpad and it reports `String keys: 477`, `Unused keys: 1`. | Yes |
| The PR title is `fix:` | `fix: reach subfolder books, …` | Yes |

## Issue acceptance, item by item

**1. Books in any folder.** A new `BROWSE_ROW = 1` sits between Search and the header
(`src/activities/catalog/PublicationsActivity.h:53-56`). `activateIndex` sends it to
`activityManager.goToFileBrowser()` (`PublicationsActivity.cpp:183-187`), which the issue
suggested reusing. `goToFileBrowser` replaces the stack (`src/activities/ActivityManager.cpp:204-206`),
as the spec requires (A1.2). The row takes the index as its `actionValue`, like Search,
and a long press on it does nothing, because `entryIndexForRow` returns -1 below
`FIRST_BOOK_ROW`. The row label and hint are in both YAMLs. Publications is reached
from the launcher by touch, so the entry is reachable.

**2. One weekday source.**
- `src/util/WeekdayNames.h` holds the one documented Monday-first order. It has the
  `static_assert` from spec m6.
- The three consumers all use it, as the spec specifies:
  - the settings loop, `src/SettingsList.h:253`;
  - the sleep screen, `StudySleepScreen.cpp:194-198` into `StudySleepPick.h:161-175`;
  - the strip, which takes `copyInitial` of each name (`MeetingsActivity.cpp:187-188`).
- The short forms are derived by `copyInitial` (`MeetingWeekView.cpp:37-48`).
  `STR_WEEKDAYS` and `STR_WEEKDAYS_NARROW` are gone from `src`, `lib/I18n/translations`,
  `test` and `scripts`. I grepped for both, since `gen_i18n.py` scans comments.
- The two Spanish changes on screen, "M" for Wednesday and "Domingo" with a capital,
  are spec decisions A2.2 and A2.3, and the PR body calls both out.

**3. Duplicated helpers.** Every pair the issue names is folded:
- **Civil date and the day shift.** Both now live only in `src/util/CivilDate.h`. The
  copies in `WolWeekScan.cpp` and `StudySleepPick.h` are deleted, and both files now
  include `util/CivilDate.h`. The helper bodies are byte-identical to the old
  `WolWeekScan.cpp` copies, as spec §3.1 requires ("moved verbatim").
- **Cover thumbnail.** It goes through `CoverThumb::pathFor`
  (`PublicationsActivity.cpp:80-81`). The BW1 decode stays, as the spec says.
- **Language label.** It is built by `src/util/PublicationLanguage.h`, and both call
  sites use it.
- **Download progress throttle.** It is now `src/util/ProgressThrottle.h`. The
  predicate matches the three deleted copies character for character, with times
  moved to `uint32_t` per spec M1/A3.7. All three activities (Meeting, Bible and
  Buscar) use it, and all five reset sites now call `reset()`.
  `MeetingDownloadActivity`'s separate `lastRepaintMs` timer stays, as the spec says.

`CivilDate` and `ProgressThrottle` are host-tested in new suites. The cover and
language folds have no host test. The spec justifies both gaps: `CoverThumb` needs
`Epub` and HalStorage (A3.4), and no host suite can see `StrId` (A3.6).

**4a. Local "this week".** All four sites the spec lists now read the week through
`readLocalDate`:
- `MeetingsActivity.cpp:107-109`
- `LauncherActivity.cpp:178-183`
- `MeetingDownloadActivity.cpp:125-133`
- `MeetingWeekPrefetch.cpp:26-36`

The sleep screen already used the local date. In the strip, today's highlight is
passed only when `shifted` is true (`MeetingsActivity.cpp:137`). This keeps today's
blank strip when the time cannot be read (spec m5 / A4.2). `WolWeekScan.h:28`'s
comment is reworded, as spec m7 asks. `localDateOrUtc`, including its UTC fallback,
is tested in `test/civil_date/CivilDateTest.cpp`.

**4b. v1 retirement.** It is handled as described in the table above. The condition
(on or after 2026-12-28, and no supported device still on v1.17.3 or older), the
download-count caveat and the four-step checklist all match spec §4b. The workflow
header and `docs/file-formats.md` carry the same text.

**5. Retire confirmation.** The English wording is reworded and Spanish gets its first
translation (`english.yaml:391`, `spanish.yaml:391`). Both are the spec's exact
strings. The popup button stays `STR_DELETE`, which the spec lists as a non-goal.

**Issue "Verify" section.** Running `gen_i18n.py` confirms the unused count. The PR body
reports `pio run`, ctest (1156/1156), the script tests and clang-format as passing. I
did not re-run the build or the host tests. Nothing in the diff contradicts those
results. The two device checks the issue asks for are listed for the human tester.

## Tests: behaviour or restatement?

The new tests check observable outcomes, and none of them mirrors the code under test:
- **Calendar:** known weekdays (1970-01-01 = Thursday, 2026-09-27 = Sunday), 30
  February and month 13 rejected, a year boundary and a leap day crossed.
- **Offset:** the day shifts back, forward and not at all, and a corrupt offset is
  clamped.
- **Throttle:** the 5 % and 5 s thresholds at their exact edges, and a real 32-bit
  `millis()` wrap. The test repaints at `0x100 + 648` ms, which is 5000 ms past
  `0xFFFFF000`. This is the device's arithmetic, which spec M1 asked the host to run.
- **Initials:** `copyInitial` is tested on ASCII, a two-byte lead, empty and null
  input, and a buffer too small to hold the character.

`StudySleepPickTest` gains `ADateThatDoesNotExistGivesNoLine` (A3.1) and moves its
Spanish expectation to "Domingo" (A2.3). `TheEpochWasAThursday` is deleted, but
`CivilDateTest` covers the same fact, so no coverage is lost.

## Plan divergence

The implementation follows the plan task by task, including the step in commit 3
that maps the ISO weekday onto the still Sunday-first list with `% 7`, which item 2's
commit then removes (`faae0067`). The one divergence is `USER_GUIDE.md` §10 and §12.
That file is outside the plan's lock. The PR body says so and gives the reason: #179
had merged before the rebase. The change is two documentation sentences that describe
this PR's own behaviour, so it adds no scope that needs a decision.

## Scope

Nothing is quietly dropped: every issue bullet and every spec requirement has a
matching change, cited above. Nothing extra was added either. The PR adds no
firmware behaviour beyond the five items, no stored format changes, and it does not
touch `MappedInputManager` or `HalClock`.

VERDICT: CLEAR
