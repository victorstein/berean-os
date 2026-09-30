Tier: heavy

# PR #243 code-quality review: the reader sheet keeps the page after a sub-screen (#239)

- **Reviewed:** `gh pr diff 243` (branch `fix/239-sheet-keeps-page`, head `bf52da91`, code in `94bb471b..24ea3808`)
- **Source files changed:** `src/activities/reader/EpubReaderActivity.{h,cpp}` only; the rest of the diff is the
  pipeline's research, spec, plan and review documents.
- **Lens:** pattern reuse, naming and structure against siblings, dead or noisy code and comments, error-handling
  shape, test design, duplication.

## Findings

None at BLOCKER, MAJOR or MINOR.

## What was checked, with evidence

### One way to do it, and it mirrors existing patterns

- **Synchronous repaint from a loop-task callback.** `reopenReaderMenu` (`EpubReaderActivity.cpp:306-326`) calls
  `requestUpdateAndWait()` from a `startActivityForResult` handler, as `HighlightsActivity.cpp:441` and
  `PassageSelectActivity.cpp:310` already do. The precondition (loop task, no `RenderLock` held) matches the asserts in
  `ActivityManager.cpp:313-320` and is recorded once, on the declaration (`EpubReaderActivity.h:153-154`), rather than
  repeated at each call site.
- **Shared state under `RenderLock`.** The clear (`:314-317`) and the read (`:320-323`) use the same short scoped-lock
  block as the existing `openReaderMenu` read of `chapterPassageCount` (`:282-286`). The write in `renderBook`
  (`:1478`) sits beside `pageShown = true` (`:1477`) inside the render task's lock, so the new flag follows the same
  ownership as its sibling.
- **Cancel routing.** `openHighlights` now takes `CancelTo` (`EpubReaderActivity.h:175-176`, `.cpp:424`) and captures
  it into the handler exactly as `openChapterPicker` (`:951-956`) and `openBibleSearch` (`:968-973`) do. It was moved
  in the header to sit with those siblings, after the `CancelTo` enum it needs (`.h:172-176`). No second enum, flag or
  overload was introduced.
- **A single reopen path.** All six sub-screen cancel handlers now call `reopenReaderMenu()` (`:772`, `:818`, `:832`,
  `:859`, `:954`, `:971`, plus the Highlights handler at `:438`). The only remaining direct calls of
  `openReaderMenu` are the two from-the-page opens with `true` (`:614`, `:630`) and the night branch inside the helper
  (`:311`). No handler still uses the old `openReaderMenu(false)` idiom, so there are not two ways to reopen the sheet.

### Naming and structure

- `pageRendered` pairs naturally with `pageShown`. Its header comment (`.h:56-58`) states the one thing that separates
  them (per-render versus session-long) and who writes it, in the same shape as the `pageShown` comment above it.
- `reopenReaderMenu` reads as the counterpart of `openReaderMenu`. The early return for night mode keeps the main path
  flat, in line with the file's guard-clause style.
- The updated `openReaderMenu` comment (`.h:150-151`) now points at the one place that chooses `false`, which is
  correct for the merged state.

### Dead code, comments, logging

- No commented-out code and no dead parameters. The old default-argument `openHighlights(optional)` declaration was
  removed, not left beside the new one.
- Comments explain reasons rather than restate code. The night-mode comment (`:307-308`) gives the reason the render
  is skipped, and the rewritten `openHighlights` comment (`:425-432`) drops the stale "Task 7's job" narration and says
  why the Bookmarks handler is not reused. That comment's reference to "the BOOKMARKS case below" is still accurate
  (`:769`).
- The two `LOG_DBG` lines (`:310`, `:324`) use the file's `"ERS"` tag and record which branch ran. That is the
  diagnostic a device run needs, and it adds no noise to the page-turn hot path.

### Error handling

- A failed redraw returns from `renderBook` before `:1478` (the retry and give-up returns at `:1449-1466`), so
  `pageRendered` stays false and the sheet falls back to the cleared form. This is fail-safe degradation through the
  existing `pageOnScreen == false` path in `EpubReaderMenuActivity.cpp:258`, rather than a new error branch. It
  matches the file's established approach of logging and falling back.
- No allocation is added, so the `makeUniqueNoThrow` and OOM-logging rules do not come into play.

### Tests

- No host test was added. Activities are not in `test/CMakeLists.txt`, and the behaviour is a render-produced boolean
  sequenced across activities. The spec (A-12) and the plan record this decision openly, and the PR body presents the
  host run as a regression check only. A test that stubbed the render task to set a bool would test the stub rather
  than the behaviour, so leaving it out is the better design, not a gap. The owner's device steps are the real
  evidence.

### Duplication

- The night check in `reopenReaderMenu` (`:309`) covers the same condition as the menu's own `"night"` reason
  (`EpubReaderMenuActivity.cpp:260-263`). This is an observation, not a finding. The two checks do different jobs:
  the menu decides how to draw, while the helper decides whether a render is worth paying for first. The comment at
  `:307-308` says so, and the only cost of the two drifting apart would be one wasted refresh or one cleared sheet,
  never a wrong frame. Pulling the rule into a shared helper for a single boolean would add indirection without
  removing any risk.

## Note (not a finding)

`gh pr checks 243` currently shows only `lint-title`. The build and format jobs had not reported when this review ran.
They gate the merge independently of this review.

VERDICT: CLEAR
