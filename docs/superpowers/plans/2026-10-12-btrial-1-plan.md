# Tag display order (btrial-1) Implementation Plan

**Goal:** `TagPalette` caches one display order (D2 collation), exposes it as `activeIdsByName()` and
`nameRank()`, and `StudyStore::activeTags()` / `tagNamesFor()` read tags in that order.

**Architecture:** `TagPalette::Entry` gains a `rank`, and the palette gains `byName_`, the entry
indices in display order. `rebuildDisplayOrder()` keeps the two in step and runs only after an
appending `add` and an accepted `fromJson`.

- The rebuild folds each name into a zero-padded 24-byte key in one nothrow `SortKey[n]` buffer,
  then sorts by key, raw name and id.
- If that buffer cannot be allocated, it logs and falls back to allocation order.
- A header-only helper, `study::inDisplayOrder`, orders a passage's ≤ 8 tags for `tagNamesFor`, so
  the ordering is host-testable.

**Tech stack:** C++20 (GNU++20 on device), ArduinoJson 7.4.2, GoogleTest 1.17.0, CMake host suite,
and PlatformIO 6.1.19 with xtensa GCC 14.2.0 for the `x4pro` firmware.

**Spec:** `docs/superpowers/specs/2026-10-12-btrial-1-design.md`. It is the authority, and its
assumptions A1–A23 are referenced below. Read its Architecture section once before Task 2.

FILES: lib/StudyStore/StudyStore/TagPalette.h, lib/StudyStore/StudyStore/TagPalette.cpp, lib/StudyStore/StudyStore/TagDisplayOrder.h
FILES: src/study/StudyStore.h, src/study/StudyStore.cpp
FILES: test/tag_palette/CMakeLists.txt, test/tag_palette/TagPaletteTest.cpp, test/tag_palette/TagDisplayOrderTest.cpp, test/tag_palette/FailingNothrowNew.h, test/tag_palette/FailingNothrowNew.cpp
FILES: test/migration_planner/CMakeLists.txt, test/passage_doc/CMakeLists.txt, test/storage_io/CMakeLists.txt

---

## Before you start

**Rules.** Each one comes from the repo's `CLAUDE.md` or this pipeline. Do not relax any of them.

- **Work only in the worktree,** `/Volumes/stein/.herdr/worktrees/berean-os/feature-tag-order-rank`,
  on branch `feature/tag-order-rank`.
- **Commit after each task. Never push,** never open a PR, never run `hpipe submit`, and never run
  `bd` or `bv`.
- **Every commit message ends with this trailer line:**
  `Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG`. Add no co-author line.
- **Comments:** only a non-obvious *why*, a field's meaning, or a gotcha. Never a comment that
  restates the next line. The code in this plan already follows that; copy it as written.
- **Formatting:** only through `./bin/clang-format-fix`. Never call `clang-format` directly.
- **Re-read every file before you edit it.** A sibling task may have changed it since this plan
  was written. Each edit below gives the exact text to find. If that text is not there, stop and
  report; do not improvise a merge.
- **Stage only the paths each task names.** Never `git add -A`, and never a bare `git commit -a`.

**Tools on this machine.**

- `pio` is not on `PATH`: use `~/.platformio/penv/bin/pio`.
- The host test build is configured at `build/test` (gitignored). If it is missing, run
  `cmake -S test -B build/test` first.
- Scratch files (logs, the pristine copy in Task 3) go in this session's scratch directory,
  `/private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad`,
  which the commands below spell out in full. Never put them in the repo.

**Two traps, both hit while this plan was being proven.**

1. **A stale object hides a revert.** The host build uses `/usr/bin/make`, GNU Make 3.81, which
   compares timestamps to the second. If you change a source file back within a second of the last
   build, `make` thinks the object is current and runs the old code. After any quick revert, run
   `touch <file>` and rebuild.
2. **Two firmware builds can race.** `pio run` shares ESP-IDF's per-user component cache
   (`~/Library/Caches/Espressif/ComponentManager/`) with sibling worktrees. If `pio run` fails with
   `index.lock': File exists` while another worktree is building, the other build is holding the
   cache. Wait for it to finish (`ps -axo pid,command | grep '[p]io run'`), then rerun. **Never
   delete the lock file**: a live sibling build owns it.

**Baseline, measured on 2026-10-12 at `2d86ae59`:**

- `TagPaletteTest` 14, `MigrationPlannerTest` 14, `PassageDocTest` 97, `StorageIoTest` 69, for 194
  in total.
- The full host suite was green.

---

## Task 0: Preflight

**Files:** none changed.

- [ ] **Step 1: Confirm the branch and a clean tree**

Run:
```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/feature-tag-order-rank
git branch --show-current
git status --short
```
Expected: `feature/tag-order-rank`, and no lines from `git status --short`. Stop if the tree is
dirty.

- [ ] **Step 2: Confirm the four target files still match this plan**

Run:
```bash
grep -n 'activeIds\|nameRank\|activeIdsByName\|nextRaw_ = 1' lib/StudyStore/StudyStore/TagPalette.h
grep -n 'palette_.activeIds()\|tagNamesFor' src/study/StudyStore.cpp
```
Expected:
- `TagPalette.h` shows `std::vector<TagId> activeIds() const;` and
  `uint16_t nextRaw_ = 1;  // 0 is UNLABELLED`, with no `nameRank` or `activeIdsByName`;
- `StudyStore.cpp` shows `palette_.activeIds()` inside `activeTags()` and the `tagNamesFor`
  definition.

If either function already exists, stop and report: someone has done part of this.

- [ ] **Step 3: Record the baseline**

Run:
```bash
cmake --build build/test --target TagPaletteTest MigrationPlannerTest PassageDocTest StorageIoTest -j 8 2>&1 | grep -E 'error|Built target (TagPalette|MigrationPlanner|PassageDoc|StorageIo)Test'
for b in tag_palette/TagPaletteTest migration_planner/MigrationPlannerTest passage_doc/PassageDocTest storage_io/StorageIoTest; do printf '%s: ' $b; build/test/$b | grep -E '^\[  PASSED|FAILED' | tr '\n' ' '; echo; done
```
Expected: four `Built target` lines, no `error`, and 14 / 14 / 97 / 69 passed.

---

## Task 1: Link the fold into the four TagPalette host targets

From Task 2 on, `TagPalette.cpp` includes `<BibleSearch/Fold.h>`, `<Logging.h>` and `<Memory.h>`. On
the host, `<Logging.h>` must be the no-op stub in `test/stubs`. Without `lib/Memory` on the include
path, `<Memory.h>` silently resolves to the C library's `<memory.h>`. This task only adds plumbing,
so nothing should change behaviour.

**Files:**
- Modify: `test/tag_palette/CMakeLists.txt` (whole file)
- Modify: `test/migration_planner/CMakeLists.txt`
- Modify: `test/passage_doc/CMakeLists.txt`
- Modify: `test/storage_io/CMakeLists.txt`

- [ ] **Step 1: Replace `test/tag_palette/CMakeLists.txt` with this exact content**

```cmake
# test/stubs supplies the no-op <Logging.h>. lib/Memory must be on the path, or
# <Memory.h> resolves to the C library's <memory.h>. Modelled on
# test/bible_search_index.
add_executable(TagPaletteTest
  TagPaletteTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
)

target_include_directories(TagPaletteTest PRIVATE
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/Fold.h>, as TagPalette.cpp includes it
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

target_link_libraries(TagPaletteTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(TagPaletteTest)
```

- [ ] **Step 2: Edit `test/migration_planner/CMakeLists.txt`**

Find:
```cmake
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
```
Replace with:
```cmake
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
```

Find:
```cmake
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Serialization
)
```
Replace with:
```cmake
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/Fold.h>, as TagPalette.cpp includes it
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)
```

- [ ] **Step 3: Edit `test/passage_doc/CMakeLists.txt`**

Find:
```cmake
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
```
Replace with:
```cmake
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
```

Find:
```cmake
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
)
```
Replace with:
```cmake
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/Fold.h>, as TagPalette.cpp includes it
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)
```

- [ ] **Step 4: Edit `test/storage_io/CMakeLists.txt`**

This target already has `test/stubs` and `lib/Memory`, and `test/stubs` must stay first.

Find:
```cmake
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PassageDoc.cpp
```
Replace with:
```cmake
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PassageDoc.cpp
```

Find:
```cmake
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/StudyStore
```
Replace with:
```cmake
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/BibleSearch  # <BibleSearch/Fold.h>, as TagPalette.cpp includes it
  ${REPO_ROOT}/lib/StudyStore
```

- [ ] **Step 5: Build the four targets and run them**

Run:
```bash
cmake --build build/test --target TagPaletteTest MigrationPlannerTest PassageDocTest StorageIoTest -j 8 2>&1 | grep -E 'error|Built target (TagPalette|MigrationPlanner|PassageDoc|StorageIo)Test'
for b in tag_palette/TagPaletteTest migration_planner/MigrationPlannerTest passage_doc/PassageDocTest storage_io/StorageIoTest; do printf '%s: ' $b; build/test/$b | grep -E '^\[  PASSED|FAILED' | tr '\n' ' '; echo; done
```
Expected: four `Built target` lines, no `error`, and 14 / 14 / 97 / 69 passed, unchanged from
Task 0. CMake re-runs its configure step on its own, because the `CMakeLists.txt` files changed.

- [ ] **Step 6: Commit**

```bash
git add test/tag_palette/CMakeLists.txt test/migration_planner/CMakeLists.txt test/passage_doc/CMakeLists.txt test/storage_io/CMakeLists.txt
git commit -F - <<'EOF'
test: link the fold into the TagPalette host targets

TagPalette.cpp is about to fold names with BibleSearch::foldAppend and
log through Logging.h. Every host target that compiles it gains Fold.cpp,
lib/BibleSearch, lib/Memory and the no-op Logging stub.

Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG
EOF
```

---

## Task 2: The display order in `TagPalette`

**Files:**
- Modify: `test/tag_palette/TagPaletteTest.cpp` (tests first)
- Modify: `lib/StudyStore/StudyStore/TagPalette.h`
- Modify: `lib/StudyStore/StudyStore/TagPalette.cpp`

- [ ] **Step 1: Add the helpers to `test/tag_palette/TagPaletteTest.cpp`**

Find (the top of the file):
```cpp
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include "StudyStore/TagPalette.h"

namespace {
```
Replace with:
```cpp
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "StudyStore/TagPalette.h"

namespace {

struct Row {
  uint16_t id;
  const char* name;
  bool retired = false;
};

JsonDocument paletteJson(std::initializer_list<Row> rows) {
  JsonDocument doc;
  doc["v"] = study::TagPalette::FORMAT_VERSION;
  const auto tags = doc["t"].to<JsonArray>();
  for (const Row& r : rows) {
    const auto row = tags.add<JsonObject>();
    row["i"] = r.id;
    row["n"] = r.name;
    if (r.retired) row["r"] = true;
  }
  return doc;
}

study::TagPalette loaded(std::initializer_list<Row> rows) {
  const JsonDocument doc = paletteJson(rows);
  study::TagPalette p;
  EXPECT_TRUE(p.fromJson(doc.as<JsonVariantConst>()));
  return p;
}

std::vector<uint16_t> rawIds(const std::vector<study::TagId>& ids) {
  std::vector<uint16_t> out;
  out.reserve(ids.size());
  for (const study::TagId id : ids) out.push_back(study::toRaw(id));
  return out;
}

std::vector<std::string> namesInDisplayOrder(const study::TagPalette& p) {
  std::vector<std::string> out;
  for (const study::TagId id : p.activeIdsByName()) out.push_back(p.name(id));
  return out;
}

using Names = std::vector<std::string>;
```

- [ ] **Step 2: Append the ordering and rank tests**

`IgnoresCase` adds `alfa` *before* `Alfa` (plan review 0, MAJOR 1). In the other order, the id
tiebreak alone gives the same result, and the raw-name level would go untested. Task 3's M6 proves
the test now reaches that level.

At the bottom of `test/tag_palette/TagPaletteTest.cpp`, find:
```cpp
TEST(TagPaletteRoundTrip, HoldsTheUsersRealPaletteSize) {
  study::TagPalette p;
  for (int i = 0; i < 48; ++i) ASSERT_TRUE(p.add("etiqueta" + std::to_string(i)).has_value());
  JsonDocument doc;
  p.toJson(doc);
  EXPECT_LT(measureJson(doc), 4096u) << "the palette must stay comfortably inside any store budget";
}

}  // namespace
```
Replace with:
```cpp
TEST(TagPaletteRoundTrip, HoldsTheUsersRealPaletteSize) {
  study::TagPalette p;
  for (int i = 0; i < 48; ++i) ASSERT_TRUE(p.add("etiqueta" + std::to_string(i)).has_value());
  JsonDocument doc;
  p.toJson(doc);
  EXPECT_LT(measureJson(doc), 4096u) << "the palette must stay comfortably inside any store budget";
}

TEST(TagPaletteOrder, IgnoresCase) {
  study::TagPalette p;
  p.add("beta");
  p.add("alfa");
  p.add("Alfa");
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{3, 2, 1}))
      << "equal folded keys tie on the raw bytes, so 'Alfa' precedes 'alfa' though allocated later";
}

TEST(TagPaletteOrder, IgnoresAccents) {
  study::TagPalette p;
  p.add("éxodo");
  p.add("esperanza");
  p.add("Ezequiel");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"esperanza", "éxodo", "Ezequiel"}));
}

TEST(TagPaletteOrder, FoldsEnyeIntoN) {
  study::TagPalette p;
  p.add("anzuelo");
  p.add("año");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"año", "anzuelo"}));

  study::TagPalette q;
  q.add("nube");
  q.add("ñu");
  EXPECT_EQ(namesInDisplayOrder(q), (Names{"ñu", "nube"}));
}

TEST(TagPaletteOrder, SkipsOneLeadingInvertedMark) {
  study::TagPalette p;
  p.add("reino");
  p.add("¿Por qué?");
  p.add("paz");
  p.add("¡Ánimo!");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"¡Ánimo!", "paz", "¿Por qué?", "reino"}))
      << "unskipped, both marks pass through the fold and sort after z";
}

TEST(TagPaletteOrder, SkipsOnlyTheFirstMark) {
  study::TagPalette p;
  p.add("¿¡Hola");
  p.add("zeta");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"zeta", "¿¡Hola"}));
}

// Accepted with D2: digits sort as text.
TEST(TagPaletteOrder, SortsDigitsAsText) {
  study::TagPalette p;
  p.add("2 Reyes");
  p.add("10 mandamientos");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"10 mandamientos", "2 Reyes"}));
}

// fromJson dedupes by id only, so a file can hold two entries with one name.
TEST(TagPaletteOrder, BreaksANameTieOnTheId) {
  const study::TagPalette p = loaded({{7, "fe"}, {3, "fe"}});
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{3, 7}));
}

TEST(TagPaletteOrder, BreaksATruncatedNameTieOnTheId) {
  const study::TagPalette p = loaded({{9, "abcdefghijklmnopqrstuvwxBBBBBB"}, {4, "abcdefghijklmnopqrstuvwxAAAAAA"}});
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{4, 9}))
      << "fromJson cuts both names to the same 24 bytes";
}

TEST(TagPaletteOrder, ExcludesRetiredTags) {
  study::TagPalette p;
  p.add("gamma");
  const auto beta = p.add("beta");
  p.add("alfa");
  p.retire(*beta);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "gamma"}));
}

TEST(TagPaletteOrder, LeavesActiveIdsInAllocationOrder) {
  study::TagPalette p;
  p.add("zeta");
  p.add("alfa");
  EXPECT_EQ(rawIds(p.activeIds()), (std::vector<uint16_t>{1, 2}));
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{2, 1}));
}

TEST(TagPaletteOrder, IsEmptyForAnEmptyPalette) {
  EXPECT_TRUE(study::TagPalette{}.activeIdsByName().empty());
  EXPECT_TRUE(loaded({}).activeIdsByName().empty());
}

TEST(TagPaletteRank, OrdersRetiredIds) {
  study::TagPalette p;
  const auto gamma = p.add("gamma");
  const auto beta = p.add("beta");
  const auto alfa = p.add("alfa");
  p.retire(*beta);
  EXPECT_LT(p.nameRank(*alfa), p.nameRank(*beta));
  EXPECT_LT(p.nameRank(*beta), p.nameRank(*gamma)) << "a passage still carrying a retired tag lists it in place";
}

TEST(TagPaletteRank, IsMaxForUnlabelledAndUnknownIds) {
  study::TagPalette p;
  p.add("alfa");
  EXPECT_EQ(p.nameRank(study::UNLABELLED), UINT16_MAX);
  EXPECT_EQ(p.nameRank(study::toTagId(999)), UINT16_MAX);
}

TEST(TagPaletteRank, IsRebuiltByAnAppendingAdd) {
  study::TagPalette p;
  p.add("mu");
  p.add("alfa");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "mu"}));
}

TEST(TagPaletteRank, IsRebuiltByFromJson) {
  study::TagPalette p;
  p.add("mu");
  const JsonDocument doc = paletteJson({{1, "zeta"}, {2, "alfa"}});
  ASSERT_TRUE(p.fromJson(doc.as<JsonVariantConst>()));
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "zeta"}));
}

TEST(TagPaletteRank, SurvivesACopy) {
  study::TagPalette p;
  const auto zeta = p.add("zeta");
  const auto mu = p.add("mu");
  const auto alfa = p.add("alfa");
  p.retire(*mu);

  const study::TagPalette q = p;
  EXPECT_EQ(rawIds(q.activeIdsByName()), rawIds(p.activeIdsByName()));
  for (const auto id : {*zeta, *mu, *alfa}) EXPECT_EQ(q.nameRank(id), p.nameRank(id));
}

TEST(TagPaletteRank, RetireAndReviveLeaveRanksUnchanged) {
  study::TagPalette p;
  const auto gamma = p.add("gamma");
  const auto alfa = p.add("alfa");
  const auto beta = p.add("beta");
  const std::vector<uint16_t> before{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)};

  p.retire(*alfa);
  EXPECT_EQ((std::vector<uint16_t>{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)}), before);

  EXPECT_EQ(p.add("alfa"), alfa);
  EXPECT_EQ((std::vector<uint16_t>{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)}), before);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "beta", "gamma"}));
}

}  // namespace
```

- [ ] **Step 3: Build to see it fail**

Run:
```bash
cmake --build build/test --target TagPaletteTest 2>&1 | grep -E 'error' | head -3
```
Expected: compile errors such as `error: no member named 'activeIdsByName' in 'study::TagPalette'`.

- [ ] **Step 4: Declare the API and the cache in `lib/StudyStore/StudyStore/TagPalette.h`**

Find:
```cpp
  // Active tags in allocation order, for a picker.
  std::vector<TagId> activeIds() const;
```
Replace with:
```cpp
  // Active tags in allocation order, for a picker.
  std::vector<TagId> activeIds() const;

  // Active tags in display order: case and accents ignored, ñ read as n, one leading ¿ or ¡
  // skipped, then the raw name, then the id.
  std::vector<TagId> activeIdsByName() const;

  // An id's position in display order among all entries, retired ones included, so a passage
  // that still carries a retired tag lists it in place. UINT16_MAX for UNLABELLED and unknown ids.
  uint16_t nameRank(TagId id) const;
```

Find:
```cpp
  struct Entry {
    TagId id{};
    std::string name;
    bool active = true;
  };

  std::vector<Entry> entries_;
  uint16_t nextRaw_ = 1;  // 0 is UNLABELLED
```
Replace with:
```cpp
  struct Entry {
    TagId id{};
    std::string name;
    bool active = true;
    uint16_t rank = 0;  // position in display order among all entries
  };

  // Only a change to the set of names can move a rank, so only an appending add and fromJson
  // call this; retire and a revive leave the order as it is.
  void rebuildDisplayOrder();

  std::vector<Entry> entries_;
  std::vector<uint16_t> byName_;  // entry indices in display order
  uint16_t nextRaw_ = 1;          // 0 is UNLABELLED
```

`rank` goes last so the two existing brace initialisers, `{id, name, true}` and
`{tagId, std::move(text), …}`, stay valid. Both compile without warnings under host `-Wextra` and
xtensa `-Wall -Wextra` (spec A2).

- [ ] **Step 5: Implement it in `lib/StudyStore/StudyStore/TagPalette.cpp`**

Find (the top of the file):
```cpp
#include "StudyStore/TagPalette.h"

#include <FormatVersion.h>

#include <algorithm>

namespace study {
namespace {
const std::string kEmpty;
}  // namespace
```
Replace with:
```cpp
#include "StudyStore/TagPalette.h"

#include <BibleSearch/Fold.h>
#include <FormatVersion.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace study {
namespace {

const std::string kEmpty;

// A fold is never longer than its input, and a name is at most MAX_TAG_NAME_BYTES.
constexpr size_t KEY_BYTES = TagPalette::MAX_TAG_NAME_BYTES;

struct SortKey {
  char key[KEY_BYTES];  // folded name, zero-padded
  uint16_t index;       // into entries_
};

// The fold passes ¿ (C2 BF) and ¡ (C2 A1) through unchanged, so a name opening with one would sort
// after z.
std::string_view withoutLeadingInvertedMark(std::string_view name) {
  if (name.size() >= 2 && static_cast<unsigned char>(name[0]) == 0xC2) {
    const auto second = static_cast<unsigned char>(name[1]);
    if (second == 0xBF || second == 0xA1) name.remove_prefix(2);
  }
  return name;
}

void foldKey(const std::string& name, std::string& scratch, char (&key)[KEY_BYTES]) {
  scratch.clear();
  BibleSearch::foldAppend(scratch, withoutLeadingInvertedMark(name));
  memcpy(key, scratch.data(), std::min(scratch.size(), KEY_BYTES));
}

}  // namespace
```

The log module is a literal `"TAGS"`, as `lib/` code writes it (`VerseTextScanner.cpp:311`,
`LOG_ERR("BSRCH", …)`). A `MODULE` constant would warn `-Wunused-const-variable` on the host, where
`LOG_ERR` expands to nothing.

Find (in `add`):
```cpp
  const TagId id = toTagId(nextRaw_++);
  entries_.push_back({id, name, true});
  return id;
```
Replace with:
```cpp
  const TagId id = toTagId(nextRaw_++);
  entries_.push_back({id, name, true});
  rebuildDisplayOrder();
  return id;
```

Find:
```cpp
std::vector<TagId> TagPalette::activeIds() const {
  std::vector<TagId> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_) {
    if (e.active) out.push_back(e.id);
  }
  return out;
}
```
Replace with:
```cpp
std::vector<TagId> TagPalette::activeIds() const {
  std::vector<TagId> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_) {
    if (e.active) out.push_back(e.id);
  }
  return out;
}

std::vector<TagId> TagPalette::activeIdsByName() const {
  std::vector<TagId> out;
  out.reserve(activeCount());
  for (const uint16_t index : byName_) {
    if (entries_[index].active) out.push_back(entries_[index].id);
  }
  return out;
}

uint16_t TagPalette::nameRank(const TagId id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return e.rank;
  }
  return UINT16_MAX;
}

void TagPalette::rebuildDisplayOrder() {
  const size_t count = entries_.size();
  byName_.resize(count);
  if (count == 0) return;

  const auto keys = makeUniqueNoThrow<SortKey[]>(count);
  if (!keys) {
    LOG_ERR("TAGS", "OOM: %zu B for the tag display order; showing allocation order", count * sizeof(SortKey));
    for (size_t i = 0; i < count; ++i) {
      entries_[i].rank = static_cast<uint16_t>(i);
      byName_[i] = static_cast<uint16_t>(i);
    }
    return;
  }

  std::string scratch;
  scratch.reserve(KEY_BYTES);
  for (size_t i = 0; i < count; ++i) {
    keys[i].index = static_cast<uint16_t>(i);
    foldKey(entries_[i].name, scratch, keys[i].key);
  }

  std::sort(keys.get(), keys.get() + count, [this](const SortKey& a, const SortKey& b) {
    if (const int byKey = memcmp(a.key, b.key, KEY_BYTES)) return byKey < 0;
    const Entry& left = entries_[a.index];
    const Entry& right = entries_[b.index];
    if (left.name != right.name) return left.name < right.name;
    return toRaw(left.id) < toRaw(right.id);
  });

  for (size_t position = 0; position < count; ++position) {
    byName_[position] = keys[position].index;
    entries_[keys[position].index].rank = static_cast<uint16_t>(position);
  }
}
```

`makeUniqueNoThrow<SortKey[]>` value-initialises (`lib/Memory/Memory.h:33`), so every key starts
zeroed and the zero padding is free.

The fallback in the `!keys` branch belongs here, not later: the repo requires every fallible
allocation to be null-checked and logged (`CLAUDE.md` "Always use `makeUniqueNoThrow`"). Task 3
proves it.

Find (at the end of `fromJson`):
```cpp
  nextRaw_ = static_cast<uint16_t>(std::max<uint32_t>(stored, highest + 1u));
  return true;
```
Replace with:
```cpp
  nextRaw_ = static_cast<uint16_t>(std::max<uint32_t>(stored, highest + 1u));
  rebuildDisplayOrder();
  return true;
```

Do **not** add `rebuildDisplayOrder()` anywhere else: not in `retire`, not in the revive branch of
`add`, and not before `fromJson`'s early `return false`s (spec A8).

- [ ] **Step 6: Build and run**

Run:
```bash
cmake --build build/test --target TagPaletteTest 2>&1 | grep -E 'error|warning' | head
build/test/tag_palette/TagPaletteTest | grep -E '^\[  PASSED|FAILED'
```
Expected: no `error` or `warning` lines, and `[  PASSED  ] 31 tests.` (14 old + 17 new).

- [ ] **Step 7: Run the other three targets that compile `TagPalette.cpp`**

Run:
```bash
cmake --build build/test --target MigrationPlannerTest PassageDocTest StorageIoTest -j 8 2>&1 | grep -E 'error' | head -3
for b in migration_planner/MigrationPlannerTest passage_doc/PassageDocTest storage_io/StorageIoTest; do printf '%s: ' $b; build/test/$b | grep -E '^\[  PASSED|FAILED' | tr '\n' ' '; echo; done
```
Expected: no `error`, and 14 / 97 / 69 passed.

- [ ] **Step 8: Commit**

```bash
git add lib/StudyStore/StudyStore/TagPalette.h lib/StudyStore/StudyStore/TagPalette.cpp test/tag_palette/TagPaletteTest.cpp
git commit -F - <<'EOF'
feat: order tags by name in TagPalette

Adds activeIdsByName() and nameRank(): a cached display order that
ignores case and accents, folds ñ into n, skips one leading ¿ or ¡, and
breaks ties on the raw name, then the id. It is rebuilt only by an
appending add and an accepted fromJson. If the key buffer cannot be
allocated, it falls back to allocation order.

Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG
EOF
```

---

## Task 3: Rebuild triggers and the OOM fallback

The behaviour exists after Task 2, so these tests pass on their first run. Step 6 proves that they
can fail. A rebuild with unchanged names produces identical ranks, so "not rebuilt" can only be
observed by counting allocations. The rebuild's `SortKey[n]` buffer is a palette's only nothrow
allocation:

- `std::string` and `std::vector` use the throwing form, which the injector never fails.
- ArduinoJson allocates with `malloc`.

Failing the 1st nothrow allocation inside a guard therefore means "a rebuild ran".

**Files:**
- Create: `test/tag_palette/FailingNothrowNew.h`, `test/tag_palette/FailingNothrowNew.cpp` (copied)
- Modify: `test/tag_palette/CMakeLists.txt`
- Modify: `test/tag_palette/TagPaletteTest.cpp`

- [ ] **Step 1: Copy the injector**

Its header says it is confined to one suite, so each suite carries its own copy (spec A20).

Run:
```bash
cp test/ditherers/FailingNothrowNew.h test/ditherers/FailingNothrowNew.cpp test/tag_palette/
diff test/ditherers/FailingNothrowNew.h test/tag_palette/FailingNothrowNew.h && diff test/ditherers/FailingNothrowNew.cpp test/tag_palette/FailingNothrowNew.cpp && echo identical
```
Expected: `identical`. Do not edit the copies.

- [ ] **Step 2: Wire the injector and `-fno-builtin` into `test/tag_palette/CMakeLists.txt`**

Find:
```cmake
  TagPaletteTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
```
Replace with:
```cmake
  TagPaletteTest.cpp
  FailingNothrowNew.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
```

Find:
```cmake
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)
```
Replace with:
```cmake
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

# Clang may omit a replaceable operator new called from a new-expression when
# the result is only null-checked, zeroed and freed, so an injected failure
# would never be observed. -fno-builtin keeps every call real.
target_compile_options(TagPaletteTest PRIVATE -fno-builtin)
```

- [ ] **Step 3: Add the include and the guard helper to `test/tag_palette/TagPaletteTest.cpp`**

Find:
```cpp
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "StudyStore/TagPalette.h"
```
Replace with:
```cpp
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "FailingNothrowNew.h"
#include "StudyStore/TagPalette.h"
```

Find:
```cpp
using Names = std::vector<std::string>;
```
Replace with:
```cpp
using Names = std::vector<std::string>;

// The rank rebuild's key buffer is the only nothrow allocation a palette makes: strings and vectors
// use the throwing form, which the injector never fails, and ArduinoJson allocates with malloc.
// So "the Nth nothrow allocation fired" counts rebuilds. Assertions stay outside the guard.
template <typename Body>
bool nthNothrowAllocationFired(const std::size_t n, Body body) {
  failing_new::ScopedNthNothrowFailure failure(n);
  body();
  return failure.fired();
}
```

- [ ] **Step 4: Append the rebuild and OOM tests**

At the bottom of `test/tag_palette/TagPaletteTest.cpp`, find:
```cpp
  EXPECT_EQ(p.add("alfa"), alfa);
  EXPECT_EQ((std::vector<uint16_t>{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)}), before);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "beta", "gamma"}));
}

}  // namespace
```
Replace with:
```cpp
  EXPECT_EQ(p.add("alfa"), alfa);
  EXPECT_EQ((std::vector<uint16_t>{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)}), before);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "beta", "gamma"}));
}

TEST(TagPaletteRebuild, AnAppendingAddMakesExactlyOneNothrowAllocation) {
  study::TagPalette p;
  p.add("mu");
  EXPECT_TRUE(nthNothrowAllocationFired(1, [&] { p.add("alfa"); }));

  study::TagPalette q;
  q.add("mu");
  EXPECT_FALSE(nthNothrowAllocationFired(2, [&] { q.add("alfa"); }))
      << "the rebuild made more than one nothrow allocation";
}

TEST(TagPaletteRebuild, AcceptedFromJsonMakesExactlyOneNothrowAllocation) {
  const JsonDocument doc = paletteJson({{1, "zeta"}, {2, "alfa"}});
  study::TagPalette p;
  EXPECT_TRUE(nthNothrowAllocationFired(1, [&] { p.fromJson(doc.as<JsonVariantConst>()); }));

  study::TagPalette q;
  EXPECT_FALSE(nthNothrowAllocationFired(2, [&] { q.fromJson(doc.as<JsonVariantConst>()); }))
      << "the rebuild made more than one nothrow allocation";
}

TEST(TagPaletteRebuild, RetireDoesNotRebuild) {
  study::TagPalette p;
  const auto alfa = p.add("alfa");
  p.add("beta");
  EXPECT_FALSE(nthNothrowAllocationFired(1, [&] { p.retire(*alfa); }));
}

TEST(TagPaletteRebuild, ReviveDoesNotRebuild) {
  study::TagPalette p;
  const auto alfa = p.add("alfa");
  p.retire(*alfa);
  EXPECT_FALSE(nthNothrowAllocationFired(1, [&] { p.add("alfa"); }));
  EXPECT_TRUE(p.isActive(*alfa));
}

TEST(TagPaletteRebuild, ReAddingAnActiveNameDoesNotRebuild) {
  study::TagPalette p;
  p.add("alfa");
  EXPECT_FALSE(nthNothrowAllocationFired(1, [&] { p.add("alfa"); }));
}

TEST(TagPaletteRebuild, ARejectedFromJsonDoesNotRebuild) {
  study::TagPalette p;
  p.add("zeta");
  p.add("alfa");
  JsonDocument doc = paletteJson({{1, "mu"}});
  doc["v"] = study::TagPalette::FORMAT_VERSION + 1;

  bool accepted = true;
  EXPECT_FALSE(nthNothrowAllocationFired(1, [&] { accepted = p.fromJson(doc.as<JsonVariantConst>()); }));
  EXPECT_FALSE(accepted);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "zeta"})) << "a refused file leaves the palette as it was";
}

TEST(TagPaletteRebuild, AnEmptyPaletteAllocatesNothing) {
  const JsonDocument doc = paletteJson({});
  study::TagPalette p;
  EXPECT_FALSE(nthNothrowAllocationFired(1, [&] { p.fromJson(doc.as<JsonVariantConst>()); }));
}

TEST(TagPaletteOom, FallsBackToAllocationOrderOnAdd) {
  study::TagPalette p;
  const auto zeta = p.add("zeta");
  const auto mu = p.add("mu");
  study::TagId alfa{};
  EXPECT_TRUE(nthNothrowAllocationFired(1, [&] { alfa = *p.add("alfa"); }));

  EXPECT_EQ(rawIds(p.activeIdsByName()), rawIds(p.activeIds())) << "a failed rebuild shows allocation order";
  EXPECT_EQ(p.nameRank(*zeta), 0);
  EXPECT_EQ(p.nameRank(*mu), 1);
  EXPECT_EQ(p.nameRank(alfa), 2);
}

TEST(TagPaletteOom, FallsBackToAllocationOrderOnFromJson) {
  const JsonDocument doc = paletteJson({{1, "zeta"}, {2, "mu"}, {3, "alfa"}});
  study::TagPalette p;
  EXPECT_TRUE(nthNothrowAllocationFired(1, [&] { p.fromJson(doc.as<JsonVariantConst>()); }));
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{1, 2, 3}));
}

TEST(TagPaletteOom, TheNextRebuildRestoresNameOrder) {
  study::TagPalette p;
  p.add("zeta");
  p.add("mu");
  EXPECT_TRUE(nthNothrowAllocationFired(1, [&] { p.add("alfa"); }));

  p.add("beta");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "beta", "mu", "zeta"}));
}

}  // namespace
```

- [ ] **Step 5: Build and run**

Run:
```bash
cmake --build build/test --target TagPaletteTest 2>&1 | grep -E 'error|warning' | head
build/test/tag_palette/TagPaletteTest | grep -E '^\[  PASSED|FAILED'
```
Expected: no `error` or `warning` lines, and `[  PASSED  ] 41 tests.`

- [ ] **Step 6: Prove the tests bite, then restore**

Each mutation below must turn the named tests red. Keep a pristine copy and restore it after each
one. Run `touch` before every build, because of trap 1 in "Before you start".

```bash
F=lib/StudyStore/StudyStore/TagPalette.cpp
cp $F /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good
run() { sleep 1; touch $F; cmake --build build/test --target TagPaletteTest >/dev/null 2>&1 || { echo "build failed"; return; }; build/test/tag_palette/TagPaletteTest 2>&1 | grep -E '^\[  FAILED  \] [A-Z]' | sed 's/ (.*//; s/\[  FAILED  \] //' | sort -u | tr '\n' ' '; echo; }
mut() { python3 - "$F" "$1" "$2" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p).read(); assert s.count(old) == 1, old; open(p, 'w').write(s.replace(old, new))
PY
}
echo "M1 no rebuild in fromJson:"; mut $'highest + 1u));\n  rebuildDisplayOrder();' 'highest + 1u));'; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
echo "M2 fallback leaves stale ranks:"; mut $'      entries_[i].rank = static_cast<uint16_t>(i);\n      byName_[i] = static_cast<uint16_t>(i);\n' ''; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
echo "M3 retire rebuilds:"; mut $'    if (e.id == id) e.active = false;\n  }' $'    if (e.id == id) e.active = false;\n  }\n  rebuildDisplayOrder();'; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
echo "M4 no mark skip:"; mut 'if (second == 0xBF || second == 0xA1) name.remove_prefix(2);' '(void)second;'; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
echo "M5 no id tiebreak:"; mut 'return toRaw(left.id) < toRaw(right.id);' 'return false;'; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
echo "M6 no raw-name tiebreak:"; mut 'if (left.name != right.name) return left.name < right.name;' ''; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
echo "restored:"; run; build/test/tag_palette/TagPaletteTest | grep -E '^\[  PASSED'
git diff --quiet -- $F || git diff --stat -- $F
```
Expected, as measured while proving this plan:
- M1: `TagPaletteOom.FallsBackToAllocationOrderOnFromJson TagPaletteOrder.BreaksANameTieOnTheId TagPaletteOrder.BreaksATruncatedNameTieOnTheId TagPaletteRank.IsRebuiltByFromJson TagPaletteRebuild.AcceptedFromJsonMakesExactlyOneNothrowAllocation`
- M2: `TagPaletteOom.FallsBackToAllocationOrderOnAdd TagPaletteOom.FallsBackToAllocationOrderOnFromJson`
- M3: `TagPaletteRebuild.RetireDoesNotRebuild`
- M4: `TagPaletteOrder.SkipsOneLeadingInvertedMark`
- M5: `TagPaletteOrder.BreaksANameTieOnTheId TagPaletteOrder.BreaksATruncatedNameTieOnTheId`
- M6: `TagPaletteOrder.IgnoresCase`
- restored: an empty failure line, then `[  PASSED  ] 41 tests.` The last `git diff` prints nothing,
  because `TagPalette.cpp` is back to the Task 2 commit.

If any mutation leaves every test green, a test is not checking what it claims. Stop and report.

- [ ] **Step 7: Commit**

```bash
git add test/tag_palette/FailingNothrowNew.h test/tag_palette/FailingNothrowNew.cpp test/tag_palette/CMakeLists.txt test/tag_palette/TagPaletteTest.cpp
git commit -F - <<'EOF'
test: cover the tag rank rebuild and its OOM fallback

Counts the rebuild's one nothrow allocation with the replaced global
operator new from test/ditherers, copied into this suite and built with
-fno-builtin. Only an appending add and an accepted fromJson rebuild. A
failed key buffer shows allocation order, and the next rebuild restores
name order.

Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG
EOF
```

---

## Task 4: `inDisplayOrder` for a passage's tags

**Files:**
- Create: `test/tag_palette/TagDisplayOrderTest.cpp` (test first)
- Modify: `test/tag_palette/CMakeLists.txt`
- Create: `lib/StudyStore/StudyStore/TagDisplayOrder.h`

- [ ] **Step 1: Write `test/tag_palette/TagDisplayOrderTest.cpp`**

```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/TagDisplayOrder.h"

namespace {

std::vector<uint16_t> rawIds(const study::DisplayOrderedTags& ordered) {
  std::vector<uint16_t> out;
  out.reserve(ordered.count);
  for (size_t i = 0; i < ordered.count; ++i) out.push_back(study::toRaw(ordered.ids[i]));
  return out;
}

TEST(TagDisplayOrder, OrdersAPassagesTagsByName) {
  study::TagPalette p;
  const auto gamma = *p.add("gamma");
  const auto alfa = *p.add("alfa");
  const auto beta = *p.add("beta");
  EXPECT_EQ(rawIds(study::inDisplayOrder(p, {gamma, alfa, beta})),
            (std::vector<uint16_t>{study::toRaw(alfa), study::toRaw(beta), study::toRaw(gamma)}));
}

TEST(TagDisplayOrder, PlacesARetiredTagByItsName) {
  study::TagPalette p;
  const auto gamma = *p.add("gamma");
  const auto beta = *p.add("beta");
  const auto alfa = *p.add("alfa");
  p.retire(beta);
  EXPECT_EQ(rawIds(study::inDisplayOrder(p, {gamma, beta, alfa})),
            (std::vector<uint16_t>{study::toRaw(alfa), study::toRaw(beta), study::toRaw(gamma)}))
      << "a passage still carrying a retired tag must list it in place";
}

TEST(TagDisplayOrder, PutsUnknownIdsLastByRawId) {
  study::TagPalette p;
  const auto alfa = *p.add("alfa");
  EXPECT_EQ(rawIds(study::inDisplayOrder(p, {study::toTagId(900), alfa, study::toTagId(800)})),
            (std::vector<uint16_t>{study::toRaw(alfa), 800, 900}));
}

TEST(TagDisplayOrder, LeavesTheStoredOrderAlone) {
  study::TagPalette p;
  const auto gamma = *p.add("gamma");
  const auto alfa = *p.add("alfa");
  const std::vector<study::TagId> stored{gamma, alfa};

  study::inDisplayOrder(p, stored);
  EXPECT_EQ(stored, (std::vector<study::TagId>{gamma, alfa})) << "the sleep screen reads a passage's first stored tag";
}

TEST(TagDisplayOrder, ConsidersAtMostTheTagCap) {
  study::TagPalette p;
  std::vector<study::TagId> nine;
  for (int i = 0; i < 9; ++i) nine.push_back(*p.add("t" + std::to_string(i)));
  EXPECT_EQ(study::inDisplayOrder(p, nine).count, study::PassageDoc::MAX_TAGS_PER_PASSAGE)
      << "PassageDoc never stores more, and the result has no room for more";
}

}  // namespace
```

- [ ] **Step 2: Add it to the target in `test/tag_palette/CMakeLists.txt`**

Find:
```cmake
  TagPaletteTest.cpp
  FailingNothrowNew.cpp
```
Replace with:
```cmake
  TagPaletteTest.cpp
  TagDisplayOrderTest.cpp
  FailingNothrowNew.cpp
```

- [ ] **Step 3: Build to see it fail**

Run:
```bash
cmake --build build/test --target TagPaletteTest 2>&1 | grep -E 'error' | head -3
```
Expected: `fatal error: 'StudyStore/TagDisplayOrder.h' file not found`.

- [ ] **Step 4: Create `lib/StudyStore/StudyStore/TagDisplayOrder.h`**

```cpp
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/TagPalette.h"

namespace study {

// A passage's tags in display order (TagPalette::nameRank, then raw id), for a list row. A copy, so
// stored order -- which the sleep screen reads for its tag pill -- never moves.
struct DisplayOrderedTags {
  std::array<TagId, PassageDoc::MAX_TAGS_PER_PASSAGE> ids{};
  size_t count = 0;
};

// Ids past MAX_TAGS_PER_PASSAGE are ignored; PassageDoc never stores more.
inline DisplayOrderedTags inDisplayOrder(const TagPalette& palette, const std::vector<TagId>& tags) {
  struct Ranked {
    uint16_t rank;
    TagId id;
  };
  std::array<Ranked, PassageDoc::MAX_TAGS_PER_PASSAGE> ranked{};

  DisplayOrderedTags out;
  out.count = std::min(tags.size(), ranked.size());
  for (size_t i = 0; i < out.count; ++i) ranked[i] = {palette.nameRank(tags[i]), tags[i]};
  std::sort(ranked.begin(), ranked.begin() + out.count, [](const Ranked& a, const Ranked& b) {
    return a.rank != b.rank ? a.rank < b.rank : toRaw(a.id) < toRaw(b.id);
  });
  for (size_t i = 0; i < out.count; ++i) out.ids[i] = ranked[i].id;
  return out;
}

}  // namespace study
```

An editor's language server may flag `'StudyStore/PassageDoc.h' file not found` in this header,
because it has no include paths. Ignore that; the CMake and PlatformIO builds are the authority.

- [ ] **Step 5: Build, run, and count the new cases**

Run:
```bash
cmake --build build/test --target TagPaletteTest 2>&1 | grep -E 'error|warning' | head
build/test/tag_palette/TagPaletteTest | grep -E '^\[  PASSED|FAILED'
ctest --test-dir build/test -R TagDisplayOrder 2>&1 | grep -E 'tests passed'
```
Expected:
- no `error` or `warning` lines;
- `[  PASSED  ] 46 tests.`;
- `100% tests passed out of 5`. A test file missing from `add_executable` would be silently
  uncompiled, and this count is the guard against that.

- [ ] **Step 6: Commit**

```bash
git add lib/StudyStore/StudyStore/TagDisplayOrder.h test/tag_palette/TagDisplayOrderTest.cpp test/tag_palette/CMakeLists.txt
git commit -F - <<'EOF'
feat: order a passage's tags for display

inDisplayOrder copies a passage's at most 8 tag ids, ordered by
TagPalette::nameRank and then the raw id, for a list row. It is
header-only, so test/tag_palette covers it without StudyStore's Epub and
SD dependencies, as StudyReuse.h does. Stored order never moves.

Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG
EOF
```

---

## Task 5: `StudyStore` reads tags in display order

`StudyStore.cpp` has no host target: it depends on Epub, Arduino and SD. The order it applies is
covered by Tasks 2–4, and this task is glue. The firmware build is its test.

**Files:**
- Modify: `src/study/StudyStore.h`
- Modify: `src/study/StudyStore.cpp`

- [ ] **Step 1: Update the two comments in `src/study/StudyStore.h`**

Find (one whole line; the comment continues unchanged on the next):
```cpp
  // Active tags in allocation order, paired with their names, for a picker or a
```
Replace with:
```cpp
  // Active tags in display order, paired with their names, for a picker or a
```

Find:
```cpp
  // Comma-joined names of the tags a passage carries, for a list row. Retired
  // tags still resolve -- a passage that carries one must not render a blank.
```
Replace with:
```cpp
  // Comma-joined names of the tags a passage carries, in display order, for a
  // list row. Retired tags still resolve -- a passage that carries one must not
  // render a blank.
```

- [ ] **Step 2: Include the helper in `src/study/StudyStore.cpp`**

Find:
```cpp
#include "StudyStore/PubKey.h"
#include "StudyStore/UnitAnchors.h"
```
Replace with:
```cpp
#include "StudyStore/PubKey.h"
#include "StudyStore/TagDisplayOrder.h"
#include "StudyStore/UnitAnchors.h"
```

- [ ] **Step 3: Rewrite `activeTags()`**

The rewrite reserves before its loop (spec A23).

Find:
```cpp
std::vector<StudyStore::TagView> StudyStore::activeTags() const {
  std::vector<TagView> out;
  for (const study::TagId id : palette_.activeIds()) out.push_back({id, palette_.name(id)});
  return out;
}
```
Replace with:
```cpp
std::vector<StudyStore::TagView> StudyStore::activeTags() const {
  const std::vector<study::TagId> ids = palette_.activeIdsByName();
  std::vector<TagView> out;
  out.reserve(ids.size());
  for (const study::TagId id : ids) out.push_back({id, palette_.name(id)});
  return out;
}
```

- [ ] **Step 4: Rewrite `tagNamesFor()`**

Find:
```cpp
std::string StudyStore::tagNamesFor(const size_t passageIndex) const {
  if (passageIndex >= passages_.passages().size()) return {};
  std::string out;
  for (const study::TagId id : passages_.passages()[passageIndex].tags) {
    const std::string name = tagName(id);
    if (name.empty()) continue;
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}
```
Replace with:
```cpp
std::string StudyStore::tagNamesFor(const size_t passageIndex) const {
  if (passageIndex >= passages_.passages().size()) return {};
  const study::DisplayOrderedTags ordered = study::inDisplayOrder(palette_, passages_.passages()[passageIndex].tags);
  std::string out;
  for (size_t i = 0; i < ordered.count; ++i) {
    const std::string name = tagName(ordered.ids[i]);
    if (name.empty()) continue;
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}
```

- [ ] **Step 5: Build the firmware**

Run it in the foreground. It took 3 min 11 s from a cold `.pio` while this plan was being proven.
See trap 2 if it fails on `index.lock`.

```bash
~/.platformio/penv/bin/pio run -e x4pro > /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/btrial-1-pio-run.log 2>&1; echo "pio exit=$?"
tail -4 /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/btrial-1-pio-run.log
grep -nE '(TagPalette|TagDisplayOrder|StudyStore|Fold)\.(cpp|h)[^ ]*: (warning|error)' /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/btrial-1-pio-run.log
```
Expected:
- `pio exit=0`;
- `x4pro  SUCCESS` in the tail;
- no output from the last `grep`, meaning no warning or error in a file this change touches.

This build is what proves `lib/StudyStore` links against `lib/BibleSearch` (spec A22).

If it fails to find `<BibleSearch/Fold.h>` or to link `BibleSearch::foldAppend`, stop. Do not add a
`library.json` or change `lib_ldf_mode`: report it, because the fix is a decision (spec A22).

- [ ] **Step 6: Commit**

```bash
git add src/study/StudyStore.h src/study/StudyStore.cpp
git commit -F - <<'EOF'
feat: list study tags in display order

StudyStore::activeTags() now returns the palette's display order, and
reserves before it fills. tagNamesFor() joins a passage's tags through
inDisplayOrder, so a passage row reads alphabetically while the stored
order, which the sleep screen's tag pill reads, stays as it was.

Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG
EOF
```

---

## Task 6: Full verification

**Files:** none, unless the formatter changes something. In that case it changes only files
declared above.

- [ ] **Step 1: Run the whole host suite**

Run:
```bash
cmake -S test -B build/test >/dev/null && cmake --build build/test -j 6 2>&1 | grep -E 'error' | head -5
ctest --test-dir build/test --output-on-failure -j 6 2>&1 | tail -3
```
Expected: no `error`, and `100% tests passed out of 1710`. That figure was measured with this
plan's code applied, and includes its 32 new tests: 17 in Task 2, 10 in Task 3 and 5 in Task 4. If siblings have landed tests in the
meantime, the total is higher, and what matters is `100% tests passed`.

- [ ] **Step 2: Run the static analysis CI runs**

Run:
```bash
~/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high > /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/btrial-1-pio-check.log 2>&1; echo "check exit=$?"
grep -nE 'StudyStore\.(cpp|h)|TagDisplayOrder|TagPalette' /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/btrial-1-pio-check.log | head
tail -5 /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/btrial-1-pio-check.log
```
Expected:
- `check exit=0`;
- `No defects found`;
- no defect lines naming `StudyStore`, `TagDisplayOrder` or `TagPalette`.

That was measured, in 14 s, with this plan's code applied. CI runs exactly this
(`.github/workflows/ci.yml:76`).

- [ ] **Step 3: Format the whole tree, as CI does**

Run:
```bash
./bin/clang-format-fix
git status --short
```
Expected: `git status --short` prints nothing. The code above was formatted with this wrapper while
the plan was proven.

If it does print files, inspect the diff. If `git status` lists a path that is not on a `FILES:`
line, do not stage it. Restore it with `git checkout -- <path>` and report it as a discovery: a
local clang-format can differ from CI's, and the wrapper formats the whole tree. Stage only the
listed paths that *are* declared, by name, and commit:

```bash
git diff --stat
git add <only the paths git status listed>
git commit -F - <<'EOF'
style: format the tag display order change

Claude-Session: https://claude.ai/code/session_01TKLqk1GxD3S6T2BZ1kMBvG
EOF
```

- [ ] **Step 4: Confirm the branch holds exactly this task's commits**

Run:
```bash
git log --oneline 2d86ae59..HEAD
git status --short
```
Expected: the plan commit, then Tasks 1–5 (and the optional style commit from Step 3), and a clean
tree. Stop here. The push and `hpipe submit` are the worker's, after it verifies this work.

---

## Hand-off notes for the worker (spec A18)

These are not steps for the implementer. They are what the worker must carry forward after
`implement`.

- **The picker's commit order changes until `t5` lands.** Today's
  `TagPickerActivity::commitAndFinish` reports selected ids in `tags_` order
  (`TagPickerActivity.cpp:216-225`), and `tags_` is `STUDY.activeTags()`. Once this task lands on
  the batch branch, that picker **stores** a passage's tags in display order until `t5`
  (`feature/tag-picker-grid`) switches its commit to `activeIds()`. State this in the last message
  of the implement turn: "t1 changes the picker's commit order until t5 lands; t5 must not be
  dropped from the batch."
- **Device checks for the owner,** from the spec: passage rows list tags alphabetically; the picker
  lists tags alphabetically (tested on the full batch); serial shows no `TAGS` OOM line when a
  publication opens; heap is flat across a publication open and close.
- **The OOM log line has no host check.** The host `LOG_ERR` is a no-op. It is checked at PR review
  by inspection, and its format string by `-Wformat` during `pio run` (`platformio.ini:70-72`,
  `lib/Logging/Logging.h:42`).

## Self-review against the spec

| Spec item | Task |
|---|---|
| `activeIdsByName`, `nameRank` signatures and semantics | 2 |
| `Entry::rank` last, `byName_`, invariant I1 | 2 (fallback and rebuild both write all of `rank` and `byName_`) |
| D2 key: mark skip, fold, 24-byte zero pad, `memcmp`, then raw name, then id | 2; tests 2 and 3 (M4, M5, M6) |
| Rebuild only on appending `add` and accepted `fromJson` (A8) | 2; tests 3 (M1, M3) |
| One nothrow allocation; OOM fallback that is not sticky (A7, A10) | 2; tests 3 (M2) |
| Empty palette allocates nothing (A9) | 2; test 3 |
| `activeIds()` unchanged | test 2 (`LeavesActiveIdsInAllocationOrder`) |
| `inDisplayOrder`, its cap and stored order untouched (A14, A15) | 4 |
| `activeTags()` display order with reserve; `tagNamesFor()` display order; comments | 5 |
| Four CMake targets plus the injector and `-fno-builtin` (A20, A21) | 1, 3, 4 |
| Firmware link (A22), `pio check`, full-tree format | 5, 6 |
| A18 hand-off | Hand-off notes |

## Plan review 0 outcome

Review: `docs/superpowers/reviews/btrial-1-plan-review-0.md`, verdict CLEAR, with one MAJOR and one
MINOR. Both are accepted and fixed above.

| Finding | Change |
|---|---|
| MAJOR 1: no test reached the raw-name tiebreak | `IgnoresCase` allocates `alfa` before `Alfa` and expects `[3, 2, 1]`. Task 3 adds M6, which deletes that comparator line. The spec's row is updated to match. The fix was measured with this plan replayed onto the tree: M6 fails exactly `TagPaletteOrder.IgnoresCase`, M1 and M5 keep their lists, and all 46 tests pass once restored |
| MINOR 1: the formatter step could stage an undeclared path | Task 6 Step 3 restores any path not on a `FILES:` line and reports it as a discovery |
