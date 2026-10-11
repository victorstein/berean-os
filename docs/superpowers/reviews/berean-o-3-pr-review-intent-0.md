Tier: heavy

# PR #266 intent review: does it do what berean-o-3 asked?

PR: `perf: make opening the Bible from Home cheap` (`perf/r12-bible-open` → `main`, head `65541ec8`).
Reviewed against the brief (`hpipe brief --task t3`), the spec
(`docs/superpowers/specs/2026-10-11-berean-o-3-design.md`) and the plan
(`docs/superpowers/plans/2026-10-11-berean-o-3-plan.md`).

**Result:** the PR does what was asked. It implements every spec requirement, including the harder
half: the take-time revalidation, the clean-load and pubkey conditions on study reuse, no park after
Delete cache, and the PSRAM nav cache. It follows the plan verbatim. It neither drops nor adds scope.
There are two MINOR findings. Both concern the PR body or how deep one test goes, and neither touches
behaviour.

## Evidence gathered

### Brief acceptance criteria

| Criterion | Status | Evidence |
|---|---|---|
| `Book loaded in` drops substantially, with the new figure and the log lines that prove it in the PR | Met as far as it can be before the device run | PR body gives the baseline (501–507 ms), says "to measure" for the after figures, and names the proving lines. The brief's shared context says "Device runs are the owner's … Never claim a device result you have not seen", so "to measure" is the right answer. The log tags were checked against the source: `[READER] Book loaded in` `ReaderActivity.cpp:60`; `[EBP] Loading ePub` `Epub.cpp:362`; `[BMC] Loaded cache data` `BookMetadataCache.cpp:533`; `[STUDY] Open steps` (`MODULE = "STUDY"`, `StudyStore.cpp:17`); `[PARK] …` `ParkedBook.cpp`; `[ERS] Study already open for this book` `EpubReaderActivity.cpp:278`; `[RBS] Already first …` `RecentBooksStore.cpp:33` |
| No regression in host suites for the book cache, recents or study | Met | Re-run independently from a scratch CMake wrapper, outside `build/`, with the two new suites added: **100% tests passed out of 1678**, matching the PR. The suites the change reaches pass 242/242 (`BookMetadataCache`, `ParkedBook`, `StudyReuse`, `RecentBooksDoc`, the `StorageIoTest` suites, `BibleNavCache`, `PassageDoc`/`PassageText`, `CrossPointState`, `PubKey`). No compiler warning comes from a new or changed host-compiled file |
| A cache format change bumps its version | Not engaged (correct) | No `book.bin` layout change. The spine pass reads the existing v10 layout through a buffer (`BookMetadataCache.cpp:71-90,515-530`). The spec decides this in A3 and lists it as a non-goal in §3 |
| Shared append points untouched; "For the orchestrator" lines given | Met | `test/CMakeLists.txt` and `src/main.cpp` are not in the diff. The PR body carries `add_subdirectory(book_metadata_cache)` and `add_subdirectory(parked_book)`. Both new executables (`ParkedBookTest`, `StudyReuseTest`) live under `test/parked_book/CMakeLists.txt`, so those two lines cover all three suites |
| Explicit PSRAM for the new cache, justified in the PR body | Met | `makePsramNavCache` uses `heap_caps_calloc(1, sizeof(BibleNavCache), MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` with a destroying deleter (`EpubReaderActivity.cpp:148-161`), modelled on `CatalogIndexStore::PsramFree` (`CatalogIndexStore.h:86-89`). The PR's "Memory" section justifies it |
| Timings in `LOG_DBG`; no `std::function`; title `perf: …`; body ends `Refs berean-o-3` | Met | `StudyStore.cpp:89-90`; the last line of the PR body is `Refs berean-o-3` |

### Spec requirements → implementation

- **A3/A4, cold-load buffered pass.** `readCumulativeSizes` checks every read and skips hrefs of any
  length in 64-byte chunks (`BookMetadataCache.cpp:71-90`). On failure `load()` clears the sizes,
  closes `bookFile` and returns false, leaving `loaded` false (`:521-530`). `Epub::load` then takes its
  build branch (`Epub.cpp:412-416`).
- **A6, `reopen()`.** Closes the old handle, reopens the file, and compares size, version, `lutOffset`,
  `spineCount` and `tocCount`. On a mismatch it closes and stays unloaded (`BookMetadataCache.cpp:537-565`).
- **A7, `reopenCache`.** Runs `load()`'s own `hasCache()`/`loadFromCache()`/`clear()` sequence and
  returns false instead of rebuilding (`Epub.cpp:520-531`, against `Epub.cpp:375-407`). `load()` always
  creates `cssParser` and clears its rules before returning (`Epub.cpp:366,407`). So a parked `Epub`
  holds the same CSS state as a fresh one, and an embedded-style toggle between round trips is
  re-checked under the current value (`EpubReaderActivity.cpp:218-219`).
- **A5/A8, the slot.** `take()` always empties the slot and misses on path, on size (0 never matches)
  or on `reopenCache` (`ParkedBookOf.h`). The destructor clears the slot on a read-folder move or
  after Delete cache, and parks otherwise (`EpubReaderActivity.cpp:172-187`). `cacheDeleted` is set in
  the Delete-cache branch (`:960`). A null `epub` or an EPUB that cannot be sized clears the slot
  (`ParkedBook.cpp`, `park`). `ActivityManager` destroys the old activity before the new `onEnter`
  (`ActivityManager.cpp:142-160`), so take always runs after park.
- **A9/A10, study reuse.** All four conditions are in place: `units_ && units_->ready()`, the
  `weak_ptr` identity, `loadedCleanly_`, and the `local-` key re-resolved through `keyInputsFor`
  (`StudyStore.cpp:37-54,110-119`). `closePublication` clears the new fields (`:104-105`). The reuse
  branch sets `highlightsLoaded = true`, clears the link source through `resumePublication`, and still
  runs the `saveDisabled()` toast, `repairTexts` and registration (`EpubReaderActivity.cpp:274-288`).
- **A11.** The nav cache moves with the parked `Epub` (`:186,223`). The `static_assert` about routing
  by size was removed because it no longer describes the allocation (`BibleBookIndex.h`). The Home
  Go-to path does go through the reader (`onBookLoaded` → `openChapterPicker`,
  `EpubReaderActivity.cpp:1122-1124`) and hits `navCache->hasBooks()` (`BibleNavigationActivity.cpp:116-121`).
- **A14, R5.** `normalise`, then `isAlreadyFront`, then an early return before `pruneMissing` and the
  save (`RecentBooksStore.cpp:27-34`). The comparison is on path, title and author
  (`RecentBooksDoc.cpp:43-47`).
- **A16.** One `LOG_DBG` with the pubkey, palette, passages and unit-index times (`StudyStore.cpp:89-90`).
- **§5.1 row 9 comments.** `BibleNavCache.h:15-18` and `BookMetadataCache.h:68` are updated.
- **Spec reviews.** Spec review 1 raised MAJOR 1–2 and MINOR 3–6, and plan review 0 raised MINOR 1–5.
  All of them show up in the code or the tests: the clean-load and pubkey conditions, no park after
  Delete cache, the long and straddling href tests, `highlightsLoaded`, the link-source reset, clearing
  the slot when there is nothing to park, and the `skipLoadingCss = false` assertion in
  `ParkedBookTest.cpp`.

### Plan conformance

Every new file was diffed mechanically against the plan's code blocks: `ParkedBookOf.h`,
`ParkedBook.{h,cpp}`, `StudyReuse.h`, `ParkedBookTest.cpp`, `StudyReuseTest.cpp`, and
`BookMetadataCacheTest.cpp` (Steps 3.5 and 5.1). All are identical apart from markdown fence lines.
The edits to existing files match Steps 1.3–11.12 text for text. The ten implementation commits map
one to one onto Tasks 1–11, with Tasks 3 and 4 in one commit as Step 4.6 says. No `fix:` or `style:`
commit was needed. The one recorded deviation from the spec, keeping the template in `ParkedBookOf.h`,
is explained in the plan (Task 7). Nothing in the PR diverges from the plan without a stated reason.

### Do the tests exercise behaviour?

`BookMetadataCacheTest` runs the real `BookMetadataCache.cpp` against the in-memory Storage fake. Its
six pre-`reopen` tests were run against **`origin/main`'s** `BookMetadataCache.cpp`:
`TheSpineIsReadInBufferWindowsNotPerEntry` fails with 15,762 reads, and `AShortSpineBlockFailsTheLoad`
fails because `load()` returns true. The other four pass. So the suite pins the behaviour that changed,
not the implementation. A layout replay of `EntriesStraddlingABufferRefillLoadWhole` shows refill
boundaries falling inside a short href, inside the length and pod fields, and inside three ~200-byte
hrefs. Spec review MINOR 3's straddle case is therefore really exercised. `ParkedBookTest` checks the
miss reasons, `use_count` release, slot emptying and both `skipLoadingCss` values. For the second
MINOR below, see `StudyReuseTest`.

### Scope

Nothing in the diff goes beyond the spec. The `keyInputsFor` extraction is needed for the
re-resolution, and the lambda rename in `addBook` is forced by the new `book` local. Every reduction
is a stated decision: `openPublication`'s cold cost is only instrumented (§3, A16); there is no format
change (A3); there is no second slot (A2). None of them is silent.

## Findings

### MINOR 1: device step 3 expects a nav-cache hit that the steps before it never set up, and step 4 assumes embedded style is on

`navCache` is allocated only on a Go to (`EpubReaderActivity.cpp:1012-1016`). A reader that never
opened Go to parks a null cache (`:186`), and `BibleNavigationActivity::loadBooks` reads from the cache
only when `navCache && navCache->hasBooks()` (`BibleNavigationActivity.cpp:116-121`). The PR's step 2
is "Home → Bible five times", with no Go to. Step 3 then says "After a round trip, a Home Go to logs
`[BNV] Go to: books from cache`". In that order, the first Home Go to still rebuilds the list
(`books ready` at about +260 ms) and only the second one hits. The owner would read a correct build
as a failed one. Spec §9.1 step 3 has the same wording, so the PR inherited it.

Step 4 also expects `[PARK] Parked book dropped: its cache changed` on the tap after Clear cache. That
holds only with embedded style on. With it off the take passes, as spec §6.4 says ("**Off**: warm,
reading the **new** file"). Style on is the default and the owner's setting, but the step should say so.

**Fix inline (PR body only):** step 3 should read "Home → Bible → Go to → Home, then Home Go to: the
second Go to logs `[BNV] Go to: books from cache`". Step 4 should start "With embedded style on (the
default)". The before→after row for Home Go to should say it measures the second Go to.

### MINOR 2: StudyReuseTest checks the pubkey clause against a stubbed bool, not the real re-resolution

Spec §9 asks that "a `local-` key that now resolves to a registered one refuses". `StudyReuseTest`
models that by flipping a global `keyResolves = false` behind the callback
(`test/parked_book/StudyReuseTest.cpp:11-15,60-66`), so the suite is a truth table of the two-line
`mayReuse`. The code that turns a registry entry into a different key is not run by any host test.
That is `keyInputsFor` plus `keyStillResolves`, and the `in.keyStable = study::pubKeyIsStable(pubKey_)`
wiring in `isOpenFor` (`src/study/StudyStore.cpp:37-54,116`). The spec's design accepts that
`StudyStore.cpp` has no host suite, so this is not a gap against the spec's letter.
`study::resolvePubKey` and `study::pubKeyIsStable` are host-compiled already (`PubKeyTest`), though.

**Optional fix inline:** one test feeding real `study::PubKeyInputs` through `resolvePubKey` would turn
the clause into a test of the actual resolution. It would take `registered` from `nullopt` to a value
for a non-Bible path, and assert that the key changes and that `pubKeyIsStable` was false for the
`local-` key. No behaviour change is needed.

## Considered and not raised

- **No after figure in the PR.** Not a finding. The brief gives the device run to the owner and forbids
  claimed results. The bead should not close until the owner has filled in the "to measure" cells.
- **A truncated `book.bin` now rebuilds without the Indexing popup** (`loadFreshEpub`'s check is
  existence-only, `EpubReaderActivity.cpp:197-201`). This is the same as today's behaviour on a version
  bump, and the spec puts it out of scope in §3. A4's new trigger is rare, because `buildBookBin`
  already deletes short writes. Not a finding against this PR.
- **The extra `reusingParkedBook &&` guard on study reuse** (`EpubReaderActivity.cpp:274`) is in plan
  Step 11.8. It is redundant, since a fresh `Epub` can never match the `weak_ptr`, but harmless.
- **Other holders of the parked `Epub`.** `BibleSearchStore` identifies the Bible by path, size and
  spine count, not by object (`BibleSearchStore.cpp:92-111,398-401`). `BibleSearchIndexer` lives inside
  `BibleSearchActivity`, a reader sub-activity, and no study or reader code creates a task. Every
  `STUDY.` caller sits under `src/activities/reader/`. A5's claim that "nothing reads through a parked
  book" holds on the merged `main`, R11 included.

VERDICT: CLEAR
