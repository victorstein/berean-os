# Whole-branch review: follow-ups #132 / #133 (PRs #135, #134)

Reviewed on `main` at `a27c564e` (#135, squash) on top of `e9717778` (#134, squash).
Contracts read: `gh issue view 132`, `gh issue view 133`,
`docs/superpowers/specs/2026-09-26-issue-132-design.md` (Problem, Goal, Non-goals, A1–A4),
and both squash commits (`git show a27c564e`, `git show e9717778`).

## Verification run

- Host suite, configured into a scratch dir (not the repo's `build/`):
  `cmake -S test -B <scratch>/hostbuild && cmake --build <scratch>/hostbuild -j8 && ctest --test-dir <scratch>/hostbuild -j8`
  → `100% tests passed out of 965`.
- `ctest -R 'PersistableStoreGuard|Version|FormatVersion'` → `100% tests passed out of 60`;
  `PersistableStoreGuard` lists 7 tests (#959–#965), including both #134 cases.
- `pio run -e x4pro` → `x4pro SUCCESS 00:00:54.683`. It covers the three stores with no host
  suite (`PubKeyRegistry`, `MigrationRunner`, `MeetingWeekCache`) and the new
  `lib/StudyStore` → `<FormatVersion.h>` include edge.
- `git status --short` is empty after both builds.

## Seam: #134 written before #135 edited `FormatVersion.h`

#135's only change to `lib/Serialization/FormatVersion.h` is the comment at lines 10-13. The
body of `isKnownFormatVersion` (`:14-16`, `version > 0 && version <= newestKnown`) and
`loadRefusedAfter` (`:23-36`) are byte-identical to what #134 was tested against. #134's
`ProbeStore::fromJson` (`test/persistable_store/PersistableStoreTest.cpp:33-34`) reads
`doc["v"] | FORMAT_VERSION`, which is the accepting idiom the new comment documents at
`FormatVersion.h:11`. Its `NEWER` (`{"v":2,...}`, `:43`) is refused and its legacy case
(`:83-91`) is accepted under both versions of the header. There is no contradiction. The two
#134 tests pass on merged `main`.

#133's requirements are all met in `PersistableStoreTest.cpp:115-148`:
- future-version `.tmp`: the promotion is asserted (`:120-121`); both saves are refused
  (`:124-125`); the primary keeps `NEWER` byte-for-byte and no `.tmp` reappears (`:126-127`).
- garbage `.tmp`: the load reports Missing (`:138`), the `.tmp` is still on the fake card
  (`:139`), and the next save succeeds and replaces it (`:142-145`). The test starts
  refused (`:131-134`), so it would catch a load that wrongly kept the flag.

## Sweep for version reads #135 might have missed

`grep -rn '\["v"\]\|"v"' src lib` finds every JSON `"v"` read on `main`. Each one either
calls the helper or has a stated reason not to:

| Site | Read | Status |
|---|---|---|
| `src/CrossPointSettings.cpp:110-111`, `CrossPointState.cpp:64-65`, `WifiCredentialStore.cpp:32-33`, `util/RecentBooksDoc.cpp:57-58` | `\| FORMAT_VERSION` | helper (predates the batch) |
| `src/study/PubKeyRegistry.cpp:29-30, 58-59, 85-86` | `\| FORMAT_VERSION` | helper (#135) |
| `src/study/MigrationRunner.cpp:69-70, 97-98` | `\| LEDGER_FORMAT_VERSION` | helper (#135) |
| `src/network/MeetingWeekCache.cpp:25-26` | `\| FORMAT_VERSION` | helper (#135) |
| `lib/Epub/Epub/HighlightDoc.cpp:122-123` | `\| FORMAT_VERSION` | helper (#135) |
| `src/util/BookmarkDoc.cpp:32-33` | `\| FORMAT_VERSION` | helper (#135) |
| `lib/StudyStore/StudyStore/TagPalette.cpp:80-81`, `ChapterCompletion.cpp:113-114`, `PassageDoc.cpp:222-223` | `\| 0` | helper (#135) |
| `src/RecentBooksStore.cpp:18` | `\| FORMAT_VERSION` | only feeds a `LOG_ERR` after `RecentBooksDoc::fromJson` has already refused; not a check |
| `src/study/MigrationRunner.cpp:138` (`writeReport`) | write only | spec Non-goals: nothing reads the report back |

Other version checks that are not a JSON `"v"`:
- `src/activities/settings/FontDownloadActivity.cpp:117-118` reads `doc["version"] | 0 != FONTS_MANIFEST_VERSION`.
  This is a downloaded manifest parsed from a temp file that is removed at `:109`. It is not a
  persisted store, and exact-match is the right rule for a wire format.
- `CatalogStamp.cpp:62` and `IndexReader.cpp:30-31` are binary headers. The spec's Non-goals
  exclude them with a reason: `IndexReader` refuses older versions too, which is a different rule.
- `lib/Catalog/Catalog/CatalogIndex.h:47` `valid()` is catalog data validity, not a format gate.

`grep -rnE 'version\s*(<=|>)\s*0|> *(FORMAT_VERSION|LEDGER_FORMAT_VERSION)|FORMAT_VERSION *<' src lib`
now matches only the helper itself (`FormatVersion.h:15`) and `CatalogIndex.h:47`. No hand-rolled
spelling is left, and no second helper was added: `grep isKnown|knownVersion|KnownVersion` finds
nothing besides `isKnownFormatVersion`.

## Absent-`"v"` preservation, store by store

Before (from the `-` lines of `git show a27c564e`) → after (current `main`):

| Store | Before | After | Absent `"v"` |
|---|---|---|---|
| PubKeyRegistry `record` | `\| 0`, `> N` → 0 passes | `PubKeyRegistry.cpp:29` `\| N` → N known | accepted → accepted |
| PubKeyRegistry `findBySymbol`/`lookup` | `(\|0) > N` | `:58`, `:85` `\| N` | accepted → accepted |
| MigrationRunner `readLedger`/`appendLedger` | `(\|0) > N` | `MigrationRunner.cpp:69`, `:97` `\| N` | accepted → accepted |
| MeetingWeekCache `load` | `(\|0) > N` | `MeetingWeekCache.cpp:25` `\| N` | accepted → accepted |
| HighlightDoc | `\| 0`, `> N` | `HighlightDoc.cpp:122` `\| N` | accepted → accepted |
| BookmarkDoc | `\| N`, `<=0 \|\| >N` | `BookmarkDoc.cpp:32` `\| N` | accepted → accepted |
| TagPalette / ChapterCompletion / PassageDoc | `\| 0`, `<=0 \|\| >N` | `\| 0` unchanged | refused → refused |

This matches the issue's column exactly. The value is never used after the check:
`grep -n '\bversion\b'` in all eight files returns only the read line and the check line. A
changed default therefore cannot leak into any later branch or migration. The two write paths
still let a missing file through. `readDocFromFileAdopting` returns Missing,
`mayOverwriteAfterRead` passes, and the empty document has no `"v"`, which reads as `N`. So
the first write on a fresh card is still allowed (`PubKeyRegistry.cpp:24-34`,
`MigrationRunner.cpp:92-102`). A non-integer `"v"` keeps its old answer at every site too:
`|` yields the default, which was accepted before and after at the five accepting stores, and
refused before and after at the three `| 0` stores (spec A3).

The one intended behaviour change is that a written `v <= 0` is now refused where it used to
be accepted. On HighlightDoc it goes through the same `fromJson`-false path a future version
already used (`src/util/HighlightFile.cpp:24-29`), so it gets the same protection.

## Test registration

Every directory under `test/` that has a `CMakeLists.txt` appears in an `add_subdirectory(...)`
in `test/CMakeLists.txt`; a loop over `test/*/` found none unregistered. Neither PR adds a new
directory. #135 adds `lib/Serialization` to the include paths of five existing suites, and all
five build.

## Issue requirements vs what landed

#132's per-store test matrix (absent, 0, -1, current, current+1):
- HighlightDoc: absent/0/-1 are new (`HighlightDocTest.cpp:73-93`); current and current+1
  already existed (`:31`, `:59`).
- TagPalette: absent/0/-1 are new (`TagPaletteTest.cpp:96-117`); current is the round trip
  (`:65`); current+1 already existed (`:88`).
- ChapterCompletion: 0/-1 are new (`:276-284`); absent (`:271`), current+1 (`:266`) and
  current as round trip (`:207`) already existed.
- PassageDoc: absent/0/-1/current are new (`PassageDocTest.cpp:53-82`); current+1 already existed.
- BookmarkDoc: -1 is new (`BookmarkDocTest.cpp:110`); absent, 0 and current+1 already existed
  (`:81`, `:101`, `:118`).
- PubKeyRegistry, MigrationRunner and MeetingWeekCache have no suite. Searching
  `test/**/CMakeLists.txt` for their `.cpp` files finds nothing, and the PR says so, as the
  issue asked.

## Findings

### MINOR 1: RecentBooksDoc's existing suite has no `-1` case

The issue asks for the five-case matrix for "any others with an existing suite under `test/`".
`test/recent_books_doc/` is such a suite. It covers absent (`RecentBooksDocTest.cpp:270`),
0 (`:290`), current (`:264`) and current+1 (`:300`), but it has no `"v": -1`. Correctness is
not at risk: `RecentBooksDoc.cpp:58` calls the helper, and `FormatVersionTest.cpp:19` pins
`-1` at the helper. The gap only matters if someone later replaces that call with a
hand-rolled check. Fix: add a single `ANegativeVersionIsRefused` case beside `:290`.

### Not findings

- The `LOG_ERR` text at `MeetingWeekCache.cpp:27`, `MigrationRunner.cpp:71,99` and
  `PubKeyRegistry.cpp:31` still says "newer", even when the cause is `v <= 0`. The issue said
  to keep refusal behaviour unchanged, and spec A5 kept the strings on purpose. Only a
  hand-edited file can reach that path.

No BLOCKER or MAJOR. The two PRs compose without conflict. Every JSON store version check now
has one spelling, and the absent-`"v"` column is preserved exactly.

BLOCKERS: 0
MAJORS: 0
MINORS: 1

VERDICT: CLEAR
