# Plan: berean-o-15 (t2): the preaching topics store and the `bible.json` passes

Spec: `docs/superpowers/specs/2026-10-15-berean-o-15-design.md`. Read it first. Everything here
implements it, and the labelled assumptions A1–A15 explain every choice. The parts that matter most
while coding:

- §2 `PreachingTopicsFile`, the 7 save steps;
- §3 `PreachingPassages`;
- the error table.

FILES: lib/Serialization/SdPaths.h, src/study/PreachingTopicsFile.h, src/study/PreachingTopicsFile.cpp
FILES: src/study/PreachingPassages.h, src/study/PreachingPassages.cpp
FILES: test/storage_io/CMakeLists.txt, test/storage_io/PreachingTopicsFileTest.cpp, test/storage_io/PreachingPassagesTest.cpp

## Ground rules for whoever carries this out

- Work only in `/Volumes/stein/.herdr/worktrees/berean-os/feature-preaching-topics-store`, on
  branch `feature/preaching-topics-store`. **Commit, never push.**
- Re-read every file before you edit it. A sibling task may have landed changes. If a file differs
  from what a step quotes, adapt the edit to the file as it is now; do not overwrite.
- **Code is self-documenting.** Use meaningful names. A comment is only for a non-obvious *why*: a
  constraint, a gotcha, a workaround. Do not write comments that restate the code or narrate
  history. Keep the comments written into this plan; they are the only ones needed.
- CLAUDE.md rules that apply here:
  - no bare `new`, no `std::function`;
  - `.reserve()` before a `push_back` loop;
  - locals under 256 B;
  - `HalStorage` only, never SdFat;
  - no `file.close()` on a local `HalFile`;
  - write only through `writeDocToFileAtomic`.
- Do not edit `lib/Serialization/PersistableStore.h`, `test/CMakeLists.txt`, translations or anything
  under `src/activities/`. The spec's "Hand-off" section has the one `PersistableStore.h` comment line,
  for the orchestrator.
- Every commit message ends with the line
  `Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE`, after a blank line.

### Commands used throughout

Host tests. The build directory may already exist; configuring again is harmless. Ninja is not
installed locally, so do not pass `-G Ninja`.

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target StorageIoTest -j8
./build/test/storage_io/StorageIoTest --gtest_brief=1
```

To run only this task's suites, add `--gtest_filter='PreachingTopics*:PreachingPassages*'`.

Baseline before any step: `69 tests from 6 test suites ran … [  PASSED  ] 69 tests.`

---

## Task 1: the path constant

### Step 1.1: add `PREACHING_FILE` to `SdPaths.h`

Edit `lib/Serialization/SdPaths.h`. Replace:

```cpp
// Not under PASSAGES_DIR: the passage scan reads every *.json there as a publication's passages.
inline constexpr char HOME_VERSE_FILE[] = "/.berean/home-verse.json";
```

with:

```cpp
// Not under PASSAGES_DIR: the passage scan reads every *.json there as a publication's passages.
inline constexpr char HOME_VERSE_FILE[] = "/.berean/home-verse.json";
inline constexpr char PREACHING_FILE[] = "/.berean/preaching.json";
```

Replace:

```cpp
static_assert(isUnder(HOME_VERSE_FILE, BEREAN_DIR));
```

with:

```cpp
static_assert(isUnder(HOME_VERSE_FILE, BEREAN_DIR));
static_assert(isUnder(PREACHING_FILE, BEREAN_DIR));
static_assert(!isUnder(PREACHING_FILE, PASSAGES_DIR));
```

### Step 1.2: build and commit

Run the host-test commands above. Expect 69 passed; `SdPaths.h` is included by `PersistableStore.cpp`'s
dependents, so a bad assert fails the compile.

```bash
git add lib/Serialization/SdPaths.h
git commit -m "feat(preaching): declare the preaching topics file path

Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE"
```

---

## Task 2: `PreachingTopicsFile`, the load and the latch

### Step 2.1: write the failing load tests

Create `test/storage_io/PreachingTopicsFileTest.cpp`:

```cpp
// PreachingTopicsFile's load, latch and save against the Storage fake.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingTopicsFile.h"
#include "StudyStore/TagPalette.h"
#include "TagPaletteFile.h"

namespace {

const std::string PATH = PreachingTopicsFile::PATH;
const std::string TMP_PATH = PATH + ".tmp";
const std::string TAGS_PATH = TagPaletteFile::PATH;

std::vector<study::TagId> ids(const std::initializer_list<uint16_t> raw) {
  std::vector<study::TagId> out;
  out.reserve(raw.size());
  for (const uint16_t id : raw) out.push_back(study::toTagId(id));
  return out;
}

// Ids 1..count, all active, written to tags.json.
study::TagPalette seedPalette(const int count) {
  study::TagPalette palette;
  for (int i = 1; i <= count; ++i) palette.add("tag-" + std::to_string(i));
  EXPECT_EQ(TagPaletteFile::save(palette), TagPaletteFile::SaveResult::Ok);
  return palette;
}

class PreachingTopicsFileIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingTopicsFileIo, AMissingFileIsAnEmptySelectionThatMaySave) {
  seedPalette(3);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Empty);
  EXPECT_TRUE(topics.tags().empty());
  EXPECT_FALSE(topics.saveRefused());
}

TEST_F(PreachingTopicsFileIo, LoadReadsTheStoredIds) {
  seedPalette(3);
  storage_fake::putFile(PATH, R"({"v":1,"tags":[3,1]})");
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(topics.tags(), ids({3, 1}));
  EXPECT_FALSE(topics.saveRefused());
  EXPECT_TRUE(topics.palette().isActive(study::toTagId(2)));
}

TEST_F(PreachingTopicsFileIo, DuplicateIdsLoadOnce) {
  storage_fake::putFile(PATH, R"({"v":1,"tags":[5,5,9]})");
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(topics.tags(), ids({5, 9}));
}

TEST_F(PreachingTopicsFileIo, ANewerVersionIsRefusedAndLatchesSaving) {
  const std::string newer = R"({"v":2,"tags":[1]})";
  storage_fake::putFile(PATH, newer);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(topics.tags().empty());
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({1})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_EQ(storage_fake::fileBytes(PATH), newer);
}

TEST_F(PreachingTopicsFileIo, ACorruptFileLatchesSavingAndSurvives) {
  const std::string corrupt = R"({"v":1,"tags":[)";
  storage_fake::putFile(PATH, corrupt);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_EQ(storage_fake::fileBytes(PATH), corrupt);
}

TEST_F(PreachingTopicsFileIo, AnUnreadableFileLatchesSaving) {
  storage_fake::putFile(PATH, R"({"v":1,"tags":[1]})");
  storage_fake::failReadsOf(PATH);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(topics.saveRefused());
}

TEST_F(PreachingTopicsFileIo, MoreThanTheTagCapIsRefusedAndExactlyTheCapLoads) {
  std::string atCap = R"({"v":1,"tags":[)";
  for (size_t id = 1; id <= study::TagPalette::MAX_ACTIVE_TAGS; ++id) {
    if (id > 1) atCap += ",";
    atCap += std::to_string(id);
  }
  storage_fake::putFile(PATH, atCap + "]}");
  PreachingTopicsFile atCapTopics;
  EXPECT_EQ(atCapTopics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(atCapTopics.tags().size(), study::TagPalette::MAX_ACTIVE_TAGS);

  storage_fake::putFile(PATH, atCap + ",201]}");
  PreachingTopicsFile overCap;
  EXPECT_EQ(overCap.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(overCap.saveRefused());
}

TEST_F(PreachingTopicsFileIo, MalformedDocumentsAreRefused) {
  for (const char* malformed : {R"({"tags":[1]})", R"({"v":1,"tags":3})", R"({"v":1})", R"({"v":1,"tags":[0]})",
                                R"({"v":1,"tags":[-1]})", R"({"v":1,"tags":[70000]})", R"({"v":1,"tags":["a"]})",
                                R"([1,2])"}) {
    storage_fake::putFile(PATH, malformed);
    PreachingTopicsFile topics;
    EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed) << malformed;
    EXPECT_TRUE(topics.saveRefused()) << malformed;
  }
}

TEST_F(PreachingTopicsFileIo, AnInterruptedWriteIsRecoveredFromTheTemp) {
  storage_fake::putFile(TMP_PATH, R"({"v":1,"tags":[4]})");
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::RecoveredFromTemp);
  EXPECT_EQ(topics.tags(), ids({4}));
  EXPECT_FALSE(topics.saveRefused());
  EXPECT_TRUE(storage_fake::fileBytes(PATH).has_value());
}

TEST_F(PreachingTopicsFileIo, ACorruptPaletteLatchesSavingAndBothFilesSurvive) {
  const std::string corruptPalette = R"({"v":1,"t":[)";
  const std::string picks = R"({"v":1,"tags":[1]})";
  storage_fake::putFile(TAGS_PATH, corruptPalette);
  storage_fake::putFile(PATH, picks);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({1})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_EQ(storage_fake::fileBytes(TAGS_PATH), corruptPalette);
  EXPECT_EQ(storage_fake::fileBytes(PATH), picks);
}

TEST_F(PreachingTopicsFileIo, SavingBeforeLoadingIsRefused) {
  seedPalette(3);
  PreachingTopicsFile topics;
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({1})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_FALSE(storage_fake::fileBytes(PATH).has_value());
}

}  // namespace
```

Edit `test/storage_io/CMakeLists.txt`. Replace:

```cmake
  TagPaletteFileTest.cpp
  PassageFileTest.cpp
```

with:

```cmake
  TagPaletteFileTest.cpp
  PassageFileTest.cpp
  PreachingTopicsFileTest.cpp
```

and replace:

```cmake
  ${REPO_ROOT}/src/study/PassageFile.cpp
```

with:

```cmake
  ${REPO_ROOT}/src/study/PassageFile.cpp
  ${REPO_ROOT}/src/study/PreachingTopicsFile.cpp
```

In the comment at the top of that file, replace `and the TagPaletteFile and PassageFile loaders built on`
with `and the TagPaletteFile, PassageFile and PreachingTopicsFile loaders built on`.

### Step 2.2: confirm the tests fail

Run the host-test commands. Expect the build to fail because `PreachingTopicsFile.h` does not exist.

### Step 2.3: implement the load, with `save` refusing only

Create `src/study/PreachingTopicsFile.h`:

```cpp
#pragma once

#include <SdPaths.h>
#include <TempAdoption.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "StudyStore/TagPalette.h"

// The tags the owner picked as preaching topics, in /.berean/preaching.json:
// {"v":1,"tags":[12,31,7]}. One object is one session. load() reads this file
// and tags.json, and a failed read of either refuses every save from this
// object, so the picks are never overwritten with what could not be read. A new
// object refuses to save until load() has run.
//
// Loop task only: load() can rename a leftover .tmp into place, which is safe
// only while no other task touches these files (PersistableStore.h,
// loadAdopting).
class PreachingTopicsFile {
 public:
  static constexpr int FORMAT_VERSION = 1;
  static constexpr size_t SAVE_BYTE_BUDGET = 2048;
  static constexpr const char* PATH = sdpaths::PREACHING_FILE;

  using LoadResult = AdoptedLoad;
  enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed, Refused };

  // Tests pass a smaller budget: no list this store accepts reaches 2048 bytes.
  explicit PreachingTopicsFile(size_t saveBudget = SAVE_BYTE_BUDGET) : saveBudget_(saveBudget) {}

  // preaching.json's outcome. A failed tags.json shows only in saveRefused().
  LoadResult load();

  const std::vector<study::TagId>& tags() const { return tags_; }
  const study::TagPalette& palette() const { return palette_; }
  bool saveRefused() const { return saveRefused_; }

  // Keeps the picks active in palette(), once each. Any result but Ok leaves
  // tags() as it was.
  SaveResult save(const std::vector<study::TagId>& picked);

 private:
  study::TagPalette palette_;
  std::vector<study::TagId> tags_;
  size_t saveBudget_;
  bool saveRefused_ = true;
};
```

Create `src/study/PreachingTopicsFile.cpp`:

```cpp
#include "PreachingTopicsFile.h"

#include <ArduinoJson.h>
#include <FormatVersion.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <utility>

#include "TagPaletteFile.h"

namespace {

constexpr const char* MODULE = "PREACH";

void appendUnique(std::vector<study::TagId>& tags, const study::TagId id) {
  if (std::find(tags.begin(), tags.end(), id) == tags.end()) tags.push_back(id);
}

bool parseTopics(const JsonVariantConst json, std::vector<study::TagId>& out) {
  if (!json.is<JsonObjectConst>()) return false;
  if (!persist::isKnownFormatVersion(json["v"] | 0, PreachingTopicsFile::FORMAT_VERSION)) return false;
  const JsonVariantConst stored = json["tags"];
  if (!stored.is<JsonArrayConst>()) return false;
  const JsonArrayConst storedIds = stored.as<JsonArrayConst>();
  if (storedIds.size() > study::TagPalette::MAX_ACTIVE_TAGS) return false;

  out.clear();
  out.reserve(storedIds.size());
  for (const JsonVariantConst id : storedIds) {
    if (!id.is<uint32_t>()) return false;
    const uint32_t raw = id.as<uint32_t>();
    if (raw == 0 || raw > UINT16_MAX) return false;
    appendUnique(out, study::toTagId(static_cast<uint16_t>(raw)));
  }
  return true;
}

}  // namespace

PreachingTopicsFile::LoadResult PreachingTopicsFile::load() {
  palette_ = study::TagPalette{};
  const bool paletteFailed = TagPaletteFile::load(palette_) == TagPaletteFile::LoadResult::Failed;
  if (paletteFailed) LOG_ERR(MODULE, "Tag palette unreadable; preaching topics will not be saved this session");

  std::vector<study::TagId> loaded;
  const LoadResult result = PersistableStoreBase::loadAdopting(
      PATH, &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) {
        return parseTopics(json, *static_cast<std::vector<study::TagId>*>(target));
      },
      &loaded);
  const bool topicsFailed = result == LoadResult::Failed;
  if (topicsFailed) LOG_ERR(MODULE, "%s unreadable or of an unknown format; not saved this session", PATH);

  tags_ = topicsFailed ? std::vector<study::TagId>{} : std::move(loaded);
  saveRefused_ = paletteFailed || topicsFailed;
  return result;
}

PreachingTopicsFile::SaveResult PreachingTopicsFile::save(const std::vector<study::TagId>& picked) {
  if (saveRefused_) {
    LOG_ERR(MODULE, "Refusing to save %s: it or the tag palette was not read cleanly", PATH);
    return SaveResult::Refused;
  }
  return SaveResult::WriteFailed;
}
```

### Step 2.4: run the tests

Run the host-test commands with `--gtest_filter='PreachingTopics*'`. Expect all 11
`PreachingTopicsFileIo` tests to pass. Then run without the filter: expect 80 passed.

### Step 2.5: commit

```bash
git add src/study/PreachingTopicsFile.h src/study/PreachingTopicsFile.cpp \
        test/storage_io/PreachingTopicsFileTest.cpp test/storage_io/CMakeLists.txt
git commit -m "feat(preaching): load the preaching topics and latch saving on failure

Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE"
```

---

## Task 3: `PreachingTopicsFile`, the save

### Step 3.1: write the failing save tests

In `test/storage_io/PreachingTopicsFileTest.cpp`, insert these tests just before the final
`}  // namespace`:

```cpp
TEST_F(PreachingTopicsFileIo, SaveThenLoadRoundTripsThePicks) {
  seedPalette(40);
  PreachingTopicsFile topics;
  ASSERT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Empty);
  ASSERT_EQ(topics.save(ids({12, 31, 7})), PreachingTopicsFile::SaveResult::Ok);
  EXPECT_EQ(topics.tags(), ids({12, 31, 7}));
  EXPECT_EQ(storage_fake::fileBytes(PATH), std::string(R"({"v":1,"tags":[12,31,7]})"));
  EXPECT_FALSE(storage_fake::fileBytes(TMP_PATH).has_value());

  PreachingTopicsFile reloaded;
  EXPECT_EQ(reloaded.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(reloaded.tags(), ids({12, 31, 7}));
}

TEST_F(PreachingTopicsFileIo, SavePrunesRetiredUnknownAndRepeatedIds) {
  study::TagPalette palette = seedPalette(20);
  palette.retire(study::toTagId(7));
  ASSERT_EQ(TagPaletteFile::save(palette), TagPaletteFile::SaveResult::Ok);

  PreachingTopicsFile topics;
  ASSERT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Empty);
  ASSERT_EQ(topics.save(ids({12, 7, 12, 99})), PreachingTopicsFile::SaveResult::Ok);
  EXPECT_EQ(topics.tags(), ids({12}));
  EXPECT_EQ(storage_fake::fileBytes(PATH), std::string(R"({"v":1,"tags":[12]})"));
  EXPECT_TRUE(topics.palette().isActive(study::toTagId(12)));
}

TEST_F(PreachingTopicsFileIo, OverTheByteBudgetIsRefusedAndNothingChanges) {
  seedPalette(3);
  storage_fake::putFile(PATH, R"({"v":1,"tags":[1]})");
  PreachingTopicsFile topics(10);
  ASSERT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(topics.save(ids({1, 2, 3})), PreachingTopicsFile::SaveResult::TooLarge);
  EXPECT_EQ(topics.tags(), ids({1}));
  EXPECT_EQ(storage_fake::fileBytes(PATH), std::string(R"({"v":1,"tags":[1]})"));
  EXPECT_FALSE(storage_fake::fileBytes(TMP_PATH).has_value());
}

TEST_F(PreachingTopicsFileIo, AFailedWriteKeepsTheSelectionAndTheFile) {
  seedPalette(3);
  storage_fake::putFile(PATH, R"({"v":1,"tags":[1]})");
  PreachingTopicsFile topics;
  ASSERT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  storage_fake::failWritesTo(TMP_PATH);
  EXPECT_EQ(topics.save(ids({2})), PreachingTopicsFile::SaveResult::WriteFailed);
  EXPECT_EQ(topics.tags(), ids({1}));
  EXPECT_EQ(storage_fake::fileBytes(PATH), std::string(R"({"v":1,"tags":[1]})"));
}

TEST_F(PreachingTopicsFileIo, AMissingPaletteAllowsSavingAndPrunesEveryPick) {
  PreachingTopicsFile topics;
  ASSERT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Empty);
  EXPECT_FALSE(topics.saveRefused());
  EXPECT_EQ(topics.palette().activeCount(), 0u);
  EXPECT_EQ(topics.save(ids({3})), PreachingTopicsFile::SaveResult::Ok);
  EXPECT_EQ(storage_fake::fileBytes(PATH), std::string(R"({"v":1,"tags":[]})"));
}

TEST_F(PreachingTopicsFileIo, MoreActivePicksThanTheCapAreRefusedNotWritten) {
  const size_t overCap = study::TagPalette::MAX_ACTIVE_TAGS + 1;
  std::string paletteJson = R"({"v":1,"n":)" + std::to_string(overCap + 1) + R"(,"t":[)";
  std::vector<study::TagId> picked;
  picked.reserve(overCap);
  for (size_t id = 1; id <= overCap; ++id) {
    if (id > 1) paletteJson += ",";
    paletteJson += R"({"i":)" + std::to_string(id) + R"(,"n":"t)" + std::to_string(id) + R"("})";
    picked.push_back(study::toTagId(static_cast<uint16_t>(id)));
  }
  storage_fake::putFile(TAGS_PATH, paletteJson + "]}");

  PreachingTopicsFile topics;
  ASSERT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Empty);
  ASSERT_EQ(topics.palette().activeCount(), overCap);
  EXPECT_EQ(topics.save(picked), PreachingTopicsFile::SaveResult::TooLarge);
  EXPECT_TRUE(topics.tags().empty());
  EXPECT_FALSE(storage_fake::fileBytes(PATH).has_value());
}
```

### Step 3.2: confirm the tests fail

Run the host-test commands with `--gtest_filter='PreachingTopics*'`. Expect the build to succeed and
these to fail, because `save` returns `WriteFailed`:

- `SaveThenLoadRoundTripsThePicks`;
- `SavePrunesRetiredUnknownAndRepeatedIds`;
- `OverTheByteBudgetIsRefusedAndNothingChanges`;
- `AMissingPaletteAllowsSavingAndPrunesEveryPick`;
- `MoreActivePicksThanTheCapAreRefusedNotWritten`.

`AFailedWriteKeepsTheSelectionAndTheFile` may pass already.

### Step 3.3: implement the save

In `src/study/PreachingTopicsFile.cpp`, add `#include <SaveBudget.h>` after
`#include <PersistableStore.h>`. Then replace:

```cpp
    return SaveResult::Refused;
  }
  return SaveResult::WriteFailed;
}
```

with:

```cpp
    return SaveResult::Refused;
  }

  std::vector<study::TagId> kept;
  kept.reserve(picked.size());
  for (const study::TagId id : picked) {
    if (palette_.isActive(id)) appendUnique(kept, id);
  }
  // TagPalette::fromJson does not cap active tags, so more can be picked than load() accepts back.
  if (kept.size() > study::TagPalette::MAX_ACTIVE_TAGS) {
    LOG_ERR(MODULE, "Refusing to save %u topics: over the %u-tag cap", static_cast<unsigned>(kept.size()),
            static_cast<unsigned>(study::TagPalette::MAX_ACTIVE_TAGS));
    return SaveResult::TooLarge;
  }

  JsonDocument json;
  json["v"] = FORMAT_VERSION;
  const JsonArray storedIds = json["tags"].to<JsonArray>();
  for (const study::TagId id : kept) storedIds.add(study::toRaw(id));
  // ArduinoJson drops what it cannot allocate; the shorter document would pass the budget and be written.
  if (json.overflowed()) {
    LOG_ERR(MODULE, "Out of memory serialising %s; not written", PATH);
    return SaveResult::WriteFailed;
  }
  if (!persist::fitsBudget(measureJson(json), saveBudget_)) {
    LOG_ERR(MODULE, "%s exceeds its save budget; not written", PATH);
    return SaveResult::TooLarge;
  }
  if (!PersistableStoreBase::writeDocToFileAtomic(PATH, json)) return SaveResult::WriteFailed;

  tags_ = std::move(kept);
  return SaveResult::Ok;
}
```

### Step 3.4: run the tests

Run the host-test commands. Expect all 17 `PreachingTopicsFileIo` tests to pass, and 86 passed
overall.

### Step 3.5: commit

```bash
git add src/study/PreachingTopicsFile.cpp test/storage_io/PreachingTopicsFileTest.cpp
git commit -m "feat(preaching): save the picked topics atomically within budget

Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE"
```

---

## Task 4: `PreachingPassages`, the counts pass

### Step 4.1: write the failing counts tests

Create `test/storage_io/PreachingPassagesTest.cpp`:

```cpp
// The read-only bible.json passes behind Predicación's Mis temas.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingPassages.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"
#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"

namespace {

using PreachingPassages::Status;

const std::string BIBLE = "/.berean/passages/bible.json";

void putBible(const std::string& rows) { storage_fake::putFile(BIBLE, R"({"v":4,"p":[)" + rows + "]}"); }

study::TagId tag(const uint16_t raw) { return study::toTagId(raw); }

struct CountdownJsonAllocator : ArduinoJson::Allocator {
  int remaining = 1 << 30;
  void* allocate(const size_t size) override {
    if (remaining <= 0) return nullptr;
    --remaining;
    return malloc(size);
  }
  void deallocate(void* pointer) override { free(pointer); }
  void* reallocate(void* pointer, const size_t size) override {
    if (remaining <= 0) return nullptr;
    --remaining;
    return realloc(pointer, size);
  }
};

class PreachingPassagesIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingPassagesIo, CountsEveryPickedTagIncludingThoseWithNoPassages) {
  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]},)"
           R"({"u":"v:1:1:2:0","s":10,"r":"R2","t":[3,4]},)"
           R"({"u":"v:1:1:3:0","s":10,"r":"R3","t":[4]})");
  std::vector<PreachingPassages::TagCount> counts;
  ASSERT_EQ(PreachingPassages::countByTag({tag(3), tag(4), tag(5)}, nullptr, counts), Status::Ok);
  ASSERT_EQ(counts.size(), 3u);
  EXPECT_EQ(counts[0].tag, tag(3));
  EXPECT_EQ(counts[0].passages, 2);
  EXPECT_EQ(counts[1].tag, tag(4));
  EXPECT_EQ(counts[1].passages, 2);
  EXPECT_EQ(counts[2].tag, tag(5));
  EXPECT_EQ(counts[2].passages, 0);
}

TEST_F(PreachingPassagesIo, CountsSkipUnaddressableRowsAndRepeatedTags) {
  putBible(R"({"u":"garbage","s":10,"r":"R1","t":[3]},)"
           R"({"u":"v:1:1:2:0","s":10,"r":"R2","t":[3,3]})");
  std::vector<PreachingPassages::TagCount> counts;
  ASSERT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Ok);
  ASSERT_EQ(counts.size(), 1u);
  EXPECT_EQ(counts[0].passages, 1);
}

TEST_F(PreachingPassagesIo, AMissingFileIsMissingNotZero) {
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Missing);
  EXPECT_TRUE(counts.empty());
  EXPECT_FALSE(PreachingPassages::isAvailable(Status::Missing));
}

TEST_F(PreachingPassagesIo, UnreadableFilesAreUnreadable) {
  for (const char* contents : {"", R"({"v":5,"p":[]})", R"({"v":4})", R"({"p":[]})", R"({"v":4,"p":[{"u":)"}) {
    storage_fake::putFile(BIBLE, contents);
    std::vector<PreachingPassages::TagCount> counts;
    EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Unreadable) << contents;
    EXPECT_TRUE(counts.empty()) << contents;
  }

  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]})");
  storage_fake::failReadsOf(BIBLE);
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Unreadable);
}

TEST_F(PreachingPassagesIo, CountsOutOfMemoryIsReported) {
  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]})");
  CountdownJsonAllocator exhausted;
  exhausted.remaining = 0;
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, &exhausted, counts), Status::OutOfMemory);
  EXPECT_TRUE(counts.empty());
}

TEST_F(PreachingPassagesIo, CountsNeverWriteOrAdoptATemp) {
  const std::string tmp = BIBLE + ".tmp";
  storage_fake::putFile(tmp, R"({"v":4,"p":[]})");
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Missing);
  EXPECT_FALSE(storage_fake::fileBytes(BIBLE).has_value());
  EXPECT_TRUE(storage_fake::fileBytes(tmp).has_value());

  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]})");
  const auto before = storage_fake::fileBytes(BIBLE);
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Ok);
  EXPECT_EQ(storage_fake::fileBytes(BIBLE), before);
}

}  // namespace
```

Edit `test/storage_io/CMakeLists.txt`. Replace:

```cmake
  PreachingTopicsFileTest.cpp
```

with:

```cmake
  PreachingTopicsFileTest.cpp
  PreachingPassagesTest.cpp
```

and replace:

```cmake
  ${REPO_ROOT}/src/study/PreachingTopicsFile.cpp
```

with:

```cmake
  ${REPO_ROOT}/src/study/PreachingTopicsFile.cpp
  ${REPO_ROOT}/src/study/PreachingPassages.cpp
```

### Step 4.2: confirm the tests fail

Run the host-test commands. Expect the build to fail because `PreachingPassages.h` does not exist.

### Step 4.3: implement the shared read and the counts pass

Create `src/study/PreachingPassages.h`:

```cpp
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"
#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"

// Read-only passes over the Bible's tagged passages, /.berean/passages/bible.json,
// for Predicación's Mis temas. Both stream the file through an ArduinoJson filter.
// Neither writes it, nor renames a leftover .tmp beside it.
//
// The firmware must pass PsramJsonAllocator::json() and ::passageDoc():
// ArduinoJson's pool blocks are under the 4,096-byte PSRAM routing threshold, so
// a nullptr on the device would parse into internal SRAM.
namespace PreachingPassages {

enum class Status : uint8_t { Ok, Missing, Unreadable, OutOfMemory };

// Missing and Unreadable both mean "cannot read the verses" to the UI; they stay
// apart for the log.
constexpr bool isAvailable(const Status status) { return status == Status::Ok; }

struct TagCount {
  study::TagId tag;
  uint16_t passages;
};

struct Passage {
  std::string reference;
  study::PassageText text;  // the whole text, else the snippet
  study::Unit start;
  uint16_t spine = 0;  // documentSpine, a hint
};

// One count per entry of `tags`, in the same order, zero included. `out` is
// empty unless the result is Ok.
Status countByTag(const std::vector<study::TagId>& tags, ArduinoJson::Allocator* json, std::vector<TagCount>& out);

// Every Bible passage carrying `tag`, by spine, then start Unit. `out` is empty
// unless the result is Ok.
Status passagesWithTag(study::TagId tag, const study::PassageDoc::Allocators& allocators,
                       std::vector<Passage>& out);

}  // namespace PreachingPassages
```

Create `src/study/PreachingPassages.cpp`:

```cpp
#include "PreachingPassages.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <FormatVersion.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>

#include <algorithm>
#include <cstdio>
#include <optional>

#include "StudyStore/PubKey.h"

namespace PreachingPassages {
namespace {

constexpr const char* MODULE = "PREACH";
constexpr size_t READ_BUFFER_BYTES = 512;
// The Home verse scan's cap (StudyPassageScan.cpp): the store's budget plus slack.
constexpr size_t MAX_FILE_BYTES = study::PassageDoc::SAVE_BYTE_BUDGET + 4096;
constexpr size_t PATH_BYTES = sizeof(sdpaths::PASSAGES_DIR) + 16;

// The parser pulls one character per read() and every HalFile::read takes
// storageMutex, hence the buffer. PersistableStore.cpp's twin is file-local.
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

struct RowTags {
  study::TagId ids[study::PassageDoc::MAX_TAGS_PER_PASSAGE];
  size_t count = 0;

  bool carries(const study::TagId id) const { return std::find(ids, ids + count, id) != ids + count; }
};

JsonDocument newDocument(ArduinoJson::Allocator* allocator) {
  return allocator ? JsonDocument(allocator) : JsonDocument();
}

void biblePassagesPath(char* out, const size_t outSize) {
  snprintf(out, outSize, "%s/%s.json", sdpaths::PASSAGES_DIR, study::BIBLE_PUB_KEY);
}

// Both passes parse through here, so the filtered deserializeJson template is
// instantiated once.
Status readFiltered(JsonDocument& doc, const JsonDocument& filter) {
  if (filter.overflowed()) {
    LOG_ERR(MODULE, "OOM: passage filter");
    return Status::OutOfMemory;
  }
  char path[PATH_BYTES];
  biblePassagesPath(path, sizeof(path));
  // Checked first: a failed open logs, and a Bible with nothing tagged is ordinary.
  if (!Storage.exists(path)) return Status::Missing;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return Status::Unreadable;
  const size_t bytes = file.size();
  if (bytes == 0 || bytes > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "%s: %u bytes, outside 1..%u", path, static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_FILE_BYTES));
    return Status::Unreadable;
  }

  serialization::BufferedFileReader buffered(file, READ_BUFFER_BYTES);
  JsonFileReader reader(buffered);
  const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  if (error == DeserializationError::NoMemory || doc.overflowed()) {
    LOG_ERR(MODULE, "OOM: parsing %s", path);
    return Status::OutOfMemory;
  }
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", path, error.c_str());
    return Status::Unreadable;
  }
  const int version = doc["v"] | 0;
  if (!persist::isKnownFormatVersion(version, study::PassageDoc::FORMAT_VERSION) || !doc["p"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "%s: unknown passage format v%d", path, version);
    return Status::Unreadable;
  }
  return Status::Ok;
}

// PassageDoc::fromJson skips a row whose start does not parse.
std::optional<study::Unit> rowStart(const JsonVariantConst row) { return study::unitFromCompact(row["u"] | ""); }

// As PassageDoc::fromJson and its normaliseTags read "t": no 0, nothing past
// UINT16_MAX, no repeats, at most MAX_TAGS_PER_PASSAGE.
RowTags rowTags(const JsonVariantConst row) {
  RowTags tags;
  for (const JsonVariantConst stored : row["t"].as<JsonArrayConst>()) {
    if (tags.count == study::PassageDoc::MAX_TAGS_PER_PASSAGE) break;
    const uint32_t raw = stored | 0u;
    if (raw == 0 || raw > UINT16_MAX) continue;
    const study::TagId id = study::toTagId(static_cast<uint16_t>(raw));
    if (!tags.carries(id)) tags.ids[tags.count++] = id;
  }
  return tags;
}

}  // namespace

Status countByTag(const std::vector<study::TagId>& tags, ArduinoJson::Allocator* json, std::vector<TagCount>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["p"][0]["u"] = true;
  filter["p"][0]["t"] = true;

  JsonDocument doc = newDocument(json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  out.reserve(tags.size());
  for (const study::TagId id : tags) out.push_back({id, 0});
  [[maybe_unused]] uint32_t rows = 0;
  for (const JsonVariantConst row : doc["p"].as<JsonArrayConst>()) {
    if (!rowStart(row)) continue;
    ++rows;
    const RowTags carried = rowTags(row);
    for (TagCount& count : out) {
      if (carried.carries(count.tag) && count.passages < UINT16_MAX) ++count.passages;
    }
  }
  LOG_INF(MODULE, "Counts: %u rows, %u tags, %lu ms", static_cast<unsigned>(rows),
          static_cast<unsigned>(tags.size()), millis() - started);
  return Status::Ok;
}

Status passagesWithTag(study::TagId, const study::PassageDoc::Allocators&, std::vector<Passage>& out) {
  out.clear();
  return Status::Unreadable;
}

}  // namespace PreachingPassages
```

### Step 4.4: run the tests

Run the host-test commands. Expect all 6 `PreachingPassagesIo` tests to pass, and 92 passed overall.

If `CountsOutOfMemoryIsReported` reports `Unreadable`, ArduinoJson returned `IncompleteInput` instead
of `NoMemory` for this case. Do not loosen the test. Print `error.c_str()` from the test's view
through a temporary `fprintf`, find the code ArduinoJson 7.4.2 actually returns, and report it
before changing anything. The spec (§3, step 4) maps only `NoMemory` and `overflowed()` to
`OutOfMemory`.

### Step 4.5: commit

```bash
git add src/study/PreachingPassages.h src/study/PreachingPassages.cpp \
        test/storage_io/PreachingPassagesTest.cpp test/storage_io/CMakeLists.txt
git commit -m "feat(preaching): count the Bible passages behind each topic

Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE"
```

---

## Task 5: `PreachingPassages`, one tag's passages

### Step 5.1: write the failing tag-passages tests

In `test/storage_io/PreachingPassagesTest.cpp`, insert just before the final `}  // namespace`:

```cpp
void* failingTextAllocate(size_t) { return nullptr; }

TEST_F(PreachingPassagesIo, ATagsPassagesComeInBibleOrder) {
  putBible(R"({"u":"v:40:1:2:0","s":40,"r":"Ref 3","w":"third","t":[7]},)"
           R"({"u":"v:1:1:1:0","s":12,"r":"Ref 1","w":"first","t":[7]},)"
           R"({"u":"v:40:1:1:0","s":40,"r":"Ref 2","w":"second","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;
  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  ASSERT_EQ(passages.size(), 3u);
  EXPECT_EQ(passages[0].reference, "Ref 1");
  EXPECT_EQ(passages[1].reference, "Ref 2");
  EXPECT_EQ(passages[2].reference, "Ref 3");
  EXPECT_EQ(passages[0].spine, 12);
  EXPECT_EQ(passages[0].start, (study::Unit{study::UnitKind::Verse, 1, 1, 1, 0}));
  EXPECT_EQ(passages[2].text.view(), "third");
}

TEST_F(PreachingPassagesIo, ATextFallsBackToTheSnippet) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"Whole","x":"short","w":"the whole text","t":[7]},)"
           R"({"u":"v:1:1:2:0","s":12,"r":"Legacy","x":"snippet only","t":[7]},)"
           R"({"u":"v:1:1:3:0","s":12,"r":"Bare","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;
  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  ASSERT_EQ(passages.size(), 3u);
  EXPECT_EQ(passages[0].text.view(), "the whole text");
  EXPECT_EQ(passages[1].text.view(), "snippet only");
  EXPECT_EQ(passages[2].text.view(), "");
  EXPECT_EQ(passages[2].reference, "Bare");
}

TEST_F(PreachingPassagesIo, OnlyPassagesCarryingTheTagAreReturned) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"Mine","w":"a","t":[3,7]},)"
           R"({"u":"v:1:1:2:0","s":12,"r":"Other","w":"b","t":[3]},)"
           R"({"u":"garbage","s":12,"r":"Unaddressable","w":"c","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;
  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  ASSERT_EQ(passages.size(), 1u);
  EXPECT_EQ(passages[0].reference, "Mine");

  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(9), study::PassageDoc::Allocators{}, passages), Status::Ok);
  EXPECT_TRUE(passages.empty());
}

TEST_F(PreachingPassagesIo, ATagsPassagesReportMissingAndUnreadable) {
  std::vector<PreachingPassages::Passage> passages;
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Missing);
  EXPECT_TRUE(passages.empty());

  storage_fake::putFile(BIBLE, R"({"v":5,"p":[]})");
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages),
            Status::Unreadable);
  EXPECT_TRUE(passages.empty());
}

TEST_F(PreachingPassagesIo, ATagsPassagesOutOfMemoryIsReported) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"R","w":"text","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;

  CountdownJsonAllocator exhausted;
  exhausted.remaining = 0;
  const study::PassageDoc::Allocators noJson{study::defaultTextAllocator(), &exhausted};
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), noJson, passages), Status::OutOfMemory);
  EXPECT_TRUE(passages.empty());

  const study::PassageDoc::Allocators noText{study::TextAllocator{failingTextAllocate, study::freeText}, nullptr};
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), noText, passages), Status::OutOfMemory);
  EXPECT_TRUE(passages.empty());
}

TEST_F(PreachingPassagesIo, ATagsPassagesNeverWriteTheFile) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"R","w":"text","t":[7]})");
  const auto before = storage_fake::fileBytes(BIBLE);
  std::vector<PreachingPassages::Passage> passages;
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  EXPECT_EQ(storage_fake::fileBytes(BIBLE), before);
  EXPECT_FALSE(storage_fake::fileBytes(BIBLE + ".tmp").has_value());
}
```

### Step 5.2: confirm the tests fail

Run the host-test commands with `--gtest_filter='PreachingPassages*'`. Expect it to build, and expect
these to fail because the stub returns `Unreadable`:

- `ATagsPassagesComeInBibleOrder`;
- `ATextFallsBackToTheSnippet`;
- `OnlyPassagesCarryingTheTagAreReturned`;
- `ATagsPassagesReportMissingAndUnreadable` (the `Missing` half);
- `ATagsPassagesOutOfMemoryIsReported`;
- `ATagsPassagesNeverWriteTheFile`.

### Step 5.3: implement the tag pass

In `src/study/PreachingPassages.cpp`, add `#include <Utf8.h>` after `#include <SdPaths.h>`, and
`#include <utility>` after `#include <optional>`.

Inside the anonymous namespace, insert this just before its closing `}  // namespace`, after
`rowTags`:

```cpp
// As PassageDoc::fromJson and StudyPassageScan read "s": absent, or past UINT16_MAX, is 0.
uint16_t spineHint(const JsonVariantConst row) {
  const uint32_t raw = row["s"] | 0u;
  return raw <= UINT16_MAX ? static_cast<uint16_t>(raw) : 0;
}

// Separate from passagesWithTag so the parse document is released before the sort.
Status collectPassages(const study::TagId tag, const study::PassageDoc::Allocators& allocators,
                       std::vector<Passage>& out, uint32_t& rowsSeen) {
  JsonDocument filter;
  filter["v"] = true;
  for (const char* key : {"u", "s", "x", "w", "r", "t"}) filter["p"][0][key] = true;

  JsonDocument doc = newDocument(allocators.json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst rows = doc["p"].as<JsonArrayConst>();
  size_t matching = 0;
  for (const JsonVariantConst row : rows) {
    ++rowsSeen;
    if (rowStart(row) && rowTags(row).carries(tag)) ++matching;
  }
  out.reserve(matching);

  for (const JsonVariantConst row : rows) {
    const std::optional<study::Unit> start = rowStart(row);
    if (!start || !rowTags(row).carries(tag)) continue;

    Passage passage{utf8SafeSummary(row["r"] | "", study::PassageDoc::MAX_REFERENCE_BYTES),
                    study::PassageText(allocators.text), *start, spineHint(row)};
    const char* whole = row["w"] | "";
    const bool assigned = whole[0] != '\0'
                              ? passage.text.assign(whole)
                              : passage.text.assign(utf8SafeSummary(row["x"] | "", study::PassageDoc::MAX_SNIPPET_BYTES));
    if (!assigned) {
      LOG_ERR(MODULE, "OOM: passage text for tag %u", static_cast<unsigned>(study::toRaw(tag)));
      out.clear();
      return Status::OutOfMemory;
    }
    out.push_back(std::move(passage));
  }
  return Status::Ok;
}
```

Then replace the stub:

```cpp
Status passagesWithTag(study::TagId, const study::PassageDoc::Allocators&, std::vector<Passage>& out) {
  out.clear();
  return Status::Unreadable;
}
```

with:

```cpp
Status passagesWithTag(const study::TagId tag, const study::PassageDoc::Allocators& allocators,
                       std::vector<Passage>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();
  uint32_t rowsSeen = 0;
  const Status status = collectPassages(tag, allocators, out, rowsSeen);
  if (status != Status::Ok) return status;

  // One key, spine then Unit: ordering verses by Unit and the rest by spine is not
  // transitive when the file mixes two editions' spine hints.
  std::stable_sort(out.begin(), out.end(), [](const Passage& a, const Passage& b) {
    if (a.spine != b.spine) return a.spine < b.spine;
    return a.start < b.start;
  });
  LOG_INF(MODULE, "Tag %u: %u of %u rows, %lu ms", static_cast<unsigned>(study::toRaw(tag)),
          static_cast<unsigned>(out.size()), static_cast<unsigned>(rowsSeen), millis() - started);
  return Status::Ok;
}
```

`rowsSeen` is read only by `LOG_INF`. If the firmware build warns that it is set but unused, mark it
`[[maybe_unused]]`.

### Step 5.4: run the tests

Run the host-test commands. Expect all 12 `PreachingPassagesIo` tests to pass, and 98 passed
overall.

### Step 5.5: commit

```bash
git add src/study/PreachingPassages.cpp test/storage_io/PreachingPassagesTest.cpp
git commit -m "feat(preaching): read one topic's Bible passages in Bible order

Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE"
```

---

## Task 6: format, firmware build, final check

### Step 6.1: format the whole tree

```bash
./bin/clang-format-fix
git status --short
```

It must be the unsuffixed form: CI formats the whole tree, and `-g` skips committed new files. If it
changed any of this task's files, re-run the host-test commands and expect 98 passed. Then commit:

```bash
git add lib/Serialization/SdPaths.h src/study/PreachingTopicsFile.h src/study/PreachingTopicsFile.cpp \
        src/study/PreachingPassages.h src/study/PreachingPassages.cpp \
        test/storage_io/PreachingTopicsFileTest.cpp test/storage_io/PreachingPassagesTest.cpp
git commit -m "style(preaching): apply clang-format

Claude-Session: https://claude.ai/code/session_01BaMk5RupZD8VMrn39WsjLE"
```

Skip the commit if nothing changed. If the formatter touched files outside this task, do not commit
them: `git checkout -- <those files>`, and report them.

### Step 6.2: build the firmware

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-15-pio.log | tail -20
grep -nE "warning:.*(Preaching|SdPaths)" /tmp/berean-o-15-pio.log
```

Expect `SUCCESS`, and no `grep` output: no warning in this task's files. Record the `Flash:` line
from the output, for A11's flash delta. If a warning names this task's files, fix it, re-run the
host tests and this build, and commit as `fix(preaching): …`.

Do not run two `pio run` processes at once in this project.

### Step 6.3: final host run

Run the host-test commands without a filter. Expect `98 tests from 8 test suites ran … [  PASSED  ]
98 tests.` and `git status --short` empty, with no untracked files of this task's.
