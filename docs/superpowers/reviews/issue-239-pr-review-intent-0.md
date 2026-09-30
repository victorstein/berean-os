Tier: heavy

# PR #243 intent review: the reader sheet keeps the page after a sub-screen (#239)

- **Reviewed:** `gh pr diff 243` (branch `fix/239-sheet-keeps-page`, head `24ea3808`)
- **Against:** issue #239, spec `docs/superpowers/specs/2026-09-30-issue-239-design.md`, and plan
  `docs/superpowers/plans/2026-09-30-issue-239-plan.md`
- **Source files changed:** `src/activities/reader/EpubReaderActivity.{h,cpp}` only. The spec's Architecture section
  limits the change to these two files, and `EpubReaderMenuActivity` is untouched (spec Non-goals).

## Findings

None at BLOCKER, MAJOR or MINOR.

## Issue acceptance criteria

| # | Criterion | Status | Evidence |
|---|---|---|---|
| 1 | After Go to, Search, Footnotes, Bookmarks or Highlights and then back, the sheet shows the current page above it | Met in code; needs a device check | Go to `EpubReaderActivity.cpp:954`, Search `:971`, Footnotes `:818`, Bookmarks `:772`, Highlights and Tags here `:438`. Each calls `reopenReaderMenu()` (`:306-326`), which redraws with `requestUpdateAndWait()` (`:318`) and then calls `openReaderMenu(onPage)` (`:325`). |
| 1b | The same page, not a neighbour | Met by existing logic | This is spec A-7. The PR adds no new position code, and `releaseSectionKeepingPosition` is unchanged. |
| 2 | Recent chips and "Tags here (n)" stay correct | Met | `openReaderMenu` computes both after the redraw (spec A-8), and the PR does not change `openReaderMenu`. The failure-only edge, where "Tags here" can read 0 after a failed redraw, is disclosed in the spec's Error handling section and in the PR's trade-offs. |
| 3 | PSRAM and internal heap return to baseline after the sheet closes, and this is logged | Met; needs a device check | The PR adds no allocation. The existing `logHeap` before/open/closed lines serve as the log (spec A-11). The new `LOG_DBG` at `:310` and `:324` records which branch each round trip took. The PR's device step 4 covers the check. |
| 4 | Device: Eclesiastés 4 → menu → Go to → back → back shows the page | Owner check pending | This is device step 1 in both the PR and the spec. It cannot be verified off-device, and the PR says so. |

## Spec requirements

- **A-1 and A-4 (redraw, then a per-render flag).** `pageRendered` is declared at `EpubReaderActivity.h:56-58`. It is
  cleared under `RenderLock` at `.cpp:314-317` and read under `RenderLock` at `:319-323`. `renderBook` sets it at
  `:1478`, beside `pageShown = true` and only after `renderContents`. That write also happens under `RenderLock`,
  because the render task holds the lock for the whole of `render()` (`ActivityManager.cpp:52-59`). The failure
  returns before `:1478` (the page-load retry and give-up at `:1449-1466`, plus the earlier build and empty-chapter
  returns) leave the flag false, as A-4 requires. `requestUpdateAndWait` is legal here because the handler runs after
  the reader is made current and the lock is released (`ActivityManager.cpp:117,126-127`). After the handler, the
  pending push suppresses the extra `requestUpdate` (`:131-133`).
- **A-2 (one helper for all six cancel sites).** `grep openReaderMenu(` finds only `:311` (the night branch inside the
  helper) and the two from-the-page calls `openReaderMenu(true)` at `:614` and `:630`. No sub-screen handler calls
  `openReaderMenu(false)` any more. All six sites are covered: Bookmarks `:772`, Footnotes `:818`, Text settings
  `:832`, Go to % `:859`, Go to `:954` and Search `:971`.
- **A-3 (Highlights and Tags here).** `openHighlights` now takes a `CancelTo` argument (`.h:175-176`, `.cpp:424`). The
  HIGHLIGHTS and TAGS_HERE menu actions pass `CancelTo::Menu` (`:879`, `:883`). The entry intent passes
  `CancelTo::Page` (`:1053`). The Bible-tags redirect (`openBibleTags`, `:372-375`) opens a different reader with
  `goToReader`, not a sub-screen, so it correctly has no reopen path.
- **A-5 (night mode).** Night mode takes the early return at `:309-313` with `openReaderMenu(false)`, so there is no
  redraw.
- **A-6 (Text settings reflows).** The handler still calls `releaseSectionKeepingPosition()` before
  `reopenReaderMenu()` (`:831-832`). The blocked-loop cost is disclosed in the spec, the PR body and device step 7.
- **A-11 (log line).** The log messages are `"Reopening menu cleared: night"` and
  `"Reopening menu %s"` with `over page` / `cleared: no page`, which match the spec's wording exactly.
- **Stale comments.** The spec names two comments this change makes wrong, and both are rewritten.
  `EpubReaderActivity.h:150-151` (the `pageOnScreen` comment) and `.cpp:425-432` (the `openHighlights` comment) now
  describe the `CancelTo` routing.

## Scope

- **No scope reduction.** Every sub-screen the issue names is wired. Go to % and Text settings are wired too, which
  follows the owner's "any sub-screen" rule.
- **No scope expansion.** Nothing outside the reader activity changed. The three documented non-goals hold:
  confirmed results still navigate, entry intents still cancel to the page, and `Tag`/`HIGHLIGHT_PASSAGE` is
  untouched.
- **Snapshot ownership.** No buffer crosses activities, which respects the issue's rejected alternative.

## Tests

The PR adds no host test. Spec A-12 records this as a deliberate decision: `test/CMakeLists.txt` does not build
activities, and the behaviour is a render-produced boolean in activity sequencing. The plan's "How TDD applies"
section carries the same decision, and the PR says plainly that the 1506/1506 host run is only a regression check.
None of this is hidden or misrepresented, so I do not raise it as a finding. The real behavioural evidence is the
owner's device run, steps 1-7 in the PR body.

## Plan conformance

The diff matches plan Steps 1-4 almost word for word. The `openHighlights` replacement, including its comment, is
character-identical to plan lines 440-457. The call sites match Step 4's expected grep result: three calls and no
bare `openHighlights()`. The header move of `openHighlights` below `CancelTo` follows Step 4's note. The plan puts the
`pageRendered` declaration beside `pageShown`, and that is where it is. I found no divergence that the PR leaves
unexplained.

## Note for the owner (not a finding)

The PR body reports an `x4pro` build that failed in about 7 s with no captured cause, followed by a clean rerun. This
is outside an intent review, but CI's `x4pro` build should confirm the change builds cleanly before merge.

VERDICT: CLEAR
