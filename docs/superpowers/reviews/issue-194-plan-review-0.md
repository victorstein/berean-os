Tier: standard

# Issue #194 — plan review 0

Plan: `docs/superpowers/plans/2026-09-29-issue-194-plan.md`
Spec: `docs/superpowers/specs/2026-09-29-issue-194-design.md`
Branch `feature/194-compact-metrics-no-progress` at `eac9289a`. I re-read every code site the plan quotes against the tree. I did not run `pio run`.

## Summary

The plan is sound. Every spec requirement maps to a step:

- Goals 1–2 and A-1..A-5: Task 3.
- Goal 3, D3 and A-6..A-9: Tasks 1–2.
- Goal 5: Tasks 5–7.
- Goal 6, D4 and A-11..A-13: Tasks 4–5.
- Goal 7 and A-17/A-18: Tasks 8–9.
- A-19: Task 0.2 and Task 10.5.
- Gates: Task 10.

Every "replace this" block the plan quotes matches the current source character for character:

- `UiTabListActivity.cpp:76-87`
- `BaseTheme.cpp:327-333, 376-380`
- `LyraTheme.h:12-14, 32`
- `EpubReaderMenuActivity.h:33-36, 85-87` and `.cpp:18-29, 181-191`
- `EpubReaderActivity.cpp:257-281, 1631-1639`
- `LauncherActivity.h:86-89` and `.cpp:41, 84-98, 135`
- `StudySleepScreen.cpp:30, 54-57, 86, 230-289, 303, 319, 361-364, 396-401`
- `USER_GUIDE.md:103, 152, 371`

Names and signatures stay the same from step to step: `ListRowHeight::Inputs` and `resolve`, `bibleReference`, `currentBibleChapter() const` and `readerMenuTitle() const`. The designated-initializer order is correct: `touchListRowHeight` is declared directly after `listWithSubtitleRowHeight` (`BaseTheme.h:39`) and is placed there in `LyraTheme.h`.

The FILES lines (plan:7-16) are at column 0, outside any fence, and cover every committed path. `test/CMakeLists.txt` is edited only locally and is reverted in Step 10.4. That follows the precedent in `2026-09-26-issue-111-plan.md:16, 668`.

Other checks that passed:

- **Translation count.** The plan's figure of 98 is exact: 1 + 2 + 32 + 31 + 32, verified with the plan's own grep. None of the matching YAML values is multi-line, so the line-deleting `sed` is safe.
- **Stale references.** The only code references to the five keys are the ones the plan deletes: `EpubReaderMenuActivity.cpp:184-187`, `LauncherActivity.cpp:95` and `StudySleepScreen.cpp:284`.
- **Step 7.2 grep.** Its expected output of exactly one line is correct. After the deletions, `-w left` matches only the comment at `StudySleepScreen.cpp:314`.
- **Include removals.** `LauncherActivity.cpp` still gets `PubKey.h` from its own include at `:24`. `StudySleepScreen.cpp` uses nothing else from `ChapterCompletion.h`.
- **Behaviour parity in Step 4.5.** `resolveBibleChapterNumber` resets the number to -1 and returns early for a non-Bible (`EpubReaderActivity.cpp:1592-1598`). The number is resolved on every section load, whatever the title mode (`:1173`), so the menu title sees it too.
- **No lock in `readerMenuTitle()`.** This matches today's `openReaderMenu`, which already reads `bookFile` through `calculateProgress` and `getBookSize` without the lock (`:259-267`). The plan's lookup replaces that access; it does not add a new one.

Tasks 3, 6, 7 and 9 have no red test. That is appropriate: the spec gives them device checks, not host tests (spec "Testing strategy"), and they are metric values or deletions. Tasks 2 and 5 use a failing firmware build as their red step, and every commit point builds.

No BLOCKER and no MAJOR. Four MINORs, all fixable inline.

## Findings

### MINOR 1 — Step 8.3's gen_i18n warning check cannot fail

- **Claim.** plan:908-915 runs `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ 2>&1 | grep -i "warning"` and treats no match as proof of the spec's gate: "`gen_i18n.py` prints no 'keys not in English' warning" (spec:310).
- **Problem.** The script prints that warning only when `verbose` is set, and verbose defaults to off on the command line. The check passes whatever the YAML contains. The Step 8.2 counts (98 deleted, 0 added) cover the same risk, so the real exposure is small. But the PR body cites this check as evidence (plan:1018-1019), and it proves nothing.
- **Evidence.** `scripts/gen_i18n.py:234-238` (`if verbose: print(f"  WARNING: ...")`), `:826` (`verbose: bool = False`), `:984-989` (`--verbose` is `store_true`).
- **Fix.** Add `--verbose` to the Step 8.3 command:

  ```bash
  python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ --verbose 2>&1 | grep "WARNING"
  ```

  With `--verbose` the script also prints INFO fallback lines, so match only `WARNING`. Run the same command once before Step 8.2 as a baseline. If an unrelated warning already exists on `main`, the gate is "no new WARNING line", not "no WARNING line".

### MINOR 2 — two comments still name Lyra as the detached-battery theme

- **Claim.** A-3 moves Lyra to `headerBatteryDetached = false`. The plan rewrites the two `BaseTheme.cpp` comments the spec lists (Step 3.1) and no others.
- **Problem.** Two more comments become false once Lyra shares the title line. The project's comment rule asks for comments written for the merged state, and spec review 0 MINOR 8 was about exactly this kind of drift.
- **Evidence.**
  - `src/components/themes/BaseTheme.h:57-59`: "Battery in its own corner strip … with the title on the lower sub-band spanning the full width (Lyra), vs sharing the title line with a width reserve (Classic)."
  - `src/components/themes/BaseTheme.cpp:340-341`, inside `if (batteryDetached)`: "… so long book titles span the header (Lyra layout)."
- **Fix.** In Step 3.1, drop "(Lyra)" and "(Classic)" from `BaseTheme.h:57-59`, and drop "(Lyra layout)" from `BaseTheme.cpp:341`. Describe both modes by what they do, not by which theme uses them. Add `src/components/themes/BaseTheme.h` to the Step 3.4 `git add`. It is already on a FILES line (plan:8).

### MINOR 3 — Step 2.1 gives two contradictory include positions

- **Claim.** plan:229-236 says "add the include after `#include "components/UITheme.h"`", then says "put it on the line above".
- **Problem.** An implementer following the plan literally hits a contradiction. The second instruction is the correct one: `ListRowHeight` sorts before `UITheme` (`UiListActivity.cpp:10`). Step 2.3 already states it correctly.
- **Fix.** Replace the first sentence with "add the include on the line above `#include "components/UITheme.h"`" and delete the sorting note.

### MINOR 4 — nothing checks that `test/CMakeLists.txt` was never committed

- **Claim.** Step 10.4 (plan:999-1003) runs `git checkout -- test/CMakeLists.txt` followed by `git status --short`, expecting a clean tree.
- **Problem.** If the local line was ever committed (a stray `git add`, or a `style:` commit in Step 10.2 made carelessly), `git checkout --` restores the committed version from `HEAD`. `git status` then prints nothing and the check passes. This exact failure was a MAJOR in an earlier plan.
- **Evidence.** `docs/superpowers/plans/2026-09-17-issue-58-plan.md:1364`, whose fix added `git log --oneline origin/main..HEAD -- test/CMakeLists.txt`.
- **Fix.** In Step 10.4, before the push, add `git log --oneline main..HEAD -- test/CMakeLists.txt`, expected to print nothing.

VERDICT: CLEAR
