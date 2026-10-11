# berean-o-6 (R11) design: keep the Bible search index open, and make the first search after boot cheap

Bead `berean-o-6`, task `t6`, branch `perf/r11-search-index-resident`. Research:
`docs/superpowers/research/2026-10-11-berean-o-6-research.md` (cited below as "research §n").
Baseline: `docs/superpowers/notes/responsiveness-baseline.md` (on `main` since `3e56b629`).
Every `file:line` was re-read on this branch at `7c06c2f9`. Nothing under `src/study`,
`src/activities/reader/BibleSearch*` or `lib/BibleSearch` has changed on `origin/main` since.

**Decision D1 is answered** (§ "Decisions"): the persisted fingerprint record is in scope, as
option (a). Parts marked **[D1]** are the record and everything that follows from it. They are
settled and in scope like the rest of this spec.

## Problem

Opening Bible search does SD work after the "Opening" frame lands. The loop is blocked the whole
time (`BibleSearchActivity.cpp:183-194`). The measured costs come from the raw baseline captures
(research §2):

| Entry | Frame → keyboard | Where the time goes |
|---|---|---|
| First after boot | 2,436 / 2,440 ms (runs 1 and 2) | `resolveDocuments` ~1.95 s, book-name load ~250 ms, index open + term cache ~220 ms |
| Later, same boot | 500 / 499 / 505 ms | book-name load ~250 ms, index open + term cache ~220 ms, ~30 ms other |

Every boot repeats the first-entry cost, and every wake is a boot (`main.cpp:139-145`).

The causes:

1. **Exit frees everything.** `onExit` calls `BibleSearchStore::close()`
   (`BibleSearchActivity.cpp:117-125`), which frees the reader, its ~444 KB term cache and its
   file (`BibleSearchStore.cpp:260-263`). `open()` begins with `close()` (`:229-230`).
2. **The book-name table is an activity member.** It is reloaded on every entry
   (`BibleSearchActivity.cpp:189-192`, `BibleSearchActivity.h:111`). Loading streams the
   book-nav page and joins it to the TOC (`BibleBookNameTable.cpp:22-62`).
3. **The fingerprint lives only in RAM.** The fingerprint, and the document list it covers, are
   cached in the `BibleSearchStore` singleton keyed by EPUB path and size
   (`BibleSearchStore.cpp:107-117`), so the cache dies with every boot. Recomputing it costs:
   - 66 reads of the units-index book map (`BibleSearchStore.cpp:146-157` →
     `UnitIndexCache.cpp:375-394`);
   - 1,189 random-access spine-entry reads (`BibleSearchStore.cpp:179-188` →
     `BookMetadataCache.cpp:508-525`).

## Goal

1. **A second or later search entry in one boot does no index reopen and no name load.** It runs:
   - one EPUB size read,
   - one index-file open,
   - one 48-byte header read and comparison.

   Target: frame → keyboard well under 100 ms (from ~500 ms).
2. **[D1] The first search after a boot reads the fingerprint from a 40-byte record** instead of
   recomputing it. Expected frame → keyboard ≈ 0.5 s (from ~2.44 s). That covers the name load,
   the index open and one small read. It is an estimate until the device run measures it.
3. **Memory held between entries is bounded, logged, and released**:
   - **immediately** on an index rebuild or a query I/O failure;
   - **at the next search entry** on a Bible change (detected there, A3);
   - **when the screen is left** if PSRAM is below the floor (A6);
   - **at sleep**, by the chip reset (A7).

   Both Bible-change and pressure release are lazy. A deleted or replaced Bible's ~450 KB stays
   held until the next search entry or sleep. The PR body's memory section says exactly this,
   never "released eagerly".
4. **Every phase is timed in `LOG_DBG`**, so the device run proves the after-figures phase by
   phase.

## Non-goals

- **The entry refresh mode.** R20 (t1) owns `enterState(State::Opening, /*fullRefresh=*/true)`
  (`BibleSearchActivity.cpp:114`); this task does not touch it.
- **Making `resolveDocuments` itself faster.** That needs a sequential spine pass and a
  single-read book map. Both are new APIs in `lib/Epub/Epub/BookMetadataCache` and
  `src/study/StudyStore`, which t3 (R12, heavy) holds (research §5, §9). It would be a follow-up
  after R12.
- **Persisting the book-name table.** The first search after boot still pays its ~250 ms. Names
  are a `src/activities/reader` type that the Go to grid also loads. A shared persisted name cache
  belongs with R12/R13. This task files it via `hpipe discover`.
- **Reacting to PSRAM pressure after the screen is left.** There is no release hook in the tree
  (research §3, table row "PSRAM pressure"). A process-wide one is a cross-cutting contract (see
  A6).
- **Changing `bible.idx`, its fingerprint definition, or `IndexReader`.** Changing the
  fingerprint would make every built index Stale.
- **The SD clock (R2, t5).**

## Assumptions (each one is a decision the review should attack)

- **A1 — Residency lives in the `BibleSearchStore` singleton, not in the activity or the reader.**
  The store already outlives activities and caches per-Bible derived data (the document list,
  `BibleSearchStore.cpp:92-117`). The baseline shows the user going Home between searches, which
  destroys `EpubReaderActivity` (research §2, run 1 lines 1235-1263). So a cache owned by the
  reader, like `BibleNavCache` (`EpubReaderActivity.h:50`), would not survive the measured
  scenario. Modelled on the store's own document cache.

- **A2 — The SD handle is closed at exit, and only the in-memory reader is kept.** On re-entry the
  store re-opens `bible.idx` into the same `indexFile_` member and reads the 48-byte header. It
  then compares those bytes with `writeHeader(reader_->header())`, and the file's size with
  `header().fileSize`. If both match, it keeps the reader with its term cache, page and header.
  Otherwise it closes and opens fresh, as today.
  - **Why not keep the handle open:** with `showHiddenFiles` on, the file browser lists dot-files
    and deletes them with no protected-path check (`FileBrowserActivity.cpp:56`, `:160-170`). A
    handle held across activities could read freed clusters after such a delete.
  - **Why the comparison is exact:** `writeHeader` serialises all 48 bytes and leaves none
    unparsed (`IndexFormat.cpp:32-46`), so comparing the bytes compares every field.
  - **Why reusing the reader is safe:** the reader reaches the file only through
    `source_.ctx = &indexFile_` (`BibleSearchStore.cpp:99-105`). Re-opening the same member object
    keeps that pointer valid, and `IndexReader` needs no change.
  - **The residual risk:** an index rewritten in place with a byte-identical header. That only
    happens with a rebuild of the same Bible, and the builder's output is deterministic
    (`IndexTest.cpp:518`, `ResumingFromACheckpointWritesAByteIdenticalFile`). Even then the
    rebuild path closes the store first (`BibleSearchIndexer.cpp:57-58`).

- **A3 — One "Bible identity" keys every resident thing.** The identity is EPUB path, EPUB size
  and spine count. The path and size are what the document cache uses today
  (`BibleSearchStore.cpp:114`). The spine count is added because the fingerprint covers it
  (`:178`) and the units index validates on it (`UnitIndexCache.cpp:63-83`). The store
  re-identifies on every `open`/`status`/`documents` call. Each such call opens the EPUB once for
  its size, which is today's cost (`:83-88`). `restoreBookNames`/`keepBookNames` do **not**
  re-identify: they compare against the identity the preceding `open()` or `status()` set during
  the same Opening work (A13). If no identity is set, they keep nothing and restore nothing. So a
  re-entry opens the EPUB exactly once (Goal 1). A different identity releases everything resident
  before anything else runs.

- **A4 — The book-name table is kept as a copy in the store, never lent.** The activity keeps its
  own `bookNames` member (`BibleSearchActivity.h:111`); render reads it on the render task
  (`BibleSearchActivity.cpp:860`).
  - At Opening, `restoreBookNames` copies the resident table into it (4,228 B, `memcpy`).
  - After a fresh load, `keepBookNames` copies it out.
  - Because the table is copied, it is never lent, so a release can never leave render holding a
    dangling pointer.
  - `BibleBookNameTable` is trivially copyable (char arrays and an `int`,
    `BibleBookNameTable.h:57-59`); a `static_assert` pins that.
  - The resident copy is an `AllocatedBuffer` from `psramAllocator()`, which is explicit PSRAM
    (`BibleSearchStore.cpp:25`, `Allocator.h:24-67`), the idiom this file already uses for
    `documentsBuffer_` (`:164`).
  - Including `activities/reader/BibleBookNameTable.h` from `src/study` has precedent:
    `UnitIndexCache.cpp:13` includes `activities/reader/SpineHtmlStream.h`.

- **A5 — Names are kept even when the index is not usable.** The prompt, starting and building
  states draw book names too (`BibleSearchActivity.cpp:860`), and their load costs the same
  ~250 ms.

- **A6 — PSRAM pressure is a retention floor checked when the screen is left.** `park()` measures
  `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` and `heap_caps_get_largest_free_block(...)` with
  the resident blocks still held. It keeps them only if free ≥ **3 MiB** and the largest block ≥
  **1 MiB**.
  - **Pattern:** the same retain-only-above-a-floor bet `SdCardFont::resetStyleMiniData` makes
    (`SdCardFont.cpp:73`, `:118-125`).
  - **Sizing:** sized to budgets, not measured sizes. The largest PSRAM working set reachable while
    parked is the catalog's update check. `checkRemote` validates the staged download with
    `loadFrom` while the current index is still held (`CatalogIndexStore.cpp:234-237`). The held
    copy is released only in `applyStaged` (`:277`). By the store's own budgets that is:
    - the held index, ≤1 MiB;
    - the compressed staging buffer, ≤512 KB;
    - the inflated output, ≤1 MiB.

    That totals 2.5 MiB (`CatalogIndexStore.h:77-78`). 3 MiB covers it with margin for the other
    explicit PSRAM users, such as the study JSON allocator (`PsramJsonAllocator.cpp:12,19`). The
    1 MiB largest-block floor covers the inflate's output block.

    The index build (2.88 MB) is not a parked-time consumer: `BibleSearchIndexer::begin` closes the
    store first (`BibleSearchIndexer.cpp:57-58`).
  - **Normal case:** baseline PSRAM free while reading is ~8.2 MB, so the floor never trips
    (research §4).
  - **Known gap:** an allocation that fails *after* parking does not release us. If R12 (t3) adds
    its own PSRAM session holder, the two floors do not see each other's later allocations. Both
    gaps are accepted and documented at the floor constant.

- **A7 — Sleep needs no code.** Deep sleep is a full chip reset on wake (`main.cpp:139-145`,
  `:243-281`), so PSRAM and the index reader are gone. At sleep time the store holds no SD handle
  (A2), and the record write is complete before the screen leaves (A10), so there is nothing to
  flush. An explicit close would be a line in the shared `src/main.cpp`, and it would buy nothing
  but a log line.

- **A8 — A query I/O failure and the rebuild path keep today's `close()` semantics.** `close()`
  still frees the reader, its term cache and its file (`BibleSearchStore.cpp:260-263`). It does
  not free the resident names, which stay valid for the same Bible (A3).
  `BibleSearchIndexer::begin` (`:57-58`), `runSearch`'s failure path (`BibleSearchActivity.cpp:539`)
  and the activity's `beginBuild` (`:255-256`) are unchanged.

- **A9 [D1] — The fingerprint is persisted as a fixed 40-byte record in the Bible's EPUB cache
  directory, `epub->getCachePath() + "/search-fingerprint.bin"`.**
  - **Key:** EPUB size, spine count and `study::UNIT_INDEX_VERSION`
    (`UnitIndexFormat.h:37`, public). A units-format bump rebuilds the book map, and including the
    version makes the record go stale with it.
  - **Why the cache directory:** every path that deletes or replaces a Bible on the device already
    removes that directory:
    - web routes (`FileRoutes.cpp:398, 549, 639, 740`);
    - the file browser (`FileBrowserActivity.cpp:169`);
    - `CardBooks::remove` (`CardBooks.cpp:57`);
    - Clear Cache (`ClearCacheActivity.cpp:115-133`).

    The same `clearBookCache` → `Epub::clearCache` → `removeDir` chain (`BookCacheUtils.cpp:23-26`,
    `Epub.cpp:520-533`) is also reached from:
    - WebDAV (`WebDAVHandler.cpp:376, 425, 534`);
    - the publication downloader (`PublicationDownloader.cpp:110, 252`).

    Moving a finished book renames the directory along with it (`EpubReaderActivity.cpp:121-133`).
  - **Where the record outlives `book.bin`:** `book.bin` can be rebuilt in place, and the record
    survives that.
    - A `BOOK_CACHE_VERSION` mismatch makes `BookMetadataCache::load` fail
      (`BookMetadataCache.cpp:466-472`). `Epub::load` then rebuilds `book.bin` in the same directory
      (`Epub.cpp:374, 413-419`). A `book.bin` deleted on its own is rebuilt the same way.
    - The key cannot carry `BOOK_CACHE_VERSION`. The constant is file-local to
      `BookMetadataCache.cpp:13-14`, a file t3 holds.
    - **The consequence is benign.** It is the same EPUB, so spine order and documents are
      unchanged. Only a cache-format change that alters spine hrefs would make the record disagree
      with a recompute. Even then the index stays usable, and the next build's `documents()`
      rewrites the record.
    - The record's `VERSION` comment and the `docs/file-formats.md` section therefore say the
      version must be bumped with any `BOOK_CACHE_VERSION` change that alters spine hrefs.
  - **Exposure compared with today:**
    - A Bible replaced off-device by a same-size file is equally undetected. `book.bin` is keyed
      only by path hash and `BOOK_CACHE_VERSION` (`Epub.h:48`, `BookMetadataCache.cpp:460-472`), so
      today's recomputation reads the same stale hrefs.
    - A same-size replacement on the device clears the directory, so the record is recomputed.
    - The units index already accepts this exposure in writing: "A same-size content change is
      deliberately not detected here" (`UnitIndexFormat.h:22-29`).
  - **Placement precedent:** `src/` code already writes its own small binary file into
    `<cachePath>/` (`ProgressFile.h:32-62`).

- **A10 [D1] — The record is written once, synchronously, right after a *complete*
  `resolveDocuments`, from one call site.**
  - **Complete only.** `resolveDocuments` succeeds even when a book resolves to no documents. Such
    a book is logged and skipped (`BibleSearchStore.cpp:146-157`), and only `count == 0` fails
    (`:158-161`). `UnitIndexCache::spineIndicesForBook` returns empty on transient failures too:
    - an OOM of its 3,937-B map (`UnitIndexCache.cpp:383-386`);
    - a failed open (`:389`);
    - a failed seek or short read (`:390-391`).

    A partial fingerprint persisted would outlive the boot that produced it. It would either make
    a valid index Stale on every boot, or seal in a partial index built that boot.
    `resolveDocuments` therefore also reports whether every book 1-66 contributed at least one
    document; the predicate `BibleSearchFingerprintFile::coversEveryBook` is pure and host-tested.
    - A partial resolve serves this boot from RAM exactly as today, and is **never** written.
    - The cost: an edition that genuinely lacks a book recomputes on every boot. The shipped NWT
      has 66, and the store already logs a missing book as an error (`:155`).
  - **One writer.** A single private `persistFingerprint()` is called after any complete resolve,
    whether from `fingerprintFor` or from `documents()` on the indexer path. It writes only if
    `mayOverwrite(recordLoad_)`.
    - `recordLoad_` is the last `load` result for the current identity, reset by `identify()`.
    - If no load has happened yet for this identity (the indexer path), `persistFingerprint` loads
      once first, so a `TooNew` or `Unreadable` record is never overwritten from either path
      (A11).
  - **When:** a fresh compute happens on the first search after the record was lost, or from
    `BibleSearchIndexer::begin`. The write runs inside the busy frame already on screen (Opening or
    Starting).
  - **How:** temp file, then remove, then rename, mirroring `ProgressFile::writeAtomic`
    (`ProgressFile.h:32-62`). It verifies the on-card size, as `BibleSearchIndexer::writeAtomic`
    does (`BibleSearchIndexer.cpp:292-305`).
  - **On failure:** logged and ignored. The fingerprint is already in RAM for this boot, and the
    record is derived data.
  - **Owning task:** the main loop task, the only task allowed in `BibleSearchStore`
    (`BibleSearchStore.h:25-28`). `HalFile` takes `storageMutex` on every call.

- **A11 [D1] — A record from a newer build is refused and left on the card.** For one with a
  higher version, the fingerprint is recomputed in RAM for this boot and the file is not
  overwritten. This mirrors "Neither is overwritten" for `bible.idx` (`docs/file-formats.md:595`).
  - A wrong magic, a wrong length or a failed checksum (Corrupt), or a key mismatch (Stale), is
    recomputed **and** overwritten.
  - **The decode order is what makes "newer is left alone" hold.** It mirrors `bible.idx`, which
    checks magic, then version, then size (`IndexFormat.cpp:48-50`, `IndexReader.cpp:27-40`). The
    order is fixed in § "Architecture", `BibleSearchFingerprintFile`.
    - The version is read before the length, so a newer format of any length is `TooNew`.
    - The magic is read before the version, so a foreign file can never pose as `TooNew` and pin
      itself on the card.
  - **The cost:** after a downgrade, a newer record costs ~2 s per boot until the firmware is
    upgraded again.

- **A12 [D1] — The record serves only the fingerprint, never the document list.** `open()` and
  `status()` need only the fingerprint (`BibleSearchStore.cpp:212, 222, 243`). The indexer keeps
  calling `documents()`, which always resolves for real (`BibleSearchIndexer.cpp:59-64`). A build
  takes minutes, so its extra ~2 s is noise. A wrong record can therefore never make a build index
  the wrong documents.

  A build started in a boot whose resolve was partial still indexes that partial list from the RAM
  cache (`BibleSearchIndexer.cpp:59` → `documents()`), exactly as today. A10 keeps that from
  persisting: the partial fingerprint is never written, so the next boot recomputes, finds the
  partial index Stale, and offers the rebuild — today's behaviour.

- **A13 — Opening runs the index work first, then the names.** Today names load before the index
  (`BibleSearchActivity.cpp:190-193`). The order changes so that `open()` establishes the identity
  that `restoreBookNames` then reuses. Nothing between the two reads names. The prompt and the
  keyboard are entered only after both have run, as today.

## Decisions

- **D1 — DECIDED 2026-10-11 by the human: option (a).** The question was whether to add a new
  on-disk record (A9-A12); the data-dev rules make a new on-disk format the user's call
  (`.claude/agents/data-dev.md`, "Prime directive" 2). The answer, verbatim:

  "Add the 40-byte version-refusing BSFP record at /.crosspoint/epub_<hash>/search-fingerprint.bin
  (key = EPUB size + spine count + UNIT_INDEX_VERSION, FNV-64 check), written temp+rename like
  ProgressFile.h, read with one 40-byte read, serving only the fingerprint and never the build's
  document list. Document the format in docs/file-formats.md."

  - **Where each part of the answer lives:**
    - the record: A9-A12 and the `BibleSearchFingerprintFile` section;
    - the format doc: the § "Docs" section, now a requirement.
  - **Rejected:**
    - (b) the same record under `/.berean/search/`. It would outlive a same-size replacement on the
      device, which regresses today's recompute-after-reboot.
    - (c) no record.
    - (d) a faster recompute through t3's files.
  - **Settled.** Neither the review nor the plan reopens it. If any part proves impossible as
    stated, it goes back as a new decision.

## Architecture

### New: `src/study/BibleSearchFingerprintFile.{h,cpp}` [D1]

A pure codec plus load and save through `HalStorage`, with no `Epub` and no `STUDY`. It is
modelled on:
- the binary header codec in `lib/StudyStore/StudyStore/UnitIndexFormat.{h,cpp}`
  (magic + version + `memcpy` fields, tested in `test/unit_index_format`);
- the atomic writer in `src/activities/reader/ProgressFile.h`.

```cpp
namespace BibleSearchFingerprintFile {
inline constexpr uint32_t MAGIC = 0x50465342;  // "BSFP" little-endian
// Bump when what the fingerprint covers, how verse documents are resolved, or (through a
// BOOK_CACHE_VERSION change) the spine hrefs it hashes, changes.
inline constexpr uint16_t VERSION = 1;
inline constexpr size_t RECORD_BYTES = 40;
inline constexpr const char* FILE_NAME = "search-fingerprint.bin";

// FNV-1a 64, the hash every bible.idx on a card is already keyed by. Moved out of the store's
// anonymous namespace and declared here so the store and the record share one, tested definition.
struct Fnv1a64 {
  uint64_t hash = 14695981039346656037ull;
  void add(const void* data, size_t length);
  void addU32(uint32_t value);  // little-endian bytes
  void addU64(uint64_t value);  // little-endian bytes
};

struct Key { uint64_t epubSize; uint32_t spineCount; uint16_t unitIndexVersion; };
struct Record { Key key; uint32_t verseDocuments; uint64_t fingerprint; };
enum class Load { Ok, Missing, Unreadable, Corrupt, TooNew, Stale };

void encode(uint8_t (&out)[RECORD_BYTES], const Record& r);
// `length` is the file's real size; `in` holds min(length, RECORD_BYTES) bytes.
Load decode(const uint8_t* in, size_t length, const Key& expected, Record& out);
Load load(const std::string& cachePath, const Key& expected, Record& out);
bool save(const std::string& cachePath, const Record& r);  // temp + rename; false on any failure
// Whether a load result may be replaced by a fresh record (everything but TooNew and Unreadable).
bool mayOverwrite(Load result);
// True when every canonical book 1..66 contributed at least one verse document; only then may a
// resolve be persisted (A10). `books` is the resolved list's book column.
bool coversEveryBook(const uint8_t* books, uint32_t count);
}
```

**Layout, version 1, little-endian, every field through `memcpy`:**

| Offset | Field |
|---|---|
| 0 | magic `BSFP` u32 |
| 4 | `version` u16 |
| 6 | `unitIndexVersion` u16 |
| 8 | `epubSize` u64 |
| 16 | `spineCount` u32 |
| 20 | `verseDocuments` u32 |
| 24 | `fingerprint` u64 |
| 32 | `check` u64: FNV-1a 64 over bytes 0-31 |

**Byte budget:** a valid record is exactly `RECORD_BYTES`. `save` writes exactly that many bytes
and verifies `fileSize()` after the flush. A load is one `openFileForRead` plus one `read` of
`min(fileSize(), RECORD_BYTES)` bytes, never `Storage.readFile`. It passes `fileSize()`, not the
count read, as `decode`'s `length`, so a 41-byte file is refused through `load` as well as
through `decode`.

**Decode order (fixed; this order is what makes A11 hold):**
1. `length < 6`, or a bad magic → `Corrupt`.
2. version > `VERSION` → `TooNew`, whatever the length and check.
3. version < `VERSION` → `Corrupt`.
4. `length != RECORD_BYTES` → `Corrupt`.
5. A check mismatch → `Corrupt`.
6. A key mismatch → `Stale`.

**Failure classes:**
- `Missing`: no file.
- `Unreadable`: the file exists but cannot be opened, or its read fails. It is transient, so it
  is not overwritten.
- A read returning fewer bytes than `min(fileSize(), RECORD_BYTES)` is `Corrupt`.

**The hash:** `Fnv1a64` is declared in this header (above), not in an anonymous namespace. The
store's fingerprint uses it through `addU32`/`addU64` exactly as today (`BibleSearchStore.cpp:174-189`).
A byte-order slip there would turn every user's index Stale, so the tests pin it (§ "Testing
strategy").

### New: `src/study/BibleSearchResidency.h` (header-only, pure)

These are the two pure decisions the store makes, kept apart so the host suite can test them
without `Epub`. Modelled on `BibleNavCacheOf` being kept Epub-free for `test/number_grid`
(`BibleNavCache.h:19-20`).

```cpp
namespace BibleSearchResidency {
inline constexpr size_t MIN_FREE_PSRAM = 3u * 1024u * 1024u;
inline constexpr size_t MIN_LARGEST_PSRAM_BLOCK = 1024u * 1024u;
constexpr bool keepAfterExit(const size_t freePsram, const size_t largestPsramBlock) {
  return freePsram >= MIN_FREE_PSRAM && largestPsramBlock >= MIN_LARGEST_PSRAM_BLOCK;
}
// The held reader may be reused when the re-read header bytes and the file size are the held ones.
inline bool heldIndexMatches(const BibleSearch::IndexHeader& held, const uint8_t* raw,
                             const size_t rawLength, const uint32_t fileSize) {
  if (rawLength < BibleSearch::INDEX_HEADER_BYTES || fileSize != held.fileSize) return false;
  uint8_t expected[BibleSearch::INDEX_HEADER_BYTES];
  BibleSearch::writeHeader(expected, held);
  return memcmp(expected, raw, sizeof(expected)) == 0;
}
}
```

### Changed: `src/study/BibleSearchStore.{h,cpp}`

**New members:**
- `identity_` (path, size, spine count);
- `fingerprint_` and `fingerprintKnown_`;
- **[D1]** `recordLoad_` and `recordLoaded_`: the last record `load` result for this identity,
  reset by `identify()` (A10);
- `names_`, an `AllocatedBuffer` sized `sizeof(BibleBookNameTable)`, plus `namesKept_`.

The existing document cache (`documentsPath_`, `documentsEpubSize_`, `documentsReady_`,
`documents_`) is re-keyed to `identity_`.

| Method | Change |
|---|---|
| `identify(epub)` (private, new) | Sizes the EPUB and reads `getSpineItemsCount()`. On a different identity it calls `releaseResident()` and clears the fingerprint, the document cache and **[D1]** `recordLoaded_`. False when the EPUB cannot be sized. |
| `fingerprintFor(epub, out)` (private, new) | RAM → **[D1]** record (`load`, remembered in `recordLoad_`) → `resolveDocuments`. Logs `Fingerprint <source> in N ms`. |
| `resolveDocuments(epub, size)` | Also reports completeness through `coversEveryBook` (A10). |
| `persistFingerprint()` (private, new) **[D1]** | The only writer. Called after a **complete** resolve, from both `fingerprintFor` and `documents()`. Loads once if `!recordLoaded_`, then calls `save` only if `mayOverwrite(recordLoad_)`. |
| `documents(epub)` | Unchanged contract. A successful resolve also sets `fingerprint_`, and **[D1]** a complete one calls `persistFingerprint()`. |
| `status(epub)` | Uses `fingerprintFor` instead of `documents()`. |
| `open(epub, statusOut)` | No longer starts with `close()`. Identify → fingerprint → if a reader is held for this fingerprint, **resume** (A2); otherwise close and open fresh as today. Then `cacheTermIndex()`, which is a no-op when the cache is held and a retry when it is not (`IndexReader.cpp:50-52`). Logs `Index resumed in N ms` or `Index opened in N ms (term index B bytes)`. |
| `close()` | Unchanged (A8). |
| `park()` (new) | Closes `indexFile_` if open, then applies A6. Below the floor → `releaseResident()` plus an `INF` log of free/largest. Otherwise a `DBG` log of what is held. |
| `releaseResident()` (private, new) | `close()` plus frees `names_`. |
| `restoreBookNames(epub, out)` / `keepBookNames(epub, in)` (new) | No re-identify: compare `epub->getPath()` and spine count against the identity the preceding `open()`/`status()` set (A3, A13), then copy (A4). Restore is false when nothing is held for that identity or none is set. |

### Changed: `src/activities/reader/BibleSearchActivity.cpp`

Three edits, none of them in `enterState` or `onEnter` (R20's ground):

- **`onExit`** (`:117-125`): `BibleSearchStore::getInstance().close()` becomes `.park()`. The
  indexer cancel stays first, so a running build (which closed the store at `begin`) parks names
  only.
- **`runBusyWork`, Opening** (`:189-194`): `openIndex()` first, then names via
  `restoreBookNames` or else `load` + `keepBookNames` (A13). Then the existing keyboard/prompt
  step, now deferred to after names. Logs
  `Opened in N ms (index <resumed|opened|unusable>, names <resident|loaded|unavailable>)`.
- **`openIndex`** (`:220-232`): split, so Opening can run names between the index result and
  `openKeyboard()`/`showPrompt()`. The Ready path (`:195-197`) keeps calling it unchanged.
  Ready's names are already loaded from Opening.

### Changed: `test/bible_search_index/`

New test files and the two new sources are added to this directory's own `CMakeLists.txt`. That
file is registered to this task, so no shared append point is touched.

### Docs

**[D1] Required by the decision:** a `docs/file-formats.md` section for
`/.crosspoint/epub_<hash>/search-fingerprint.bin`, placed after `bible.idx` (`:554-596`). The
section covers:
- the layout table;
- the key;
- the version and refusal rules, and the decode order (A11);
- that only a complete resolve is persisted (A10);
- the temp+rename write;
- that the record lives and dies with the Bible's cache directory, except for an in-place
  `book.bin` rebuild, which is why `VERSION` must be bumped with any `BOOK_CACHE_VERSION` change
  that alters spine hrefs (A9).

`docs/file-formats.md` is also on t3's file list, and this task does not hold it yet. At
`implement` it is added to this task's files once t3's lock allows. If the lock is still held
when the PR opens, the exact section text goes in the PR body under "For the orchestrator". It is
never dropped.

## Data and control flow

### First search after a boot, record present [D1]

1. Frame lands.
2. `open`:
   - identify (one EPUB open);
   - `fingerprintFor` → no RAM fingerprint → `load` → `Ok` → `Fingerprint from record in N ms`;
   - fresh index open and term cache, ~220 ms.
3. `restoreBookNames` → false → `load` (~250 ms) → `keepBookNames`.
4. Keyboard.

### First search ever, or after the record was lost

Same as above, but step 2's fingerprint is computed by `resolveDocuments` (~1.95 s, the existing
`INF` line). **[D1]** If the resolve covered every book, `persistFingerprint()` writes the record,
unless the record found on the card is `TooNew` or `Unreadable`. A partial resolve is used from RAM
only (A10).

### Later search, same boot

1. Frame lands.
2. `open`:
   - identify (unchanged identity);
   - `Fingerprint from memory`;
   - held reader → reopen `indexFile_`, read 48 B, `heldIndexMatches` → `Index resumed in N ms`.
3. `restoreBookNames` → true → `memcpy`.
4. Keyboard.

### Leaving the screen

The indexer is cancelled if one is running. Then `park()` closes the file and either keeps the
reader and names, or releases them below the floor.

### Bible replaced between searches (size or spine count differs)

`identify` sees a new identity → `releaseResident()` → everything is recomputed for the new
Bible, as on a first search.

### Index rebuilt (Prompt → Build)

Unchanged:
- `begin()` → `close()` frees the reader;
- the build runs;
- Ready → `open` → no held reader → fresh open.

The names stay resident throughout.

### `bible.idx` deleted from the file browser while parked

Resume's open fails → close → fresh open → `Missing` → `status` → prompt.

## Error handling

| Condition | Behaviour |
|---|---|
| EPUB cannot be sized | `identify` false. `open`/`status` report `Unreadable`, as `documents()` does today (`BibleSearchStore.cpp:110-112`, `:205`, `:233-234`). Names load without being kept. |
| Resume: open fails, short header read, header or size differs | Log, `close()`, open fresh. |
| Resume: term cache was never allocated | `cacheTermIndex()` retries; on failure it logs and serves lookups from the card (`BibleSearchStore.cpp:253-256`). |
| PSRAM below the floor at exit | Release everything, `LOG_INF` with the two figures. The next entry pays today's costs. |
| `names_` cannot be allocated | Log `OOM`, keep nothing. The activity's own table is unaffected. |
| [D1] Record `Missing`/`Corrupt`/`Stale` | Recompute, then overwrite. |
| [D1] Record `TooNew` | Recompute in RAM, do not overwrite (A11). |
| [D1] Record `Unreadable` (exists, cannot be opened or read) | Recompute, do not overwrite. |
| [D1] Partial resolve (a book 1-66 resolved to no documents) | Serve this boot from RAM, as today. The record is **not** written (A10). The existing per-book `LOG_ERR` stays, plus one `LOG_ERR` saying the fingerprint was not persisted. |
| [D1] Record write fails (temp, short write, remove, rename) | `LOG_ERR`, carry on. The fingerprint is in RAM. A leftover `.tmp` is overwritten by the next save. |

## Memory

Held after the screen is left, until a release event. The PR body states this table with the
device's logged figures.

| Block | Bytes | Region | New? |
|---|---|---|---|
| Term index | ~440-444 KB (`IndexReader.h:52`; search design `:183`) | PSRAM, explicit (`psramAllocate`, `BibleSearchStore.cpp:25`) | Held longer; same allocation |
| Posting page | 4,096 | PSRAM, explicit | Held longer |
| `IndexReader` object | ~184 by member sum (research §4) | Internal (`makeUniqueNoThrow`, below the 4,096 B threshold) | Held longer. Kept as is: moving it would need placement new for an object smaller than the allocator's overhead |
| Book-name copy | 4,228 | PSRAM, explicit (`AllocatedBuffer` + `psramAllocator()`) | **New** |
| Document list | 3,567 | PSRAM | Today held all boot. **[D1]** With a record hit it is never resolved at all, so 3,567 B is saved on those boots |
| Identity path + fingerprint | the path string and 8 B | Internal | Replaces `documentsPath_` |

Internal SRAM added: none beyond the `IndexReader` object living longer. No allocation on the
render path.

## Testing strategy

TDD. Each test is written and run red before the code exists. All tests are host tests in
`test/bible_search_index/`.

**[D1] `FingerprintFileTest.cpp`** — codec, modelled on `test/unit_index_format/UnitIndexFormatTest.cpp`:
- round trip;
- a golden 40-byte vector pins the layout;
- the decode order:
  - `VERSION + 1` at lengths 6, 40 and 48, each with a wrong check → `TooNew`;
  - a 40-byte record with a wrong magic and version bytes `0xFFFF` → `Corrupt`;
  - lengths 0 and 5 → `Corrupt`;
  - `VERSION` at lengths 39 and 41 → `Corrupt`;
  - a flipped check byte → `Corrupt`;
  - version `VERSION - 1` (i.e. 0) with a valid check → `Corrupt`;
- `epubSize`, `spineCount` and `unitIndexVersion` mismatch → `Stale`;
- `mayOverwrite` is false only for `TooNew` and `Unreadable`;
- `Fnv1a64` pinned to the published FNV-1a-64 vectors: `""` → `cbf29ce484222325`, `"a"` →
  `af63dc4c8601ec8c`;
- `addU32`/`addU64` equal `add()` over the value's little-endian bytes;
- `coversEveryBook`:
  - true for a list with every book 1-66;
  - false with any one book missing;
  - false for an empty list;
  - unaffected by duplicates and order.

**[D1] `FingerprintFileIoTest.cpp`** — load and save against `test/stubs/HalStorageFake` (seeded
with `putFile`, inspected with `fileBytes`), modelled on `test/storage_io/AtomicWriteTest.cpp`:
- save leaves exactly 40 B and no `.tmp`;
- `failWritesTo(tmp)` leaves the previous record byte-identical and returns false;
- `failRenamesFrom(tmp)` returns false;
- load of an absent file → `Missing`;
- `failReadsOf(path)` → `Unreadable`;
- a file truncated to 20 B → `Corrupt`;
- a 41-byte file holding a valid `VERSION` record plus one byte → `Corrupt` through `load`;
- a 48-byte `VERSION + 1` file → `TooNew` through `load`;
- `readCallsOn(path)` is 1, so the load is one read, not a byte-at-a-time loop.

**`ResidencyTest.cpp`** — `keepAfterExit` at, below and above each floor.
`heldIndexMatches`:
- true for an unchanged header;
- false for each of the twelve serialised fields changed (the magic and the eleven `IndexHeader` fields);
- false for a short buffer;
- false when the file size differs from `fileSize`;
- built from a real index made by `IndexBuilder`, as the existing tests build theirs
  (`IndexTest.cpp:101` onward).

**Existing suite:** must stay green. The term-cache behaviour a resumed reader relies on is
already covered (`IndexTest.cpp:639`, `:695`, `:946`).

**Not host-testable, verified on the device and by the build:** the store and activity wiring.
`BibleSearchStore.cpp` needs `Epub` and `STUDY` (research §7).

**Build and format:** `~/.platformio/penv/bin/pio run -e x4pro` (`pio` is not on PATH, research
§8), then `./bin/clang-format-fix` over the whole tree.

**Device (the owner's run).** Flash the `x4pro` dev build, then:

1. **Cold boot, first search ever on this build.** Expect the existing
   `[BSEARCH] 1189 verse documents, fingerprint b063e8dc59554f2d` line, `Fingerprint computed`,
   and **[D1]** no record error.
2. **Back, then search again.** Expect `Index resumed in N ms`, `names resident`, and frame →
   `Entering activity: KeyboardEntry` under 100 ms (before: 500 ms).
3. **Home → Bible → search.** Same as step 2; this is the baseline's own scenario.
4. **Sleep, wake, Bible → search.** **[D1]** Expect `Fingerprint from record in N ms` and frame →
   keyboard ≈ 0.5 s (before: 2,436 ms).
5. **Leave search, then confirm the park.** Expect the `Parked:` line, with PSRAM free about
   450 KB lower than before the first search.

All figures are at the SD clock of the build flashed. Say which, since R2 (t5) changes it.

## Order of work (for the plan)

1. `BibleSearchResidency.h` and its tests.
2. **[D1]** `BibleSearchFingerprintFile` and its tests.
3. Store: identity, fingerprint source, resume, park/release, names.
4. Activity: `onExit` → `park`, Opening reorder and names, timing logs.
5. **[D1]** `docs/file-formats.md` section.
6. Build, format, PR.

## Overlap

- **t1 (R20)** shares `BibleSearchActivity.cpp`. This task stays out of `onEnter` and `enterState`
  (`:101-115`, `:129-139`). Its edits are `onExit`, the Opening case of `runBusyWork` and `openIndex`
  (`:117-125`, `:189-194`, `:220-232`). Whichever lands second rebases.
- **t3 (R12)** holds `docs/file-formats.md` (the [D1] section) and `StudyStore` (not edited
  here). If R12 adds a PSRAM session holder, A6's floor applies to whatever is held when search is
  left.
- **For the orchestrator:** no `test/CMakeLists.txt` or `src/main.cpp` lines are needed (A7, and
  new sources go in `test/bible_search_index/CMakeLists.txt`).

## Changes after decision D1

- D1 is now recorded as decided (option a), with the answer quoted in § "Decisions". The "if (c)"
  fallback is removed.
- The `docs/file-formats.md` section is now a requirement of the decision, not optional (§ "Docs").
  Its contents are listed, and so is how it lands while t3 holds that file.
- No design change. The answer matches A9-A12 as written: the location, the key, the FNV-64 check,
  the temp+rename write, the single 40-byte read, and fingerprint-only use.

## Changes after review 1

Review: `docs/superpowers/reviews/berean-o-6-spec-review-1.md` (verdict CLEAR; 0 BLOCKER, 2 MAJOR,
6 MINOR). There is no review 0 on disk: the pass-0 reviewer stopped on an API rate limit before
writing anything. Every finding was checked against the code and accepted. None of them reopens D1.

- **M1, partial resolve persisted.** A10 now persists only a resolve in which every book 1-66
  contributed. The new pure predicate `coversEveryBook` is host-tested. A12 explains why a build
  in a partial boot still heals on the next boot, and the error table has a "partial resolve" row.
- **M2, decode order.** The order is fixed in the `BibleSearchFingerprintFile` section: magic,
  then version (`TooNew` whatever the length), then length, check and key. `load` passes
  `fileSize()` and reads `min(size, 40)` bytes. The codec and I/O tests now cover each step of the
  order.
- **m1, writer and gate.** One private `persistFingerprint()` writes. It is gated by
  `mayOverwrite(recordLoad_)`, which is kept per identity and loads once on the indexer path (A10,
  method table).
- **m2, floor sizing.** `MIN_FREE_PSRAM` is raised from 2 MiB to 3 MiB. It is sized to the catalog
  update check's budgeted 2.5 MiB (`CatalogIndexStore.cpp:234-237`, `:277`), not to the 1.5 MiB
  inflate (A6).
- **m3, lifetime.** A9 no longer says the record lives exactly as long as `book.bin`. It names the
  in-place rebuild case, the reason the key cannot carry `BOOK_CACHE_VERSION`, and the rule to bump
  `VERSION` with an href-changing `BOOK_CACHE_VERSION` change. The record's `VERSION` comment and
  the docs section say the same. A9's deletion list gains WebDAV and the downloader.
- **m4, EPUB opens per re-entry.** `restoreBookNames`/`keepBookNames` no longer re-identify; they
  compare against the identity `open()`/`status()` set (A3, method table). A re-entry opens the
  EPUB once, as Goal 1 says.
- **m5, Goal 3.** Goal 3 now says when each event releases memory. Bible change and PSRAM pressure
  are both lazy, and the PR body must not claim eager release.
- **m6, the hash and the sketch.**
  - `Fnv1a64` is declared in `BibleSearchFingerprintFile.h`, and tests pin it to the published
    vectors and its little-endian helpers.
  - `keepAfterExit` and `heldIndexMatches` have their bodies in the header-only sketch, and
    `heldIndexMatches` is `inline`.
