Tier: standard

# Plan review 0: btrial-2 (tag chip states and the pure chip helpers)

Plan: `docs/superpowers/plans/2026-10-12-btrial-2-plan.md`
Spec: `docs/superpowers/specs/2026-10-12-btrial-2-design.md`

I read the plan, the spec and the sources it edits (`TagChipRow.h`, `TagChipView.{h,cpp}`,
`HighlightsActivity.cpp:130-180, 690-715`, `TagFilterActivity.cpp:90-106, 161, 290`,
`TagChipRowTest.cpp`, `test/tag_rows/CMakeLists.txt`, `english.yaml:373`, `.gitignore`). I did not
apply or build it.

## Findings

No BLOCKERs and no MAJORs.

### MINOR 1: Step 6.3 stages every modified file, so a format change outside the lock gets committed

- **Claim.** Step 6.3 says the format pass should change only files the plan declares, then commits
  with `git add -u` (plan:557-566).
- **Problem.** `./bin/clang-format-fix` runs over the whole tree. If it changes a file outside the
  `FILES:` lines (plan:8-10), the plan says nothing about stopping, and `git add -u` commits it. That
  commits a file the lock does not cover, with nothing telling the implementer to report it.
- **Evidence.** plan:553-566. `git add -u` stages every tracked modification, not only the declared
  paths.
- **Fix.** Replace `git add -u` with the declared paths:
  `git add src/activities/reader/TagChipRow.h src/activities/reader/TagChipView.h src/activities/reader/TagChipView.cpp src/activities/reader/HighlightsActivity.cpp src/activities/reader/TagFilterActivity.cpp test/tag_rows/TagChipRowTest.cpp lib/I18n/translations/english.yaml`.
  Add before it: "If `git status --short` lists any other file, stop and report it; do not commit
  it."

### MINOR 2: the final report leaves out the spec's device checks, the only check of the new looks

- **Claim.** Step 6.4 reports commits, ctest, pio and warnings only (plan:570-574).
- **Problem.** No test in the plan exercises `draw`'s state table, the call-site mappings, or the
  A9 flash correction. No host target compiles `TagChipView.cpp` (plan:334-335; spec:259-260), so
  the firmware build shows only that the code compiles. The spec's device checks (spec:268-274) are
  the only check of those looks. CLAUDE.md ("What only the human tester can verify — flag these for
  the user") requires the hand-off. The plan never states them, so the report gives the tester
  nothing to run.
- **Evidence.** spec:268-274; plan:570-574.
- **Fix.** Append to Step 6.4: "List the owner's device checks from the spec's Testing strategy:
  the Highlights row looks as today and a tapped chip inverts while the old one goes white; the A9
  case (filter by X, untag X's last passage so X stays as a zero-count chip, tap a tag after X:
  only that tag inverts); the filter grid looks as today, with the current filter the only inverted
  chip."

## What I checked and found sound

- **Spec coverage.** Every spec item maps to a step. These are `Kind::NewTag` appended last,
  `ChipState`, `constexpr pickerState` with the §4 table, and the `findChip` template that reads only
  `.kind`/`.id` (Tasks 1-2). The four-state `draw` writes into `styles.normal` through a reference,
  so it adds no `BoxStyle` local. It copies `normal` to `selected/focused/active/disabled`. It keeps
  `StateSelected` only for Active, never sets `StateDisabled`, sets bold for Active and Action, and
  draws Disabled as `dither(LightGray)` with no border and width 0. That is the spec §2 table field
  for field (plan:366-417, spec:109-114). Task 4 also covers the `measure` comment rewording, the
  `NewTag` cases in `buildEntries` (`continue`) and `isSelected` (`false`), the two call-site
  mappings and the YAML key after `STR_TAG_NEW` (Tasks 1, 4, 5).
- **Tests against the spec list.** The pickerState cases at 7, 8 and after a deselect, NewTag at
  0/7/8 and the `static_assert` are all there. So are the findChip cases: by id, by kind, a retired
  tag, an empty list, Tag vs Unlabelled with UNLABELLED's id in both directions. The name-order
  `candidates` test uses real `TagChips::count` over `FakePassage`s, with zero-count HOPE as the
  filter in the middle. Its expected `perTag {2,0,1}` is right for the given passages.
- **`-Wswitch`.** The only switches over `TagChips::Kind` are the two in `TagChipView.cpp:27-40,
  46-53`, and the plan edits both in the same commit as the enum. Every other use is a `!=` test
  (`TagFilterActivity.cpp:161, 290`, `HighlightsActivity.cpp:156`), and `NewTag` cannot reach them.
- **Flash change beyond A9.** In Highlights, tapping the already-selected chip returns early with
  no rebuild (`HighlightsActivity.cpp:157-160`). `…` clears the flash (`:174`). So outside the A9
  case, a flashed chip is the one the tap just made Active, and it looks the same as today.
- **File lock.** All three `FILES:` lines are at column 0 outside code fences (plan:8-10). They
  cover every tracked file a step edits. `build/pio-btrial-2.log` is gitignored (`.gitignore:13`).
  The i18n headers that Step 5.2 regenerates are gitignored build outputs, and every `pio run`
  rewrites them anyway.
- **No push / PR / submit step.** Step 6.4 tells the implementer not to push.

VERDICT: CLEAR
