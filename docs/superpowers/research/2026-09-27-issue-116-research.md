# Issue #116 research — FreeInkUI toast for transient outcome messages

Branch `feat/116-toast-outcome-messages`, based on `dc97292e`
(`chore: record batch 2's review and the missing 1.16.11 changelog entry (#151)`).
Every line number below was read on that commit.

## Headline findings

1. **The popup is not in the middle of the screen, and the issue's premise
   needs that correcting.** `BaseTheme::drawPopup` places the box at
   `y = screenHeight * metrics.popupTopOffsetRatio`
   (`src/components/themes/BaseTheme.cpp:475`). That ratio is `0.075f` for
   Classic (`BaseTheme.h:164`) and `0.165f` for Lyra (`lyra/LyraTheme.h:49`).
   Lyra is the default theme (`src/CrossPointSettings.h:317`,
   `uint8_t uiTheme = LYRA;`), so on an untouched device every outcome
   message sits in the top sixth of the page, over the first lines of text.
2. **The toast's default bottom anchor lands on the reader's status bar.**
   `fui::toast` puts the panel at `bounds.bottom() - panelSize.height - margin`
   with `margin = 16` (`toast.h:20,36-37`). The status bar reserves up to
   19 + 6 + 1 = 26 px (below) plus a 3 px bezel inset. A toast laid out
   against the full screen overlaps it. The bounds passed to `fui::toast` have
   to stop above the status bar on reader screens. See "Status bar geometry".
3. **Nothing about the toast is hardware-new.** `fui::toast` is a
   positioning wrapper around `fui::popup` (`toast.h:25-50`). The pixels go
   through the same `GfxRendererTarget` that `BaseTheme::drawHeader` already
   uses on every screen (`BaseTheme.cpp:288-290`). The refresh is whatever the
   wrapper calls afterwards. Today `drawPopup` ends in
   `renderer.displayBuffer()` (`BaseTheme.cpp:495`), whose default is
   `HalDisplay::FAST_REFRESH` (`lib/GfxRenderer/GfxRenderer.h:189`).
4. **The popup does not wrap; the toast does.** `drawPopup` measures one line
   (`getTextWidth`, `getLineHeight`, `BaseTheme.cpp:476-477`), so a long
   translation runs off the screen. The toast wraps to 3/4 of the bounds width
   (`toast.h:28-31`), so its height depends on the message. That makes the
   status-bar clearance a per-message calculation, not a constant.
5. **No host test touches FreeInkUI or the theme today.**
   `grep -rln "freeink::ui\|FreeInkUICore\|fui::" test` returns nothing. The
   only test near this change is `test/posted_message/PostedMessageQueueTest.cpp`,
   which covers queue timing, not drawing. `FreeInkUICore.h` includes only
   `<stddef.h>`, `<stdint.h>`, `<string.h>` and `<atomic>`
   (`FreeInkUICore.h:13-17`), so toast geometry could be host-tested against a
   fake `DrawTarget`. No suite does that yet; it would be a new pattern.

## Which files own the behaviour

| Concern | Owner |
|---|---|
| The popup drawn for every message | `BaseTheme::drawPopup`, `src/components/themes/BaseTheme.cpp:468-497`; declared `virtual` at `BaseTheme.h:221`. No theme overrides it: `grep -rn drawPopup src/components` hits only `BaseTheme.{h,cpp}`. |
| The toast component | `freeink-sdk/libs/ui/FreeInkUI/include/components/overlays/toast.h` (header-only template). Already in the umbrella header, `FreeInkUI.h:39`, and so already included by `FreeInkUIGfxRenderer.h:14`, which `BaseTheme.cpp:3` includes. |
| The posted-message queue | `src/activities/PostedMessage.{h,cpp}`, `src/activities/PostedMessageQueue.h` |
| Reader bookmark messages | `EpubReaderActivity.cpp:1301-1303` (draw), `:504-507` (expiry), `:1748-1761` (`bookmarkToastString`), `:1782-1795` (arming in `addBookmark`) |
| Wallpaper-save result | `src/activities/util/BmpViewerActivity.cpp:226-236` |
| Status bar height | `UITheme::getStatusBarHeight`, `src/components/UITheme.cpp:132-140`; the bar itself is drawn by `BaseTheme::drawStatusBar`, `BaseTheme.cpp:524+` |

`grep -rn "toast\|ToastAnchor" src lib` finds no use of the component; the
only hits are comments and the reader's `BookmarkToast` enum
(`EpubReaderActivity.h:47`).

## Current control flow

### `drawPopup` (`BaseTheme.cpp:468-497`)

It measures one line in `UI_12_FONT_ID` (bold when `metrics.popupTextBold`).
It fills a frame of `popupFrameThickness` and then an inner box: a plain
rectangle for Classic, or rounded (`popupCornerRadius`, 6 on Lyra,
`LyraTheme.h:53`). It draws the text with `popupTextInverted` as `drawText`'s
`black` argument (`BaseTheme.cpp:494`). That gives black text on a
black-framed white box on Classic (`true`, `BaseTheme.h:170`), and white text
on a white-rimmed black box on Lyra (`false`, `LyraTheme.h:55`). This was
corrected after spec review 0. It then
calls `renderer.displayBuffer()` (FAST). It returns the box `Rect`, which
`fillPopupProgress` (`:499-522`) reuses for progress bars. The toast wrapper
must not change that contract for the progress callers.

### Posted messages (`PostedMessage.cpp:28-35`)

`drawNext` takes the queue mutex, gets `queue.next(millis())`, and draws it
with `GUI.drawPopup` **after** the caller's own `displayBuffer`. The popup is
therefore a second FAST refresh laid over a finished screen. `next()`
(`PostedMessageQueue.h:34-39`) keeps returning the same message on every render
for `MIN_DISPLAY_MS = 2500` (`:19`) and then advances.

**Nothing schedules a redraw when the hold expires.**
`grep -rn "MIN_DISPLAY_MS\|PostedMessage::" src` finds no timer. A posted
message comes off the screen only when the screen next renders for its own
reasons. Its dismissal is that screen's normal refresh. This behaviour is the
same for a toast.

`drawNext` has ten call sites. Six of them are the ones the issue names:

- `UiListActivity.cpp:168`
- `ReaderActivity.cpp:180,185`
- `HighlightsActivity.cpp:536`
- `TagFilterActivity.cpp:159`
- `BibleSearchActivity.cpp:999`

`PassageLinksActivity` is not a direct caller. It posts through `UiListActivity`.

**The other four are not in the issue, but they will change too:**

- `FontDownloadActivity.cpp:696`
- `ClockSyncActivity.cpp:148`
- `OtaUpdateActivity.cpp:170`
- `LauncherActivity.cpp:521`

Routing `drawNext` to the toast moves their messages to the bottom as well.
`ReaderUtils::showMessage` (`ReaderUtils.h:228-233`) is a thin
`PostedMessage::post`. The reader's `LoadDisabled` notice
(`EpubReaderActivity.cpp:1779`) and `PassageSelectActivity`/`TagPickerActivity`
messages already arrive through `drawNext`.

### Reader bookmark messages

`addBookmark` sets `showBookmarkMessage = true` and `bookmarkMessageTime`
(`EpubReaderActivity.cpp:1788-1789`), records the outcome in `bookmarkToast`,
and requests a render. `render` draws the page:

1. `renderContents` (`:1361+`) does the B/W pass and calls
   `ReaderUtils::displayWithRefreshCycle` (`:1461`).
2. That picks HALF on the refresh cadence and FAST otherwise
   (`ReaderUtils.h:165-177`).
3. The anti-aliasing grayscale pass follows (`:1465+`, ending in
   `displayGrayBuffer` and `cleanupGrayscaleWithFrameBuffer`).
4. Last, at `:1301-1303`, `GUI.drawPopup(renderer, bookmarkToastString(...))`
   draws into the restored B/W buffer and FAST-refreshes over the
   grayscale page.

`loop()` clears the flag after `BOOKMARK_MESSAGE_DURATION_MS = 2500`
(`ReaderUtils.h:21`) and calls `requestUpdate()` (`:504-507`). **Dismissal is
therefore one full page re-render, not a region repaint:** the popup area is
redrawn with the page and refreshed through the same cadence-driven mode as a
page turn.

Five outcomes map to strings in `bookmarkToastString` (`:1748-1761`):

- Added → `STR_BOOKMARK_ADDED`
- Removed → `STR_BOOKMARK_REMOVED`
- TooLarge → `STR_BOOKMARKS_TOO_LARGE`
- SaveFailed and LoadDisabled → `STR_ERROR_GENERAL_FAILURE`

`LoadDisabled` goes both ways: through `addBookmark`'s popup (`:1791-1794`) and
through `showMessage` (`:1779`).

### BmpViewer (`BmpViewerActivity.cpp:226-236`)

On success it shows `STR_DONE` or `STR_SETTINGS_SAVE_FAILED`, depending on
`SETTINGS.saveToFileAtomic()`. On failure it shows `STR_FAILED_LOWER`. Both
are drawn immediately with `drawPopup`. Then `delay(1000)` holds the screen and
`onEnter()` redraws the viewer. The comment at `:228-229` records why this path
does not post. The hold is 1 s here, not 2.5 s; the issue does not ask to
change it.

### Blocking popups that stay on `drawPopup`

Twenty-five call sites in all (`grep -rn drawPopup src lib | wc -l` → 25,
including the definition and the declaration). These stay:

- `STR_INDEXING`: `EpubReaderActivity.cpp:188,322,1083,1086,1119,1197`
- `STR_INDEX_FAILED`: `:1015`
- `STR_LOADING_POPUP`: `main.cpp:187,200`; `BmpViewerActivity.cpp:99,195`
- the rest are progress or blocking states in other activities

## Status bar geometry (portrait, X4 Pro)

- `getStatusBarHeight()` is `statusBarVerticalMargin` when the text lane is
  visible, plus `progressBarHeightPx + progressBarMarginTop` when the bar is on
  (`UITheme.cpp:138-139`).
- Both themes use `statusBarVerticalMargin = 19` and `progressBarMarginTop = 1`
  (`BaseTheme.h:155,157`; `LyraTheme.h:40,42`).
- `progressBarHeightPx = (thickness + 1) * 2` with thickness 0–2
  (`CrossPointSettings.cpp:257-258`, enum `CrossPointSettings.h:85-88`). That
  is at most 6 px, and 0 when the bar is hidden.
- The worst case is therefore **26 px** of status bar.
- The X4 Pro profile does not override `viewableInsets`: none of the
  `XTEINK_X4_PRO` initialiser lines (`BoardConfig.h:1363-1420`) mention it. It
  inherits the default `{top 9, right 3, bottom 3, left 3}`
  (`BoardConfig.h:625-630`), so the bar's bottom edge sits 3 px above the
  panel edge.
- The status bar text sits at
  `screenHeight - getStatusBarHeight() - orientedMarginBottom - paddingBottom - 4`
  (`BaseTheme.cpp:536`).

A toast whose bounds end at `screenHeight - orientedMarginBottom -
getStatusBarHeight()` clears the bar, with the component's own 16 px margin as
the gap. List screens draw no status bar. Whether they need a different
bottom bound is a spec question. The host build cannot answer it; it needs a
device photo.

## Nearest existing examples

- **Theme method wrapping a FreeInkUI component:** `BaseTheme::drawHeader`
  (`BaseTheme.cpp:284-394`). It builds a `fui::GfxRendererFrame<1>` on the
  render-path stack from `uiScaleSpec()` fonts, and gets the shared tokens
  through `refreshSharedUiThemeTokens(ui.target)` instead of copying
  `ThemeTokens` onto the stack (comment at `:291-296`). It fills `fui::*Props`
  from tokens (`props.styles = tokens.popup`, `:340`) and calls the component.
  `drawToast` should mirror it.
- **Popup-family component styled from the popup metrics:**
  `src/components/OptionPopup.h:193-203`. It takes `fui::defaultPopupStyles()`
  and opts back into `popupFrameThickness` and `popupCornerRadius`, so the
  FreeInkUI dialog matches the legacy `drawPopup` frame. This is the model for
  making the toast look like the theme's popup rather than FreeInkUI's default.
- **Host test for the message path:** `test/posted_message/PostedMessageQueueTest.cpp`,
  with its `CMakeLists.txt` beside it. The timing it pins down does not change
  under this issue.

## Refresh and ghosting: what the code can and cannot tell us

- The toast is drawn and refreshed exactly like the popup: into the B/W buffer
  after the screen's own display call, then FAST. Only its position and size
  differ.
- Dismissal is not a region repaint on either path. In the reader it is a full
  page render (`:504-507`); elsewhere it is the screen's next normal render.
  The issue's worry ("removing a toast means repainting that region") is about
  the FAST waveform leaving a residue where a black frame was. That residue is
  the same for any overlay drawn with FAST. Only the device can say whether a
  bottom-anchored box over reader text leaves a worse ghost than the current
  top one.
- There is no windowed black-and-white update (`.claude/agents/ui-dev.md`,
  "Constraints that bite here"). A "half refresh for the dismissal" fallback
  would mean forcing HALF on the next page render. The reader already has that
  knob: `pagesUntilFullRefresh`, and `forcedRefreshPending` at `:1383-1384`.

## Tool and package versions (as installed)

- PlatformIO Core **6.1.19** (`/Volumes/stein/.platformio/penv/bin/pio --version`).
- Platform: pioarduino `platform-espressif32` **55.03.37** (`platformio.ini:15`).
- Arduino-ESP32 framework **3.3.7**
  (`~/.platformio/packages/framework-arduinoespressif32/package.json`).
- `freeink-sdk` submodule at **`310ec61`** (2026-08-16, "Remove row rectangle
  tracking from list component"), per `git submodule status`. `toast.h` is
  read from that commit.
- C++ standard: gnu++2a (`platformio.ini:40`, per `CLAUDE.md`).

## Open questions for the spec

1. **Bottom bound per screen.** Should the reader pass bounds ending above
   `getStatusBarHeight()`, list screens pass the full viewable area, and
   `drawToast` take an optional bottom inset? Or should `drawToast` always
   subtract the status bar height, at the cost of floating a little high on
   list screens?
2. **Styling.** Should the toast use the theme's popup look (frame thickness,
   radius, inverted text on Classic, as `OptionPopup.h` does)? Or FreeInkUI's
   `tokens.popup`, which `drawHeader` uses? The issue says "the theme's text
   style" and leaves the box open.
3. **The four unlisted `drawNext` callers** (Launcher, FontDownload, ClockSync,
   OtaUpdate) move to the toast with the change. The spec should say whether
   that is intended. OTA and FontDownload repaint a progress bar; is a bottom
   toast clear of it?
4. **Host-testable seam.** A pure function that computes the toast bounds from
   screen height, viewable inset and status bar height can be tested like
   `PostedMessageQueue`, without FreeInkUI. Testing `fui::toast` itself needs a
   fake `DrawTarget`, which no suite does yet.

None of this reaches another surface, changes an on-disk format, or needs a
migration. The change is confined to `src/components/themes` and the three
call sites in `src/activities`, so the `standard` tier stands.
