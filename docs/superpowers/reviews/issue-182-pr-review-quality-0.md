Tier: heavy

# PR #183 — code quality review 0

Scope: `gh pr diff 183`, code and tests only (`lib/StudyStore`, `src/activities/boot_sleep`,
`src/activities/reader`, `src/study/StudyStore.cpp`, `test/`). The spec, plan and research documents
were not reviewed for quality.

## Summary

The change follows the patterns already in the files it touches. I found no BLOCKER or MAJOR
issues. Four MINOR findings follow, all fixable inline.

What mirrors existing patterns (checked, no finding):

- **`PassageDoc` v3.** `displayTextFromJson` (`lib/StudyStore/StudyStore/PassageDoc.cpp:85-96`)
  has the same shape as `linksFromJson`: refuse on a malformed field and never repair it, and the
  `fromJson` loop clears `passages_` on failure (`PassageDoc.cpp:272-275`).
  - `formatVersionFor` (`PassageDoc.cpp:106-110`) extends the old "lowest version that holds
    everything" ternary instead of adding a second mechanism.
  - The header comments state the refusal contract in the same way as the links cap
    (`PassageDoc.h:35-40` and `:47-51`).
- **The v2-reader refusal test** (`test/passage_doc/PassageDocTest.cpp`,
  `AVersionTwoReaderRefusesAFileCarryingIt`) copies the existing v1 test at
  `PassageDocTest.cpp:441-443`.
- **`setAnchor`** (`src/activities/reader/PassageSelectActivity.cpp:300-306`) merges two code paths
  that had drifted apart, which caused the long-press `anchorOffset` bug. It is a good consolidation.
- **`fitPassage`** (`src/activities/boot_sleep/StudySleepFit.h`) is new code and does not duplicate
  `GfxRenderer::wrappedText`:
  - `wrappedText` (`lib/GfxRenderer/GfxRenderer.h:299-303`) cuts mid-word with an ellipsis and has
    one fixed size.
  - The issue explicitly rejects that behaviour.
  - `fitPassage` breaks on the same characters (U+0020 only, `StudySleepFit.h:41-42`).
  - It follows the host-testable, callback-context shape of its sibling `StudySleepPick.h`: a free
    function pointer plus `ctx`, like `RandomFn`. It avoids `std::function`, as CLAUDE.md requires.
  - No other pure wrap helper exists in `src/` or `lib/` (grep for `splitWords` and `MeasureFn`).
- **Stack budget.** `Candidate` grew from 121 to 385 bytes of text, and `Sampler` holds two of
  them. It is still heap-allocated through `makeUniqueNoThrow`
  (`src/activities/boot_sleep/StudySleepScreen.cpp:376`), so it adds nothing to the stack.
- **`StudySleepFitTest.cpp` is well designed.**
  - It uses a deterministic px-per-byte measure, and each case documents its arithmetic.
  - It covers "largest size that fits, not the smallest", the ellipsis only at the floor on a word
    boundary, over-wide words, zero height, U+202F and empty input.

## Findings

### MINOR 1: two identical U+2026 constants added in the same PR

- `src/activities/reader/PassageLabel.h:10` adds `passage_label::ELLIPSIS = "\xE2\x80\xA6"`.
- `src/activities/boot_sleep/StudySleepFit.h:14` adds `study_sleep::FIT_ELLIPSIS = "\xE2\x80\xA6"`.

Both headers are pure and host-tested, so either could own the constant and the other could use it.
The easier fix is to give them the same name, which makes the duplication plain. Low cost, but it is
exactly the "second copy of the same thing" this review looks for.

### MINOR 2: `StudyStore::addPassage` parameter `snippet` now carries the whole text

- `src/study/StudyStore.h:119` still names the parameter `snippet`.
- `src/study/StudyStore.cpp:286-287` now assigns it to both fields:
  - `passage.snippet = snippet;`
  - `passage.displayText = snippet;`
- The caller passes `label.text()` of up to 384 bytes
  (`src/activities/reader/PassageSelectActivity.cpp:381`). `PassageDoc::add` derives the 120-byte
  snippet from it.

A reader of the signature would expect the argument to already be the bounded snippet. Rename it to
`text` (or `passageText`) in the declaration and the definition.

### MINOR 3: `ASelectionAcrossAPageTurnKeepsItsStart` does not test the page-turn code

`test/passage_label/PassageLabelTest.cpp:30-43` rebuilds by hand the slicing that the activity does
("anchorIndex to end of page, then top of page to endIndex") and feeds the result to `Builder`. The
first cause of the bug was index arithmetic in `PassageSelectActivity`, which this test never runs:

- `appendWords(anchorIndex >= 0 ? anchorIndex : 0, ...)` at `PassageSelectActivity.cpp:149`;
- the `lo`/`hi` choice at `PassageSelectActivity.cpp:373-375`.

The test would pass unchanged against the old, broken activity.

The activity cannot be host-tested, and device checks 4 and 5 in the PR body cover it, so this is not
a gap in coverage the PR hides. The test name overclaims, though. Rename it to describe what it
actually checks, for example `ConcatenatesSuccessiveFeedsInOrder`, or drop it: `JoinsWordsWithSingleSpaces`
already covers joining.

### MINOR 4: `passage` means two different things in `drawScreen`

In `src/activities/boot_sleep/StudySleepScreen.cpp:327-333`:

- `const auto passage = study_sleep::fitPassage(content.passage->text, ...)` is a `FitResult`;
- `content.passage` is the `Candidate`.

Both are then used close together (`passage.lines` at `:344`, and `content.passage->reference` at
`:350`). Naming the result `fitted` or `passageFit` removes the ambiguity.

## Not findings (considered and rejected)

- **`wholeTextFits`** (`src/activities/boot_sleep/StudySleepPick.h:354-356`) is a trivial predicate,
  pulled out so that `offerRow`'s fallback can be host-tested without ArduinoJson. Its comment
  explains why it is deliberately lenient where `fromJson` is strict. That extraction is how this
  file is already structured, not dead weight.
- **`label.reset()` in `setAnchor`** makes the `appendWords(0, ...)` fallback at
  `PassageSelectActivity.cpp:149` safe even before an anchor exists. `advancePage` also returns
  early unless `phase == PickingEnd` (`:132`), so that path cannot run in practice.
- **Comments.** None restates the next line. The new ones explain constraints: the failed-turn
  double-append at `:146-147`, the whitespace-before-length ordering at `PassageDoc.cpp:120-121`,
  and the widening-needs-a-version-bump rule at `PassageDoc.h:39-40`.

VERDICT: CLEAR
