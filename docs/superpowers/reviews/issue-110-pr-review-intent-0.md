Tier: heavy

# PR #171 — intent review 0 (issue #110)

Reviewed: `gh pr diff 171` (`git diff 9a307341..e8352265`) against issue #110, the spec
`docs/superpowers/specs/2026-09-27-issue-110-design.md` and the plan
`docs/superpowers/plans/2026-09-27-issue-110-plan.md`.

## Method

- Pulled every code block out of the plan and diffed it against the committed file:
  - `ReaderBookmarks.cpp` (plan `:295-467`)
  - `AutoPageTurn.h` (`:823-859`)
  - `AutoPageTurnTest.cpp` (`:727-799`)
  - `BookmarkMatchTest.cpp` (`:81-137`)

  The only differences:
  - the `activities/Activity.h` include and its comment (`ReaderBookmarks.cpp:12-15`), which the PR explains;
  - a clang-format reflow of the `offset` ternary (`ReaderBookmarks.cpp:125-128`).

  The tests and `AutoPageTurn.h` match the plan byte for byte.
- Read the whole `EpubReaderActivity.{h,cpp}` diff hunk by hunk, checking each hunk against the spec's "Data and control flow" list.
- Checked the plan's leftover counts:
  - `grep -c lastPageTurnTime EpubReaderActivity.cpp` = 9, as the plan says (`:952`).
  - The file is 1,789 lines, down from 1,952 at `9a307341`, which matches the PR body.
- Confirmed the justification for the include deviation: `EndOfBookOptions.cpp:13` carries the same include.

## Acceptance against issue #110

The issue's "What it needs" has three groups.

| Issue item | Status |
|---|---|
| Reader: bookmark and passage controller | Bookmark controller delivered (`ReaderBookmarks.{h,cpp}`, `util/BookmarkMatch.h`). The passage controller was rejected with evidence (spec A2). |
| Reader: then auto page turn | Delivered (`AutoPageTurn.h`, wired at every former `automaticPageTurnActive` and `pageTurnDuration` site). |
| Web server: route groups | Not in this PR. Deferred to a follow-up (spec A1). The PR body carries text ready to file. |
| `setup()`: named init stages | Not in this PR. Same follow-up. |

The issue itself says "Take one piece out at a time, each a behaviour-preserving PR". One PR was never expected to cover all three groups. The deferral is not silent: the spec (A1), the plan (`:996-1012`) and the PR body ("What is done and what remains", plus the follow-up text) all state it.

## Spec requirements

- **A3.** Plain member, no heap allocation: `EpubReaderActivity.h:44` `ReaderBookmarks bookmarks;`.
- **A4.** The activity keeps the `!section || !epub` guard, the `RenderLock` read and every `requestUpdate()`: `EpubReaderActivity.cpp`, the new `addBookmark`. `ReaderBookmarks` includes nothing that gives it access to `Activity` members.
- **A5.** Toggle is split into `beginToggle` and `toggle`. `getCurrentPosition()` runs only after `beginToggle` returns true, and after the locked read, as before. `pageRange` is computed inside `toggle` after `progress`, which is the original order. The SD read (`getTextFromSectionFile`) stays on the add path only (`ReaderBookmarks.cpp`, `toggle`).
- **A6.** Field types are unchanged, and each is touched from the same call sites (`loop`, `renderBook`, `renderStatusBar`).
- **A7.** `lastPageTurnTime` stays on the activity and is passed to `due()`. `toggleAutoPageTurn` still sets it only after the range check. `AutoPageTurn::start` clears `active_` on rejection, so the old `automaticPageTurnActive = false;` on that path is preserved.
- **A8 and A9.** The pure headers are host-free. `getPageProgressRange` moved into `ReaderBookmarks.cpp`, not the pure header.
- **A10.** `test/CMakeLists.txt` is not committed. The two hand-off lines are in the PR body, at the anchors that exist in the file (`return_stack`, `bookmark_doc`).
- **A11.** The `"ERS"` log tag and messages are unchanged.
- **Call sites.** All nine substitutions from the call-site list are present, including the reworded `openHighlights` comment (`EpubReaderActivity.cpp:344-345`).

## Scope

No expansion. The diff touches only the files declared by the plan (`:10-14`) and the pipeline documents. No settings, strings, on-disk format or refresh behaviour changed.

## Tests

The tests check behaviour, not a restatement of the code:

- `AutoPageTurnTest` hard-codes the expected intervals and pages-per-minute (`{0, 60000, 20000, 10000, 5000}`, `{0, 1, 3, 6, 12}`) instead of deriving them from `RATES`. It checks both sides of every due boundary. It exercises the `millis()` wrap in a way that works whatever the width of `unsigned long`.
- `BookmarkMatchTest` covers:
  - the exact computed match outside the range;
  - the epsilon on both edges;
  - clamping;
  - the re-paginated-chapter fallback, with both negative and positive assertions.
- `ReaderBookmarks` is not host-built. The spec gives the reason (`Epub` and `Section`) and names the substitute (`--color-moved` review). My plan-to-file diff confirms the moved body.

## Findings

**MINOR 1: the merge precondition from spec A1 is not yet met.**
- `gh issue list --search "follow-up to #110" --state all` returns only #110 itself.
- The PR ends with `Closes #110`. Its body says the follow-up "must be filed before merge". Spec A1 assigns that filing to the orchestrator.
- If the PR merges first, the web-server and `setup()` work has no tracking issue.
- This is not a code defect, and the PR describes it accurately. It is a gate the orchestrator must clear before merging: file the follow-up from the PR body text and link it on the PR.

**MINOR 2: the passage controller the issue named was not built, and the human should see that decision.**
- Issue #110 asks for "the bookmark and passage controller". The PR delivers only the bookmark half.
- The evidence supports the decision. The passage entry points are thin `startActivityForResult` wrappers (`EpubReaderActivity.cpp:325-362`), and a separate object would call back into the activity for every operation.
- Spec review 0 accepted it, and the ready-to-file follow-up text records it.
- No change is needed. The orchestrator should keep that line when it files the follow-up, so the issue author sees the rejection instead of finding it only in a spec.

No BLOCKER or MAJOR findings. The implementation matches the plan exactly, apart from the one explained deviation, and it meets every spec requirement.

VERDICT: CLEAR
