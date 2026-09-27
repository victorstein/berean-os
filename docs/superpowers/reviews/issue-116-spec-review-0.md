Tier: standard

# Issue #116 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-116-design.md` against issue #116
(`gh issue view 116 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-27-issue-116-research.md`, on `589fb566`.

## What holds

I checked these claims against the code and found them correct. They are listed so the next
pass does not re-verify them:

- `drawPopup` does not wrap and sits at the top. `BaseTheme.cpp:475-480` measures one line,
  and the ratios are at `BaseTheme.h:164` and `LyraTheme.h:49`. Lyra is the default
  (`CrossPointSettings.h:317`). The overflow claim holds. I summed Ubuntu 12 advances from
  `lib/EpdFont/builtinFonts/ubuntu_12_regular.h`, without kerning.
  `STR_TAG_LIMIT_PER_HIGHLIGHT` comes to about 626 px regular and 659 px bold. Even
  `STR_BOOKMARKS_TOO_LARGE`, at about 457 px plus 2×16 margin, is wider than 480 today.
- A2's colour mapping is right, and the research note is wrong. `drawPopup`'s rounded path fills
  the outer box White and the inner box Black (`BaseTheme.cpp:484-485`). It passes
  `popupTextInverted` as `black` (`:494`), so Lyra (`false`, `LyraTheme.h:55`) draws white text
  on a black box with a white rim. The rect path gives Classic a black frame, a white box and
  black text. Research lines 64-66 state the reverse. The spec corrected it, so the spec wins.
- The stroke draws inside the rect: `GfxRenderer.cpp:821-833`, `:877-927`, and
  `FreeInkUIGfxRenderer.h:101-115`. A white stroke gives a white rim
  (`black = paint.color != Color::White`, `:104`), which is what A2 and A4 need.
- The status bar math. Its height is at most 19 + (2+1)·2 + 1 = 26 (`UITheme.cpp:132-140`,
  `CrossPointSettings.cpp:257-258`, `CrossPointSettings.h:85-88`). The portrait insets are
  `{9,3,3,3}`, unrotated (`GfxRenderer.cpp:2368-2376`, `BoardConfig.h:625-630`). The status-bar
  text lane's top is `800-26-3-0-4 = 767` (`BaseTheme.cpp:536`, reader `paddingBottom=0` at
  `EpubReaderActivity.cpp:1666`). The toast's bottom is `800-3-26-16 = 755`, so it clears.
- Measure and draw produce the same line breaks. `fui::toast` measures with `layoutText`
  (`toast.h:31`), but `GfxRendererTarget::text` draws with `GfxRenderer::wrappedText`
  (`FreeInkUIGfxRenderer.h:183`), a different algorithm. Both are greedy on the same
  `getTextWidth(...) <= width` test (`FreeInkUICore.h:784-785`, `GfxRenderer.cpp:1827`). The draw
  rect's width is the widest measured line, so each line lays out the same way at either width.
  There is no divergence finding.
- FONT_BODY is `UI_12_FONT_ID` (`UIScale.h:17`), which is the font `drawPopup` uses.
- The render task stack is 8192 bytes (`ActivityManager.cpp:32-33`), so a `GfxRendererFrame<1>`
  plus `layoutText`'s ~400 B of locals is not a stack risk.
- The posted-message keys exist only in `english.yaml`. A loop over the 15 keys posted through
  `post`/`showMessage` shows each one defined in exactly one file.

## Findings

### MAJOR 1: A3's premise is wrong. The longest message needs three lines, not two, which leaves the cap no headroom

- **Claim.** A3, spec lines 216-219: three lines at 3/4 width "hold the longest posted message …
  expected to need two lines". Device checklist step 3, line 314: "It wraps to two lines".
- **Problem.** The usable text width is `474*3/4 - 2*(popupMarginX + frame)`. That is 319 px
  on Lyra and 321 px on Classic (`toast.h:28,31`; the A4 padding). A greedy wrap of the real
  strings at that width gives these results:
  - `STR_TAG_LIMIT_PER_HIGHLIGHT` needs **3 lines** on both themes: 286/280/49 px regular,
    305/290/52 px bold.
  - `STR_HIGHLIGHTS_LOAD_FAILED` needs **3 lines** in bold on Classic: 279/235/88 px.
  - `STR_LINK_SOURCE_MARKED`'s first line comes to exactly 319 px of 319.

  So `TOAST_MAX_LINES = 3` is met with no line to spare. Kerning or a slightly different
  advance could push a line over, and the store refusal the change exists to show would be
  ellipsised. Checklist step 3 would also report a pass that matches no expectation, or a
  false failure.
- **Evidence.** Sums of the glyph advances (third field, fp4) in
  `lib/EpdFont/builtinFonts/ubuntu_12_{regular,bold}.h`, run through the same greedy rule as
  `layoutText` (`FreeInkUICore.h:774-800`). These are estimates: they ignore kerning. That is
  exactly the "measure it" step A3 defers to implementation, and it is feasible on the host.
- **Fix.** Pick one:
  - Set `TOAST_MAX_LINES = 4`. That costs about one UI_12 line of panel height, and the
    worst-case panel still ends at y = 755.
  - Or set `props.maxWidth` wider than the 3/4 default (for example
    `bounds.width - 2*margin`), so the longest string fits in two lines.

  Whichever you choose, restate A3 with the measured line counts. Change checklist step 3 to
  "wraps to three lines (two with the wider maxWidth), not ellipsised".

### MAJOR 2: "The toast … allocates nothing" is false for the messages that motivate the change

- **Claim.** Spec lines 174-178: "The toast holds no state and allocates nothing … `measureWrappedText`
  and `layoutText` iterate in place". Checklist step 7 bases its heap check on
  "`drawToast` allocates nothing".
- **Problem.** Only the *measure* goes through `layoutText`. The *draw* goes through
  `GfxRendererTarget::text`. A message too wide for one line, which is every store refusal
  (MAJOR 1), takes the `maxLines > 1` branch there. That branch builds
  `std::vector<std::string>` via `renderer.wrappedText` (`FreeInkUIGfxRenderer.h:183`). That
  function copies `remaining`, `currentLine`, `testLine` and `word` as `std::string` for each
  word (`GfxRenderer.cpp:1797-1850`). Those are small, transient heap allocations, and they
  repeat on every render during the 2.5 s hold (`PostedMessageQueue.h:35-46`). `CLAUDE.md`
  ("Resource justification", and the string policy in "The resource protocol") requires this
  to be stated and justified, not denied.

  The companion claim, "the stack peak is no deeper than a header draw" (lines 175-177), is
  also unsupported. `drawHeader` never calls `layoutText`: `grep -rn "measureWrappedText\|layoutText"`
  finds no header component. `layoutText` adds `char buf[228]` plus `Range[16]`
  (`FreeInkUICore.h:744,765-770`). That is harmless on the 8 KB render task, but the reason
  given is wrong.
- **Fix.** Restate the resource paragraph. The toast allocates transiently inside the SDK
  adapter's multi-line draw path, every allocation is freed before `drawToast` returns, the
  total is a few hundred bytes, and it runs on the render task. It is the same path every
  wrapping FreeInkUI list row already takes. Keep checklist step 7, but reframe it: free heap
  must return to baseline after the toast clears, so there is no leak. Do not justify it by
  "no allocation". Drop or correct the stack-parity sentence.

### MAJOR 3: The named ghosting fallback would not force a HALF refresh on a text page

- **Claim.** A5, lines 233-236: if a ghost appears, "the fallback is to set `forcedRefreshPending`
  (`EpubReaderActivity.cpp:1383-1384`) when `loop()` clears the bookmark message".
- **Problem.** `forcedRefreshPending` only feeds `cleanImageBasePending` (`:1383-1385`), and that
  value only picks HALF on the **image** path (`:1450-1455`). A text page goes through
  `ReaderUtils::displayWithRefreshCycle` (`:1461`), which chooses HALF only when
  `pagesUntilFullRefresh <= 1` (`ReaderUtils.h:165`). On an ordinary reader page, which is
  the page the issue's ghosting check targets, setting the flag alone is a no-op. The
  existing knob that works sets both, under the render lock: `ReaderActivity::handleForcedRefresh`
  (`ReaderActivity.cpp:187-195`: `pagesUntilFullRefresh = 1; forcedRefreshPending = true;`
  inside `RenderLock`). The issue also asks for this fallback "if it doesn't [clear
  cleanly]", as part of this change, so it needs to be correct when it is reached.
- **Fix.** Rewrite the fallback as follows. When `loop()` clears `showBookmarkMessage`
  (`EpubReaderActivity.cpp:504-507`), take `RenderLock`, set `pagesUntilFullRefresh = 1`
  and `forcedRefreshPending = true` (mirroring `handleForcedRefresh`), then `requestUpdate()`.
  Say that it covers only the bookmark toast: posted messages have no dismissal hook
  (spec lines 156-158), so a ghost there has no reader-side fallback. Whether the fallback
  lands in this PR or a follow-up, the device checklist should state which.

### MINOR 1: Numbers that contradict each other or the code

- Line 20 says posted messages "run to 54 characters", and the next line says the longest,
  `STR_TAG_LIMIT_PER_HIGHLIGHT`, is 53. It is 53, and no posted key is longer (from the
  key scan above). Make it 53.
- Line 51 says "the 20 other `drawPopup` call sites". `grep -rn drawPopup src lib` gives 25
  hits. Take away the declaration, the definition and 3 comments
  (`BaseTheme.cpp:503`, `EpubReaderActivity.cpp:1774`, `SpineHtmlStream.cpp:38`), and 20 calls
  remain. Four of those move, so **16** stay. Say 16.
- Testing case 1 (line 286) cites `toast.h:36-37` for the bottom anchor. Those lines are the
  Center branch; the Bottom branch is `toast.h:38-39`.

### MINOR 2: Stale "popup" wording the spec does not list

Lines 141-143 reword only `PostedMessage.h:5-7` and `ReaderUtils.h:228-232`. Two more
comments describe the path that changes:

- `EpubReaderActivity.cpp:1773-1774` says the LoadDisabled toast goes through "drawPopup
  [which] ends in a full e-ink refresh". It now goes through the toast, and the refresh is
  FAST, not full.
- `PostedMessageQueue.h:9-11` says "a popup drawn once would be gone".

Add both to the reword list.

### MINOR 3: Device checklist step 3's language repeat tests nothing

Line 315 says "If a long-string language is available (German or Russian), repeat". The
spec itself says at lines 22-23 that no translation defines the posted-message keys, which
the key scan confirms, so German and Russian show the same English string. Drop the
sentence, or point it at a string that *is* translated, such as `STR_ERROR_GENERAL_FAILURE`
("Fehler: Allgemeiner Fehler", `german.yaml:149`), through the SaveFailed bookmark toast.

### MINOR 4: A1 misses the auto-page-turn title lane

When the text lane is hidden, `getStatusBarHeight()` is 0 or only the progress bar. In that
case the reader moves the "auto turn" title *up* by `statusBarVerticalMargin`
(`EpubReaderActivity.cpp:1642-1647`) into space `getStatusBarHeight()` does not reserve. With
the status bar hidden, the title's top is `800-0-3-4-19 = 774`, and the toast's bottom is
`800-3-0-16 = 781`, so they overlap by about 7 px. This is rare, since it needs auto-turn plus a
bookmark or posted message, and it lasts 2.5 s. State it in A1 as an accepted edge case, or
reserve `statusBarVerticalMargin` whenever auto-turn is active.

### MINOR 5: A7 says the bottom of the unlisted screens is free, but the Launcher's is not

A7 (lines 246-250) argues from OTA and FontDownload, whose content is centred. The Launcher
is also a `drawNext` caller (`LauncherActivity.cpp:521`), and it puts its resume strip at the
bottom (`LauncherActivity.cpp:308-309`). A settings-save failure posted there will cover the
resume card for 2.5 s. The toast takes no input, so the card still responds to taps, and the
decision can stand. A7 should name this case instead of claiming the bottom is free.

## Verdict

None of these findings reverses a decision, changes scope, or needs a judgement only the
human can make. MAJOR 1 adjusts a parameter inside A3's own decision, MAJOR 2 corrects a
resource statement, and MAJOR 3 names the right existing knob for a fallback the spec
already plans. Fix all three and the MINORs inline.

VERDICT: CLEAR
