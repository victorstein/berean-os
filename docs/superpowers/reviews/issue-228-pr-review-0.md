Tier: light

# PR #229 review, pass 0: no whole-Bible progress in the reader status bar (#228)

Reviewed against `origin/main` (`e3e5c94e`). The local `main` ref is stale, so `git diff main...HEAD`
also shows the 1.28.1 release files. Against `origin/main` the PR touches 11 files, and every one of
them is in scope.

## Intent

Acceptance criteria from issue #228:

- **A Bible never shows the book percentage, and a `BOOK_PROGRESS` bar shows chapter progress.** Met.
  `EpubReaderActivity.cpp:1824-1826` passes `!isBible`, using the same Bible test that
  `readerMenuTitle()` uses at `:1788`. In `BaseTheme.cpp`, all three percent reads in the text block
  now go through `progressView.showBookPercent`. The bar's `BOOK_PROGRESS` test now reads
  `progressView.barTracksBook`, so a Bible falls into the unchanged chapter `else` branch. That is
  "exactly as `CHAPTER_PROGRESS` does", as the issue asks.
- **Non-Bible output is unchanged for every combination.** Met. With `showWholeBookProgress = true`,
  `resolve` is the identity on both bools (`StatusBarProgress.h:20-22`). `CHAPTER_PROGRESS` and
  `HIDE_PROGRESS` both map to `barTracksBook = false`, which already selected the chapter branch
  before this change. Whether a bar is drawn at all still comes from `sb.showsProgressBar()`, which
  the PR does not touch. The Settings preview call (`StatusBarSettingsActivity.cpp:279`) picks up
  the `true` default.
- **A host test covers the Bible and non-Bible cases.** Met. `StatusBarProgressTest.cpp` sweeps all
  four setting combinations for each case, and adds a `static_assert`.
- **Build, format and `pio check`.** The PR reports all three as clean. I did not re-run them.
- **Device checks.** These are listed for the human in the PR body.

Spec requirements are fully implemented:

- The rule is one pure `constexpr` function.
- The theme stays Bible-agnostic behind a trailing defaulted flag.
- `Inputs` is built with designated initialisers, as spec-review MINOR 2 asked.
- `test/CMakeLists.txt` is untouched.
- `USER_GUIDE.md` is updated at the "Customise status bar" bullet (issue Change 4).

There is no silent scope reduction. The spec names two limits and does not hide them: the empty
text lane in A4 (a Bible whose only enabled text item is the book percentage), and the
screenshot-filename `%dpct` percentage. The PR body repeats both. There is no scope expansion
either: no setting, default or format change.

The code matches the plan exactly. The plan's `resolve` uses designated initialisers for `View`,
while the spec's sketch uses positional ones. The implementation follows the plan, and the two
are equivalent. The plan itself is sound. Reducing the bar enum to one bool (A5) keeps the enum out
of the firmware-free header without restating it, and cannot change the other two modes.

The tests exercise the rule's full truth table, not a sample, so they check behaviour rather than
restating the implementation. They do not cover the theme wiring, but the issue asks only for the
decision function to be host-tested, and the wiring is a mechanical substitution that is covered by
the device checks.

## Quality

- The new code mirrors `src/components/ListRowHeight.h` and `ListRowHeightTest.cpp`:
  - same header shape, namespace and `Inputs`/`resolve` naming;
  - same "free of … so the host suite can exercise it" header comment;
  - a test helper that builds `Inputs`;
  - CMake registration that copies the `ListRowHeightTest` block line for line.
- There is no second way of doing an existing thing, and no duplicated logic.
- The comments on `Inputs` and `View` fields state their source or meaning (for example "false in a
  Bible: a reference, not a book to finish"). None restates the next line.
- There is no dead or commented-out code.
- The only Bible check is in the activity, as the issue required.
- The code allocates nothing, has no failure path, and adds no new error handling. That is correct
  for a pure function.

## Findings

None.

VERDICT: CLEAR
