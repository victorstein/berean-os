Tier: heavy

# PR #146 review: code quality (pass 0)

**Scope:** code quality of `refactor/102-remove-unreachable-fork-code` at `bc436e76`, diffed
against `main`. Decision d1 (Lyra stays) is treated as settled. I did not run `pio`, because
other workers hold the shared build lock. Every claim below comes from reading the tree, `git show
main:` and `rg`.

## Findings

### MAJOR 1: the PR leaves 11 icon headers with no includers

The spec's LyraTheme row (`2026-09-27-issue-102-design.md:234`) removed the 32 px and 24 px icon
includes from `LyraTheme.cpp`. On `main`, `LyraTheme.cpp:16-30` was the only file in `src`/`lib`
that included 11 of those headers. After the PR, nothing includes them:

- `src/components/icons/book24.h`
- `src/components/icons/cover.h` (Lyra3Covers and RoundedRaff also included it; both are deleted)
- `src/components/icons/file24.h`
- `src/components/icons/folder.h`
- `src/components/icons/folder24.h`
- `src/components/icons/hotspot.h`
- `src/components/icons/image24.h`
- `src/components/icons/recent.h`
- `src/components/icons/text24.h`
- `src/components/icons/transfer.h`
- `src/components/icons/wifi.h`

To check: `git grep -l 'icons/<name>"' main -- src lib` lists only `LyraTheme.cpp` (plus the two
deleted themes, for `cover.h`). The same search on HEAD finds nothing, and outside
`docs/superpowers/` and `freeink-sdk/`, the only references to their symbols are the files'
own definitions.

The files cost no flash, because an untouched header is never compiled. But this PR exists to
remove unreachable fork code, and it creates 11 new dead files. A future reader will assume that
`WifiIcon` or `RecentIcon` is drawn somewhere. `search24.h` and `search32.h` had no includers on
`main` either, so they are the same class of file and can go in the same commit.

**Fix inline:** `git rm` those 11 files, plus the two search icons if you choose. The build
confirms it.

This does not change scope or reverse any decision. The issue asks for this class of removal,
and the spec already removed these files' only includes.

### MINOR 1: two includes lost their last user

- `#include <HalStorage.h>` at `src/components/themes/BaseTheme.cpp:8` and
  `src/components/themes/lyra/LyraTheme.cpp:6`. On `main`, the only `Storage.` uses in these
  files were the cover-bitmap reads in the deleted methods
  (`main:BaseTheme.cpp:587,637` and `main:LyraTheme.cpp:422`, all `openFileForRead("HOME", …)`).
  HEAD has no `Storage`, `HalFile` or `FsFile` use in either file. The spec applied the rule
  "drop an include if the build confirms nothing else needs it" to `RecentBooksStore.h`, but not
  to this one.
- `#include <functional>` at `src/components/themes/BaseTheme.h:5`. Every `std::function` in
  this header belonged to `drawList`, `drawButtonMenu` or `drawRecentBookCover`, and all three
  are removed. Remove it only if the build agrees, because other files may rely on it
  transitively.

### MINOR 2: half of the `getPressedFrontButton` comment was reworded

`src/MappedInputManager.cpp:378-379` still begins "The raw front-button scan for the remap flow.
Deliberately unmapped: the remap activity needs physical presses…". The PR reworded only the
last paragraph of this same block (`:389-390`). The remap activity is gone, and
`getPressedFrontButton` now has no callers (`rg getPressedFrontButton src lib test` finds only
the declaration and the definition).

The function stays, because the input layer is a non-goal. Reword the opening line for the
merged state, e.g. "A raw, unmapped front-button scan for flows that need physical presses, not
logical roles." That matches how the PR already handled the `frontButton*` comments at
`CrossPointSettings.cpp:87,188`.

### MINOR 3: home-screen constants that were already dead, in a file this PR edits

`src/components/themes/BaseTheme.cpp:25-27` defines `homeMenuMargin`, `homeMarginTop` and
`subtitleY`. Neither `main` nor HEAD reads them anywhere. They are home-screen residue from the
same fork, and this PR has the anonymous namespace open. Delete the three lines. This is
optional, since the PR did not create them.

### MINOR 4: a stray blank line

`src/components/themes/lyra/LyraTheme.cpp:24` adds an empty line before `}  // namespace`.
None of the siblings has one; for example, see `BaseTheme.cpp`'s namespace close.

## What holds up

- **The `setTheme` fallthrough** (`src/components/UITheme.cpp:34-43`) follows the existing
  switch shape. It groups `LYRA`, the two retired constants and `default:` into one arm instead
  of adding a second fallback mechanism. Its comment explains why, which is that `currentTheme`
  must never be null, rather than repeating the code.
- **The enum comment** (`src/CrossPointSettings.h:217-219`) is accurate against the clamp. The
  generic clamp at `CrossPointSettings.cpp:166-169` uses `enumValues.size()` (now 2) and falls
  back to the field default, `LYRA`.
- **Comment rewording follows the merged-state rule.** `BaseTheme.h:45,51,59,64-65`,
  `UiTabListActivity.{h,cpp}` and `GfxRenderer.h:379-380` now name the metric or mechanism, not
  a deleted theme or screen. `GfxRenderer.h`'s new wording matches its one live caller
  (`SleepActivity.cpp:460-473`, which saves a band).
- **Removals are complete at the symbol level.**
  - Nothing references `goToRecentBooks`, `RemapFrontButtons`, `STR_REMAP_*`,
    `getCoverThumbPath`, `drawEmptyRecents`, or the A4 metrics fields.
  - `RecentBooksStore.h` is out of the theme and `UITheme` includes.
  - The `struct RecentBook` forward declaration is gone from `BaseTheme.h`.
- **Kept items are intentional and still read by live code.** `tabPillFullSlot` is now false
  for every theme, but spec A4 keeps it as theme data, and `UiTabListActivity.cpp:134,164,183`
  still reads it. `listTitleBold` is read at `UIThemeTokens.h:38` and in two reader
  activities.
- **Tests:** none are added, which matches spec A9. What changes is absence, and the compiler
  enforces absence. No test was written only to be present.

## Verdict rationale

The only MAJOR is one mechanical `git rm` that finishes what the PR set out to do. It changes
no decision and no scope, and needs no human judgment. The MINORs are include hygiene and
comment accuracy. All of them can be fixed inline.

VERDICT: CLEAR
