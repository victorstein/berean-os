# berean-o-3 implementation plan — make opening the Bible from Home cheap

**Spec:** `docs/superpowers/specs/2026-10-11-berean-o-3-design.md` (cleared spec review 1). Read its §4
assumptions before starting; this plan implements them and cites them as A1–A18.

**What gets built:**

1. A buffered cold load of the book cache's spine sizes, with no on-disk format change.
2. A one-slot "parked book" that keeps the reader's loaded `Epub` and its Go-to cache across a Home round
   trip, revalidated when it is taken.
3. Reuse of the study state when it is still open for that same book.
4. R5: no recents save when nothing changed.
5. Per-step timing for the study open.

FILES: lib/Epub/Epub/BookMetadataCache.h, lib/Epub/Epub/BookMetadataCache.cpp, lib/Epub/Epub.h, lib/Epub/Epub.cpp
FILES: src/activities/reader/ParkedBookOf.h, src/activities/reader/ParkedBook.h, src/activities/reader/ParkedBook.cpp
FILES: src/activities/reader/EpubReaderActivity.h, src/activities/reader/EpubReaderActivity.cpp
FILES: src/activities/reader/BibleNavCache.h, src/activities/reader/BibleBookIndex.h
FILES: src/study/StudyStore.h, src/study/StudyStore.cpp, src/study/StudyReuse.h
FILES: src/util/RecentBooksDoc.h, src/util/RecentBooksDoc.cpp, src/RecentBooksStore.cpp
FILES: test/book_metadata_cache/, test/parked_book/, test/recent_books_doc/RecentBooksDocTest.cpp

## Ground rules for every task

- **Comments.** Write only the non-obvious "why", for the merged state. Never paraphrase the next line, and
  never narrate before and after (`CLAUDE.md`, "Comments"). The comments in the code below already follow
  this, so copy them as written.
- **Commit with explicit pathspecs.** Use `git add <the files the step names>` and
  `git commit -m ... -- <paths>`, never a bare `git commit -a`. End every commit message with the
  `Claude-Session:` trailer the session gives you, and add no co-author line.
- **Never edit `test/CMakeLists.txt`.** It is a shared append point. The two new suites run through a
  local wrapper project under `build/` (gitignored; `git check-ignore build/o3-tests/CMakeLists.txt`
  confirms it). The orchestrator adds the real `add_subdirectory` lines after merge; they are in the PR
  body (Task 13).
- **`EpubReaderActivity` may have moved.** Sibling task t4 (R21) holds that file and lands first. Before
  Task 11, re-read `src/activities/reader/EpubReaderActivity.{h,cpp}` in full. The edits below are
  anchored on quoted text, not line numbers. If an anchor changed, apply the same intent to the current
  text, and say so in the PR.
- **Firmware build once, at the end** (Task 12). The firmware sources (`StudyStore.cpp`, `ParkedBook.cpp`,
  `EpubReaderActivity.cpp`, `Epub.cpp`) have no host suite. They are compiled by `pio run` in Task 12, and
  any error is fixed there. Every host-testable piece is test-first.
- **Paths.** `REPO` means `/Volumes/stein/.herdr/worktrees/berean-os/perf-r12-bible-open`. Run every
  command from `REPO`.

---

## Task 0 — Workspace

**Step 0.1 — Bootstrap the worktree** (submodule and clang-format 21; `pio` is not on `PATH`):

```bash
./bin/bootstrap
~/.platformio/penv/bin/pio --version   # expect: PlatformIO Core, version 6.1.19
```

**Step 0.2 — Create the local test wrapper.** It configures the whole `test/` tree and then adds this
task's new suites without touching `test/CMakeLists.txt`. Write `build/o3-tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(o3_local_tests C CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory(${REPO_ROOT}/test repo_tests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
include(GoogleTest)

# New suites from berean-o-3, uncommented as their directories appear (Tasks 3 and 7).
# add_subdirectory(${REPO_ROOT}/test/book_metadata_cache book_metadata_cache)
# add_subdirectory(${REPO_ROOT}/test/parked_book parked_book)
```

**Step 0.3 — Configure, and run the suites this task touches, as a baseline:**

```bash
cmake -S build/o3-tests -B build/o3-tests/out
cmake --build build/o3-tests/out -j8 --target RecentBooksDocTest StorageIoTest BibleNavCacheTest PassageDocTest CrossPointStateTest PubKeyTest
ctest --test-dir build/o3-tests/out -R 'RecentBooksDoc|StorageIo|BibleNavCache|PassageDoc|CrossPointState|PubKey' --output-on-failure
```

Expect every test to pass. If any fails before you change anything, stop and report it. Nothing to commit.

---

## Task 1 — R5: `RecentBooksDoc::isAlreadyFront` (A14)

**Step 1.1 — Failing tests.** Append to the end of `test/recent_books_doc/RecentBooksDocTest.cpp`:

```cpp

// Opening the book already at the top of the list changes nothing, so RecentBooksStore::addBook
// skips its prune and its save. RecentBook::operator== compares paths only, which is not enough.
TEST(RecentBooksDocFront, TheFirstEntryWithTheSameTitleAndAuthorIsAlreadyFront) {
  const std::vector<RecentBook> books = {makeBook("/b.epub", "Biblia", "WTS"), makeBook("/w.epub", "Atalaya", "WTS")};
  EXPECT_TRUE(RecentBooksDoc::isAlreadyFront(books, makeBook("/b.epub", "Biblia", "WTS")));
}

TEST(RecentBooksDocFront, AnEmptyListHasNoFront) {
  EXPECT_FALSE(RecentBooksDoc::isAlreadyFront({}, makeBook("/b.epub", "Biblia", "WTS")));
}

TEST(RecentBooksDocFront, ABookInSecondPlaceIsNotFront) {
  const std::vector<RecentBook> books = {makeBook("/b.epub", "Biblia", "WTS"), makeBook("/w.epub", "Atalaya", "WTS")};
  EXPECT_FALSE(RecentBooksDoc::isAlreadyFront(books, makeBook("/w.epub", "Atalaya", "WTS")));
}

TEST(RecentBooksDocFront, AChangedTitleOrAuthorIsNotFront) {
  const std::vector<RecentBook> books = {makeBook("/b.epub", "Biblia", "WTS")};
  EXPECT_FALSE(RecentBooksDoc::isAlreadyFront(books, makeBook("/b.epub", "Biblia (2019)", "WTS")));
  EXPECT_FALSE(RecentBooksDoc::isAlreadyFront(books, makeBook("/b.epub", "Biblia", "Watchtower")));
}

// The stored entry went through normalise() when it was added; a candidate is compared only after
// the same normalise(), so an over-long title still matches its capped copy.
TEST(RecentBooksDocFront, AnOverlongTitleMatchesItsStoredCappedCopy) {
  RecentBook stored = makeBook("/b.epub", "", "WTS");
  stored.title = std::string(RecentBooksDoc::MAX_TITLE_BYTES + 40, 't');
  RecentBook candidate = stored;
  RecentBooksDoc::normalise(stored);
  ASSERT_LT(stored.title.size(), candidate.title.size());

  RecentBooksDoc::normalise(candidate);
  EXPECT_TRUE(RecentBooksDoc::isAlreadyFront({stored}, candidate));
}
```

**Step 1.2 — Run it red:**

```bash
cmake --build build/o3-tests/out --target RecentBooksDocTest
```

Expect a compile error: `no member named 'isAlreadyFront' in namespace 'RecentBooksDoc'`.

**Step 1.3 — Implement.** In `src/util/RecentBooksDoc.h`, insert directly after the
`bool normalise(RecentBook& book);` declaration (and its comment block, which stays):

```cpp

// True when `candidate`, already normalise()d, is the first entry with the same title and author:
// adding it would change nothing, so the store can skip its prune and its save.
bool isAlreadyFront(const std::vector<RecentBook>& books, const RecentBook& candidate);
```

In `src/util/RecentBooksDoc.cpp`, insert directly after the closing `}` of `bool normalise(RecentBook& book)`:

```cpp

bool isAlreadyFront(const std::vector<RecentBook>& books, const RecentBook& candidate) {
  if (books.empty()) return false;
  const RecentBook& front = books.front();
  return front.path == candidate.path && front.title == candidate.title && front.author == candidate.author;
}
```

**Step 1.4 — Run it green:**

```bash
cmake --build build/o3-tests/out --target RecentBooksDocTest && ctest --test-dir build/o3-tests/out -R RecentBooksDoc --output-on-failure
```

Expect every test to pass, including the five `RecentBooksDocFront.*`.

**Step 1.5 — Commit:**

```bash
git add src/util/RecentBooksDoc.h src/util/RecentBooksDoc.cpp test/recent_books_doc/RecentBooksDocTest.cpp
git commit -m "perf: tell when a book is already first in recents" -- src/util/RecentBooksDoc.h src/util/RecentBooksDoc.cpp test/recent_books_doc/RecentBooksDocTest.cpp
```

---

## Task 2 — R5: skip the prune and the save (A14)

`RecentBooksStore.cpp` reaches `Arduino.h` and has no host suite (`test/recent_books_doc/RecentBooksDocTest.cpp:1-4`).
The rule it calls is the one Task 1 tested.

**Step 2.1 — Edit `src/RecentBooksStore.cpp`.** Replace this exact block:

```cpp
void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author) {
  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front, bounded: title and author arrive straight from EPUB metadata.
  RecentBook book{path, title, author};
  RecentBooksDoc::normalise(book);
  recentBooks.insert(recentBooks.begin(), std::move(book));
```

with:

```cpp
void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author) {
  // Bounded first: title and author arrive straight from EPUB metadata, and the stored copy was
  // bounded the same way, so the two compare equal when nothing changed.
  RecentBook book{path, title, author};
  RecentBooksDoc::normalise(book);
  if (RecentBooksDoc::isAlreadyFront(recentBooks, book)) {
    LOG_DBG("RBS", "Already first in recent books; not saved");
    return;
  }

  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& entry) { return entry.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  recentBooks.insert(recentBooks.begin(), std::move(book));
```

The rest of the function (trim to `MAX_RECENT_BOOKS`, `saveToFileAtomic()`, its `LOG_ERR`) stays as it is.
The lambda parameter is renamed to `entry` because `book` is now a local in the same scope.

**Step 2.2 — Commit:**

```bash
git add src/RecentBooksStore.cpp
git commit -m "perf: skip the recents save when the book is already first" -- src/RecentBooksStore.cpp
```

---

## Task 3 — Host suite for `BookMetadataCache` (failing tests first)

The suite compiles the real `BookMetadataCache.cpp` against the in-memory Storage fake. That needs three
small test-local pieces:

- a `WString.h` shim, so `FsHelpers.h` reaches the stub `String`;
- a force-included forward declaration of `Print`, which `ZipFile.h` names;
- a link-time `ZipFile` substitute, because only `buildBookBin` uses ZipFile and this suite never builds.

This combination was compiled, linked and run on the host on 2026-10-11 against a scratch copy carrying
Tasks 4–5's code, through a gtest-free harness making the same assertions as the tests below. All
passed, with 79 reads against a bound of 79 for 3,937 entries.

**Step 3.1 — Create `test/book_metadata_cache/shim/WString.h`:**

```cpp
#pragma once

// FsHelpers.h includes Arduino's <WString.h>; the stub <Arduino.h> carries the String this suite needs.
#include <Arduino.h>
```

**Step 3.2 — Create `test/book_metadata_cache/shim/PrintForward.h`:**

```cpp
#pragma once

// ZipFile.h declares readFileToStream(..., Print&, ...) and relies on the real HalStorage.h to bring
// Print in. The stub HalStorage.h does not, and this suite never calls it, so a declaration is enough.
class Print;
```

**Step 3.3 — Create `test/book_metadata_cache/ZipFileStub.cpp`:**

```cpp
// LINK-TIME substitute for lib/ZipFile/ZipFile.cpp. Only BookMetadataCache::buildBookBin calls
// ZipFile, and this suite never builds a cache: every call fails, so a test that reached one would
// fail rather than read a zip.
#include <ZipFile.h>

bool ZipFile::open() { return false; }
bool ZipFile::close() { return true; }
bool ZipFile::getInflatedFileSize(const char*, size_t*) { return false; }
int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>&, std::deque<uint32_t>&) { return 0; }
```

**Step 3.4 — Create `test/book_metadata_cache/CMakeLists.txt`:**

```cmake
# Runs the real BookMetadataCache against the in-memory Storage fake in test/stubs. Modelled on
# test/storage_io (real sources, test/stubs first on the include path) and test/pagination (a
# LINK-TIME substitute, here ZipFileStub.cpp, for a class the suite must not pull in).
#
# shim/WString.h lets FsHelpers.h reach the stub String; shim/PrintForward.h is force-included
# because ZipFile.h names Print, which the stub HalStorage.h does not declare.
add_executable(BookMetadataCacheTest
  BookMetadataCacheTest.cpp
  ZipFileStub.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookMetadataCacheTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/shim
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/ZipFile
)

target_compile_options(BookMetadataCacheTest PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim/PrintForward.h)

target_link_libraries(BookMetadataCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookMetadataCacheTest)
```

**Step 3.5 — Create `test/book_metadata_cache/BookMetadataCacheTest.cpp`:**

```cpp
// Host coverage for BookMetadataCache::load() over a book.bin seeded in the in-memory Storage fake.
// The layout is the one docs/file-formats.md documents and buildBookBin writes; buildBookBin itself
// needs a real zip and is not exercised here.

#include <BookMetadataCache.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "HalStorageFake.h"

namespace {

// BOOK_CACHE_VERSION in BookMetadataCache.cpp. A bump there must bump this, and the layout below.
constexpr uint8_t BOOK_CACHE_VERSION = 10;
constexpr char CACHE_DIR[] = "/cache";
constexpr char BOOK_BIN[] = "/cache/book.bin";
// Every HalFile read load() makes before the spine pass: version, lutOffset, spineCount, tocCount,
// and a length plus the bytes of each of the five metadata strings.
constexpr size_t HEADER_READS = 4 + 5 * 2;
// SPINE_PASS_BUFFER_SIZE in BookMetadataCache.cpp.
constexpr size_t SPINE_PASS_BUFFER = 4096;

struct Spine {
  std::string href;
  uint32_t cumulativeSize;
  int16_t tocIndex;
};

struct Toc {
  std::string title;
  std::string href;
  std::string anchor;
  uint8_t level;
  int16_t spineIndex;
};

template <typename T>
void putPod(std::string& out, const T& value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void putString(std::string& out, const std::string& s) {
  putPod(out, static_cast<uint32_t>(s.size()));
  out += s;
}

struct BookBin {
  std::string bytes;
  size_t spineBlockOffset = 0;
  size_t spineBlockBytes = 0;
};

BookBin makeBookBin(const std::vector<Spine>& spine, const std::vector<Toc>& toc,
                    const uint8_t version = BOOK_CACHE_VERSION) {
  std::string metadata;
  for (const char* field : {"Title", "Author", "es", "", ""}) putString(metadata, field);

  const uint32_t lutOffset =
      sizeof(uint8_t) + sizeof(uint32_t) + 2 * sizeof(uint16_t) + static_cast<uint32_t>(metadata.size());
  const uint32_t lutSize = static_cast<uint32_t>(sizeof(uint32_t) * (spine.size() + toc.size()));
  const uint32_t entriesOffset = lutOffset + lutSize;

  std::string spineBlock;
  std::vector<uint32_t> spineLut;
  for (const Spine& entry : spine) {
    spineLut.push_back(entriesOffset + static_cast<uint32_t>(spineBlock.size()));
    putString(spineBlock, entry.href);
    putPod(spineBlock, entry.cumulativeSize);
    putPod(spineBlock, entry.tocIndex);
  }

  std::string tocBlock;
  std::vector<uint32_t> tocLut;
  for (const Toc& entry : toc) {
    tocLut.push_back(entriesOffset + static_cast<uint32_t>(spineBlock.size() + tocBlock.size()));
    putString(tocBlock, entry.title);
    putString(tocBlock, entry.href);
    putString(tocBlock, entry.anchor);
    putPod(tocBlock, entry.level);
    putPod(tocBlock, entry.spineIndex);
  }

  BookBin bin;
  putPod(bin.bytes, version);
  putPod(bin.bytes, lutOffset);
  putPod(bin.bytes, static_cast<uint16_t>(spine.size()));
  putPod(bin.bytes, static_cast<uint16_t>(toc.size()));
  bin.bytes += metadata;
  for (const uint32_t pos : spineLut) putPod(bin.bytes, pos);
  for (const uint32_t pos : tocLut) putPod(bin.bytes, pos);
  bin.spineBlockOffset = bin.bytes.size();
  bin.spineBlockBytes = spineBlock.size();
  bin.bytes += spineBlock;
  bin.bytes += tocBlock;
  return bin;
}

// `count` entries with NWT-shaped hrefs; every seventh is ~200 bytes, longer than the 64-byte skip
// scratch. Cumulative sizes run 1000, 2000, ...
std::vector<Spine> spineOf(const size_t count) {
  std::vector<Spine> spine;
  spine.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    std::string href = "OEBPS/10010611" + std::to_string(i) + "-split16.xhtml";
    if (i % 7 == 3) href = "OEBPS/Text/" + std::string(190, static_cast<char>('a' + i % 26)) + ".xhtml";
    spine.push_back({href, static_cast<uint32_t>((i + 1) * 1000), static_cast<int16_t>(i % 5)});
  }
  return spine;
}

const std::vector<Toc> TOC = {
    {"Génesis", "OEBPS/1001061100.xhtml", "", 1, 0},
    {"Éxodo", "OEBPS/1001061101.xhtml", "chapter1", 2, 1},
};

class BookMetadataCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

}  // namespace

TEST_F(BookMetadataCacheTest, LoadKeepsEveryCumulativeSize) {
  const std::vector<Spine> spine = {
      {"", 100, -1},
      {"a.xhtml", 250, 0},
      {"OEBPS/1001061146-split16.xhtml", 900, 1},
      {"OEBPS/Text/" + std::string(200, 'x') + ".xhtml", 1500, 1},
  };
  storage_fake::putFile(BOOK_BIN, makeBookBin(spine, TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  EXPECT_TRUE(cache.isLoaded());
  ASSERT_EQ(cache.getSpineCount(), 4);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(cache.getCumulativeSize(i), spine[i].cumulativeSize) << "entry " << i;
  EXPECT_EQ(cache.coreMetadata.title, "Title");
}

TEST_F(BookMetadataCacheTest, EntriesStraddlingABufferRefillLoadWhole) {
  const std::vector<Spine> spine = spineOf(400);
  const BookBin bin = makeBookBin(spine, TOC);
  ASSERT_GT(bin.spineBlockBytes, 3 * SPINE_PASS_BUFFER) << "the spine must span several refills";
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  for (size_t i = 0; i < spine.size(); ++i) {
    EXPECT_EQ(cache.getCumulativeSize(static_cast<int>(i)), spine[i].cumulativeSize) << "entry " << i;
  }
}

// The NWT's 3,937 entries took 276 ms on the device read four HalFile calls at a time, each taking
// storageMutex. The pass must cost one read per buffer window instead.
TEST_F(BookMetadataCacheTest, TheSpineIsReadInBufferWindowsNotPerEntry) {
  const std::vector<Spine> spine = spineOf(3937);
  const BookBin bin = makeBookBin(spine, TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  const size_t windows = (bin.spineBlockBytes + SPINE_PASS_BUFFER - 1) / SPINE_PASS_BUFFER;
  EXPECT_LE(storage_fake::readCallsOn(BOOK_BIN), HEADER_READS + windows)
      << "per-entry reads would be " << HEADER_READS + 4 * spine.size();
}

TEST_F(BookMetadataCacheTest, AnotherVersionIsRefused) {
  storage_fake::putFile(BOOK_BIN, makeBookBin(spineOf(3), TOC, BOOK_CACHE_VERSION - 1).bytes);

  BookMetadataCache cache(CACHE_DIR);
  EXPECT_FALSE(cache.load());
  EXPECT_FALSE(cache.isLoaded());
}

// A truncated book.bin used to load garbage sizes; it must fail so Epub::load rebuilds it.
TEST_F(BookMetadataCacheTest, AShortSpineBlockFailsTheLoad) {
  const BookBin bin = makeBookBin(spineOf(50), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes.substr(0, bin.spineBlockOffset + bin.spineBlockBytes / 2));

  BookMetadataCache cache(CACHE_DIR);
  EXPECT_FALSE(cache.load());
  EXPECT_FALSE(cache.isLoaded());
  EXPECT_EQ(cache.getCumulativeSize(0), 0u);
}

TEST_F(BookMetadataCacheTest, EntriesStillResolveThroughTheLookupTablesAfterLoad) {
  const std::vector<Spine> spine = spineOf(20);
  storage_fake::putFile(BOOK_BIN, makeBookBin(spine, TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  for (const int i : {0, 3, 19}) {
    const BookMetadataCache::SpineEntry entry = cache.getSpineEntry(i);
    EXPECT_EQ(entry.href, spine[i].href) << "entry " << i;
    EXPECT_EQ(entry.cumulativeSize, spine[i].cumulativeSize) << "entry " << i;
    EXPECT_EQ(entry.tocIndex, spine[i].tocIndex) << "entry " << i;
  }
  const BookMetadataCache::TocEntry toc = cache.getTocEntry(1);
  EXPECT_EQ(toc.title, "Éxodo");
  EXPECT_EQ(toc.anchor, "chapter1");
  EXPECT_EQ(toc.spineIndex, 1);
}
```

**Step 3.6 — Wire the suite into the local wrapper.** In `build/o3-tests/CMakeLists.txt`, uncomment the
`book_metadata_cache` line:

```cmake
add_subdirectory(${REPO_ROOT}/test/book_metadata_cache book_metadata_cache)
```

**Step 3.7 — Run it red:**

```bash
cmake -S build/o3-tests -B build/o3-tests/out
cmake --build build/o3-tests/out --target BookMetadataCacheTest && ctest --test-dir build/o3-tests/out -R BookMetadataCache --output-on-failure
```

Expect exactly two failures:

- `TheSpineIsReadInBufferWindowsNotPerEntry`: the read count is ~15,762 against a bound of ~79.
- `AShortSpineBlockFailsTheLoad`: `load()` returns true today.

The other four pass. They pin the current behaviour that Task 4 must keep. Do not commit yet.

---

## Task 4 — Buffered spine pass (A3, A4)

**Step 4.1 — `lib/Epub/Epub/BookMetadataCache.cpp`, constants.** Replace:

```cpp
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;
```

with:

```cpp
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;
// load()'s spine pass reads through one window this size. Transient, and not above the 4,096-byte
// PSRAM routing threshold, so it is internal SRAM for the length of the pass.
constexpr size_t SPINE_PASS_BUFFER_SIZE = 4096;
// Each spine entry's href is skipped through a stack scratch this size, a chunk at a time, so the
// pass allocates nothing however long the href is.
constexpr size_t HREF_SKIP_CHUNK = 64;
```

**Step 4.2 — Same file, the pass itself.** Replace the end of the anonymous namespace:

```cpp
  serialization::readPod(file, entry.spineIndex);
  return entry;
}
}  // namespace
```

with:

```cpp
  serialization::readPod(file, entry.spineIndex);
  return entry;
}

// Keeps only each spine entry's cumulativeSize. False on any short read: a truncated book.bin must
// fail the load, and so be rebuilt, rather than load garbage.
bool readCumulativeSizes(serialization::BufferedFileReader& in, const uint16_t count, std::vector<uint32_t>& out) {
  uint8_t scratch[HREF_SKIP_CHUNK];
  for (uint16_t i = 0; i < count; i++) {
    uint32_t hrefBytes = 0;
    if (in.read(&hrefBytes, sizeof(hrefBytes)) != sizeof(hrefBytes)) return false;
    while (hrefBytes > 0) {
      const size_t chunk = std::min<size_t>(hrefBytes, sizeof(scratch));
      if (in.read(scratch, chunk) != chunk) return false;
      hrefBytes -= static_cast<uint32_t>(chunk);
    }
    uint32_t cumulativeSize = 0;
    int16_t tocIndex = 0;
    if (in.read(&cumulativeSize, sizeof(cumulativeSize)) != sizeof(cumulativeSize)) return false;
    if (in.read(&tocIndex, sizeof(tocIndex)) != sizeof(tocIndex)) return false;
    out.push_back(cumulativeSize);
  }
  return true;
}
}  // namespace
```

**Step 4.3 — Same file, `load()`.** Replace:

```cpp
  // Cache cumulative spine sizes in RAM. The progress bar (every render) and percent
  // jumps otherwise pay 2 seeks + a heap-allocating SpineEntry read per access. Spine
  // entries are stored contiguously in index order immediately after the LUTs, so read
  // them in a single sequential pass.
  cumulativeSizes.clear();
  cumulativeSizes.reserve(spineCount);
  const uint32_t lutSize = (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  bookFile.seek(lutOffset + lutSize);
  for (uint16_t i = 0; i < spineCount; i++) {
    cumulativeSizes.push_back(readSpineEntry(bookFile).cumulativeSize);
  }

  loaded = true;
```

with:

```cpp
  // Cache cumulative spine sizes in RAM. The progress bar (every render) and percent
  // jumps otherwise pay 2 seeks + a heap-allocating SpineEntry read per access. Spine
  // entries are stored contiguously in index order immediately after the LUTs, so read
  // them in one buffered sequential pass rather than four mutex-guarded SD calls each.
  loadedFileSize = bookFile.size();
  cumulativeSizes.clear();
  cumulativeSizes.reserve(spineCount);
  const uint32_t lutSize = (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  bool spineRead = bookFile.seek(lutOffset + lutSize);
  if (spineRead) {
    serialization::BufferedFileReader spineIn(bookFile, SPINE_PASS_BUFFER_SIZE);
    spineRead = readCumulativeSizes(spineIn, spineCount, cumulativeSizes);
  }
  if (!spineRead) {
    LOG_ERR("BMC", "Spine block in %s%s is short; cache unusable", cachePath.c_str(), bookBinFile);
    cumulativeSizes.clear();
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  loaded = true;
```

**Step 4.4 — `lib/Epub/Epub/BookMetadataCache.h`.** Replace:

```cpp
  // Cumulative spine sizes, cached in RAM at load() so progress/percent lookups are
  // O(1) instead of 2 seeks + a heap-allocating SpineEntry read per access (4 bytes
  // per spine item; <1KB for typical books).
  std::vector<uint32_t> cumulativeSizes;
```

with:

```cpp
  // Cumulative spine sizes, cached in RAM at load() so progress/percent lookups are
  // O(1) instead of 2 seeks + a heap-allocating SpineEntry read per access (4 bytes
  // per spine item: 15.7 KB for the 3,937-item NWT, which lands in PSRAM by size).
  std::vector<uint32_t> cumulativeSizes;
  // book.bin's size when load() read it; reopen() keeps the cache only while it is unchanged.
  size_t loadedFileSize = 0;
```

**Step 4.5 — Run it green:**

```bash
cmake --build build/o3-tests/out --target BookMetadataCacheTest && ctest --test-dir build/o3-tests/out -R BookMetadataCache --output-on-failure
```

Expect all six tests to pass.

**Step 4.6 — Commit:**

```bash
git add lib/Epub/Epub/BookMetadataCache.h lib/Epub/Epub/BookMetadataCache.cpp test/book_metadata_cache/
git commit -m "perf: read the book cache's spine sizes in one buffered pass" -- lib/Epub/Epub/BookMetadataCache.h lib/Epub/Epub/BookMetadataCache.cpp test/book_metadata_cache/
```

---

## Task 5 — `BookMetadataCache::reopen()` (A6)

**Step 5.1 — Failing tests.** Append to the end of `test/book_metadata_cache/BookMetadataCacheTest.cpp`:

```cpp

// A parked book's cache is checked again before reuse (ParkedBook). The old handle may name a file
// that was deleted and rebuilt since, so reopen() reads a fresh one and compares it with load()'s.
TEST_F(BookMetadataCacheTest, ReopenKeepsAnUntouchedCache) {
  const std::vector<Spine> spine = spineOf(20);
  storage_fake::putFile(BOOK_BIN, makeBookBin(spine, TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  EXPECT_TRUE(cache.reopen());
  EXPECT_TRUE(cache.isLoaded());
  EXPECT_EQ(cache.getSpineEntry(19).href, spine[19].href);
}

TEST_F(BookMetadataCacheTest, ReopenNeedsALoadFirst) {
  storage_fake::putFile(BOOK_BIN, makeBookBin(spineOf(3), TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  EXPECT_FALSE(cache.reopen());
}

TEST_F(BookMetadataCacheTest, ReopenFailsOnceTheFileIsGone) {
  storage_fake::putFile(BOOK_BIN, makeBookBin(spineOf(3), TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  ASSERT_TRUE(Storage.remove(BOOK_BIN));
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}

TEST_F(BookMetadataCacheTest, ReopenFailsWhenTheSizeChanged) {
  const BookBin bin = makeBookBin(spineOf(3), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  storage_fake::putFile(BOOK_BIN, bin.bytes + "x");
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}

TEST_F(BookMetadataCacheTest, ReopenFailsWhenTheVersionChangedAtTheSameSize) {
  const BookBin bin = makeBookBin(spineOf(3), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  std::string otherVersion = bin.bytes;
  otherVersion[0] = static_cast<char>(BOOK_CACHE_VERSION + 1);
  storage_fake::putFile(BOOK_BIN, otherVersion);
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}

// Byte 5 is the low byte of spineCount: u8 version at 0, u32 lutOffset at 1-4.
TEST_F(BookMetadataCacheTest, ReopenFailsWhenTheSpineCountChangedAtTheSameSize) {
  const BookBin bin = makeBookBin(spineOf(3), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  std::string otherSpineCount = bin.bytes;
  otherSpineCount[5] = static_cast<char>(otherSpineCount[5] ^ 1);
  storage_fake::putFile(BOOK_BIN, otherSpineCount);
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}
```

The header layout is `docs/file-formats.md:59-65`.

**Step 5.2 — Run it red:**

```bash
cmake --build build/o3-tests/out --target BookMetadataCacheTest
```

Expect a compile error: `no member named 'reopen' in 'BookMetadataCache'`.

**Step 5.3 — Declare it.** In `lib/Epub/Epub/BookMetadataCache.h`, replace:

```cpp
  bool load();
  SpineEntry getSpineEntry(int index);
```

with:

```cpp
  bool load();
  // After time away from a loaded cache: closes book.bin and opens it again, keeping the cache only
  // if the file's size and header still equal what load() read. The old handle may name a file
  // deleted and rebuilt since, so it is never read again. False leaves the cache unloaded.
  bool reopen();
  SpineEntry getSpineEntry(int index);
```

**Step 5.4 — Implement it.** In `lib/Epub/Epub/BookMetadataCache.cpp`, insert directly before the line
`uint32_t BookMetadataCache::getCumulativeSize(const int index) const {`:

```cpp
bool BookMetadataCache::reopen() {
  if (!loaded) return false;
  loaded = false;
  // Explicit close() required: member variable persists beyond function scope
  bookFile.close();
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    LOG_DBG("BMC", "Reopen: book.bin is gone");
    return false;
  }

  uint8_t fileVersion = 0;
  uint32_t fileLutOffset = 0;
  uint16_t fileSpineCount = 0;
  uint16_t fileTocCount = 0;
  serialization::readPod(bookFile, fileVersion);
  serialization::readPod(bookFile, fileLutOffset);
  serialization::readPod(bookFile, fileSpineCount);
  serialization::readPod(bookFile, fileTocCount);
  if (bookFile.size() != loadedFileSize || fileVersion != BOOK_CACHE_VERSION || fileLutOffset != lutOffset ||
      fileSpineCount != spineCount || fileTocCount != tocCount) {
    LOG_DBG("BMC", "Reopen: book.bin changed since load");
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  loaded = true;
  return true;
}

```

**Step 5.5 — Run it green:**

```bash
cmake --build build/o3-tests/out --target BookMetadataCacheTest && ctest --test-dir build/o3-tests/out -R BookMetadataCache --output-on-failure
```

Expect all 12 tests to pass.

**Step 5.6 — Commit:**

```bash
git add lib/Epub/Epub/BookMetadataCache.h lib/Epub/Epub/BookMetadataCache.cpp test/book_metadata_cache/BookMetadataCacheTest.cpp
git commit -m "perf: let a loaded book cache recheck its file on SD" -- lib/Epub/Epub/BookMetadataCache.h lib/Epub/Epub/BookMetadataCache.cpp test/book_metadata_cache/BookMetadataCacheTest.cpp
```

---

## Task 6 — `Epub::reopenCache` (A7)

No host suite compiles `Epub.cpp` (spec §9). This is `reopen()`, which Task 5 tested, followed by the
existing CSS calls. Task 12 compiles it, and device step 6 exercises it.

**Step 6.1 — `lib/Epub/Epub.h`.** Replace:

```cpp
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);
```

with:

```cpp
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);
  // Re-checks a loaded book against its cache on SD after time away from it: reopens book.bin
  // (BookMetadataCache::reopen) and, unless skipLoadingCss, requires the CSS rules cache to be
  // present and current. False means the caller must load a fresh Epub, which rebuilds whatever is
  // missing; this never rebuilds.
  bool reopenCache(bool skipLoadingCss);
```

**Step 6.2 — `lib/Epub/Epub.cpp`.** Insert directly before the line `bool Epub::clearCache() const {`:

```cpp
bool Epub::reopenCache(const bool skipLoadingCss) {
  if (!bookMetadataCache || !cssParser || !bookMetadataCache->reopen()) return false;
  if (skipLoadingCss) return true;
  // load()'s CSS check without its rebuild branch: the caller loads a fresh Epub instead, and that
  // load rebuilds the rules and drops the sections exactly as a cold open does.
  if (!cssParser->hasCache() || !cssParser->loadFromCache()) {
    LOG_DBG("EBP", "Reopen: CSS rules cache missing or stale");
    return false;
  }
  cssParser->clear();
  return true;
}

```

**Step 6.3 — Commit:**

```bash
git add lib/Epub/Epub.h lib/Epub/Epub.cpp
git commit -m "perf: recheck a loaded Epub against its cache" -- lib/Epub/Epub.h lib/Epub/Epub.cpp
```

---

## Task 7 — `ParkedBookOf` (A5, A8)

The rules are a template over the book and the nav cache, so the host suite can run them without `Epub`.
It is modelled on `BibleNavCacheOf` (`src/activities/reader/BibleNavCache.h:19-22`) and its suite
(`test/number_grid/BibleNavCacheTest.cpp:13-18`).

**Deviation from the spec, recorded:** spec §5.1 puts the template in `ParkedBook.h`. Here it lives in
`ParkedBookOf.h`, so that the host suite can include it without `Epub.h`, and `ParkedBook.h` (Task 10)
holds the firmware instance. This is the same split as `BibleNavCache.h` and `BibleBookIndex.h`.

**Step 7.1 — Create `test/parked_book/CMakeLists.txt`:**

```cmake
# The warm-open rules, header-only and Epub-free by design: ParkedBookOf
# (src/activities/reader/ParkedBookOf.h) and study_reuse::mayReuse (src/study/StudyReuse.h).
# Modelled on test/number_grid's BibleNavCacheTest.
add_executable(ParkedBookTest
  ParkedBookTest.cpp
)

target_include_directories(ParkedBookTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ParkedBookTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ParkedBookTest)
```

**Step 7.2 — Create `test/parked_book/ParkedBookTest.cpp`:**

```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "activities/reader/ParkedBookOf.h"

// A parked book is handed back only while it is still the same file with the same cache, and a
// take that misses must let go of what was parked: the slot must never keep a stale Epub alive.

namespace {

struct FakeBook {
  bool cacheStillMatches = true;
  int reopenCalls = 0;
  bool lastSkipLoadingCss = false;

  bool reopenCache(const bool skipLoadingCss) {
    ++reopenCalls;
    lastSkipLoadingCss = skipLoadingCss;
    return cacheStillMatches;
  }
};

struct FakeNav {
  int marker = 0;
};

using Slot = ParkedBookOf<FakeBook, FakeNav>;

constexpr char PATH[] = "/Biblia.epub";
constexpr uint64_t SIZE = 14945282;

}  // namespace

TEST(ParkedBook, AnEmptySlotMisses) {
  Slot slot;
  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::Empty);
}

TEST(ParkedBook, TheSameUnchangedBookIsHandedBackWithItsNavCache) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  auto nav = std::make_shared<FakeNav>();
  slot.park(PATH, SIZE, {book, nav});

  Slot::Miss miss = Slot::Miss::Empty;
  const Slot::Held taken = slot.take(PATH, SIZE, true, miss);
  EXPECT_EQ(taken.book, book);
  EXPECT_EQ(taken.nav, nav);
  EXPECT_EQ(miss, Slot::Miss::None);
  EXPECT_EQ(book->reopenCalls, 1);
  EXPECT_TRUE(book->lastSkipLoadingCss);
  EXPECT_TRUE(slot.empty());
}

TEST(ParkedBook, AnotherPathMissesAndReleasesTheParkedBook) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  auto nav = std::make_shared<FakeNav>();
  slot.park(PATH, SIZE, {book, nav});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take("/Atalaya.epub", SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::OtherPath);
  EXPECT_EQ(book.use_count(), 1);
  EXPECT_EQ(nav.use_count(), 1);
  EXPECT_EQ(book->reopenCalls, 0);
  EXPECT_TRUE(slot.empty());
}

TEST(ParkedBook, AChangedFileSizeMissesAndReleases) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  slot.park(PATH, SIZE, {book, nullptr});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE + 1, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::FileChanged);
  EXPECT_EQ(book.use_count(), 1);
  EXPECT_EQ(book->reopenCalls, 0);
}

TEST(ParkedBook, AFileThatCouldNotBeSizedMisses) {
  Slot slot;
  slot.park(PATH, SIZE, {std::make_shared<FakeBook>(), nullptr});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, 0, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::FileChanged);
}

TEST(ParkedBook, ACacheThatNoLongerMatchesMissesAndReleases) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  book->cacheStillMatches = false;
  auto nav = std::make_shared<FakeNav>();
  slot.park(PATH, SIZE, {book, nav});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::CacheChanged);
  EXPECT_EQ(book->reopenCalls, 1);
  EXPECT_EQ(book.use_count(), 1);
  EXPECT_EQ(nav.use_count(), 1);
}

TEST(ParkedBook, ParkingReplacesAndReleasesThePreviousBook) {
  Slot slot;
  auto first = std::make_shared<FakeBook>();
  auto second = std::make_shared<FakeBook>();
  slot.park("/Atalaya.epub", SIZE, {first, nullptr});
  slot.park(PATH, SIZE, {second, nullptr});
  EXPECT_EQ(first.use_count(), 1);

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, second);
}

TEST(ParkedBook, ClearReleasesTheParkedBook) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  slot.park(PATH, SIZE, {book, nullptr});
  slot.clear();
  EXPECT_TRUE(slot.empty());
  EXPECT_EQ(book.use_count(), 1);
}

TEST(ParkedBook, ATakeThatMissedLeavesNothingForTheNextTake) {
  Slot slot;
  slot.park(PATH, SIZE, {std::make_shared<FakeBook>(), nullptr});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE + 1, false, miss).book, nullptr);
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::Empty);
}
```

**Step 7.3 — Wire the suite into the local wrapper.** In `build/o3-tests/CMakeLists.txt`, uncomment the
`parked_book` line:

```cmake
add_subdirectory(${REPO_ROOT}/test/parked_book parked_book)
```

**Step 7.4 — Run it red:**

```bash
cmake -S build/o3-tests -B build/o3-tests/out
cmake --build build/o3-tests/out --target ParkedBookTest
```

Expect a compile error: `'activities/reader/ParkedBookOf.h' file not found`.

**Step 7.5 — Create `src/activities/reader/ParkedBookOf.h`:**

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// One book kept across a Home round trip: the reader parks its loaded book on exit, and the next
// reader takes it back instead of loading it again. Nothing reads through a parked book, so a
// change on the card is caught when it is taken: the path and the EPUB's size must match, and the
// book must still agree with its cache on SD (Book::reopenCache). take() always empties the slot.
//
// A template over the book and the Go-to cache so the host suite can exercise the rules without
// Epub (test/parked_book); the firmware instantiates it once, in ParkedBook.h.
template <typename Book, typename NavCache>
class ParkedBookOf {
 public:
  struct Held {
    std::shared_ptr<Book> book;
    std::shared_ptr<NavCache> nav;
  };

  // Why a take() handed nothing back, for the log.
  enum class Miss : uint8_t { None, Empty, OtherPath, FileChanged, CacheChanged };

  void park(std::string path, const uint64_t fileSize, Held held) {
    path_ = std::move(path);
    fileSize_ = fileSize;
    held_ = std::move(held);
  }

  void clear() {
    held_ = {};
    path_.clear();
    fileSize_ = 0;
  }

  bool empty() const { return held_.book == nullptr; }

  // `fileSize` 0 means the EPUB could not be sized, which never matches.
  Held take(const std::string& path, const uint64_t fileSize, const bool skipLoadingCss, Miss& miss) {
    Held held = std::move(held_);
    const std::string parkedPath = std::move(path_);
    const uint64_t parkedSize = fileSize_;
    clear();

    if (!held.book) {
      miss = Miss::Empty;
      return {};
    }
    if (parkedPath != path) {
      miss = Miss::OtherPath;
      return {};
    }
    if (fileSize == 0 || fileSize != parkedSize) {
      miss = Miss::FileChanged;
      return {};
    }
    if (!held.book->reopenCache(skipLoadingCss)) {
      miss = Miss::CacheChanged;
      return {};
    }
    miss = Miss::None;
    return held;
  }

 private:
  std::string path_;
  uint64_t fileSize_ = 0;
  Held held_;
};
```

**Step 7.6 — Run it green:**

```bash
cmake --build build/o3-tests/out --target ParkedBookTest && ctest --test-dir build/o3-tests/out -R ParkedBook --output-on-failure
```

Expect all nine tests to pass.

**Step 7.7 — Commit:**

```bash
git add src/activities/reader/ParkedBookOf.h test/parked_book/
git commit -m "perf: add a one-book parking slot for Home round trips" -- src/activities/reader/ParkedBookOf.h test/parked_book/
```

---

## Task 8 — `study_reuse::mayReuse` (A9)

**Step 8.1 — Add the executable.** Append to `test/parked_book/CMakeLists.txt`:

```cmake

add_executable(StudyReuseTest
  StudyReuseTest.cpp
)

target_include_directories(StudyReuseTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(StudyReuseTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StudyReuseTest)
```

**Step 8.2 — Create `test/parked_book/StudyReuseTest.cpp`:**

```cpp
#include <gtest/gtest.h>

#include "study/StudyReuse.h"

// A warm open keeps StudyStore's state only when a fresh openPublication would build the same
// state: same book, a clean last load, and a pubkey the registry still resolves the same way.

namespace {

int resolveCalls = 0;
bool keyResolves = true;

bool keyStillResolves(void*) {
  ++resolveCalls;
  return keyResolves;
}

study_reuse::Inputs reusable() {
  study_reuse::Inputs in;
  in.openAndReady = true;
  in.sameBook = true;
  in.loadedCleanly = true;
  in.keyStable = true;
  return in;
}

class StudyReuse : public ::testing::Test {
 protected:
  void SetUp() override {
    resolveCalls = 0;
    keyResolves = true;
  }
};

}  // namespace

TEST_F(StudyReuse, AStableKeyOpenForTheSameBookIsReusedWithoutAskingTheRegistry) {
  EXPECT_TRUE(study_reuse::mayReuse(reusable(), &keyStillResolves, nullptr));
  EXPECT_EQ(resolveCalls, 0);
}

TEST_F(StudyReuse, AStoreNotOpenOrNotReadyIsNotReused) {
  study_reuse::Inputs in = reusable();
  in.openAndReady = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
}

TEST_F(StudyReuse, AStoreOpenForAnotherBookIsNotReused) {
  study_reuse::Inputs in = reusable();
  in.sameBook = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
}

TEST_F(StudyReuse, AFailedPaletteOrPassagesReadIsRetriedNotReused) {
  study_reuse::Inputs in = reusable();
  in.loadedCleanly = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
}

TEST_F(StudyReuse, AnUnstableKeyIsReusedOnlyWhileItStillResolvesTheSame) {
  study_reuse::Inputs in = reusable();
  in.keyStable = false;
  EXPECT_TRUE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
  keyResolves = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
  EXPECT_EQ(resolveCalls, 2);
}

TEST_F(StudyReuse, TheRegistryIsNotAskedWhenAnotherConditionAlreadyFails) {
  study_reuse::Inputs in = reusable();
  in.keyStable = false;
  in.sameBook = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
  EXPECT_EQ(resolveCalls, 0);
}
```

**Step 8.3 — Run it red:**

```bash
cmake -S build/o3-tests -B build/o3-tests/out
cmake --build build/o3-tests/out --target StudyReuseTest
```

Expect a compile error: `'study/StudyReuse.h' file not found`.

**Step 8.4 — Create `src/study/StudyReuse.h`:**

```cpp
#pragma once

// Whether a reader may keep the study state StudyStore already holds instead of opening it again.
// Pure, so test/parked_book exercises it without StudyStore's Epub and SD dependencies.
namespace study_reuse {

struct Inputs {
  bool openAndReady = false;   // StudyStore is open and its unit index is ready
  bool sameBook = false;       // it was opened for this very Epub object
  bool loadedCleanly = false;  // that open read the palette and the passages without a Failed result
  bool keyStable = false;      // study::pubKeyIsStable(pubKey)
};

// `keyStillResolves` re-resolves the pubkey against the registry and says whether it is unchanged.
// It is asked only for an unstable (local-) key: a registry write at Home can turn that into a
// registered key, and tags saved under the old one would be lost to every later open.
inline bool mayReuse(const Inputs& in, bool (*keyStillResolves)(void* ctx), void* ctx) {
  if (!in.openAndReady || !in.sameBook || !in.loadedCleanly) return false;
  return in.keyStable || keyStillResolves(ctx);
}

}  // namespace study_reuse
```

**Step 8.5 — Run it green:**

```bash
cmake --build build/o3-tests/out --target StudyReuseTest && ctest --test-dir build/o3-tests/out -R StudyReuse --output-on-failure
```

Expect all six tests to pass.

**Step 8.6 — Commit:**

```bash
git add src/study/StudyReuse.h test/parked_book/CMakeLists.txt test/parked_book/StudyReuseTest.cpp
git commit -m "perf: define when an open study store can be reused" -- src/study/StudyReuse.h test/parked_book/CMakeLists.txt test/parked_book/StudyReuseTest.cpp
```

---

## Task 9 — `StudyStore`: reuse check, link-source reset, step timing (A9, A10, A16)

**Step 9.1 — `src/study/StudyStore.h`, public API.** Replace:

```cpp
  bool isOpen() const { return units_ != nullptr; }
```

with:

```cpp
  bool isOpen() const { return units_ != nullptr; }

  // Whether a reader may keep this store's state for `epub` instead of calling openPublication:
  // open and ready, opened for this very Epub, loaded without a Failed read, and under a pubkey
  // that still resolves the same (study_reuse::mayReuse). An unstable key costs a registry read.
  bool isOpenFor(const std::shared_ptr<Epub>& epub) const;
  // What a fresh openPublication would reset that a reused one keeps: the marked link source.
  void resumePublication();
```

**Step 9.2 — Same file, private state.** Replace:

```cpp
  std::unique_ptr<UnitIndexCache> units_;
```

with:

```cpp
  std::unique_ptr<UnitIndexCache> units_;
  // The Epub the last openPublication was for, and whether it read the palette and the passages
  // without a Failed result. Both feed isOpenFor; closePublication clears them.
  std::weak_ptr<Epub> openedFor_;
  bool loadedCleanly_ = false;
```

**Step 9.3 — `src/study/StudyStore.cpp`, include.** Replace:

```cpp
#include "PubKeyRegistry.h"
#include "StudyStore/PubKey.h"
```

with:

```cpp
#include "PubKeyRegistry.h"
#include "StudyReuse.h"
#include "StudyStore/PubKey.h"
```

**Step 9.4 — Same file, helpers.** Replace the end of the anonymous namespace:

```cpp
std::string repairUnitText(void* ctx, const study::Unit& unit) {
  auto* self = static_cast<RepairTextContext*>(ctx);
  return self->units->unitText(self->spine, unit);
}

}  // namespace
```

with:

```cpp
std::string repairUnitText(void* ctx, const study::Unit& unit) {
  auto* self = static_cast<RepairTextContext*>(ctx);
  return self->units->unitText(self->spine, unit);
}

// The canon check the pubkey ladder requires is deferred to the index, which
// reads the book-nav page anyway. Until it has, an NWT-shaped Bible is trusted
// -- that is the only Bible this device has ever opened.
study::PubKeyInputs keyInputsFor(const Epub& epub) {
  study::PubKeyInputs keyInputs;
  keyInputs.isBible = epub.getBibleBookNavSpineIndex() >= 0;
  keyInputs.canonVerified = keyInputs.isBible;
  keyInputs.bookPath = epub.getPath();
  keyInputs.registered = PubKeyRegistry::lookup(keyInputs.bookPath);
  return keyInputs;
}

struct KeyCheck {
  const Epub* epub;
  const std::string* pubKey;
};

bool keyStillResolves(void* ctx) {
  const auto* check = static_cast<const KeyCheck*>(ctx);
  return study::resolvePubKey(keyInputsFor(*check->epub)) == *check->pubKey;
}

}  // namespace
```

**Step 9.5 — Same file, `openPublication` and `closePublication`.** Replace the two functions in full.
Today's text runs from `bool StudyStore::openPublication(` through the closing `}` of
`void StudyStore::closePublication()`. The new text:

```cpp
bool StudyStore::openPublication(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer) {
  closePublication();
  if (!epub) return false;

  [[maybe_unused]] const unsigned long startedMs = millis();
  pubKey_ = study::resolvePubKey(keyInputsFor(*epub));
  [[maybe_unused]] const unsigned long keyMs = millis();

  const auto paletteResult = TagPaletteFile::load(palette_);
  if (paletteResult == TagPaletteFile::LoadResult::Failed) {
    LOG_ERR(MODULE, "Tag palette unreadable; saving disabled for this session");
    saveDisabled_ = true;
  }
  [[maybe_unused]] const unsigned long paletteMs = millis();

  const auto passageResult = PassageFile::load(pubKey_, passages_);
  if (passageResult == PassageFile::LoadResult::Failed) {
    LOG_ERR(MODULE, "Passages for %s unreadable; saving disabled for this session", pubKey_.c_str());
    saveDisabled_ = true;
  }
  [[maybe_unused]] const unsigned long passagesMs = millis();

  units_ = makeUniqueNoThrow<UnitIndexCache>(epub, pubKey_, renderer);
  if (!units_ || !units_->begin()) {
    LOG_ERR(MODULE, "Unit index unavailable; addressing degraded to document offsets");
  }
  LOG_DBG(MODULE, "Open steps: pubkey %lu ms, palette %lu ms, passages %lu ms, units %lu ms", keyMs - startedMs,
          paletteMs - keyMs, passagesMs - paletteMs, millis() - passagesMs);

  openedFor_ = epub;
  loadedCleanly_ =
      paletteResult != TagPaletteFile::LoadResult::Failed && passageResult != PassageFile::LoadResult::Failed;
  PsramJsonAllocator::logMemory("Study open");
  return true;
}

void StudyStore::closePublication() {
  units_.reset();
  passages_ = study::PassageDoc{PsramJsonAllocator::passageDoc()};
  pubKey_.clear();
  linkSource_.reset();
  openedFor_.reset();
  loadedCleanly_ = false;
  // saveDisabled_ deliberately survives: it is a property of the session's
  // knowledge that a file may hold data we could not read, not of one book.
}

bool StudyStore::isOpenFor(const std::shared_ptr<Epub>& epub) const {
  if (!epub) return false;
  study_reuse::Inputs in;
  in.openAndReady = units_ && units_->ready();
  in.sameBook = openedFor_.lock() == epub;
  in.loadedCleanly = loadedCleanly_;
  in.keyStable = study::pubKeyIsStable(pubKey_);
  KeyCheck check{epub.get(), &pubKey_};
  return study_reuse::mayReuse(in, &keyStillResolves, &check);
}

void StudyStore::resumePublication() { linkSource_.reset(); }
```

The only behaviour changes in `openPublication` are the `millis()` stamps, one `LOG_DBG` and the two new
fields. The pubkey inputs moved into `keyInputsFor` unchanged, comment included.

**Step 9.6 — Commit:**

```bash
git add src/study/StudyStore.h src/study/StudyStore.cpp
git commit -m "perf: let a reader reuse the open study state, and time its open" -- src/study/StudyStore.h src/study/StudyStore.cpp
```

---

## Task 10 — The firmware's parked book (`ParkedBook.{h,cpp}`)

**Step 10.1 — Create `src/activities/reader/ParkedBook.h`:**

```cpp
#pragma once

#include <Epub.h>

#include <string>

#include "BibleBookIndex.h"
#include "ParkedBookOf.h"

// The firmware's one parked book (ParkedBookOf). The reader parks on exit and takes on load, both
// on the loop task, so the slot needs no lock of its own.
namespace ParkedBook {

using Slot = ParkedBookOf<Epub, BibleNavCache>;
using Held = Slot::Held;

// Parks `held` for `path`. With no book, or an EPUB that cannot be sized, it parks nothing and
// empties the slot.
void park(const std::string& path, Held held);
void clear();
// The parked book for `path` if it is still valid (ParkedBookOf::take), else nothing. Always
// leaves the slot empty.
Held take(const std::string& path, bool skipLoadingCss);

}  // namespace ParkedBook
```

**Step 10.2 — Create `src/activities/reader/ParkedBook.cpp`:**

```cpp
#include "ParkedBook.h"

#include <HalStorage.h>
#include <Logging.h>

#include <utility>

namespace ParkedBook {
namespace {

constexpr const char* MODULE = "PARK";

Slot& slot() {
  static Slot instance;
  return instance;
}

// 0 when the file cannot be opened, which no parked size matches.
uint64_t epubSize(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return 0;
  return file.fileSize64();
}

[[maybe_unused]] const char* missReason(const Slot::Miss miss) {
  switch (miss) {
    case Slot::Miss::None:
      return "none";
    case Slot::Miss::Empty:
      return "nothing parked";
    case Slot::Miss::OtherPath:
      return "another book";
    case Slot::Miss::FileChanged:
      return "the EPUB changed";
    case Slot::Miss::CacheChanged:
      return "its cache changed";
  }
  return "unknown";
}

}  // namespace

void park(const std::string& path, Held held) {
  const uint64_t size = held.book ? epubSize(path) : 0;
  if (size == 0) {
    slot().clear();
    return;
  }
  slot().park(path, size, std::move(held));
  LOG_DBG(MODULE, "Parked %s", path.c_str());
}

void clear() { slot().clear(); }

Held take(const std::string& path, const bool skipLoadingCss) {
  if (slot().empty()) {
    LOG_DBG(MODULE, "No parked book");
    return {};
  }
  Slot::Miss miss = Slot::Miss::None;
  Held held = slot().take(path, epubSize(path), skipLoadingCss, miss);
  if (held.book) {
    LOG_DBG(MODULE, "Reusing the parked book");
  } else {
    LOG_DBG(MODULE, "Parked book dropped: %s", missReason(miss));
  }
  return held;
}

}  // namespace ParkedBook
```

`missReason` is `[[maybe_unused]]` because the release env's `LOG_LEVEL=0` compiles `LOG_DBG` away
(`lib/Logging/Logging.h:57-61`).

**Step 10.3 — Commit:**

```bash
git add src/activities/reader/ParkedBook.h src/activities/reader/ParkedBook.cpp
git commit -m "perf: hold the firmware's parked book" -- src/activities/reader/ParkedBook.h src/activities/reader/ParkedBook.cpp
```

---

## Task 11 — `EpubReaderActivity`: take, park, study reuse, PSRAM nav cache (A8–A11)

Re-read both files in full first (Ground rules).

**Step 11.1 — `src/activities/reader/EpubReaderActivity.h`, the nav-cache comment.** Replace:

```cpp
  // What the Bible navigator resolved, kept for this reader's life. Allocated on the first Go to in
  // a Bible, so other books and readers that never use Go to pay nothing.
  std::shared_ptr<BibleNavCache> navCache;
```

with:

```cpp
  // What the Bible navigator resolved, kept with `epub` and parked with it on exit (ParkedBook).
  // Allocated in PSRAM on the first Go to in a Bible, so other books and readers that never use Go
  // to pay nothing.
  std::shared_ptr<BibleNavCache> navCache;
```

**Step 11.2 — Same file, the Delete-cache flag.** Replace:

```cpp
  bool pendingReadFolderMove = false;
```

with:

```cpp
  bool pendingReadFolderMove = false;
  // Set by the reader menu's Delete cache: the owner asked for a rebuild, so this book is not parked
  // on exit and its next open loads it afresh.
  bool cacheDeleted = false;
```

**Step 11.3 — Same file, the cold-load helper.** Replace:

```cpp
  bool loadBook() override;
```

with:

```cpp
  // A new Epub, loaded from its cache or indexed when it has none.
  bool loadFreshEpub(bool skipLoadingCss);
  bool loadBook() override;
```

**Step 11.4 — `src/activities/reader/EpubReaderActivity.cpp`, includes.** Make three insertions:

- `#include <esp_heap_caps.h>` on the line before `#include <esp_system.h>`;
- `#include <new>` on the line after `#include <limits>`;
- `#include "ParkedBook.h"` on the line after `#include "PageTurn.h"`.

**Step 11.5 — Same file, the PSRAM nav-cache allocator.** In the top anonymous namespace, insert directly
before the line `}  // namespace` that precedes `EpubReaderActivity::~EpubReaderActivity() {`:

```cpp
// The Go-to cache is held across Home with the parked book, so it is asked of PSRAM explicitly rather
// than left to the size threshold, which falls back to internal RAM when PSRAM is short.
struct PsramNavCacheFree {
  void operator()(BibleNavCache* cache) const {
    cache->~BibleNavCache();
    heap_caps_free(cache);
  }
};

std::shared_ptr<BibleNavCache> makePsramNavCache() {
  void* raw = heap_caps_calloc(1, sizeof(BibleNavCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!raw) return nullptr;
  return std::shared_ptr<BibleNavCache>(new (raw) BibleNavCache(), PsramNavCacheFree{});
}

```

**Step 11.6 — Same file, the destructor.** Replace:

```cpp
  section.reset();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
```

with:

```cpp
  section.reset();
  if (pendingReadFolderMove && epub) {
    ParkedBook::clear();
    const std::string srcPath = epub->getPath();
```

and replace:

```cpp
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }
}
```

with:

```cpp
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else if (cacheDeleted) {
    ParkedBook::clear();
    epub.reset();
  } else {
    // The next open of this book takes it back instead of reloading it. A null epub (a reader that
    // finished before loadBook) parks nothing and empties the slot.
    ParkedBook::park(bookPath, {std::move(epub), std::move(navCache)});
  }
}
```

**Step 11.7 — Same file, `loadBook` and the extracted cold load.** Replace:

```cpp
bool EpubReaderActivity::loadBook() {
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, sdpaths::CROSSPOINT_DIR);
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object");
    return false;
  }

  const bool uncached = !Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
    disableFastInitialRefresh();
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  bool loaded;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = loadedEpub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (!loaded) {
    LOG_ERR("ERS", "Failed to load EPUB");
    return false;
  }
  epub = std::move(loadedEpub);

```

with:

```cpp
bool EpubReaderActivity::loadFreshEpub(const bool skipLoadingCss) {
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, sdpaths::CROSSPOINT_DIR);
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object");
    return false;
  }

  const bool uncached = !Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
    disableFastInitialRefresh();
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  bool loaded;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = loadedEpub->load(true, skipLoadingCss);
  }
  if (!loaded) {
    LOG_ERR("ERS", "Failed to load EPUB");
    return false;
  }
  epub = std::move(loadedEpub);
  return true;
}

bool EpubReaderActivity::loadBook() {
  const bool skipLoadingCss = SETTINGS.embeddedStyle == 0;
  ParkedBook::Held parked = ParkedBook::take(bookPath, skipLoadingCss);
  const bool reusingParkedBook = parked.book != nullptr;
  if (reusingParkedBook) {
    epub = std::move(parked.book);
    navCache = std::move(parked.nav);
  } else if (!loadFreshEpub(skipLoadingCss)) {
    return false;
  }

```

Everything after it — `ImageBlock::clearSessionRenderFailures()`, `setExtractor`, `setupCacheDir`,
`progress.bin`, bookmarks — stays unchanged.

**Step 11.8 — Same file, the study open in `loadBook`.** Replace:

```cpp
  highlightsLoaded = STUDY.openPublication(epub, renderer);
  if (STUDY.saveDisabled()) {
```

with:

```cpp
  if (reusingParkedBook && STUDY.isOpenFor(epub)) {
    // Still open from this book's last open in this boot, and still what a fresh open would build.
    STUDY.resumePublication();
    highlightsLoaded = true;
    LOG_DBG("ERS", "Study already open for this book");
  } else {
    highlightsLoaded = STUDY.openPublication(epub, renderer);
  }
  if (STUDY.saveDisabled()) {
```

**Step 11.9 — Same file, Delete cache.** In the `MenuAction::DELETE_CACHE` case, replace:

```cpp
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
```

with:

```cpp
          section.reset();
          epub->clearCache();
          cacheDeleted = true;
          epub->setupCacheDir();
```

**Step 11.10 — Same file, the Go-to allocation.** Replace:

```cpp
    if (!navCache) {
      // unique_ptr to shared_ptr, as loadBook() hands the Epub over.
      auto cache = makeUniqueNoThrow<BibleNavCache>();
      if (cache) {
        navCache = std::move(cache);
      } else {
        LOG_ERR("ERS", "OOM: Go to cache");
      }
    }
```

with:

```cpp
    if (!navCache) {
      navCache = makePsramNavCache();
      if (!navCache) {
        LOG_ERR("ERS", "OOM: Go to cache");
      }
    }
```

**Step 11.11 — `src/activities/reader/BibleNavCache.h`, the lifetime comment.** Replace:

```cpp
// What the Bible navigator read from SD, kept by the reader so a second Go to reads none of it.
// Only a successful load is kept. Nothing invalidates it during a reader's life: the book never
// changes under a live reader. The navigator reads and writes it on the loop task only.
```

with:

```cpp
// What the Bible navigator read from SD, kept with the Epub it was read from so a second Go to reads
// none of it, across Home too while that Epub is parked (ParkedBook). Only a successful load is kept.
// It is never invalidated on its own: it lives and dies with that Epub, which does not change while
// it is held. The navigator reads and writes it on the loop task only.
```

**Step 11.12 — `src/activities/reader/BibleBookIndex.h`, the stale routing note.** Delete these three
lines; the allocation is now explicit PSRAM (`makePsramNavCache`), not size-routed:

```cpp
// Above CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (4096), so makeUniqueNoThrow asks PSRAM first. That is a
// preference: under PSRAM exhaustion it falls back to internal RAM.
static_assert(sizeof(BibleNavCache) > 4096, "keep the cache PSRAM-preferred by size");
```

**Step 11.13 — Commit:**

```bash
git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp src/activities/reader/BibleNavCache.h src/activities/reader/BibleBookIndex.h
git commit -m "perf: keep the loaded book across a Home round trip" -- src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp src/activities/reader/BibleNavCache.h src/activities/reader/BibleBookIndex.h
```

---

## Task 12 — Build, format, full host run

**Step 12.1 — Firmware build** (once; never run two `pio run`s at the same time):

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tail -40
```

Expect `SUCCESS` and no warning from a file this task touched. Fix any error or warning in the file it
names, and re-run. The likely ones, and their fixes:

- **Missing `millis` in `StudyStore.cpp`:** it includes `<Arduino.h>` (`StudyStore.cpp:3`); re-check that
  the include survived.
- **A `-Wshadow`-style or unused warning:** recheck Steps 2.1 and 10.2.

**Step 12.2 — Format the whole tree.** clang-format-fix skips untracked files, so every new file must
already be committed (Tasks 3–10 did that):

```bash
./bin/clang-format-fix
git status --short
```

If it changed files, review the diff (formatting only), then:

```bash
git add -u
git commit -m "style: format berean-o-3 changes" -- $(git diff --cached --name-only)
```

Check that `git status --short` lists neither `test/CMakeLists.txt` nor anything under `build/`.

**Step 12.3 — Every affected host suite, one last time:**

```bash
cmake --build build/o3-tests/out -j8 --target BookMetadataCacheTest ParkedBookTest StudyReuseTest RecentBooksDocTest StorageIoTest BibleNavCacheTest PassageDocTest CrossPointStateTest PubKeyTest
ctest --test-dir build/o3-tests/out -R 'BookMetadataCache|ParkedBook|StudyReuse|RecentBooksDoc|StorageIo|BibleNavCache|PassageDoc|CrossPointState|PubKey' --output-on-failure
```

Expect all to pass. If the firmware build in 12.1 changed any code, re-run 12.1 after formatting.

**Step 12.4 — Push:**

```bash
git push
```

---

## Task 13 — Open the PR

Title (conventional, lowercase, imperative): `perf: make opening the Bible from Home cheap`. Target `main`.

**Step 13.1 — Write the body** to a scratch file (not in the repo): the text below, then a `## Verified`
section holding the exact `ctest` summary line from Step 12.3 and the `SUCCESS` line plus the RAM/Flash
summary from Step 12.1, then the closing lines shown at the end.

**Step 13.2 — Open it:**
`gh pr create --repo victorstein/berean-os --base main --title "perf: make opening the Bible from Home cheap" --body-file <that scratch file>`.

```markdown
## What

Opening the Bible from Home spent `[READER] Book loaded in` 501–507 ms before anything was drawn
(device baseline, `docs/superpowers/notes/responsiveness-baseline.md`). This change:

- **Cold opens:** reads the book cache's 3,937 spine sizes in one buffered pass instead of four
  mutex-guarded SD calls per entry. There is no `book.bin` format change and no version bump (spec A3),
  so no re-index and no lost chapter layouts.
- **Warm opens:** parks the reader's loaded `Epub` and its Go-to cache when the reader exits, and takes
  them back on the next open of the same book. A take first checks that the path and EPUB size are
  unchanged, reopens `book.bin` and compares its header, and re-checks the CSS cache; a failed check
  means a normal cold load.
- **Study state:** reuses it when it is still open for that same `Epub`, loaded cleanly, under an
  unchanged pubkey.
- **Recents (R5):** skips the save and the `exists()` sweep when the book is already first.
- **Measurement:** times each step of the study open.

Spec: `docs/superpowers/specs/2026-10-11-berean-o-3-design.md`. Plan: `docs/superpowers/plans/2026-10-11-berean-o-3-plan.md`.

## Before → after (device run is the owner's)

| | Before (baseline) | After |
|---|---|---|
| `Loading ePub` → `[BMC] Loaded cache data: 3937 spine, 129 TOC entries` | 274–278 ms | to measure |
| `[READER] Book loaded in`, Home → Bible after a round trip | 501–507 ms | to measure |
| `[READER] Book loaded in`, first open after boot | 501–507 ms | to measure |
| Home Go to: `[BNV] Go to: books ready` | +259–262 ms | to measure |

**Lines that prove the after figure:**

- **Warm open:** `[PARK] Reusing the parked book` and `[ERS] Study already open for this book`, then
  `[READER] Book loaded in N ms`.
- **Cold open:** `[PARK] No parked book` (or `Parked book dropped: …`), then the
  `[EBP] Loading ePub` → `[BMC] Loaded cache data` delta, then `[STUDY] Open steps: pubkey N ms, palette
  N ms, passages N ms, units N ms`.
- **Recents:** `[RBS] Already first in recent books; not saved`.

## How to verify on the device

Flash the `x4pro` dev build (cable, or `POST /upload?path=/` plus Settings → SD firmware update, with
`curl -H "Expect:"`). Capture serial with `cat /dev/cu.usbmodem* > serial.log`.

1. Boot → Home → Bible. A cold open: read the `Loading ePub` → `Loaded cache data` delta and
   `Book loaded in`.
2. Home → Bible five times. Each open should show the warm lines above. On a warm open a tagged passage
   still paints, and Home's "Continue at" updates after leaving the book.
3. After a round trip, a Home Go to logs `[BNV] Go to: books from cache`.
4. Settings → Clear cache → Home → Bible. `Cache not found, building spine/TOC cache` appears while Home
   is entering (its cover thumbnail rebuilds `book.bin`). On the tap, `Parked book dropped: its cache
   changed`, then the CSS rebuild, and the page opens at the book's text reference: Clear cache removes
   `progress.bin`, as it does today.
5. Reader menu → Delete cache → Home → Bible: no `[PARK] Parked` line on the way out, then a cold open at
   the saved page.
6. Turn embedded style off and on between round trips: no crash, and the styling is correct.
7. `MEM Free` at Home after a round trip should be about the before figure (~215 KB internal).

## Memory

- **Spine pass:** one transient 4,096-byte buffer in internal SRAM for the length of `load()`, the build
  path's size. If it cannot be allocated, the read falls back to correct, unbuffered passthrough.
- **Parked `Epub`:** no new memory. Home already kept the previous `Epub` alive through `StudyStore`'s
  unit index (research §3); it is now reused instead of discarded.
- **Go-to cache (~6 KB):** now held across Home with the parked book, so it is allocated in **explicit
  PSRAM** (`heap_caps_calloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` with a destroying deleter, modelled on
  `CatalogIndexStore::PsramFree`) instead of being size-routed with an internal-RAM fallback.
- **Slot:** a function-local static, holding a path string, two `shared_ptr`s and a `uint64_t`.

## For the orchestrator

Add to `test/CMakeLists.txt`:

    add_subdirectory(book_metadata_cache)
    add_subdirectory(parked_book)

Refs berean-o-3
```

End the PR body with the session URL line the session's attribution reminder gives you, on its own line
after `Refs berean-o-3`, if that reminder asks for one.
