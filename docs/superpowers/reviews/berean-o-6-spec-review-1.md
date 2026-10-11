Tier: standard

# berean-o-6 (R11) spec review 1

Reviewed: `docs/superpowers/specs/2026-10-11-berean-o-6-design.md` at `30c51438`. Checked against the
`hpipe brief --task t6` brief, the research note, and the code on this branch. No code has changed under
`src/study`, `src/activities/reader`, `lib/BibleSearch`, `test/bible_search_index` or `test/stubs` between
the branch base `1ba1cd34` and `origin/main` `478cf1ed`; `git diff --stat 1ba1cd34 478cf1ed` lists only
docs, `platformio.ini` and `version.txt`. There is no `spec-review-0` file on disk for this bead, so this
review has no earlier fixes to re-check.

The design is sound. I checked the parts the review was asked to attack and they hold:

- **A1.** The baseline log shows `Exiting activity: EpubReader` then `Entering activity: Launcher`
  between searches (`baseline.log` lines 1235-1237 and 1272-1274).
- **A2.**
  - `source_.ctx = &indexFile_` (`BibleSearchStore.cpp:99-105`).
  - `writeHeader` covers all 48 bytes (`IndexFormat.cpp:32-46`).
  - `IndexReader::open` already ties `fileSize` to `source.size` (`IndexReader.cpp:36`).
  - `cacheTermIndex` is idempotent (`:50-52`).
- **A4.** `BibleBookNameTable` is char arrays plus an `int`, 4,228 B (`BibleBookNameTable.h:57-59`). It
  does not depend on renderer state: `renderer` is only lent to `SpineHtmlStream` for the framebuffer
  (`SpineHtmlStream.cpp:23,41`).
- **A7.** Deep sleep is a chip reset (`main.cpp:243-281`).
- **A9.** Every on-device replace or delete clears the cache directory:
  - `clearBookCache` → `Epub::clearCache` → `removeDir` (`BookCacheUtils.cpp:23-26`, `Epub.cpp:520-533`);
  - it is reached from FileRoutes, WebDAV (`WebDAVHandler.cpp:376,425,534`), the downloader
    (`PublicationDownloader.cpp:110,252`), the file browser and `CardBooks::remove` (`:57`);
  - so the list in A9 is incomplete, but only on the safe side.
- **No overlap with R20.** R20's plan edits only the `onEnter` refresh flag and comments
  (`perf-r20-fast-entry-refresh/docs/superpowers/plans/2026-10-11-berean-o-1-plan.md:140-200`).

The two MAJOR findings are both about the persisted record (the D1 part). Neither reopens D1. Both are
fixed by tightening the spec.

## MAJOR

### M1. A partial `resolveDocuments` is persisted, which turns a one-boot glitch into a permanent one

- **Claim.**
  - A10 (spec:188): "The record is written once, synchronously, right after `resolveDocuments`
    succeeds."
  - A12 (spec:211): "A wrong record can therefore never make a build index the wrong documents."
- **Problem.** `resolveDocuments` succeeds even when books are missing.
  - A book whose `spineIndicesForBook` comes back empty is logged and skipped
    (`BibleSearchStore.cpp:146-157`, the log at `:155`). Only `count == 0` fails (`:158-161`).
  - `UnitIndexCache::spineIndicesForBook` returns an empty vector on transient failures, and these
    look the same as "no documents":
    - OOM of the 3,937-B map (`UnitIndexCache.cpp:383-386`; below the 4,096 B PSRAM threshold, so
      internal SRAM);
    - a failed open (`:389`);
    - a failed seek or short read (`:390-391`).

  **Today** the partial fingerprint lives only in RAM for one boot (`BibleSearchStore.cpp:114`). The
  next boot recomputes the full one and heals.

  **With the record**, keyed only by size, spine count and units version, the partial fingerprint
  persists:
  - **(a)** After a record loss (Clear Cache, first ever), a glitched resolve writes `F_partial`. The
    valid full index on the card is then Stale on every boot, and the user is pushed into a
    multi-minute rebuild they do not need.
  - **(b)** A build started in the glitched boot gets the same partial list from the RAM cache
    (`BibleSearchIndexer.cpp:59` → `documents()` cache hit). It writes a partial index whose
    fingerprint equals the record, so search is "Ok" forever and silently missing a book. Today the
    next boot's recompute marks that index Stale and offers the rebuild.

  A12 guards the build against a wrong *record*. It does not guard the record against a wrong
  *resolve*.
- **Fix.**
  1. Have `resolveDocuments` report whether every book 1-66 contributed at least one document.
  2. Write the record only when that is true. A partial resolve still serves this boot from RAM,
     exactly as today.
  3. State the rule in A10 and add an error-table row: "partial resolve → RAM only, record not
     written".
  4. Put the predicate in the pure, host-tested header (`BibleSearchResidency.h` or
     `BibleSearchFingerprintFile`), because `BibleSearchStore.cpp` is not host-compiled.

  The cost is that an edition missing a book recomputes on every boot. The shipped NWT has 66 books,
  and the store already logs a missing book as an error (`:155`).

### M2. The decode order is unspecified, and the stated pieces contradict A11's "newer is left on the card"

- **Claim.**
  - A11 (spec:200-206) and D1's "version-refusing" record.
  - "Checks on load" (spec:289-293) lists version → TooNew first, then magic, check and lower version
    → Corrupt.
  - "`decode` refuses any other length" (spec:285).
  - The tests expect "39 and 41 bytes → `Corrupt`" (spec:462) and "version + 1 → `TooNew`"
    (spec:463).
  - Load is "one 40-byte `read`" (spec:287).
- **Problem.**
  1. **Length before version.** If the length is checked before the version, a future format of any
     other length decodes as Corrupt. `mayOverwrite(Corrupt)` is true, so this build overwrites it.
     That breaks A11 in exactly the case versioning exists for.
  2. **Version before magic.** If the version is checked before the magic, as the list reads, a
     non-BSFP file whose bytes 4-5 exceed 1 is TooNew. It is never overwritten and costs ~2 s on
     every boot until Clear Cache.
  3. **Which length `load` passes is unstated.** With one 40-byte read, passing the count read lets a
     41-byte v1 file decode Ok, so "refuses any other length" holds only in the codec test. Passing
     `fileSize()` triggers problem 1.

  The bible.idx reader this file sits beside already uses the safe order: magic, then version, then
  size (`IndexFormat.cpp:48-50`, `IndexReader.cpp:27-40`).
- **Fix.** Specify this order:
  1. length < 6, or a bad magic → `Corrupt`;
  2. version > `VERSION` → `TooNew`, whatever the length and check;
  3. version < `VERSION` → `Corrupt`;
  4. length ≠ 40 → `Corrupt`;
  5. a check mismatch → `Corrupt`;
  6. a key mismatch → `Stale`.

  `load` passes `fileSize()` as the length and reads `min(size, 40)` bytes in its one read.

  Replace the tests with these:
  - `VERSION+1` at 6, 40 and 48 bytes with a wrong check → `TooNew`;
  - a 40-byte file with a wrong magic and version bytes `0xFFFF` → `Corrupt`;
  - a 41-byte `VERSION` file → `Corrupt` through `load` as well as through `decode`.

## MINOR

### m1. Where the record is written, and how `mayOverwrite` gates it, is not pinned down

- **Claim.**
  - The method table (spec:329) says `documents()` writes the record after every successful resolve.
  - `fingerprintFor` recomputes after a non-Ok load (spec:328).
  - The error table says `TooNew` and `Unreadable` are not overwritten (spec:433-434).
- **Problem.** The spec never says where the last load result is kept, or which call site writes. If
  `fingerprintFor` recomputes through `documents()`, its unconditional write overwrites a `TooNew`
  record, which breaks A11.
- **Fix.**
  - Have one private `persistFingerprint()` called after any successful resolve.
  - Gate it on `mayOverwrite(recordLoad_)`, where `recordLoad_` is kept per identity and reset by
    `identify()`.
  - If no load has happened yet for this identity (the indexer path), load once before deciding.

### m2. The A6 floor is sized against the wrong worst case

- **Claim (spec:140-145).** "The largest PSRAM working set reachable while parked is Buscar's catalog
  inflate … 1.5 MiB together. 2 MiB covers it with margin."
- **Problem.** `checkRemote` validates the staged download with `loadFrom` while the current index is
  still held (`CatalogIndexStore.cpp:234-237`). The release only comes later, in `applyStaged`
  (`:277`). By budget that is 2.5 MiB:
  - held ≤1 MiB;
  - compressed ≤512 KB;
  - inflated ≤1 MiB (`CatalogIndexStore.h:77-78`).

  That is above the 2 MiB floor. Other explicit PSRAM users are not listed either
  (`PsramJsonAllocator.cpp:12,19`).
- **Fix.** Correct the sizing text. Raise `MIN_FREE_PSRAM` to 3 MiB; free PSRAM while reading is
  ~8.2 MB (baseline `MEM Study open` lines), so the floor still never trips in normal use. Otherwise,
  state plainly that it is sized to measured sizes, not budgets.

### m3. A9's lifetime claim is false for a `book.bin` rebuilt in place

- **Claim (spec:177).** "The record therefore lives exactly as long as the `book.bin` its hrefs came
  from."
- **Problem.** A `BOOK_CACHE_VERSION` mismatch makes `BookMetadataCache::load` return false
  (`BookMetadataCache.cpp:466-472`). `Epub::load` then rebuilds `book.bin` in the same directory
  (`Epub.cpp:374, 413-419`) and never removes it. A `book.bin` deleted on its own does the same. Either
  way the record survives.
  - The research recommended keying on `BOOK_CACHE_VERSION` (research §5, §11 Q1).
  - D1's key omits it.
  - The constant is file-local (`BookMetadataCache.cpp:13`, in an anonymous namespace), so keying on
    it would need t3's file.

  The consequence is benign. Only a format change that alters hrefs makes the record disagree with a
  recompute. The index stays usable, and the next build's `documents()` rewrites the record. This is
  not a reason to reopen D1.
- **Fix.**
  - Correct the sentence.
  - Add to the `VERSION` comment: "and with any `BOOK_CACHE_VERSION` change that alters spine hrefs".
  - Say the same in the `docs/file-formats.md` section.

### m4. Goal 1, A3 and A13 disagree on how many times the EPUB is opened on a re-entry

- **Claim.**
  - Goal 1 (spec:45): a later entry does "one EPUB size read".
  - A3 (spec:111): `restoreBookNames` re-identifies, and each such call opens the EPUB.
  - A13 (spec:216): `restoreBookNames` "reuses" the identity `open()` established.
- **Problem.** Read literally, A3 gives two EPUB opens per re-entry and Goal 1 gives one. The plan
  cannot satisfy both.
- **Fix.** Make `restoreBookNames`/`keepBookNames` compare against the identity that `open()` or
  `status()` set during the same Opening. Nothing runs between them (A13), so they need no re-size.
  Then fix A3's list.

### m5. Goal 3 claims more than the design does for acceptance criterion 3

- **Claim (spec:53-58).** Memory "is … released on a Bible change … PSRAM below a floor when the
  screen is left". The brief's acceptance criterion: "released on the listed events".
- **Problem.**
  - A Bible change is detected lazily, at the next store call. Until the next search entry or sleep,
    a deleted or replaced Bible's ~450 KB stays held.
  - PSRAM pressure is checked only at exit (non-goals, spec:73-75).
  - Both choices are defensible. R12's spec makes the same validate-at-take argument (its A5). But
    the PR body must not claim eager release.
- **Fix.** Reword Goal 3 to say that a Bible change is released at the next search entry or at
  sleep, and pressure is checked at exit. The PR body's memory section repeats this. No code.

### m6. `Fnv1a64` cannot be shared from an anonymous namespace, and nothing pins it

- **Claim (spec:295-296).** "`Fnv1a64` moves from the store's anonymous namespace into this file's,
  so the store and the record share one implementation."
- **Problem.**
  - An anonymous namespace in `BibleSearchFingerprintFile.cpp` is invisible to
    `BibleSearchStore.cpp`.
  - Every `bible.idx` already on a card is keyed by this hash, through `addU32`/`addU64`
    (`BibleSearchStore.cpp:176-189`).
  - The store is not host-compiled, so a byte-order slip in the move would turn every user's index
    Stale with no failing test. The golden record vector only exercises `add()`.
- **Fix.**
  - Declare the hash in `BibleSearchFingerprintFile.h`.
  - Add a test that pins the FNV-1a-64 vectors: `""` → `cbf29ce484222325`, `"a"` →
    `af63dc4c8601ec8c`.
  - Add a test that `addU32`/`addU64` equal `add()` over the little-endian bytes.
  - Two smaller fixes to the sketch: `heldIndexMatches` in the header-only `BibleSearchResidency.h`
    must be `inline`, and `keepAfterExit` needs its body in the header.

Findings: 0 BLOCKER, 2 MAJOR (fixed inline; neither reverses D1, changes scope or needs the human), 6 MINOR.

VERDICT: CLEAR
