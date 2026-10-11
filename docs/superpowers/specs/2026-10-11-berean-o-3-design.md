# berean-o-3 design — make opening the Bible from Home cheap (R12 + R5)

Bead `berean-o-3`, branch `perf/r12-bible-open`, base `478cf1ed`. Inputs: the brief (`hpipe brief --task t3`),
the research note `docs/superpowers/research/2026-10-11-berean-o-3-research.md` (cited as **RN §n**), the
device baseline `docs/superpowers/notes/responsiveness-baseline.md`, and roadmap items R5 and R12
(`docs/superpowers/plans/2026-10-11-responsiveness-roadmap.md:239-252,349-368`). Every `file:line` below was
re-read on this branch.

## 1. Problem

Every Home → Bible pays `[READER] Book loaded in` 501–507 ms before anything is drawn (baseline,
`responsiveness-baseline.md:43,69-71`). Almost all of it is two calls (RN §1):

| Cost | Measured | Cause |
|---|---|---|
| `BookMetadataCache::load` | 274–278 ms, 15 opens | reads all 3,937 spine entries through an unbuffered `HalFile`, 4 mutex-guarded reads plus a discarded heap `std::string` per entry, only to keep a 15,748-byte `cumulativeSizes` array (`BookMetadataCache.cpp:484-494`; `HalStorage.cpp:138-141,158`) |
| `STUDY.openPublication` | 207–211 ms, 15 opens | registry, palette, a 43,864-byte passages file parsed and then fully re-serialised for its budget check, unit-index header (`StudyStore.cpp:40-72`; `PassageDoc.cpp:304-349`). Split between those steps unmeasured. |

After it: ~72 ms of saves before the render task starts (`ReaderActivity.cpp:62-66`), of which the recents
save always runs even when nothing changed (`RecentBooksStore.cpp:27-51`). And a Go to from Home rebuilds
the book list (`books ready` +259–262 ms, RN §2.8) because `BibleNavCache` dies with the reader
(`EpubReaderActivity.h:50`).

Two facts from research shape the design:

- **Home already keeps the last book alive.** Nothing calls `StudyStore::closePublication()` except
  `openPublication()` itself (`StudyStore.cpp:41,74`), and `UnitIndexCache` holds a
  `std::shared_ptr<Epub>` (`UnitIndexCache.h:27,87`). So after the reader is destroyed, the previous
  `Epub` — its `BookMetadataCache`, `cumulativeSizes`, and an open `book.bin` handle
  (`BookMetadataCache.h:56`) — survives at Home and is then thrown away and re-read on the next open
  (RN §3).
- **A `BOOK_CACHE_VERSION` bump is not cheap on this unit.** With embedded style on — the default
  (`CrossPointSettings.h:337`) and the owner's setting (RN §2.4) — a rebuild also runs
  `Storage.removeDir(<cache>/sections)` (`Epub.cpp:497-501`), so every laid-out Bible chapter is
  dropped and rebuilds on its next visit at 1.2–1.8 s each (`responsiveness-baseline.md`, "Go to chapter
  → page, chapter not yet built"), and no Indexing popup is shown because that check is existence-only
  (`EpubReaderActivity.cpp:171-182`). RN §5.

## 2. Goal

1. **Warm open** (Home → the book the reader last had, same boot): skip `Epub::load`,
   `openPublication` and the Go-to book-list rebuild. Target: `Book loaded in` under 50 ms on the Bible
   (estimate; components in §7).
2. **Cold open** (first open after boot or wake, or a different book): `BookMetadataCache::load` on the
   Bible under 100 ms (estimate; the transfer floor is ~64 ms, RN §6), so `Book loaded in` ≈ 300 ms.
3. **R5:** no recents save, and no `exists()` sweep, when the opened book is already first and unchanged.
4. **Measure the 209 ms:** per-step timing for `openPublication`, so the next round can target it.

Acceptance (brief): `Book loaded in` on Home → Bible drops substantially, stated in the PR with the log
lines that prove it; no host-suite regression in book cache, recents or study suites; any cache format
change bumps its version (none is made, A3).

## 3. Non-goals

- **No `book.bin` format change** (A3), hence no `BOOK_CACHE_VERSION` bump and no `docs/file-formats.md`
  change.
- **No change to the app-state save** in `ReaderActivity::onEnter` (`ReaderActivity.cpp:62-63`) — the
  crash-loop guard; the brief says it stays (A15).
- **No reduction of `openPublication`'s cold cost** — instrumented only (A16). Its suspected main cost
  (`PassageDoc::fromJson`'s re-serialisation, `PassageDoc.cpp:341,344-349`) is the data surface's, and
  unmeasured.
- **Retention across sleep** — impossible: deep sleep resets the chip (`main.cpp:140-146`).
- **A second slot** for the Bible while another book is open (A2).
- **PSRAM-pressure eviction** (A12).
- **R13** (verse anchors in `BibleNavCache`) — roadmap follow-up (`roadmap.md:370-377`).
- **The existence-only Indexing popup check** (`EpubReaderActivity.cpp:171`) — untouched; it only matters
  for a version bump.

## 4. Assumptions — each one is a decision for review to attack

- **A1 — Three parts, combined.** (a) a buffered cold load, (b) parking the loaded book across Home,
  (c) R5. (a) helps every open; (b) helps only round trips but removes 485 ms of `Book loaded in` plus the
  253 ms Go-to rebuild, which (a) cannot touch. The brief allows combining ("the two may combine").
- **A2 — One parking slot, any book, not Bible-only.** The brief says the held book is "invalidated on
  another book". The slot holds whatever book the reader last had; opening a different book drops it.
  Consequence: alternating Bible ↔ workbook gets no warm open. Memory is unchanged by holding a non-Bible
  book, because Home already holds it through STUDY (RN §3).
- **A3 — No format change: read the existing spine block through a buffer, not a contiguous
  `cumulativeSizes` array (research option 1b, not 1a).** 1a's floor is ~6 ms against 1b's ~64 ms
  (RN §6), but 1a's version bump re-indexes every book and, with embedded style on, drops every section
  cache once (§1). Under A1(b) the 1a/1b difference applies only to cold opens (~60 ms, estimated), and
  sibling task t5 (R2, SD at 40 MHz) would roughly halve 1b's floor. A later format change stays
  possible; this one is not needed to meet the goal.
- **A4 — A short spine block fails the load.** Today `load()` checks no read result
  (`BookMetadataCache.cpp:474-494`), so a truncated `book.bin` loads garbage — `buildBookBin` already
  deletes a short-written file for exactly that reason (`:357-362`). The new pass returns false when any
  read comes back short, closing `bookFile` and leaving `loaded == false`. `Epub::load` then rebuilds
  (`Epub.cpp:412-416`) or, without `buildIfMissing`, reports failure as for a missing cache.
- **A5 — The parked book is validated when it is taken, not invalidated where the card changes.** Between
  park and take nothing reads through the parked `Epub`: the slot is empty while any reader lives (A8),
  and every other holder of it (STUDY, `BibleSearchStore`) is reached only from a reader (RN §3). So
  detecting a change at take is equivalent to dropping at the event, provided detection is complete.
  The take check: (i) same path; (ii) same EPUB file size, measured at park and at take — the identity
  `BibleSearchStore::documents` uses (`BibleSearchStore.cpp:107-117`); (iii) `Epub::reopenCache` passes
  (A6, A7). This puts **no hooks in the five-plus mutation sites** RN §4 lists, which belong to other
  surfaces and would each have to be remembered by any future mutator. Coverage is checked event by event
  in §6.4. **Residual risk:** an EPUB replaced by a different one of the identical byte size whose cache
  was then rebuilt with an identical header before the take. Consequence if it happens: stale
  `cumulativeSizes` and metadata strings (progress % and the inflate-popup threshold), while spine and TOC
  entries are read fresh from the new file. No study data is written from them.
- **A6 — Take never reads through the parked file handle; it reopens `book.bin`.** The parked handle may
  point at a deleted file whose clusters were reused — Settings → Clear cache removes the file
  (`ClearCacheActivity.cpp:97-126`) and a cover thumbnail can then rebuild `book.bin` with
  `load(true, true)` (`CoverThumb.cpp:32-41`). `BookMetadataCache::reopen()` closes the old handle, opens
  `book.bin` again, and refuses unless the file size and the header (version, `lutOffset`, `spineCount`,
  `tocCount`) equal what `load()` read. Closing a read-only handle to a deleted file writes no directory
  entry on FAT or exFAT (`.pio/libdeps/x4pro/SdFat/src/FatLib/FatFile.cpp:128-133,1231-1268`;
  `ExFatLib/ExFatFileWrite.cpp:367-378`, primary checkout).
- **A7 — Take re-runs the CSS check under the *current* `embeddedStyle`.** `Epub::load` checks the CSS
  cache when `!skipLoadingCss` and rebuilds it plus drops sections when it is missing or stale
  (`Epub.cpp:375-401`). `reopenCache(skipLoadingCss)` runs the same `hasCache()` / `loadFromCache()` /
  `clear()` sequence and returns false, rather than rebuilding, when the cache is missing or stale; the
  caller then does a cold load, which performs the rebuild exactly as today. With the setting off, no CSS
  work, as in `load`. So a warm take is never more permissive than a cold load.
- **A8 — The slot is empty while a reader lives.** `take()` always empties the slot, returning the book
  or dropping it. `park()` runs in `~EpubReaderActivity` (`EpubReaderActivity.cpp:145-162`) after
  `saveProgress` and `section.reset()`, in place of `epub.reset()`. It does **not** park on the
  read-folder move (`pendingReadFolderMove`, `:153-158`), which renames the book and its cache. A failed
  EPUB size probe at park means no park.
- **A9 — The study state is reused when STUDY is open, ready, and opened for this same `Epub` object.**
  New `StudyStore::isOpenFor(epub)`: `units_ && units_->ready()` and a `std::weak_ptr<Epub>` recorded by
  `openPublication` locks to `epub`. Otherwise `openPublication` runs as today, including the retry of a
  failed unit index (`StudyStore.cpp:66-69`). Safe because STUDY is the only runtime writer of the study
  files (`StudyStore.cpp:116,214,226`; `MigrationRunner.cpp:368-369` runs only at boot, `main.cpp:513`)
  and the web server and WebDAV refuse dot-dirs (`WebRouteUtils.cpp:10`; `WebDAVHandler.cpp:50`). The
  `saveDisabled()` toast (`EpubReaderActivity.cpp:236-238`) is still shown on a warm open:
  `saveDisabled_` survives `closePublication` by design (`StudyStore.cpp:79-80`).
- **A10 — A warm open still reads `progress.bin` and bookmarks, and still runs `repairTexts()` and Bible
  registration** (`EpubReaderActivity.cpp:196-228,242-262`; together ~15 ms, RN §1). They are cheap,
  `progress.bin` is rewritten on every exit, and keeping them makes the warm path differ from the cold
  one in exactly two places (the `Epub` and the study open).
- **A11 — `BibleNavCache` travels with the parked book, allocated in explicit PSRAM.** It now outlives
  the reader, which makes it a session cache under the brief's rule. Its single allocation site
  (`EpubReaderActivity.cpp:949-956`, today `makeUniqueNoThrow`, PSRAM-preferred by size,
  `BibleBookIndex.h` `static_assert`) moves to `heap_caps_calloc(..., MALLOC_CAP_SPIRAM |
  MALLOC_CAP_8BIT)` with a destroying-and-freeing deleter, modelled on `CatalogIndexStore::PsramFree`
  (`CatalogIndexStore.h:83-89`, `.cpp:40-46`). OOM keeps today's behaviour: `LOG_ERR("ERS", "OOM: Go to
  cache")` and a navigator without a cache. The cache's "nothing invalidates it during a reader's life"
  rule (`BibleNavCache.h:15-17`) becomes "for the life of the `Epub` it was built from": it is parked
  and dropped together with that `Epub`, never alone.
- **A12 — No PSRAM-pressure eviction.** The slot is empty during any reader (A8), and the large PSRAM
  users reached from a reader — the ~2.9 MB Bible search build, the ~444 KB term index (CLAUDE.md PSRAM
  table) — therefore never compete with it. What is parked at Home is what Home holds today through
  STUDY (RN §3) plus `BibleNavCache` (~6.0 KB, `issue-238-design.md:320`), against ~8.2 MB PSRAM free
  (`responsiveness-baseline.md:17`).
- **A13 — The crash-loop guard is unaffected.** Its counter is incremented only on the boot
  `ResumeReader` route (`main.cpp:594-601`), which is always a cold open: the slot is empty after any
  reset. Warm opens happen only after a reader exit in the same boot, after `onExit` zeroed the counter
  (`ReaderActivity.cpp:73-74`).
- **A14 — R5's rule: skip `pruneMissing()` and the save when the front entry equals the normalised
  candidate on path, title and author.** `RecentBook::operator==` compares path only
  (`src/RecentBook.h:16`), so title and author are compared explicitly, after `RecentBooksDoc::normalise`
  (`RecentBooksDoc.h:75`) — the stored entry was normalised the same way (`RecentBooksStore.cpp:39-41`).
  Skipping the prune cannot evict anything because nothing is inserted. A missing entry left in the list
  is already tolerated by both readers (`BibleFinder.cpp:28-38`, existence-checked;
  `PublicationsActivity.cpp:62-76`, which lists files on the card and only borrows titles from recents).
- **A15 — The app-state save stays** even though, on a same-book round trip, it rewrites an unchanged
  document (RN §2.7). The brief rules it out of scope.
- **A16 — `openPublication` gains one `LOG_DBG` with per-step times** (pubkey, palette, passages, unit
  index). Measurement only; no behaviour change.
- **A17 — Threading.** Park runs in the reader's destructor, which `ActivityManager::loop` reaches under
  `RenderLock` (`ActivityManager.cpp:141-145,176-182`); take runs in `loadBook` from `onEnter` on the
  loop task, after that lock is released (`:159-160`). Every SD mutator also runs on the loop task (RN §4). The slot needs no lock of
  its own; `BibleNavCache` remains loop-task-only (`BibleNavCache.h:17`).
- **A18 — No invalidation hooks are added to the mutation sites** (follows from A5). If review rejects A5,
  the fallback is a `ParkedBook::forget()` call in `clearBookCache` (`BookCacheUtils.cpp:23-30`),
  `ClearCacheActivity::clearCache`, `CardBooks::remove` and the downloader rename
  (`PublicationDownloader.cpp:112-116`), with the A6 reopen kept as the safety net.

## 5. Architecture

### 5.1 Components

| # | Component | Change | Modelled on |
|---|---|---|---|
| 1 | `BookMetadataCache::load` (`lib/Epub/Epub/BookMetadataCache.cpp:460-499`) | Spine pass through a local `serialization::BufferedFileReader` over `bookFile`; the href is skipped by reading into a fixed 64-byte stack scratch (no allocation); every read checked (A4). Records `book.bin`'s size for `reopen()`. | `buildBookBin`'s buffered spine reads (`BookMetadataCache.cpp:186-192,215-223`) |
| 2 | `BookMetadataCache::reopen()` — new | Close, reopen, compare size and header; on any mismatch close and set `loaded = false` (A6). | `load()`'s version gate (`:460-472`) |
| 3 | `Epub::reopenCache(bool skipLoadingCss)` — new (`lib/Epub/Epub.{h,cpp}`) | `bookMetadataCache->reopen()`, then the CSS check of `Epub::load` without its rebuild branch (A7). | `Epub::load` cache-hit branch (`Epub.cpp:374-410`) |
| 4 | `ParkedBookOf<Book, NavCache>` — new template, `src/activities/reader/ParkedBook.h` | One slot: `park(path, fileSize, {book, nav})`; `take(path, fileSize, skipLoadingCss)` always empties the slot and returns the pair only when path and size match and `book->reopenCache(skipLoadingCss)` passes (A5–A8). Reports why a take missed, for the log. | `BibleNavCacheOf<Books>` — a template "so the host suite can exercise the logic without Epub" (`BibleNavCache.h:19-22`, tested by `test/number_grid/BibleNavCacheTest.cpp:13-18`) |
| 5 | Firmware instance, `src/activities/reader/ParkedBook.cpp` — new | `ParkedBookOf<Epub, BibleNavCache>` as a function-local static; `park`/`take` wrappers that probe the EPUB size and log. | `StudyStore::getInstance` (`StudyStore.cpp:35-38`); size probe as `BibleSearchStore.cpp:83-88` |
| 6 | `EpubReaderActivity` (`src/activities/reader/EpubReaderActivity.{h,cpp}`) | `loadBook`: take first, else today's construct-and-load (`:165-187`); skip `openPublication` when `STUDY.isOpenFor(epub)` (A9). Destructor: park instead of `epub.reset()` (A8). Go to: PSRAM allocation of `navCache` (A11). | — |
| 7 | `StudyStore` (`src/study/StudyStore.{h,cpp}`) | `isOpenFor(epub)` + `std::weak_ptr<Epub> openedFor_` (set in `openPublication`, reset in `closePublication`); step-timing `LOG_DBG` (A9, A16). | `isOpen()` (`StudyStore.h`, `units_ != nullptr`) |
| 8 | `RecentBooksDoc::isAlreadyFront(books, candidate)` — new pure helper (`src/util/RecentBooksDoc.{h,cpp}`) + early return in `RecentBooksStore::addBook` (A14) | | the existing pure-helper split: "RecentBooksStore.cpp cannot be [host-tested]" (`RecentBooksDoc.h:12-15`) |
| 9 | Comments | `BibleNavCache.h:15-17` (A11); `BookMetadataCache.h:66-69` ("<1KB for typical books" — 15.7 KB on the Bible). | — |

### 5.2 Files and ownership

| File | Surface | In t3's registered files? |
|---|---|---|
| `lib/Epub/Epub/BookMetadataCache.{h,cpp}` | epub | yes |
| `lib/Epub/Epub.{h,cpp}` | epub | **no** — this task's own surface |
| `src/activities/reader/ParkedBook.{h,cpp}` (new) | ui (reader) | **no** — new files |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | ui | **no — held by t4 (R21)**; implement must wait for it or be serialised with it |
| `src/activities/reader/BibleNavCache.h` | ui | yes (comment only) |
| `src/study/StudyStore.{h,cpp}` | data | yes |
| `src/util/RecentBooksDoc.{h,cpp}`, `src/RecentBooksStore.cpp` | data | `RecentBooksStore` yes; `RecentBooksDoc` **no** |
| `test/book_metadata_cache/` (new), `test/parked_book/` (new), `test/recent_books_doc/` | tests | `recent_books_doc` yes; the new dirs **no** |

`ReaderActivity.cpp` needs no change, so R5 and R12 no longer collide there (roadmap `:537`).
`docs/file-formats.md` needs no change (A3).

## 6. Data and control flow

### 6.1 Cold open (unchanged except the load and the timing log)

`LauncherActivity.cpp:412` → `goToReader` → `replaceActivity` → Launcher destroyed → `ReaderActivity::onEnter`
(`ReaderActivity.cpp:43-67`) → `EpubReaderActivity::loadBook`:

1. `parkedBook` take for `bookPath` misses (empty slot, other path, changed size, or `reopenCache` false)
   and drops whatever it held; `LOG_DBG` names the reason.
2. Today's path (`EpubReaderActivity.cpp:165-187`): `Epub::load` → `BookMetadataCache::load`, now buffered.
3. `progress.bin`, bookmarks (`:196-228`).
4. `STUDY.isOpenFor(epub)` is false (a fresh `Epub`) → `openPublication` (`:235`), now logging its step
   times.
5. `repairTexts`, registration (`:242-262`). Back in `onEnter`: `Book loaded in`, app-state save,
   `RECENT_BOOKS.addBook` (R5 may skip the prune and save), `onBookLoaded`, `requestUpdate`.

### 6.2 Reader exit (park)

`~EpubReaderActivity` (`:145-162`): `ImageBlock::setExtractor(nullptr, nullptr)`; return-stack progress
save; `section.reset()`; then, unless `pendingReadFolderMove`, `parkedBook` park with the EPUB size probed
now, the `epub` and the `navCache` (possibly null). STUDY keeps its own reference as today. The slot now
holds one book; any previous occupant was already emptied by this reader's take (A8).

### 6.3 Warm open (take)

As 6.1, but at step 1 the take returns `{epub, nav}`:

1. `epub = taken.book`, `navCache = taken.nav`. Skips `makeUniqueNoThrow<Epub>`, the `book.bin` existence
   check, and `Epub::load` (`:165-187`). Still runs `ImageBlock::clearSessionRenderFailures`,
   `setExtractor`, `setupCacheDir` (`:189-194`).
2. `progress.bin`, bookmarks — as cold (A10).
3. `STUDY.isOpenFor(epub)` is true → `openPublication` skipped, `LOG_DBG` says so; the `saveDisabled`
   toast check still runs (A9).
4. As cold from `repairTexts` on.
5. A Go to (menu, or a Home Go-to intent through `onBookLoaded` → `openChapterPicker`,
   `EpubReaderActivity.cpp:1062-1065`) hands the adopted `navCache` to `BibleNavigationActivity`, which
   logs `Go to: books from cache` (`BibleNavigationActivity.cpp:116-121`).

### 6.4 What each card or setting change does to a parked book

| Event while parked | What changes on SD | Take outcome |
|---|---|---|
| Sleep, then wake | chip reset (`main.cpp:140-146`) | slot empty → cold |
| Open a different book | — | path differs → dropped; that book opens cold |
| Settings → Clear cache (`ClearCacheActivity.cpp:126`) | `epub_*` dirs removed | `book.bin` missing → `reopen` false → cold rebuild |
| …then a cover thumbnail rebuilds `book.bin` (`CoverThumb.cpp:32-41`, `load(true, true)`) | new `book.bin`, no CSS cache | header and size equal (same EPUB) → `reopen` passes; with embedded style on, the CSS cache is missing → `reopenCache` false → cold (rebuilds CSS, drops sections, as today); with it off → warm, reading the **new** file |
| Web upload / WebDAV put / download / file-browser delete of that book (`clearBookCache`, RN §4) | cache dir removed, EPUB replaced or gone | EPUB size differs, or `book.bin` missing → cold; a deleted EPUB fails `onEnter`'s `exists` check first (`ReaderActivity.cpp:46-50`) |
| Leaving File Transfer (no reboot on a touch board, `main.cpp:148-167`) | — | nothing to detect |
| Reader menu → Delete cache (`EpubReaderActivity.cpp:890-905`) | `book.bin` removed, then the reader goes Home and parks | `reopen` false → cold rebuild |
| `embeddedStyle` changed | — | `reopenCache` runs the CSS check under the new value (A7) |
| Book renamed by the downloader (`PublicationDownloader.cpp:100-123`) | new path | path differs → cold |

## 7. Memory and cost

| Item | Size | Placement | Lifetime | Why this and not stack/static |
|---|---|---|---|---|
| Spine-pass buffer | 4,096 B | internal SRAM (`makeUniqueNoThrow` inside `BufferedFileReader`; 4,096 is not above the PSRAM routing threshold) | `load()` only | the build path's size (`BUILD_IO_BUFFER_SIZE`, `BookMetadataCache.cpp:18-20`); too large for a 256-byte stack budget; OOM degrades to correct unbuffered passthrough (`BufferedFile.h:19-21`) |
| href skip scratch | 64 B | stack | per entry | under the 256-byte local limit |
| Parking slot | a path `std::string` + two `shared_ptr`s + `uint64_t` | function-local static | boot | no allocation beyond the path string |
| Parked `Epub` (with `cumulativeSizes`, 15,748 B, PSRAM by size) | — | existing objects | reader exit → next open | already held at Home today through STUDY (RN §3): **no new memory** |
| `BibleNavCache` | ~6.0 KB (`issue-238-design.md:320`) | **explicit PSRAM** (A11) | with the parked `Epub` | session cache under the brief's rule |
| `StudyStore::openedFor_` | one `weak_ptr` | static (member of a singleton) | boot | — |

**Warm `Book loaded in`, estimated:** EPUB size probe + `book.bin` reopen (two opens, unmeasured, a few ms
each) + CSS check (4 ms, `log:1005-1006` of RN's capture) + `progress.bin` 3 + bookmarks 6 +
`repairTexts`/registration 6 (RN §1) ≈ 25–40 ms. **Cold:** 505 − 276 + ~70–90 ≈ 300 ms. Both are
predictions; only the device run gives the after figure.

## 8. Error handling

- **Spine pass:** any short read or failed skip → `LOG_ERR("BMC", ...)`, close `bookFile`, `loaded = false`,
  return false (A4). Buffer OOM → passthrough, no error.
- **`reopen()`:** missing file, size or header mismatch → close, `loaded = false`, false, `LOG_DBG` with the
  reason (an expected outcome after a cache change, not an error).
- **Take:** a failed EPUB size probe counts as a changed file → miss → cold. The cold path's own error
  handling is unchanged (`EpubReaderActivity.cpp:166-186`: OOM and load failures log and return false,
  and `onEnter` finishes).
- **Park:** a failed size probe or a null `epub` → nothing parked; the `Epub` is released as today.
- **`BibleNavCache` PSRAM OOM:** `LOG_ERR("ERS", "OOM: Go to cache")`, navigator without a cache — today's
  behaviour (`EpubReaderActivity.cpp:953-955`).
- **Study:** when `isOpenFor` is false, `openPublication`'s own error paths apply unchanged
  (`StudyStore.cpp:54-69`).
- **R5:** a real save keeps its `LOG_ERR` (`RecentBooksStore.cpp:48-50`); the skip logs `LOG_DBG`.
- No new persisted store, so the storage-discipline rules for stores (CLAUDE.md) are not engaged. The
  only write path touched, the recents save, loses a call and changes nothing else.

## 9. Testing strategy

TDD per component: the failing host test first.

| Suite | New/existing | What it proves |
|---|---|---|
| `test/book_metadata_cache/` | **new** | `load()` over a `book.bin` seeded in the in-memory Storage fake (`test/stubs/HalStorageFake.h:23`, `putFile`) in the documented v10 layout (`docs/file-formats.md:7-79`): (1) cumulative sizes equal the seeded ones, including hrefs over 15 bytes and an empty href; (2) `storage_fake::readCallsOn(book.bin)` (`HalStorageFake.h:44-46`) is bounded by `ceil(spine block / 4096)` plus the header reads, not proportional to the entry count; (3) a version mismatch returns false; (4) a spine block cut short returns false and `isLoaded()` is false (A4); (5) `getSpineEntry` / `getTocEntry` still return the right entries after `load()`; (6) `reopen()` passes on an untouched file and fails after the file is removed, replaced with a different size, or rewritten with a different header. |
| `test/parked_book/` | **new** | `ParkedBookOf` with a fake `Book` exposing `reopenCache(bool)`: an empty slot misses; path mismatch, size mismatch and a failing `reopenCache` each miss **and** release the held objects (`use_count`); a match returns book and nav and empties the slot; `park` replaces and releases the previous occupant; the `skipLoadingCss` flag reaches `reopenCache`. |
| `test/recent_books_doc/` | existing (`RecentBooksDocTest`, compiles `RecentBooksDoc.cpp`, `CMakeLists.txt:1-5`) | `isAlreadyFront`: true only when the first entry matches path, title and author; false for an empty list, a match in second place, or a changed title or author. A candidate whose raw title runs past `MAX_TITLE_BYTES` matches its stored, already-capped entry once both have been through `normalise`. |
| `test/storage_io/`, `test/number_grid/` (`BibleNavCacheTest`), `test/passage_doc/`, `test/crosspoint_state/` | existing | unchanged behaviour; run them. |

**Host build of `BookMetadataCache.cpp`.** No suite compiles it today (RN §8). It passes a host syntax check
against `test/stubs` given a `WString.h` that includes the stub `<Arduino.h>` and a forward-declared
`Print` (ZipFile.h's `readFileToStream` parameter): `c++ -std=gnu++20 -fsyntax-only -include <class
Print;> -Itest/stubs -I<WString shim> -Ilib/Serialization -Ilib/Memory -Ilib/Utf8 -Ilib/ZipFile
-Ilib/FsHelpers -Ilib/Epub/Epub lib/Epub/Epub/BookMetadataCache.cpp` reported no errors on 2026-10-11.
Linking `buildBookBin`'s `ZipFile` calls uses a test-local **link-time substitute**, the pattern
`test/pagination` uses for `GfxRenderer` (`test/pagination/CMakeLists.txt:1-11`). `Epub::reopenCache` is not
host-tested: `Epub.cpp` drags in the parsers and `ZipFile`, and no suite compiles it (RN §8); it is a
two-step composition of `reopen()` (tested) and the existing CSS calls, verified on device.

**Firmware build:** `pio run -e x4pro` once after the last edit; `./bin/clang-format-fix` over the whole tree.

**For the orchestrator** (`test/CMakeLists.txt`, a shared append point):

    add_subdirectory(book_metadata_cache)
    add_subdirectory(parked_book)

### 9.1 Device verification (the owner's run)

Flash the `x4pro` dev build and capture serial (`cat /dev/cu.usbmodem*`).

1. Boot → Home → Bible. **Cold:** `Loading ePub` → `[BMC] Loaded cache data: 3937 spine, 129 TOC entries`
   should drop from 276 ms to under 100 ms; `[STUDY]` logs the step times; `[READER] Book loaded in` ≈ 300 ms
   (before: 501–507 ms).
2. Home → Bible, five times. **Warm:** the take's `LOG_DBG` hit line and the study-skip line precede
   `[READER] Book loaded in` under 50 ms. The second and later opens log the recents skip.
3. Home Go-to after a round trip: `[BNV] Go to: books from cache` and `books ready` near the warm figures
   (+15–33 ms, RN §2.8) instead of +259–262 ms.
4. Settings → Clear cache → Home → Bible: a take-miss line naming the cache, then a full re-index; the page
   opens at the right place after the rebuild.
5. Reader menu → Delete cache → Home → Bible: a take miss, then a rebuild.
6. Toggle embedded style off and on between round trips: no crash, correct styling.
7. Heap: `MEM Free` at Home after a round trip matches the before figure (~215 KB internal, RN §3).

## 10. Sequencing and collisions

- **t4 (R21) holds `EpubReaderActivity`.** hpipe's file lock enforces implement-through-merge
  serialisation; this task's implement waits for t4 to land, then re-reads the file.
- **t6 (R11)** keeps the Bible search index resident, keyed on the Bible's identity; this design uses the
  same path-plus-size identity (`BibleSearchStore.cpp:114`), so the two cannot disagree about "the same
  Bible".
- **t5 (R2)** halves the cold floor if it lands (A3); no file overlap.
- **R13** builds on A11 (the nav cache now outliving the reader).
