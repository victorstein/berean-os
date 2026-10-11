# R11: keep the Bible search index open — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to carry out this plan task by task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** a second or later Bible search entry in one boot skips the ~500 ms index reopen and name
load, and the first search after a boot reads its fingerprint from a 40-byte record instead of
spending ~2 s recomputing it.

**Architecture:** all residency lives in the `BibleSearchStore` singleton (`src/study`).
- **Between visits:** the store keeps the opened `IndexReader` (term index and posting page, in
  PSRAM) and a PSRAM copy of the book-name table. It closes the index file when the screen is left
  (`park()`). On the next `open()` it re-reads the 48-byte header and reuses the reader only if
  the header and the file size are unchanged.
- **Across boots:** the fingerprint is persisted as `search-fingerprint.bin` in the Bible's EPUB
  cache directory.
- **What is host-tested:** two pure pieces. `BibleSearchResidency.h` holds the PSRAM floor and the
  header match. `BibleSearchFingerprintFile` holds the codec, the hash and the load/save.
- **What is checked by the firmware build and the device run:** the store and activity wiring.

**Tech stack:** C++20 (GNU), ESP32-S3 Arduino/ESP-IDF via PlatformIO (`x4pro` env), GoogleTest
host suites built with CMake, and the in-memory card fake `test/stubs/HalStorageFake`.

**Spec:** `docs/superpowers/specs/2026-10-11-berean-o-6-design.md`. Its assumptions A1-A13 and
decision D1 are settled, so do not reopen them. The research note is
`docs/superpowers/research/2026-10-11-berean-o-6-research.md`.

## Files

FILES: src/study/BibleSearchResidency.h, src/study/BibleSearchFingerprintFile.h, src/study/BibleSearchFingerprintFile.cpp
FILES: src/study/BibleSearchStore.h, src/study/BibleSearchStore.cpp
FILES: src/activities/reader/BibleSearchActivity.h, src/activities/reader/BibleSearchActivity.cpp
FILES: test/bible_search_index/
FILES: docs/file-formats.md

| File | Responsibility |
|---|---|
| `src/study/BibleSearchResidency.h` (new) | Pure: the PSRAM retention floor (`keepAfterExit`) and the held-header check (`heldIndexMatches`) |
| `src/study/BibleSearchFingerprintFile.{h,cpp}` (new) | Pure codec + `HalStorage` load/save for `search-fingerprint.bin`; owns the shared `Fnv1a64` and `coversEveryBook` |
| `src/study/BibleSearchStore.{h,cpp}` | Bible identity, fingerprint source (memory → record → resolve), the single record writer, resume/park/release, kept book names |
| `src/activities/reader/BibleSearchActivity.{h,cpp}` | `onExit` parks instead of closing; Opening opens the index first, then restores or loads names; one timing log |
| `test/bible_search_index/CMakeLists.txt` | `ResidencyTest.cpp` joins `BibleSearchIndexTest`; new `BibleSearchFingerprintTest` target |
| `test/bible_search_index/ResidencyTest.cpp` (new) | Floors and header match, against a real `IndexBuilder` index |
| `test/bible_search_index/FingerprintFixtures.h` (new) | Shared key, record and byte helpers |
| `test/bible_search_index/FingerprintFileTest.cpp` (new) | Codec, decode order, hash vectors, `coversEveryBook`, `mayOverwrite` |
| `test/bible_search_index/FingerprintFileIoTest.cpp` (new) | Load and save against the card fake |
| `docs/file-formats.md` | New section for `search-fingerprint.bin` (required by D1) |

**Shared append points:**
- No line is needed in `test/CMakeLists.txt`: `add_subdirectory(bible_search_index)` already
  exists at `test/CMakeLists.txt:99`.
- No line is needed in `src/main.cpp` (spec A7).

**Overlap:**
- `docs/file-formats.md` is also on t3's file list. Declaring it here holds this whole task in
  `blocked-on-files` while t3 implements. That cost is accepted: D1 requires the doc, and an
  undeclared edit is what the lock exists to prevent.
- t1 (R20) has landed on `main` (#255). Besides `onEnter`, it touched comments in `showFailure`,
  `finishBuild`, `handleDialogInput` and `render`. None of these intersects a Task 6 block; plan
  review 0 checked this by applying Task 6 to `origin/main`'s files. Task 0's merge brings it in.

## Rules for every task

- **Re-read each file before you edit it.** A sibling task may have landed changes on `main`.
  Every "Replace" block below is quoted verbatim from this branch at `6d84b254`. If one does not
  match, stop and reconcile; never force it.
- **No `pio` on PATH.** Use `/Volumes/stein/.platformio/penv/bin/pio`. Never run two `pio run`s
  at once.
- **Format before committing:** run `./bin/clang-format-fix -g`, then `git add`, then commit.
  Task 8 runs the unsuffixed whole-tree form.
- **Comments:** only a non-obvious why. Do not paraphrase the code.
- **Commit messages:** conventional commits, with no assistant attribution trailer
  (`CLAUDE.md`, Git workflow rule 3).
- **Host build tree:** `build/test` (git-ignored). The first build after a fresh configure can fail
  on a gtest race. Re-run the same command once before believing a failure.

---

### Task 0: Baseline

**Files:** none changed.

- [ ] **Step 1: Bring the branch up to date without rewriting it**

Run:
```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/perf-r11-search-index-resident
git fetch origin
git log --oneline HEAD..origin/main
```
If that lists commits, run `git merge --no-edit origin/main`. Do not rebase: the branch is already
pushed. Then re-read `src/study/BibleSearchStore.{h,cpp}` and
`src/activities/reader/BibleSearchActivity.{h,cpp}`, and diff them against the blocks this plan
quotes.

- [ ] **Step 2: Bootstrap the worktree (idempotent)**

Run: `./bin/bootstrap`
Expected: the output ends with `clang-format 21 already present in .venv/bin` or installs it.

- [ ] **Step 3: Configure and run the existing search suite**

Run:
```bash
cmake -S test -B build/test
cmake --build build/test --target BibleSearchIndexTest -j
./build/test/bible_search_index/BibleSearchIndexTest
```
Expected: the last line is `[  PASSED  ] N tests.`, with no `FAILED`.

- [ ] **Step 4: Baseline firmware build**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro-baseline.log | tail -3`
Expected: `[SUCCESS]`. The first build in a worktree takes ~3-4 min.

---

### Task 1: `BibleSearchResidency.h` — the PSRAM floor and the held-header check

**Files:**
- Create: `src/study/BibleSearchResidency.h`
- Create: `test/bible_search_index/ResidencyTest.cpp`
- Modify: `test/bible_search_index/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `test/bible_search_index/ResidencyTest.cpp`:

```cpp
// When the search store keeps its reader after the screen is left, and when a
// kept reader may serve again.

#include <BibleSearch/IndexBuilder.h>
#include <BibleSearch/IndexFormat.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "src/study/BibleSearchResidency.h"

namespace {

using BibleSearch::IndexBuilder;
using BibleSearch::IndexHeader;
using Bytes = std::vector<uint8_t>;

constexpr uint64_t FINGERPRINT = 0x0123456789ABCDEFull;
constexpr size_t MIB = 1024u * 1024u;

bool appendBytes(void* ctx, const void* data, const size_t length) {
  auto* out = static_cast<Bytes*>(ctx);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + length);
  return true;
}

Bytes builtIndex() {
  IndexBuilder builder;
  EXPECT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 1, 0, 0), "en el principio"));
  EXPECT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 2, 0, 10), "la tierra estaba"));
  Bytes out;
  EXPECT_TRUE(builder.write(appendBytes, &out, FINGERPRINT));
  return out;
}

IndexHeader heldHeaderOf(const Bytes& bytes) {
  const std::optional<IndexHeader> header = BibleSearch::readHeader(bytes.data(), bytes.size());
  EXPECT_TRUE(header.has_value());
  return header.value_or(IndexHeader{});
}

uint32_t sizeOf(const Bytes& bytes) { return static_cast<uint32_t>(bytes.size()); }

}  // namespace

TEST(BibleSearchResidency, TheFloorsAreThreeMebibytesFreeAndAOneMebibyteBlock) {
  EXPECT_EQ(BibleSearchResidency::MIN_FREE_PSRAM, 3 * MIB);
  EXPECT_EQ(BibleSearchResidency::MIN_LARGEST_PSRAM_BLOCK, 1 * MIB);
}

TEST(BibleSearchResidency, KeepsAtAndAboveBothFloors) {
  EXPECT_TRUE(BibleSearchResidency::keepAfterExit(3 * MIB, 1 * MIB));
  EXPECT_TRUE(BibleSearchResidency::keepAfterExit(8 * MIB, 4 * MIB));
}

TEST(BibleSearchResidency, ReleasesBelowEitherFloor) {
  EXPECT_FALSE(BibleSearchResidency::keepAfterExit(3 * MIB - 1, 4 * MIB));
  EXPECT_FALSE(BibleSearchResidency::keepAfterExit(8 * MIB, 1 * MIB - 1));
  EXPECT_FALSE(BibleSearchResidency::keepAfterExit(0, 0));
}

TEST(BibleSearchResidency, AnUnchangedIndexMatchesTheHeldHeader) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  ASSERT_EQ(held.fileSize, sizeOf(bytes));
  EXPECT_TRUE(BibleSearchResidency::heldIndexMatches(held, bytes.data(), bytes.size(), sizeOf(bytes)));
}

TEST(BibleSearchResidency, ChangingAnyHeaderByteBreaksTheMatch) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  // All 48 bytes, which covers each of the twelve serialised fields (the magic
  // and the eleven IndexHeader fields).
  for (size_t i = 0; i < BibleSearch::INDEX_HEADER_BYTES; i++) {
    Bytes changed = bytes;
    changed[i] ^= 0x01;
    EXPECT_FALSE(BibleSearchResidency::heldIndexMatches(held, changed.data(), changed.size(), sizeOf(changed)))
        << "header byte " << i;
  }
}

TEST(BibleSearchResidency, AShortHeaderReadDoesNotMatch) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  EXPECT_FALSE(
      BibleSearchResidency::heldIndexMatches(held, bytes.data(), BibleSearch::INDEX_HEADER_BYTES - 1, sizeOf(bytes)));
}

TEST(BibleSearchResidency, ADifferentFileSizeDoesNotMatch) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  EXPECT_FALSE(BibleSearchResidency::heldIndexMatches(held, bytes.data(), bytes.size(), sizeOf(bytes) + 1));
  EXPECT_FALSE(BibleSearchResidency::heldIndexMatches(held, bytes.data(), bytes.size(), sizeOf(bytes) - 1));
}
```

Replace the whole of `test/bible_search_index/CMakeLists.txt` with:

```cmake
# The realistic cases build from the scanner's verbatim NWT fixtures, so expat
# is compiled here with the firmware's flags, as in bible_search_scanner.
add_executable(BibleSearchIndexTest
  IndexTest.cpp
  ResidencyTest.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexFormat.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexBuilder.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexReader.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Query.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/VerseTextScanner.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(BibleSearchIndexTest PRIVATE
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/...>, as src/study includes it
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

target_compile_definitions(BibleSearchIndexTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  BIBLE_SEARCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../bible_search_scanner/fixtures"
)

target_link_libraries(BibleSearchIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleSearchIndexTest)
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build/test --target BibleSearchIndexTest -j`
Expected: a compile error on `ResidencyTest.cpp`, `'src/study/BibleSearchResidency.h' file not found`.

- [ ] **Step 3: Write the header**

Create `src/study/BibleSearchResidency.h`:

```cpp
#pragma once

#include <BibleSearch/IndexFormat.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// What the search store keeps between visits to the search screen, decided apart
// from Epub and the card so the host suite can test it.
namespace BibleSearchResidency {

// Keeping the ~444 KB term index is a bet that search opens again soon. Below
// these floors the room is worth more. They cover the catalog update check, the
// largest PSRAM user reachable while search is closed: it holds the current
// index while it loads and inflates a staged one, 2.5 MiB by CatalogIndexStore's
// budgets. Only the screen's exit checks them; nothing asks the store to let go
// when a later allocation fails.
inline constexpr size_t MIN_FREE_PSRAM = 3u * 1024u * 1024u;
inline constexpr size_t MIN_LARGEST_PSRAM_BLOCK = 1024u * 1024u;

constexpr bool keepAfterExit(const size_t freePsram, const size_t largestPsramBlock) {
  return freePsram >= MIN_FREE_PSRAM && largestPsramBlock >= MIN_LARGEST_PSRAM_BLOCK;
}

// A kept reader may serve again only while bible.idx still starts with the
// header it was opened on and still has its size. writeHeader leaves no byte
// unwritten, so comparing bytes compares every field.
inline bool heldIndexMatches(const BibleSearch::IndexHeader& held, const uint8_t* raw, const size_t rawLength,
                             const uint32_t fileSize) {
  if (rawLength < BibleSearch::INDEX_HEADER_BYTES || fileSize != held.fileSize) return false;
  uint8_t expected[BibleSearch::INDEX_HEADER_BYTES];
  BibleSearch::writeHeader(expected, held);
  return memcmp(expected, raw, sizeof(expected)) == 0;
}

}  // namespace BibleSearchResidency
```

- [ ] **Step 4: Run it and watch it pass**

Run:
```bash
cmake --build build/test --target BibleSearchIndexTest -j
./build/test/bible_search_index/BibleSearchIndexTest
```
Expected: `[  PASSED  ]` covering every test, the 7 `BibleSearchResidency.*` tests included, with
no `FAILED`.

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix -g
git add src/study/BibleSearchResidency.h test/bible_search_index/ResidencyTest.cpp test/bible_search_index/CMakeLists.txt
git commit -m "test: decide when the search store keeps its reader"
```

---

### Task 2: `BibleSearchFingerprintFile` — codec, hash and completeness

**Files:**
- Create: `src/study/BibleSearchFingerprintFile.h`
- Create: `src/study/BibleSearchFingerprintFile.cpp`
- Create: `test/bible_search_index/FingerprintFixtures.h`
- Create: `test/bible_search_index/FingerprintFileTest.cpp`
- Modify: `test/bible_search_index/CMakeLists.txt`

- [ ] **Step 1: Write the fixtures and the failing codec test**

Create `test/bible_search_index/FingerprintFixtures.h`:

```cpp
#pragma once

// Shared values for the fingerprint record's codec and I/O tests.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "src/study/BibleSearchFingerprintFile.h"

namespace fingerprint_fixtures {

inline BibleSearchFingerprintFile::Key key() {
  BibleSearchFingerprintFile::Key k;
  k.epubSize = 36000123;
  k.spineCount = 3937;
  k.unitIndexVersion = 1;
  return k;
}

inline BibleSearchFingerprintFile::Record record() {
  BibleSearchFingerprintFile::Record r;
  r.key = key();
  r.verseDocuments = 1189;
  r.fingerprint = 0xB063E8DC59554F2Dull;
  return r;
}

inline std::vector<uint8_t> encoded(const BibleSearchFingerprintFile::Record& r = record()) {
  uint8_t raw[BibleSearchFingerprintFile::RECORD_BYTES];
  BibleSearchFingerprintFile::encode(raw, r);
  return std::vector<uint8_t>(raw, raw + BibleSearchFingerprintFile::RECORD_BYTES);
}

inline void setVersion(std::vector<uint8_t>& bytes, const uint16_t version) {
  bytes[4] = static_cast<uint8_t>(version);
  bytes[5] = static_cast<uint8_t>(version >> 8);
}

// Recomputes the check over bytes 0-31, so a test can change a field and still
// reach the checks that follow the check comparison.
inline void reseal(std::vector<uint8_t>& bytes) {
  BibleSearchFingerprintFile::Fnv1a64 check;
  check.add(bytes.data(), 32);
  memcpy(bytes.data() + 32, &check.hash, sizeof(check.hash));
}

inline std::string asString(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

}  // namespace fingerprint_fixtures
```

Create `test/bible_search_index/FingerprintFileTest.cpp`:

```cpp
// The fingerprint record's codec: its layout, the order its checks run in, and
// the hash it shares with every bible.idx already on a card.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "FingerprintFixtures.h"

namespace {

namespace bsfp = BibleSearchFingerprintFile;
using bsfp::Load;
using fingerprint_fixtures::encoded;
using fingerprint_fixtures::key;
using fingerprint_fixtures::record;
using fingerprint_fixtures::reseal;
using fingerprint_fixtures::setVersion;

Load decodeAs(const std::vector<uint8_t>& bytes, const size_t length) {
  bsfp::Record out;
  return bsfp::decode(bytes.data(), length, key(), out);
}

Load decodeWithKey(const bsfp::Key& expected) {
  const std::vector<uint8_t> bytes = encoded();
  bsfp::Record out;
  return bsfp::decode(bytes.data(), bytes.size(), expected, out);
}

std::vector<uint8_t> everyBookTwice() {
  std::vector<uint8_t> books;
  for (int book = 1; book <= 66; book++) {
    books.push_back(static_cast<uint8_t>(book));
    books.push_back(static_cast<uint8_t>(book));
  }
  return books;
}

bool covers(const std::vector<uint8_t>& books) {
  return bsfp::coversEveryBook(books.data(), static_cast<uint32_t>(books.size()));
}

}  // namespace

TEST(BibleSearchFingerprintFile, RoundTrips) {
  const std::vector<uint8_t> bytes = encoded();
  bsfp::Record out;
  ASSERT_EQ(bsfp::decode(bytes.data(), bytes.size(), key(), out), Load::Ok);
  EXPECT_EQ(out.key.epubSize, record().key.epubSize);
  EXPECT_EQ(out.key.spineCount, record().key.spineCount);
  EXPECT_EQ(out.key.unitIndexVersion, record().key.unitIndexVersion);
  EXPECT_EQ(out.verseDocuments, record().verseDocuments);
  EXPECT_EQ(out.fingerprint, record().fingerprint);
}

TEST(BibleSearchFingerprintFile, MatchesTheVersionOneLayout) {
  const std::vector<uint8_t> golden = {
      0x42, 0x53, 0x46, 0x50,                          // magic "BSFP"
      0x01, 0x00,                                      // version 1
      0x01, 0x00,                                      // unitIndexVersion 1
      0x7B, 0x51, 0x25, 0x02, 0x00, 0x00, 0x00, 0x00,  // epubSize 36,000,123
      0x61, 0x0F, 0x00, 0x00,                          // spineCount 3,937
      0xA5, 0x04, 0x00, 0x00,                          // verseDocuments 1,189
      0x2D, 0x4F, 0x55, 0x59, 0xDC, 0xE8, 0x63, 0xB0,  // fingerprint
      0xD7, 0x52, 0x5D, 0x2E, 0x77, 0x73, 0x27, 0x0A,  // check: FNV-1a 64 over bytes 0-31
  };
  EXPECT_EQ(encoded(), golden);
}

TEST(BibleSearchFingerprintFile, ANewerVersionIsTooNewAtAnyLength) {
  std::vector<uint8_t> bytes = encoded();
  setVersion(bytes, static_cast<uint16_t>(bsfp::VERSION + 1));  // the check no longer matches either
  EXPECT_EQ(decodeAs(bytes, 6), Load::TooNew);
  EXPECT_EQ(decodeAs(bytes, 40), Load::TooNew);
  EXPECT_EQ(decodeAs(bytes, 48), Load::TooNew);
}

TEST(BibleSearchFingerprintFile, AForeignFileIsCorruptEvenWithAHighVersion) {
  std::vector<uint8_t> bytes = encoded();
  bytes[0] = 'X';
  setVersion(bytes, 0xFFFF);
  EXPECT_EQ(decodeAs(bytes, 40), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, TooShortToHoldAVersionIsCorrupt) {
  const std::vector<uint8_t> bytes = encoded();
  EXPECT_EQ(decodeAs(bytes, 0), Load::Corrupt);
  EXPECT_EQ(decodeAs(bytes, 5), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, TheCurrentVersionAtAnyOtherLengthIsCorrupt) {
  const std::vector<uint8_t> bytes = encoded();
  EXPECT_EQ(decodeAs(bytes, 39), Load::Corrupt);
  EXPECT_EQ(decodeAs(bytes, 41), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, AFlippedCheckOrPayloadByteIsCorrupt) {
  std::vector<uint8_t> checkFlipped = encoded();
  checkFlipped[32] ^= 0x01;
  EXPECT_EQ(decodeAs(checkFlipped, 40), Load::Corrupt);

  std::vector<uint8_t> payloadFlipped = encoded();
  payloadFlipped[24] ^= 0x01;
  EXPECT_EQ(decodeAs(payloadFlipped, 40), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, AnOlderVersionIsCorruptEvenWhenSealed) {
  std::vector<uint8_t> bytes = encoded();
  setVersion(bytes, static_cast<uint16_t>(bsfp::VERSION - 1));
  reseal(bytes);
  EXPECT_EQ(decodeAs(bytes, 40), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, AnotherBibleOrUnitsFormatIsStale) {
  bsfp::Key otherSize = key();
  otherSize.epubSize += 1;
  EXPECT_EQ(decodeWithKey(otherSize), Load::Stale);

  bsfp::Key otherSpine = key();
  otherSpine.spineCount += 1;
  EXPECT_EQ(decodeWithKey(otherSpine), Load::Stale);

  bsfp::Key otherUnits = key();
  otherUnits.unitIndexVersion += 1;
  EXPECT_EQ(decodeWithKey(otherUnits), Load::Stale);
}

TEST(BibleSearchFingerprintFile, OnlyANewerOrUnreadableRecordIsLeftInPlace) {
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Ok));
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Missing));
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Corrupt));
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Stale));
  EXPECT_FALSE(bsfp::mayOverwrite(Load::TooNew));
  EXPECT_FALSE(bsfp::mayOverwrite(Load::Unreadable));
}

TEST(BibleSearchFingerprintFile, TheHashIsFnv1a64) {
  bsfp::Fnv1a64 empty;
  EXPECT_EQ(empty.hash, 0xcbf29ce484222325ull);

  bsfp::Fnv1a64 a;
  a.add("a", 1);
  EXPECT_EQ(a.hash, 0xaf63dc4c8601ec8cull);
}

TEST(BibleSearchFingerprintFile, TheIntegerHelpersHashLittleEndianBytes) {
  bsfp::Fnv1a64 viaHelpers;
  viaHelpers.addU32(0x11223344u);
  viaHelpers.addU64(0x8877665544332211ull);

  bsfp::Fnv1a64 viaBytes;
  const uint8_t bytes[] = {0x44, 0x33, 0x22, 0x11, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  viaBytes.add(bytes, sizeof(bytes));

  EXPECT_EQ(viaHelpers.hash, viaBytes.hash);
}

TEST(BibleSearchFingerprintFile, CoversEveryBookOnlyWhenAllSixtySixResolved) {
  std::vector<uint8_t> books = everyBookTwice();
  EXPECT_TRUE(covers(books));

  std::reverse(books.begin(), books.end());
  EXPECT_TRUE(covers(books));

  books.push_back(0);
  books.push_back(67);
  EXPECT_TRUE(covers(books)) << "values outside 1-66 are ignored";

  books.erase(std::remove(books.begin(), books.end(), uint8_t{40}), books.end());
  EXPECT_FALSE(covers(books)) << "book 40 missing";

  EXPECT_FALSE(bsfp::coversEveryBook(nullptr, 0));
}
```

Append to `test/bible_search_index/CMakeLists.txt`, after the existing
`gtest_discover_tests(BibleSearchIndexTest)` line:

```cmake

# The fingerprint record's codec, and in later tasks its I/O against the
# in-memory card. test/stubs comes first so <HalStorage.h>, <Logging.h> and
# <Arduino.h> resolve to the stubs, as in test/storage_io.
add_executable(BibleSearchFingerprintTest
  FingerprintFileTest.cpp
  ${REPO_ROOT}/src/study/BibleSearchFingerprintFile.cpp
)

target_include_directories(BibleSearchFingerprintTest PRIVATE
  ${REPO_ROOT}/test/stubs
)

target_link_libraries(BibleSearchFingerprintTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleSearchFingerprintTest)
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build/test --target BibleSearchFingerprintTest -j`
Expected: CMake or the compiler reports that `src/study/BibleSearchFingerprintFile.cpp` or
`BibleSearchFingerprintFile.h` does not exist.

- [ ] **Step 3: Write the codec**

Create `src/study/BibleSearchFingerprintFile.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

// The Bible search fingerprint, kept beside the Bible's book.bin as
// <cachePath>/search-fingerprint.bin, so the first search after a boot reads 40
// bytes instead of re-resolving every verse document. docs/file-formats.md
// documents the layout.
//
// Derived data, and it lives in the Bible's cache directory on purpose: every
// on-device delete or replace of the Bible removes that directory, and the
// record with it.
namespace BibleSearchFingerprintFile {

inline constexpr uint32_t MAGIC = 0x50465342;  // "BSFP" little-endian
// Bump when what the fingerprint covers, how verse documents are resolved, or
// (through a BOOK_CACHE_VERSION change) the spine hrefs it hashes, changes. An
// in-place book.bin rebuild leaves this record behind.
inline constexpr uint16_t VERSION = 1;
inline constexpr size_t RECORD_BYTES = 40;
inline constexpr const char* FILE_NAME = "search-fingerprint.bin";
inline constexpr uint8_t BIBLE_BOOKS = 66;

// FNV-1a 64. Every bible.idx on a card is keyed by it, so its output must never
// change: the host suite pins it to the published vectors.
struct Fnv1a64 {
  uint64_t hash = 14695981039346656037ull;

  void add(const void* data, size_t length);
  void addU32(uint32_t value);
  void addU64(uint64_t value);
};

struct Key {
  uint64_t epubSize = 0;
  uint32_t spineCount = 0;
  uint16_t unitIndexVersion = 0;
};

struct Record {
  Key key;
  uint32_t verseDocuments = 0;
  uint64_t fingerprint = 0;
};

enum class Load { Ok, Missing, Unreadable, Corrupt, TooNew, Stale };

void encode(uint8_t (&out)[RECORD_BYTES], const Record& record);
// `length` is the file's real size; `in` holds min(length, RECORD_BYTES) bytes.
// The magic is checked before the version and the version before the length, so
// a newer record of any length is TooNew and a foreign file never is.
Load decode(const uint8_t* in, size_t length, const Key& expected, Record& out);

// Whether a record that loaded this way may be replaced. A newer one belongs to
// a later build; one that could not be read may be fine on the next try.
bool mayOverwrite(Load result);

// True when every book 1-66 contributed at least one verse document. Only such a
// resolve is persisted: a book lost to a transient read failure would otherwise
// outlive the boot it happened in.
bool coversEveryBook(const uint8_t* books, uint32_t count);

}  // namespace BibleSearchFingerprintFile
```

Create `src/study/BibleSearchFingerprintFile.cpp`:

```cpp
#include "BibleSearchFingerprintFile.h"

#include <cstring>

namespace BibleSearchFingerprintFile {

namespace {

constexpr size_t MAGIC_AND_VERSION_BYTES = 6;
constexpr size_t CHECKED_BYTES = 32;

void put16(uint8_t* out, const uint16_t v) { memcpy(out, &v, sizeof(v)); }
void put32(uint8_t* out, const uint32_t v) { memcpy(out, &v, sizeof(v)); }
void put64(uint8_t* out, const uint64_t v) { memcpy(out, &v, sizeof(v)); }

uint16_t get16(const uint8_t* in) {
  uint16_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint32_t get32(const uint8_t* in) {
  uint32_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint64_t get64(const uint8_t* in) {
  uint64_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint64_t checkOf(const uint8_t* record) {
  Fnv1a64 check;
  check.add(record, CHECKED_BYTES);
  return check.hash;
}

}  // namespace

void Fnv1a64::add(const void* data, const size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
}

void Fnv1a64::addU32(const uint32_t value) {
  const uint8_t le[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                         static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  add(le, sizeof(le));
}

void Fnv1a64::addU64(const uint64_t value) {
  addU32(static_cast<uint32_t>(value));
  addU32(static_cast<uint32_t>(value >> 32));
}

void encode(uint8_t (&out)[RECORD_BYTES], const Record& record) {
  memset(out, 0, RECORD_BYTES);
  put32(out, MAGIC);
  put16(out + 4, VERSION);
  put16(out + 6, record.key.unitIndexVersion);
  put64(out + 8, record.key.epubSize);
  put32(out + 16, record.key.spineCount);
  put32(out + 20, record.verseDocuments);
  put64(out + 24, record.fingerprint);
  put64(out + 32, checkOf(out));
}

Load decode(const uint8_t* in, const size_t length, const Key& expected, Record& out) {
  if (length < MAGIC_AND_VERSION_BYTES || get32(in) != MAGIC) return Load::Corrupt;
  const uint16_t version = get16(in + 4);
  if (version > VERSION) return Load::TooNew;
  if (version < VERSION || length != RECORD_BYTES) return Load::Corrupt;
  if (get64(in + 32) != checkOf(in)) return Load::Corrupt;

  Record record;
  record.key.unitIndexVersion = get16(in + 6);
  record.key.epubSize = get64(in + 8);
  record.key.spineCount = get32(in + 16);
  record.verseDocuments = get32(in + 20);
  record.fingerprint = get64(in + 24);
  if (record.key.epubSize != expected.epubSize || record.key.spineCount != expected.spineCount ||
      record.key.unitIndexVersion != expected.unitIndexVersion) {
    return Load::Stale;
  }
  out = record;
  return Load::Ok;
}

bool mayOverwrite(const Load result) { return result != Load::TooNew && result != Load::Unreadable; }

bool coversEveryBook(const uint8_t* books, const uint32_t count) {
  bool seen[BIBLE_BOOKS + 1] = {};
  for (uint32_t i = 0; i < count; i++) {
    if (books[i] >= 1 && books[i] <= BIBLE_BOOKS) seen[books[i]] = true;
  }
  for (uint8_t book = 1; book <= BIBLE_BOOKS; book++) {
    if (!seen[book]) return false;
  }
  return true;
}

}  // namespace BibleSearchFingerprintFile
```

- [ ] **Step 4: Run it and watch it pass**

Run:
```bash
cmake --build build/test --target BibleSearchFingerprintTest -j
./build/test/bible_search_index/BibleSearchFingerprintTest
```
Expected: `[  PASSED  ] 13 tests.`

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix -g
git add src/study/BibleSearchFingerprintFile.h src/study/BibleSearchFingerprintFile.cpp test/bible_search_index/FingerprintFixtures.h test/bible_search_index/FingerprintFileTest.cpp test/bible_search_index/CMakeLists.txt
git commit -m "feat: add the Bible search fingerprint record codec"
```

---

### Task 3: `BibleSearchFingerprintFile` — load and save on the card

**Files:**
- Modify: `src/study/BibleSearchFingerprintFile.h`
- Modify: `src/study/BibleSearchFingerprintFile.cpp`
- Create: `test/bible_search_index/FingerprintFileIoTest.cpp`
- Modify: `test/bible_search_index/CMakeLists.txt`

- [ ] **Step 1: Write the failing I/O test**

Create `test/bible_search_index/FingerprintFileIoTest.cpp`:

```cpp
// The fingerprint record against the in-memory card: one read to load it, and a
// temp-then-rename save that never leaves a torn record.

#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FingerprintFixtures.h"
#include "HalStorageFake.h"

namespace {

namespace bsfp = BibleSearchFingerprintFile;
using bsfp::Load;
using fingerprint_fixtures::asString;
using fingerprint_fixtures::encoded;
using fingerprint_fixtures::key;
using fingerprint_fixtures::record;
using fingerprint_fixtures::setVersion;

const std::string CACHE = "/.crosspoint/epub_1234";
const std::string PATH = CACHE + "/search-fingerprint.bin";
const std::string TMP_PATH = PATH + ".tmp";

bsfp::Record otherRecord() {
  bsfp::Record r = record();
  r.fingerprint = 0x0102030405060708ull;
  return r;
}

class FingerprintFileIo : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_fake::reset();
    Storage.mkdir(CACHE.c_str());
  }

  static Load loadNow() {
    bsfp::Record out;
    return bsfp::load(CACHE, key(), out);
  }
};

}  // namespace

TEST_F(FingerprintFileIo, ThePathIsBesideBookBin) { EXPECT_EQ(bsfp::pathIn(CACHE), PATH); }

TEST_F(FingerprintFileIo, SaveLeavesExactlyTheRecordAndNoTemp) {
  ASSERT_TRUE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), asString(encoded()));
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(FingerprintFileIo, SavedThenLoadedRoundTrips) {
  ASSERT_TRUE(bsfp::save(CACHE, record()));
  bsfp::Record out;
  ASSERT_EQ(bsfp::load(CACHE, key(), out), Load::Ok);
  EXPECT_EQ(out.fingerprint, record().fingerprint);
  EXPECT_EQ(out.verseDocuments, record().verseDocuments);
}

TEST_F(FingerprintFileIo, SaveReplacesAnEarlierRecord) {
  storage_fake::putFile(PATH, asString(encoded(otherRecord())));
  ASSERT_TRUE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), asString(encoded()));
}

TEST_F(FingerprintFileIo, AFailedTempWriteKeepsThePreviousRecord) {
  const std::string previous = asString(encoded(otherRecord()));
  storage_fake::putFile(PATH, previous);
  storage_fake::failWritesTo(TMP_PATH);
  EXPECT_FALSE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), previous);
}

TEST_F(FingerprintFileIo, AShortTempWriteKeepsThePreviousRecordAndLeavesNoTemp) {
  const std::string previous = asString(encoded(otherRecord()));
  storage_fake::putFile(PATH, previous);
  storage_fake::failWritesAfter(TMP_PATH, 20);
  EXPECT_FALSE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), previous);
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(FingerprintFileIo, AFailedRenameReportsFailure) {
  storage_fake::failRenamesFrom(TMP_PATH);
  EXPECT_FALSE(bsfp::save(CACHE, record()));
}

TEST_F(FingerprintFileIo, AnAbsentRecordIsMissing) { EXPECT_EQ(loadNow(), Load::Missing); }

TEST_F(FingerprintFileIo, ARecordThatCannotBeOpenedIsUnreadable) {
  storage_fake::putFile(PATH, asString(encoded()));
  storage_fake::failReadsOf(PATH);
  EXPECT_EQ(loadNow(), Load::Unreadable);
}

TEST_F(FingerprintFileIo, ATruncatedRecordIsCorrupt) {
  storage_fake::putFile(PATH, asString(encoded()).substr(0, 20));
  EXPECT_EQ(loadNow(), Load::Corrupt);
}

TEST_F(FingerprintFileIo, ARecordWithATrailingByteIsCorrupt) {
  storage_fake::putFile(PATH, asString(encoded()) + "x");
  EXPECT_EQ(loadNow(), Load::Corrupt);
}

TEST_F(FingerprintFileIo, ANewerLongerRecordIsTooNew) {
  std::vector<uint8_t> bytes = encoded();
  setVersion(bytes, static_cast<uint16_t>(bsfp::VERSION + 1));
  bytes.resize(48, 0);
  storage_fake::putFile(PATH, asString(bytes));
  EXPECT_EQ(loadNow(), Load::TooNew);
}

TEST_F(FingerprintFileIo, LoadingIsOneRead) {
  storage_fake::putFile(PATH, asString(encoded()));
  ASSERT_EQ(loadNow(), Load::Ok);
  EXPECT_EQ(storage_fake::readCallsOn(PATH), 1u);
}
```

In `test/bible_search_index/CMakeLists.txt`, replace:

```cmake
add_executable(BibleSearchFingerprintTest
  FingerprintFileTest.cpp
  ${REPO_ROOT}/src/study/BibleSearchFingerprintFile.cpp
)
```

with:

```cmake
add_executable(BibleSearchFingerprintTest
  FingerprintFileTest.cpp
  FingerprintFileIoTest.cpp
  ${REPO_ROOT}/src/study/BibleSearchFingerprintFile.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
)
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build/test --target BibleSearchFingerprintTest -j`
Expected: compile errors in `FingerprintFileIoTest.cpp`: `no member named 'load'`, `'save'` and
`'pathIn'` in namespace `BibleSearchFingerprintFile`.

- [ ] **Step 3: Add load and save**

In `src/study/BibleSearchFingerprintFile.h`, replace:

```cpp
#include <cstddef>
#include <cstdint>
```

with:

```cpp
#include <cstddef>
#include <cstdint>
#include <string>
```

Then, in the same file, replace:

```cpp
// Whether a record that loaded this way may be replaced. A newer one belongs to
```

with:

```cpp
std::string pathIn(const std::string& cachePath);
// One read of min(file size, RECORD_BYTES) bytes. Missing when there is no file,
// Unreadable when it exists but cannot be opened or read.
Load load(const std::string& cachePath, const Key& expected, Record& out);
// Writes <record>.tmp, removes the old record, renames the temp into place.
// False on any failure; a failed temp write leaves the old record as it was.
bool save(const std::string& cachePath, const Record& record);

// Whether a record that loaded this way may be replaced. A newer one belongs to
```

In `src/study/BibleSearchFingerprintFile.cpp`, replace:

```cpp
#include "BibleSearchFingerprintFile.h"

#include <cstring>

namespace BibleSearchFingerprintFile {

namespace {

constexpr size_t MAGIC_AND_VERSION_BYTES = 6;
```

with:

```cpp
#include "BibleSearchFingerprintFile.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace BibleSearchFingerprintFile {

namespace {

constexpr const char* MODULE = "BSFP";
constexpr size_t MAGIC_AND_VERSION_BYTES = 6;
```

Then, in the same file, replace:

```cpp
bool mayOverwrite(const Load result) { return result != Load::TooNew && result != Load::Unreadable; }
```

with:

```cpp
std::string pathIn(const std::string& cachePath) { return cachePath + "/" + FILE_NAME; }

Load load(const std::string& cachePath, const Key& expected, Record& out) {
  const std::string path = pathIn(cachePath);
  if (!Storage.exists(path.c_str())) return Load::Missing;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return Load::Unreadable;

  // The real size, not the bytes read, goes to decode: a record one byte too
  // long must not pass as a whole one.
  const size_t length = file.fileSize();
  const size_t wanted = std::min(length, RECORD_BYTES);
  uint8_t raw[RECORD_BYTES] = {};
  if (wanted > 0) {
    const int got = file.read(raw, wanted);
    if (got < 0) return Load::Unreadable;
    if (static_cast<size_t>(got) != wanted) return Load::Corrupt;
  }
  return decode(raw, length, expected, out);
}

bool save(const std::string& cachePath, const Record& record) {
  const std::string path = pathIn(cachePath);
  const std::string tmpPath = path + ".tmp";
  uint8_t raw[RECORD_BYTES];
  encode(raw, record);
  {
    HalFile file;
    if (!Storage.openFileForWrite(MODULE, tmpPath, file)) {
      LOG_ERR(MODULE, "Cannot create %s", tmpPath.c_str());
      return false;
    }
    const size_t written = file.write(raw, RECORD_BYTES);
    file.flush();
    const size_t onCard = file.fileSize();
    if (written != RECORD_BYTES || onCard != RECORD_BYTES) {
      file.close();
      Storage.remove(tmpPath.c_str());
      LOG_ERR(MODULE, "Short write to %s: %u of %u bytes", tmpPath.c_str(), static_cast<unsigned>(onCard),
              static_cast<unsigned>(RECORD_BYTES));
      return false;
    }
  }

  // The card has no rename-over, so the old record goes first. A crash between
  // the two reads as "no record": one recompute, never a torn record.
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    LOG_ERR(MODULE, "Could not remove %s", path.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR(MODULE, "Rename %s -> %s failed", tmpPath.c_str(), path.c_str());
    return false;
  }
  return true;
}

bool mayOverwrite(const Load result) { return result != Load::TooNew && result != Load::Unreadable; }
```

- [ ] **Step 4: Run it and watch it pass**

Run:
```bash
cmake --build build/test --target BibleSearchFingerprintTest -j
./build/test/bible_search_index/BibleSearchFingerprintTest
```
Expected: `[  PASSED  ] 26 tests.` (13 codec + 13 I/O).

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix -g
git add src/study/BibleSearchFingerprintFile.h src/study/BibleSearchFingerprintFile.cpp test/bible_search_index/FingerprintFileIoTest.cpp test/bible_search_index/CMakeLists.txt
git commit -m "feat: load and save the Bible search fingerprint record"
```

---

### Task 4: Store — Bible identity, fingerprint source and the one record writer

The store is not host-compiled (it needs `Epub` and `STUDY`), so this task is verified by the
firmware build. The behaviour of `open()` is unchanged here: it still closes first. Only where
the fingerprint comes from changes: memory, then the record, then a resolve.

**Files:**
- Modify (full replacement): `src/study/BibleSearchStore.h`
- Modify (full replacement): `src/study/BibleSearchStore.cpp`

- [ ] **Step 1: Replace `src/study/BibleSearchStore.h`**

Re-read the file first. Then replace its entire contents with:

```cpp
#pragma once

#include <BibleSearch/Allocator.h>
#include <BibleSearch/IndexFormat.h>
#include <BibleSearch/IndexReader.h>
#include <BibleSearch/VerseTextScanner.h>
#include <Epub.h>
#include <HalStorage.h>
#include <SdPaths.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "study/BibleSearchFingerprintFile.h"

// The Bible search index on the card: /.berean/search/bible.idx, and the build
// checkpoint bible.partial beside it. Both are derived data -- losing them costs
// a rebuild, never anything the user made.
//
// This class only reads the index. BibleSearchIndexer writes it, and only after
// the user confirms a build, so an unreadable or newer-format index stays on the
// card until they do.
//
// The fingerprint that ties an index to its Bible is kept in memory for the Bible
// on the card -- its path, size and spine count -- and in search-fingerprint.bin
// in that Bible's cache directory, so a boot does not re-resolve every verse
// document to find it.
//
// Main task only. Every SD access goes through HalStorage, which holds
// storageMutex per call; the web server task is a second writer to the card.
// Nothing here may be reached from render(): a read would put storageMutex on
// the render path.
class BibleSearchStore {
 public:
  using Status = BibleSearch::IndexReader::Status;

  static constexpr const char* SEARCH_DIR = sdpaths::SEARCH_DIR;
  static constexpr const char* INDEX_PATH = sdpaths::SEARCH_INDEX_FILE;
  static constexpr const char* CHECKPOINT_PATH = sdpaths::SEARCH_CHECKPOINT_FILE;
  // The NWT's index is ~1.47 MB. The budget bounds a runaway build, not the
  // 50,000-byte readFile cap: this file is only ever read by offset.
  static constexpr size_t INDEX_BYTE_BUDGET = 8u * 1024u * 1024u;

  // The verse-bearing spine documents in canonical order -- book 1 to 66, each
  // book's documents in spine order -- with the book of each. The fingerprint
  // covers this list, so a checkpoint's docsDone indexes the same list it was
  // written against.
  struct Documents {
    uint64_t fingerprint = 0;
    uint32_t count = 0;
    const uint16_t* spines = nullptr;
    const uint8_t* books = nullptr;
  };

  static BibleSearchStore& getInstance();

  // Where the build's and the reader's large blocks come from. PSRAM only: a
  // board without it gets a clean OOM rather than internal SRAM starved of the
  // megabytes a build holds.
  static BibleSearch::BuildAllocator psramAllocator();

  // Resolves the Bible's verse documents and fingerprint for real, once per Bible
  // (path, size and spine count): the book map plus one spine-entry read per verse
  // document. Never served from the fingerprint record. Null when `epub` is not
  // the Bible that StudyStore has open, or cannot be sized. A call for a different
  // Bible frees the previous list, so a caller that holds on to one copies it.
  const Documents* documents(const std::shared_ptr<Epub>& epub);

  // Reads the index header only, and never writes the index. An index that is not
  // Ok -- missing, stale, unreadable or newer -- reports Incomplete when a
  // checkpoint for this Bible exists, so the prompt can say the build will resume.
  Status status(const std::shared_ptr<Epub>& epub);

  // A reader with the term index cached, kept open until close(). Null unless
  // the index is Ok; `statusOut` then says why.
  const BibleSearch::IndexReader* open(const std::shared_ptr<Epub>& epub, Status* statusOut = nullptr);
  // Frees the reader, its term cache and its file handle.
  void close();

  // Streams one spine document through a VerseTextScanner. `malformed` tells a
  // document the scanner rejected from one that could not be read; running out
  // of memory is neither, and scanner.outOfMemory() reports it.
  static bool scanSpine(const Epub& epub, uint16_t spine, BibleSearch::VerseTextScanner& scanner, bool& malformed);

  using VerseTextSink = void (*)(void* ctx, size_t wantedIndex, std::string_view text);

  // Streams `spine` once and hands `sink` the visible text of each wanted
  // verse, matched by chapter:verse. A verse not found in the document is not
  // reported. False when the document cannot be read.
  static bool verseTexts(const Epub& epub, uint16_t spine, const BibleSearch::VerseEntry* wanted, size_t count,
                         VerseTextSink sink, void* ctx);

  // A ByteSource over an open HalFile, for the reader and the checkpoint load.
  static BibleSearch::ByteSource byteSourceFor(HalFile& file);

 private:
  struct Identity {
    std::string path;
    uint64_t epubSize = 0;
    uint32_t spineCount = 0;
  };

  BibleSearchStore() = default;

  // Sizes `epub` and makes it the Bible everything here is kept for, dropping
  // whatever was kept for another. False, with nothing kept, when it cannot be
  // sized.
  bool identify(const std::shared_ptr<Epub>& epub);
  void forgetBible();
  // From memory, else from the record, else by resolving the documents.
  bool fingerprintFor(const std::shared_ptr<Epub>& epub, uint64_t& out);
  bool resolveDocuments(const std::shared_ptr<Epub>& epub);
  BibleSearchFingerprintFile::Key recordKey() const;
  void loadRecord(const std::shared_ptr<Epub>& epub);
  // The only writer of the record. Called after a resolve that covered every
  // book; leaves a record that is newer or could not be read where it is.
  void persistFingerprint(const std::shared_ptr<Epub>& epub);
  Status openIndexFile(uint64_t fingerprint);
  size_t termIndexBytes() const;

  Identity identity_;
  bool identified_ = false;
  uint64_t fingerprint_ = 0;
  bool fingerprintKnown_ = false;
  // The record as last loaded for this Bible; reset whenever the Bible changes.
  BibleSearchFingerprintFile::Load recordLoad_ = BibleSearchFingerprintFile::Load::Missing;
  bool recordLoaded_ = false;
  uint64_t recordFingerprint_ = 0;

  bool documentsReady_ = false;
  Documents documents_;
  BibleSearch::AllocatedBuffer documentsBuffer_;

  HalFile indexFile_;
  std::unique_ptr<BibleSearch::IndexReader> reader_;
};
```

- [ ] **Step 2: Replace `src/study/BibleSearchStore.cpp`**

Re-read the file first. Then replace its entire contents with:

```cpp
#include "BibleSearchStore.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "StudyStore/UnitIndexFormat.h"
#include "study/StudyStore.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr const char* MODULE = "BSEARCH";
constexpr size_t STREAM_CHUNK_BYTES = 4096;
// A resume replays a checkpoint through tens of thousands of small reads. The
// loop task is not subscribed to the task watchdog today, so these resets only
// matter if it ever is; the real cost of the long passes is a stalled UI.
constexpr uint32_t READS_PER_WATCHDOG_RESET = 256;
constexpr uint32_t SPINE_READS_PER_WATCHDOG_RESET = 64;

void* psramAllocate(const size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

void psramRelease(void* block) { heap_caps_free(block); }

bool readFileAt(void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
  static uint32_t readsSinceReset = 0;
  if (++readsSinceReset >= READS_PER_WATCHDOG_RESET) {
    readsSinceReset = 0;
    resetTaskWatchdogIfSubscribed();
  }
  auto* file = static_cast<HalFile*>(ctx);
  if (!file->seek(offset)) return false;
  return file->read(dst, len) == static_cast<int>(len);
}

class ScannerPrint final : public Print {
 public:
  explicit ScannerPrint(BibleSearch::VerseTextScanner& scanner) : scanner_(scanner) {}

  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    if (rejected_) return 0;
    if (!scanner_.feed(reinterpret_cast<const char*>(buffer), size, false)) {
      rejected_ = true;
      return 0;
    }
    return size;
  }
  bool rejected() const { return rejected_; }

 private:
  BibleSearch::VerseTextScanner& scanner_;
  bool rejected_ = false;
};

// False when the file cannot be opened or is empty: a size of 0 would still
// fingerprint, as a Bible that is not there.
bool fileSizeOf(const std::string& path, uint64_t& size) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;
  size = file.fileSize64();
  return size > 0;
}

}  // namespace

BibleSearchStore& BibleSearchStore::getInstance() {
  static BibleSearchStore instance;
  return instance;
}

BibleSearch::BuildAllocator BibleSearchStore::psramAllocator() { return {psramAllocate, psramRelease}; }

BibleSearch::ByteSource BibleSearchStore::byteSourceFor(HalFile& file) {
  BibleSearch::ByteSource source;
  source.ctx = &file;
  source.readAt = readFileAt;
  source.size = static_cast<uint32_t>(file.fileSize());
  return source;
}

bool BibleSearchStore::identify(const std::shared_ptr<Epub>& epub) {
  if (!epub) return false;
  uint64_t epubSize = 0;
  if (!fileSizeOf(epub->getPath(), epubSize)) {
    LOG_ERR(MODULE, "Cannot size %s", epub->getPath().c_str());
    forgetBible();
    return false;
  }
  const auto spineCount = static_cast<uint32_t>(std::max(epub->getSpineItemsCount(), 0));
  if (identified_ && identity_.path == epub->getPath() && identity_.epubSize == epubSize &&
      identity_.spineCount == spineCount) {
    return true;
  }
  if (identified_) LOG_INF(MODULE, "Another Bible on the card; dropping what search kept");
  forgetBible();
  identity_ = Identity{epub->getPath(), epubSize, spineCount};
  identified_ = true;
  return true;
}

void BibleSearchStore::forgetBible() {
  close();
  identified_ = false;
  identity_ = Identity{};
  fingerprint_ = 0;
  fingerprintKnown_ = false;
  recordLoad_ = BibleSearchFingerprintFile::Load::Missing;
  recordLoaded_ = false;
  recordFingerprint_ = 0;
  documentsReady_ = false;
  documents_ = Documents{};
  documentsBuffer_.reset();
}

BibleSearchFingerprintFile::Key BibleSearchStore::recordKey() const {
  BibleSearchFingerprintFile::Key key;
  key.epubSize = identity_.epubSize;
  key.spineCount = identity_.spineCount;
  key.unitIndexVersion = study::UNIT_INDEX_VERSION;
  return key;
}

void BibleSearchStore::loadRecord(const std::shared_ptr<Epub>& epub) {
  BibleSearchFingerprintFile::Record record;
  recordLoad_ = BibleSearchFingerprintFile::load(epub->getCachePath(), recordKey(), record);
  recordLoaded_ = true;
  recordFingerprint_ = recordLoad_ == BibleSearchFingerprintFile::Load::Ok ? record.fingerprint : 0;
}

bool BibleSearchStore::fingerprintFor(const std::shared_ptr<Epub>& epub, uint64_t& out) {
  if (!identify(epub)) return false;
  if (fingerprintKnown_) {
    LOG_DBG(MODULE, "Fingerprint from memory");
    out = fingerprint_;
    return true;
  }

  [[maybe_unused]] const unsigned long started = millis();
  if (!recordLoaded_) loadRecord(epub);
  if (recordLoad_ == BibleSearchFingerprintFile::Load::Ok) {
    fingerprint_ = recordFingerprint_;
    fingerprintKnown_ = true;
    out = fingerprint_;
    LOG_DBG(MODULE, "Fingerprint from record in %lu ms", millis() - started);
    return true;
  }
  if (recordLoad_ != BibleSearchFingerprintFile::Load::Missing) {
    LOG_INF(MODULE, "Fingerprint record not used (load %d)", static_cast<int>(recordLoad_));
  }

  if (!resolveDocuments(epub)) return false;
  out = fingerprint_;
  LOG_DBG(MODULE, "Fingerprint computed in %lu ms", millis() - started);
  return true;
}

const BibleSearchStore::Documents* BibleSearchStore::documents(const std::shared_ptr<Epub>& epub) {
  if (!identify(epub)) return nullptr;
  if (documentsReady_) return &documents_;
  if (!resolveDocuments(epub)) return nullptr;
  return &documents_;
}

bool BibleSearchStore::resolveDocuments(const std::shared_ptr<Epub>& epub) {
  documentsReady_ = false;
  documents_ = Documents{};
  documentsBuffer_.reset();

  if (epub->getBibleBookNavSpineIndex() < 0) {
    LOG_ERR(MODULE, "%s is not an NWT-shaped Bible", epub->getPath().c_str());
    return false;
  }
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount <= 0 || spineCount > UINT16_MAX) {
    LOG_ERR(MODULE, "Spine count %d out of range", spineCount);
    return false;
  }

  // Collected at spine-count size, then kept at the size actually used: the
  // NWT has 3,937 spine items and 1,189 verse documents.
  const size_t capacity = static_cast<size_t>(spineCount);
  BibleSearch::AllocatedBuffer scratch(psramAllocator(), capacity * (sizeof(uint16_t) + sizeof(uint8_t)));
  if (!scratch.get()) {
    LOG_ERR(MODULE, "OOM: document list for %d spine items", spineCount);
    return false;
  }
  auto* scratchSpines = scratch.as<uint16_t>();
  auto* scratchBooks = reinterpret_cast<uint8_t*>(scratchSpines + capacity);

  uint32_t count = 0;
  for (uint8_t book = 1; book <= BibleSearchFingerprintFile::BIBLE_BOOKS; book++) {
    const uint32_t before = count;
    for (const uint16_t spine : STUDY.spineIndicesForBook(book)) {
      if (count >= capacity) break;
      scratchSpines[count] = spine;
      scratchBooks[count] = book;
      count++;
    }
    // A book the map cannot resolve would silently be left out of the index.
    if (count == before) LOG_ERR(MODULE, "Book %u resolves to no verse documents", book);
    resetTaskWatchdogIfSubscribed();
  }
  if (count == 0) {
    LOG_ERR(MODULE, "No verse documents: the Bible's book map is unavailable");
    return false;
  }

  // Spines (u16) first, then books (u8): the block's alignment covers the u16s.
  if (!documentsBuffer_.allocate(psramAllocator(), count * (sizeof(uint16_t) + sizeof(uint8_t)))) {
    LOG_ERR(MODULE, "OOM: document list for %u verse documents", static_cast<unsigned>(count));
    return false;
  }
  auto* spines = documentsBuffer_.as<uint16_t>();
  auto* books = reinterpret_cast<uint8_t*>(spines + count);
  memcpy(spines, scratchSpines, count * sizeof(uint16_t));
  memcpy(books, scratchBooks, count);
  scratch.reset();

  // Device-written FAT timestamps are constant, so there is no mtime to hash.
  // The spine hrefs catch a replaced Bible whose file size happens to match.
  BibleSearchFingerprintFile::Fnv1a64 fingerprint;
  fingerprint.addU64(identity_.epubSize);
  fingerprint.addU32(static_cast<uint32_t>(spineCount));
  for (uint32_t i = 0; i < count; i++) {
    const std::string href = epub->getSpineItem(spines[i]).href;
    if (href.empty()) {
      LOG_ERR(MODULE, "Cannot read spine entry %u", spines[i]);
      documentsBuffer_.reset();
      return false;
    }
    fingerprint.add(href.data(), href.size() + 1);
    if ((i + 1) % SPINE_READS_PER_WATCHDOG_RESET == 0) resetTaskWatchdogIfSubscribed();
  }
  fingerprint.addU32(count);

  documents_.fingerprint = fingerprint.hash;
  documents_.count = count;
  documents_.spines = spines;
  documents_.books = books;
  documentsReady_ = true;
  fingerprint_ = fingerprint.hash;
  fingerprintKnown_ = true;
  LOG_INF(MODULE, "%u verse documents, fingerprint %08lx%08lx", static_cast<unsigned>(count),
          static_cast<unsigned long>(documents_.fingerprint >> 32), static_cast<unsigned long>(documents_.fingerprint));

  if (BibleSearchFingerprintFile::coversEveryBook(books, count)) {
    persistFingerprint(epub);
  } else {
    LOG_ERR(MODULE, "Not every book resolved; the fingerprint is kept for this boot only");
  }
  return true;
}

void BibleSearchStore::persistFingerprint(const std::shared_ptr<Epub>& epub) {
  if (!recordLoaded_) loadRecord(epub);
  if (recordLoad_ == BibleSearchFingerprintFile::Load::Ok && recordFingerprint_ == fingerprint_) return;
  if (!BibleSearchFingerprintFile::mayOverwrite(recordLoad_)) {
    LOG_INF(MODULE, "Fingerprint record left in place (load %d)", static_cast<int>(recordLoad_));
    return;
  }

  BibleSearchFingerprintFile::Record record;
  record.key = recordKey();
  record.verseDocuments = documents_.count;
  record.fingerprint = fingerprint_;
  if (!BibleSearchFingerprintFile::save(epub->getCachePath(), record)) {
    LOG_ERR(MODULE, "Fingerprint record not saved; the next boot recomputes it");
    return;
  }
  recordLoad_ = BibleSearchFingerprintFile::Load::Ok;
  recordFingerprint_ = fingerprint_;
}

BibleSearchStore::Status BibleSearchStore::status(const std::shared_ptr<Epub>& epub) {
  uint64_t fingerprint = 0;
  if (!fingerprintFor(epub, fingerprint)) return Status::Unreadable;

  BibleSearch::IndexReader reader(psramAllocator());
  Status indexStatus = Status::Missing;
  if (Storage.exists(INDEX_PATH)) {
    HalFile file;
    indexStatus = Storage.openFileForRead(MODULE, INDEX_PATH, file) ? reader.open(byteSourceFor(file), fingerprint)
                                                                     : Status::Unreadable;
  }
  if (indexStatus == Status::Ok) return indexStatus;

  // Whatever is wrong with bible.idx, a build confirmed now resumes from a
  // matching checkpoint, so that is what the prompt should say.
  if (Storage.exists(CHECKPOINT_PATH)) {
    HalFile file;
    if (Storage.openFileForRead(MODULE, CHECKPOINT_PATH, file) &&
        reader.open(byteSourceFor(file), fingerprint) == Status::Incomplete) {
      return Status::Incomplete;
    }
  }
  return indexStatus;
}

BibleSearchStore::Status BibleSearchStore::openIndexFile(const uint64_t fingerprint) {
  if (!Storage.exists(INDEX_PATH)) return Status::Missing;
  reader_ = makeUniqueNoThrow<BibleSearch::IndexReader>(psramAllocator());
  if (!reader_) {
    LOG_ERR(MODULE, "OOM: index reader");
    return Status::Unreadable;
  }
  if (!Storage.openFileForRead(MODULE, INDEX_PATH, indexFile_)) return Status::Unreadable;
  return reader_->open(byteSourceFor(indexFile_), fingerprint);
}

size_t BibleSearchStore::termIndexBytes() const {
  if (!reader_ || !reader_->termIndexCached()) return 0;
  return reader_->header().postingsOffset - reader_->header().termTableOffset;
}

const BibleSearch::IndexReader* BibleSearchStore::open(const std::shared_ptr<Epub>& epub, Status* statusOut) {
  close();
  uint64_t fingerprint = 0;
  const bool identified = fingerprintFor(epub, fingerprint);
  [[maybe_unused]] const unsigned long started = millis();
  const Status result = identified ? openIndexFile(fingerprint) : Status::Unreadable;
  if (statusOut) *statusOut = result;

  if (result != Status::Ok) {
    LOG_ERR(MODULE, "Index not usable (status %d)", static_cast<int>(result));
    close();
    return nullptr;
  }
  if (!reader_->cacheTermIndex()) {
    // Still correct, only slower: lookups then read the term table from the card.
    LOG_ERR(MODULE, "Term index not cached; lookups will read the card");
  }
  LOG_DBG(MODULE, "Index opened in %lu ms (term index %u bytes)", millis() - started,
          static_cast<unsigned>(termIndexBytes()));
  return reader_.get();
}

void BibleSearchStore::close() {
  reader_.reset();
  if (indexFile_.isOpen()) indexFile_.close();
}

bool BibleSearchStore::scanSpine(const Epub& epub, const uint16_t spine, BibleSearch::VerseTextScanner& scanner,
                                 bool& malformed) {
  malformed = false;
  if (!scanner.valid()) {
    LOG_ERR(MODULE, "OOM: verse text scanner");
    return false;
  }
  const std::string href = epub.getSpineItem(spine).href;
  if (href.empty()) {
    LOG_ERR(MODULE, "Cannot read spine entry %u", spine);
    return false;
  }

  ScannerPrint sink(scanner);
  const bool streamed = epub.readItemContentsToStream(href, sink, STREAM_CHUNK_BYTES);
  if (sink.rejected() || (streamed && !scanner.feed("", 0, true))) {
    if (scanner.outOfMemory()) {
      LOG_ERR(MODULE, "Out of memory parsing spine %u (%s)", spine, href.c_str());
      return false;
    }
    LOG_ERR(MODULE, "Spine %u (%s) is not well-formed verse markup", spine, href.c_str());
    malformed = true;
    return false;
  }
  if (!streamed) {
    LOG_ERR(MODULE, "Cannot stream spine %u (%s)", spine, href.c_str());
    return false;
  }
  return true;
}

bool BibleSearchStore::verseTexts(const Epub& epub, const uint16_t spine, const BibleSearch::VerseEntry* wanted,
                                  const size_t count, const VerseTextSink sink, void* ctx) {
  BibleSearch::VerseTextScanner scanner;
  bool malformed = false;
  if (!scanSpine(epub, spine, scanner, malformed)) return false;

  const std::vector<BibleSearch::VerseText> verses = scanner.take();
  for (size_t i = 0; i < count; i++) {
    const auto match = std::find_if(verses.begin(), verses.end(), [&](const BibleSearch::VerseText& verse) {
      return verse.chapter == wanted[i].chapter && verse.verse == wanted[i].verse;
    });
    if (match != verses.end()) sink(ctx, i, match->text);
  }
  return true;
}
```

**Check the hash did not move.** The fingerprint is still FNV-1a 64 over `addU64(epubSize)`,
`addU32(spineCount)`, each href plus its NUL, then `addU32(count)`, the same calls as before. Only
the type now comes from `BibleSearchFingerprintFile`. The device run confirms it: the baseline's
`fingerprint b063e8dc59554f2d` must reappear unchanged (Task 8).

- [ ] **Step 3: Build the firmware**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro.log | tail -3`
Expected: `[SUCCESS]`. Then run `grep -n "BibleSearch.*warning" build/pio-x4pro.log`.
Expected: no output.

- [ ] **Step 4: Host suites still pass**

Run:
```bash
cmake --build build/test --target BibleSearchIndexTest BibleSearchFingerprintTest -j
./build/test/bible_search_index/BibleSearchIndexTest && ./build/test/bible_search_index/BibleSearchFingerprintTest
```
Expected: both end `[  PASSED  ]`.

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix -g
git add src/study/BibleSearchStore.h src/study/BibleSearchStore.cpp
git commit -m "perf: read the Bible search fingerprint from its record"
```

---

### Task 5: Store — keep the reader and book names between visits

After this task `open()` resumes a held reader, and `park()`, `restoreBookNames()` and
`keepBookNames()` exist. The activity still calls `close()` until Task 6, so the device behaves
as before. This task is verified by the build.

**Files:**
- Modify: `src/study/BibleSearchStore.h`
- Modify: `src/study/BibleSearchStore.cpp`

- [ ] **Step 1: Header — the class comment, the public API and the members**

In `src/study/BibleSearchStore.h`, replace:

```cpp
#include "study/BibleSearchFingerprintFile.h"

// The Bible search index on the card: /.berean/search/bible.idx, and the build
```

with:

```cpp
#include "study/BibleSearchFingerprintFile.h"

class BibleBookNameTable;

// The Bible search index on the card: /.berean/search/bible.idx, and the build
```

Replace:

```cpp
// in that Bible's cache directory, so a boot does not re-resolve every verse
// document to find it.
//
```

with:

```cpp
// in that Bible's cache directory, so a boot does not re-resolve every verse
// document to find it.
//
// Between visits to the search screen it also keeps, for the same Bible, the
// opened reader -- its ~444 KB term index and 4 KB posting page, in PSRAM -- and
// a copy of the book names. park() closes the index file and keeps them while
// PSRAM allows; the next open() re-reads the index header and reuses the reader
// only if the file is unchanged. Another Bible, close() (a rebuild or a failed
// query) or a park below the PSRAM floor drops them; sleep is a chip reset.
//
```

Replace:

```cpp
  // A reader with the term index cached, kept open until close(). Null unless
  // the index is Ok; `statusOut` then says why.
  const BibleSearch::IndexReader* open(const std::shared_ptr<Epub>& epub, Status* statusOut = nullptr);
  // Frees the reader, its term cache and its file handle.
  void close();
```

with:

```cpp
  // A reader with the term index cached, kept until close() or a release. Null
  // unless the index is Ok; `statusOut` then says why.
  const BibleSearch::IndexReader* open(const std::shared_ptr<Epub>& epub, Status* statusOut = nullptr);
  // Frees the reader, its term cache and its file handle. Kept book names stay:
  // they belong to the Bible, not to the index.
  void close();
  // The search screen is leaving: closes the index file, and keeps the reader and
  // the book names for the next open() unless PSRAM is below the retention floor.
  void park();

  // Copies the book names kept for the Bible that the last open() or status()
  // identified into `out`. False when none are kept for it.
  bool restoreBookNames(const std::shared_ptr<Epub>& epub, BibleBookNameTable& out) const;
  // Keeps a copy of `names` for the Bible that the last open() or status()
  // identified. Neither call sizes the EPUB again.
  void keepBookNames(const std::shared_ptr<Epub>& epub, const BibleBookNameTable& names);
```

Replace:

```cpp
  bool identify(const std::shared_ptr<Epub>& epub);
  void forgetBible();
```

with:

```cpp
  bool identify(const std::shared_ptr<Epub>& epub);
  bool identifiedAs(const std::shared_ptr<Epub>& epub) const;
  void forgetBible();
  // Re-opens bible.idx under the held reader. True when its header and size are
  // the ones the reader was opened on.
  bool resumeHeldReader();
  void releaseResident();
```

Replace:

```cpp
  HalFile indexFile_;
  std::unique_ptr<BibleSearch::IndexReader> reader_;
};
```

with:

```cpp
  // The held reader's source. Closed while parked: the reader must not be read
  // until resumeHeldReader() has re-opened and re-checked it.
  HalFile indexFile_;
  std::unique_ptr<BibleSearch::IndexReader> reader_;
  BibleSearch::AllocatedBuffer names_;
};
```

- [ ] **Step 2: Source — includes and the names copy check**

In `src/study/BibleSearchStore.cpp`, replace:

```cpp
#include <algorithm>
#include <cstring>
#include <vector>

#include "StudyStore/UnitIndexFormat.h"
#include "study/StudyStore.h"
#include "util/TaskWatchdog.h"
```

with:

```cpp
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <vector>

#include "StudyStore/UnitIndexFormat.h"
#include "activities/reader/BibleBookNameTable.h"
#include "study/BibleSearchResidency.h"
#include "study/StudyStore.h"
#include "util/TaskWatchdog.h"
```

Replace:

```cpp
constexpr uint32_t SPINE_READS_PER_WATCHDOG_RESET = 64;

void* psramAllocate(
```

with:

```cpp
constexpr uint32_t SPINE_READS_PER_WATCHDOG_RESET = 64;

static_assert(std::is_trivially_copyable_v<BibleBookNameTable>, "kept book names are copied with memcpy");

void* psramAllocate(
```

- [ ] **Step 3: Source — forgetting a Bible releases everything kept**

Replace:

```cpp
void BibleSearchStore::forgetBible() {
  close();
```

with:

```cpp
void BibleSearchStore::forgetBible() {
  releaseResident();
```

- [ ] **Step 4: Source — `open()` resumes a held reader**

Replace the whole `open()` function:

```cpp
const BibleSearch::IndexReader* BibleSearchStore::open(const std::shared_ptr<Epub>& epub, Status* statusOut) {
  close();
  uint64_t fingerprint = 0;
  const bool identified = fingerprintFor(epub, fingerprint);
  [[maybe_unused]] const unsigned long started = millis();
  const Status result = identified ? openIndexFile(fingerprint) : Status::Unreadable;
  if (statusOut) *statusOut = result;

  if (result != Status::Ok) {
    LOG_ERR(MODULE, "Index not usable (status %d)", static_cast<int>(result));
    close();
    return nullptr;
  }
  if (!reader_->cacheTermIndex()) {
    // Still correct, only slower: lookups then read the term table from the card.
    LOG_ERR(MODULE, "Term index not cached; lookups will read the card");
  }
  LOG_DBG(MODULE, "Index opened in %lu ms (term index %u bytes)", millis() - started,
          static_cast<unsigned>(termIndexBytes()));
  return reader_.get();
}
```

with:

```cpp
const BibleSearch::IndexReader* BibleSearchStore::open(const std::shared_ptr<Epub>& epub, Status* statusOut) {
  uint64_t fingerprint = 0;
  const bool identified = fingerprintFor(epub, fingerprint);
  [[maybe_unused]] const unsigned long started = millis();
  const bool resumed =
      identified && reader_ && reader_->header().fingerprint == fingerprint && resumeHeldReader();
  Status result = Status::Ok;
  if (!resumed) {
    close();
    result = identified ? openIndexFile(fingerprint) : Status::Unreadable;
  }
  if (statusOut) *statusOut = result;

  if (result != Status::Ok) {
    LOG_ERR(MODULE, "Index not usable (status %d)", static_cast<int>(result));
    close();
    return nullptr;
  }
  // A no-op for a held cache; a retry when it could not be allocated before.
  if (!reader_->cacheTermIndex()) {
    // Still correct, only slower: lookups then read the term table from the card.
    LOG_ERR(MODULE, "Term index not cached; lookups will read the card");
  }
  LOG_DBG(MODULE, "Index %s in %lu ms (term index %u bytes)", resumed ? "resumed" : "opened", millis() - started,
          static_cast<unsigned>(termIndexBytes()));
  return reader_.get();
}
```

- [ ] **Step 5: Source — resume, park, release and the names copy**

Replace:

```cpp
void BibleSearchStore::close() {
  reader_.reset();
  if (indexFile_.isOpen()) indexFile_.close();
}
```

with:

```cpp
void BibleSearchStore::close() {
  reader_.reset();
  if (indexFile_.isOpen()) indexFile_.close();
}

bool BibleSearchStore::resumeHeldReader() {
  if (indexFile_.isOpen()) indexFile_.close();
  if (!Storage.openFileForRead(MODULE, INDEX_PATH, indexFile_)) {
    LOG_INF(MODULE, "Kept index reader dropped: %s cannot be opened", INDEX_PATH);
    return false;
  }
  // The reader seeks before every read, so this header read leaves it no
  // position to restore.
  uint8_t raw[BibleSearch::INDEX_HEADER_BYTES];
  const int got = indexFile_.read(raw, sizeof(raw));
  const auto fileSize = static_cast<uint32_t>(indexFile_.fileSize());
  if (got < 0 ||
      !BibleSearchResidency::heldIndexMatches(reader_->header(), raw, static_cast<size_t>(got), fileSize)) {
    LOG_INF(MODULE, "Kept index reader dropped: %s changed", INDEX_PATH);
    return false;
  }
  return true;
}

void BibleSearchStore::park() {
  if (indexFile_.isOpen()) indexFile_.close();
  if (!reader_ && !names_.get()) return;

  const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t largestPsram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (!BibleSearchResidency::keepAfterExit(freePsram, largestPsram)) {
    LOG_INF(MODULE, "PSRAM low (free %u, largest %u): dropping the kept index and book names",
            static_cast<unsigned>(freePsram), static_cast<unsigned>(largestPsram));
    releaseResident();
    return;
  }
  LOG_DBG(MODULE, "Parked: term index %u B, names %u B; PSRAM free %u, largest %u",
          static_cast<unsigned>(termIndexBytes()), static_cast<unsigned>(names_.size()),
          static_cast<unsigned>(freePsram), static_cast<unsigned>(largestPsram));
}

void BibleSearchStore::releaseResident() {
  close();
  names_.reset();
}

bool BibleSearchStore::identifiedAs(const std::shared_ptr<Epub>& epub) const {
  return epub && identified_ && identity_.path == epub->getPath() &&
         identity_.spineCount == static_cast<uint32_t>(std::max(epub->getSpineItemsCount(), 0));
}

bool BibleSearchStore::restoreBookNames(const std::shared_ptr<Epub>& epub, BibleBookNameTable& out) const {
  if (!names_.get() || !identifiedAs(epub)) return false;
  memcpy(static_cast<void*>(&out), names_.get(), sizeof(BibleBookNameTable));
  return true;
}

void BibleSearchStore::keepBookNames(const std::shared_ptr<Epub>& epub, const BibleBookNameTable& names) {
  if (!identifiedAs(epub)) return;
  if (!names_.get() && !names_.allocate(psramAllocator(), sizeof(BibleBookNameTable))) {
    LOG_ERR(MODULE, "OOM: kept book names (%u bytes)", static_cast<unsigned>(sizeof(BibleBookNameTable)));
    return;
  }
  memcpy(names_.get(), static_cast<const void*>(&names), sizeof(BibleBookNameTable));
}
```

- [ ] **Step 6: Build the firmware**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro.log | tail -3`
Expected: `[SUCCESS]`. Then run `grep -n "BibleSearch.*warning" build/pio-x4pro.log`.
Expected: no output.

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add src/study/BibleSearchStore.h src/study/BibleSearchStore.cpp
git commit -m "perf: keep the Bible search reader and book names between visits"
```

---

### Task 6: Activity — park on exit, index first then names, one timing line

**Files:**
- Modify: `src/activities/reader/BibleSearchActivity.h`
- Modify: `src/activities/reader/BibleSearchActivity.cpp`

Do not touch `onEnter` or `enterState`: they belong to R20 (t1).

- [ ] **Step 1: Header**

In `src/activities/reader/BibleSearchActivity.h`, replace:

```cpp
  // Loaded by the loop task in Opening, before any state that draws a name.
  // Empty when the load fails; references then read "c:v".
  BibleBookNameTable bookNames;
```

with:

```cpp
  // Filled by the loop task in Opening, before any state that draws a name: from
  // the store's kept copy, else loaded. Empty when the load fails; references
  // then read "c:v".
  BibleBookNameTable bookNames;
```

Replace:

```cpp
  void runBusyWork();
  void openIndex();
```

with:

```cpp
  void runBusyWork();
  void openIndex();
  void openIndexAndNames();
  BibleSearch::IndexReader::Status openReader();
  void showOpenResult(BibleSearch::IndexReader::Status status);
```

- [ ] **Step 2: `onExit` parks instead of closing**

In `src/activities/reader/BibleSearchActivity.cpp`, replace:

```cpp
  BibleSearchStore::getInstance().close();
  reader = nullptr;
  UiListActivity::onExit();
```

with:

```cpp
  BibleSearchStore::getInstance().park();
  reader = nullptr;
  UiListActivity::onExit();
```

- [ ] **Step 3: Opening runs the index work first, then the names**

Replace:

```cpp
    case State::Opening:
      if (!bookNames.load(epub, renderer)) {
        LOG_ERR(MODULE, "Book names unavailable; references show chapter:verse only");
      }
      openIndex();
      return;
```

with:

```cpp
    case State::Opening:
      openIndexAndNames();
      return;
```

Replace the whole `openIndex()` function:

```cpp
void BibleSearchActivity::openIndex() {
  auto& store = BibleSearchStore::getInstance();
  Status status = Status::Missing;
  reader = store.open(epub, &status);
  if (reader) {
    openKeyboard();
    return;
  }
  // open() reports only on bible.idx; a matching checkpoint beside it is a
  // build that will resume, which the prompt and the starting frame say.
  status = store.status(epub);
  showPrompt(status);
}
```

with:

```cpp
Status BibleSearchActivity::openReader() {
  auto& store = BibleSearchStore::getInstance();
  Status status = Status::Missing;
  reader = store.open(epub, &status);
  if (reader) return Status::Ok;
  // open() reports only on bible.idx; a matching checkpoint beside it is a
  // build that will resume, which the prompt and the starting frame say.
  return store.status(epub);
}

void BibleSearchActivity::showOpenResult(const Status status) {
  if (reader) {
    openKeyboard();
    return;
  }
  showPrompt(status);
}

void BibleSearchActivity::openIndex() { showOpenResult(openReader()); }

// The index first: open() identifies the Bible the kept book names are matched
// against.
void BibleSearchActivity::openIndexAndNames() {
  [[maybe_unused]] const unsigned long started = millis();
  const Status status = openReader();

  auto& store = BibleSearchStore::getInstance();
  [[maybe_unused]] const char* names = "resident";
  if (!store.restoreBookNames(epub, bookNames)) {
    if (bookNames.load(epub, renderer)) {
      store.keepBookNames(epub, bookNames);
      names = "loaded";
    } else {
      LOG_ERR(MODULE, "Book names unavailable; references show chapter:verse only");
      names = "unavailable";
    }
  }
  LOG_DBG(MODULE, "Opened in %lu ms (index %s, names %s)", millis() - started, reader ? "ready" : "unusable", names);
  showOpenResult(status);
}
```

**Spec delta, deliberate.** The spec's log reads
`index <resumed|opened|unusable>`. The activity cannot tell "resumed" from "opened", so it logs
`ready|unusable`. The store's `[BSEARCH] Index resumed|opened in N ms` line, logged just before,
carries that distinction.

- [ ] **Step 4: Build the firmware**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro.log | tail -3`
Expected: `[SUCCESS]`. Then run `grep -n "BibleSearch.*warning" build/pio-x4pro.log`.
Expected: no output.

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix -g
git add src/activities/reader/BibleSearchActivity.h src/activities/reader/BibleSearchActivity.cpp
git commit -m "perf: reopen Bible search without reloading the index or book names"
```

---

### Task 7: Document `search-fingerprint.bin`

**Files:**
- Modify: `docs/file-formats.md`

- [ ] **Step 1: Insert the section**

In `docs/file-formats.md`, replace:

```markdown
Neither is overwritten until the user confirms a rebuild. The Spanish NWT indexes to 31,078
verses, 23,568 terms and 1,469,729 bytes; the write budget is 8 MB.

## Catalog index (`catalog-<lang>.txt`, `catalog-<lang>.v2.txt`)
```

with:

```markdown
Neither is overwritten until the user confirms a rebuild. The Spanish NWT indexes to 31,078
verses, 23,568 terms and 1,469,729 bytes; the write budget is 8 MB.

## `/.crosspoint/epub_<hash>/search-fingerprint.bin`

The Bible search fingerprint (the `fingerprint` in `bible.idx`'s header), kept beside the Bible's
`book.bin`. The first search after a boot reads these 40 bytes instead of re-resolving every
verse document, which takes about 2 s on the NWT.

- **Owners.** `src/study/BibleSearchFingerprintFile.{h,cpp}` owns the layout, load and save.
  `BibleSearchStore.cpp` decides when to read and write it.
- **Derived data.** Deleting it costs one recompute.
- **Lifetime.** It lives in the Bible's cache directory on purpose: every on-device delete or
  replace of the Bible removes that directory, and the record with it. That covers web upload,
  delete, move and rename, WebDAV, the file browser, the publication downloader and Clear Cache.
  An in-place `book.bin` rebuild does not: a `BOOK_CACHE_VERSION` change, or `book.bin` deleted on
  its own. So `VERSION` must be bumped with any `BOOK_CACHE_VERSION` change that alters spine
  hrefs.
- **What is written.** Only a resolve in which every book 1-66 contributed a verse document. A
  partial one is used for that boot only.
- **How it is written.** As `search-fingerprint.bin.tmp`; then the old record is removed and the
  temp file is renamed into place.

### Version 1

Little-endian throughout; every field is copied with `memcpy`. Exactly 40 bytes.

| Offset | Field |
|---|---|
| 0 | magic `BSFP` |
| 4 | `version` u16 |
| 6 | `unitIndexVersion` u16 — `study::UNIT_INDEX_VERSION` when written |
| 8 | `epubSize` u64 |
| 16 | `spineCount` u32 |
| 20 | `verseDocuments` u32 |
| 24 | `fingerprint` u64 |
| 32 | `check` u64 — FNV-1a 64 over bytes 0-31 |

It is read with one `read` of `min(file size, 40)` bytes and checked in this order:
1. fewer than 6 bytes, or a wrong magic → corrupt;
2. a newer `version` → refused, whatever the length;
3. an older version, a length other than 40, or a `check` mismatch → corrupt;
4. a key that differs from the Bible on the card (EPUB size, spine count,
   `study::UNIT_INDEX_VERSION`) → stale.

What happens next:
- **Corrupt or stale:** recomputed and replaced.
- **Newer, or one that cannot be opened or read:** recomputed in memory and never overwritten.

## Catalog index (`catalog-<lang>.txt`, `catalog-<lang>.v2.txt`)
```

- [ ] **Step 2: Commit**

```bash
git add docs/file-formats.md
git commit -m "docs: document the Bible search fingerprint record"
```

---

### Task 8: Whole-tree checks, the follow-up, and the PR

**Files:** none new. If the whole-tree format pass rewrites any file not on the `FILES:` lines,
restore it (`git checkout -- <file>`) and report it in the PR body; do not commit it.

**Who does what:** Steps 1-4 are the implementer's. Step 5 belongs to the worker: it pushes and
opens the PR only after re-running Steps 1-3 itself. The implementer commits but never pushes.

- [ ] **Step 1: Format the whole tree, as CI does**

Run: `./bin/clang-format-fix`
Expected: exit 0. If it rewrote files on the `FILES:` lines, `git add` those and commit
`style: apply clang-format`. Restore any other rewritten file, as the task header says.

- [ ] **Step 2: All host suites**

Run:
```bash
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j
```
Expected: `100% tests passed`. If a gtest race fails on the first build, re-run the build once.

- [ ] **Step 3: The release environment, and `x4pro` only if code changed**

`x4pro` already passed at the end of Task 6. Rebuild it only if a code file changed after that.
Formatting and docs do not count (`CLAUDE.md`, "Testing checklist"). The release env has not
been built yet, and it matters here: with `LOG_LEVEL=0` the log macros expand to nothing, so an
unused log-only variable would warn there.

Run:
```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release 2>&1 | tee build/pio-release.log | tail -3
grep -n "BibleSearch.*warning" build/pio-release.log
```
Expected: `[SUCCESS]`, and the grep prints nothing.

- [ ] **Step 4: File the follow-up the spec names**

Write `build/r11-discover-names.md` (git-ignored):

```markdown
The first Bible search after every boot still loads the book-name table (~250 ms, the same work
as Go to's uncached "books ready", 252-254 ms in the 2026-10-11 baseline). R11 keeps a copy in
PSRAM between searches but cannot persist it: the table is a src/activities/reader type that the
Go to grid also loads, and a shared persisted name cache beside book.bin belongs with R12/R13's
session work. Source: docs/superpowers/specs/2026-10-11-berean-o-6-design.md, Non-goals.
```

Run:
`hpipe discover --task t6 --title "Persist the Bible book-name table beside book.bin" --body-file build/r11-discover-names.md`

- [ ] **Step 5 (worker, after re-running Steps 1-3 itself): push and open the PR**

Run: `git push`

Open the PR against `main`, titled `perf: keep the Bible search index open between searches`.
The body has the sections below, filled in, and its last line is exactly `Refs berean-o-6`:

1. **Summary.** What changed:
   - the reader and book names are kept between visits, checked against the index header on
     re-entry;
   - the fingerprint record;
   - the activity's park and the Opening order.
2. **Before → after (to be measured on the device).**
   - First search after boot, frame → keyboard: **2,436 / 2,440 ms** before (baseline runs 1 and
     2). Expected ≈ 0.5 s.
   - Later search: **500 / 499 / 505 ms** before. Expected < 100 ms.
   - The proof lines are listed in section 5. Say which SD clock the build ran at: R2 (t5) moves
     it from 20 to 40 MHz.
3. **Memory held while the reader is open** (after search has been used once):

   | Block | Bytes | Region |
   |---|---|---|
   | Term index | ~440-444 KB | PSRAM, explicit (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) |
   | Posting page | 4,096 | PSRAM, explicit |
   | Book-name copy | 4,228 | PSRAM, explicit |
   | `IndexReader` object | ~184 | internal |

   - The 3,567 B document list is no longer resolved at all on a boot that hits the record.
   - **When each is released:**
     - **immediately** by a rebuild or a query I/O failure;
     - **at the next search entry** on a Bible change;
     - **when the screen is left**, if PSRAM is below 3 MiB free or a 1 MiB largest block;
     - **at sleep**, by the chip reset.
   - **No eager release.** A deleted Bible's index stays held until the next search entry or
     sleep.
4. **Allocation justification.**
   - Book names, 4,228 B, explicit PSRAM through the module's own `AllocatedBuffer` +
     `psramAllocator()`. Not on the stack: it is far over the 256 B local-variable rule. Not static:
     that would pin 4 KB of internal `.bss` for the whole boot.
   - The term index and posting page are the existing allocations, held longer.
   - The `IndexReader` object is ~184 B, internal, held longer. Moving it would need placement new
     for an object smaller than the allocator's overhead.
   - The record I/O uses 40- and 48-byte stack buffers and no heap.
5. **Device check.** Flash the `x4pro` dev build (`LOG_LEVEL=2`) and capture serial with
   `cat /dev/cu.usbmodem* > serial.log`.
   1. **First search after flashing.** Expect:
      - `[BSEARCH] 1189 verse documents, fingerprint b063e8dc59554f2d`. It **must** equal the
        baseline value; a different value means the hash moved and every index on the card is
        Stale. Stop and report it.
      - `Fingerprint computed in N ms`;
      - `Index opened in N ms`;
      - `[BSUI] Opened in N ms (index ready, names loaded)`.
   2. **Back.** Expect `[BSEARCH] Parked: term index … B, names 4228 B; PSRAM free …`.
   3. **Search again.** Expect `Fingerprint from memory`, `Index resumed in N ms` and
      `Opened in N ms (index ready, names resident)`. Frame (`Wait complete: 8179_DRF`) →
      `Entering activity: KeyboardEntry` should be under 100 ms.
   4. **Home → Bible → Search.** Same as step 3; this is the baseline's own scenario.
   5. **Sleep, wake, Bible → Search.** Expect `Fingerprint from record in N ms` and no
      `verse documents` line. Frame → keyboard should be ≈ 0.5 s.
   6. **The file.** With a card reader, check that
      `/.crosspoint/epub_<hash>/search-fingerprint.bin` is 40 bytes.
6. **For the orchestrator.**
   - No `test/CMakeLists.txt` or `src/main.cpp` lines are needed.
   - The `docs/file-formats.md` section landed in this branch (Task 7), so there is nothing to
     apply.
   - List any file the whole-tree format pass rewrote outside this task's `FILES:` lines and
     restored (Task 8 Step 1).

---

## Self-review against the spec

- **Goal 1** (re-entry):
  - Task 5: `open()` resumes, `restoreBookNames`;
  - Task 6: `park()` on exit, Opening order.
- **Goal 2** (record): Tasks 2-4.
- **Goal 3** (release events and their wording): Task 5's `park`, `releaseResident` and
  `forgetBible`, plus the PR body's memory section.
- **Goal 4** (timings):
  - `Fingerprint from memory|from record|computed`;
  - `Index resumed|opened`;
  - `Parked`;
  - `Opened in`.
- **A1-A13:**
  - A2: `resumeHeldReader`.
  - A3/A13: `identify`, and `identifiedAs` used by the names calls without re-sizing.
  - A4: the memcpy copy plus the `static_assert`.
  - A5: names are kept on the prompt path, since `openIndexAndNames` always runs the names step.
  - A6: `keepAfterExit`, 3 MiB / 1 MiB.
  - A7: no code.
  - A8: `close()` unchanged, and it frees no names.
  - A9-A12: the record, the single gated writer, `coversEveryBook`, and the indexer still
    resolving for real through `documents()`.
- **Decode order (spec, after review 1):** `decode` plus `ANewerVersionIsTooNewAtAnyLength`,
  `AForeignFileIsCorruptEvenWithAHighVersion`, and the I/O tests for the 41-byte and 48-byte files.
- **Docs:** Task 7.
- **Follow-up:** Task 8, step 4.

## Changes after plan review 0

Review: `docs/superpowers/reviews/berean-o-6-plan-review-0.md` (verdict CLEAR; 3 MINOR). All
three are accepted and applied.

- **MINOR 1, the unreachable fallback.** Option (a) is taken. `docs/file-formats.md` stays on the
  `FILES:` lines, and the "Overlap" note now says that holds t6 while t3 implements. The Task 7
  skip branch and the matching PR-body fallback are gone. The R20 sentence is also corrected:
  R20 has landed as #255, and it does not intersect Task 6.
- **MINOR 2, formatting outside the lock.** Task 8 now restores, and reports, any file the
  whole-tree pass rewrites outside the `FILES:` lines.
- **MINOR 3, who pushes.** Task 8 Step 5 is marked as the worker's, after it re-runs Steps 1-3. The
  implementer commits but never pushes.
