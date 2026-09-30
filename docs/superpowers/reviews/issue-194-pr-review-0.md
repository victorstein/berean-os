Tier: standard

# PR #197 review 0 — compact touch metrics and no Bible progress indicators

Reviewed `origin/main...HEAD` on `feature/194-compact-metrics-no-progress` (head `8a7e7c16`) against
issue #194, the spec `docs/superpowers/specs/2026-09-29-issue-194-design.md` and the plan
`docs/superpowers/plans/2026-09-29-issue-194-plan.md`. No build was run. The evidence comes from
reading the code, grep and `git diff`.

## Intent

**Part A, header.** `LyraTheme.h:12-13,33` now sets `batteryBarHeight = 44`, `headerHeight = 44` and
`headerBatteryDetached = false`, so the band is 5 + 44 = 49 px. Every `GUI.drawHeader` call site in
`src/` passes `metrics.headerHeight` as the rect height (25 sites, grep `drawHeader(`), so
`batteryBarHeight < rect.height` (`BaseTheme.cpp:334`) is false for Lyra everywhere and true for
Classic (20 < 45), as D2 intends. The battery is centred in a 44 px rect at `band.y`
(`BaseTheme.cpp:386-387`), and the header component centres the title in the same 44 px content
height (`header.h:136-144`), so the two share a line. The subtitle goes to `props.rightLabel`, which
the component bottom-aligns to the title line inside the reserved content width (`header.h:138-146`).
The issue's hand-offset check is covered: the research note shows that `topHintButtonY`
(`LyraTheme.cpp:21`) is not header-relative and never draws on touch, and it tables the screens
that add their own offsets.

**Part A, spacing and rows.** `verticalSpacing` changes from 16 to 8. The new
`ThemeMetrics::touchListRowHeight` (`BaseTheme.h:40-42`) has a default member initializer, so Classic
omits it. `ListRowHeight::resolve` (`src/components/ListRowHeight.h:18-27`) implements A-7 exactly,
and both sync functions now always write `props.rowHeight` (`UiListActivity.cpp:125-138`,
`UiTabListActivity.cpp:77-89`), which closes the paging and drawing mismatch that A-8 names. The
theme's `rowHeight` token is untouched, so buttons and sliders keep their size. No freeink-sdk file
changed. I re-audited every `sync*Viewport` caller against `subtitle` usage in its file. Every file
that sets subtitles passes `hasSubtitle=true`, `PublicationsActivity.cpp:174` included. Every
one-line caller has zero `subtitle` references. The rows-per-page arithmetic in the PR body
(14 on Settings and 14 on the reader menu, against 9 before) follows from the stated metrics, and the
count itself is correctly left as a device check.

**Part B.**
- **Home:** `applyChaptersReadSubtitle`, its declaration and doc comment, its call and the
  `ChapterCompletionFile.h` include are all gone. The tile keeps `bibleTitleFor`
  (`LauncherActivity.cpp:117`).
- **Reader menu:** the band and its three constructor parameters and members are gone
  (`EpubReaderMenuActivity.cpp:18-26`, `.h:33-35`), and the spacer stays (A-14). `readerMenuTitle()`
  (`EpubReaderActivity.cpp:521-528`) applies A-11 and A-12 with no inflate. The status bar now shares
  `bibleReference()` through `currentBibleChapter()` under the same spine guard as before, so its
  behaviour is unchanged.
- **Sleep:** the strip, its loader, its constants, its chrome term, its draw, its load, its include
  and `Layout::left` are gone. `main` rewrote this file with the rung ladder (#193), which the plan
  predates. The PR body explains the resulting divergence: two rungs collapsed to identical
  `{SERIF_12,{true,true}}`, so one was dropped. That is the correct reading of the ladder
  (`StudySleepScreen.cpp:81-84`), and `RUNG_COUNT` and `FLOOR_RUNG` are derived, so nothing else
  depends on the count.
- **Kept:** Meetings progress and the chapter-completion store and its writers are untouched, as
  the issue requires.
- **Translation keys:** `git diff --numstat -- lib/I18n` shows 0 lines added and 98 deleted, and the
  word-anchored grep over `src lib test` returns nothing. This matches decision d1's conditions.
- **`USER_GUIDE.md`:** it is updated at the three places the spec names. Line 451 describes the kept
  store and correctly stays.

**Tests.** The seven `ListRowHeightTest` cases cover every branch of A-7, including the clamp up to
`minTouchSize`, the token fallback when the field is unset, and the fallback to the dense row and
then to 1. They drive the rule through inputs rather than restating constants.
`BibleReferenceTest` covers a known chapter, an unknown one (0 and -1) and a UTF-8 book name.
`readerMenuTitle()`'s Epub-dependent branches are not host-testable, which matches D5's scope, and
device check 3 covers them. The `test/CMakeLists.txt` hand-off is documented in the PR body, per
A-19.

**Scope.** Nothing beyond the issue changed. The Publications non-touch row change is the declared
A-9 exception.

## Quality

- **Existing patterns reused.** The pure helpers follow the `NumberGridLayout.h` precedent (D5). The
  new metrics field copies `tabPillFullSlot`'s default-initializer idiom. The row-height rule is one
  function shared by both sync paths, so the old logic is not duplicated. Classic's existing
  shared-line path is reused for the header instead of adding a new layout.
- **Comments.** Comments on the edited code are rewritten for the merged state. The spec's MINOR 8
  items (`BaseTheme.cpp` battery comment and `LauncherActivity.h` doc comment) are done. Two
  file-level comments that describe the removed features were missed (findings 1 and 2).
- **Dead code.** No code the PR touched is left commented out or dead. Because Lyra was the only
  detached theme, the `batteryDetached` branch in `drawHeader` (`BaseTheme.cpp:345-353`) is now
  unused by any shipped theme. It is still a live `ThemeMetrics` option, and the spec deliberately
  kept `drawHeader` otherwise unchanged, so I do not raise it as a finding.
- **Error handling.** Nothing new can fail. The removed completion reads take their `LOG_ERR`s with
  them, and the title helper degrades to the publication title as the spec states.
- **Naming.** Names are consistent with their siblings: `ListRowHeight::resolve`,
  `touchListRowHeight`, `currentBibleChapter`, `readerMenuTitle`.

## Findings

1. **MINOR** — `src/activities/launcher/LauncherActivity.h:21-23`. The class comment still lists
   "chapters read -- counted from the completion record itself each time the launcher opens" as live
   state the tiles carry. After this PR the launcher never reads the completion record. Drop that
   clause and keep the meeting-week example. Fix inline.
2. **MINOR** — `src/activities/boot_sleep/StudySleepScreen.h:5`. The file comment says the sleep
   image is "One of the user's tagged passages, the date and the Bible progress strip". The strip is
   gone. It should read the passage, the date, the reference and the tag, which matches the updated
   `USER_GUIDE.md` row. Fix inline.

VERDICT: CLEAR
