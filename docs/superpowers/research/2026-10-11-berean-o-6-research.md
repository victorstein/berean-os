# berean-o-6 (R11) research: keep the Bible search index open

Bead `berean-o-6`, task `t6`, branch `perf/r11-search-index-resident`, base `1ba1cd34`.
Every `file:line` below was read on this branch. Device figures come from the owner's raw
serial captures behind the baseline note (see "Evidence"), not from the note's rounded table.

## 1. Answer in brief

- **Re-entry (~500 ms) has two parts, and the brief names only one.** The term-index reopen
  costs ~220 ms. Reloading the book-name table costs about 250 ms more, and it runs on every
  entry too. Keeping only the index resident would save less than half of the 500 ms. To meet
  the acceptance criterion, the name table must stay resident as well (or be borrowed).
- **First search after boot (2.2 s) is `BibleSearchStore::resolveDocuments`** plus that same
  name load. Its cost is 66 reads of the persisted book map plus 1,189 random-access spine-entry
  reads from `book.bin`. It runs once per boot because its result is only cached in RAM.
- **Its result can be persisted.** Opening the index needs only the 64-bit fingerprint. The
  identity the units index already trusts (EPUB size and spine count) is a safe key, if the record
  dies with the Bible's `book.bin`. A new on-disk record is a user decision under the data-dev
  rules, so the spec phase will surface it.
- **There is no PSRAM-pressure mechanism in the tree.** "Close on PSRAM pressure" needs a new
  policy. The spec phase must decide whether it stays local (a free-PSRAM floor) or becomes
  cross-cutting.

## 2. Evidence: where the time goes

Sources: `/private/tmp/claude-501/-Volumes-stein-Documents-development-personal-berean-os/ee8ac7a1-b19a-4362-aa9f-09503dce7080/scratchpad/baseline.log`
(run 1, 1,940 lines) and `run2.log` (run 2, 332 lines). These are the captures the baseline note
(`git show docs/252-responsiveness-baseline:docs/superpowers/notes/responsiveness-baseline.md`)
summarises. The firmware was `1.29.8-dev-main-1ba1cd34` with the SD card at 20 MHz (research
F16).

### First search after boot

| Event | Run 1 | Run 2 |
|---|---|---|
| `Entering activity: BibleSearch` | 217237 | 444944 |
| Opening frame `Wait complete: 8179_DRF (1507 ms)` | 218816 | 446522 |
| `[BSEARCH] 1189 verse documents, fingerprint b063e8dc59554f2d` | 221032 (**+2,216**) | 448741 (**+2,219**) |
| `Entering activity: KeyboardEntry` | 221252 (**+220**) | 448962 (**+221**) |
| `New max loop duration` | 2405 ms | 2408 ms |

### Later searches (same boot)

| Opening frame done | `Entering activity: KeyboardEntry` | Gap |
|---|---|---|
| 225560 | 226060 | **500 ms** |
| 230250 | 230749 | **499 ms** |
| 453134 | 453639 | **505 ms** |

There is no `BSEARCH` line on these entries. The documents cache hit at
`BibleSearchStore.cpp:114`, so `resolveDocuments` did not run.

In every measured search the user went Home between searches. The log shows
`Exiting activity: EpubReader`, then `Entering activity: Launcher`, before each re-entry (run 1,
lines 1235-1263). **A resident index must therefore outlive `EpubReaderActivity`.** That is already
true of the `BibleSearchStore` singleton's document cache.

### Attribution, and how much of it is inferred

No sub-step timers exist on this path. The split below comes from log ordering and from one
analogue measurement. The implementation must add `LOG_DBG` timings so the owner's device run
confirms it.

| Part | Estimate | Basis |
|---|---|---|
| Index open + term cache | ~220 ms | Measured: the gap from the `BSEARCH` line to `KeyboardEntry` (§3 step 6 onward) |
| Book-name table load | ~250 ms | Inferred: the Go-to book list runs the same stream and TOC join, and logs 252-254 ms uncached (`[BNV] Go to: masthead checked +8 ms` → `books ready +262 ms`, run 1 lines 99-100, 888-889, 978-979) |
| Rest of re-entry | ~30 ms | Remainder of 500 ms: `fileSizeOf` open, `Storage.exists`, keyboard push |
| `resolveDocuments` | ~1.95 s | Remainder of 2,216 ms after the ~250 ms name load |

**Mechanism for the 1.95 s:**

1. **Book map.** `STUDY.spineIndicesForBook(book)` is called 66 times
   (`BibleSearchStore.cpp:146-157`). Each call allocates a 3,937-byte map, opens
   `/.berean/units/<pubKey>.bin`, seeks, reads the whole book map and scans it
   (`UnitIndexCache.cpp:375-394`). Its `push_back` loop has no `reserve`.
2. **Spine hrefs.** `epub->getSpineItem(spines[i]).href` is called 1,189 times
   (`BibleSearchStore.cpp:179-188`). Each call is
   `BookMetadataCache::getSpineEntry`: a seek to the LUT, a 4-byte read, a seek to the entry, then
   a string and two PODs (`BookMetadataCache.cpp:508-525`).
3. **Why that is slow.** SdFat 2.3.1 serves every sub-sector read through one shared
   per-volume sector cache (`FatFile.cpp:829-836`, `FsCache m_cache` at `FatPartition.h:195`, in
   the main checkout's `.pio/libdeps/x4pro/SdFat`). The LUT sector and the entry sector alternate,
   so each lookup most likely costs two sector loads: about 2,400 single-sector reads.
4. **For comparison.** `BookMetadataCache::load()` reads all 3,937 spine entries in one
   sequential pass (`BookMetadataCache.cpp:484-494`). The whole of `Loading ePub` →
   `Loaded cache data` takes 276 ms (run 1 lines 216659 → 216935).

Two things here are unconfirmed: whether the book map or the hrefs dominate, and the exact
per-read cost. Both need the timings above.

## 3. Current control flow

Entry is `EpubReaderActivity::openBibleSearch`. It releases the reader's section, then calls
`startActivityForResult(make_unique<BibleSearchActivity>(..., epub))`
(`EpubReaderActivity.cpp:976-989`). This is the only launch site.

1. `onEnter` clears the font cache, then calls `enterState(State::Opening, fullRefresh=true)`
   (`BibleSearchActivity.cpp:101-115`). That call belongs to R20 (t1); do not touch it.
2. `loop` → `runBusyWork` once the Opening frame is on the panel (`:168-178`, `:183-194`).
3. `bookNames.load(epub, renderer)` (`:190`) runs `BibleBookNameTable::load`:
   - `SpineHtmlStream::stream` over the book-nav spine, through `BibleNav::Scanner`;
   - `joinToc` over 129 TOC entries, one `getTocItem` each (`BibleBookNameTable.cpp:22-62`).

   The table is an activity member, 66×48 + 66×16 + 4 = 4,228 B
   (`BibleBookNameTable.h:57-59`), so it dies with the activity.
4. `openIndex` → `BibleSearchStore::open(epub, &status)` (`:220-232`). `open` calls `close()`
   first, so it always reopens (`BibleSearchStore.cpp:229-230`).
5. `documents(epub)` checks the cache (`BibleSearchStore.cpp:107-117`):
   - `fileSizeOf` opens the EPUB to read its size (`:83-88`).
   - Path and size equal to the cached ones → cache hit.
   - Otherwise → `resolveDocuments` (`:119-201`), which logs the `BSEARCH` line at `:198`.
6. `Storage.exists(INDEX_PATH)`, then `makeUniqueNoThrow<IndexReader>(psramAllocator())`,
   `openFileForRead` into the member `HalFile indexFile_`, and `IndexReader::open`. That reads the
   48-byte header, checks consistency, allocates the 4 KB page, and compares the fingerprint
   (`IndexReader.cpp:18-48`).
7. `cacheTermIndex()` reads `[termTableOffset, postingsOffset)` in one read into a PSRAM block
   (`IndexReader.cpp:50-61`). That is the term table (23,568 × 10 B = 235,680 B for the Spanish
   NWT, `docs/file-formats.md:572, 595-596`) plus the term strings, "~440 KB"
   (`IndexReader.h:52`), or "~444 KB" in the search design
   (`docs/superpowers/specs/2026-09-24-bible-verse-search-design.md:183`).
8. Success → `openKeyboard()`. Failure → `store.status(epub)` → Prompt.
9. **Exit.** `onExit` cancels any indexer, then calls `BibleSearchStore::getInstance().close()` and
   sets `reader = nullptr` (`BibleSearchActivity.cpp:117-125`). `close()` frees the reader, its
   term cache and its page, and closes `indexFile_` (`BibleSearchStore.cpp:260-263`).

### What the reader does while open

After the term cache is loaded, the reader still reads from the open file. `verse()`, posting
pages and term lookups beyond the cache all go through `source_.readAt` → `readFileAt`, which
seeks and reads `indexFile_` (`IndexReader.cpp:63-66, 77-103`; `BibleSearchStore.cpp:29-38`).

Keeping the reader resident therefore means one of two things:
- keep `indexFile_` open across activities, or
- give `IndexReader` a way to rebind its source without dropping `termIndex_`. Today
  `open()` → `close()` frees it (`IndexReader.cpp:10-20`).

### Existing closes and invalidations

| Event | Today | Where |
|---|---|---|
| Activity exit | close | `BibleSearchActivity.cpp:122` |
| Build begins | `store.close()` before the 2.88 MB builder is allocated | `BibleSearchIndexer.cpp:57-58`, activity `:255-256` |
| Query I/O failure | close | `BibleSearchActivity.cpp:539-540` |
| A different Bible (path or size) | documents re-resolved; reader reopened by step 4 | `BibleSearchStore.cpp:114-115` |
| Build finished | Ready state reopens via `openIndex` | `BibleSearchActivity.cpp:195-197, 373` |
| Sleep | nothing; deep sleep resets the chip | `main.cpp:243-281`, `ActivityManager.cpp:231-234` |
| PSRAM pressure | **no mechanism exists**: no `heap_caps_register_failed_alloc_callback`, no release hook (repo-wide grep for "pressure" and "failed_alloc" finds only internal-heap checks, e.g. `SdCardFont.cpp:975-976`) | — |

### Who can change the files under an open handle

- **`/.berean/` is protected from the web server and WebDAV.** Any path component starting with
  `.` is protected (`ProtectedPath.cpp:21-40`, used by `FileRoutes.cpp:124-708` and
  `WebDAVHandler`). So `bible.idx` cannot be deleted or replaced through them.
- **Only `BibleSearchIndexer` writes `bible.idx`, and it closes the store first.** No code
  removes `/.berean/search/`: a repo-wide grep for `removeDir` and `BEREAN_DIR` finds none. Clear
  Cache only removes `epub_*` dirs (`ClearCacheActivity.cpp:115-133`).
- **The Bible EPUB itself can change while the index is resident:**
  - a web upload, delete, move or rename (`FileRoutes.cpp:398, 549, 639, 740`);
  - a file-browser delete (`FileBrowserActivity.cpp:160-170`);
  - `CardBooks::remove` (`CardBooks.cpp:46-57`).

  Each of these also drops `/.crosspoint/epub_<hash>/`, which holds `book.bin`. On the next entry,
  `documents()` catches a size change; a same-size replacement is discussed in §5.

## 4. Memory a resident index would hold

| Block | Size | Where it comes from |
|---|---|---|
| Term index | ~440-444 KB | `psramAllocate` = `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` (`BibleSearchStore.cpp:25`), held in an `AllocatedBuffer` (`Allocator.h:24-67`). Already explicit PSRAM with RAII |
| Posting page | 4,096 B | same allocator (`IndexReader.cpp:44`) |
| `IndexReader` object | ~184 B by member sum on a 32-bit target (`IndexReader.h:98-109`, `IndexFormat.h:37-48`) | `makeUniqueNoThrow`, below `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096` (framework `esp32s3/qio_opi/include/sdkconfig.h:1041`), so internal SRAM |
| `indexFile_` | `HalFile` member of a static singleton | already in `.bss`; holding it open costs no new RAM |
| Document list | 1,189 × 3 = 3,567 B | PSRAM, **already held for the whole boot today** (`BibleSearchStore.cpp:164`, never released) |
| Book-name table, if kept | 4,228 B | an activity member today; it would need explicit PSRAM |

For scale, PSRAM free while reading is ~8.2 MB (baseline `MEM Study open` lines: 8,205,052-8,222,856).
The build's peak is 2.88 MB (search design `:177`), and the build frees the index before
allocating.

## 5. Can the first-open result be persisted?

**What the result is.** `Documents` holds `fingerprint`, `count`, `spines[]` and `books[]`
(`BibleSearchStore.h:44-49`):
- `open()` and `status()` use only `fingerprint` (`BibleSearchStore.cpp:212, 222, 243`);
- the indexer copies the lists (`BibleSearchIndexer.cpp:59-64, 75-90`).

**The fingerprint's inputs.** FNV-1a over the EPUB size, spine count, each verse document's href
(NUL included) in canonical order, then the count (`BibleSearchStore.cpp:174-189`). Changing that
definition would make every existing `bible.idx` Stale, and the user would have to rebuild. Any
persistence must store the fingerprint exactly as computed, never a different one.

**Identities already trusted for the same Bible:**

| Source | Keyed by | Where |
|---|---|---|
| `book.bin`, where the hrefs come from | `epub_<std::hash(path)>` + `BOOK_CACHE_VERSION` only; no size or content check | `Epub.h:48`, `BookMetadataCache.cpp:14, 460-472` |
| Units index, where the book map comes from | `pubKey`, then spine count + EPUB size (`headerIsStale`) | `UnitIndexCache.cpp:50-83` |
| In-RAM document cache today | path + EPUB size | `BibleSearchStore.cpp:114` |

**A same-size replacement.** Suppose a Bible is replaced by a file of identical size and spine
count. Today:
- **Through the device:** `book.bin` is dropped and rebuilt, so the hrefs change. But within one
  boot the in-RAM cache at `:114` would still serve the old fingerprint.
- **Off-device, with a card reader:** `book.bin` stays stale. The recomputed fingerprint uses the
  stale hrefs, so it does not change either.

A persisted record keyed by (format version, `BOOK_CACHE_VERSION`, EPUB size, spine count) has
**the same exposure as today, provided it is deleted whenever `book.bin` is**. Storing it inside
`epub->getCachePath()` gives that for free: every deletion path in §3 and Clear Cache already
drops that directory. Stored under `/.berean/search/`, it would outlive a device-side
same-size replacement, which today's code survives only by rebooting.

**Cost after persisting.** On a later boot the first search would pay roughly:
- one EPUB size read,
- one small record read,
- and the re-entry costs (names + index open).

So ~0.5 s instead of ~2.4 s, not counting the panel. This is an estimate until measured.

**Alternatives that compute faster instead of persisting:**
- One sequential pass over `book.bin`'s spine entries (§2, item 4), plus one book-map read
  instead of 66. This keeps no new file, but it needs a new API in
  `lib/Epub/Epub/BookMetadataCache` and in `StudyStore`/`UnitIndexCache`.
  `BookMetadataCache` and `StudyStore` are registered to sibling t3 (R12, heavy). That is another
  surface, and it overlaps a held file.
- Re-implementing `book.bin` parsing inside `src/study` would duplicate a format that `lib/Epub`
  owns. Rejected as fragile.

**Rules that apply.** A new on-disk record is "a user decision, not an autonomous one"
(`.claude/agents/data-dev.md`, Prime directive 2). The storage discipline requires:
- an atomic write;
- a byte budget;
- a format version a later build refuses;
- a named owning task.

The record would be ~3.6 KB (or 8 B of fingerprint plus its key, if only the fingerprint is kept),
far under the 40 KB streaming threshold. `docs/file-formats.md` would gain a section. That file is
also on t3's list.

## 6. Nearest existing examples

- **A resident, identity-keyed cache across activities:** `BibleSearchStore`'s own document
  cache — singleton, keyed by path + size, never released (`BibleSearchStore.cpp:92-117`). The
  resident reader should share its key and its invalidation.
- **A cache an activity hands to the next:** the reader's `std::shared_ptr<BibleNavCache>`
  (`EpubReaderActivity.h:50`). Its comment says the book never changes under a live reader
  (`BibleNavCache.h:16-17`). The menu's Recent chips borrow its `BibleBookNameTable` when one
  exists (`EpubReaderActivity.cpp:1002`). It is a template so host tests can exercise it without
  `Epub` (`BibleNavCache.h:19-20`, `test/number_grid`).
- **A singleton with an idempotent load and an explicit release:** `CatalogIndexStore::load()` /
  `release()` (`CatalogIndexStore.h:45-50`).
- **The explicit-PSRAM deleter idiom the brief cites:** `CatalogIndexStore.h:83-89`
  (`PsramFree`, `PsramBuffer`). `BibleSearch::AllocatedBuffer` with `psramAllocator()` is this
  module's existing equivalent.
- **A one-off derived result persisted so it is never recomputed at open:** the units index book
  map, "paid once per publication and never at open" (`UnitIndexCache.h:43-46`), validated by
  spine count + EPUB size (`UnitIndexCache.cpp:50-83`).

## 7. Tests available

- **`test/bible_search_index/`** (`BibleSearchIndexTest`, 977 lines). It compiles only
  `lib/BibleSearch` and its scanner dependencies (`test/bible_search_index/CMakeLists.txt:3-16`).
  `BibleSearchStore.cpp` is **not** host-compiled: it needs `Epub`, `STUDY` and `esp_heap_caps`.
  The reader-level tests a resident reader would extend are:
  - `ReopeningOnAnotherSourceDropsTheOldPage` (`IndexTest.cpp:946-964`);
  - `CachedTermIndexServesLookupsWithoutReading` (`:639`);
  - `AnUnallocatableCacheFallsBackToTheSource` (`:695`).
- **`test/storage_io/`** compiles real store sources against the in-memory card
  (`test/stubs/HalStorageFake.{h,cpp}`, `test/storage_io/CMakeLists.txt:1-25`). It is the model
  for a persisted record's codec and its version refusal.
- A new suite or source line needs `test/CMakeLists.txt` (`:99` is `bible_search_index`). That is
  an orchestrator append point.

## 8. Installed tools and packages

| | Version | How read |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version`; `pio` is not on PATH |
| platform-espressif32 | 55.03.37 (pioarduino) | `platformio.ini:15`, installed `platform.json` |
| framework-arduinoespressif32 | 3.3.7 | installed `package.json` |
| framework-arduinoespressif32-libs (ESP-IDF) | 5.5.0+sha.87912cd291 | installed `package.json` |
| toolchain-xtensa-esp-elf | 14.2.0+20251107 | installed `package.json` |
| SdFat | 2.3.1 | main checkout `.pio/libdeps/x4pro/SdFat/library.properties`; this worktree has no `.pio` yet |
| freeink-sdk | `67f7e01` | `git submodule status` |
| CMake | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 | `c++ --version` |

## 9. Siblings and file locks

From `hpipe show`:

- **t1 (R20)** also holds `src/activities/reader/BibleSearchActivity` and owns
  `enterState(..., fullRefresh)`. This task's edits there should be confined to `onExit` and the
  Opening work (`:117-125`, `:189-193`, `:220-232`).
- **t3 (R12, heavy)** holds:
  - `lib/Epub/Epub/BookMetadataCache`
  - `src/study/StudyStore`
  - `src/activities/reader/BibleNavCache`
  - `src/activities/reader/ReaderActivity`
  - `docs/file-formats.md`

  A faster `resolveDocuments` built on new APIs there would collide. If R12 keeps a
  `BibleNavCache` alive across Home, search could borrow names from it. The search activity is
  not handed the cache today (`EpubReaderActivity.cpp:980`).
- **t5 (R2)** moves the SD clock from 20 to 40 MHz. Every SD figure here, including the 220 ms
  term-cache read and the 1.95 s of small reads, is a 20 MHz figure. The PR's after-figures must
  state which SD clock they ran on.

## 10. Tier

`standard` holds for what this note recommends:
- the resident reader and name table, plus `onExit`, stay in `src/study/BibleSearch*` and
  `BibleSearchActivity`;
- a persisted fingerprint record is a new derived-cache file with a refused-newer version, not a
  migration.

Raise to `heavy` if the spec chooses either of these:
- a faster `resolveDocuments` through new `lib/Epub` or `StudyStore` APIs (another surface, held by
  t3);
- a PSRAM-pressure hook that other allocators call (a cross-cutting contract).

## 11. Open questions for the spec

1. Persist the first-open result? If so, where, and how much: only the fingerprint, or the whole
   document list? This is the decision to surface. The recommendation from §5: a version-refusing
   record in `epub->getCachePath()`, keyed by `BOOK_CACHE_VERSION` + EPUB size + spine count.
2. The PSRAM-pressure policy. A local free-PSRAM floor checked at exit and at open fits inside
   this task. A global release hook does not.
3. How to keep the reader's source alive: hold `indexFile_` open across activities, or add an
   `IndexReader` rebind that keeps `termIndex_`.
4. Where the resident name table lives. It is a `src/activities/reader` type, so `src/study`
   should not include it. An explicit-PSRAM holder in `BibleSearchActivity.cpp`, keyed like the
   document cache, keeps it inside this task's files.
