# berean-o-25 — `PreachingHandouts` Implementation Plan

**Goal:** A read-only reader for `/preaching-cards.json` (`src/study/PreachingHandouts.{h,cpp}`)
with two passes, `summaries()` and `load()`, proved by 9 host tests in `StorageIoTest`, and the
file documented in `docs/file-formats.md`.

**Spec:** `docs/superpowers/specs/2026-11-22-berean-o-25-design.md`. Read its "Assumptions" (A1–A15)
and "Departures from the plan" before starting. This plan applies all four departures.

**Modelled on:** `src/study/PreachingPassages.{h,cpp}` and `test/storage_io/PreachingPassagesTest.cpp`.

**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/feature-preaching-handouts-reader`, branch
`feature/preaching-handouts-reader`. Run every command from there. `pio` is
`/Volumes/stein/.platformio/penv/bin/pio` (not on PATH).

**Code style (applies to every step, and to any subagent's prompt):** names carry the meaning. A
comment only for a non-obvious *why*; never one that restates the next line. Follow the repo
`CLAUDE.md`: `makeUniqueNoThrow`, `LOG_*`, `.reserve()` before push loops, no explicit `close()` on
a local file. Conventional commits. No Claude or assistant attribution in any commit. Commit, but
never push: the push and the hand-off belong to the orchestrating session.

**Out of scope:** any caller, any UI, strings, `PreachingOrigin`, `PreachingContent`,
`PreachingSession`, and `test/CMakeLists.txt`, which already adds `storage_io` at `:109`.

FILES: src/study/PreachingHandouts.h, src/study/PreachingHandouts.cpp
FILES: test/storage_io/PreachingHandoutsTest.cpp, test/storage_io/CMakeLists.txt
FILES: docs/file-formats.md

---

## Task 0: Re-read, and take the base count

A sibling task may have landed on this branch's base while this one waited. Re-read before you edit.

- [ ] **Step 1: Bootstrap.** Run `./bin/bootstrap`. Expected: exit 0, and `.venv/bin/clang-format`
  exists.

- [ ] **Step 2: Re-read the files this plan touches.** Read `test/storage_io/CMakeLists.txt` in
  full, `docs/file-formats.md` from the `## \`/.berean/preaching.json\`` heading to the
  `## \`/.berean/passages/<pubkey>.json\`` heading, `src/study/PreachingPassages.cpp`, and
  `test/storage_io/PreachingPassagesTest.cpp:1-50`. If `src/study/PreachingHandouts.h` or
  `test/storage_io/PreachingHandoutsTest.cpp` already exists, stop and report: another task wrote it.

- [ ] **Step 3: Configure and take the base count.**

  ```bash
  cmake -S test -B build/test && cmake --build build/test --target StorageIoTest -j
  ./build/test/storage_io/StorageIoTest --gtest_brief=1 | tail -2
  ```

  Expected: `[  PASSED  ] N tests.` Write N down: it was 98 on 2026-11-22, and it is the
  baseline for Task 2 Step 5. If the first build fails in several suites at once, re-run the same
  build command once: a fresh configure races on gtest, which is known and harmless.

---

## Task 1: The failing tests

- [ ] **Step 1: Create `test/storage_io/PreachingHandoutsTest.cpp`** with exactly this content:

```cpp
// The read-only /preaching-cards.json passes behind Predicación's Tarjetas.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingHandouts.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

namespace {

using PreachingHandouts::Status;

const std::string FILE_PATH = PreachingHandouts::FILE_PATH;

const char* const TWO_HANDOUTS = R"({"v":1,"cards":[
  {"title":"Stress","sections":[{"heading":"","verses":[
    {"ref":"Book 1:2","href":"b-split1.xhtml#chapter1_verse2","text":"Made-up verse one."},
    {"ref":"Book 3:4, 5","href":"b-split3.xhtml#chapter3_verse4","text":"Made-up verse two."}]}]},
  {"title":"An issue","sections":[
    {"heading":"First article","verses":[{"ref":"Book 6:7","href":"b-split6.xhtml#chapter6_verse7","text":"Seven."}]},
    {"heading":"Second article","verses":[{"ref":"Book 8:9","href":"b-split8.xhtml#chapter8_verse9","text":"Nine."}]}]}]})";

void* failAllocation(size_t) { return nullptr; }
void releaseNothing(void*) {}

struct ExhaustedJsonAllocator : ArduinoJson::Allocator {
  void* allocate(size_t) override { return nullptr; }
  void deallocate(void* pointer) override { free(pointer); }
  void* reallocate(void*, size_t) override { return nullptr; }
};

class PreachingHandoutsIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingHandoutsIo, SummariesListEveryHandoutWithItsVerseCount) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  std::vector<PreachingHandouts::Summary> summaries;
  ASSERT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Ok);
  ASSERT_EQ(summaries.size(), 2u);
  EXPECT_EQ(summaries[0].title, "Stress");
  EXPECT_EQ(summaries[0].verses, 2);
  EXPECT_EQ(summaries[1].title, "An issue");
  EXPECT_EQ(summaries[1].verses, 2);
}

TEST_F(PreachingHandoutsIo, LoadsOneHandoutsSectionsAndVerses) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(1, {}, handout), Status::Ok);
  EXPECT_EQ(handout.title, "An issue");
  ASSERT_EQ(handout.headings.size(), 2u);
  EXPECT_EQ(handout.headings[0], "First article");
  EXPECT_EQ(handout.headings[1], "Second article");
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[0].section, 0);
  EXPECT_EQ(handout.verses[0].reference, "Book 6:7");
  EXPECT_EQ(handout.verses[0].href, "b-split6.xhtml#chapter6_verse7");
  EXPECT_EQ(handout.verses[0].text, "Seven.");
  EXPECT_EQ(handout.verses[1].section, 1);
  EXPECT_EQ(handout.verses[1].text, "Nine.");
}

TEST_F(PreachingHandoutsIo, ASingleArticleHandoutHasOneEmptyHeading) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ASSERT_EQ(handout.headings.size(), 1u);
  EXPECT_TRUE(handout.headings[0].empty());
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[1].reference, "Book 3:4, 5");
}

TEST_F(PreachingHandoutsIo, AMissingFileIsMissing) {
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Missing);
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(0, {}, handout), Status::Missing);
}

TEST_F(PreachingHandoutsIo, ANewerFormatIsRefusedAsTooNew) {
  storage_fake::putFile(FILE_PATH, R"({"v":2,"cards":[]})");
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::TooNew);
  EXPECT_TRUE(summaries.empty());
}

TEST_F(PreachingHandoutsIo, UnreadableFilesAreUnreadable) {
  for (const char* contents : {"", R"({"v":0,"cards":[]})", R"({"cards":[]})", R"({"v":1})", R"({"v":1,"cards":[{)"}) {
    storage_fake::putFile(FILE_PATH, contents);
    std::vector<PreachingHandouts::Summary> summaries;
    EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Unreadable) << contents;
    EXPECT_TRUE(summaries.empty()) << contents;
  }
  storage_fake::putFile(FILE_PATH, std::string(PreachingHandouts::MAX_FILE_BYTES + 1, ' '));
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Unreadable);
}

TEST_F(PreachingHandoutsIo, AnIndexPastTheEndIsUnreadable) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(2, {}, handout), Status::Unreadable);
  EXPECT_TRUE(handout.verses.empty());
}

TEST_F(PreachingHandoutsIo, AVerseWhoseHrefCannotOpenIsSkipped) {
  const std::string longest(PreachingHandouts::MAX_HREF_BYTES, 'x');
  const std::string tooLong(PreachingHandouts::MAX_HREF_BYTES + 1, 'x');
  storage_fake::putFile(FILE_PATH, R"({"v":1,"cards":[{"title":"T","sections":[{"heading":"","verses":[)"
                                   R"({"ref":"A","href":"","text":"a"},)"
                                   R"({"ref":"B","href":")" + tooLong + R"(","text":"b"},)"
                                   R"({"ref":"Longest","href":")" + longest + R"(","text":"l"},)"
                                   R"({"ref":"C","href":"c.xhtml#chapter1_verse1","text":"c"}]}]}]})");
  std::vector<PreachingHandouts::Summary> summaries;
  ASSERT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Ok);
  EXPECT_EQ(summaries[0].verses, 2);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[0].reference, "Longest");
  EXPECT_EQ(handout.verses[0].href, longest);
  EXPECT_EQ(handout.verses[1].reference, "C");
}

TEST_F(PreachingHandoutsIo, ATextThatCannotBeAllocatedIsOutOfMemory) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;

  study::PassageDoc::Allocators noText;
  noText.text = {failAllocation, releaseNothing};
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  EXPECT_EQ(PreachingHandouts::load(0, noText, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.title.empty());
  EXPECT_TRUE(handout.headings.empty());
  EXPECT_TRUE(handout.verses.empty());

  ExhaustedJsonAllocator exhausted;
  study::PassageDoc::Allocators noJson;
  noJson.json = &exhausted;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  EXPECT_EQ(PreachingHandouts::load(0, noJson, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.title.empty());
  EXPECT_TRUE(handout.headings.empty());
  EXPECT_TRUE(handout.verses.empty());
}

}  // namespace
```

- [ ] **Step 2: Add the test and the source to `StorageIoTest`.** In
  `test/storage_io/CMakeLists.txt`:

  - after the line `  PreachingPassagesTest.cpp` add `  PreachingHandoutsTest.cpp`;
  - after the line `  ${REPO_ROOT}/src/study/PreachingPassages.cpp` add
    `  ${REPO_ROOT}/src/study/PreachingHandouts.cpp`;
  - replace the first four comment lines, which today read

    ```cmake
    # Runs the real store I/O -- PersistableStore.cpp's atomic write, adopting read
    # and loadAdopting, and the TagPaletteFile, PassageFile and PreachingTopicsFile loaders built on
    # it -- against the in-memory Storage fake in
    # test/stubs. Modelled on test/pagination: real lib/ and src/ sources compiled
    ```

    with

    ```cmake
    # Runs the real store I/O -- PersistableStore.cpp's atomic write, adopting read
    # and loadAdopting, the TagPaletteFile, PassageFile and PreachingTopicsFile loaders built on
    # it, and the read-only Predicación passes -- against the in-memory Storage fake in
    # test/stubs. Modelled on test/pagination: real lib/ and src/ sources compiled
    ```

    If those four lines no longer match, because a sibling changed them, leave the comment alone.

  No other line changes. `src/study` is already on the include path (`:40`).

- [ ] **Step 3: See it fail.**

  ```bash
  cmake --build build/test --target StorageIoTest -j 2>&1 | grep -m1 -E 'error|PreachingHandouts.h'
  ```

  Expected: a compile error, `'PreachingHandouts.h' file not found`. Do not commit yet. The
  tree is committed green at the end of Task 2.

---

## Task 2: `PreachingHandouts`

- [ ] **Step 1: Create `src/study/PreachingHandouts.h`** with exactly this content:

```cpp
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

// Read-only passes over /preaching-cards.json, the owner's printed preaching cards ("handouts")
// with every verse their jw.org articles cite, for Predicación's Tarjetas. A script beside the
// cards writes it (rfid-cards/src/cards_json.py) and the owner uploads it over File Transfer,
// which refuses dot-directories: hence the SD root, outside /.berean. The device never writes it.
//
// The firmware must pass PsramJsonAllocator::json() and ::passageDoc(), as for PreachingPassages.
namespace PreachingHandouts {

inline constexpr char FILE_PATH[] = "/preaching-cards.json";
inline constexpr int FORMAT_VERSION = 1;
// cards_json.py refuses to write past the same cap.
inline constexpr size_t MAX_FILE_BYTES = 128 * 1024;
// ReaderEntryIntent::HREF_BYTES less its NUL; cards_json.py checks the same bound.
inline constexpr size_t MAX_HREF_BYTES = 63;
// So a handout's index fits PreachingOrigin's uint8_t.
inline constexpr size_t MAX_HANDOUTS = 200;
inline constexpr size_t MAX_TITLE_BYTES = 96;

enum class Status : uint8_t { Ok, Missing, Unreadable, TooNew, OutOfMemory };

struct Summary {
  std::string title;
  uint16_t verses = 0;
};

struct Verse {
  uint16_t section = 0;  // index into Handout::headings
  std::string reference;
  std::string href;
  study::PassageText text;
};

struct Handout {
  std::string title;
  std::vector<std::string> headings;  // one per section; empty for a single-article handout
  std::vector<Verse> verses;
};

// Each handout's title and the number of verses load() would return, in file order. `out` is
// empty unless the result is Ok.
Status summaries(ArduinoJson::Allocator* json, std::vector<Summary>& out);

// The handout at `index`, its verse texts through `allocators.text`. `out` is empty unless the
// result is Ok; an index past the end is Unreadable.
Status load(size_t index, const study::PassageDoc::Allocators& allocators, Handout& out);

}  // namespace PreachingHandouts
```

- [ ] **Step 2: Create `src/study/PreachingHandouts.cpp`** with exactly this content:

```cpp
#include "PreachingHandouts.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstring>
#include <utility>

namespace PreachingHandouts {
namespace {

constexpr const char* MODULE = "PREACH";
constexpr size_t READ_BUFFER_BYTES = 512;

// The parser pulls one character per read() and every HalFile::read takes storageMutex, hence the
// buffer. PreachingPassages.cpp's twin is file-local too.
class JsonFileReader {
 public:
  explicit JsonFileReader(serialization::BufferedFileReader& in) : in_(in) {}

  int read() {
    uint8_t c = 0;
    return in_.read(&c, 1) == 1 ? c : -1;
  }

 private:
  serialization::BufferedFileReader& in_;
};

JsonDocument newDocument(ArduinoJson::Allocator* allocator) {
  return allocator ? JsonDocument(allocator) : JsonDocument();
}

bool hrefOpens(const char* href) {
  const size_t bytes = strlen(href);
  return bytes > 0 && bytes <= MAX_HREF_BYTES;
}

// Both passes parse through here, so the filtered deserializeJson template is instantiated once.
Status readFiltered(JsonDocument& doc, const JsonDocument& filter) {
  if (filter.overflowed()) {
    LOG_ERR(MODULE, "OOM: handout filter");
    return Status::OutOfMemory;
  }
  // Checked first: a failed open logs, and a card with no handouts file is ordinary.
  if (!Storage.exists(FILE_PATH)) return Status::Missing;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, FILE_PATH, file)) return Status::Unreadable;
  const size_t bytes = file.size();
  if (bytes == 0 || bytes > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "%s: %u bytes, outside 1..%u", FILE_PATH, static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_FILE_BYTES));
    return Status::Unreadable;
  }

  serialization::BufferedFileReader buffered(file, READ_BUFFER_BYTES);
  JsonFileReader reader(buffered);
  const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  if (error == DeserializationError::NoMemory || doc.overflowed()) {
    LOG_ERR(MODULE, "OOM: parsing %s", FILE_PATH);
    return Status::OutOfMemory;
  }
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", FILE_PATH, error.c_str());
    return Status::Unreadable;
  }
  const int version = doc["v"] | 0;
  if (version > FORMAT_VERSION) {
    LOG_ERR(MODULE, "%s: format v%d is newer than v%d", FILE_PATH, version, FORMAT_VERSION);
    return Status::TooNew;
  }
  if (version < 1 || !doc["cards"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "%s: no v%d handouts", FILE_PATH, FORMAT_VERSION);
    return Status::Unreadable;
  }
  return Status::Ok;
}

}  // namespace

Status summaries(ArduinoJson::Allocator* json, std::vector<Summary>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["cards"][0]["title"] = true;
  filter["cards"][0]["sections"][0]["verses"][0]["href"] = true;

  JsonDocument doc = newDocument(json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst handouts = doc["cards"].as<JsonArrayConst>();
  out.reserve(handouts.size() < MAX_HANDOUTS ? handouts.size() : MAX_HANDOUTS);
  for (const JsonVariantConst handout : handouts) {
    if (out.size() == MAX_HANDOUTS) break;
    Summary summary{utf8SafeSummary(handout["title"] | "", MAX_TITLE_BYTES), 0};
    for (const JsonVariantConst section : handout["sections"].as<JsonArrayConst>()) {
      for (const JsonVariantConst verse : section["verses"].as<JsonArrayConst>()) {
        if (hrefOpens(verse["href"] | "") && summary.verses < UINT16_MAX) ++summary.verses;
      }
    }
    out.push_back(std::move(summary));
  }
  LOG_INF(MODULE, "Handouts: %u, %lu ms", static_cast<unsigned>(out.size()), millis() - started);
  return Status::Ok;
}

Status load(const size_t index, const study::PassageDoc::Allocators& allocators, Handout& out) {
  out = Handout{};
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["cards"][0]["title"] = true;
  filter["cards"][0]["sections"][0]["heading"] = true;
  for (const char* key : {"ref", "href", "text"}) filter["cards"][0]["sections"][0]["verses"][0][key] = true;

  JsonDocument doc = newDocument(allocators.json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst handouts = doc["cards"].as<JsonArrayConst>();
  if (index >= handouts.size() || index >= MAX_HANDOUTS) {
    LOG_ERR(MODULE, "No handout %u of %u", static_cast<unsigned>(index), static_cast<unsigned>(handouts.size()));
    return Status::Unreadable;
  }
  const JsonVariantConst handout = handouts[index];
  const JsonArrayConst sections = handout["sections"].as<JsonArrayConst>();
  size_t verseCount = 0;
  for (const JsonVariantConst section : sections) verseCount += section["verses"].size();

  out.title = utf8SafeSummary(handout["title"] | "", MAX_TITLE_BYTES);
  out.headings.reserve(sections.size());
  out.verses.reserve(verseCount);
  for (const JsonVariantConst section : sections) {
    if (out.headings.size() == UINT16_MAX) break;
    const auto sectionIndex = static_cast<uint16_t>(out.headings.size());
    out.headings.push_back(utf8SafeSummary(section["heading"] | "", MAX_TITLE_BYTES));
    for (const JsonVariantConst verse : section["verses"].as<JsonArrayConst>()) {
      const char* href = verse["href"] | "";
      if (!hrefOpens(href)) {
        LOG_ERR(MODULE, "Skipping \"%s\": href of %u bytes", verse["ref"] | "", static_cast<unsigned>(strlen(href)));
        continue;
      }
      Verse entry{sectionIndex, utf8SafeSummary(verse["ref"] | "", study::PassageDoc::MAX_REFERENCE_BYTES), href,
                  study::PassageText(allocators.text)};
      if (!entry.text.assign(verse["text"] | "")) {
        LOG_ERR(MODULE, "OOM: text of handout %u", static_cast<unsigned>(index));
        out = Handout{};
        return Status::OutOfMemory;
      }
      out.verses.push_back(std::move(entry));
    }
  }
  LOG_INF(MODULE, "Handout %u: %u verses, %lu ms", static_cast<unsigned>(index),
          static_cast<unsigned>(out.verses.size()), millis() - started);
  return Status::Ok;
}

}  // namespace PreachingHandouts
```

- [ ] **Step 3: Run the new tests.**

  ```bash
  cmake --build build/test --target StorageIoTest -j 2>&1 | grep -E 'PreachingHandouts.*(warning|error)'
  ctest --test-dir build/test -R PreachingHandouts --output-on-failure
  ```

  Expected: the grep prints nothing, and ctest prints `100% tests passed, 0 tests failed out of 9`.
  The host build has three known warnings that are not ours: gtest's `gtest-printers.h:528`, and
  unused `MODULE` in `PassageFile.cpp:10` and `TagPaletteFile.cpp:12`.

  If a test fails, fix the reader, not the test. Each test encodes a spec assumption (A3, A6,
  A7, A11) or acceptance criterion. If `millis` is undefined, `test/stubs/Arduino.h:20` declares
  it; check the include. If a test genuinely contradicts the spec, stop and report it.

- [ ] **Step 4: Prove the tests bite.** Temporarily change `bytes <= MAX_HREF_BYTES` in
  `hrefOpens` to `bytes < MAX_HREF_BYTES`, rebuild, and run
  `ctest --test-dir build/test -R PreachingHandouts`. Expected: `AVerseWhoseHrefCannotOpenIsSkipped`
  fails. Revert the change.

  Then delete the line `out = Handout{};` inside the `if (!entry.text.assign(...))` block of
  `load()`, the one before `return Status::OutOfMemory;`, not the one at the top of `load()`.
  Rebuild and run the same ctest. Expected: `ATextThatCannotBeAllocatedIsOutOfMemory` fails,
  because the title and heading survive the failure. Restore the line.

  Rebuild and confirm all 9 pass again. The files are untracked, so `git diff` cannot show a
  leftover mutation. Check the source instead:

  ```bash
  grep -c 'bytes <= MAX_HREF_BYTES' src/study/PreachingHandouts.cpp   # 1
  grep -c 'out = Handout{};' src/study/PreachingHandouts.cpp          # 2
  ```

- [ ] **Step 5: Run the whole suite.** Do **not** use `ctest -R StorageIo`: it matches no test and
  exits 0.

  ```bash
  ./build/test/storage_io/StorageIoTest --gtest_brief=1 | tail -2
  ```

  Expected: `[  PASSED  ] N+9 tests.`, where N is Task 0 Step 3's count (107 if N was 98).

- [ ] **Step 6: Format the new files.** The formatter skips untracked files, so stage them first:

  ```bash
  git add src/study/PreachingHandouts.h src/study/PreachingHandouts.cpp \
          test/storage_io/PreachingHandoutsTest.cpp test/storage_io/CMakeLists.txt
  ./bin/clang-format-fix
  git status --short
  ```

  Expected: `git status` lists only the four staged files, plus their unstaged reformat if the
  formatter changed them. If anything else appears, another file was out of format on the base:
  `git checkout -- <that file>`, and do not commit it. If the formatter changed any of the
  four files, `git add` them again, then rebuild and re-run Step 3.

- [ ] **Step 7: Commit.**

  ```bash
  git add src/study/PreachingHandouts.h src/study/PreachingHandouts.cpp \
          test/storage_io/PreachingHandoutsTest.cpp test/storage_io/CMakeLists.txt
  git commit -m "feat(preaching): read the preaching cards file"
  ```

---

## Task 3: Document the file

- [ ] **Step 1: Add the section to `docs/file-formats.md`.** Insert it immediately before the line
  `` ## `/.berean/passages/<pubkey>.json` `` (line 577 on 2026-11-22), after the end of the
  `/.berean/preaching.json` section, with one blank line on each side. Write it exactly as
  follows. It departs from the parent plan's text in "about 65 KB" and in the `Salmo 51` sentence
  (spec Departures 1 and 3).

````markdown
## `/preaching-cards.json`

The owner's printed preaching cards (Predicación's Tarjetas), each with every verse its jw.org
article cites. Written on a Mac by `rfid-cards/src/cards_json.py` and uploaded through File
Transfer, which refuses dot-directories: hence the SD root. Read-only on the device, by
`src/study/PreachingHandouts.{h,cpp}` (host-tested in `test/storage_io/PreachingHandoutsTest.cpp`),
on the loop task.

### Version 1

- `v` — format version. An absent, zero or negative `v` is unreadable; a newer one is refused.

```json
{"v":1,"cards":[{"title":"Menos estrés","sections":[{"heading":"","verses":[
  {"ref":"Mateo 6:34","href":"1001061144-split6.xhtml#chapter6_verse34","text":"…"}]}]}]}
```

- `cards` — in display order; at most 200 are read.
- `title` — the card's title, cut at 96 bytes.
- `sections` — one per article; `heading` is the article's title, empty for a card that links a
  single article.
- `ref` — the label, cut at 48 bytes. A whole-chapter citation is labelled with the chapter alone
  (`Salmo 51`). `text` — the verse text, whole. `href` — the NWT EPUB's own
  `<file>#chapterC_verseV` link, opened as Preguntas opens its references. A verse with an empty
  href or one over 63 bytes is skipped.

A file over 128 KB is refused before parsing. The eight cards of 2026-11-22 come to about 65 KB.
````

- [ ] **Step 2: Check the placement.**

  ```bash
  grep -n '^## `/' docs/file-formats.md | grep -B1 -A1 'preaching-cards'
  ```

  Expected: three lines in this order: `/.berean/preaching.json`, `/preaching-cards.json`,
  `/.berean/passages/<pubkey>.json`.

- [ ] **Step 3: Commit.**

  ```bash
  git add docs/file-formats.md
  git commit -m "docs: describe the preaching cards file"
  ```

---

## Task 4: Firmware build and static check

No source change in this task, unless a check finds something. In that case fix it, re-run Task 2
Steps 3, 5 and 6, and commit as `fix(preaching): <what>`.

- [ ] **Step 1: Build the firmware.**

  ```bash
  /Volumes/stein/.platformio/penv/bin/pio run -e x4pro > build/berean-o-25-pio.log 2>&1; echo "exit=$?"
  grep -n 'PreachingHandouts' build/berean-o-25-pio.log
  grep 'warning:' build/berean-o-25-pio.log | grep -cE '^(src|lib)/'
  tail -6 build/berean-o-25-pio.log
  ```

  Expected: `exit=0` and `SUCCESS`. The first grep shows only a `Compiling
  .pio/build/x4pro/src/study/PreachingHandouts.cpp.o` line: a new translation unit, so pio's
  cache cannot hide its warnings. The count is `0`. The 2026-11-22 baseline build had 248
  warnings, all under `managed_components/`, none in `src/` or `lib/`.

  If the build fails in ESP-IDF's component manager (`index.lock: File exists` under
  `~/Library/Caches/Espressif/ComponentManager/`, or `Original Arduino "idf_component.yml"
  couldnt be restored`), a sibling worktree is building at the same time. Wait a minute and run
  Step 1 again. Do not delete the lock while another `pio run` is alive (`pgrep -fl 'pio run'`).

- [ ] **Step 2: Static analysis of the new file.**

  ```bash
  /Volumes/stein/.platformio/penv/bin/pio check -e x4pro  \
    --src-filters="+<src/study/PreachingHandouts.cpp>" 2>&1 | tail -15
  ```

  Expected: no defect reported against `src/study/PreachingHandouts.cpp` or `.h`. A finding of
  the same kind cppcheck also reports for `src/study/PreachingPassages.cpp`, such as a style note
  on the identical `JsonFileReader`, is the model's pattern and is not fixed here. Note it in the
  hand-off.

- [ ] **Step 3: Clean tree.** `git status --short` prints nothing, and `git log --oneline -3` shows
  the two commits from Tasks 2 and 3 on top of the plan commit.

---

## Verification summary for the hand-off

| Check | Command | Expected |
|---|---|---|
| New tests | `ctest --test-dir build/test -R PreachingHandouts` | 9 / 9 pass |
| Mutations | Task 2 Step 4 | `AVerseWhoseHrefCannotOpenIsSkipped` and `ATextThatCannotBeAllocatedIsOutOfMemory` each fail under their mutation, then pass once restored |
| Whole suite | `./build/test/storage_io/StorageIoTest` | N + 9 pass |
| Format | `./bin/clang-format-fix` after staging | no diff |
| Firmware | `pio run -e x4pro` | SUCCESS, 0 warnings in `src/` or `lib/` |
| Static | `pio check … PreachingHandouts.cpp` | nothing new |
| Device | none | nothing calls the reader yet; the owner's pass belongs to the Tarjetas follow-up |
