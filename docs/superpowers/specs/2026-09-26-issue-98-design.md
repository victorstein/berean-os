# One `.tmp` adoption policy, in one place

**Date:** 2026-09-26
**Status:** SPEC — not implemented
**Issue:** #98, "Study-data loaders delete an unusable .tmp that the shared loader deliberately keeps"
**Target:** bereanOS, `x4pro` (ESP32-S3, 8 MB PSRAM)
**Branch:** `fix/98-single-tmp-adoption-policy`
**Builds on:** `docs/superpowers/research/2026-09-26-issue-98-research.md` (cited below as "research §n").
**Modelled on:**
- `lib/Serialization/PersistableStore.cpp:64-107` (`readDocFromFileAdopting`) for the I/O
  sequence and the keep-the-`.tmp` policy the new helper adopts;
- `lib/Serialization/TempAdoption.h:45-61` (`adoptedReadStatus`) for the new pure mapping function
  and its exhaustive host test;
- `src/study/BibleSearchStore.h:81,86-87` (`VerseTextSink` + `void* ctx`) for the callback shape;
- `src/study/BibleSearchStore.h:31` (`using Status = BibleSearch::IndexReader::Status;`) for
  aliasing a shared enum into a per-module scope;
- `test/storage_io/AdoptingReadTest.cpp` and `test/storage_io/TagPaletteFileTest.cpp` for the tests.

---

## Problem

Two policies exist for the same situation — primary file missing, `<path>.tmp` present but
unusable:

- `PersistableStoreBase::readDocFromFileAdopting` clears the document and **keeps** the `.tmp`
  (`PersistableStore.cpp:90-102`), for the reasons recorded in #51's A-12
  (`docs/superpowers/specs/2026-09-17-issue-51-design.md:312-339`).
- Five hand-rolled loaders **delete** it: `ChapterCompletionFile.cpp:55-57`,
  `PassageFile.cpp:91-93`, `TagPaletteFile.cpp:55-57`, `BookmarkFile.cpp:78-80`,
  `HighlightFile.cpp:64-66`.

The five are the files that cannot be re-downloaded — reading history, passages, tags, bookmarks,
highlights. The delete buys nothing, because the next save's `SDCardManager::writeFile` removes the
`.tmp` before re-creating it (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:282-284`).
It can cost the only copy, because `SDCardManager::readFile` returns `""` both when the card is not
initialised (`:191-194`) and when the open fails (`:196-199`); `readDocFromFileChecked` classifies
that as `Unreadable` (`PersistableStore.cpp:52-54`), `tempParsed` is false, and the delete arm runs
on a `.tmp` whose bytes may be intact (research §3).

The policy diverged because it is copied: each loader re-implements the read / maybe-read-tmp /
switch sequence (research §1-2), and a sixth store copying any of the five would copy the delete.

## Goal

1. An unusable `.tmp` beside a missing primary is **kept** on every adopting load path.
2. That policy — and the whole read / adopt / promote sequence — exists in **one** function in
   `lib/Serialization`, which all five loaders and `readDocFromFileAdopting` go through.
3. The helper is host-tested against the storage fake, including the issue's case: a
   present-but-garbage `.tmp` with no primary reports empty and leaves the `.tmp` on disk.

## Non-goals

- **Treating an unreadable `.tmp` as `Failed`** so the caller latches saving off. See A-11.
- **Any on-disk format change.** No file layout, version, path or budget changes; no cache to clear.
- **Changing any `save()` path.** All five already write through `writeDocToFileAtomic`
  (research §3, item 1).
- **Other `.tmp` files** — `BibleSearchIndexer.cpp:283`, `ProgressFile.h:34`,
  `FontDownloadActivity.cpp:87`. They do not use `TempAdoptionAction` (research §3).
- **Changing callers of the five `load()` functions.** Every `LoadResult` value keeps its name
  (A-3), so `StudyStore.cpp:43,49,57`, `MigrationRunner.cpp:208,238,277,379`,
  `LauncherActivity.cpp:83`, `EpubReaderActivity.cpp:1771` and
  `EpubReaderBookmarksActivity.cpp:34` compile unchanged.
- **UI or i18n.** No user-visible text; no `lib/I18n/translations/*.yaml` edit.
- **#63** (`PubKeyRegistry::record` / `MigrationRunner::appendLedger` ignoring a non-`Ok` read).
  Untouched; their `readDocFromFileAdopting` calls keep their exact current behaviour (A-7).

---

## Assumptions, for the review to attack

| | Assumption | Decided in |
|---|---|---|
| **A-1** | The policy is **keep** the `.tmp`. `DeleteTempReportEmpty` is renamed **`KeepTempReportEmpty`**, and no code path calls `Storage.remove` on a `.tmp` in response to it. | §Policy |
| **A-2** | The new helper is `PersistableStoreBase::loadAdopting`, a static in `PersistableStore.{h,cpp}` beside `readDocFromFileAdopting` — not a new file, because `PersistableStore.cpp` is the one TU that holds the JSON parser (`PersistableStore.h:14-22`). | §Architecture |
| **A-3** | A new `enum class AdoptedLoad : uint8_t { Loaded, Empty, RecoveredFromTemp, Failed }` lives in `lib/Serialization/TempAdoption.h`. Each of the five per-store `LoadResult` enums is replaced by `using LoadResult = AdoptedLoad;`. Values, order and names are identical to all five today (`ChapterCompletionFile.h:22`, `PassageFile.h:21-26`, `TagPaletteFile.h:16`, `BookmarkFile.h:19-24`, `HighlightFile.h:22-27`). | §Result type |
| **A-4** | A new pure `constexpr AdoptedLoad adoptedLoad(TempAdoptionAction action, bool accepted)` maps the decision plus the store's `fromJson` verdict to the result — the load-side sibling of `adoptedReadStatus`. It is the host-tested unit for the result mapping. | §Control flow |
| **A-5** | The reader is a parameter: `using DocReader = DocReadStatus (*)(const char* path, JsonDocument& doc);`. Four loaders pass `&PersistableStoreBase::readDocFromFileChecked`; `PassageFile` passes its streaming `readInto`, whose signature changes from `const std::string&` to `const char*`. The **same** reader is used for the primary and the `.tmp`, as every loader does today. | §Hooks |
| **A-6** | The store's parse is a function pointer plus context, context first: `using DocAcceptor = bool (*)(void* target, JsonVariantConst json);`. Call sites pass a captureless lambda. No `std::function`, no template. | §Hooks |
| **A-7** | `readDocFromFileAdopting` is re-expressed on the same private core as `loadAdopting` (reader fixed to `readDocFromFileChecked`), so the adopt/promote/keep sequence exists once. Its observable behaviour — return values, `doc.clear()` on an unusable `.tmp`, its log lines — is unchanged, and `test/storage_io/AdoptingReadTest.cpp` passes unmodified. | §Architecture |
| **A-8** | The core reads the primary and the `.tmp` into **one** `JsonDocument`, as `readDocFromFileAdopting` does (`PersistableStore.cpp:73`), instead of the two the loaders use today. Safe because both readers return before touching `doc` when the path is missing (`PersistableStore.cpp:48-50`, `PassageFile.cpp:32`), and the `.tmp` is read only when the primary is `Missing`. | §Control flow |
| **A-9** | Promotion happens **before** `fromJson`, as in all five loaders today (e.g. `HighlightFile.cpp:51-59`). A promoted `.tmp` whose contents the store rejects returns `Failed` with the file now at the primary path. | §Control flow |
| **A-10** | Logging moves into the helper under module `"PERSIST"`, naming the path. The five loaders gain the `LOG_INF "Recovered … from an interrupted write"` line the shared helper already emits (`PersistableStore.cpp:82`), and `TagPaletteFile` gains the rejected-after-recovery `LOG_ERR` it lacks today (`TagPaletteFile.cpp:51-52`). Per-store module tags (`COMPLETE`, `PASSAGE`, `TAGS`, `BKM`, `HLFILE`) are lost on these lines only. | §Error handling |
| **A-11** | An **unreadable** `.tmp` and an **unparseable** `.tmp` stay one case (`tempAdoptionAction` receives only `tempParsed`, `TempAdoption.h:30-41`) and both report `Empty`. Keeping the file protects it only until the caller's next save truncates it; closing that window means reporting `Failed` on a read error, which changes caller behaviour and is out of scope. | §Policy |
| **A-12** | `BookmarkFile::load` keeps its `bookmarks.clear()` before the call (`BookmarkFile.cpp:33`) and its `LOG_DBG` count after a `Loaded` result (`:58`); they are outside the policy. | §Call sites |
| **A-13** | Host coverage: the helper against the fake (new `test/storage_io/LoadAdoptingTest.cpp`); `adoptedLoad` exhaustively (`test/temp_adoption/`); and three of the five real loaders compiled unmodified into `StorageIoTest` — `TagPaletteFile` (already there), `ChapterCompletionFile` and `PassageFile` (new). `BookmarkFile` and `HighlightFile` are **not** host-built; after this change their `load()` is a single call with no branch of its own. | §Testing |
| **A-14** | No shared-file line is needed. Both suites are already registered (`test/CMakeLists.txt:102-103`); new sources go into `test/storage_io/CMakeLists.txt`, which is not on the report-don't-edit list (`.claude/agents/data-dev.md`, "Shared files"). | §Testing |

---

## Policy (A-1, A-11)

Keep. The three reasons from #51's A-12 were re-verified against today's tree in research §3; none
has changed, and nothing in the five loaders' files argues otherwise — none of them documents why it
deletes.

**Rename.** Once no caller deletes, `DeleteTempReportEmpty` names an action nobody takes, and the
next store author reading `TempAdoption.h:26` is told the opposite of the policy. It becomes
`KeepTempReportEmpty`, with its comment changed to "`.tmp` exists but is unusable; leave it — the
next save truncates it". The rename touches `TempAdoption.h:26,41,55`,
`PersistableStore.cpp:90`, `test/temp_adoption/TempAdoptionTest.cpp` (the
`MissingPrimaryWithAnUnparseableTempIsDiscarded` test at `:40` and the `adoptedReadStatus` test at
`:54`), and the five loaders' `case` labels, which disappear anyway. Historical design docs under
`docs/superpowers/` keep the old name; they describe the past.

**What keeping does and does not buy (A-11).** A kept `.tmp` survives until the next save of that
store, whose `writeFile` removes it (`SDCardManager.cpp:282-284`). If the `.tmp` was unreadable only
transiently, the next boot's load reads it, `tempParsed` is true, and it is promoted — **provided no
save happened in between**. A save in the same session still destroys it, exactly as it would
today. Closing that window requires `tempAdoptionAction` to distinguish `Unreadable` from
`ParseError` for the `.tmp` and report `Failed`, which latches saving off in every caller
(`ChapterCompletionFile.h:24-25`, `PassageFile.h:28-29`, `TagPaletteFile.h:18-20`,
`BookmarkFile.h:28-31`, `HighlightFile.h:31-35`). That is a behaviour change for five callers and a
different issue; the PR description will propose it as a follow-up.

## Architecture (A-2, A-7)

```
lib/Serialization/TempAdoption.h        + AdoptedLoad, adoptedLoad(); rename KeepTempReportEmpty
lib/Serialization/PersistableStore.h    + DocReader, DocAcceptor, loadAdopting()
lib/Serialization/PersistableStore.cpp  + anonymous-namespace core; readDocFromFileAdopting and
                                          loadAdopting both call it
src/study/ChapterCompletionFile.{h,cpp} load() → one loadAdopting call; LoadResult alias
src/study/PassageFile.{h,cpp}           same; readInto takes const char*
src/study/TagPaletteFile.{h,cpp}        same
src/util/BookmarkFile.{h,cpp}           same, around clear() and LOG_DBG (A-12)
src/util/HighlightFile.{h,cpp}          same
```

`lib/` cannot include `src/` (commit `6156de32`, "Design notes" — the reason #64 moved the decision
into `lib/Serialization` at all), so the shared result type must live in `lib/` and the per-store
names become aliases of it (A-3). The alias precedent is `BibleSearchStore.h:31`.

**Why `PersistableStore.cpp` and not a new `AdoptingLoad.cpp`.** The core calls whatever reader it
is given, so it does not itself instantiate `deserializeJson`; but `readDocFromFileAdopting` lives
in `PersistableStore.cpp` and must share the core (A-7), and a private core in an anonymous
namespace keeps both callers in one TU with no new header. `PassageFile.cpp:42` keeps its own
`deserializeJson` instantiation for its reader type, exactly as today.

**The core** (private to `PersistableStore.cpp`):

```cpp
// Reads `path` with `read`; if it is Missing, reads `<path>.tmp` into the same doc and
// promotes it when it parses. Returns the decision; `primary` receives the primary's status.
TempAdoptionAction readAdopting(const char* path, DocReader read, JsonDocument& doc,
                                DocReadStatus& primary);
```

It is `PersistableStore.cpp:65-89` with the reader parameterised: the promote arm (rename, then
`LOG_INF` / `LOG_ERR`) moves in unchanged, and the keep arm does nothing to the card.

`readDocFromFileAdopting(path, doc)` becomes: call the core with `readDocFromFileChecked`; on
`KeepTempReportEmpty`, `doc.clear()` (the A-10 clear from #51, `PersistableStore.cpp:91-96`,
moved not changed); return `adoptedReadStatus(primary, action)`.

`loadAdopting` is:

```cpp
static AdoptedLoad loadAdopting(const char* path, DocReader read, DocAcceptor accept, void* target);
```

## Result type (A-3, A-4)

```cpp
enum class AdoptedLoad : uint8_t {
  Loaded,             // primary read, parsed and accepted
  Empty,              // nothing usable on disk -- safe to save over
  RecoveredFromTemp,  // an interrupted write left .tmp as the only copy; promoted and accepted
  Failed,             // unreadable, unparseable or rejected -- DATA MAY STILL EXIST
};

constexpr AdoptedLoad adoptedLoad(TempAdoptionAction action, bool accepted);
```

| `action` | `accepted` | result |
|---|---|---|
| `UseLoaded` | true / false | `Loaded` / `Failed` |
| `PromoteTempAndUseIt` | true / false | `RecoveredFromTemp` / `Failed` |
| `ReportEmpty` | — | `Empty` |
| `KeepTempReportEmpty` | — | `Empty` |
| `ReportFailed` | — | `Failed` |
| anything else (`default`) | — | `Failed` |

This is exactly the table the five switches implement today (research §2), with the policy arm's
side effect removed. The `default` → `Failed` arm mirrors `adoptedReadStatus`'s `default`
(`TempAdoption.h:57-59`): an action added later defaults to the safe direction, the same reasoning
`mayOverwriteAfterRead` records (`DocReadStatus.h:30-31`).

`accepted` is meaningful only for the two arms that call `fromJson`; `loadAdopting` passes `false`
for the others and never calls `accept` for them.

Aliasing (A-3) keeps each header's own doc comment on `load()` — the per-store meaning of `Failed`
("MUST latch saving off", `ChapterCompletionFile.h:24-25` and siblings) stays where its caller
reads it. The per-value comments move to `AdoptedLoad`.

## Hooks (A-5, A-6)

```cpp
using DocReader   = DocReadStatus (*)(const char* path, JsonDocument& doc);
using DocAcceptor = bool (*)(void* target, JsonVariantConst json);
```

`readDocFromFileChecked` already has the `DocReader` signature (`PersistableStore.h:67`).
`PassageFile`'s `readInto` (`PassageFile.cpp:31`) changes its first parameter to `const char*`;
`Storage.openFileForRead` has a `const char*` overload (`lib/hal/HalStorage.h:40`), and its two
`path.c_str()` log arguments become `path`.

Context-first `void*` is the repo's callback shape (`BibleSearchStore.h:81`,
`OtaUpdater.h:14`, `CrossPointSettings.h:372`). `CLAUDE.md` ("Template and `std::function`
bloat") rules out `std::function`; a template over the store type would not cover
`BookmarkDoc::fromJson(JsonVariantConst, std::vector<BookmarkEntry>&)` (`BookmarkDoc.h:50`), which
is a free function, without a wrapper anyway.

A call site, `ChapterCompletionFile::load`:

```cpp
LoadResult load(const std::string& pubKey, study::ChapterCompletion& record) {
  return PersistableStoreBase::loadAdopting(
      path(pubKey).c_str(), &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) { return static_cast<study::ChapterCompletion*>(target)->fromJson(json); },
      &record);
}
```

The temporary `std::string` from `path(pubKey)` lives until the end of the full expression, which
covers the call.

## Data and control flow

`loadAdopting(path, read, accept, target)`:

1. `JsonDocument doc;` then `action = readAdopting(path, read, doc, primary)`:
   1. `primary = read(path, doc)`.
   2. If `primary == Missing`: `tmp = path + ".tmp"`; `tempExists = Storage.exists(tmp)`; if so,
      `tempParsed = read(tmp, doc) == Ok` (A-5, A-8).
   3. `action = tempAdoptionAction(primary, tempExists, tempParsed)` (`TempAdoption.h:30-43`,
      unchanged).
   4. `PromoteTempAndUseIt` → `Storage.rename(tmp, path)`; `LOG_INF` recovered, or `LOG_ERR` and
      continue with the document in hand (A-9).
   5. `KeepTempReportEmpty` → nothing touches the card (A-1).
2. `accepted = (action is UseLoaded or PromoteTempAndUseIt) && accept(target, doc)`.
3. If a `fromJson` call returned false, `LOG_ERR("PERSIST", "Rejected %s …")` — "Recovered %s but
   rejected its contents" on the promote arm (A-10).
4. `return adoptedLoad(action, accepted)`.

The card operations, by case:

| primary | `.tmp` | reads | card change | result |
|---|---|---|---|---|
| parses, accepted | any | primary | none | `Loaded` |
| parses, rejected | any | primary | none | `Failed` |
| unreadable / garbage | any | primary | none — `.tmp` never consulted | `Failed` |
| missing | missing | primary | none | `Empty` |
| missing | parses, accepted | primary, `.tmp` | `.tmp` → primary | `RecoveredFromTemp` |
| missing | parses, rejected | primary, `.tmp` | `.tmp` → primary | `Failed` |
| missing | parses, rename fails | primary, `.tmp` | none; `.tmp` kept | `RecoveredFromTemp` |
| missing | unreadable / garbage | primary, `.tmp` | **none — `.tmp` kept** | `Empty` |

Only the last row changes behaviour for the five loaders; every other row is today's behaviour,
which the new tests pin.

**Memory.** One `JsonDocument` per load instead of two (A-8). In ArduinoJson 7.4.2 (research §5) a
`JsonDocument` allocates its pool on first use, and the second document is populated only on the
`Missing` branch, so on the common path this changes no allocation; it is not claimed as a saving.
No new heap allocation is introduced. The `.tmp` path string is built only on the `Missing`
branch, as in `PersistableStore.cpp:71`.

**Flash.** Five inline copies of the sequence become five single calls plus one shared function. No
size claim is made here; the plan records `pio run` flash usage before and after.

**Concurrency.** Unchanged. All five files are owned by the Arduino loop task
(research §4); the helper takes no lock of its own, like `readDocFromFileAdopting`
(`PersistableStore.h:80-87`), and puts nothing on the render path.

## Call sites (A-12)

Each `load()` reduces to building its path and one `loadAdopting` call. The `#include
<TempAdoption.h>` lines in the five `.cpp` files go, since they no longer name the enum; each
header that declares `using LoadResult = AdoptedLoad;` includes `<TempAdoption.h>` instead, which is
free of Arduino (`TempAdoption.h:12-15`). Comments that described the per-store switch —
`BookmarkFile.cpp:53-54`, `HighlightFile.cpp:52-55`, `PassageFile.cpp:81-82`,
`BookmarkFile.cpp:68-69` — go with it; `HighlightFile.h:9-15` is updated to name `loadAdopting`.

`PersistableStore.h:73-74` ("An unparseable .tmp is … left on the card; see the call site for why")
is updated to point at `KeepTempReportEmpty`'s comment, which becomes the single home of the reason.

## Error handling

- **`Failed` still means "may still hold data"** — a primary that exists but cannot be read or
  parsed never becomes `Empty` (`adoptedLoad` has no path from `ReportFailed` to `Empty`; the test
  asserts it exhaustively).
- **Rename failure on promote** keeps the document and the `.tmp`, and logs `LOG_ERR` without the
  "Recovered" line (`PersistableStore.cpp:83-88`, moved unchanged).
- **Rejected contents** log `LOG_ERR` and return `Failed` on both arms (A-10).
- **Unusable `.tmp`** returns `Empty` with no log from the helper; the reader has already logged the
  read or parse error (`PersistableStore.cpp:53,58`, `PassageFile.cpp:36,44`).
- No `abort`, no exceptions, no new allocation path that can fail silently.

## Testing

TDD order: each new test is written and run red before its code.

### `test/temp_adoption/TempAdoptionTest.cpp` — pure, `TempAdoptionTest`

- Rename carried through: `MissingPrimaryWithAnUnparseableTempIsDiscarded` (`:40`) becomes
  `…IsKeptAndReportedEmpty`, asserting `KeepTempReportEmpty`.
- New `AdoptedLoad` tests, mirroring the `AdoptedReadStatus` block (`:44-91`): each row of the
  §Result type table, plus an exhaustive loop over every action × `accepted` asserting
  `ReportFailed` is never `Empty` and `accepted == false` never yields `Loaded` or
  `RecoveredFromTemp`.

### `test/storage_io/LoadAdoptingTest.cpp` — new, against the fake, `StorageIoTest`

Modelled on `AdoptingReadTest.cpp`. A tiny test target (`struct Target { int v = -1; bool accept =
true; int calls = 0; }`) with a `DocAcceptor` that records `doc["v"]`. One test per row of the
card-operations table, including:

- **The issue's case:** no primary, `.tmp` = `{"v":` → `Empty`, `fileBytes(TMP)` unchanged, primary
  absent, `accept` never called.
- An **unreadable** `.tmp` (`storage_fake::failReadsOf(TMP)`) → `Empty`, `.tmp` still on the card —
  the transient-failure case A-12 of #51 exists for.
- A counting `DocReader` proves the supplied reader, not `readDocFromFileChecked`, reads both the
  primary and the `.tmp` (A-5).
- A promoted-then-rejected `.tmp` returns `Failed` and the primary now holds the bytes (A-9).

### Real loaders in `StorageIoTest`

- `TagPaletteFileTest.cpp`: add `AGarbageTempIsKeptAndTheLoadIsEmpty`, and drop the header note at
  `:1-3` that defers this arm to #98.
- New `ChapterCompletionFileTest.cpp` and `PassageFileTest.cpp`, modelled on
  `TagPaletteFileTest.cpp`: round trip, recovery from `.tmp`, garbage `.tmp` kept, unreadable
  primary `Failed` and untouched. The passage suite also seeds a `.tmp` larger than 50,000 bytes (under `PassageDoc::SAVE_BYTE_BUDGET` = 200,000, `PassageDoc.h:31`) and
  asserts it is recovered — it proves the streaming reader is the one used on the `.tmp`.
- `test/storage_io/CMakeLists.txt` gains `ChapterCompletionFile.cpp`, `PassageFile.cpp` and their
  `lib/StudyStore` dependencies, taken from `test/chapter_completion/CMakeLists.txt` and
  `test/passage_doc/CMakeLists.txt` (`ChapterCompletion.cpp`; `PassageDoc.cpp`, `Unit.cpp`,
  `UnitFingerprint.cpp`, `Utf8.cpp`), plus `lib/Utf8` on the include path.

### Regression

`test/storage_io/AdoptingReadTest.cpp` passes **unmodified** — the proof that A-7's re-expression
changed nothing about `readDocFromFileAdopting`.

### Build and quality

`pio run` (x4pro) once after the last code edit, recording flash usage against the base;
`./bin/clang-format-fix` over the whole tree; the full host suite with `ctest`.

### What only the device can verify

Flag for the human tester, adapted from #64's recipe: on a dev build, with the card in a reader,
delete `/.berean/tags.json` and put a truncated `/.berean/tags.json.tmp` beside it. Boot and open
the tag list. **Expected:** the palette is empty, serial shows the parse error for the `.tmp`, and
`tags.json.tmp` is **still on the card** until the first tag is saved. Today it is removed at boot.
