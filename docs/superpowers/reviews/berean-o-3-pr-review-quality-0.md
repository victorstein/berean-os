Tier: heavy

# PR #266 code-quality review: berean-o-3, a cheap Bible open from Home

PR: `perf: make opening the Bible from Home cheap` (`perf/r12-bible-open` → `main`, head `11d3307c`).
Scope: the 26 code and test files in the diff against `origin/main` (merge base `672b57d0`). The
docs files (research, spec, plan, reviews) were read only for context.

**Result:** the code is sound and mostly follows the patterns it sits beside. There are no BLOCKER or
MAJOR findings. Two MINOR findings can be fixed inline. One is a second deserializer for the spine
entry layout, with nothing tying it to the writer. The other is a predicate whose name has fallen
behind what it checks.

## How this was checked

- Every changed hunk was read and compared with the code next to it: the `BookMetadataCache`
  (de)serializers and build path, `Epub::load`, the PSRAM deleters in `CatalogIndexStore`,
  `CoverBandCache` and `PageFrameCache`, `BibleNavCacheOf`, `BibleSearchStore::identify`, the
  `StudyStore` open/close pair, the `RecentBooksDoc`/`RecentBooksStore` split, and the host-test
  conventions in `test/storage_io`, `test/pagination` and `test/number_grid`.
- The four affected host suites were built and run from a scratch CMake wrapper outside `build/`:
  `BookMetadataCacheTest` 12/12, `ParkedBookTest` 9/9, `StudyReuseTest` 6/6 and `RecentBooksDocTest`
  29/29. No new or changed project file produced a compiler warning. The only warning came from
  googletest's own `gtest-printers.h`.

## Findings

### MINOR 1: `readCumulativeSizes` is a second deserializer for the spine entry layout, and nothing ties it to the writer

The file already decided to have one (de)serializer per entry type, shared by every stream type:
"Entry (de)serializers, templated so they run over HalFile and the Buffered* wrappers alike"
(`lib/Epub/Epub/BookMetadataCache.cpp:28-30`). `writeSpineEntryTo` (`:31-37`) and
`readSpineEntryFrom` (`:51-58`) are that pair. The build path already streams spine entries through a
`BufferedFileReader` with `readSpineEntryFrom(spineIn)` (`:245`, `:304`, `:332`).

The new load pass does not reuse the pair. `readCumulativeSizes` (`:71-90`) decodes the same
`u32 len | href | u32 cumulativeSize | i16 tocIndex` layout by hand. The reasons for that are good and
partly written down: no heap `std::string` per entry for 3,937 entries (`:24-26`), and a checked
length on every read (`:71-72`). What is missing is the coupling. The spine layout is now decoded in
two places in this file, and in a third, hand-built copy in the test (`makeBookBin`,
`test/book_metadata_cache/BookMetadataCacheTest.cpp:66-73`, which says it follows
`docs/file-formats.md` because `buildBookBin` needs a real zip, `:2-3`).

Here is what that costs. A future change to the entry layout bumps `BOOK_CACHE_VERSION` and updates
`writeSpineEntryTo` and `readSpineEntryFrom` as the version rule requires. If it misses
`readCumulativeSizes`, the host suite still passes, because it writes its own bytes. On the device the
short-read guard does not reliably catch the drift either. The TOC block follows the spine block
directly, so a misaligned parse reads TOC bytes as spine fields and fails only if it happens to hit
EOF. The result is garbage cumulative sizes, which feed the progress bar and percent jumps, or a
cache that fails and rebuilds on every open.

**Fix inline:** the minimum is one line above `readCumulativeSizes` saying that it decodes
`writeSpineEntryTo`'s layout without allocating, and must change with it. That is a real "why"
comment, not narration. A stronger version, if wanted, checks the block's end after the pass. With
`tocCount > 0`, `spineIn.position()` must equal TOC LUT entry 0, the offset where `buildBookBin`
starts the TOC block. A layout drift would then fail the load loudly instead of loading wrong sizes.

### MINOR 2: `StudyStore::isOpenFor` checks more than its name says, unlike its sibling `isOpen()`

`isOpen()` is a plain getter (`src/study/StudyStore.h:102`, `units_ != nullptr`). `isOpenFor`
(`src/study/StudyStore.h:107`, `src/study/StudyStore.cpp:110-119`) is the reuse rule,
`study_reuse::mayReuse` (`src/study/StudyReuse.h:17`). It checks open-and-ready, the same `Epub`
object, a palette and passages load without a `Failed` result, and a pubkey that still resolves the
same. For a `local-` key that last check reads the registry on SD, behind a `const` method. So the
store can be open for exactly this `Epub` and still return false, which is the designed behaviour
when the last load failed. The name dates from before spec review 1 added the clean-load and key
conditions (`docs/superpowers/reviews/berean-o-3-spec-review-1.md:79`). It no longer matches the
rule, the helper it wraps (`mayReuse`), or the call it gates (`resumePublication`,
`src/activities/reader/EpubReaderActivity.cpp:274-278`). A later caller who takes it for an
open-state check gets false for a store that is open.

**Fix inline:** rename it to a name that matches its partner and its rule, e.g.
`canResume(epub)` / `resumePublication()`, or `mayReuseFor(epub)`. That touches
`StudyStore.h:107,187`, `StudyStore.cpp:110` and `EpubReaderActivity.cpp:274`. The log line
`Study already open for this book` can stay, since it is printed only on the reuse branch.

## The brief's checks

| Check | Result | Evidence |
|---|---|---|
| Mirrors an existing pattern rather than adding a second way | Yes, except MINOR 1 | `ParkedBookOf`/`ParkedBook` copy the `BibleNavCacheOf`/`BibleNavCache` split: a template over the heavy type so the host suite runs without `Epub`, instantiated once in firmware (`ParkedBookOf.h:13-14`, against `BibleNavCache.h:20-21`). `makePsramNavCache` follows the repo's demand-PSRAM-with-a-deleter idiom (`EpubReaderActivity.cpp:148-161`, against `CatalogIndexStore.h:83-89`). Placement-new is used only because this is a typed object, not a byte buffer. `isAlreadyFront` goes in the pure, host-tested `RecentBooksDoc`, not the store (`RecentBooksDoc.cpp:43-47`). `mayReuse` uses a `fn(void*)` + ctx callback like `repairSpanText`/`repairUnitText` in the same file (`StudyStore.cpp:24-32`), not `std::function` |
| Naming and structure consistent with siblings | Yes, except MINOR 2 | `MODULE = "PARK"` like `CoverBand.cpp:15`. `loadFreshEpub` is cut cleanly out of the old `loadBook` body (`EpubReaderActivity.cpp:190-215`). `cacheDeleted` sits next to `recentsEntryRemoved`/`pendingReadFolderMove` (`EpubReaderActivity.h:54-58`). The new constants follow `BUILD_IO_BUFFER_SIZE`'s form and explain themselves (`BookMetadataCache.cpp:21-26`) |
| No dead code, no commented-out code | Yes | The routing `static_assert` that no longer applied was removed (`BibleBookIndex.h`), not left behind. Comments that restated the code (`// Remove existing entry if present`) were dropped from `addBook`. There is no commented-out code anywhere in the diff |
| No comments restating the next line | Yes | The new comments give mechanism or reason: the PSRAM fallback (`EpubReaderActivity.cpp:148-149`), the stale handle in `reopen()` (`BookMetadataCache.h:122-124`), and normalising before comparing (`RecentBooksStore.cpp:28-29`). The repeated `// Explicit close() required` lines are this repo's required marker (CLAUDE.md, `DESTRUCTOR_CLOSES_FILE`) |
| Error handling matches the established shape | Yes | The short-spine branch logs, clears, closes the member file and returns false, the same shape as the version-mismatch branch above it (`BookMetadataCache.cpp:494-499` against `:524-530`). An OOM in `makePsramNavCache` returns null and the caller keeps today's `OOM: Go to cache` line (`EpubReaderActivity.cpp:1013-1016`). Each failed check in the warm path logs its own reason once at DBG, and the caller falls back to a cold load |
| Tests well designed, not merely present | Yes | `BookMetadataCacheTest` runs the real `BookMetadataCache.cpp` against the Storage fake, using the `test/storage_io` + `test/pagination` link-time-substitute model (`CMakeLists.txt:1-3`, `ZipFileStub.cpp`). It pins behaviour, not implementation: read-call count against buffer windows (`:159-169`), truncation (`:180-188`), a refill straddle with 200-byte hrefs (`:144-157`), and the LUT paths still working after the buffered pass (`:190-207`). The intent review confirmed that two of these fail on `origin/main`'s code. `ParkedBookTest` asserts the release of what was parked through `use_count` on every miss, plus the `skipLoadingCss` pass-through, not just return values. The copied `BOOK_CACHE_VERSION` in the test (`:17-18`) fails loudly on drift, because every load test would refuse the file |
| Nothing duplicated that already exists | Yes, except MINOR 1 | `keyInputsFor` removes what would otherwise have been a second copy of the pubkey-input setup (`StudyStore.cpp:37-44`). See also "Considered and not raised" below |

## Considered and not raised

These were checked and judged not worth a finding. They are listed so the next reader does not
re-derive them.

- **`epubSize` (`ParkedBook.cpp:18-23`) does the same job as `BibleSearchStore.cpp:68-73`'s
  `fileSizeOf`.** Both are four-line, translation-unit-private helpers. Sharing one would mean a new
  header and an edit to an unrelated store. That costs more than the duplication does.
- **`Miss::Empty` can't happen in the firmware path.** `ParkedBook::take` returns early on an empty
  slot (`ParkedBook.cpp:56-59`), so `missReason`'s `Empty` case is never logged. The early return has
  a reason, since it skips an SD open on every cold path, and the `switch` has to stay exhaustive. The
  template's own test does reach `Empty`.
- **The `ParkedBook::clear()` calls in the read-folder and Delete-cache destructor branches
  (`EpubReaderActivity.cpp:174,181`)** are no-ops, because `take()` has already emptied the slot. They
  cost nothing and say what the branch means.
- **`reopenCache` repeats `load()`'s two-line CSS validity check, and `reopen()` repeats the
  four-field header read.** Both are short and point back to their source in a comment
  (`Epub.cpp:523-524`).
- **The force-included `shim/PrintForward.h` is the first `-include` under `test/`.** The shared
  stub drops `Print` on purpose (`test/stubs/HalStorage.h:7-9`). A local shim, documented in the
  suite's CMakeLists, is the least invasive way to compile `ZipFile.h` without changing every suite's
  stub.
- **`StudyReuseTest` stubs the pubkey clause.** The intent review already raised this as MINOR 2, and
  the author declined it with a reason.
- **The step-timing `[[maybe_unused]] millis()` lines in `openPublication`
  (`StudyStore.cpp:67-90`)** follow `Epub::load`'s own permanent step timing
  (`Epub.cpp:421,430,448,486`).

VERDICT: CLEAR
