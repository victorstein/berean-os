Tier: standard

# PR review 1: berean-o-6 (R11), PR #259

PR #259, `perf: keep the Bible search index open between searches`. Branch
`perf/r11-search-index-resident` at `0f1ebbd4`, which is also the PR head (checked with
`gh pr view`). The branch is not behind `origin/main` on any file it touches. This is the first
review of the PR; no pass-0 review was dispatched.

Reviewed against:
- the brief (`hpipe brief --task t6`);
- the spec `docs/superpowers/specs/2026-10-11-berean-o-6-design.md`;
- the plan `docs/superpowers/plans/2026-10-11-berean-o-6-plan.md`;
- `CLAUDE.md` and `.claude/agents/data-dev.md`.

What I ran myself:
- `cmake -S test -B build/review-test`, then built `BibleSearchIndexTest` and
  `BibleSearchFingerprintTest`. **61 passed** and **26 passed**, with no warnings from the new
  files.
- The golden record's check bytes, recomputed in Python from the layout table:
  `d7525d2e7773270a`. This matches `FingerprintFileTest.cpp` `MatchesTheVersionOneLayout`, so the
  vector was not just copied from the implementation's output.
- `gh pr checks 259`: Build x4pro, unit-tests, clang-format, cppcheck, Test Status, title lint and
  script-tests all pass on `0f1ebbd4`.
- A diff of the plan's test names against the PR's: identical (32 tests).
- A diff of the plan's `BibleSearchResidency.h` block against the file: byte-identical.

No `pio` run (that belongs to the main session); CI's Build x4pro covers the dev build.

## Intent

**Acceptance criteria (brief)**

1. **Re-entry no longer pays the ~500 ms reopen.**
   - `onExit` now calls `park()` instead of `close()` (`BibleSearchActivity.cpp:122`).
   - `park()` closes only the file and keeps the reader, term cache and names
     (`BibleSearchStore.cpp:376-391`).
   - `open()` resumes when the fingerprint, the re-read 48-byte header and the file size all match
     (`:326-335`, `:358-374`).
   - The names come back by `memcpy` (`:403-407`, activity `:245`).
   - The re-entry path is one EPUB size read, one `bible.idx` open and one header read, which is
     what spec Goal 1 lists: `restoreBookNames` does not re-identify (`:398-401`).
   - Met by construction. The PR body says the device run has not been done, and does not claim
     the figure.
2. **The first search after boot is cheaper, or the PR explains why not.**
   - The ~1.95 s `resolveDocuments` is skipped on a record hit (`fingerprintFor`, `:141-157`).
   - The PR body gives the before figures (2,436 / 2,440 ms) and an expected ≈ 0.5 s, labelled as
     an expectation.
   - It names the proof lines: `Fingerprint from record in N ms`, no `verse documents` line, and
     frame → `Entering activity: KeyboardEntry`.
   - Met, pending the owner's device run, and the PR says so.
3. **Memory held is stated, and released on the listed events.**
   - The PR body's table (term index, posting page, 4,228 B of names, ~184 B `IndexReader`) matches
     the code. 4,228 B is `66×48 + 66×16 + 4` (`BibleBookNameTable.h:57-59`).
   - Release events in the code:
     - rebuild: `BibleSearchIndexer.cpp:57-58`, unchanged;
     - query I/O failure: `BibleSearchActivity.cpp:568`, unchanged;
     - Bible change: `identify` → `forgetBible` → `releaseResident` (`BibleSearchStore.cpp:101-107`,
       `:112-113`);
     - PSRAM floor at exit: `:380-386`;
     - sleep: the chip reset (spec A7).
   - Met.

**Brief work items**
- **Resident in explicit PSRAM, keyed by the Bible's identity.**
  - The term index, posting page and names all come from `psramAllocator()`, which is
    `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` (`:31`).
  - The identity is path, size and spine count (`:92-110`).
  - The names use the store's own `AllocatedBuffer` idiom rather than the brief's `PsramFree`
    example. That is the better choice here: it is how `documentsBuffer_` in the same class
    already does it (spec A4).
- **The "PSRAM pressure" release event** is implemented as a retention floor checked when the
  screen is left. Pressure that arises later does not release.
  - This narrowing is disclosed in three places: the spec's Non-goals, the comment at
    `BibleSearchResidency.h:13-18`, and the PR body's "when the screen is left" wording.
  - A process-wide release hook would commit other surfaces to a contract, so leaving it out is
    within the brief. It is not a silent reduction.
- **The 2.2 s was diagnosed and persisted** under decision D1. The record follows the decided
  shape:
  - its location and key;
  - the decode order: magic, then version, then length, check and key
    (`BibleSearchFingerprintFile.cpp:78-99`);
  - temp + rename (`:120-154`);
  - one 40-byte read (`:101-118`);
  - fingerprint-only use, since `documents()` always resolves for real (`BibleSearchStore.cpp:168-173`).
- **Entry refresh mode (R20).** Untouched: the diff changes only `onExit`, the Opening case of
  `runBusyWork`, and `openIndex`.

**Spec requirements: all implemented, including the harder halves**
- **A2:** the exact header-bytes and size comparison.
- **A3/A13:** identity first, then names, with no second EPUB open.
- **A4:** a copy, never a loan, with the `static_assert` at `BibleSearchStore.cpp:29`.
- **A5:** names are kept on the prompt path, because `openIndexAndNames` always runs the names
  step.
- **A6:** floors of 3 MiB free and a 1 MiB largest block, measured with the blocks still held.
- **A8:** `close()` is unchanged, and the names survive it (`:353-356` versus `:393-396`).
- **A10:**
  - only a complete resolve is persisted (`:257-261`);
  - there is one writer, gated by `mayOverwrite`;
  - on the indexer path that writer loads the record first (`:265-270`).
- **A11:** `TooNew` and `Unreadable` are left on the card.
- **A12:** the indexer still resolves the documents itself.
- **Docs:** the `docs/file-formats.md` section covers everything § "Docs" lists, including the
  `BOOK_CACHE_VERSION` bump rule.

**Tests exercise behaviour.** The tests check:
- each step of the decode order, at the lengths the spec names;
- a golden layout vector, verified independently above;
- the published FNV-1a vectors and little-endian helper equivalence. These pin the hash that every
  existing `bible.idx` is keyed by;
- a header mismatch on every one of the 48 bytes, against a real `IndexBuilder` index;
- load and save against the card fake, including "one read, not a byte loop", a short temp write
  and the 41-byte and 48-byte files.

The store and activity wiring is not host-testable (`Epub`/`STUDY`), as the spec states. The two
pure decisions it relies on were extracted and tested.

**Divergence from the plan.** There is one, and it is explained. The activity's `Opened in` line
logs `index ready|unusable`, not the spec's `resumed|opened|unusable`. Plan Task 6 calls this a
"Spec delta, deliberate" (plan `:2220`), and the store's `Index resumed|opened in N ms` line
(`BibleSearchStore.cpp:348`) carries the distinction. Everything else I checked matches the plan
blocks.

**Scope.**
- Nothing is edited outside the plan's `FILES:` lines.
- `test/CMakeLists.txt` and `src/main.cpp` are untouched.
- The follow-up for the persisted name table is filed as `t6/x1`; `hpipe discoveries` lists it.
- No expansion.

## Quality

**It mirrors existing patterns.**
- **Codec:** follows `IndexFormat.cpp` and `UnitIndexFormat.cpp`, including the per-file
  anonymous `put*`/`get*` helpers each sibling codec carries.
- **`save()`:** follows `BibleSearchIndexer::writeAtomic` (`BibleSearchIndexer.cpp:282-310`) step
  for step:
  - flush, then verify the on-card size;
  - `file.close()` before `Storage.remove(tmpPath)`, the close-before-delete case `CLAUDE.md`
    requires (`BibleSearchFingerprintFile.cpp:135`);
  - remove, then rename.
- **The pure, header-only residency file:** follows the `BibleNavCache` idea.
- **Nothing is duplicated.**
  - `Fnv1a64` and `BIBLE_BOOKS` were moved out of the store, not copied.
  - The repo has no generic binary atomic-write helper to reuse. `ProgressFile::writeAtomic` is
    hard-wired to `progress.bin`, and `writeDocToFileAtomic` is JSON-only.

**Naming and structure.**
- `BibleSearchFingerprintFile` sits beside `ProgressFile`.
- The new members keep the store's trailing-underscore style.
- The activity split (`openReader` / `showOpenResult` / `openIndexAndNames`) keeps `openIndex()`'s
  Ready path behaviour-identical.

**Error handling** matches the established shape.
- `save()` logs with `LOG_ERR` and returns false.
- `load()` returns a classified enum, which the store logs once.
- A record failure never blocks search; it falls back to today's recompute.

**Memory.** Every new block is justified in the PR body. Nothing new is allocated on the render
path.

**Comments** are almost all non-obvious *why*s:
- the floor sizing and the known gap;
- the "real size, not bytes read" rule in `load`;
- the remove-then-rename crash window;
- why the index is opened before the names.

Two small blemishes follow.

## Findings

1. **MINOR: a stale, plan-narrating comment in the test CMake.**
   - `test/bible_search_index/CMakeLists.txt:43-44` reads "The fingerprint record's codec, and in
     later tasks its I/O against the in-memory card."
   - The I/O suite is already in this target (`:48`, `FingerprintFileIoTest.cpp`), so "in later
     tasks" describes the plan's sequence, not the merged state. `CLAUDE.md` § Comments says to
     write for the merged state and remove before/after narration.
   - `:41-42` is also a doubled blank line.
   - Fix: "The fingerprint record's codec, and its load and save against the in-memory card." Drop
     the extra blank line.

2. **MINOR: a dead local in `openReader()`.**
   - `src/activities/reader/BibleSearchActivity.cpp:219-220` declares
     `Status status = Status::Missing;` and passes `&status` to `store.open()`.
   - The function then returns either `Status::Ok` or `store.status(epub)` (`:221-224`), so
     `status` is never read.
   - `open()` defaults `statusOut` to `nullptr` (`src/study/BibleSearchStore.h:88`).
   - The old `openIndex()` also discarded this value, but there the variable at least had a later
     use. The rewrite leaves a local with no reader at all.
   - Fix: `reader = store.open(epub);` and drop the local.

Neither finding affects behaviour; both can be fixed inline.

VERDICT: CLEAR
