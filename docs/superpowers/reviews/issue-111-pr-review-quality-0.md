# PR #118 review — code quality

Scope: `gh pr diff 118`, code only (`lib/`, `src/`, `test/`). The docs under
`docs/superpowers/` were not reviewed for quality.

## Summary

The change is a clean, mechanical consolidation, and it copies an existing pattern. `SdPaths.h`
follows `lib/Serialization/SaveBudget.h` closely: a lower-case namespace (`persist` → `sdpaths`),
`inline constexpr` constants, and one `constexpr bool` helper (`SaveBudget.h:15-26` vs
`SdPaths.h:8-64`). `test/sd_paths/CMakeLists.txt` is `test/save_budget/CMakeLists.txt` with the
names changed. Nothing adds a second way of doing something:

- Every call site that used to build a child path from a file-local constant now builds it the
  same way from the shared one, e.g. `PassageFile.cpp:54`, `ChapterCompletionFile.cpp:18`,
  `UnitIndexCache.cpp:48` and `CatalogIndexStore.cpp:51-53`.
- The `mkdir` calls and error handling are unchanged in shape (`CatalogIndexStore.cpp:206-208`).
- The store-owned aliases (`PubKeyRegistry::PATH`, `TagPaletteFile::PATH`,
  `MeetingWeekCache::PATH`, `MigrationRunner::REPORT_PATH`/`LEDGER_PATH`, and
  `BibleSearchStore::SEARCH_DIR`/`INDEX_PATH`/`CHECKPOINT_PATH`) keep their names and types. Only
  the initialiser changed, so no caller had to change.

The sweep is complete. Outside `SdPaths.h`, the only literal left in `src`/`lib` is
`src/main.cpp:227`, and the PR hands that line to the orchestrator.

No dead code, no commented-out code, and no new narrating comments. The file-local
`BEREAN_DIR`/`STUDY_DIR`/`LEGACY_DIR`/`PASSAGES_DIR`/`UNITS_DIR`/`COMPLETION_DIR` definitions are
removed, not left orphaned.

The tests are designed well, not just present:

- They pin the on-card strings by hand, with the reason stated (`SdPathsTest.cpp:5-7`), rather than
  deriving them from the header they test.
- The `isUnder` cases cover the edges that matter: the root itself, an empty tail, and a sibling
  that shares the prefix (`SdPathsTest.cpp:53-64`).

## Findings

### MINOR

1. **The header comment claims more than the header covers.** `lib/Serialization/SdPaths.h:36`
   (diff line; file line 5) says "Every fixed path this firmware reads or writes on the SD card".
   The firmware still has fixed SD paths outside both roots:
   - `/crash_report.txt` (`lib/hal/HalSystem.cpp:104`)
   - `/sleep.bmp` (`src/activities/boot_sleep/SleepActivity.cpp:556`)
   - `/.sleep-overlay` (`SleepActivity.cpp:36`)
   - `/read` (`src/activities/reader/EpubReaderActivity.cpp:78`)
   - `/screenshots/...` (`src/util/ScreenshotUtil.cpp:20`)

   Leaving these out is correct, because the issue is about the two firmware-owned roots. The
   comment should say that, e.g. "Every fixed path under the firmware's two SD roots". A comment
   that overstates its coverage invites the next person to trust it.

2. **Include style in `PersistableStore.cpp` is inconsistent.** `lib/Serialization/PersistableStore.cpp:11`
   adds `#include "SdPaths.h"` as its own quoted block. The same file includes its other
   same-directory header angle-bracketed, in the main block: `#include <ObfuscationUtils.h>` (`:5`).
   Every other call site in the PR uses `<SdPaths.h>`. Move it to `<SdPaths.h>` beside
   `<ObfuscationUtils.h>`.

3. **The new test is not wired into the build by this diff.** `test/sd_paths/` is never built,
   because `test/CMakeLists.txt:101` (`add_subdirectory(save_budget)`) has no `sd_paths` sibling.
   Until that line lands, `SdPathsTest` is dead in CI. The PR body says so and gives the line to
   the orchestrator under the shared-file protocol, so this is a merge-gate item, not a defect in
   the work. It is recorded here so it is not lost.

No BLOCKER or MAJOR findings. All three MINORs can be fixed inline and none reverses a decision.

VERDICT: CLEAR
