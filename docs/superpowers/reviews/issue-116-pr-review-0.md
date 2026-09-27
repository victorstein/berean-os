Tier: standard

# PR #153 review 0 — toast for transient outcome messages (issue #116)

Reviewed `git diff main...HEAD` on `feat/116-toast-outcome-messages` (head `91fcf510`)
against issue #116, `docs/superpowers/specs/2026-09-27-issue-116-design.md` and
`docs/superpowers/plans/2026-09-27-issue-116-plan.md`.

## Intent

**Acceptance criteria from the issue:**

- *Add `GUI.drawToast(renderer, message)` wrapping `fui::toast` with the bottom anchor and
  the theme's text style.* Met. It is declared `virtual` beside `drawPopup` at
  `src/components/themes/BaseTheme.h:222-224` and implemented at
  `src/components/themes/BaseTheme.cpp:500-547`. It sets `props.anchor = fui::ToastAnchor::Bottom`,
  uses `FONT_BODY` (the `UI_12_FONT_ID` that `drawPopup` uses), and takes `popupTextBold`.
- *Route `PostedMessage::drawNext` through it.* Met, at `src/activities/PostedMessage.cpp:34`.
- *Route the reader's bookmark messages through it.* Met, at
  `src/activities/reader/EpubReaderActivity.cpp:1302`. That is the only draw site for
  `bookmarkToastString`, so Added, Removed, Too large and Save failed all move together.
- *Route BmpViewer's Done and Failed through it.* Met, at
  `src/activities/util/BmpViewerActivity.cpp:230,232`.
- *Keep `drawPopup` for blocking or progress states.* Met. `drawPopup` and
  `fillPopupProgress` are unchanged, and only the four named calls moved.
- *Watch for: the status bar.* Addressed. `ToastLayout::bounds` subtracts
  `UITheme::getStatusBarHeight()` (`src/components/ToastLayout.h:20-25`,
  `UITheme.cpp:132-140`), and on top of that `fui::toast` keeps its 16 px margin above the
  bounds bottom (`toast.h:38-39`). This is host-tested at
  `test/posted_message/ToastLayoutTest.cpp:24-32`.
- *Watch for: the message stays up as long as the popup did.* Met by construction. No timing
  code is touched: not `PostedMessageQueue::MIN_DISPLAY_MS`, not
  `BOOKMARK_MESSAGE_DURATION_MS`, and not BmpViewer's `delay(1000)`.
- *Watch for: ghosting, with a HALF-refresh fallback.* Deferred to the device, and this is
  explained. Spec A5 keeps the FAST refresh that `drawPopup` used. The PR's device checklist,
  step 8, names the exact fallback (`pagesUntilFullRefresh = 1` plus `forcedRefreshPending`
  under `RenderLock`) if a ghost shows up. The issue itself asks for a device check first
  ("check … and fall back … if it doesn't"), so deferring does not quietly shrink the scope.

**Spec requirements.** I checked each one:

- A2 colours match `drawPopup`.
  - Classic (`popupTextInverted = true`, `BaseTheme.h:170`): the old code filled the outer
    rect black and the inner rect white, with black text. The toast uses paper White and ink
    Black for both the border and the text.
  - Lyra (`false`, `LyraTheme.h:55`): the old code drew a white rim, a black box and white
    text. The toast uses paper Black and ink White.
  - `GfxRendererTarget::text` resolves `black = !inverted && color != White`
    (`FreeInkUIGfxRenderer.h:133`), so `text.color = ink` gives the right polarity.
- A3: `maxLines = ToastLayout::MAX_LINES` (4).
- A4: the padding formula is at `ToastLayout.h:38-42`.
- A5: it ends in `renderer.displayBuffer()` with the FAST default.
- A6: it returns early, with no refresh, on a null or empty message (`BaseTheme.cpp:501`).
- A8: the method is `virtual`.
- A9: BmpViewer still draws immediately.
- All four comment rewords listed in the spec are present, plus the test header comment.

**Scope.**

- Nothing beyond the spec. The four unlisted `drawNext` screens (Launcher, FontDownload,
  ClockSync, OtaUpdate) move as a side effect of the shared entry point. Spec A7 names and
  accepts this, and the PR description lists them.
- No strings or translation files change.
- `drawPopup` is not modified.

**Divergence from the spec and plan.** The spec put the test in a new `test/toast_layout/`
suite with an orchestrator `add_subdirectory` hand-off. It also named the line cap
`TOAST_MAX_LINES`. The plan (`plan.md:14-18`) moves the test into the existing
`test/posted_message/` suite. It also puts `padding()` and `MAX_LINES` in `ToastLayout.h`, so
the padding rule can be host-tested too. The code matches the plan, the plan explains the move,
and the PR description says `test/CMakeLists.txt` is untouched. I found no divergence from the
plan that goes unexplained.

**Tests.** The seam is chosen well. The only rule that can be tested on the host is the bottom
bound against the status bar, and the tests check it through observable edges (`y + height`),
not by repeating the formula. The tests cover:

- the worst-case status bar, and a hidden one
- the full viewable width
- the clamp to empty
- use in a constant expression

The fixture constants match their sources:

- the insets `{9,3,3,3}` match `BoardConfig.h:625-630`
- the Lyra and Classic popup metrics match `LyraTheme.h:50-53` and `BaseTheme.h:165-168`

## Quality

**Pattern reuse.**

- `drawToast` builds `fui::GfxRendererFrame<1>` from `uiScaleSpec()` fonts the same way
  `drawHeader` does.
- The style block (`BaseTheme.cpp:530-539`) copies the `OptionPopup.h:196-203` idiom
  line for line: `defaultPopupStyles()`, then set the border, width and radius, then copy
  `normal` into the other four states.
- `ToastLayout.h` follows `PostedMessageQueue.h`: a pure, dependency-free `constexpr` header
  that the host can test.

**Duplication.** I found none. No second popup-style helper exists to reuse, and
`drawPopup`'s own geometry cannot be shared, because it draws the frame outside the box and
`fui::popup` strokes inside it.

**Naming and structure.** These are consistent with the neighbours:

- `drawToast` sits next to `drawPopup` and has the same signature shape.
- The `Bounds` and `Padding` structs use TRBL order, which matches `getOrientedViewableTRBL`
  and `fui::Insets`.

**Comments.** Every comment added explains a non-obvious *why*:

- that `popupTextInverted`'s name reads backwards
- why the frame is added to the padding
- why the status bar is always reserved

None of them restates the next line. The reworded comments describe the code as it now is,
not the change.

**Resources.**

- No new persistent allocation.
- The transient `wrappedText` heap use on the draw path is stated and justified in the spec
  ("Data and control flow").
- Stack use matches `drawHeader`.
- The casts to `int16_t` are safe for an 800 px screen.

**Dead code.** None. `props.styles.normal.foreground` is set, but `fui::popup` does not read
it. Setting it keeps the style block complete and harmless, which matches how the style set is
used elsewhere, so it is not a finding.

## Findings

1. **MINOR:** `test/posted_message/ToastLayoutTest.cpp:67`.
   `static_assert(ToastLayout::MAX_LINES == 4, ...)` restates the constant at
   `src/components/ToastLayout.h:43`. It is a change detector, not a behaviour check. It
   cannot catch the failure it is named after ("the longest refusal must fit without an
   ellipsis"), because that depends on font metrics the host test does not load, and it
   fails only when someone edits the number on purpose. The rationale already lives in the
   comment beside it and in spec A3. You can delete it or keep it; it does no harm.

VERDICT: CLEAR
