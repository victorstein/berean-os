Tier: standard

# Spec review 0: issue #238 (Go to latency)

Spec: `docs/superpowers/specs/2026-09-30-issue-238-design.md`
Research: `docs/superpowers/research/2026-09-30-issue-238-research.md`
Branch `perf/238-go-to-latency` at `19ab5b71`; `origin/main` at `4c0b8cb5`.

## What holds up

The core of the design checks out against the code and the user's EPUB. Nothing below touches it.

- **A2 (book spines from the TOC).** `book.bin` is built from the EPUB 3 nav first
  (`Epub.cpp:447-465`). Each `TocEntry.spineIndex` comes from an exact match of the normalised href
  against the spine, through the FNV hash index because the spine has at least 400 items
  (`BookMetadataCache.cpp:111-128, 413-449`). Both sides are normalised the same way
  (`TocNavParser.cpp:132`, `ContentOpfParser.cpp:191`). I simulated this over
  `/Volumes/stein/Downloads/nwt_S.epub`: all 66 `<a>` targets in `biblebooknav.xhtml` have a
  matching TOC entry, and the first-match TOC spine equals today's spine-walk index for **every**
  target (the diff came back empty). The trimmed `x4pro-upload/nwt_S.epub` has 129 TOC links and
  is missing none of the 66.
- **A3 (walk from the nav page).** For all 61 chapter-nav books, every chapter lies after its nav
  page: the minimum offset is +1 and the maximum is +150 (Psalms, 543 → 693). Genesis runs 29 → 79
  and Revelation 1,321 → 1,343. So "≤ 151 spine reads" is exact.
- **A6 (loop task only).** `activateIndex` is reached from `handleButtons` and from the row-action
  trampoline, which `routeListTouch` dispatches inside `UiListActivity::loop`
  (`UiListActivity.cpp:32-56, 60-69, 84-87`). `onEnter` runs on the loop task
  (`ActivityManager.cpp:158-160`). No lock is needed.
- **A4 sizes.** `BibleBookNameTable` is 4,228 B, and the rest of the book table is
  132 + 66 + 1,056 + 4 × 48 + 16 + 8 B, with `BookGrid::MAX_SECTIONS = 4` at
  `BookGridLayout.h:24`. That totals ~5.7 KB. The `sdkconfig` threshold lines are as cited
  (`framework-arduinoespressif32-libs/esp32s3/sdkconfig:2153-2154`).
- **Cost 4 (FAST on entry)** and `displayBuffer` being blocking (`GfxRenderer.cpp:1724-1728` →
  `HalDisplay.cpp:60-66`) are both as stated. Log lines carry `millis()` timestamps
  (`Logging.cpp:46-47`), and `LOG_DBG` is live in `x4pro` (`LOG_LEVEL=2`, `platformio.ini:174`).
  That means the Home path can be measured across `LauncherActivity` and the navigator.

## Findings

### MAJOR 1: A9 would make every Recent chip lose its abbreviation once the cache is warm

- **Claim (A9).** "With the cache filled, [`collectRecentChips`] uses the cached table instead."
  The cached table is `BibleBookIndex::names`, which is filled by the navigator's `loadBooks`.
- **Problem.** The navigator never fills that table's abbreviations. It calls only
  `bookNames.joinToc(...)` (`BibleNavigationActivity.cpp:111`), and `joinToc` explicitly
  **clears** every abbreviation (`BibleBookNameTable.cpp:55-58`; the header says so at
  `BibleBookNameTable.h:38-40`, "Clears every abbreviation; load() fills them afterwards"). The
  navigator writes its labels into its own `bookAbbrev` instead (`:118-125`).
  `collectRecentChips` reads exactly the field that would be empty:
  `names->abbreviationFor(place.unit.book)` (`EpubReaderActivity.cpp:909`). With an empty
  abbreviation, `formatChipLabel` falls back to the raw stored reference
  (`PlacesDoc.cpp:64-66`). The result: after the first Go to, every menu open shows
  "Génesis 1:3"-style full references in place of "Gén. 1:3". That is a visible regression in a
  feature the spec says is unchanged. The testing strategy has no A9 case that would catch it.
- **Fix.** Have the cached table carry the same abbreviations `load()` gives it. One way is to
  factor `load()`'s tail (`BibleBookNameTable.cpp:42-49`) into a
  `joinBookNav(const Epub&, BookNavPage&)` (or a `setAbbreviations(labels)`), and have the
  navigator's `loadBooks` call it in place of the bare `joinToc`. Add a host case: a table filled
  the navigator's way returns the page label from `abbreviationFor`. Do not point the chips at
  `BibleBookIndex::abbrev`. That array falls back to the full name when a label is empty
  (`BibleNavigationActivity.cpp:122-124`), and `load()` does not.

### MAJOR 2: A10 as scoped cannot move the metric it is gated on

- **Claim (Goal 4, A10).** The target is the first grid frame within ~500 ms of the tap on a warm
  menu Go to. A10 is decided from the "before" band time (> 150 ms). If A10 is built, the snapshot
  buffer is "owned by the navigator … freed in `onExit`".
- **Problem.** A buffer that the navigator owns and frees in `onExit` never exists at the first
  paint of any Go to. Every Go to constructs a new navigator
  (`EpubReaderActivity.cpp:862`, `std::make_unique<BibleNavigationActivity>`). The spec concedes
  this: "This speeds up selection moves and level changes, not the first frame." So on the warm
  path, the only render-side cost the spec names (the full 783-row BMP stream plus ~50 K
  `drawPixel`, `CoverBand.cpp:19-58`) is left on the critical path. The trigger meanwhile reads
  the first-frame band time. If the device shows band + FAST refresh above 500 ms, the spec has no
  lever and no stated outcome. The issue offered "Cache the band in PSRAM (7.2 KB)" precisely as a
  warm-path candidate.
- **Fix.** Make the decision consistent with the goal. **If** A10 triggers, keep the band bytes in
  `BibleNavCache` (reader lifetime, same PSRAM placement). The first paint of a warm Go to then
  restores them with `writeFramebufferRegion` in place of `CoverBand::draw`. The content is fixed
  for the reader's life: same `Epub`, same thumbnail, portrait only. Also state what the PR reports
  if the warm first frame still misses ~500 ms: the dominant milestone, and no further change in
  this issue. If the navigator-scoped version is kept deliberately, say so and drop "(> 150 ms on
  the first frame)" as its trigger, since it would then be gated on selection-move time.

### MINOR 1: `takeTocSpine`'s signature cannot do the range check the spec promises

- **Claim.** Error handling: "`takeTocSpine` accepts only `0 <= spine < spineCount`". The test
  list includes "a negative or out-of-range spine is ignored".
- **Problem.** The declared signature is
  `takeTocSpine(targets, out, count, tocHref, tocSpine)`, with no `spineCount`. Its own comment says
  only "tocSpine >= 0". It cannot reject an out-of-range index.
- **Fix.** Add `int spineCount` to `takeTocSpine`. `joinToc` gets it from
  `epub.getSpineItemsCount()`, and the comment should say `0 <= tocSpine < spineCount`.

### MINOR 2: `findTargetByHref` contradicts the "duplicate targets both resolve" test

- **Claim.** "Matching uses `BibleNav::filenameTail` and `BibleNav::findTargetByHref`." A
  `resolveFrom` test requires "duplicate targets both resolve".
- **Problem.** `findTargetByHref` returns the **first** matching index, even if that slot is
  already resolved (`BibleNavScanner.cpp:193-199`). Built on it, `resolveFrom` would resolve only
  the first duplicate, and `takeTocSpine` would be blocked on an already-set first slot. Today's
  walk loops over every `j` (`Epub.cpp:970-976`).
- **Fix.** Say that `resolveFrom` compares every unset `out[j]` against
  `filenameTail(spineHref)`, as `Epub.cpp:972-975` does, and that `takeTocSpine` does the same.
  Keep `findTargetByHref` for the name join only.

### MINOR 3: the fallback to `resolveFilenamesToSpineIndices` "for only those targets" needs a mechanism

- **Claim (A2).** Unresolved targets go to the unchanged `resolveFilenamesToSpineIndices`, "for
  **only those targets**".
- **Problem.** That function resets every `out[i]` to `-1` first (`Epub.cpp:964`) and has no
  subset mode. Using it for a subset means compacting the targets into a scratch array and mapping
  the results back. `SpineSearch::resolveFrom(first = 0)` already skips preset entries, by the
  spec's own contract.
- **Fix.** Make the fallback `SpineSearch::resolveFrom(..., first = 0, ...)`. That gives one walk
  implementation, already host-tested, and still no `lib/Epub` change.

### MINOR 4: "PSRAM by size" is a preference, not a pin

- **Claim (A4, A10).** `makeUniqueNoThrow` "places it in PSRAM", and
  `static_assert(sizeof(BibleNavCache) > 4096)` "pins the routing".
- **Problem.** With `CONFIG_SPIRAM_USE_MALLOC=y`, a request above `ALWAYSINTERNAL` tries SPIRAM
  **first** and falls back to internal RAM. The research note says as much ("PSRAM is only
  guaranteed through `heap_caps_malloc(MALLOC_CAP_SPIRAM)`", research "Cost 3"). The #204 spec
  committed the band cache to `heap_caps_malloc` + `PsramFree` explicitly
  (`2026-09-30-issue-204-design.md:113-120`). The practical risk is small with 8 MB PSRAM, and the
  codebase already relies on by-size placement (`EpubReaderActivity.cpp:901-905`). But the spec
  diverges from both documents without saying so.
- **Fix.** Reword the claim to "PSRAM-preferred by size; the `static_assert` keeps it above the
  threshold". For the A10 band buffer, follow #204's explicit `heap_caps_malloc` + `PsramFree`, or
  note that it deliberately does not.

### MINOR 5: `BibleBookIndex` cannot name the navigator's private constants

- **Claim.** `BibleNavCache.h` declares `BibleBookIndex` with `MAX_BOOKS`, `BOOK_ABBREV_BYTES`,
  `BOOK_NAME_BYTES` and `MAX_CHAPTERS`, and it is "no Epub".
- **Problem.** `BOOK_ABBREV_BYTES` and `MAX_CHAPTERS` are **private** members of
  `BibleNavigationActivity` (`BibleNavigationActivity.h:42-46`), and the navigator's header would
  include the cache header. Also, `BibleBookNameTable.h` includes `<Epub.h>` and
  `SpineHtmlStream.h` (`BibleBookNameTable.h:3,11`). The spec anticipates the second point but not
  the first. It also never names the file `SpineSearch` lives in.
- **Fix.** Hoist the four constants into the cache header, or into `BibleNavLimits.h`, and have the
  navigator alias them. Name the file for `SpineSearch`. Its test target needs
  `${REPO_ROOT}/lib/Epub` (or `lib/Epub/Epub`) on the include path, plus `BibleNavScanner.cpp` and
  expat, as `test/bible_book_join/CMakeLists.txt` does.

### MINOR 6: the Overlap section and several line references are stale

- **Claim.** "PR #236 edits `EpubReaderActivity.{h,cpp}` … If #236 has landed, rebase first";
  `openChapterPicker (:848)`, `collectRecentChips (:898)`.
- **Problem.** #236 is **merged** (`bed75801`, and `origin/main` is now `4c0b8cb5`, release
  1.29.3). It also changed `LauncherActivity.{h,cpp}`, which this spec edits (`openReader`). On
  `origin/main`, `openChapterPicker` is at `:908`, `collectRecentChips` at `:954`, `openReader` at
  `LauncherActivity.cpp:395`, and "Rendered page in" at `:1450`. The navigator references have
  drifted too, even at this base: `onEnter` is `:57-83` (the spec says 56-81), the heap `LOG_INF` is
  `:79-82` (76-80), `resolveFilenamesToSpineIndices` is `:109` (103), and the chapter resolve is
  `:166` (158).
- **Fix.** Rebase onto `origin/main` before planning. Update the Overlap paragraph to "merged;
  touched `EpubReaderActivity` and `LauncherActivity`", and refresh the cited lines.

## Verdict rationale

The measured facts, the two walk removals and the reader-life cache are sound. The EPUB data
confirms A2 and A3 exactly. MAJOR 1 is a concrete regression, and its fix is mechanical. MAJOR 2
amends a measurement-gated option rather than reversing a decision, and neither needs the owner's
judgment. Fix both inline along with the MINORs.

VERDICT: CLEAR
