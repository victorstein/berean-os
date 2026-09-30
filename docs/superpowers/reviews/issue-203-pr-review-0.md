Tier: standard

# PR #223 review 0: Home redesign for a reference Bible (issue #203)

Reviewed `main...cbe6d263` (`feature/203-home-redesign`) against issue #203, the spec
(`docs/superpowers/specs/2026-09-30-issue-203-design.md`) and the plan
(`docs/superpowers/plans/2026-09-30-issue-203-plan.md`).

What I checked independently, in a scratch worktree at `/private/tmp` that has since been removed:
- **Host tests.** I built `HomeLayoutTest` and `StudySleepPickTest`, with `add_subdirectory(home_layout)` added locally. `HomeLayoutTest` passed 33 of 33 and `StudySleepPickTest` passed 56 of 56.
- **Firmware build.** `pio run -e x4pro` gave `SUCCESS`, with no warnings from `src/` or `lib/`.
- **Plan match.** A script compared each changed `src/` and `test/` file with the plan's code block for it. All 18 are byte-identical, so the code does not depart from the plan anywhere.

## Intent

**Acceptance criteria.**

- **No Bible progress on Home: met.**
  - Nothing on Home reads Bible progress.
  - The only percentage is the workbook's, from `readBookProgressPercent(workbookPath)` (`LauncherActivity.cpp:194-195`). The issue explicitly allows it.
- **Each target lands on the right screen: met on the host; the landing itself needs the device.**
  - `HomeTargets::route` (`HomeTargets.h:49-79`) gives:
    - Continue: OpenAt with a place and None without one, both with `fast=true`.
    - Go to, Tags and Search: BookGrid, Tags and Search.
    - Recent and the verse card: OpenAt.
    - With no Bible, the reader targets route to Download.
  - `HomeTargetsTest.cpp` covers each of these routes.
  - `openReader` (`LauncherActivity.cpp:429-445`) builds each intent:
    - Continue and Recent use `ReaderEntryIntent::openAt(Place)`.
    - The verse card carries `pick.start` and `pick.spine` (A12, the review-0 MAJOR 2 fix).
- **Paint first, then fill; later entries don't re-scan: met, per decision d1.**
  - While the verse is pending, `onEnter` skips `requestUpdate()` (`:83`).
  - `loop()` then paints with `requestUpdateAndWait()`, scans, and repaints (`:471-478`).
  - `HomeVerse::isCurrent` keys the cache on the date and `bible.json`'s size (`HomeVerse.cpp:39-62`), so later entries within a wake do not scan.
  - The date seed makes each wake of the same day pick the same verse. This is the spec's reading of "once per day" (A6, d1), and it is stated openly.
- **Heap logged: met.**
  - `logMemory` logs internal free, the largest block and PSRAM free at `LOG_INF` (`:257-262`).
  - It runs on entry and after the verse fills.
- **Clean-entry refresh: unchanged.** `launcherNeedsCleanPaint` still drives the first paint (`:397-401`).

**Spec coverage.** Each spec requirement is implemented in full:

| Spec item | Where it is implemented |
|---|---|
| A1–A3: Continue, the no-places case, and the Recent rows | `LauncherActivity.cpp:139-151`, `pickRecent` into three fixed slots |
| A4: `bible.json` only | `HomeVerse.cpp:28-30` |
| A5: floor-rung gate, the 512-byte prefilter, the watchdog every 32 rows | `HomeVerse.cpp:82-91`, `StudyPassageScan.cpp:35,103,133` |
| A6 and A7: `dailySeed` and `splitmixDraw` | `StudySleepPick.h` |
| A6: the fixed fallback seed when there is no date | `HomeVerse.cpp:68-73` |
| A6: the sleep ring is never touched | `pickFromFile` passes `nullptr` as the ring |
| A8: empty results cached, OOM results not | `HomeVerse.cpp:102-115` |
| A11: two hints | `LauncherActivity.cpp:323-327` |
| A13: Meetings card with `LibraryIcon`, the ISO week, a strip marking today only with local time, the range, and the % | `LauncherActivity.cpp:163-196, 342-378` |
| A14: icon row, with `FolderIcon` for Publications | `:392-395` |
| A15: no Bible gives a single Download button | `:294-297` |
| A16: header on the plate, and the Masthead-style fallback | `:277-292`; the fallback matches `Masthead.cpp:89` |
| A17 and A21: geometry | `HomeLayout.h`, pinned in `HomeLayoutTest.cpp` |
| A18: ring and selection styles | `HomeTargets::ring`, `drawButton` |
| A19a: per-target refresh flag | `HomeTargets.h:40-54` |

**Scope.** The PR neither drops nor adds scope.
- Four changes go beyond the issue text. The spec and the plan's "Deviations" section name and justify each one:
  - `formatDate` is now exported from the sleep screen;
  - the sleep screen's scan now resets the watchdog;
  - dead Meetings-cover code is removed;
  - the YAML keys are committed in this branch rather than handed off.
- The PR body lists the hand-offs: the `test/CMakeLists.txt` line and the four committed keys.

**Plan divergence.** There is none. The code matches the plan byte for byte. The plan's six "Deviations from the spec" are its own, and the PR body points to them.

**Tests.** They exercise behaviour rather than restating the implementation:
- The same seed gives the same pick.
- The date seed is measured over 30 days for spread and for adjacent-day independence.
- A pick carries its `start` and `spine` through `Sampler::fill`.
- The cache re-keys on the day, the file size and the file appearing, and an undated entry stays stable.
- `packLines` enforces its bounds.
- Every route is covered, and the ring order is checked for three states.
- Geometry is pinned against the real Lyra and Base metric tables.

The activity's glue, such as which place feeds which intent, is left to the device checklist. That is the repo's established boundary for activities.

## Quality

- **It follows existing patterns.**
  - `HomeLayout` follows `MastheadLayout` and `test/masthead`.
  - `HomeTargets` follows `LauncherRefresh.h` and `ReaderEntryIntent::route`.
  - The `HomeVerse` singleton follows `CatalogIndexStore::getInstance`.
  - The scan is moved out of the sleep screen (`StudySleepScreen.cpp`), not copied. The sleep screen now calls the shared scan through `pickFromAll`, with its ring passed in.
- **Resource rules are followed.**
  - `makeUniqueNoThrow` is used for the `Sampler` and `ScanBuffers`.
  - The JSON document is in PSRAM.
  - The ~612 B static `.bss` entry is justified (plan Deviation 2), and a `static_assert` holds it to 640 B or less.
  - Nothing is allocated per frame.
  - Render reads fixed buffers only.
- **Error handling matches the scan it was lifted from.** OOM logs `LOG_ERR`, is not cached, and falls back to the empty hint.
- **Naming, dead code and comments are clean.** Naming matches the sibling files. No commented-out code is left. The comments explain reasons, such as the "close before reopen" in `pickFromFile` and the missing `mappedInput.update()`, rather than restating the next line.
- **One small copy was introduced.** The lift left a second `hardwareRandom` (MINOR 2).

## Findings

1. **MINOR: the Spanish "too long" hint is always cut off.**
   - **Where:** `LauncherActivity.cpp:324-326`, with `spanish.yaml:432`.
   - **What happens:** the A11 hint is drawn through `drawTextIn`, which uses `truncatedText` to fit one line in the verse text box's width (448 px). `STR_HOME_TAGS_TOO_LONG` in Spanish ("Tus pasajes etiquetados son demasiado largos para mostrarse aquí") measures about 528 px in `notosans_8`, summed from the glyph advances in `notosans_8_regular.h`. So Spanish users always see it cut, with an ellipsis. English (about 380 px) and the Spanish empty hint (about 317 px) fit.
   - **Fixes:** the box is 102 px tall, so either of these works:
     - shorten the Spanish string, for example "Tus pasajes son muy largos para mostrarse aquí";
     - wrap the hint over two lines.
2. **MINOR: `hardwareRandom` now exists twice.**
   - **Where:** `StudyPassageScan.cpp:42` and `StudySleepScreen.cpp:97`, both in anonymous namespaces.
   - **What happens:** the lift copied the one-line helper instead of sharing it. The sleep screen still needs it for its `Sampler`, and the scan needs it for its random start.
   - **Fix:** declare it once, in `StudyPassageScan.h` (or `StudySleepPick.h` behind the Arduino boundary), and use that in both places.

Neither finding changes behaviour, scope or a decision. Both can be fixed inline.

VERDICT: CLEAR
