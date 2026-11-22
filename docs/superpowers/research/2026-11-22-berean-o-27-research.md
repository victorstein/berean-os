# berean-o-27 research — clear the cppcheck style findings failing CI

## What gates CI

- `.github/workflows/ci.yml:76`: `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`.
- `platformio.ini:20-24`: `check_tool = cppcheck`, `check_flags = --enable=all ... --inline-suppr`.
  `--inline-suppr` is on, so a `// cppcheck-suppress <id>` comment on the line above a finding silences it.
  `--suppress=unmatchedSuppression` is also on, so a suppression that stops matching is not itself reported.

## Tool versions, verified

| Where | cppcheck | Evidence |
|---|---|---|
| Local (macOS) | 2.11 (`tool-cppcheck 2.11.0+230717`) | `~/.platformio/packages/tool-cppcheck/package.json`; `cppcheck --version` → `Cppcheck 2.11` |
| CI, failing run 45006621169 (2026-11-22) | 2.11 (`tool-cppcheck@2.11.0+230717 has been installed!`) | run log, Build x4pro job; runner `Image: ubuntu-26.04` |
| CI, last green `main` run 40963604803 (2026-10-29, `1c2b6445`) | not logged (package cache hit) | cppcheck job: `Image: ubuntu-24.04`, `No defects found` |

**The brief's guess that "the runner's cppcheck changed" is not supported.** The cppcheck package is
the same 2.11.0+230717 locally and in the failing run. The code did not change either:
`git diff --stat 1c2b6445 6fb52ff5` touches only two docs files. What did change is the runner image
(ubuntu-24.04 → ubuntu-26.04).

### Why CI fails now: include resolution (corrected after spec review 0)

The findings appear only when cppcheck cannot resolve the project includes. With the types resolved
(ArduinoJson's `JsonVariantConst::operator|` and the element types of the vectors), cppcheck stays quiet.
- In a fresh worktree, before `.pio/libdeps` existed, my first `pio check` exited 1 with the same five lines as CI.
- After that run installed the libdeps, the same command on the unchanged tree prints `No defects found` and exits 0
  (re-run at `8f8fd9a8`).
- The bundled cppcheck run directly, with the `check_flags` and no includes file, prints exactly CI's five lines with rc 3.
  The command is in the spec's Testing section.

The CI cppcheck job evidently analyses with includes unresolved; `platformio.ini:21-23` already notes that on a fresh
checkout. The image change is the likely trigger. So local `pio check` is **not** a red test here, only a regression guard.

## The five sites (all under `src/activities`, surface `ui`)

1. `src/activities/catalog/PublicationsActivity.cpp:97-99`: `recentPaths.reserve(recents.size()); for (const RecentBook& book : recents) recentPaths.push_back(book.path);`
2. `src/activities/catalog/PublicationsActivity.cpp:100-102`: the same shape over `entries_` → `listedPaths` (`entry.path`).
   `<algorithm>` is already included (`:11`). `<iterator>` (`std::back_inserter`) is not included directly.
3. `src/activities/launcher/HomeVerseDoc.cpp:133`: `persist::isKnownFormatVersion(doc["v"] | 0, FORMAT_VERSION)`.
   `|` here is ArduinoJson's default-value operator on a `JsonVariantConst`, not a bitwise OR, so the finding is a false positive.
   The same idiom appears unflagged in assignment form elsewhere, for example `src/study/PreachingPassages.cpp:89` (`const int version = doc["v"] | 0;`).
   cppcheck flags only this call-argument use.
4. `src/activities/preaching/PreachingSession.cpp:122-127`, `ensureTopics()`: `activeTopics.reserve(picked.size())`, then a loop over
   `byName` that pushes each id found in `picked`. This is a filter that keeps `byName` order. `<algorithm>` is included (`:11`).
5. `src/activities/reader/GridMarks.h:21-23`, `Bits::clear()`: `for (auto& byte : bytes) byte = 0;` over `uint8_t bytes[(CAPACITY + 7) / 8]`.
   The header includes only `<cstdint>` and project headers (`:3-8`). A `std::fill` would need `<algorithm>` (and `<iterator>` for `std::begin`/`std::end`).

All four files were last changed between 2026-09-29 and 2026-10-15
(`git log -1` per file: `7d019349`, `106dfc4b`, `89972e1f`, `99a88230`). Each change was green at the time.

## Nearest existing examples of each rewrite in this repo

- `std::transform` + `std::back_inserter` after a `.reserve()`: `src/activities/reader/PassageLinksActivity.cpp:28-30`.
  This is the same surface and the same shape as findings 1–2.
- `std::copy_if` + `std::back_inserter` with a lambda: `src/study/BookPathIndex.cpp:25-26`. This is the shape for finding 4.
- Inline suppression with a reason, in the same surface: `src/activities/reader/EndOfBookOptions.cpp:101`,
  `src/activities/catalog/CatalogSearchActivity.cpp:403`, and `src/activities/ActivityResult.h:110`. These are the pattern for finding 3.

## Test coverage of the touched code

- `GridMarks::Bits::clear()` is covered by the host test `test/number_grid/GridMarksTest.cpp:62`.
- `HomeVerseDoc::fromJson` is covered by `test/home_layout/HomeVerseDocTest.cpp`, including the format-version path.
- `PreachingSession::ensureTopics()` and the `PublicationsActivity` path-list build have no host test. Both depend on
  Arduino and FreeInkUI types. The rewrites keep behaviour identical: same iteration order, same `.reserve()`,
  and no new allocation beyond the existing `push_back` growth, which `reserve` already covers. The red→green test
  for those two is `pio check`, and the build is `pio run -e x4pro`.

## Scope and tier

All five sites are in `src/activities` (surface `ui`). Nothing changes a contract, an on-disk format, or another
surface, and `platformio.ini` / `ci.yml` stay untouched. Tier `light` stands.
