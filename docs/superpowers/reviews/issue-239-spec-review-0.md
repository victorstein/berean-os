Tier: heavy

# Issue #239 spec review, pass 0

**Reviewed:** `docs/superpowers/specs/2026-09-30-issue-239-design.md` against `gh issue view 239 --repo victorstein/berean-os`
and `docs/superpowers/research/2026-09-30-issue-239-research.md`, on branch `fix/239-sheet-keeps-page` (HEAD `c8aba82d`).

## What was checked and holds

- **The sequencing in A-1 is legal.** In the pop branch, `ActivityManager::loop` sets `currentActivity` to the reader
  (`ActivityManager.cpp:116`), unlocks (`:126`) and then calls the handler (`:127`). The post-handler `requestUpdate()`
  runs only when no action is pending (`:131-133`). The render task takes `RenderLock` and renders `currentActivity`
  (`:47-58`), and it signals the waiter only after `render()` returns (`:60-67`). `requestUpdateAndWait`'s three asserts
  (`:313-320`) cannot fire from a loop-task handler with the lock released. The loop task is not added to the task
  watchdog: `grep -rn "enableLoopWDT\|esp_task_wdt_add" src lib freeink-sdk` returns nothing. So blocking the loop task
  in `ulTaskNotifyTake` is safe.
- **The A-4 per-render flag is needed and placed correctly.** `pageShown` is set only at `EpubReaderActivity.cpp:1392`
  and nothing clears it. Every path that draws no page returns before `:1392`:
  - `showBuildError` at `:1161`, `:1196`, `:1236`, `:1250`, `:1308`, `:1315` and `:1325`
  - the empty-chapter and out-of-bounds messages (`:1343-1358`)
  - the page-load failure (`:1363-1382`)
  - the spine-end return (`:1127-1129`)
  - `ReaderActivity::render`'s end-of-book branch (`ReaderActivity.cpp:168-185`)
- **Night mode matches the sheet.** A-5 tests the same `SETTINGS.screenInverted != 0` condition that `decideMode` uses
  (`EpubReaderMenuActivity.cpp:255-258`). A "rotated" sheet is always newly constructed with `rotated = false`
  (`EpubReaderMenuActivity.h:77`), so skipping the render is correct.
- **The inventory of A-2 and A-3 is complete.**
  - `grep -n "openReaderMenu(" EpubReaderActivity.cpp` finds exactly the six `false` sites the spec names (`:695`,
    `:741`, `:755`, `:782`, `:869`, `:886`) plus the two `true` sites (`:537`, `:553`).
  - `openHighlights` has only three callers: `:794`, `:798` and the entry intent at `:968`. The spec routes the first two
    to `CancelTo::Menu` and the third to `Page`.
  - Bookmarks' `bookmarks.load(...)` stays ahead of the reopen (`:692`).
- **A-3 is within scope.** The issue's own acceptance line names Highlights: "After Go to, Search, Footnotes, Bookmarks or
  Highlights followed by back, the sheet shows the current page above it". Today Highlights-cancel returns with no sheet
  (`:365`). Reopening the sheet follows the owner's text and matches every other menu sub-screen, so it is not a scope
  change that needs a human call.
- **A-8 holds on the success path.** `renderContents` recomputes `chapterPassageCount` (`:1580`), and `renderBook`
  sets `currentPageVisibleOffset` (`:1385`) before `openReaderMenu` reads both under `RenderLock` (`:281-285`). The
  re-render also makes "Tags here" more accurate after a Highlights round trip in which passages were deleted.
- **The claim of no new allocation holds.** The only buffers involved are the existing section load and the sheet's
  existing snapshot (`EpubReaderMenuActivity.cpp:266-277`, freed at `:175`).

## Findings

### MINOR 1: A-7 describes the wrong mechanism for the common "same page" case

- **Claim** (A-7, and step 3 of the data flow): after `releaseSectionKeepingPosition`, a null section "is rebuilt with
  `offsetJump = cachedVisibleTextOffset` … resolved by `getPageForVisibleTextOffset`".
- **Problem:** that is true only when the section cache fails to load. On Go to and Search the render spec is unchanged,
  so the cache normally loads. When it does, `renderBook` clears `cachedVisibleTextOffset` and
  `cachedChapterTotalPageCount` *before* it computes `offsetJump`. `offsetJump` therefore stays empty, and the page comes
  from `nextPageNumber`. `applyDeferredReposition` is then a no-op because both cached values are cleared. The outcome
  is still the same page, because the pagination and the spec are the same. But the stated guarantee is not the one that
  runs. An implementer or device tester who debugs a "neighbouring page" report would look in the wrong place. The
  offset path runs only for Text settings after a spec change (cache miss).
- **Evidence:**
  - `EpubReaderActivity.cpp:1167-1171`: `if (cacheLoaded) { cachedChapterTotalPageCount = 0; cachedVisibleTextOffset.reset(); }`
  - `EpubReaderActivity.cpp:1176-1179`: `offsetJump = cachedVisibleTextOffset;` runs after the reset.
  - `EpubReaderActivity.cpp:1268`: `section->currentPage = nextPageNumber;`
  - `EpubReaderActivity.cpp:1416-1418`: the early return of `applyDeferredReposition`.
- **Fix:** rewrite A-7. With a cache hit (Go to, Search, and Text settings with no change), the page is restored from
  `nextPageNumber` (`:1268`), which `releaseSectionKeepingPosition` saved (`:264`). With a cache miss (Text settings
  with a changed spec), it is restored through `offsetJump = cachedVisibleTextOffset` (`:1178`, `:1273`). Change data
  flow step 3 to match.

### MINOR 2: A-2's "no caller" sentence contradicts the spec's own helper, and two comments go stale

- **Claim** (A-2): "After this change `openReaderMenu(false)` has no caller".
- **Problem:** `reopenReaderMenu()` calls `openReaderMenu(false)` itself in the night branch, and `openReaderMenu(onPage)`
  can pass `false` (spec lines 132 and 146). Two existing comments also describe behaviour that this change reverses. The
  spec's "No change to `EpubReaderMenuActivity`" scope leaves both in place:
  - The header comment on `openReaderMenu` says `pageOnScreen` is "False from a sub-screen's result handler, whose last
    frame is what the framebuffer holds" (`EpubReaderActivity.h:136-137`). After the change, a result handler passes
    `true` when the re-render drew a page.
  - `openHighlights`' block comment says the reuse of `progressChangeResultHandler` is wrong because it "reopens the
    reader menu on cancel, both wrong here" (`EpubReaderActivity.cpp:355-362`). A-3 now makes this handler reopen the
    menu on cancel from the sheet.
- **Evidence:** the lines cited above. `grep -n "openReaderMenu(" src/activities/reader/EpubReaderActivity.cpp` also
  shows that no other site is left.
- **Fix:** change A-2 to say "no sub-screen handler calls `openReaderMenu(false)` directly; only `reopenReaderMenu`
  chooses the flag". Add both comment rewrites to the Architecture section. The `openHighlights` comment should keep
  its point about `bookmarks.load` and drop the menu clause, or rephrase it in `CancelTo` terms.

### MINOR 3: A-6 and A-9 do not say that the loop task is blocked for the whole rebuild

- **Claim** (A-6): on Text settings, the re-render "can show the 'Indexing' popup and take a build … the cost is
  accepted".
- **Problem:** the cost named is time and a popup. It leaves out that `requestUpdateAndWait` blocks the loop task for
  the whole build. That includes the synchronous `buildSomeMore` loop until the target offset is reached
  (`EpubReaderActivity.cpp:1244-1255`). During that time `ActivityManager::loop`, and with it the Home gesture and all
  input polling (`ActivityManager.cpp:73-89`), does not run. Today the same build runs asynchronously after the sheet
  closes, and the loop task stays live. This does not deadlock or trip the watchdog (see "What was checked"). It is
  still a real behaviour difference on the one path that can take seconds, and the spec should state it rather than
  leave it to be found on the device. The same applies, more briefly, to Go to and Search when the cached section is
  partial (`:1210-1255`).
- **Evidence:** `ActivityManager.cpp:322-323` (`xTaskNotify` and then `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`), and
  the build loop cited above.
- **Fix:** add one sentence to A-6. It should say that the loop task is blocked until the reflowed page is drawn, so
  input and Home are ignored during the "Indexing" build. This is accepted because the sheet cannot usefully open
  before the page exists. Add a device step: after changing the font size on a large chapter, confirm that the device
  does not hang and that the sheet opens once the build finishes.

### MINOR 4: when the re-render draws no page, the error handling claims "as today", but "Tags here" becomes 0

- **Claim** (Error handling, and A-8): when the render draws no page, "the sheet opens `Cleared` as today", and without
  a page render both "Tags here" and the Recent chips "already work from cached state".
- **Problem:** `renderBook` zeroes `chapterPassageCount` at `:1341`, before the empty-chapter, out-of-bounds and
  page-load-failure returns (`:1343-1382`). A re-render that ends on the page-load failure therefore reopens the
  cleared sheet with "Tags here (0)" for a chapter that has passages. Today no re-render happens, so the previous count
  survives. The page-load path also resets `section` (`:1370-1371`), and it does so after `releaseSectionKeepingPosition`
  had already recorded the cached position. The Recent chips then rely on whatever `cachedVisibleTextOffset` still
  holds. This is a failure-only edge case, not a regression of the fix.
- **Evidence:** `EpubReaderActivity.cpp:1341`, `:1363-1382`, and `:1480-1482` (the `captureLeftPlace` fallback).
- **Fix:** qualify the Error handling bullet: "the sheet opens `Cleared`; on a render failure, 'Tags here' may read 0
  until the next successful page render". Alternatively, have `reopenReaderMenu` restore the count it read before the
  render when `pageRendered` is false. Either is acceptable. The first needs no code.

VERDICT: CLEAR
