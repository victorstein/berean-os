Tier: heavy

# Issue #102 plan review, pass 0

**Plan:** `docs/superpowers/plans/2026-09-27-issue-102-plan.md`
**Spec:** `docs/superpowers/specs/2026-09-27-issue-102-design.md` (Design v2)
**Tree:** `ff6a12fc` (worktree `refactor/102-remove-unreachable-fork-code`)

Decision d1 is settled and was not re-examined. The review checked that the plan
carries it out: `setTheme` maps `LYRA_3_COVERS`/`ROUNDEDRAFF` to Lyra, all four
enumerators stay, and Lyra is still the default.

## How the plan was checked

I executed the plan mechanically in a scratch copy (`git archive HEAD` under
`$TMPDIR`) instead of only reading it:

- **Every Old → New block** (1.2–5.1, 38 blocks) was parsed from the plan and
  applied by exact string replacement. Each `old` text matched exactly once,
  except 1.7, which matched twice as the plan says. None of them had drifted.
- **The five anchored cuts** (4.7, 4.11, 4.12) ran through the plan's own `cut.py`.
  Each anchor matched once. The seams are as the plan says: `}` + blank line
  before `drawHeader`/`drawPopup`; the namespace keeps four constants, one blank
  line, then `}  // namespace`; `LyraTheme.cpp` ends with `drawSideButtonHints`'s
  `}` and one `\n`. Removed definitions at column 0 are exactly `drawList`,
  `drawRecentBookCover`, `getMenuRowHeight`, `drawButtonMenu` (BaseTheme) and
  `iconForName`, `drawList`, `drawRecentBookCover`, `drawEmptyRecents`,
  `drawButtonMenu` (Lyra). No live code sat inside a cut span.
- **The YAML filter (6.1)** removed `942 lines` across 32 files, all deletions,
  which matches the plan's expected count. The spec's A5 table adds up to the
  same number: 224+124+1+256+217+56+64.
- **The gen_i18n before/after (0.3, 6.2)**, run in scratch copies: the before set
  is the 12 keys the plan predicts, `comm -13` is empty, and the after set is
  only `STR_HIGHLIGHTS_TOO_LARGE`.
- **The final gate (`gate102.sh`)**: all seven checks are `ok` on the edited copy.
  In the real worktree, `rg` respects `.gitignore`, so the generated `I18n*`
  files are never searched (`rg -l RoundedRaff lib -g '!*.yaml'` → nothing).
- **Every `STR_*` token left in `src lib`** (comments included, because
  `gen_i18n.py` scans them) is present in `english.yaml`. The deletion removes
  nothing that is still used.
- **Residual dependencies.** No icon symbol from the removed `LyraTheme.cpp`
  includes is still referenced. The only icon use is `BookmarkStatusIcon` in
  `BaseTheme.cpp:37`, whose include stays. `RecentBook` no longer appears in
  `BaseTheme.h`, `LyraTheme.h`, `UITheme.cpp`, `BaseTheme.cpp` or `LyraTheme.cpp`.
  The only `ThemeMetrics` designated initialisers are `BaseTheme.h:124` and
  `LyraTheme.h:9`, and both are trimmed in field order. `SettingAction` is
  referenced only in `SettingsActivity.{h,cpp}`. No build filter, CMake file or
  host test names a deleted file.
- **FILES lines** (`plan.md:15-31`) are at column 0, comma-separated, and
  repo-relative. The pipeline's parser accepts that form (`gating.ts:34`,
  `gating.test.ts:118`). Every file a step edits, deletes or `git add`s is
  covered, including `roundedraff/` and `lib/I18n/translations/` as directory
  prefixes.

**Spec coverage:** every Goal, A1–A10, every Architecture row, every stale
comment, every gate in Testing strategy, the device checks, and the PR title
and body requirements map to a step. The plan's own table (`plan.md:1081-1095`)
is accurate.

Each commit is self-consistent. Task 3 drops `RecentBooksStore.h` from
`UITheme.cpp` while `getCoverThumbPath` still exists, but that function uses
only `std::string`. Tasks 1–3 leave the base virtuals in place, so the
overrides in the deleted themes are never orphaned.

## MINOR 1 — Gate 2.6's expected output is wrong, and an implementer following it literally will stop

**Claim.** `plan.md:317-318`: after Task 2,
`rg -n '\bHomeActivity\b|RecentBooksActivity|goToRecentBooks' src lib` "prints
only `BaseTheme.h:229`".

**Problem.** It also prints a RoundedRaff line. That file is deleted in Task 3,
but it is still present at the end of Task 2. An implementer told to expect one
hit will see two and has to decide on their own whether that is drift.

**Evidence.** `src/components/themes/roundedraff/RoundedRaffTheme.cpp:190`:
`const int rowHeight = getMenuRowHeight(renderer);  // shared with HomeActivity's touch grid`.
The `\b` after `HomeActivity` matches before `'s`.

**Fix.** Change 2.6 to: "prints only `BaseTheme.h:229` and
`RoundedRaffTheme.cpp:190` (both removed in Tasks 3–4)".

## MINOR 2 — `docs/contributing/touch-and-ui.md` is left with a broken link to a deleted file and a stale "sole user" claim

**Claim.** The spec's stale-reference sweep ("Stale comments to rewrite", A10)
and the plan's FILES list cover the in-tree references to the deleted screens.

**Problem.** A contributor guide links to one deleted file as a worked example.
It also names `HomeActivity` as the only user of two `MappedInputManager`
helpers. After this change those helpers have no caller at all, and the page
does not say so. Neither the spec nor the plan mentions the file.

**Evidence.**
- `docs/contributing/touch-and-ui.md:17` links
  `../../src/activities/home/RecentBooksActivity.cpp` as the long-press example.
- `docs/contributing/touch-and-ui.md:132` reads "`rowTouch` / `colTouch` … Sole
  remaining user: `HomeActivity`".
- `rg -n '\browTouch\b|\bcolTouch\b' src lib` finds callers only at
  `HomeActivity.cpp:209,234`. The declarations and definitions are
  `MappedInputManager.h:75,78` and `.cpp:183,202`, which stay because the input
  layer is out of scope.

**Fix.** Add `FILES: docs/contributing/touch-and-ui.md` and a step in Task 2:
- On line 17, drop the `RecentBooksActivity` link, or point it at a live
  long-press `UiListActivity` if there is one.
- On line 132, write "No remaining caller; kept until the input-layer
  replacement lands."

Documentation only, no scope change.

## Not raised

- **Per-task commits are not built individually.** Tasks 1–7 commit without a
  build, and Task 8 builds once. That follows the root `CLAUDE.md` rule to
  build once after the last edit, and the gate script stands in as the red/green
  test, as the spec's TDD note allows. I checked each intermediate commit for
  self-consistency by hand (above).
- **3.3 adds a `default:` label.** It sits alongside the four enumerators and
  directly serves d1's "never null".
- **Constants that were already unused.** `homeMenuMargin`, `homeMarginTop` and
  `subtitleY` in `BaseTheme.cpp:25-27` had no users before this change either,
  so the change does not orphan them. Out of scope.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
