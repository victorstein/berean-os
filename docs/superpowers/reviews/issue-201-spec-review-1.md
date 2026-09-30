Tier: heavy

# Issue #201 spec review, pass 1

Reviewed: `docs/superpowers/specs/2026-09-30-issue-201-design.md` at `39a4ecba`, against issue #201
(`gh issue view 201 --repo victorstein/berean-os`), the research note, the pass-0 review, the mockup
the issue cites (artifact `4qvfHbNnJ77F2gM5DELQrQ`, read with the Artifact tool), and the code at
`a22af1d1` (the worktree's base). `main` has since moved to `06e52c84`. See finding 6.

## How the pass-0 fixes landed

| Pass-0 finding | Applied? | Check |
|---|---|---|
| 1 BLOCKER: mockup mismatch | Yes | A7/A14/A17/A18 now match the mockup's `menuProposed` (the `RECENT` caption at `top+142`, content-sized pills `'Rev 21:4', 'Gen 1', 'Ps 83:18'`) and `homeProposed` (`'Revelation 21:4', 'Genesis 1', 'Psalm 83:18'`, "Continue · Isaiah 40:31"). The chip abbreviations are new work, and they introduce finding 1. |
| 2 MAJOR: unit cache off-lock, build under lock | Yes, for capture | Capture sits inside the existing `RenderLock` scopes. The lines are right: `EpubReaderActivity.cpp:919/926/937/952/962/1665`, `clearDeferredReposition` at `:1681`, lock scope closing at `:1700`. `peekUnits` never reaches `buildDocument` (`UnitIndexCache.cpp:190`, `:240`). The one remaining off-lock unit-cache access is chip resolution, which is finding 2. |
| 3 MAJOR: refused store grows in RAM | Yes | `record()` returns early while `loadRefused` (`PersistableStore.h:52`), and a host case covers it. |
| 4–7 MINOR | Yes | There is a `LOG_DBG` dump. `labelFor` and `MenuResult` plumbing are listed. The store self-loads lazily. The end-of-book branch `:925-929` is a capture site. |

## Findings

### 1. MAJOR: loading the abbreviation table at menu open can inflate the book-nav page, and the inflate hands the menu a blank framebuffer to snapshot

**Claim.** A17 says `openReaderMenu` loads a transient `BibleBookNameTable` "outside the lock",
before the menu is constructed. The book-nav page is "already cached on SD once the grid or search
has opened it". Spec lines 136-143 and 358-364.

**Problem.** `BibleBookNameTable::load` streams with the default `WhenMissing::Inflate`
(`BibleBookNameTable.cpp:39`). When the HTML cache is missing, `SpineHtmlStream::stream` takes a
`GfxRenderer::FrameBufferLoan` (`SpineHtmlStream.cpp:34-40`). That lends the framebuffer's bytes to
the inflate as scratch memory, and "restore returns the buffer white, so the caller must redraw the
full screen" (`GfxRenderer.h:342-348`, `GfxRenderer.cpp:135-155`). The menu's first render then
snapshots the page from that framebuffer. Its comment says this is "the one moment the page can be
saved" (`EpubReaderMenuActivity.cpp:199-201`, `:247-255` `readFramebufferRegion`), and the lent
bytes still look like a valid buffer, so the snapshot succeeds. The sheet then paints a blank or
garbage page area above itself, and the differential refresh erases the page the reader was
looking at.

The HTML cache is not reliably warm:
- It lives in `epub->getCachePath() + "/html/"` (`Section.cpp:477-478`).
- It is wiped by the reader's own "Delete cache" (`EpubReaderActivity.cpp:838`) and by any
  book-cache version bump.
- The book map that first streamed the nav page persists separately, in
  `sdpaths::UNITS_DIR` (`UnitIndexCache.cpp:48`), so it is never rebuilt and never re-warms the
  cache.
- The chapter-number lookup deliberately uses `WhenMissing::Fail` (`EpubReaderActivity.cpp:1600-1608`).

So after a cache clear, the first menu open that has chips shows this bug, and it keeps happening
until the user opens the grid or search.

**Fix.**
- Add a `SpineHtmlStream::WhenMissing` parameter to `BibleBookNameTable::load`, defaulting to
  `Inflate` so the existing callers are unchanged.
- In `openReaderMenu`, pass `WhenMissing::Fail`. A17's existing fallback (the chip shows `r`)
  already covers a failed load.
- Record in A17 that a stream from `openReaderMenu` must never borrow the framebuffer.
- Device check: Delete cache, reopen the Bible, turn a chapter, open the menu. The page stays
  visible above the sheet.

### 2. MINOR: chip resolution touches the unit cache off-lock while the reader is current, and the precedent cited for it is the wrong one

**Claim.** `locatePlace` "is called only from loop-task code that holds no lock (the chip handler,
the intent consumer) — the same conditions under which `locate()` builds today" (spec 282-285).

**Problem.** `locate()` runs from `HighlightsActivity`, and while that activity is current the
render task renders the highlights screen, not the reader. The chip handler runs in the menu's
result handler. That handler is invoked after `lock.unlock()`, with the reader already restored as
`currentActivity` (`ActivityManager.cpp:115-127`). Any render the render task picks up in that
window runs `passagesInDocument` → `unitsFor` (`EpubReaderActivity.cpp:1396-1401`,
`StudyStore.cpp:215`). That is the same `cached_` that `locateUnit` → `unitsFor` rewrites
(`StudyStore.cpp:162-183`, `UnitIndexCache.cpp:225-247`). Today the window is narrow:
`requestUpdate()` only notifies at the end of `ActivityManager::loop` (`:167-172`), after the
handler has run. But it is an unstated invariant, not the precedent the spec gives. The accurate
precedent is `STUDY.repairTexts()` in `loadBook` (`EpubReaderActivity.cpp:220-224`), which runs
off-lock in `onEnter` with the reader already current. That precedent covers the intent consumer,
not the chip.

**Fix.**
- Resolve the hint first inside a `RenderLock`, using `study::documentOffsetOf(peekUnits(spineHint), unit)`.
  This path cannot build.
- Fall back to the unlocked, possibly-building `locatePlace` only on a miss (a stale edition).
- State the invariant that makes the fallback safe: no render is requested before the handler
  returns.
- Cite `repairTexts` as the precedent, not `locate()`.

### 3. MINOR: the spec's own device check expects "Genesis 1:1", but its own A2 makes that place chapter-only

**Claim.** The device check (spec 445-447) expects that after "Revelation 21 → (Go to) Genesis 1 →
Home" the last `PLC saved:` line reads `Genesis 1:1 | Revelation 21:… | …`.

**Problem.** A chapter picked from the grid opens at page 0. In NWT markup, visible text comes
before `chapter1_verse1`: the `w_navigation` line and the `<h1>` book heading
(`test/bible_search_scanner/fixtures/genesis1.xhtml:9-12`). The page-0 offset of 0 therefore
precedes the first anchor, `resolve` falls back (`UnitAnchors.cpp:88-94`), and A2/A7 record the
chapter-only "Genesis 1". The spec's own JSON example (spec 202) shows this, and so does the "Gen 1…"
chip in the next sentence. A tester following line 446 would report a false failure.

**Fix.** Make the expected line `Genesis 1 | Revelation 21:… | …`.

### 4. MINOR: the budget table's per-entry overhead is 30 bytes, not 31, so the "measures exactly" test fails if the table is copied

**Claim.** Per entry `{"u":"","r":"","c":,"s":,"o":}` is 31 bytes, for a total of
`19 + 11 + 12 × 175` = **2,130** (spec 235, 241). The test asserts that a worst-case document measures
exactly `SAVE_BUDGET` (spec 424).

**Problem.** `python3 -c "print(len('{\"u\":\"\",\"r\":\"\",\"c\":,\"s\":,\"o\":}'))"` prints `30`.
The wrapper (19) and the `u` maximum (28) check out. The correct total is `19 + 11 + 12 × 174` =
**2,118**. Because `RecentBooksDoc`'s convention is an exact-equality test
(`RecentBooksDoc.h:64-66`), an implementer who transcribes 31 gets a red test and no guidance on
which side is wrong.

**Fix.** Change the row to 30, the per-entry total to 174 and the total to 2,118.

### 5. MINOR: the issue's "No PSRAM use" is departed from without saying so

**Claim.** The issue's heap criterion says "about 1–1.5 KB RAM while held. No PSRAM use." The
mockup says the same ("No PSRAM"). A17 and the Memory section add a ~4.2 KB table per menu open,
which lands in PSRAM because it is over the 4,096-byte auto-routing threshold.

**Problem.** The cost is justified: the chips need the abbreviations, the table lives only for
the duration of the call, and the sheet already holds a PSRAM snapshot. But it is a departure from
a literal acceptance line, and it is not labelled as one, although every other departure is (A8,
A16). The reviewer of the PR will read "No PSRAM" against a log line that shows PSRAM moving.

**Fix.** Add one sentence to A17 or the Memory section: "The issue's 'No PSRAM use' is read as the
store's resident cost. The only PSRAM this adds is transient, at menu open, for the mockup's
abbreviated chips." Repeat it in the PR body.

### 6. MINOR: the spec calls `a22af1d1` "current `main`", but `main` has moved

**Claim.** "All `file:line` citations are at `a22af1d1` (current `main`)" (spec 4-5).

**Problem.** `git log --oneline a22af1d1..main` shows `06e52c84` (#219, cover mastheads) and the
1.25.0 release. #219 edits `BibleNavigationActivity.{h,cpp}`, `MeetingsActivity.cpp` and
`PublicationsActivity.cpp`. Two of the eight `goToReader` call sites have moved on `main`:
- `MeetingsActivity.cpp:408` → `:434`
- `PublicationsActivity.cpp:191` → `:236`

None of the spec's `EpubReaderActivity`, `ActivityManager` or store citations changed
(`git diff --stat a22af1d1 main` does not list them).

**Fix.** Rebase the branch before planning, drop the words "current `main`", and re-cite the moved
call sites. `BibleNavigationActivity`'s constructor, which `openChapterPicker` reuses, must be
re-read on the rebased tree.

### 7. MINOR: A20 moves an issue acceptance criterion to #203 without leaving anything testable in #201

**Claim.** The issue's acceptance criteria include "each intent opens the right target". A20 says
#201 verifies intents "by review of the consumption code", and that #203 exercises them on device.

**Problem.** Pass 0 offered this deferral as one option. As written, though, #201 merges a
cross-activity contract with no executable check of its dispatch. The A13 rules are the part most
likely to be wrong: `Search` or `OpenAt` on a non-Bible book is dropped, and `BookGrid` falls back
to the TOC. They are pure logic.

**Fix.** Extract the dispatch into a pure function, for example
`ReaderEntryIntent::route(Kind, bool isBible) -> {Ignore, Locate, ChapterGrid, TocList, Search, Highlights}`,
beside `ReaderEntryIntent.h`. Host-test it in `test/ui_layout`, which is already the reader's
Arduino-free suite. The PR body should say that on-device intent checks are #203's.

## Checked and holds

- The dedupe, eviction, head-equal skip and version refusal paths. `isKnownFormatVersion` with
  `| 0` refuses an absent `v`, and `loadRefusedAfter` keeps a `Missing` file writable
  (`FormatVersion.h:14-33`).
- The lazy self-load in A15. A parse error or unreadable file is not refused, and `places.json`
  then gets overwritten. That is acceptable for a recent-places list, and the spec says it is
  base-class behaviour.
- The `peekUnits` contract: `readEntry` and `loadAnchors` only read SD, and `loadAnchors` stamps
  `book` from the entry (`UnitIndexCache.cpp:124-173`).
- A2's chapter-only mapping: `resolve` falls back before the first anchor, and `documentOffsetOf`
  resolves the first anchor at offset 0 (`UnitAnchors.cpp:88-120`).
- One document per chapter: 1,189 Verse documents (`Unit.h:18-19`), so (book, chapter) dedupe
  equals spine dedupe.
- The exit site:
  - Both the replace path and the stacked path hold `RenderLock` while calling `onExit`
    (`ActivityManager.cpp:139-150`, `:175-181`).
  - `section` and `epub` are still alive there.
  - A capture on the end-of-book branch plus the out-of-range guard in `onExit` records the last
    chapter exactly once.
- Intent consumption from `onEnter`:
  - The lock is released first (`:159-160`).
  - A sub-activity started there is pushed in the same `ActivityManager::loop` pass, before the
    end-of-loop render notify (`:162`, `:167-172`), so the reader page never flashes.
  - After a pop the reader is re-rendered (`:130-133`), which A12 relies on.
- Chip hit-testing. Hits registered after the plate guard win, because routing is newest-first
  (`EpubReaderMenuActivity.cpp:396-400`). `ACTION_USER + 2` is free (`EpubReaderMenuActivity.h:40-41`).
- Sheet fit with the band, on the host-test metrics (`ReaderMenuSheetLayoutTest.cpp:12-28`):
  451 + 44 + 8 = 503 ≤ ⅔ × 800.
- `STR_LINK_TARGET_NOT_FOUND` exists (`english.yaml:404`). `STR_RECENT` does not, so it is a
  hand-off.
- `sizeof(Place)` and SSO arithmetic ("Revelation 21:4" is exactly 15 B). The resident cost is
  internal SRAM, well under the auto-routing threshold.

VERDICT: CLEAR
