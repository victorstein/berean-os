# berean-o-6 (R11) design: keep the Bible search index open, and make the first search after boot cheap

Bead `berean-o-6`, task `t6`, branch `perf/r11-search-index-resident`. Research:
`docs/superpowers/research/2026-10-11-berean-o-6-research.md` (cited below as "research §n").
Baseline: `docs/superpowers/notes/responsiveness-baseline.md` (on `main` since `3e56b629`).
Every `file:line` was re-read on this branch at `7c06c2f9`. Nothing under `src/study`,
`src/activities/reader/BibleSearch*` or `lib/BibleSearch` has changed on `origin/main` since.

**Pending decision D1** (§ "Decisions"): whether to add the persisted fingerprint record. Every
part of this spec marked **[D1]** depends on its answer. Everything else stands either way.

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
3. **Memory held between entries is bounded, logged, and released** on:
   - a Bible change,
   - an index rebuild,
   - a query I/O failure,
   - PSRAM below a floor when the screen is left,
   - sleep.
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
  re-identifies on every `open`/`status`/`documents`/`restoreBookNames` call. Each call opens the
  EPUB once for its size, which is today's cost (`:83-88`). A different identity releases
  everything resident before anything else runs.

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
  the resident blocks still held. It keeps them only if free ≥ **2 MiB** and the largest block ≥
  **1 MiB**.
  - **Pattern:** the same retain-only-above-a-floor bet `SdCardFont::resetStyleMiniData` makes
    (`SdCardFont.cpp:73`, `:118-125`).
  - **Sizing:** the largest PSRAM working set reachable while parked is Buscar's catalog inflate.
    It holds a ≤512 KB compressed buffer and allocates a ≤1 MiB output while the source is live
    (`CatalogIndexStore.h:77-78`, `CatalogIndexStore.cpp:108-133`), 1.5 MiB together. 2 MiB covers
    it with margin, and the 1 MiB largest-block floor covers its output block. The index build
    (2.88 MB) is not a parked-time consumer, because `BibleSearchIndexer::begin` closes the store
    first (`BibleSearchIndexer.cpp:57-58`).
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

    Moving a finished book renames the directory along with it (`EpubReaderActivity.cpp:121-133`).
    The record therefore lives exactly as long as the `book.bin` its hrefs came from.
  - **Exposure compared with today:**
    - A Bible replaced off-device by a same-size file is equally undetected. `book.bin` is keyed
      only by path hash and `BOOK_CACHE_VERSION` (`Epub.h:48`, `BookMetadataCache.cpp:460-472`), so
      today's recomputation reads the same stale hrefs.
    - A same-size replacement on the device clears the directory, so the record is recomputed.
    - The units index already accepts this exposure in writing: "A same-size content change is
      deliberately not detected here" (`UnitIndexFormat.h:22-29`).
  - **Placement precedent:** `src/` code already writes its own small binary file into
    `<cachePath>/` (`ProgressFile.h:32-62`).

- **A10 [D1] — The record is written once, synchronously, right after `resolveDocuments` succeeds.**
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
  - **The cost:** after a downgrade, a newer record costs ~2 s per boot until the firmware is
    upgraded again.

- **A12 [D1] — The record serves only the fingerprint, never the document list.** `open()` and
  `status()` need only the fingerprint (`BibleSearchStore.cpp:212, 222, 243`). The indexer keeps
  calling `documents()`, which always resolves for real (`BibleSearchIndexer.cpp:59-64`). A build
  takes minutes, so its extra ~2 s is noise. A wrong record can therefore never make a build index
  the wrong documents.

- **A13 — Opening runs the index work first, then the names.** Today names load before the index
  (`BibleSearchActivity.cpp:190-193`). The order changes so that `open()` establishes the identity
  that `restoreBookNames` then reuses. Nothing between the two reads names. The prompt and the
  keyboard are entered only after both have run, as today.

## Decisions

- **D1 — PENDING (surfaced with `hpipe decide` as the last action of this phase).** Should a new
  on-disk record (A9-A12) be added? The data-dev rules make a new on-disk format the user's call
  (`.claude/agents/data-dev.md`, "Prime directive" 2).
  - **Recommendation: yes,** as specified in A9-A12.
  - **Alternatives:**
    - (b) the same record under `/.berean/search/`. It would outlive a same-size replacement on the
      device, which regresses today's recompute-after-reboot.
    - (c) no record. Goal 2 is dropped, and the first search after boot stays ~2.4 s.
    - (d) a faster recompute. That needs t3's files and raises the tier to heavy.
  - **If (c):** delete every [D1] item, the record files and the record tests. The rest stands
    unchanged.

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
// Bump when what the fingerprint covers, or how verse documents are resolved, changes.
inline constexpr uint16_t VERSION = 1;
inline constexpr size_t RECORD_BYTES = 40;
inline constexpr const char* FILE_NAME = "search-fingerprint.bin";

struct Key { uint64_t epubSize; uint32_t spineCount; uint16_t unitIndexVersion; };
struct Record { Key key; uint32_t verseDocuments; uint64_t fingerprint; };
enum class Load { Ok, Missing, Unreadable, Corrupt, TooNew, Stale };

void encode(uint8_t (&out)[RECORD_BYTES], const Record& r);
Load decode(const uint8_t* in, size_t length, const Key& expected, Record& out);
Load load(const std::string& cachePath, const Key& expected, Record& out);
bool save(const std::string& cachePath, const Record& r);  // temp + rename; false on any failure
// Whether a load result may be replaced by a fresh record (everything but TooNew and Unreadable).
bool mayOverwrite(Load result);
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

**Byte budget:** the record is exactly `RECORD_BYTES`. `decode` refuses any other length. `save`
writes exactly that many bytes and verifies `fileSize()` after the flush. A load is one
`openFileForRead` plus one 40-byte `read`, never `Storage.readFile`.

**Checks on load:**
- A version above `VERSION` → `TooNew`.
- A bad magic or check, or a lower version → `Corrupt`.
- A key mismatch → `Stale`.

**Failure classes:** `Unreadable` means the file exists but cannot be opened or read; it is
transient, so it is not overwritten. A short read is `Corrupt`. `Fnv1a64` moves from the store's
anonymous namespace into this file's, so the store and the record share one implementation.

### New: `src/study/BibleSearchResidency.h` (header-only, pure)

These are the two pure decisions the store makes, kept apart so the host suite can test them
without `Epub`. Modelled on `BibleNavCacheOf` being kept Epub-free for `test/number_grid`
(`BibleNavCache.h:19-20`).

```cpp
namespace BibleSearchResidency {
inline constexpr size_t MIN_FREE_PSRAM = 2u * 1024u * 1024u;
inline constexpr size_t MIN_LARGEST_PSRAM_BLOCK = 1024u * 1024u;
constexpr bool keepAfterExit(size_t freePsram, size_t largestPsramBlock);
// The held reader may be reused when the re-read header bytes and the file size are the held ones.
bool heldIndexMatches(const BibleSearch::IndexHeader& held, const uint8_t* raw, size_t rawLength,
                      uint32_t fileSize);
}
```

### Changed: `src/study/BibleSearchStore.{h,cpp}`

**New members:**
- `identity_` (path, size, spine count);
- `fingerprint_` and `fingerprintKnown_`;
- `names_`, an `AllocatedBuffer` sized `sizeof(BibleBookNameTable)`, plus `namesKept_`.

The existing document cache (`documentsPath_`, `documentsEpubSize_`, `documentsReady_`,
`documents_`) is re-keyed to `identity_`.

| Method | Change |
|---|---|
| `identify(epub)` (private, new) | Sizes the EPUB and reads `getSpineItemsCount()`. On a different identity it calls `releaseResident()` and clears the fingerprint and document cache. False when the EPUB cannot be sized. |
| `fingerprintFor(epub, out)` (private, new) | RAM → **[D1]** record → `resolveDocuments`. Logs `Fingerprint <source> in N ms`. |
| `documents(epub)` | Unchanged contract. A successful resolve also sets `fingerprint_` and **[D1]** writes the record (A10). |
| `status(epub)` | Uses `fingerprintFor` instead of `documents()`. |
| `open(epub, statusOut)` | No longer starts with `close()`. Identify → fingerprint → if a reader is held for this fingerprint, **resume** (A2); otherwise close and open fresh as today. Then `cacheTermIndex()`, which is a no-op when the cache is held and a retry when it is not (`IndexReader.cpp:50-52`). Logs `Index resumed in N ms` or `Index opened in N ms (term index B bytes)`. |
| `close()` | Unchanged (A8). |
| `park()` (new) | Closes `indexFile_` if open, then applies A6. Below the floor → `releaseResident()` plus an `INF` log of free/largest. Otherwise a `DBG` log of what is held. |
| `releaseResident()` (private, new) | `close()` plus frees `names_`. |
| `restoreBookNames(epub, out)` / `keepBookNames(epub, in)` (new) | Identify, then copy (A4). Restore is false when nothing is held for this identity. |

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

**[D1]** A `docs/file-formats.md` section for `search-fingerprint.bin`, next to `bible.idx`
(`:554-596`). That file is on t3's list, so the section text goes to the orchestrator if the lock
is still held at implement.

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
`INF` line) and **[D1]** the record is written.

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
- each of bad magic, flipped check byte, 39 and 41 bytes → `Corrupt`;
- version + 1 → `TooNew`;
- `epubSize`, `spineCount` and `unitIndexVersion` mismatch → `Stale`;
- `mayOverwrite` is false only for `TooNew` and `Unreadable`.

**[D1] `FingerprintFileIoTest.cpp`** — load and save against `test/stubs/HalStorageFake` (seeded
with `putFile`, inspected with `fileBytes`), modelled on `test/storage_io/AtomicWriteTest.cpp`:
- save leaves exactly 40 B and no `.tmp`;
- `failWritesTo(tmp)` leaves the previous record byte-identical and returns false;
- `failRenamesFrom(tmp)` returns false;
- load of an absent file → `Missing`;
- `failReadsOf(path)` → `Unreadable`;
- a file truncated to 20 B → `Corrupt`;
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
