Tier: heavy

# PR #146 review: intent (pass 0)

**Scope:** does PR #146 (`refactor/102-remove-unreachable-fork-code`, 7f14a887) deliver issue #102 as
the spec (`docs/superpowers/specs/2026-09-27-issue-102-design.md`) and plan
(`docs/superpowers/plans/2026-09-27-issue-102-plan.md`) define it, with no silent scope change?
This pass did not run `pio`, because other workers hold the shared build lock. The build, flash and
cppcheck figures come from the PR body. Everything else was re-checked against the tree.

## Findings

None at BLOCKER, MAJOR or MINOR.

## Issue #102 acceptance, item by item

| Issue item | Where it is met | Status |
|---|---|---|
| Delete `HomeActivity` | files deleted; includes out of `ActivityManager.cpp:13-17` | met |
| Delete `RecentBooksActivity` and `goToRecentBooks` | files deleted; `ActivityManager.cpp` and `ActivityManager.h:87` | met |
| Delete `ButtonRemapActivity` | files deleted; include, `!hasTouch()` insert and dispatch case out of `SettingsActivity.cpp`; `RemapFrontButtons` out of `SettingsActivity.h` | met |
| Themes: Lyra, Lyra3Covers, RoundedRaff | Lyra3Covers and RoundedRaff deleted, and Lyra kept per decision d1 (see below) | met as corrected |
| `drawButtonMenu`, `drawRecentBookCover`, `getMenuRowHeight`, `drawList` | removed from `BaseTheme.{h,cpp}` and `LyraTheme.{h,cpp}`. `rg '\bdrawList\(|drawButtonMenu|drawRecentBookCover|getMenuRowHeight' src lib test` finds nothing | met |
| 11 `STR_CALIBRE_*` keys, all 32 languages | the `rg -c` gate over `lib/I18n/translations` finds nothing. `git diff --stat` shows 32 files, 942 deletions, 0 additions | met |
| `HighlightFile.h` include in `EpubReaderActivity.cpp` | `rg HighlightFile src/activities/reader/EpubReaderActivity.cpp` finds nothing | met |
| Keep `FileBrowserActivity` and `EndOfBookOptions` | both untouched | met |
| Old saved theme values still load | enum keeps all four constants (`CrossPointSettings.h:220`). The clamp falls back to the field default (`CrossPointSettings.cpp:166-169`, default `LYRA` at `CrossPointSettings.h:317`) | met |
| Regenerate i18n | the generated headers are gitignored and rebuilt by `pio run`. The PR reports `gen_i18n.py --verbose`: unused keys went from 12 to 1, with no new orphans | met |
| `pio run` succeeds; flash delta in the PR | the PR body has a table: Flash 5,557,906 → 5,525,722 B (−32,184 B) and `firmware.bin` −32,192 B, with a baseline that matches spec step 0 | met (per PR; not re-run here) |

## Decision d1: carried out and explained

- **Lyra stays and is still the default.** `LyraTheme.{h,cpp}` remain, and `uiTheme = LYRA` is
  unchanged (`src/CrossPointSettings.h:317`).
- **The two retired themes are deleted.** Lyra3Covers and RoundedRaff are gone from the tree.
  `rg 'Lyra3Covers|RoundedRaff' src lib` finds nothing. The only hits anywhere are in
  `freeink-sdk/`, which is outside this repo's scope.
- **All four `UI_THEME` constants stay** (`CrossPointSettings.h:220`). A comment at `:217-219`
  explains why.
- **`setTheme` maps the retired values to Lyra** (`src/components/UITheme.cpp:36-44`).
  `LYRA_3_COVERS`, `ROUNDEDRAFF` and `default:` all fall through to `LyraTheme`/`LyraMetrics`, so
  `currentTheme` is never left null.
- **The options list has two entries** (`src/SettingsList.h:273-274`). That makes the theme row
  a toggle (`SettingsActivity.cpp:261,275`), as spec A2 says.
- **The PR body explains the correction.** Its "Correction to the issue: Lyra stays" section gives
  the reason (Lyra is the default and renders every live screen) and cites d1. Its "Older settings
  still load" section describes the fallback and what users will see.

## Spec requirements beyond the issue

- **A3/A4:** `getCoverThumbPath`, `drawEmptyRecents` and the eight metrics fields are removed.
  - The metrics gate finds nothing.
  - The `LyraTheme.cpp` file-local symbols and the icon/`RecentBooksStore.h` includes are gone
    (`LyraTheme.cpp:12-25`).
  - `rg 'iconForName|mainMenuIconSize|listIconSize|mainMenuColumns|coverWidth' src/components`
    finds nothing.
  - `tabPillFullSlot` stays, as A4 requires (`BaseTheme.h:66`; still read at
    `UiTabListActivity.cpp:134,164,183`).
- **A5:** 31 keys come out, which matches the table (11 + 1 Hebrew + 17 + 2). The removal is
  deletions only, and the gate regex finds nothing in any YAML.
- **A6, A7 and the non-goals are respected.**
  - `HomeMenuItem`, `src/main.cpp`, `RecentBooksStore`, `HighlightFile::save`, the `GfxRenderer`
    region functions and the `frontButton*` fields are untouched. Only the comments on the region
    functions and `frontButton*` fields changed.
  - `isHomeActivity` is intact at `Activity.h:53`, `ActivityManager.cpp:75` and
    `LauncherActivity.h:32`.
- **A10:** `USER_GUIDE.md:263` now reads "Classic or Lyra".
- **Stale comments:** every spec item is reworded for the merged state:
  - `GfxRenderer.h:379`
  - `MappedInputManager.cpp:390`
  - `CrossPointSettings.cpp:87,188`
  - `BaseTheme.h:46,52,60,67`
  - `UiTabListActivity.{h,cpp}`

## Scope

- **No silent reduction.** Every item the issue and the spec list is removed or explicitly
  deferred with a reason: `HomeMenuItem` (A6), `HighlightFile::save` (A7) and `USER_GUIDE.md` §4
  (non-goal). The PR body repeats each of these.
- **No expansion.** No code is added beyond the `setTheme` fallthrough that d1 requires.
  `docs/contributing/touch-and-ui.md` was changed because it linked a deleted file. That change is
  plan step 2.6, from plan review MINOR 2, so it is planned.

## Divergence from the plan

None found.
- **Commits match the plan.** The six code commits follow Tasks 1–6 in order, with the plan's
  commit messages.
- **The skipped format commit is expected.** Task 7's commit was conditional on
  `clang-format-fix` changing something, and the PR reports the tree clean.
- **No includes came back.** Task 8.1 allowed restoring an include if the build needed it.
  `UITheme.cpp`, `BaseTheme.cpp` and `LyraTheme.cpp` all ship without `RecentBooksStore.h`, so
  none was restored.
- **The measured line count matches the plan.** YAML deletions are 942, the figure the plan
  predicted in 6.1.

## Tests

No host test is added. This is spec decision A9, and it holds:
- **What is under test is absence**, which the compiler and the completeness gates assert.
- **The one behavioural claim is checked on device.** Stored 2 or 3 loading as Lyra goes through
  the generic enum clamp, which has no host harness. The claim is in the device-tester checklist
  in the PR (steps 1–2).
- **No test restates the implementation.**

VERDICT: CLEAR
