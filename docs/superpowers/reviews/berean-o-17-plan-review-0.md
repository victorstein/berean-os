Tier: standard

# berean-o-17 (t4) — Show mode: plan review 0

Plan: `docs/superpowers/plans/2026-10-15-berean-o-17-plan.md`
Spec: `docs/superpowers/specs/2026-10-15-berean-o-17-design.md`

## What was checked

- **Spec coverage.** Every Goal bullet and A1–A18 maps to a step. A1: Task 1 (`verseCount`/`verseCard`).
  A2/A9: `drawFooter` (plan:942-956). A3/A4: Task 3 (`RUNG_FONT_IDS`, `splitsAWord`). A5/A6: Task 4
  `paginate`, with the reference drawn only where its lines fall. A7/A8: `computeGeometry`/`drawPage`.
  A10–A12: `loop()` in the spec's order, `ButtonNavigator` rather than `detectPageTurn`. A13: Task 2.
  A14: `displayWithRefreshCycle` with the counter starting at 0. A15–A18: Task 6. The error-handling
  table is covered too: the clamp at plan:837, finishing at once on an empty deck at plan:826-830, the
  range guard and the OOM toast at plan:1027-1035, and a null `layout` at plan:1044. Nothing is dropped.
  The spec's testing list maps onto Tasks 1–4.
- **`paginate` traced by hand** against every Task 4 fixture: `{0,3,4}` (heights 60/20/50),
  `{0,4}`, the wrapped reference `{0,3,4}`, and `{0..5}` for the 10 px box. It always terminates:
  `end ≥ first+1`, and it returns when `end ≥ lines`. When the tail fits, the loop exits once `first`
  reaches `tailStart`, so the reference is never split and never orphaned (A5's three rules).
- **`splitsAWord`** against `wrapText` (`PreachingCardLayout.cpp:174-222`). A span ends on a non-break
  byte only on the over-wide-word or ≥128-byte fallback, so the test matches A4. A span that ends at
  the end of the text is excluded.
- **Push/pop and threading** against `ActivityManager.cpp:93-160`. The push is deferred
  (`pendingActivity`). The handler runs with the render lock released, so `RenderLock` in
  `onShowFinished` is correct, and `requestUpdate()` follows the handler. `finish()` called from
  `onEnter` is handled on the next pass of the pending loop. The quote action fires on touch release
  (`FreeInkUICore.h:1246`). `clearTapFlash()` called during dispatch is honoured
  (`FreeInkApp.h:796`, `flashSuppressed_`).
- **API shapes**: `detectTouchPageTurn`'s `TouchPageTurn{prev,next}`, `isTouchMenuTap`,
  `displayWithRefreshCycle(const GfxRenderer&, int&)` (`ReaderUtils.h:79-177`),
  `drawCenteredText(fontId, y, text, black, style)` (`GfxRenderer.cpp:632`), `truncatedText`,
  `selectVerse` (`PreachingCardNav.h:40`), `ActivityResult::data` as a `std::variant`
  (`ActivityResult.h:98-112`), and `STR_PREACHING_SHOW_POSITION` = `"%d / %d · %s"`
  (`english.yaml:495`). All match the plan's use.
- **File lock.** The four `FILES:` lines (plan:7-10) are at column 0, outside any fence, and list
  every tracked file any step creates or edits. The only other writes are the gitignored build
  outputs `.pio/*.log` and `build/test`. No step pushes, opens a PR or runs `hpipe submit`.

## Findings

### MINOR

**m1 — Task 7.2's "the two warning counts are equal" compares a full build with an incremental
one.**
- Claim: plan:52-59 records `grep -c "warning:"` from the Task 0 build. Plan:1100-1105 expects the
  final build's count to be equal.
- Problem: Task 0 is the first `pio run` in a fresh worktree, so it is a full build (the spec cites
  R§6: no `.pio/` yet). The final run is incremental and recompiles only the files changed since
  Task 6. PlatformIO prints a warning only for a translation unit it compiles, so the final log
  normally has far fewer warnings than the baseline. The implementer then either chases a phantom
  difference or learns to wave the check through. The per-file greps at plan:972 and plan:1060 are
  what actually catch a new warning in this plan's files. The equality check adds nothing and
  misleads.
- Fix: in 7.2, replace the count comparison with
  `grep -n "warning:" .pio/final-build.log | grep "PreachingShow\|PreachingTopicActivity"`, and expect
  no output. Drop the baseline count from Task 0.2, or keep 0.2 only to prove the tree builds before
  any edit. In the "Report back" line, replace "both warning counts" with that grep's result.

**m2 — The rung fit also rejects a rung when the *reference* splits a word. A4 names only the quote.**
- Claim: plan:548 adds `!splitsAWord(card.reference, out.reference)` to `fitsOnePage`.
- Problem: spec A4 (`design.md:158-166`) defines a fit as the quote stacking within the box with
  "no word of the quote" broken. The plan widens the rule to the reference, without saying so. The
  effect is benign and arguably right: a bold reference should not split either, and in practice a
  book name at 18 is far narrower than the column. Still, it is an unflagged departure, and no test
  pins it.
- Fix: keep the code, and add one line to the Task 3 notes, or to the spec's A4, saying a split
  reference word also steps the size down. Optionally add a test: a quote that fits at rung 0 with a
  16-byte reference word takes rung 1.

VERDICT: CLEAR
