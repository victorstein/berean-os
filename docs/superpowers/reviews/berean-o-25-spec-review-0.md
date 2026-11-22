Tier: standard

# Spec review 0: berean-o-25, `PreachingHandouts`

Reviewed: `docs/superpowers/specs/2026-11-22-berean-o-25-design.md` (commit `357b906e`), against
the brief (`hpipe brief --task t1`), the research note
`docs/superpowers/research/2026-11-22-berean-o-25-research.md`, the parent plan
`docs/superpowers/plans/2026-11-22-preaching-cards.md:519-988` that the spec adopts, the parent
design §2–3 and §5, and the code.

## What was checked and holds

- **Acceptance coverage.** Every acceptance item is in the spec: constants (`FILE_PATH`,
  `FORMAT_VERSION` 1, `MAX_FILE_BYTES` 128 KiB, `MAX_HREF_BYTES` 63, `MAX_HANDOUTS` 200), the five
  statuses, `v > 1` TooNew, absent/0 `v`, missing `cards`, 0 bytes, over the cap and malformed
  JSON as Unreadable with nothing partial, the href skip in both passes, 9 tests in
  `StorageIoTest`, the docs section before `/.berean/passages/<pubkey>.json`
  (`docs/file-formats.md:577`), and "about 65 KB".
- **Model fidelity.** The plan's `readFiltered` matches `src/study/PreachingPassages.cpp:59-95`
  step for step, except the version check, which splits TooNew from Unreadable as the brief
  requires. `JsonFileReader` is the model's class (`PreachingPassages.cpp:27-40`).
- **A1.** `protectedpath::isProtectedName` refuses any path component starting with `.`
  (`lib/ProtectedPath/ProtectedPath.cpp:21-23`); upload checks it
  (`src/network/CrossPointWebServer.cpp:588`). The SD root is the only fit.
- **A6 / `MAX_HREF_BYTES`.** `ReaderEntryIntent::openHref` refuses `link.size() >= HREF_BYTES`
  with `HREF_BYTES = 64` (`src/activities/reader/ReaderEntryIntent.h:23,48-49`), so 63 is exactly
  the largest href it holds.
- **A7.** `PreachingOrigin::question` is `uint8_t` (`src/activities/preaching/PreachingOrigin.h:11`);
  200 fits.
- **A8.** `PassageDoc::MAX_REFERENCE_BYTES = 48` (`lib/StudyStore/StudyStore/PassageDoc.h:44`);
  `utf8SafeSummary(std::string, size_t)` (`lib/Utf8/Utf8.h:32`).
- **Memory claims.** On a 32-bit target ArduinoJson uses 2-byte slot ids and 128-slot pools,
  "1024 bytes" (`build/test/_deps/arduinojson-src/src/ArduinoJson/Configuration.hpp:105-117`), so
  pool blocks are under the 4,096-byte routing threshold as A9/Memory say; string length is 2 bytes,
  up to 65,535 (`:140-145`); nesting default 10 (`:93`), the file nests 7. `BufferedFileReader`
  allocates its buffer with `makeUniqueNoThrow` and falls back to passthrough when that fails
  (`lib/Serialization/BufferedFile.h:76-84`), so a buffer OOM is not misreported.
- **A5.** `HalStorage::openFileForRead` passes through to `SDCard.openFileForRead` with the
  module name (`lib/hal/HalStorage.cpp:100-105`), the logging path the model avoids.
- **`PassageText`** is move-only with `noexcept` moves and an `assign` that keeps the old text on
  failure (`lib/StudyStore/StudyStore/PassageText.h:31-66`), so `Verse` moves into the vector and
  `out = Handout{}` releases earlier texts.
- **Departure 2 is real.** `ctest --test-dir build/test -R StorageIo` prints "No tests were
  found!!!" and exits 0; `-N -R PreachingPassages` lists 12; the executable runs 98 tests and
  passes. 98 + 9 = 107 is right for today's base.
- **Formatter note.** `bin/clang-format-fix` lists files with `git ls-files --exclude-standard`
  and no `--others`, so an untracked new file is skipped; staging first is correct.
- **Non-goal gc-sections claim.** `/Volumes/stein/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/pioarduino-build.py`
  and `flags/ld_flags` carry `gc-sections`.
- **A13 citations.** `PreachingContent.cpp:81` and `PreachingSession.cpp:151` call the model with
  the PSRAM allocators.

No BLOCKER and no MAJOR found. The design is the parent plan's code, which is a close copy of a
reader already shipped and tested, and the two departures are both correct.

## MINOR

### 1. The tests leave the href boundary and the parse-OOM path unproved

- **Claim.** The spec's test table says `AVerseWhoseHrefCannotOpenIsSkipped` proves A6 with
  "empty and 64-byte hrefs", and the error table lists "Filter or parse out of memory →
  `OutOfMemory`" and "A verse text cannot be allocated → empty, earlier verses released".
- **Problem.** No test loads a 63-byte href, so an off-by-one (`bytes < MAX_HREF_BYTES`) would pass
  all nine tests while dropping hrefs that `openHref` accepts. No test drives the JSON allocator to
  failure, though the model's test does. `ATextThatCannotBeAllocatedIsOutOfMemory` fails on the
  first verse, so "earlier verses released" is never exercised.
- **Evidence.** Plan test `:620-633` builds only `""` and `MAX_HREF_BYTES + 1` hrefs.
  `test/storage_io/PreachingPassagesTest.cpp:185-189` covers parse OOM with
  `CountdownJsonAllocator`. Plan `:635-642` uses an allocator that always fails.
- **Fix.** In `AVerseWhoseHrefCannotOpenIsSkipped`, add a verse whose href is exactly
  `MAX_HREF_BYTES` bytes and expect it kept (the count becomes 2). Optionally, add a
  `CountdownJsonAllocator` case to the OOM test, as the model does. The brief counts 9 tests, so
  extend existing tests and do not add new ones.

### 2. The Memory section leaves out the internal SRAM an open handout holds

- **Claim.** Memory: "Output. One handout's texts through `allocators.text`: PSRAM on the device."
- **Problem.** Only `Verse::text` goes through the allocator. `reference`, `href`, `headings` and
  `title` are `std::string` on the default heap. An href such as
  `1001061144-split6.xhtml#chapter6_verse34` (40 B) is over the SSO, so there is one internal-SRAM
  allocation per verse: about 77 for the largest handout, a few KB held while it is open. The
  CLAUDE.md resource rule ("justify any new heap allocation") wants this stated. It is the same
  trade the model makes with `Passage::reference` (`src/study/PreachingPassages.h:35`), so the
  design itself can stay.
- **Fix.** Add one bullet to Memory: per verse, the href and any ref over the SSO are on the
  internal heap, a few KB for 77 verses. Leave them so on purpose, as `Passage::reference` is,
  because the follow-up copies the href into `ReaderEntryIntent::href`.

### 3. The brief's whole-chapter label note is not carried into the docs section

- **Claim.** Departure 1 is the only change to the plan's Task 5 docs text ("Everything else in
  Tasks 3–5 is taken as written").
- **Problem.** The brief's docs instruction goes on: "Whole-chapter citations are labelled with the
  chapter alone (`Salmo 51`)." The plan's `ref` bullet (`:980`) says only "the label, cut at 48
  bytes", so the shipped format doc will not say how a whole-chapter label looks. The reader does
  not care, but a tester checking a card against the doc will.
- **Fix.** Add a departure: the `ref` bullet adds "a whole-chapter citation is labelled with the
  chapter alone, e.g. `Salmo 51`".

### 4. "98 + 9 = 107" is pinned to a base that siblings can move

- **Claim.** Testing: `./build/test/storage_io/StorageIoTest  # 98 + 9 = 107 pass`.
- **Problem.** The brief warns that a sibling may land on the batch branch before `implement`. A
  sibling that adds `StorageIoTest` cases changes the total, and an implementer who checks against
  107 will hunt a phantom failure.
- **Fix.** Phrase the expectation as "all pass, the count being the base's plus 9". Re-read the
  base count when `implement` starts.

VERDICT: CLEAR
