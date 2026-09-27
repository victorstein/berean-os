Tier: standard

# Issue #116 plan review 0

Plan: `docs/superpowers/plans/2026-09-27-issue-116-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-116-design.md`
Read on `6eecb975`.

## What was checked, and held

- **Spec-to-step coverage.** Every spec element has a step:
  - `ToastLayout::bounds` with the spec's formula and clamp: step 2, plan:177-182, spec:85-88.
  - `drawToast` is `virtual void`, beside `drawPopup` (`BaseTheme.h:221`): step 4a.
  - The null/empty guard (A6), the insets, the status-bar reservation (A1), and every `ToastProps` field the spec lists, set as spec:115-126 says. `maxWidth` and `margin` are left at their defaults. It ends with `displayBuffer()` (A5): step 4c.
  - All four call sites: step 5.
  - All four spec comment rewords, plus two more (`addBookmark`, `loop()`) and the host test's header: step 6.
  - Test cases 1-5 from spec:327-336: step 1.
  - The build and full-tree format: step 7.
  - The device checklist and the accepted edge cases carried into the PR: step 8.
- **The one deviation is declared and sound.** Plan:12-22 puts `ToastLayoutTest` in `test/posted_message/` rather than a new `test/toast_layout/`. That keeps the shared `test/CMakeLists.txt` untouched, and `add_subdirectory(posted_message)` is already registered at `test/CMakeLists.txt:79`. This is a packaging change, not a scope or design reversal. Moving `TOAST_MAX_LINES` into `ToastLayout::MAX_LINES`, and padding into a pure `ToastLayout::padding`, adds host coverage for A3 and A4 without changing either value.
- **The step 4 code matches the real SDK types.**
  - `ToastProps` has `message/text/styles/padding/anchor/maxWidth/margin` (`toast.h:15-23`).
  - `TextStyle` has `font/color/maxLines(uint8_t)/bold` (`FreeInkUICore.h:534-544`).
  - `BoxStyle` has `background/foreground/border/borderWidth/radius` (`FreeInkUICore.h:579-586`).
  - `StyleSet` has `normal…disabled` (`:588-593`).
  - `Insets` is `{top,right,bottom,left}` as `int16_t` (`:87-92`), which matches the plan's argument order. `fui::Rect` is `{x,y,width,height}` as `int16_t` (`:94-98`).
  - `GfxRendererFrame(renderer, small, body, title)` exposes `frame` (`FreeInkUIGfxRenderer.h:243-259`). `toast.h` is reachable through `FreeInkUI.h:39`.
  - `uiScaleSpec().bodyFontId` is `UI_12_FONT_ID` (`src/components/UIScale.h:14-17`), the font `drawPopup` uses (`BaseTheme.cpp:476`).
  - `getStatusBarHeight` is static (`UITheme.h:37`), so calling it through `getInstance()` compiles.
  - `getOrientedViewableTRBL(int*,int*,int*,int*)` is at `GfxRenderer.h:205`.
- **Colours reproduce both themes.** The adapter's `stroke` draws white when `color == White` (`FreeInkUIGfxRenderer.h:103-116`). Its `text` draws white when `color == White` (`:137`).
  - Lyra (`popupTextInverted=false`, `LyraTheme.h:55`) gets a black fill, a white stroke and white text. That matches `drawPopup`'s white rim around a black box (`BaseTheme.cpp:484-487,494`).
  - Classic gets the inverse.
- **The padding test values are right.** Lyra 16/12/2 gives 8/18. Classic 15/15/2 gives 9/17 (`LyraTheme.h:50-52`, `BaseTheme.h:165-167`).
- **Measure and draw wrap alike.** The toast sizes its panel from `layoutText`, and the draw uses `renderer.wrappedText`. Both are greedy word wraps over the same `getTextWidth`. The content rect equals the widest measured line, so the draw reproduces the measured lines and does not spill past the panel (`FreeInkUICore.h:733-800`, `GfxRenderer.cpp:1797-1860`).
- **Every "old" snippet matches the tree.**
  - `PostedMessage.cpp:34`
  - `EpubReaderActivity.cpp:514,1302,1773-1774,1787`
  - `BmpViewerActivity.cpp:230,232`
  - `PostedMessage.h:5`
  - `PostedMessageQueue.h:11`
  - `ReaderUtils.h:232`
  - `PostedMessageQueueTest.cpp:2`
  - The `CMakeLists.txt` header
- **The test commands work as written.** Step 6's grep expectation holds: `PostedMessage.h:6` keeps "immediate popup". The ctest regex matches the `PostedMessageQueue.*` test names (`PostedMessageQueueTest.cpp:15`). The lock script at plan:50 exists. The branch tracks `origin/feat/116-toast-outcome-messages`, so a bare `git push` works.
- **The FILES lines are complete.** Plan:7-10 sit at column 0, outside any fence, as repo-relative paths. They cover every file a step touches: steps 1-6, plus step 7's formatter, which only reaches files those steps edited.

## Findings

### MINOR 1: step 7's warning check is vacuous

- **Claim.** Plan:565-567 says to check for new warnings with `… run -e x4pro 2>&1 | grep -n "BaseTheme\|ToastLayout" | grep -i warn`, which "should print nothing".
- **Problem.** This is a second `pio run` straight after the first succeeded. It is incremental, so `BaseTheme.cpp` is not recompiled and no warning is re-emitted. The grep prints nothing whether or not the first build warned. It also contradicts the plan's "once after the last code edit" rule (plan:46).
- **Evidence.** Plan:562 runs the build. Plan:567 runs it again only to grep.
- **Fix.** Capture the one build and grep its log:

  `…/pio-locked.sh run -e x4pro 2>&1 | tee build/x4pro.log`

  then:

  `grep -n "BaseTheme\|ToastLayout" build/x4pro.log | grep -i warn`

### MINOR 2: steps 4-6 commit firmware code that nothing has compiled yet

- **Claim.** Each step leaves the tree committable.
- **Problem.** Step 4's commit (plan:366-373) and step 5's commit (plan:435-446) contain firmware-only code. The host tests do not build it, and the firmware build is deferred to step 7. That deferral follows `CLAUDE.md` ("build once after the last code edit"). But if step 7 fails, the plan does not say how to repair it, so an implementer might amend or rewrite history under a sibling. I found no compile error in the step 4 code (see above), so this is procedural.
- **Evidence.** Plan:363-364 ("The build in step 7 compiles it"). Plan:559-588 has no failure branch.
- **Fix.** Add one line to step 7: if the build fails, fix the code and commit it as a new `fix: … (#116)` commit before formatting. Do not amend earlier commits.

### MINOR 3: step 4 has no red test

- **Claim.** Every step starts with a failing test.
- **Problem.** Steps 4-6 have none. For step 4 this is inherent: the spec states the draw is not host-testable (spec:69-71). The testable parts, bounds, padding and the line cap, were pulled into `ToastLayout` and tested red-first in steps 1-3. Steps 5 and 6 are call-site swaps and comment rewords. The plan says so for step 4 (plan:363-364), and the device checklist covers what it draws.
- **Evidence.** Plan:265-373.
- **Fix.** None required. This is recorded so the gap is visibly accepted rather than overlooked.

## Verdict

The plan maps every spec requirement to a concrete step, with full code. Names and signatures stay consistent from step 3 to step 4 (`ToastLayout::Bounds`, `bounds`, `Padding`, `padding`, `MAX_LINES`). An implementer could execute it literally against the current tree. The three MINORs can be fixed inline.

VERDICT: CLEAR
