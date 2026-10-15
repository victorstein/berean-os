Tier: standard

# PR review 0: berean-o-15 (t2), the preaching topics store and the `bible.json` passes

Range: `origin/batch/20261015-predicaci-n-193x...origin/feature/preaching-topics-store` at
`ebbdf045`. Code: `lib/Serialization/SdPaths.h`, `src/study/PreachingTopicsFile.{h,cpp}`,
`src/study/PreachingPassages.{h,cpp}`, `test/storage_io/{CMakeLists.txt,PreachingTopicsFileTest.cpp,PreachingPassagesTest.cpp}`.
There are no empty reply commits on the branch: no earlier findings exist to answer.

What I ran myself:

- `StorageIoTest`, built from this tree into a scratch build dir: **98 tests from 8 suites pass**.
  That is the 69 baseline plus 17 `PreachingTopicsFileIo` and 12 `PreachingPassagesIo`. The only
  host warnings in the new files are `unused variable 'MODULE'`, the same one `TagPaletteFile.cpp:12`
  and `PassageFile.cpp:10` already raise on the host, because `LOG_*` compiles out there.
- `pio run -e x4pro`, into a scratch build dir: **SUCCESS**. The build has one warning, the existing
  `WebSocketsClient.cpp:573` deprecation in libdeps. None comes from the new files.
- I did not run `clang-format-fix`, because it rewrites files. The last commit is
  `style(preaching): apply clang-format`, and the tree is clean.

## Intent

**Brief acceptance, item by item:**

- **Path.** `/.berean/preaching.json` is declared at `SdPaths.h:28`. It is outside `PASSAGES_DIR`,
  and the build enforces that with `static_assert(!isUnder(PREACHING_FILE, PASSAGES_DIR))`. Format
  `{"v":1,"tags":[...]}`: the round trip test asserts the exact bytes
  (`PreachingTopicsFileTest.cpp:161`).
- **Writes.** They go only through `writeDocToFileAtomic` (`PreachingTopicsFile.cpp:94`).
  - The budget is checked first with `persist::fitsBudget` (`:90`), and also after an `overflowed()`
    guard (`:86`), as `PassageFile::save` does.
  - `v > 1` is refused through `isKnownFormatVersion(json["v"] | 0, 1)` (`:24`), which also refuses
    an absent `v`.
  - More than 200 ids is refused before dedupe (`:28`). This is A15.
- **Latch.** `saveRefused_` starts `true` (`PreachingTopicsFile.h:48`), so an object that never ran
  `load()` cannot save (A3). `load()` sets the latch when `preaching.json` or `tags.json` comes back
  `Failed` (`.cpp:45,55,59`). `save()` checks the latch before anything else (`:64`).
  - This is the "one obvious way to hold the latch" the brief asks for. T5 cannot reach a write
    without going through it.
  - A missing file is `Empty`, with saving allowed (`AMissingFileIsAnEmptySelectionThatMaySave`). A
    save after an `Empty` load is exercised in `SaveThenLoadRoundTripsThePicks`.
- **Pruning.** Inactive and unknown ids are dropped on save, and duplicates are removed (`:71-73`).
  The test retires 7, uses the never-allocated id 99, and repeats 12 (`:169-180`). The palette
  comes from the object's own `TagPaletteFile::load`, not from the caller. That is the spec's M1
  fix (A9), and it closes the "empty `StudyStore::palette()` prunes everything" hole.
- **Read-only streaming.** Both passes stream through a 512 B `BufferedFileReader` with an
  ArduinoJson filter (`PreachingPassages.cpp:78-80`). They check `exists`, open for read, and never
  use `Storage.readFile`, `loadAdopting` or a write. Two tests assert the file's bytes are unchanged
  and that a leftover `.tmp` is neither adopted nor created (`PreachingPassagesTest.cpp:106-118,196-203`).
  - **Counts.** The counts filter keeps only `u` and `t`, so no text is ever allocated.
  - **Text in PSRAM.** Texts are `PassageText` on the caller's `allocators.text`, so they land in
    PSRAM on the device. The header tells T3 it must pass the PSRAM allocators.
  - **Bible order.** Ordering is a `stable_sort` on `(spine, start)`. It is tested with a
    spine-before-book case and a tie that keeps file order (`:124-143`).
  - **Snippet fallback.** The `snippet` fallback, and the empty-text case, are tested at `:145-156`.
- **Missing versus unavailable.** A missing file returns `Status::Missing`, which is distinct from
  `Ok` with zero, and `isAvailable` is false for it. Unreadable cases (empty, over the cap, `v:5`,
  no `v`, no `p`, truncated, a failed read) return `Unreadable`. OOM in the parse document or in a
  text returns `OutOfMemory` with `out` cleared, and none of them aborts.
- **Timing.** Each pass logs its duration with `LOG_INF` on success (`:185,204`).

**Tests against the brief's list.** Every listed case has a test:

- round trip;
- budget;
- newer version;
- over the cap, plus exactly 200;
- retired-tag pruning;
- the latch: a newer version, a corrupt file, an unreadable file, a corrupt palette, and save before
  load;
- a missing file;
- counts of 0, 1 and several (the 1 is in `CountsSkipUnaddressableRowsAndRepeatedTags`);
- order with the fallback;
- a missing file in both passes.

The tests check observable results: file bytes, return codes, `tags()`. They do not mirror the
implementation.

**Scope.** There is no UI, no `tr()` key, and no change to `tags.json`, `PassageDoc` or
`TagPalette`. The two passes do what was asked and nothing more.

**Departures.** All of them are recorded in the spec and accepted there:

- the counts peak grows with rows (A4);
- the tag pass holds every row's text transiently (A5);
- the latch is per object (A2).

The code follows the plan's listings almost verbatim, so there is no unexplained divergence from
the plan.

**Landing note, not a finding.** The spec's hand-off line has not been applied, and it is not t2's to
apply. It adds `PreachingTopicsFile` to the list at `lib/Serialization/PersistableStore.h:111`. The
orchestrator applies it at landing.

## Quality

**Patterns it follows:**

- **The store** uses `TagPaletteFile`'s shape: `AdoptedLoad`, `loadAdopting` with
  `readDocFromFileChecked`, and the `SaveResult` enum plus `Refused`.
- **The latch** follows `StudyStore::saveDisabled_`.
- **The passes** follow `StudyPassageScan`: the same file cap, the same row rules, and spine read
  with `| 0u` and clamped to 0. `rowTags` copies `PassageDoc::fromJson` and `normaliseTags`:
  0 dropped, no repeats, at most 8 tags.
- **Naming.** The `PREACH` module tag and the file names match their siblings.

**Small duplications.** Two are worth knowing about:

- `JsonFileReader` repeats the file-local one at `PersistableStore.cpp:22`.
- `newDocument` repeats `HomeVerseFile.cpp:20`.

Spec A11 accepts both, because `PersistableStore.*` is outside t2's files, and the comment says
why. Both are a few lines, so I am not raising them.

**CLAUDE.md rules:**

- no bare `new` and no `std::function`;
- `.reserve()` before every `push_back` loop, including the two-walk count-then-fill in
  `collectPassages`;
- stack locals of 34 B (the path) and 24 B (`RowTags`);
- no `close()` on the local `HalFile`;
- `HalStorage` only.

**Comments.** They state real reasons: the PSRAM threshold, why `exists` is checked first, why the
sort uses one key, and why the budget is a constructor parameter. There is no dead or commented-out
code. The `parseTopics` load fills a local vector, and `tags_` is assigned only after the call
succeeds. `save()` leaves `tags_` alone on every non-`Ok` path, and tests check this for `TooLarge`
(both kinds) and `WriteFailed`.

## Findings

1. **MINOR**: the comment at `src/study/PreachingPassages.cpp:114` misdescribes `PassageDoc::fromJson`.
   - The comment reads: "As PassageDoc::fromJson and StudyPassageScan read "s": absent, or past
     UINT16_MAX, is 0."
   - `StudyPassageScan.cpp:92-93` does clamp to 0.
   - `PassageDoc::fromJson` does not. It reads `static_cast<uint16_t>(v["s"] | 0)`
     (`lib/StudyStore/StudyStore/PassageDoc.cpp:321`), which wraps a value over `UINT16_MAX`.
   - The behaviour of `spineHint` is fine. `toJson` never writes a spine over 65,535, and clamping
     to 0 matches the scan. Only the comment's claim about `fromJson` is wrong. Drop
     "PassageDoc::fromJson and" from it, so the next reader does not take the two as equivalent.

No BLOCKER and no MAJOR.

VERDICT: CLEAR
