Tier: heavy

# Issue #200 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-30-issue-200-design.md` against `gh issue view 200 --repo victorstein/berean-os`
and `docs/superpowers/research/2026-09-30-issue-200-research.md`, on `feature/200-reader-menu-sheet` (base `67eaaf1e`).

## What holds up

I checked these against the code and found them correct:

- **The snapshot premise.** `restoreBwBuffer` frees the chunks after one use and rewrites the controller baseline
  (`GfxRenderer.cpp:2348-2354`, `FreeInkDisplay.cpp:868-877`). So A-1 (the menu owns its snapshot) is justified.
- **The async refresh.** The reader's only async refresh is the tiled grayscale path
  (`EpubReaderActivity.cpp:1369`, `overlapRefresh = tiledGrayscale && …`). That path always ends in
  `cleanupGrayscaleWithFrameBuffer()` (`:1487,1507,1536`). So A-6's "framebuffer == controller baseline" holds on the
  reader paths I traced. The toast (`BaseTheme.cpp:547`) and popup (`:498`) both display what they draw.
- **Night mode.** Polarity is applied per render at output (`ActivityManager.cpp:58`, `FreeInkDisplay.cpp:577-579`),
  so A-7 is right.
- **Back and Home.** The left-edge swipe is already Back (`MappedInputManager.cpp:222-227,264-265`), and Home already
  closes the menu (`EpubReaderMenuActivity.cpp:92-95`).
- **Two columns.** `setContentMargin` rebuilds `content_` from `frame.safeRect()` (`FreeInkApp.h:65-67`), and
  `list(props, h)` takes its band from the top of that (`:301`). So A-17's per-column technique works mechanically.
- **Safe area.** Touch zeroes `buttonHintsHeight` (`UITheme.cpp:52-54`). So `getScreenSafeArea(…, true, false)` is the
  whole 480×800 screen, and the A-16 arithmetic holds: 44 + 8 + 77 + 8 + 300 + 8 ≈ 445 px, which puts the sheet top at
  about y = 355.
- **PSRAM placement and the heap log.** `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096` is in the S3 `qio_opi`
  `sdkconfig.h`, at `:1040-1041` (the research cites `:1072-1075`, which has drifted). `ESP.getFreeHeap()` is internal
  only (`Esp.cpp:163-164`), so the V-2 log can tell the two pools apart.
- **Enum alias.** Every external use is `EpubReaderMenuActivity::MenuAction::X` (`EpubReaderActivity.h:114`,
  `EpubReaderActivity.cpp:283,710-851`), so the alias in A-10 is source-compatible.

## Findings

### MAJOR 1: every reopen after a sub-screen gets the blank, scrolling menu, which misses the issue's main criterion; the rejected alternative was not the only one

**Claim (A-5, Goal 6, A-22).** Every result handler that reopens the menu passes `pageOnScreen = false` and gets
today's full-screen menu, which "may scroll". Repainting first was rejected because three of those paths release the
section, so a repaint would mean a rebuild. The spec leaves the rest to "a follow-up if the owner wants the sheet
there".

**Problem.**
- These are the most common ways the menu comes back: cancel from Go to, the primary quick action; from Search; from
  Footnotes; from Go to %; from Bookmarks; and any return from Text settings
  (`EpubReaderActivity.cpp:675-677,728-731,745-748,757-760,771-774,798-801`).
- On each of them the user gets the blanked page and a list that still scrolls. The Bible's flat model is 4 quick
  actions + 11 rows = 15 items (A-13). A-22 renders them as one list, and the page holds 14 rows after #197
  (`git show 3591e3fe`: "Reader menu … 743 → 14").
- The issue's goal is "Stop the reader menu from blanking the page and scrolling". Its first acceptance criterion is
  "Every menu item is reachable without scrolling", with no exception. It also asks to "Check the
  `EpubReaderActivity` handoff so the page isn't cleared before the menu opens."
- The rejection rests on a false dichotomy: repaint the page, or go full-screen. Two cheaper options were not
  considered:
  - **(a) A snapshot the reader owns and that survives the sub-screen.** The reader stays alive on the activity stack
    (`ActivityManager.cpp:151-155`), and it outlives the menu. The reader holds the `PageSnapshot`, the menu captures
    into it through a pointer on its first render (A-4 is unchanged), and the reader releases it on its next
    `renderContents`. On the Go to, Search, Footnotes and Go to % cancel paths the page has not changed, so the snapshot
    is still valid, and the released section does not matter because nothing is re-laid-out. The panel shows the
    sub-screen there, so the first sheet paint is a HALF. That is exactly the "clean entry" case the issue's refresh
    rule allows. The cost is 48 KB of PSRAM held while the sub-screen is open. Bookmarks and Text settings can change
    what the page shows, so they would still fall back.
  - **(b) The fallback uses the sheet layout, not today's list.** When no page is available, clear the screen and draw
    the same two-column layout. Then no path scrolls, including OOM and night mode. The trade is that it departs from
    the issue's literal "use the full-screen menu".

**Evidence.**
- A-22 says the fallback "may scroll".
- Goal 6 adds "the framebuffer is not known to hold the page" as a fallback trigger, and A-5 routes all six reopen
  paths to it.
- `buildMenuItems` today yields 15 items for a Bible (`EpubReaderMenuActivity.cpp:41-82`).

**Fix.** The owner has to choose: accept the scrolling full-screen menu on reopen, or take (a) or (b). The spec then
records that choice in A-5 and A-22 and adds a matching device step to V-4. If (a) is chosen, A-1 and A-24 move the
snapshot's ownership to the reader, and the first paint on a reopen is a HALF. This reverses A-1's ownership and
narrows or keeps the issue's acceptance criterion, so it is the owner's decision, not an inline fix.

### MAJOR 2: `screen.button(props, rect)` with an icon cannot draw the tile the spec sizes

**Claim (A-16, A-17 step 3.5, "Design at a glance").**
- Tiles are `screen.button(props, rect)` with an icon, modelled on `BibleSearchActivity.cpp:771-780`.
- `tileHeight = max(minTouchSize, iconSize 32 + body lineHeight + 2·gap)`, which is about 77 px: icon **above**
  label, as in the mockup (research §8, "icon above label").

**Problem.**
- `fui::button` lays an icon and a label out **side by side**, not stacked.
- So the geometry A-16 computes (and the layout and `fits` tests assert) describes a tile the named API does not
  produce. Implemented as written, you get a 77 px-tall button with the icon and label in one horizontal line.
- The label also has to fit in about 98 px (480 / 4 minus `content.inset(2,4,2,4)`) beside a 32 px icon and a 4 px
  gap, which leaves about 62 px. Spanish "Buscar" and "Marcar" are near that limit.
- The cited precedent passes no icon at all.

**Evidence.**
- `freeink-sdk/libs/ui/FreeInkUI/include/components/controls/button.h:63-73`:
  `totalW = iconW + props.gap + labelSize.width`, and the icon is drawn at `x`, then the text at
  `x + iconW + props.gap`, both vertically centred in the same content rect.
- `BibleSearchActivity.cpp:768-780` sets only `label` and `action` on its `ButtonProps`.

**Fix.** State how the tile is put together:

- A `screen.button` with no label and no icon supplies the hit area (with `minTouchSize`), the border and the
  state styles.
- The icon and label are then drawn stacked inside the same rect with `screen.target().bitmap(...)` and
  `screen.target().text(...)`, using the foreground of the resolved state style, so a focused or flashed tile stays
  legible.

Alternatively, accept the horizontal layout and recompute `tileHeight` as `max(minTouchSize, max(32, lineHeight) + pad)`.
In that case add a label-width check at quarter width to `ReaderMenuSheetLayoutTest`. Either way, correct the "Modelled
on" row.

### MINOR 3: `ReaderMenuModel` is called pure and host-tested, but its inputs are not

`buildMenuItems` reads `Frontlight.present()` and `#if BEREAN_CAP_ROTATION` (`EpubReaderMenuActivity.cpp:70,73`), and
it carries `StrId` labels. No host suite uses `StrId` or the generated `I18nKeys.h`: `grep -rln "I18nKeys\|StrId" test`
returns nothing. The data flow only says `ReaderMenuModel::build(...)`.

**Fix.** Make `hasFrontlight` and `hasRotation` explicit `build` inputs, keep label ids out of the model (the activity
maps action to `StrId`), and say so in the Design table.

### MINOR 4: when labels are set is internally inconsistent

- The data flow sets "row labels set once" in the constructor (line 320, as `buildMenuRowItems` does today at
  `:26,32-39`). But the mode is only decided on the first render, and it can later drop from sheet to full-screen.
- A-15 makes the Auto turn label depend on the mode (`STR_AUTO_TURN` in the sheet, `STR_AUTO_TURN_PAGES_PER_MIN`
  full-screen).
- A-22 renders the quick-action items as list rows in the fallback without saying which labels they carry: the tile's
  "Go to" and "Mark", or today's "Select Chapter" and "Toggle bookmark".

**Fix.** Set labels when the mode is decided, and whenever it drops. Name the fallback labels for the four quick
actions.

### MINOR 5: state shared across tasks without the lock

- A-9 releases the snapshot inside the `ROTATE_SCREEN` popup callback. That runs on the loop task
  (`OptionPopup::handleInput` → `onSelectCallback`, `OptionPopup.h:78-82`; callback at
  `EpubReaderMenuActivity.cpp:107-115`) with no `RenderLock`, while the render task may be inside `restoreInto`.
- The sheet/full-screen mode flag is written on the render task (data flow: decided on the first render) and read on
  the loop task (A-20's `navigateButtons` override, and routing).

**Fix.** Take `RenderLock lock(*this)` around the release and the mode change, as `moveSelectionTo` does
(`UiListActivity.cpp:76`), and make the mode a `std::atomic`. The rotation path is compiled out on the X4 Pro, but the
spec claims it is "here for correctness".

### MINOR 6: A-14 holds `RenderLock` across a call that can take `RenderLock` itself

- `passagesInDocument` → `UnitIndexCache::unitText` → `SpineHtmlStream::stream(..., WhenMissing::Inflate)`
  (`UnitIndexCache.cpp:388-397`, default argument in `SpineHtmlStream.h`).
- When the chapter's HTML cache is missing, `stream` constructs a second `RenderLock` (`SpineHtmlStream.cpp:37`). That
  is a plain `xSemaphoreTake` (`ActivityManager.cpp:328`), so it deadlocks.
- The same branch also draws the "Indexing" popup and takes a `FrameBufferLoan`, which hands the framebuffer back
  **white** (`GfxRenderer.h:342-348`). That happens immediately before the menu captures.
- The page render at `EpubReaderActivity.cpp:1398` already relies on the same precondition, and it has just succeeded
  for this spine, so this is latent rather than live. The spec should still state it and guard it.

**Fix.** Either compute `n` only on a path that cannot inflate (for example, a `WhenMissing::Fail` variant), or record
the precondition in A-14.

Also:
- `PaintedPassage` is 16 B on the 32-bit target, not 24 (`StudyStore.h:107-113`).
- The `spineFilter` rebuild in `HighlightsActivity`'s delete path runs under `RenderLock` (`HighlightsActivity.cpp:407`)
  and carries the same precondition.

### MINOR 7: icon churn and a duplicate symbol

- **Duplicate symbol.** `src/components/icons/search32.h:17` already defines `static const freeink::Icon icon_search_32`.
  That file is orphaned (no `src/` file includes it, nor `search24.h`). Adding `search = search` to the manifest emits
  a second `icon_search_32` into `listIcons.h`, so any translation unit that includes both fails to compile.
- **Regeneration churn.** A full regeneration with a freshly installed `rsvg-convert` (Resources) can change the bits
  of the 13 existing icons.
- **Formatter.** The manifest's recipe says `clang-format -i` (`listIcons.manifest:6`), which CLAUDE.md forbids.

**Fix.**
- Delete `search24.h` and `search32.h` in the same change.
- Require the existing icon arrays to be byte-identical after regeneration, and revert them if they are not.
- Format with `./bin/clang-format-fix`.

### MINOR 8: guard taps cause needless refreshes, and handler registration is unstated

- **Needless refreshes.** `FreeInkApp::route` calls `invalidate(Fast)` and arms a tap flash for **any** routed event
  (`FreeInkApp.h:729-741`), including `ACTION_CHROME`. `routeListTouch` then calls `requestUpdate()` whenever the app is
  invalidated (`UiListActivity.cpp:68`). So every tap on a plate gap costs a full restore and a FAST refresh.
- **Handler registration.** `ACTION_CLOSE` needs a handler registered with `app.on` after `UiListActivity::onEnter`'s
  `resetUi()`. The menu does not override `onEnter` today, and the spec doesn't mention one.

**Fix.** Add an `onEnter` override that registers `ACTION_CLOSE`. In sheet mode, route through `handleCustomInput` with
`UiAppHost::routeTouch` so an `ACTION_CHROME` event is consumed without a render.

Related: the data flow allocates 48 KB and only then computes `fits`. Compute the layout first, since it needs only the
counts and metrics, so a sheet that can't fit never allocates.

## Verdict rationale

There are no BLOCKER-severity defects. MAJOR 2 and all the MINORs can be fixed inline. MAJOR 1 cannot: whether the
menu may fall back to a blank, scrolling screen on every sub-screen return narrows the issue's acceptance criterion
that every item is reachable without scrolling. The spec itself defers that to "if the owner wants". Its fixes also
reverse A-1 (who owns the snapshot) or reinterpret the issue's "use the full-screen menu" fallback. That is the owner's
judgment, so the verdict is BLOCKER.

VERDICT: BLOCKER
BLOCKERS: 0
MAJORS: 2
