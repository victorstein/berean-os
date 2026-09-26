# Issue #111 research — SD path literals repeated across translation units

Baseline: branch `refactor/111-sd-path-constants` at `2f303f6f` (release 1.16.0).
Every `file:line` below was read at that commit. Search commands:

```bash
grep -rn '"/\.crosspoint' src lib test --include='*.cpp' --include='*.h'
grep -rn '"/\.berean'     src lib test --include='*.cpp' --include='*.h'
```

## 1. Who owns the behaviour

Nothing owns it. There is no shared definition of either SD root; each translation
unit spells the literal itself. The issue's list is **incomplete** — the grep finds
more sites than it names.

### `/.crosspoint` — as a bare root (issue listed all but one)

| Site | Use |
|---|---|
| `src/util/BookCacheUtils.cpp:17`, `:21` | `Epub(path, "/.crosspoint")` cache-dir argument |
| `src/activities/launcher/LauncherActivity.cpp:230` | same |
| `src/activities/catalog/PublicationsActivity.cpp:83` | same |
| `src/activities/boot_sleep/SleepActivity.cpp:823` | same |
| `src/activities/reader/EpubReaderActivity.cpp:179` | same, via `makeUniqueNoThrow<Epub>` |
| `src/RecentBooksStore.cpp:118` | same |
| `src/study/MigrationRunner.cpp:266` | same, via `makeUniqueNoThrow<Epub>` |
| **`src/activities/home/HomeActivity.cpp:63`** | same — **not in the issue** |
| `src/activities/settings/ClearCacheActivity.cpp:100` | `Storage.open("/.crosspoint")` |
| `src/activities/settings/ClearCacheActivity.cpp:120` | `String fullPath = "/.crosspoint/" + itemName;` (Arduino `String`) |
| `lib/Serialization/PersistableStore.cpp:12`, `:23` | `Storage.mkdir("/.crosspoint")` in `writeDocToFile` / `writeDocToFileAtomic` |

### `/.crosspoint` — as a full path or subdirectory (none in the issue)

| Site | Literal |
|---|---|
| `src/CrossPointSettings.h:431` | `getFilePath()` → `"/.crosspoint/settings.json"` |
| `src/CrossPointState.h:36` | `getFilePath()` → `"/.crosspoint/state.json"` |
| `src/RecentBooksStore.h:27` | `getFilePath()` → `"/.crosspoint/recent.json"` |
| `src/WifiCredentialStore.h:47` | `getFilePath()` → `"/.crosspoint/wifi.json"` |
| `src/main.cpp:227` | `constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";` |
| `src/study/MigrationRunner.cpp:31` | `LEGACY_DIR = "/.crosspoint/highlights"` (no trailing slash) |
| `src/util/HighlightFile.cpp:14` | `highlightsDir()` → `"/.crosspoint/highlights/"` (trailing slash) |
| `src/util/BookmarkUtil.cpp:10` | `getBookmarksDir()` → `"/.crosspoint/bookmarks/"` (trailing slash) |

`/.crosspoint/highlights` is therefore spelled twice, with and without a trailing
slash — callers concatenate differently (`HighlightFile.cpp` appends a stem,
`MigrationRunner.cpp:48` passes it to `Storage.listFiles`).

### `/.berean` — the root, defined in 5 TUs (issue list is correct)

| Site | Name | Use |
|---|---|---|
| `src/study/MigrationRunner.cpp:30` | `BEREAN_DIR` | `Storage.mkdir` at `:116`, `:171` |
| `src/study/PubKeyRegistry.cpp:12` | `BEREAN_DIR` | `Storage.mkdir` at `:45` |
| `src/study/TagPaletteFile.cpp:15` | `BEREAN_DIR` | `Storage.mkdir` at `:74` |
| `src/network/MeetingWeekCache.cpp:12` | `BEREAN_DIR` | `Storage.mkdir` at `:55` |
| `src/network/CatalogIndexStore.cpp:27` | `STUDY_DIR` (different name) | `indexPath()` at `:51`, `exists`/`mkdir` at `:204-205` |

### `/.berean` — full paths and subdirectories, each written out whole (not in the issue)

| Site | Constant |
|---|---|
| `src/study/PubKeyRegistry.h:30` | `pubkeyregistry::PATH = "/.berean/pubkeys.json"` |
| `src/study/TagPaletteFile.h:25` | `tagpalettefile::PATH = "/.berean/tags.json"` |
| `src/network/MeetingWeekCache.h:31` | `PATH = "/.berean/meeting-weeks.json"` |
| `src/study/MigrationRunner.h:51`, `:52` | `REPORT_PATH`, `LEDGER_PATH` |
| `src/study/BibleSearchStore.h:32-34` | `SEARCH_DIR`, `INDEX_PATH`, `CHECKPOINT_PATH` (class `static constexpr`) |
| `src/study/PassageFile.cpp:12` | `PASSAGES_DIR = "/.berean/passages"` |
| `src/study/UnitIndexCache.cpp:17` | `UNITS_DIR = "/.berean/units"` |
| `src/study/ChapterCompletionFile.cpp:12` | `COMPLETION_DIR = "/.berean/completion"` |

These already have one definition each; they only repeat the root prefix.

### Not code paths — out of scope for the swap

- Comments naming the paths (e.g. `src/study/MigrationRunner.h:5`,
  `lib/Serialization/PersistableStore.h:53`, `src/util/RecentBooksDoc.h:41`).
  `RecentBooksDoc.h:41-43` hand-derives a 57-byte bound from `"/.crosspoint" (12)`;
  if the root becomes a constant, that arithmetic could become a `static_assert`,
  but that is a choice for the spec.
- `lib/ProtectedPath/ProtectedPath.cpp` — matches any dot-prefixed component
  (`ProtectedPath.h:5-8`), it has no root literal and needs none.
- Host tests use the literals as *data*: `test/protected_path/ProtectedPathTest.cpp:16-62`,
  `test/recent_books_doc/RecentBooksDocTest.cpp:55,235,295`. These assert on-card
  names and should stay literal — a test that reads the constant can't catch a
  rename of it.
- `docs/file-formats.md:3,350,377` documents the paths; no build input.

## 2. Current control flow

There is no flow to change — this is a spelling consolidation. The facts that
constrain it:

- **`Epub` takes `const std::string& cacheDir`** (`lib/Epub/Epub.h:46`) and builds
  `cacheDir + "/epub_" + hash` (`:48`). Every call site today passes a `const char*`
  literal, so a `std::string` temporary is already constructed per call; passing a
  `constexpr const char*` constant instead is byte-for-byte the same work.
- **`getFilePath()` returns `const char*`** to a string with static storage
  (`CrossPointSettings.h:431` etc.), consumed by `PersistableStore`. Whatever
  replaces the literal must still be a null-terminated, static-lifetime string.
- **`/.crosspoint` is created implicitly** by `PersistableStoreBase::writeDocToFile*`
  (`PersistableStore.cpp:12,23`); `BookmarkFile.cpp:105` and `HighlightFile.cpp:82`
  rely on that and create their own subdirectory. `/.berean` is created
  independently by each of the five stores above before their first write.
- **`ClearCacheActivity.cpp:120`** builds `"/.crosspoint/" + itemName` as an Arduino
  `String` (`const char* + String` operator). A replacement must keep that
  expression type-valid, or build the path differently.

### The one real design constraint

Full-path constants like `"/.crosspoint/settings.json"` cannot be composed from a
`constexpr` root with standard C++20 at namespace scope: `constexpr std::string`
cannot outlive constant evaluation, and adjacent-literal concatenation only works
between literals, not named constants. Options are (a) a preprocessor macro root so
literal pasting works, (b) a `consteval` fixed-size char-array join, or (c) keep
full paths written out whole beside the roots in the same header, with a
`static_assert`/host test that each starts with its root. **No compile-time string
join helper exists in this repo** —
`grep -rn "concat\|StaticString\|FixedString\|consteval" src lib --include='*.h'`
returns only unrelated comments (`GfxRenderer.h:144`, `CssParser.h:110-111`).
(b) would be a new cross-cutting mechanism; per `.claude/agents/data-dev.md` that is
an escalation, not a default.

## 3. Installed tool and package versions

| Tool | Command | Output |
|---|---|---|
| PlatformIO Core | `~/.platformio/penv/bin/pio --version` | `PlatformIO Core, version 6.1.19` |
| ESP32 platform | `platformio.ini:15` | pioarduino `platform-espressif32` `55.03.37` |
| Arduino core | `~/.platformio/packages/framework-arduinoespressif32/package.json` | `"version": "3.3.7"` |
| Xtensa toolchain | `~/.platformio/packages/toolchain-xtensa-esp-elf/package.json` | `"version": "14.2.0+20251107"` (GCC 14) |
| Host compiler | `c++ --version` | `Apple clang version 21.0.0 (clang-2100.0.123.102)` |
| CMake | `cmake --version` | `cmake version 4.4.2` |

Both compilers accept `inline constexpr` variables and `consteval` (C++20,
`-std=c++2a` per `CLAUDE.md`). `pio` is not on `PATH`
(`ls ~/.platformio/penv/bin/pio` is the invocation used above).

## 4. Nearest existing example

**`lib/Serialization/SaveBudget.h`** is the model to mirror:

- a header-only file in `lib/Serialization/`, which the issue also proposes as the
  home (`SdPaths.h`);
- `#pragma once`, a `namespace persist { ... }`, `inline constexpr` values
  (`SaveBudget.h:15-23`), a comment carrying the non-obvious why;
- already reachable from every surface that will include the new header —
  `#include <SaveBudget.h>` in `src/study/MigrationRunner.cpp:10`,
  `src/study/PubKeyRegistry.cpp:7`, `src/study/TagPaletteFile.cpp:7`,
  `src/network/MeetingWeekCache.cpp:7`, `src/util/BookmarkFile.cpp:7`, and
  `lib/Serialization/PersistableStore.h:11`;
- a host test, `test/save_budget/SaveBudgetTest.cpp`, wired by
  `test/save_budget/CMakeLists.txt` (adds `${REPO_ROOT}/lib/Serialization` to the
  include path) and `test/CMakeLists.txt:101` (`add_subdirectory(save_budget)`).
  A new `test/sd_paths/` would need one more `add_subdirectory` line in
  `test/CMakeLists.txt` — a shared append point that `data-dev.md` says to report
  in the PR description rather than edit.

**Nearest example of the same *kind* of change** (mechanically sweeping call sites
onto one shared definition, no behaviour change): `5d2a34f0`
"fix: save every persisted store atomically, within a budget (#42)" — converted 54
call sites across 24 files and, like this issue, found a site the issue had not
listed. Its PR body counts converted sites and names the extras, which is the
reporting shape to reuse here.

## 5. Scope summary for the spec

- 13 `/.crosspoint` root uses in 10 files (issue said 10+; `HomeActivity.cpp:63` is
  the missed one).
- 8 `/.crosspoint/...` full-path or subdirectory literals in 8 files.
- 5 `/.berean` root definitions (matches the issue) plus 11 full-path/subdirectory
  constants that repeat the prefix.
- No on-card rename, no format-version change, no cache invalidation: every
  resulting string must be byte-identical to today's, including the trailing-slash
  difference between `HighlightFile.cpp:14` and `MigrationRunner.cpp:31`.
