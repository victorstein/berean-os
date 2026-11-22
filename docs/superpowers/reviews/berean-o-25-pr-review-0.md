Tier: standard

# berean-o-25 — PR review 0

Range: `origin/batch/20261122-tarjetas-preaching-cards-in-predicaci-n-65ka...origin/feature/preaching-handouts-reader`
(head `8bb44928`). Code commits: `e69e9bf4 feat(preaching): read the preaching cards file`,
`8bb44928 docs: describe the preaching cards file`. The rest of the range is research, spec, plan and
their reviews. There are no empty reply commits, so the worker has rejected no finding.

What I ran:

- Extracted the plan's code blocks and diffed them against the files on the branch.
  `src/study/PreachingHandouts.h` and `.cpp` match this bead's plan byte for byte, and they also match
  the parent plan (`docs/superpowers/plans/2026-11-22-preaching-cards.md`, Task 4). The test file
  matches this bead's plan except for clang-format's line wrapping at `PreachingHandoutsTest.cpp:121-127`.
  It differs from the parent plan only by spec Departure 4: the 63-byte href and the parse-OOM case.
- `cmake --build build/test --target StorageIoTest` built, and `ctest --test-dir build/test -R PreachingHandouts`
  reported "100% tests passed out of 9". `./build/test/storage_io/StorageIoTest` reported
  `[  PASSED  ] 107 tests.`, which is the plan's 98 + 9.
- Deleted the host objects and rebuilt them: no warnings.
- `pio run -e x4pro`, with the object deleted and a fresh `PLATFORMIO_BUILD_CACHE_DIR` so the build
  cache could not serve it: `Compiling .pio/build/x4pro/src/study/PreachingHandouts.cpp.o` gave no
  warning. The only warning in the build is in the WebSockets libdep, and it was already there.
  SUCCESS.

## Intent

Each acceptance criterion in the brief:

- **Header and implementation as in plan Task 4.** Met. `FILE_PATH` "/preaching-cards.json"
  (`PreachingHandouts.h:21`), `FORMAT_VERSION` 1 (`:22`), `MAX_FILE_BYTES` 128 KiB (`:24`),
  `MAX_HREF_BYTES` 63 (`:26`), `MAX_HANDOUTS` 200 (`:28`), and `summaries()` and `load()` (`:53,57`)
  with Ok / Missing / Unreadable / TooNew / OutOfMemory (`:31`). The 63 is right:
  `ReaderEntryIntent::HREF_BYTES` is 64 (`src/activities/reader/ReaderEntryIntent.h:23`).
- **9 tests in `StorageIoTest`, whole suite green.** Met. See the commands above.
  `test/storage_io/CMakeLists.txt` adds the test and the source next to `PreachingPassages`.
- **Status mapping.**
  - `v` > 1 is TooNew (`PreachingHandouts.cpp:72-75`), and the check runs before the `cards` check.
  - Absent or 0 `v`, or a missing `cards` array, is Unreadable (`:76-79`).
  - A file of 0 bytes or over the cap is Unreadable before any parse (`:54-58`).
  - Malformed JSON is Unreadable (`:67-70`).
  - `out` is cleared on entry to both passes (`:86,115`) and reset on a text OOM (`:155`), so nothing
    partial is returned.
  - Both passes skip a bad href through the same `hrefOpens` (`:37-40,105,147`), so a summary's count
    matches `load()`'s verse count.
  - Every one of these is tested. The 63-byte boundary is pinned (`PreachingHandoutsTest.cpp:117,134-135`).
- **`docs/file-formats.md`.** Met. Version 1 is documented before the
  `/.berean/passages/<pubkey>.json` heading. It says "about 65 KB", not 50 KB, and labels a
  whole-chapter citation with the chapter alone (`Salmo 51`), as the brief asks.
- **`pio run -e x4pro`, no new warnings.** Met. See above.

Spec coverage:

- Every row of the spec's error table (`design.md:143-155`) is implemented.
- All four "Departures from the plan" are applied.
- No caller, UI, string, `PreachingOrigin` or `PreachingContent` change came in, so there is no
  scope expansion.
- The `CMakeLists.txt` comment edit is the plan's own Task 1 Step 2.
- One small spec-to-plan divergence: spec Departure 4 names the model's `CountdownJsonAllocator`
  with `remaining = 0`, and the plan wrote an equivalent `ExhaustedJsonAllocator`
  (`PreachingHandoutsTest.cpp:34-38`). The behaviour is the same, plan review 0 accepted it, and it
  is not a finding.

The tests check behaviour, not the implementation:

- titles, counts and file order
- section indexes
- an empty heading surviving
- Missing in both passes
- TooNew
- six unreadable shapes, each also checking that the output is empty
- an index past the end
- both href bounds plus the exact-63 keep
- text OOM and parse OOM, each leaving title, headings and verses empty

## Quality

- **It mirrors the model.** `readFiltered`, `newDocument`, the file-local `JsonFileReader`, the
  512-byte `BufferedFileReader`, `exists` before `open` to avoid a log, the size check before
  parsing, OOM detection through `NoMemory || overflowed()`, the filters, `utf8SafeSummary` caps, and
  `[[maybe_unused]] millis()` timing with one `LOG_INF` are all the shape of
  `src/study/PreachingPassages.cpp:29-95,161-188`.
- **The deliberate departures are justified.** TooNew as its own status (spec A3) and the
  third copy of `JsonFileReader` (A14, with the model's own comment as precedent) both have reasons.
- **Resources.**
  - `reserve()` comes before both push loops (`:99,139-140`).
  - There are no bare `new`, no `std::function`, and no explicit `close()`.
  - The largest stack local is the `BufferedFileReader` handle; its buffer is on the heap.
- **No dead code.** Nothing is commented out, and no comment narrates the code.
- **The comments explain why.** The SRAM routing note, the reasons for the 63, 200 and 128 KiB
  caps, and the single template instantiation are all non-obvious.
- **The tests follow `PreachingPassagesTest.cpp`.** Same fixture, `storage_fake`, made-up text and
  allocator injection.

## Findings

1. **MINOR: the `Handout::headings` comment says the opposite of what the code returns.**
   `src/study/PreachingHandouts.h:47` reads `// one per section; empty for a single-article handout`.
   For a single-article handout the vector is not empty: it holds one empty string.
   `load()` pushes a heading for every section (`PreachingHandouts.cpp:144`), and
   `ASingleArticleHandoutHasOneEmptyHeading` asserts `headings.size() == 1` and `headings[0].empty()`
   (`PreachingHandoutsTest.cpp:77-78`). The follow-up UI will read this header, and it could test
   `headings.empty()` to decide whether to draw headings, which would never be true. Suggested
   wording: `// one per section; a single-article handout's one heading is empty`.

2. **MINOR: the format doc leaves out the heading cap.** `docs/file-formats.md` gives the cut for
   `title` (96 bytes) and `ref` (48 bytes) but not for `heading`. `load()` cuts headings at
   `MAX_TITLE_BYTES` too (`PreachingHandouts.cpp:144`; spec A8). The generator lives outside this
   repo and is written against this document, so the document should state every limit. Add
   "cut at 96 bytes" to the `heading` clause.

VERDICT: CLEAR
