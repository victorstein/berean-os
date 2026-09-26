# Issue #132 — research

Base: `06e53ecf` (`chore(main): release 1.16.7`). Every claim below was read at
that commit in this worktree.

## The helper

`lib/Serialization/FormatVersion.h:13-15`:

```cpp
constexpr bool isKnownFormatVersion(const int version, const int newestKnown) {
  return version > 0 && version <= newestKnown;
}
```

Its comment (`FormatVersion.h:10-12`) assumes callers read `doc["v"] | FORMAT_VERSION`,
so that an absent `"v"` arrives as the default and a written `0` is refused. The
helper itself only judges an `int`; **which value an absent `"v"` becomes is
decided entirely by the `| default` at each call site.** That is the lever that
lets each store keep its absent-`"v"` behaviour while sharing one check:

- `doc["v"] | FORMAT_VERSION` → absent accepted (all current constants are ≥ 1).
- `doc["v"] | 0` → absent refused (helper rejects 0).

Pinned by `test/format_version/FormatVersionTest.cpp:5-22` (constexpr, current,
older, 0, -1, newer).

## The four sites that already use it (the pattern to mirror)

All four read `const int version = doc["v"] | FORMAT_VERSION;` then call the helper:

| Site | On refusal |
|---|---|
| `src/CrossPointSettings.cpp:110-116` | `LOG_ERR("CPS", "Refusing %s: unknown format v%d (this build knows v1..v%d)", ...)`, `return false` |
| `src/CrossPointState.cpp:63-69` | same shape, module `"STATE"` |
| `src/WifiCredentialStore.cpp:31-37` | same shape, module `"WCS"` |
| `src/util/RecentBooksDoc.cpp:57-58` | bare `return false` (no log — pure doc parser) |

They were routed by #123 (`2674c3c2`, "fix: version the four inherited stores and
refuse unknown formats"), which is the nearest existing example of this change:
same helper, same `| FORMAT_VERSION` idiom, version tests added per suite
(`test/recent_books_doc/RecentBooksDocTest.cpp:264-337`).

## The hand-rolled sites

The issue's table is accurate against the code, with **one site it misses**:
`src/study/PubKeyRegistry.cpp:28-32` (the write path, `record`) has the same
`doc["v"] | 0` / `version > FORMAT_VERSION` shape as the two read paths it lists.

| Site | Code | Constant | Absent `"v"` today | `0` / `-1` today | On refusal |
|---|---|---|---|---|---|
| `src/study/PubKeyRegistry.cpp:28-29` (record) | `const int version = doc["v"] \| 0; if (version > FORMAT_VERSION)` | `FORMAT_VERSION = 1` (`:13`) | accepted | accepted | `LOG_ERR(MODULE, "Refusing to rewrite a newer registry format")`, `return false`, file untouched |
| `src/study/PubKeyRegistry.cpp:57` (findBySymbol) | `if ((doc["v"] \| 0) > FORMAT_VERSION)` | same | accepted | accepted | `return std::nullopt`, no log |
| `src/study/PubKeyRegistry.cpp:83` (lookup) | same | same | accepted | accepted | `return std::nullopt`, no log |
| `src/study/MigrationRunner.cpp:68` (readLedger) | `if ((doc["v"] \| 0) > LEDGER_FORMAT_VERSION)` | `LEDGER_FORMAT_VERSION = 1` (`:31`) | accepted | accepted | `LOG_ERR(MODULE, "Refusing to read a newer ledger format")`, `return std::nullopt` |
| `src/study/MigrationRunner.cpp:95` (appendLedger) | same | same | accepted | accepted | `LOG_ERR(MODULE, "Refusing to rewrite a newer ledger format")`, `return false` |
| `src/network/MeetingWeekCache.cpp:24` (load) | `if ((doc["v"] \| 0) > FORMAT_VERSION)` | `FORMAT_VERSION = 1` (`:13`) | accepted | accepted | `LOG_ERR(MODULE, "Refusing to read a newer week cache format")`, `return false` |
| `lib/Epub/Epub/HighlightDoc.cpp:121-122` | `const int version = doc["v"] \| 0; if (version > FORMAT_VERSION) return false;` | `FORMAT_VERSION = 1` (`HighlightDoc.h:22`) | accepted | accepted | `return false` |
| `lib/StudyStore/StudyStore/TagPalette.cpp:78-79` | `doc["v"] \| 0`; `version <= 0 \|\| version > FORMAT_VERSION` | `1` (`TagPalette.h:35`) | refused | refused | `return false` |
| `lib/StudyStore/StudyStore/ChapterCompletion.cpp:111-112` | same | `1` (`ChapterCompletion.h:30`) | refused | refused | `return false` |
| `lib/StudyStore/StudyStore/PassageDoc.cpp:221-222` | same | `FORMAT_VERSION = 2`, `LINKLESS_FORMAT_VERSION = 1` (`PassageDoc.h:24-25`) | refused | refused | `return false` |
| `src/util/BookmarkDoc.cpp:31-32` | `doc["v"] \| FORMAT_VERSION`; `version <= 0 \|\| version > FORMAT_VERSION` | `1` (`BookmarkDoc.h:23`) | accepted (as 1) | refused | `return false` |

In none of the parser sites (`HighlightDoc`, `TagPalette`, `ChapterCompletion`,
`PassageDoc`, `BookmarkDoc`) is `version` read again after the check
(`grep -n version` over the five files returns only the two lines each), so the
change is confined to the check itself.

`PassageDoc` is the only store whose newest version is not 1: `current + 1` is 3,
and it writes `v:1` when no passage has links (`PassageDoc.cpp:189`).

### Control flow per store

- **Parser stores** (`HighlightDoc`, `TagPalette`, `ChapterCompletion`,
  `PassageDoc`, `BookmarkDoc`): `fromJson(JsonVariantConst)` is a pure function
  of the document; the version check is the first thing after the
  `is<JsonObjectConst>()` guard (except `HighlightDoc`, which has no such guard,
  and `BookmarkDoc`, which clears its output first at `:25`). Refusal = `false`,
  and the caller keeps the file.
- **File-level helpers** (`PubKeyRegistry`, `MigrationRunner` ledger,
  `MeetingWeekCache`): read via `PersistableStoreBase::readDocFromFileAdopting`,
  then check `"v"`. Write paths (`PubKeyRegistry::record`,
  `appendLedger`) check the version **before** stamping `doc["v"] =` and
  rewriting (`PubKeyRegistry.cpp:33`, `MigrationRunner.cpp:99`), so a refusal
  leaves the file untouched. `MeetingWeekCache::save` (`:38-`) builds a fresh
  doc and does not read first — it has no version check to change.
- The comment at `MigrationRunner.cpp:55-60` is why ledger refusal matters: a
  ledger misread as "nothing migrated" re-runs the migration and
  `PassageDoc::add` appends duplicates.

### Where an absent `"v"` could come from

Every one of the four "accepting" stores has stamped `"v"` since the commit that
created its file: `git show <first-add>:<file> | grep -c 'doc["v"] = '` returns 1
for `HighlightDoc.cpp` (`38dfbc1f`), `PubKeyRegistry.cpp` (`071259ce`),
`MigrationRunner.cpp` (`610b993f`) and `MeetingWeekCache.cpp` (`b1412ef7`). So an
absent `"v"` in those stores is only a hand-edited or foreign file; the issue
still requires the behaviour be preserved, and `| FORMAT_VERSION` (= 1 for all
four) does so.

### A second-order behaviour: non-integer `"v"`

ArduinoJson's `operator|` returns the default for an absent **or unconvertible**
value; `RecentBooksDocVersion.AStringVersionReadsAsTheDefault`
(`test/recent_books_doc/RecentBooksDocTest.cpp:309-321`) pins `"v":"2"` reading as
the default. Consequence per store if the default is chosen as above:

- Accepting stores: a string `"v"` is accepted today (`0 > 1` is false) and would
  still be accepted (`1` is known). Unchanged.
- Refusing stores with `| 0` kept: a string `"v"` is refused today and still
  refused. Unchanged.

So "keep the `| default` that gives today's absent-`"v"` answer" also preserves
today's unconvertible-`"v"` answer at every site.

### Out of scope, checked

- `lib/Catalog/Catalog/CatalogStamp.cpp:62` and
  `lib/BibleSearch/BibleSearch/IndexReader.cpp:30-31` compare a **binary header**
  field, not JSON `"v"`; `isKnownFormatVersion` does not describe them (the index
  refuses any older version too).
- `MigrationRunner.cpp:135` `writeReport` stamps `"v":1` on a report nobody reads
  back — no check.
- `test/passage_doc/PassageDocTest.cpp:410-412` re-states the **v1 build's**
  guard inline to prove a released v1 reader refuses a links file. It models
  shipped code, not this build's check, and should not be routed through the
  helper.

## Build / dependency facts

- No library under `lib/` other than `lib/Serialization` includes a
  Serialization header today:
  `grep -rln '#include <\(SaveBudget\|DocReadStatus\|PersistableStore\|FormatVersion\|SdPaths\)\.h>' lib | grep -v ^lib/Serialization`
  returns nothing. `lib/StudyStore` includes only the std library,
  `<ArduinoJson.h>` and `<Utf8.h>`; `HighlightDoc.cpp` includes only
  `<Utf8.h>` and std headers. Routing `TagPalette`, `ChapterCompletion`,
  `PassageDoc` and `HighlightDoc` through the helper therefore gives
  `lib/StudyStore` and `lib/Epub` their **first** include of `lib/Serialization`.
  `FormatVersion.h` depends only on `DocReadStatus.h` (`FormatVersion.h:3`), and
  its own comment says it is kept free of Arduino and ArduinoJson for exactly this
  kind of host use (`:5-7`). `platformio.ini` sets no `lib_ldf_mode`, so the
  PlatformIO default (chain) resolves `<FormatVersion.h>` to `lib/Serialization`
  as it already does for `src/`.
- Host-test include paths (each suite's own `CMakeLists.txt`, **not** the shared
  `test/CMakeLists.txt`):
  - `test/bookmark_doc/CMakeLists.txt` and `test/recent_books_doc/CMakeLists.txt`
    already add `${REPO_ROOT}/lib/Serialization`.
  - `test/tag_palette`, `test/chapter_completion`, `test/passage_doc`,
    `test/highlight_doc` do **not**; each would need
    `${REPO_ROOT}/lib/Serialization` added to `target_include_directories`.
    No source file needs adding (the helper is header-only `constexpr`).
  - `test/CMakeLists.txt:68,110-112,122,124,126` already registers every
    relevant suite, so no shared-file line is needed.
- Installed / pinned versions:
  - ArduinoJson `7.4.2` — `platformio.ini` `lib_deps` (`bblanchon/ArduinoJson @ 7.4.2`)
    and `test/CMakeLists.txt:29-31` (`GIT_TAG v7.4.2`).
  - GoogleTest `v1.17.0` — `test/CMakeLists.txt:15-17`.
  - `cmake --version` → `cmake version 4.4.2`.
  - `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19`
    (`pio` is not on `PATH`; see the fresh-worktree bootstrap note).
  - Standard: `-std=gnu++2a` (root `CLAUDE.md`, `platformio.ini:40`).

## Host-test coverage today

| Store | Suite | Absent | `0` | `-1` | current | current+1 |
|---|---|---|---|---|---|---|
| `BookmarkDoc` | `test/bookmark_doc` | ✓ `:81` | ✓ `:101` | — | ✓ (round trip) | ✓ `:110` |
| `TagPalette` | `test/tag_palette` | — | — | — | ✓ `:98` | ✓ `:88-94` |
| `ChapterCompletion` | `test/chapter_completion` | ✓ refused `:271-274` | — | — | ✓ | ✓ `:266-269` |
| `PassageDoc` | `test/passage_doc` | — | — | — | ✓ `:55` | ✓ `:46-51` |
| `HighlightDoc` | `test/highlight_doc` | — | — | — | ✓ `:60-63` | ✓ `:65-70` (hard-coded `2`) |
| `PubKeyRegistry` | none | | | | | |
| `MigrationRunner` | none | | | | | |
| `MeetingWeekCache` | none (`test/meeting_week_table` covers `MeetingWeekTable` only) | | | | | |

`grep -rln 'PubKeyRegistry\|MigrationRunner\|MeetingWeekCache' test` returns
nothing. Per the issue, these three get no new harness; the PR says so.

The failing-first tests the issue asks for: `0` and `-1` must fail today for
`HighlightDoc` (accepted today); for the three `lib/StudyStore` stores and
`BookmarkDoc`, `0` / `-1` are already refused, so those cases pin existing
behaviour rather than fail first.
