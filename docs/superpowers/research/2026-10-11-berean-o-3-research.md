# berean-o-3 research — make opening the Bible from Home cheap (R12 / R5)

Branch `perf/r12-bible-open`, fast-forwarded to `478cf1ed` (`origin/main`, release 1.29.9). Since the
baseline build `1ba1cd34`, `main` has changed only docs and the version (`git diff --stat 1ba1cd34
origin/main`: the three responsiveness docs, `CHANGELOG.md`, `platformio.ini` version, `version.txt`).
So every code line the baseline measured is still the code cited here. Every claim cites a line read on
this branch or a command run on 2026-10-11.

**Device log source.** The raw serial capture behind `docs/superpowers/notes/responsiveness-baseline.md`
is not in the repo. It is still on this machine at
`/private/tmp/claude-501/-Volumes-stein-Documents-development-personal-berean-os/ee8ac7a1-…/scratchpad/baseline.log`
(1,940 lines; `run2.log` is a subset of it). Lines quoted below are verbatim, and `log:N` means line N of
that file.

## Installed tools and packages

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on `PATH`) |
| ESP32 platform | pioarduino `55.03.37` | `platformio.ini:15`; `~/.platformio/platforms/espressif32/platform.json` |
| Arduino core | `framework-arduinoespressif32` 3.3.7 | its `package.json` |
| ArduinoJson | 7.4.2 | `platformio.ini:155`; host tests pin `v7.4.2` (`test/CMakeLists.txt:31`) |
| googletest | v1.17.0 | `test/CMakeLists.txt:17` |
| CMake / host compiler | 4.4.2 / Apple clang 21.0.0 | `cmake --version`, `c++ --version` |
| freeink-sdk gitlink | `67f7e01` | `git submodule status` |

## 1. Where `Book loaded in` goes

One open, `log:1003-1012`:

```
[198578] [DBG] [ACT] Entering activity: EpubReader
[198581] [DBG] [EBP] Loading ePub: /Traducción del Nuevo Mundo (nwt-S) - WATCHTOWER.epub
[198857] [DBG] [BMC] Loaded cache data: 3937 spine, 129 TOC entries
[198861] [DBG] [CSS] Loaded 0 rules from cache
[198861] [DBG] [EBP] Loaded ePub: /Traducción del Nuevo Mundo (nwt-S) - WATCHTOWER.epub
[198864] [DBG] [ERS] Loaded cache: 1099, 8
[198870] [DBG] [BKM] Loaded 1 bookmarks from file
[199079] [DBG] [MEM] Study open: internal free 214284 (min 168144), PSRAM free 8208640
[199085] [DBG] [READER] Book loaded in 506 ms
[199157] [DBG] [ERS] Loading file: OEBPS/1001061146-split16.xhtml, index: 1099
```

| Step | Δ | Code |
|---|---|---|
| `exists`, SD font check, orientation | 3 ms | `ReaderActivity.cpp:46-53` |
| `BookMetadataCache::load` | **276 ms** | `BookMetadataCache.cpp:460-499` |
| CSS cache check | 4 ms | `Epub.cpp:375-407` |
| `progress.bin` | 3 ms | `EpubReaderActivity.cpp:196-217` |
| bookmarks | 6 ms | `EpubReaderActivity.cpp:228` |
| `STUDY.openPublication` | **209 ms** | `EpubReaderActivity.cpp:235`, `StudyStore.cpp:40-72` |
| `repairTexts`, Bible registration | 6 ms | `EpubReaderActivity.cpp:242-262` |
| app-state save, recents save, render task wake | 72 ms | `ReaderActivity.cpp:62-66` |

Across every Bible open in the capture (awk over `log`), the metadata load took **274–278 ms** (15 opens)
and the study open **207–211 ms** (15 opens). From `Book loaded in` to the next `Loading file` took 72–98 ms on the
six plain opens where awk could pair the two lines. The other two pairs are Go-to opens (3,985 and 5,855 ms), which open the picker first.

For contrast, the 21-spine meeting workbook's whole metadata load takes 4 ms (`log:993-994`). Fixed
overhead is therefore at most 4 ms, and almost all of the 276 ms scales with the spine count.

## 2. Owners and control flow

### 2.1 Home → reader

1. `LauncherActivity.cpp:397-412` builds a `ReaderEntryIntent` and calls `activityManager.goToReader`.
2. `ActivityManager::goToReader` (`ActivityManager.cpp:208-229`) calls `ReaderActivity::create`
   (`ReaderActivity.cpp:25-37`, a `makeUniqueNoThrow<EpubReaderActivity>`) and then `replaceActivity`.
3. `replaceActivity` (`:184-196`) defers. `loop()`'s Replace branch (`:139-158`) runs the old activity's
   `onExit`, destroys it, clears the stack, then calls the new activity's `onEnter`. All of this happens on
   the loop task. The reader object is constructed first, but the Launcher is destroyed before its `onEnter` runs.
4. Reader → Home is the same mechanism with `goHome()` (`:242-244`), so the `EpubReaderActivity` and its
   members (`epub`, `navCache`, `bookmarks`) are destroyed.

### 2.2 `ReaderActivity::onEnter` (`ReaderActivity.cpp:43-67`)

`loadBook()` sits between `loadStartMs` (`:55`) and the `Book loaded in` log (`:60`). After the log come
`APP_STATE.openEpubPath = bookPath; APP_STATE.saveToFileAtomic();` (`:62-63`),
`RECENT_BOOKS.addBook(...)` (`:64`), `onBookLoaded()` (`:65`) and `requestUpdate()` (`:66`). `onExit`
(`:69-78`) sets `readerActivityLoadCount = 0` and saves app state again.

### 2.3 `EpubReaderActivity::loadBook` (`EpubReaderActivity.cpp:164-265`)

- `makeUniqueNoThrow<Epub>` (`:165`), then `uncached = !Storage.exists(<cache>/book.bin)` (`:171`). Only
  when uncached does it draw the Indexing popup and take a `FrameBufferLoan` (`:172-182`).
- `loadedEpub->load(true, SETTINGS.embeddedStyle == 0)` (`:181`). The second argument is
  `skipLoadingCss`.
- `progress.bin` (`:196-217`), `bookmarks.load` (`:228`), `STUDY.openPublication(epub, renderer)`
  (`:235`), `STUDY.repairTexts()` (`:242`), `registerBibleIfUnknown` (`:253-262`).

### 2.4 `Epub::load` on a cache hit (`Epub.cpp:361-410`)

It allocates `BookMetadataCache` and `CssParser` (`:365-367`), then calls `bookMetadataCache->load()`
(`:374`). When `!skipLoadingCss` it also loads the CSS cache, `cssParser->loadFromCache()` (`:377`), and
releases it straight away with `cssParser->clear()` (`:407`).

`embeddedStyle` defaults to 1 (`CrossPointSettings.h:337`). The owner's log prints
`[CSS] Loaded 0 rules from cache` on every open. That line is in `CssParser::loadFromCache`
(`CssParser.cpp:955`), which runs only when `!skipLoadingCss`, so the owner's unit has embedded style on.

### 2.5 `BookMetadataCache::load` — the 276 ms

`BookMetadataCache.cpp:460-499`:

- It reads the version (`:465-472`), `lutOffset`, the counts, and five metadata strings (`:474-482`).
- It then seeks past both LUTs and reads **every spine entry** to keep only `cumulativeSize`:

  ```cpp
  for (uint16_t i = 0; i < spineCount; i++) {
    cumulativeSizes.push_back(readSpineEntry(bookFile).cumulativeSize);
  }
  ```

  The loop is at `:492-494`. `readSpineEntry` (`:546-548` → `readSpineEntryFrom`, `:45-52`) makes four
  `HalFile` reads per entry: string length, href bytes, `cumulativeSize` and `tocIndex`
  (`Serialization.h:22-25`, `:46-51`). It also builds a `std::string` href, only to discard it.
- Every `HalFile::read` takes `storageMutex` (`HalStorage.cpp:138-141,158`). `bookFile` is unbuffered.

The 3,937-entry spine therefore costs 15,748 mutex-guarded SdFat reads and up to 3,937 heap allocations,
all to fill a 15,748-byte vector.

**The spine, measured.** I ran a Python pass over `content.opf` from the local `nwt_S.epub`
(`/Volumes/stein/Downloads/nwt_S.epub`, 14,945,282 B). Its 3,937 `itemref`s match the device log. Whether
this file is byte-identical to the copy on the device is not verified. The results:

| | |
|---|---|
| href bytes (`OEBPS/…`, as stored; the log shows `OEBPS/1001061146-split16.xhtml`) | min 15, mean 30.5, max 35 |
| hrefs over 15 bytes (libstdc++ keeps at most 15 chars inline, so these likely heap-allocate) | 3,936 of 3,937 |
| spine block in `book.bin` (`u32 len + href + u32 cum + s16 toc`) | **159,467 B** |
| a contiguous `u32 cumulativeSizes[spineCount]` | **15,748 B** |
| `biblebooknav.xhtml` spine index | 2 |

**`book.bin` layout** (`buildBookBin`, `BookMetadataCache.cpp:194-348`; `docs/file-formats.md:7-79`): a
header (`u8 version`, `u32 lutOffset`, `u16 spineCount`, `u16 tocCount`), the metadata strings, a
`u32 spineLut[]`, a `u32 tocLut[]`, the spine entries, then the TOC entries. There is no fixed-width
cumulative-size array. The version is `BOOK_CACHE_VERSION = 10` (`:14`), and a mismatch makes `load()`
return false (`:467-471`).

**Where the RAM cache came from.** `6af9a049 perf: cache cumulative spine sizes in RAM (#2441)` added
`cumulativeSizes`. Its header comment still says "<1KB for typical books" (`BookMetadataCache.h:66-69`);
on this Bible it is 15.7 KB, which is above the 4,096-byte threshold and so lands in PSRAM by size
(`BibleBookIndex.h` comment on `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). The cache is needed from the first
frame:

- `renderStatusBar` calls `calculateProgress` on every render, the Bible included (`EpubReaderActivity.cpp:1906`). Only the bar's
  visibility depends on `!isBible()` (`:1930-1931`).
- `calculateProgress` reads `getBookSize()`, which is the last entry, plus two neighbours
  (`Epub.cpp:888-893,924-930`).
- `jumpToPercent` walks the whole array (`EpubReaderActivity.cpp:731-758`).
- `SpineHtmlStream` needs one item's size before it inflates (`SpineHtmlStream.cpp:33-34`).

`getSpineEntry(i)`, which seeks through the LUT on every call (`BookMetadataCache.cpp:508-525`), is what
`resolveHrefToSpineIndex` uses. That function walks the spine linearly (`Epub.cpp:936-961`), but for the
Bible it stops at index 2, and `getBibleBookNavSpineIndex` memoises the result per `Epub`
(`Epub.cpp:980-985`, `Epub.h:36`). So it is not a cost here.

### 2.6 `StudyStore::openPublication` — the 209 ms (`StudyStore.cpp:40-72`)

The function runs these steps in order. How the 209 ms splits between them is **not measured**: there is
no log between `Loaded 1 bookmarks` and `Study open`.

1. `closePublication()` (`:41`).
2. `getBibleBookNavSpineIndex()` (`:45`): 3 LUT reads, memoised.
3. `PubKeyRegistry::lookup(path)` (`:51` → `PubKeyRegistry.cpp:78-95`). This is
   `readDocFromFileAdopting` → `readDocFromFileChecked` → `Storage.readFile` (`PersistableStore.cpp:124-139,201-213`).
   `SDCardManager::readFile` appends one byte per `f.read()` to an Arduino `String`
   (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:201-207`). The same file is read on
   Home, in 8–16 ms: `log:990-992`, from `Recent books loaded` to `Bible: … (registry)` to
   `Registry has mwb issue`.
4. `TagPaletteFile::load` (`:54`; `TagPaletteFile.cpp:18-23`): the palette, through the same
   `readDocFromFileChecked`.
5. `PassageFile::load` (`:60`; `PassageFile.cpp:18-26`). It streams the passages file through a
   512-byte `BufferedFileReader` into an ArduinoJson document on the PSRAM allocator
   (`PersistableStore.cpp:141-163`). It then calls `PassageDoc::fromJson`
   (`lib/StudyStore/StudyStore/PassageDoc.cpp:304-342`), which ends in `measureBytes()` (`:344-349`):
   **a full re-serialisation of every passage into a second JsonDocument**, just to check the budget.
   The Bible's passages file is 43,864 B: the wake and sleep scans of the same file log
   `Verse scan: 43864 bytes` (`log:65`) and `Study pick: … 43864 bytes parsed` (`log:1333`), at
   405–607 ms and 445–450 ms. Those scans do more per row (fit measurement), so they bound the
   parse cost from above, not exactly.
6. `UnitIndexCache::begin` (`:66-69`; `UnitIndexCache.cpp:50-122`). It opens the 15 MB EPUB just to read
   `size()` (`:57-61`), then reads the 16-byte index header (`:74-86`). It rebuilds only on a stale header.

The likely dominant step is 5, by its size and the double JSON pass, but that is inference. The spec must
either time the steps (`LOG_DBG` per step) or choose a design that skips the whole call on a warm open.

### 2.7 The saves (`ReaderActivity.cpp:62-64`)

- **App state.** At boot, `ResumeReader` clears `openEpubPath`, increments `readerActivityLoadCount` and
  saves *before* opening the reader (`main.cpp:594-601`). `onEnter`'s save writes the path back
  (`ReaderActivity.cpp:62-63`), and `onExit` zeroes the count and saves (`:73-74`). That is the
  crash-loop guard the brief and roadmap R5 (`roadmap.md:239-252`) say to keep.
- **Observation.** On a Home → Bible round trip to the same book, `openEpubPath` already equals
  `bookPath` and the count is already 0, both from the previous `onExit`. The serialised fields
  (`CrossPointState.cpp:51-70`) hold no timestamp. So this save rewrites an unchanged document. Skipping it
  would take a dirty check that does not exist today. That is outside the brief's mandate ("must stay"),
  so it is recorded here, not proposed.
- **Recents.** `RecentBooksStore::addBook` (`RecentBooksStore.cpp:27-51`) always calls
  `pruneMissing()`, which is one `Storage.exists` per entry (`:78-84`); the log shows 6 entries,
  `log:990`. It then erases and reinserts at the front, normalises (`RecentBooksDoc::normalise`,
  `src/util/RecentBooksDoc.h:75`), and **always** calls `saveToFileAtomic()` (`:48-50`). Nothing checks
  whether the book is already first and unchanged.
- How the 72 ms splits between the two saves and the render task wake-up is not measured.

### 2.8 Go to and `BibleNavCache`

- `navCache` is a `std::shared_ptr<BibleNavCache>` member of `EpubReaderActivity`
  (`EpubReaderActivity.h:50`). It is created lazily on the first Go to (`EpubReaderActivity.cpp:949-959`)
  and dies with the reader.
- `BibleNavigationActivity::loadBooks` uses it when `hasBooks()` and logs `books from cache`
  (`BibleNavigationActivity.cpp:111-120`). Otherwise it streams `biblebooknav.xhtml` through
  `SpineHtmlStream` and `BibleNav::Scanner` (`:122-…`).
- Measured: three Home Go tos log `books ready` at +262, +259 and +261 ms, against masthead at +8, +7 and
  +8 ms (`log:99-100,888-889,978-979`). Go tos from the reader menu with a warm cache log +33, +32 and
  +15 ms (`log:673-675,735-737,810-812`).
- The cache's content is the book table plus one book's chapter spines (`BibleNavCache.h:21-60`,
  `BibleBookIndex.h:9-21`). Its size is asserted above 4 KB so that it is PSRAM-preferred
  (`BibleBookIndex.h` `static_assert`). #238 sized it at ~6.0 KB
  (`docs/superpowers/specs/2026-09-30-issue-238-design.md:320`). #238 recorded the reader-lifetime limit
  deliberately as assumption A1 (`:70-77`): "A process-wide cache keyed by the book would need an
  invalidation rule for a replaced EPUB … No such pattern exists in `src/activities`".

## 3. What already survives a Home round trip

**`closePublication()` is only ever called from `openPublication()` itself.** `grep -rn
"closePublication\|openPublication" src lib` finds `StudyStore.cpp:41,74`, the declarations
(`StudyStore.h:37-38`), the one `openPublication` call (`EpubReaderActivity.cpp:235`), and a comment. No
activity's `onExit` or destructor closes it. This has three consequences:

- `StudyStore::units_` (`UnitIndexCache`) holds `std::shared_ptr<Epub> epub_` (`UnitIndexCache.h:27,87`).
  When the reader is destroyed, `~EpubReaderActivity` calls `epub.reset()` (`EpubReaderActivity.cpp:152-161`),
  but the Study store's reference keeps **the whole previous `Epub` alive at Home**. That includes its
  `BookMetadataCache`, the 15.7 KB `cumulativeSizes`, and an **open `bookFile` handle on `book.bin`**
  (`BookMetadataCache.h:56`). The palette, passages and pubkey stay resident too.
- Retaining the Bible's `Epub` across Home would therefore add **no memory** compared with what Home holds
  today (`log:996`, `Memory on entry: internal free 215244 … PSRAM free 8208640`, measured with it still
  alive). The new allocations would be the `BibleNavCache` (~6 KB, PSRAM by size) and whatever holder
  object the spec adds.
- Today, nothing reads through that stale `Epub` after the reader exits: every STUDY consumer is a reader
  sub-activity or `BibleSearchStore`, which is reached only from the reader (`grep -rln "STUDY\." src`).
  It is, however, an open file handle that survives Settings → Clear cache, a web delete and a book move
  (§4). Nothing prevents removing a file that is still open:
  - `SDCardManager::removeDir` calls `vol().remove()` on each file
    (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:375-404`);
  - SdFat's `FatFile::remove` frees the file's cluster chain and marks the entry deleted, with no check
    for other open handles (`.pio/libdeps/x4pro/SdFat/src/FatLib/FatFileLFN.cpp:508-538`, primary
    checkout);
  - the exFAT path was not read.

## 4. Invalidation surface for any retention

- **Sleep resets the chip.** Deep sleep is "a full chip reset on wake"
  (`main.cpp:140-146`; `enterDeepSleep` → `powerManager.startDeepSleep`, `:243-280`). RAM retention dies
  by construction, so the first Bible open after a wake is always cold.
- **Leaving File Transfer does *not* reset an X4 Pro.** `CrossPointWebServerActivity::onExit` calls
  `silentRestart()` (`CrossPointWebServerActivity.cpp:119-128`). On touch boards that becomes
  `finishWifiSessionWithoutRestart()`, which turns WiFi off and returns without a reboot
  (`main.cpp:148-161,164-167`; `FREEINK_CAP_TOUCH`, `BoardConfig.h:176`). A web upload of the Bible
  therefore happens while retained state is alive.
- **Book and cache mutators.** Most go through `clearBookCache(path)` (`src/util/BookCacheUtils.cpp:23-30`):
  - the web server (`CrossPointWebServer.cpp:622,691`; `FileRoutes.cpp:398,549,639,740`);
  - WebDAV (`WebDAVHandler.cpp:376,425,534`);
  - the downloader (`PublicationDownloader.cpp:110,252`);
  - the file browser (`FileBrowserActivity.cpp:169,227`).

  These do **not** go through it:
  - Settings → Clear cache (`ClearCacheActivity.cpp:97-126`, `removeDir` per `epub_*`);
  - `CardBooks::remove` (`CardBooks.cpp:46-62`, `removeDir(bookCachePath(...))`);
  - the reader's own `epub->clearCache()` (`EpubReaderActivity.cpp:898`);
  - `moveFinishedBookToReadFolder`, which renames the book and its cache dir (`EpubReaderActivity.cpp:121-141`);
  - the downloader's cache-dir rename (`PublicationDownloader.cpp:112-116`).
- **One task mutates.** The only task `src/` creates is the render task (`ActivityManager.cpp:32`);
  `grep -rn xTaskCreate src lib freeink-sdk/libs` finds otherwise only SDK input, audio and BLE tasks.
  The web server is pumped from `CrossPointWebServerActivity::loop` (`:351`). Every mutator above runs on
  the loop task, the same task as `onEnter`.
- **Study files cannot be changed from outside.** The web server and WebDAV refuse dot-dirs
  (`WebRouteUtils.cpp:10` → `protectedpath::isProtectedPath`; `WebDAVHandler.cpp:50,292,…`). Migration
  runs only at boot (`main.cpp:513`). HomeVerse and the sleep screen read the passages file directly, not
  through STUDY.
- **A file replaced in place is invisible to the cache.** `BookMetadataCache` checks only its version
  (`BookMetadataCache.cpp:467`; `.claude/agents/epub-dev.md`, "validates on version only"), so the
  `clearBookCache` convention is what catches a replaced EPUB. The nearest identity check that does exist
  is `BibleSearchStore::documents`, keyed on path plus file size (`BibleSearchStore.cpp:107-117`).
- **The `Epub` depends on a setting.** `load(…, SETTINGS.embeddedStyle == 0)`
  (`EpubReaderActivity.cpp:181`) decides whether the CSS cache is validated or rebuilt (`Epub.cpp:375-401`).

## 5. Consequences of bumping `BOOK_CACHE_VERSION`

- Only `BookMetadataCache` reads or writes `book.bin`'s layout. `grep -rn "book\.bin" src lib` finds only
  comments, plus the existence check at `EpubReaderActivity.cpp:171`.
- A mismatch makes `load()` return false, and `Epub::load` rebuilds (`Epub.cpp:412-515`). **With embedded
  style on** (the default, and the owner's setting, §2.4), the rebuild also runs `parseCssFiles()` and
  `Storage.removeDir(<cache>/sections)` (`Epub.cpp:497-501`). The first open after the update would
  therefore re-index the 3,937-spine Bible and drop every laid-out chapter of it. Each chapter then
  rebuilds on its first visit, which the baseline measures at 1.2–1.8 s
  (`responsiveness-baseline.md`, "Go to chapter → page, chapter not yet built"). Other books get the same
  treatment on their own first open.
- A stale-version `book.bin` still *exists*, so `loadBook` shows no Indexing popup and takes no
  framebuffer loan (`EpubReaderActivity.cpp:171-182`). The re-index runs under the previous screen. Every
  past bump had the same gap; the last one was `dde7e037`, v8 → v10.
- No document records how long the Bible's re-index takes. `grep` finds no "Total indexing" figure in
  `docs/`.
- Other callers that would see a stale cache:
  - `readBookProgressPercent` (`BookCacheUtils.cpp:32-37`, `load(false, true)`) returns no bar until the
    rebuild;
  - the sleep cover path rebuilds at sleep (`SleepActivity.cpp:827-834`), but only in cover mode. The
    owner uses the study sleep screen (`log:1331-1333`).

## 6. Options, by mechanism

These are inputs for the spec, not a design. The lower bounds use F16's ~2.5 MB/s raw SD ceiling
(1-bit at 20 MHz, `docs/superpowers/research/2026-10-11-responsiveness-research.md:208-215`). They are
estimates, not measurements.

| Option | Mechanism | Removes | Lower bound on what remains | Cost / risk |
|---|---|---|---|---|
| **1a** Contiguous `u32 cumulativeSizes[]` in `book.bin`, read in one `read()` | 1 SD read of 15.7 KB, no per-entry mutex, no string allocations | most of 276 ms, on every open | ~6 ms of transfer | §5: a version bump re-indexes and, with embedded style on, drops every section cache once |
| **1b** No format change: read the existing spine block through `serialization::BufferedFileReader`, skipping href bytes instead of allocating | ~39 4 KB reads instead of 15,748 mutex-guarded ones; no heap churn | per-call overhead (~210 of 276 ms by subtraction, inferred) | ~64 ms to stream 159 KB | no migration; R2's 40 MHz would halve the floor |
| **1c** Sidecar size array beside `book.bin` | as 1a, without touching `book.bin` | as 1a | as 1a, plus a fallback for caches without it | a new on-disk format, which is a user decision (`.claude/agents/epub-dev.md`, "Prime directive" 2) |
| **1d** Lazy per-index reads | 2 seeks + 1 read per lookup | load cost moves to first use | — | `renderStatusBar` needs the last entry on every render (`EpubReaderActivity.cpp:1906`) and `jumpToPercent` needs them all (`:731-758`). This undoes #2441. |
| **2** Keep `Epub` + `BibleNavCache` + study state across Home (R12) | skip `Epub::load`, `openPublication` and `loadBooks` on a warm open | ~485 ms of `Book loaded in`, plus 253 ms on Go to | progress, bookmarks and saves (~10 ms + 72 ms) | §3–§4: invalidation, ownership across `replaceActivity`, the crash-loop guard. A cold open after a wake is unchanged. |
| **3** R5: skip `addBook`'s prune and save when the book is already first and unchanged | no `exists()` sweep, no atomic write | part of the 72 ms | — | `test/recent_books_doc` compiles only `RecentBooksDoc.cpp` (§8) |

1a and 1b both help every open (cold, warm, after a wake). 2 helps only round trips, but covers the 209 ms
and the 253 ms, which 1a/1b cannot. They combine.

## 7. Nearest existing examples

- **Bumping `book.bin` and documenting it:** `dde7e037`, which changes `BookMetadataCache.cpp:14` plus
  the `### Version N` heading and `EXPECTED_VERSION` in `docs/file-formats.md`.
- **Buffered sequential reads over a `HalFile`:** `buildBookBin`'s three 4 KB buffers
  (`BookMetadataCache.cpp:186-192`, `BUILD_IO_BUFFER_SIZE` at `:18-20`, `readSpineEntryFrom` templated over
  `BufferedFileReader` at `:22-63`), and `PersistableStore`'s `JsonFileReader` (`PersistableStore.cpp:18-33`).
  `BufferedFileReader::seek` moves inside the buffered window without I/O
  (`lib/Serialization/BufferedFile.h:113-124`), which is what a skip-the-href read needs.
- **The RAM size cache itself:** `6af9a049` (#2441).
- **Session-scoped state keyed by publication identity:**
  - `BibleSearchStore::documents`, path plus file size, across activity lifetimes
    (`BibleSearchStore.cpp:107-117,195-197`);
  - `StudyStore`, a singleton holding one publication's state (`StudyStore.h:31-38`);
  - `HomeVerse`, a singleton cache validated against a date and file-size key
    (`HomeVerse.cpp:40-53,62-66`).
- **Explicit PSRAM:** `CatalogIndexStore::PsramFree` / `PsramBuffer` (`CatalogIndexStore.h:83-89`,
  `.cpp:40-46`).
- **A cache that lives for a screen's life:** `BibleNavCache`, #238
  (`docs/superpowers/specs/2026-09-30-issue-238-design.md`).

## 8. Tests

- **No host suite compiles `BookMetadataCache` or `Epub`.** `grep -rln "BookMetadataCache\|Epub.cpp\|ZipFile" test`
  finds only a Python data generator.
- **`test/storage_io` runs the real `PersistableStore.cpp`, `PassageFile.cpp` and `TagPaletteFile.cpp`
  against the in-memory fake** (`test/storage_io/CMakeLists.txt:1-46`). The fake has
  `storage_fake::readCallsOn(path)`, which "tells a buffered reader from a byte-at-a-time one"
  (`test/stubs/HalStorageFake.h:44-46`). A `BookMetadataCache::load` test can seed a `book.bin` with
  `putFile` and assert both the values and the read-call count. `buildBookBin` needs `ZipFile`; `load()`
  needs only `HalStorage`.
- **`test/recent_books_doc` builds only `RecentBooksDoc.cpp`** (`test/recent_books_doc/CMakeLists.txt:1-5`,
  24 tests). `RecentBooksStore::addBook` depends on `PersistableStore` and `HalStorage` and is not
  host-tested.
- **Related suites:** `test/crosspoint_state`, `test/passage_doc`, `test/storage_io`, and
  `test/number_grid`, which exercises `BibleNavCacheOf` (`BibleNavCache.h:19-20`).
- **`test/CMakeLists.txt` is an orchestrator append point** (brief; `.claude/agents/epub-dev.md`).

## 9. Collisions and scope

- `hpipe show --task t3` registers `lib/Epub/Epub/BookMetadataCache`, `src/activities/reader/ReaderActivity`,
  `src/activities/reader/BibleNavCache`, `src/RecentBooksStore`, `src/study/StudyStore`,
  `docs/file-formats.md` and `test/recent_books_doc/`.
- **Option 2 reaches files outside that list:**
  - `src/activities/reader/EpubReaderActivity.{h,cpp}`, for `navCache` (`.h:50`) and `loadBook`
    (`.cpp:164-265`). **t4 (R21) holds that file** (`hpipe show --task t4`).
  - The invalidation hooks: `src/util/BookCacheUtils.cpp`, `ClearCacheActivity.cpp` and `CardBooks.cpp`.
  - Possibly `src/study/UnitIndexCache`.
- **Option 1 stays inside the list**, plus a new host-test directory.
- **Siblings:**
  - t6 (R11) holds `src/study/BibleSearch*` and keys the Bible on path plus size. A shared identity rule
    with any R12 holder would keep the two from disagreeing.
  - t7 (R22) holds `HomeVerse` and `StudySleepScreen`, which read the passages file directly and are
    unaffected.
  - t2 (R9) holds `LauncherActivity`.
- **Tier.** The task already reaches three surfaces: `epub` (`BookMetadataCache`), `data`
  (`RecentBooksStore`, `StudyStore`) and `ui` (the reader). Option 1a is an on-disk format migration.
  The tier is already `heavy`, the highest, so there is nothing to raise.

## 10. Not measured, and open

- How the 209 ms splits across `openPublication`'s steps (§2.6).
- How the 72 ms splits between the app-state save, the recents save and the render task wake-up (§2.7).
- How long the Bible takes to re-index after a `BOOK_CACHE_VERSION` bump, and what dropping its section
  caches costs over a study session (§5).
- The estimates in §6 are transfer-time floors, not predictions. Only a device run gives the after figure.
