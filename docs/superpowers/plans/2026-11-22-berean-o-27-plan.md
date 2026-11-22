# berean-o-27 implementation plan: clear the cppcheck style findings

Spec: `docs/superpowers/specs/2026-11-22-berean-o-27-design.md`. Read its Architecture table and its
Assumptions A1–A6 first.

FILES: src/activities/reader/GridMarks.h, src/activities/catalog/PublicationsActivity.cpp
FILES: src/activities/preaching/PreachingSession.cpp, src/activities/launcher/HomeVerseDoc.cpp

The rules for this work:
- Do not touch `platformio.ini`, `.github/workflows/ci.yml`, or any other file.
- Re-read each file before you edit it; a sibling may have changed it. If the lines below no longer match, stop and report.
- Never push. Commit locally only.
- Keep comments to a non-obvious "why" only (CLAUDE.md, Comments).

The red→green test is the include-less cppcheck command below. Local `pio check` passes even on the
unfixed base once `.pio/libdeps` exists, so it is only a regression guard (spec, Testing strategy).

```bash
CPPCHECK() {
  ~/.platformio/packages/tool-cppcheck/cppcheck --enable=all --language=c++ --std=c++20 --inline-suppr \
    --suppress=missingIncludeSystem --suppress=missingInclude --suppress=unusedFunction \
    --suppress=unmatchedSuppression --error-exitcode=3 --quiet \
    --template='{file}:{line}: [{severity}] {message} [{id}]' "$@"
}
```

Paste that function into each shell before you use it. Run every command from the worktree root
`/Volumes/stein/.herdr/worktrees/berean-os/fix-cppcheck-style-findings`.

---

## Step 1: confirm red (2 min)

```bash
CPPCHECK src/activities/catalog/PublicationsActivity.cpp src/activities/launcher/HomeVerseDoc.cpp \
  src/activities/preaching/PreachingSession.cpp src/activities/reader/GridMarks.h; echo "rc=$?"
```

Expected: exactly these five lines, then `rc=3`.

```
src/activities/catalog/PublicationsActivity.cpp:99: [style] Consider using std::transform algorithm instead of a raw loop. [useStlAlgorithm]
src/activities/catalog/PublicationsActivity.cpp:102: [style] Consider using std::transform algorithm instead of a raw loop. [useStlAlgorithm]
src/activities/launcher/HomeVerseDoc.cpp:133: [style] Operator '|' with one operand equal to zero is redundant. [badBitmaskCheck]
src/activities/preaching/PreachingSession.cpp:126: [style] Consider using std::copy_if algorithm instead of a raw loop. [useStlAlgorithm]
src/activities/reader/GridMarks.h:22: [style] Consider using std::fill algorithm instead of a raw loop. [useStlAlgorithm]
```

A different output means the base moved. Stop and report. Nothing to commit.

Then configure the host tests once (Step 6 reuses this build):

```bash
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
```

## Step 2: `GridMarks.h` uses `std::fill` (3 min)

File: `src/activities/reader/GridMarks.h`.

The red test for this step is the GridMarks line from Step 1. Test that `clear()` still works using the existing host test,
`test/number_grid/GridMarksTest.cpp:59-65`.

Edit 1: in the includes, replace

```cpp
#include <cstdint>
```

with

```cpp
#include <algorithm>
#include <cstdint>
#include <iterator>
```

Edit 2: replace

```cpp
  void clear() {
    for (auto& byte : bytes) byte = 0;
  }
```

with

```cpp
  void clear() { std::fill(std::begin(bytes), std::end(bytes), uint8_t{0}); }
```

Run:

```bash
CPPCHECK src/activities/reader/GridMarks.h; echo "rc=$?"
cmake --build build/test --target GridMarksTest && ./build/test/number_grid/GridMarksTest
```

Expected: no cppcheck output, `rc=0`, and every GridMarksTest case passes. If the test binary sits elsewhere, use
`find build/test -name GridMarksTest -type f`.

Commit:

```bash
git add src/activities/reader/GridMarks.h
git commit -m "fix: clear GridMarks bits with std::fill"
```

## Step 3: `PublicationsActivity.cpp` uses `std::transform` (4 min)

File: `src/activities/catalog/PublicationsActivity.cpp`. It is modelled on `src/activities/reader/PassageLinksActivity.cpp:28-30`.

Edit 1: in the includes, replace

```cpp
#include <algorithm>
```

with

```cpp
#include <algorithm>
#include <iterator>
```

(This is the `<algorithm>` include at line 11. It is the only one in the file.)

Edit 2: replace

```cpp
  std::vector<std::string> recentPaths;
  recentPaths.reserve(recents.size());
  for (const RecentBook& book : recents) recentPaths.push_back(book.path);
  std::vector<std::string> listedPaths;
  listedPaths.reserve(entries_.size());
  for (const Entry& entry : entries_) listedPaths.push_back(entry.path);
```

with

```cpp
  std::vector<std::string> recentPaths;
  recentPaths.reserve(recents.size());
  std::transform(recents.begin(), recents.end(), std::back_inserter(recentPaths),
                 [](const RecentBook& book) { return book.path; });
  std::vector<std::string> listedPaths;
  listedPaths.reserve(entries_.size());
  std::transform(entries_.begin(), entries_.end(), std::back_inserter(listedPaths),
                 [](const Entry& entry) { return entry.path; });
```

Run:

```bash
CPPCHECK src/activities/catalog/PublicationsActivity.cpp; echo "rc=$?"
```

Expected: no output, `rc=0`. This file has no host test (spec, Testing). Step 6's `pio run` compiles it.

Commit:

```bash
git add src/activities/catalog/PublicationsActivity.cpp
git commit -m "fix: build publication path lists with std::transform"
```

## Step 4: `PreachingSession.cpp` uses `std::copy_if` (3 min)

File: `src/activities/preaching/PreachingSession.cpp`. It is modelled on `src/study/BookPathIndex.cpp:25-26`.

Edit 1: in the includes, replace

```cpp
#include <cstdio>
#include <optional>
```

with

```cpp
#include <cstdio>
#include <iterator>
#include <optional>
```

Edit 2: in `PreachingSession::ensureTopics()`, replace

```cpp
  activeTopics.reserve(picked.size());
  for (const study::TagId id : byName) {
    if (std::find(picked.begin(), picked.end(), id) != picked.end()) activeTopics.push_back(id);
  }
```

with

```cpp
  activeTopics.reserve(picked.size());
  std::copy_if(byName.begin(), byName.end(), std::back_inserter(activeTopics), [&picked](const study::TagId id) {
    return std::find(picked.begin(), picked.end(), id) != picked.end();
  });
```

Run:

```bash
CPPCHECK src/activities/preaching/PreachingSession.cpp; echo "rc=$?"
```

Expected: no output, `rc=0`. This file has no host test. Step 6's `pio run` compiles it.

Commit:

```bash
git add src/activities/preaching/PreachingSession.cpp
git commit -m "fix: filter preaching topics with std::copy_if"
```

## Step 5: suppress the `HomeVerseDoc.cpp` false positive (2 min)

File: `src/activities/launcher/HomeVerseDoc.cpp`. It is modelled on `src/activities/reader/EndOfBookOptions.cpp:99-101`.

Replace

```cpp
  out.valid = false;
  if (!persist::isKnownFormatVersion(doc["v"] | 0, FORMAT_VERSION)) return Restore::UnknownFormat;
```

with

```cpp
  out.valid = false;
  // ArduinoJson's `|` returns the value or a default; cppcheck reads it as a bitmask.
  // cppcheck-suppress badBitmaskCheck
  if (!persist::isKnownFormatVersion(doc["v"] | 0, FORMAT_VERSION)) return Restore::UnknownFormat;
```

Run:

```bash
CPPCHECK src/activities/launcher/HomeVerseDoc.cpp; echo "rc=$?"
cmake --build build/test --target HomeVerseDocTest && ./build/test/home_layout/HomeVerseDocTest
```

Expected: no cppcheck output, `rc=0`, and every HomeVerseDocTest case passes.

Commit:

```bash
git add src/activities/launcher/HomeVerseDoc.cpp
git commit -m "fix: suppress cppcheck false positive on ArduinoJson default"
```

## Step 6: full verification (about 10 min, mostly waiting)

Run each command and record its result for the hand-off.

```bash
CPPCHECK src/activities/catalog/PublicationsActivity.cpp src/activities/launcher/HomeVerseDoc.cpp \
  src/activities/preaching/PreachingSession.cpp src/activities/reader/GridMarks.h; echo "rc=$?"
```

Expected: no output, `rc=0`.

```bash
~/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high; echo "rc=$?"
```

Expected: `No defects found`, `rc=0`.

```bash
~/.platformio/penv/bin/pio run -e x4pro; echo "rc=$?"
```

Expected: `SUCCESS`, `rc=0`, and no new warnings in the four touched files.

```bash
cmake --build build/test && ctest --test-dir build/test --output-on-failure; echo "rc=$?"
```

Expected: all tests pass, `rc=0`.

```bash
./bin/clang-format-fix && git status --short
```

Expected: no changes. If the formatter rewrapped any of the four files, commit that:

```bash
git add src/activities/reader/GridMarks.h src/activities/catalog/PublicationsActivity.cpp \
  src/activities/preaching/PreachingSession.cpp src/activities/launcher/HomeVerseDoc.cpp
git commit -m "style: clang-format cppcheck fixes"
```

If clang-format touches any other file, revert it with `git checkout -- <file>` and report it. If any command
fails, stop and report its output. Do not push.
