# Issue #239 — research: the reader sheet loses the page after a sub-screen

Branch `fix/239-sheet-keeps-page`, based on `58546aaf` (release 1.29.2). Every `file:line` below was read in this
worktree on 2026-09-30.

## 1. Which files own the behaviour

| File | Role |
|---|---|
| `src/activities/reader/EpubReaderActivity.cpp` | Opens the sheet (`openReaderMenu`, :269), handles every sub-screen result, releases and rebuilds the section, renders the page (`renderBook`, :1116). |
| `src/activities/reader/EpubReaderMenuActivity.{h,cpp}` | The sheet. Decides `OverPage` vs `Cleared` on its first render (`decideMode`), owns the 48 KB-class page snapshot. |
| `src/activities/ActivityManager.cpp` | The push/pop/result-handler sequencing and the render task; decides what the framebuffer holds when the sheet first renders. |
| `src/activities/reader/ReaderActivity.cpp:187` | `ReaderActivity::render` → `renderBook()`; the reader's only render entry. |

Design history: the sheet came from #200 (`a22af1d1`, PR #217). Its spec records the fallback as decision d1
(`docs/superpowers/specs/2026-09-30-issue-200-design.md:44-49`) and lists "page visible on reopen" as a rejected
follow-up with reader-owned snapshot (`:540-542`).

## 2. Current control flow

### 2.1 How the sheet chooses its mode

- `openReaderMenu(bool pageOnScreen)` (`EpubReaderActivity.cpp:269-302`) builds the Tags-here count under `RenderLock`
  from `chapterPassageCount` (:282-285), the Recent chips from `captureLeftPlace()` (:284, :286-287), and pushes
  `EpubReaderMenuActivity` with `pageOnScreen` (:288-291).
- `true` is passed only from the reading surface's own input (:537, :553).
- `decideMode()` runs on the sheet's first render (`EpubReaderMenuActivity.cpp:520-521`). Its reason chain
  (:252-265): `rotated` → `!pageOnScreen` ("not-on-page") → night mode → `!renderer.hasFrameBuffer()` → `!fitsOverPage`.
  With no reason it allocates `snapshotBytes(page.w, page.h)` via `makeUniqueNoThrow` and copies the page region out
  of the framebuffer (:266-277); an OOM or zero-byte read falls back to `reason = "oom"`. It logs
  `sheet mode: cleared|over-page (<reason>)` (:279-281) and `heap before/open` around it.
- `render()` (:518-537) writes the snapshot back or calls `clearScreen()`, then draws the plate. First paint is
  `FAST_REFRESH` over the page, `HALF_REFRESH` for a cleared entry (:533-535).
- `onExit()` frees the snapshot and logs `heap closed` (:174-178). The snapshot lives exactly as long as the sheet.

### 2.2 Every path that reopens the sheet with `pageOnScreen = false`

| Sub-screen (menu action) | Result handler | Releases section first? | On cancel |
|---|---|---|---|
| Go to (`SELECT_CHAPTER`) | `openChapterPicker(CancelTo::Menu)`, :848-874 | **yes**, :856 | `openReaderMenu(false)`, :869 |
| Search (`SEARCH_BIBLE`) | `openBibleSearch(CancelTo::Menu)`, :878-891 | **yes**, :882 | `openReaderMenu(false)`, :886 |
| Footnotes | lambda, :738-747 | no | `openReaderMenu(false)`, :741 |
| Bookmarks | `progressChangeResultHandler`, :691-725 (used at :832-834) | no | `bookmarks.load(...)` then `openReaderMenu(false)`, :692-695 |
| Go to % | lambda, :778-785 | no | `openReaderMenu(false)`, :782 |
| Text settings | lambda, :751-756 | **yes**, :754, on *every* result (it has no confirm) | `openReaderMenu(false)`, :755 |

Two actions in the issue's list do **not** reopen the sheet today:

- **Highlights** and **Tags here** go through `openHighlights` (:354-375), whose handler returns on cancel (:365) with
  no `openReaderMenu` call. `ActivityManager` then `requestUpdate()`s the reader (`ActivityManager.cpp:130-133`), so
  back from Highlights lands on the **page with no sheet**. The page is visible, but it is not "the sheet shows the
  page above it". The spec has to decide whether Highlights-cancel should start reopening the sheet (a behaviour
  change) or whether the acceptance line is already met for it.
- **Tag (`HIGHLIGHT_PASSAGE`)** goes through `openHighlightPassage` (:326-352), whose handler only `requestUpdate()`s
  (:351): back to the page, no sheet. Not in the issue's list.

### 2.3 Why the page is blank after Go to → back → back

1. The sheet finishes with `SELECT_CHAPTER`; `onReaderMenuConfirm` → `openChapterPicker(CancelTo::Menu)`.
2. `releaseSectionKeepingPosition()` (:258-267) stores `cachedVisibleTextOffset` (via `rememberCurrentContentOffset`,
   :1462-1467), `cachedSpineIndex`, `cachedChapterTotalPageCount`, `nextPageNumber`, then `section.reset()`.
3. `BibleNavigationActivity` renders over the whole framebuffer.
4. On cancel, `ActivityManager::loop` pops it, makes the reader `currentActivity`, **unlocks** `RenderLock` and calls
   the handler (`ActivityManager.cpp:115-127`). The handler calls `openReaderMenu(false)`, which sets a pending push,
   so the "re-render the popped activity" `requestUpdate()` is skipped (:130-133). The reader never renders.
5. The sheet's first render sees `pageOnScreen == false` → `Cleared` → `clearScreen()` + HALF refresh. The owner's
   photo is exactly this.

### 2.4 What the fix can lean on

- **Position survives the release.** `renderBook` rebuilds a null section from the cached position:
  `offsetJump = cachedVisibleTextOffset` when `currentSpineIndex == cachedSpineIndex` and no page/anchor jump is
  pending (`EpubReaderActivity.cpp:1173-1179`), then `getPageForVisibleTextOffset` (:1272-1276). This is the path the
  `CancelTo::Page` entry intents already use (comment at :868) — so "the same page, not a neighbouring one" is the
  existing guarantee of the visible-text-offset rebuild, not something new.
- **Recent chips survive the release.** `captureLeftPlace` (:1469-1499) falls back to `cachedVisibleTextOffset` when
  `section` is null (:1480-1482). So the chips are already correct after a round trip; a re-render leaves them
  correct because `renderContents` sets `currentPageVisibleOffset` to the same page.
- **Tags here survives the release** only because nothing resets `chapterPassageCount` between the last render and the
  reopen; `renderBook` zeroes it (:1341) and `renderContents` recomputes it (:1580). A re-render recomputes it for the
  same spine, so it stays correct.
- **A synchronous render from a loop-task callback has precedent.** `Activity::requestUpdateAndWait()`
  (`Activity.cpp:11` → `ActivityManager.cpp:296-326`) notifies the render task and blocks until it finishes. Its three
  asserts forbid the render task, a second waiter, and holding `RenderLock`. The result handler runs with the lock
  released (`ActivityManager.cpp:125-126`) and `currentActivity` already set to the reader (:116), so a
  `requestUpdateAndWait()` there renders the **reader's** page. Existing uses from the loop task with no lock held:
  `HighlightsActivity.cpp:441` (whose comment at :434-440 walks the same assert reasoning) and
  `PassageSelectActivity.cpp:310`.
- **The framebuffer holds the page after `renderBook`.** #200's spec A-6 (`2026-09-30-issue-200-design.md:195-198`)
  established that every reader render path ends with the framebuffer matching the panel, which is what the
  from-the-page sheet already relies on.

### 2.5 Costs the spec must weigh

- **Two refreshes instead of one.** `renderBook` displays the page itself (`renderContents`'
  `displayBuffer`/grayscale pass, :1559, :1608), then the sheet's first paint is a FAST refresh of the sheet region.
  Today's cleared path is one HALF refresh. The re-render also pays a section reload from the SD cache
  (`loadSectionFile`, :1167) on the three release paths, and may trip the periodic full refresh via
  `pagesUntilFullRefresh`.
- **Text settings changes layout.** After `TextSettingsActivity` the section is released because the render spec may
  have changed; a re-render there reflows the chapter with the new settings, possibly with an "Indexing" popup
  (:1223-1226) and a long build. That is a real, visible cost on that one path.
- **Night mode and rotation stay cleared** — `decideMode` still returns `night` / `rotated`; a re-render does not
  help there, and the issue keeps the fallback for them.
- **Build failure.** If `renderBook` fails (`showBuildError`, :1119-1123), the framebuffer holds the error popup, not
  a page; passing `pageOnScreen = true` blindly would snapshot the error. The reader has `pageShown`
  (`EpubReaderActivity.h:55`, set at :1392) but it is sticky for the session, not per-render.

## 3. Installed tool and package versions

| Tool | Command | Output |
|---|---|---|
| PlatformIO Core | `~/.platformio/penv/bin/pio --version` | `PlatformIO Core, version 6.1.19` |
| Platform | `grep platform platformio.ini` | `platformio.ini:15` pioarduino `platform-espressif32` `55.03.37` |
| Arduino-ESP32 | `framework-arduinoespressif32/package.json` | `"version": "3.3.7"` |
| CMake (host tests) | `cmake --version` | `cmake version 4.4.2` |

No new library or SDK API is involved: `requestUpdateAndWait`, `readFramebufferRegion`/`writeFramebufferRegion` and
`makeUniqueNoThrow` all already exist and are in use (cited above).

## 4. Nearest existing example of this kind of change

- **The mechanism:** `HighlightsActivity.cpp:434-442` — force one synchronous repaint of the current activity from a
  loop-task callback so that what is drawn next sits over a known-clean frame. The same shape here: in the cancel
  handler, `requestUpdateAndWait()` renders the reader's page, then `openReaderMenu(true)` pushes the sheet over it.
- **The rebuild-on-cancel:** `openChapterPicker`'s own `CancelTo::Page` path (:866-869) — cancel returns, the popped
  reader re-renders from the cached position.
- **The heap logging the acceptance asks for** already exists: `EpubReaderMenuActivity::logHeap` at `before`, `open`
  and `closed` (:207-211, :251, :281, :176), internal and PSRAM.

## 5. Tests

There are no host tests for activities; `test/` covers pure logic. The sheet's only host suite is
`test/ui_layout/ReaderMenuSheetLayoutTest.cpp` (geometry). The mode decision and the handler sequencing live in
activity code that `test/CMakeLists.txt` does not build. If the spec wants TDD coverage, the testable seam would be a
pure function extracted from the decision ("reopen over page?" given release/night/rotation/render-success), in the
style of `ReaderEntryIntent::route` (used at `EpubReaderActivity.cpp:942`). Everything else is device-verified via the
`MENU` log lines above.

## 6. Tier

Stays `heavy`. The change is confined to the `ui` surface (`src/activities/reader/`): no on-disk format, no store, no
contract with another surface, no migration. Not raised.

## 7. Open questions for the spec

1. Highlights / Tags here / Tag: should back from them start reopening the sheet (behaviour change), or does "page
   visible" already satisfy the acceptance line for them? (§2.2)
2. Text settings: re-render (reflow, possibly long) before the sheet, or keep the cleared sheet there? (§2.5)
3. How the reader knows a render actually produced a page before claiming `pageOnScreen = true`. (§2.5)
