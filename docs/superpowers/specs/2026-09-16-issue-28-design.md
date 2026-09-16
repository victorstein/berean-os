# Bookmarks: a save budget, an atomic write, and a load that cannot be overwritten

**Date:** 2026-09-16
**Status:** SPEC — not implemented
**Issue:** #28, "Bookmarks have no save budget and no atomic write"
**Target:** bereanOS, `x4pro` (ESP32-S3, 8 MB PSRAM)
**Branch:** `fix/28-bookmark-save-budget`
**Modelled on:** `src/util/HighlightFile.{h,cpp}` + `lib/Epub/Epub/HighlightDoc.{h,cpp}`, the
same doc/shell split, enum results and budget guard, with `src/study/StudyStore.cpp` as the model
for the caller-side latch and rollback.
**Builds on:** `docs/superpowers/research/2026-09-16-issue-28-research.md` — every measurement
quoted below comes from there.

---

## Problem

`BookmarkFile::save` writes through `PersistableStoreBase::writeDocToFile`
(`src/util/BookmarkFile.cpp:62`) — non-atomic, and measuring nothing. `BookmarkFile::load` collapses
"no file yet" and "file unreadable" into one `false` (`src/util/BookmarkFile.cpp:17-19`), and the
reader discards even that (`src/activities/reader/EpubReaderActivity.cpp:1747`). The chain that
follows is confirmed, not theorised (research §2): a set past `SDCardManager::readFile`'s 50,000-byte
cap (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202-210`) comes back truncated,
fails `deserializeJson` with `IncompleteInput`, loads as empty, and the next save writes the empty
list over the user's file.

It is reachable. A bookmark costs ≈206 serialised bytes and the set reaches 45,000 at **219
records** (research §1), all in one file, because the bookmark file is keyed on the book path
(`src/util/BookmarkUtil.cpp:10-12`) and this device's central book is one EPUB containing all 66
Bible books (`src/activities/launcher/LauncherActivity.cpp:93-99`).

## Goal

A bookmark save can no longer destroy the bookmark file, and a bookmark that was not written is
never shown as if it had been.

## Non-goals

- **Rescuing an already-truncated file.** A file over 50,000 bytes today cannot be read by any
  code path this change touches. It reports `Failed`, saving latches off, and the bytes stay on the
  card for a future recovery tool. Nothing here deletes it.
- **A `BookmarkDoc` in `lib/`.** `HighlightDoc` lives in `lib/Epub` because Epub code uses it;
  nothing outside `src/` uses bookmarks. `BookmarkEntry.h` stays at `src/BookmarkEntry.h` and the
  new format unit sits beside its shell in `src/util/`. Relocating `BookmarkEntry` would touch
  `EpubReaderActivity.h:13`, `ActivityResult.h` and the bookmarks activity for no behavioural gain.
- **Documenting the format in `docs/file-formats.md`.** Neither highlights nor passages nor the tag
  palette are there; that file covers the EPUB caches. Following the local convention beats
  following the global rule inconsistently.
- **Editing `lib/I18n/translations/*.yaml`.** Another task owns them (brief constraint). See A-7.
- **Touching `lib/Serialization/PersistableStore.{h,cpp}`.** Brief constraint, and nothing here
  needs it: every primitive already exists.
- **Streaming the read.** Rejected with a number below.

---

## Assumptions, for the review to attack

| | Assumption | Decided in |
|---|---|---|
| **A-1** | The byte budget stays at `persist::DEFAULT_SAVE_BUDGET` (45,000). It is a truncation guard, not a RAM guard. | §Budget |
| **A-2** | `MAX_BOOKMARKS = 64`, enforced only when adding. It is the growth bound that keeps a *new* file far from 45,000. | §Budget |
| **A-3** | `save()` refuses on bytes only, never on count, so an over-count legacy set can still be deleted down. | §Budget |
| **A-4** | A format version is added (`"v": 1`); **absent or `0` is read as 1**, not refused. | §Format |
| **A-5** | Over-budget-on-load is `Loaded`, not `Failed` — deliberately unlike `PassageDoc::fromJson`. | §Load |
| **A-6** | `addBookmark()` takes ownership of the toast (`showBookmarkMessage`), which gives the reader-menu path feedback it does not have today. | §Reader |
| **A-7** | The three refusals ship pointing at `STR_ERROR_GENERAL_FAILURE` until the i18n task lands the real keys. | §Strings |
| **A-8** | `load()` returns `LoadResult`, so both call sites must change; the old `bool` contract in `BookmarkFile.h:11-13` goes away. | §Load |

---

## Architecture

The split is `HighlightDoc` ↔ `HighlightFile`, copied:

| New/changed | Mirrors | Why |
|---|---|---|
| `src/util/BookmarkDoc.{h,cpp}` (new) | `lib/Epub/Epub/HighlightDoc.{h,cpp}` | Format rules, caps and JSON shape, free of `Arduino.h` — so it is host-testable. |
| `src/util/BookmarkFile.{h,cpp}` | `src/util/HighlightFile.{h,cpp}` | Storage shell: `LoadResult`/`SaveResult`, the `.tmp` decision, the budget guard, atomic write. |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | `src/study/StudyStore.cpp:37-46, 208-241` | Session latch on `Failed`; rollback when a save refuses. |
| `src/activities/reader/EpubReaderBookmarksActivity.cpp` | `src/activities/reader/PassageSelectActivity.cpp:26-34`, `StudyStore.cpp:233-241` | Bail on `Failed`; restore the entry when a delete's save refuses. |
| `test/bookmark_doc/` (new) | `test/highlight_doc/` | The only host-buildable surface this work adds. |

**`BookmarkDoc.cpp` does not instantiate the JSON serializer.** It builds and reads a
`JsonDocument`; `serializeJson`/`deserializeJson` stay inside `PersistableStore.cpp`, which is the
whole point of that class (`lib/Serialization/PersistableStore.h:13-21`). `HighlightDoc.cpp` and
`lib/StudyStore/StudyStore/PassageDoc.cpp` are the proof that this is flash-neutral.

**The `.tmp` decision logic is reused, not rewritten.** `highlightLoadAction()`
(`src/util/HighlightFileAction.h:34-46`) is already shared across stores: `src/study/PassageFile.cpp`
includes it (`:8`) and switches on it (`:72-97`) under its own `LoadResult`. Bookmarks do the same.
The store-flavoured name is the repo's, not a new one, and it is exhaustively tested
(`test/highlight_file/HighlightFileActionTest.cpp`). `highlightSaveAction()`
(`HighlightFileAction.h:49-56`) is reused for the same reason: it is what guarantees the refusal
happens *before* any file or directory is touched.

---

## The format

```jsonc
{"v":1,"bookmarks":[{"xpath":"…","percentage":0.4831,"summary":"…","si":26,"pc":31,"pp":14,"vo":41233}]}
```

Unchanged except for `"v"`. Field names, order and the conditional `"vo"` stay exactly as
`BookmarkFile.cpp:47-56` writes them today, so a file written by this build is still readable by the
current one.

**A-4 — absent version means 1.** `PassageDoc::fromJson` refuses `version <= 0`
(`lib/StudyStore/StudyStore/PassageDoc.cpp:101-103`), which is right for a store with no files in
the field. Bookmarks have files in the field since CrossPoint, all without `"v"`. Refusing them
would report `Failed` for every existing user, latch saving off, and look exactly like the bug this
issue fixes. So:

```cpp
const int version = doc["v"] | BookmarkDoc::FORMAT_VERSION;   // absent == legacy v1
if (version <= 0 || version > BookmarkDoc::FORMAT_VERSION) return false;
```

A future build still refuses a version it does not know, which is what CLAUDE.md's rule 4 asks for.

`summary` is re-bounded on the load path with `utf8SafeSummary(v, MAX_SUMMARY_BYTES)`, mirroring
`PassageDoc.cpp:116-117`: the field is bounded at creation today
(`src/util/BookmarkUtil.cpp:14` → `lib/Utf8/Utf8.h:32`, 72 bytes) but a file on an SD card is not a
trusted input. `MAX_SUMMARY_BYTES = 72` preserves the current figure. `xpath` is **not** bounded —
truncating it corrupts the position it addresses (`ProgressMapper::toCrossPoint` parses it back,
`lib/ProgressMapper/ProgressMapper.cpp:804-811`).

---

## The budget

Two numbers with two different jobs, exactly as `HighlightDoc.h:15-19` states it: *"Bytes, not entry
counts, are the safety invariant… The caps below bound growth; they do not guarantee the byte
bound."*

**A-1 — `SAVE_BYTE_BUDGET = persist::DEFAULT_SAVE_BUDGET` (45,000).** Same value and same reason as
`src/util/HighlightFile.h:38-42` and `src/study/TagPaletteFile.cpp:70`: headroom under the 50,000
read cap. Research §5 measured that a document *at* this budget costs ≈99 KB of internal SRAM
concurrently (45,129-byte `String` alive across `deserializeJson`,
`lib/Serialization/PersistableStore.cpp:50-59`, plus a 53,892-byte `JsonDocument` pool). That is a
real problem and it is **not** solved by lowering this constant, because the cost is paid on the
*read* of a file that already exists — before any budget is consulted. Lowering it would instead
make a legacy set unrecoverable (A-3). The RAM argument is answered by A-2.

**A-2 — `MAX_BOOKMARKS = 64`, checked when adding.** A new file can therefore hold at most
64 × ≈206 ≈ 13,200 bytes typical, 64 × ≈251 ≈ 16,080 worst-case-but-plausible (research §1) — under
40 % of the budget, with a transient load cost around 35 KB rather than 99 KB and ≈14 KB resident in
`cachedBookmarks` for the session (`EpubReaderActivity.h:47`; ≈224 B/entry on the device's
libstdc++ — 80 for the vector element plus two small heap blocks for the two `std::string`s).
Mirrors `HighlightDoc::MAX_HIGHLIGHTS = 400` (`HighlightDoc.h:23`), enforced the same way:
`HighlightDoc::addHighlight` refuses at the cap and returns false (`HighlightDoc.cpp:56`).
The reader already reserves 16 (`EpubReaderActivity.cpp:55`), so 64 is four times the shape the code
was written for, and tagged passages — 400 per publication — remain the annotation mechanism.
**This is the number most worth arguing about.** Raising it to 128 roughly doubles both figures.

**A-3 — `save()` never refuses on count.** A user arriving from CrossPoint with 100 bookmarks
(≈20,600 bytes) loads fine, cannot add, and **can delete**, because each delete's save is measured
in bytes and passes. Had the count been checked on save, every delete would be refused and the set
could never be brought back under the cap — an unrecoverable dead end reachable from existing data.

---

## Control flow

### `BookmarkFile::load(bookPath, bookmarks) -> LoadResult`

A structural copy of `HighlightFile::load` (`src/util/HighlightFile.cpp:23-70`):

1. `path = BookmarkUtil::getBookmarkPath(bookPath)`, `tmpPath = path + ".tmp"`.
2. `PersistableStoreBase::readDocFromFileChecked(path, primaryJson)` → `DocReadStatus`.
3. Only when that is `Missing`, probe `<path>.tmp` — a present primary is never second-guessed.
4. `switch (highlightLoadAction(primaryStatus, tempExists, tempParsed))`, all five arms as in
   `HighlightFile.cpp:40-68`, including promoting the temp **before** validating its contents.
5. `BookmarkDoc::fromJson` fills the vector; false → `Failed`.

`LoadResult` is `{Loaded, Empty, RecoveredFromTemp, Failed}`, copied from `HighlightFile.h:21-26`
with its comment: `Failed` means the bytes may still hold the user's data and the caller **must
not** save.

This closes the `.tmp` window that arrives with `writeDocToFileAtomic`: it removes the destination
before renaming (`PersistableStore.cpp:35-42`), so a power loss there leaves `.tmp` as the only copy
(research §6). Adopting the atomic write without this arm would trade one loss window for another.

**A-5 — over budget on load is `Loaded`, not `Failed`.** `PassageDoc::fromJson` returns
`measureBytes() <= SAVE_BYTE_BUDGET` (`PassageDoc.cpp:128-137`), folding "too big" into the same
failure as "unreadable". For bookmarks that would latch saving off and make deletion impossible,
which is A-3's dead end by another route. The two states are genuinely different: *unreadable* is
unrecoverable and must never be written over; *too big but read perfectly* is recoverable by
deleting, and the save guard already refuses the write. `fromJson` therefore never drops an entry
for budget or count — the same rule, and for the same reason, as `PassageDoc.cpp:120-127`.

### `BookmarkFile::save(bookPath, bookmarks) -> SaveResult`

A structural copy of `HighlightFile::save` (`HighlightFile.cpp:72-86`):

1. `BookmarkDoc::toJson(bookmarks, json)`.
2. `highlightSaveAction(measureJson(json), SAVE_BYTE_BUDGET)` → `RefuseTooLarge` returns
   `SaveResult::TooLarge` **before** `mkdir` or any write.
3. `Storage.mkdir(BookmarkUtil::getBookmarksDir())` — `writeDocToFileAtomic` only ensures
   `/.crosspoint` (`PersistableStore.cpp:23`), the subdirectory stays ours, as today
   (`BookmarkFile.cpp:59-60`).
4. `writeDocToFileAtomic` → `Ok` / `WriteFailed`.

`SaveResult` is `{Ok, TooLarge, WriteFailed}`, copied from `HighlightFile.h:37`.

### `EpubReaderActivity` — the latch

`loadCachedBookmarks()` (`:1737-1749`) keeps the result and, on `Failed`, sets a new
`bookmarksSaveDisabled` member and surfaces it, mirroring line-for-line what `onEnter` already does
for the study store three lines later (`:240` then `:247-250`):

```cpp
if (BookmarkFile::load(epub->getPath(), cachedBookmarks) == BookmarkFile::LoadResult::Failed) {
  bookmarksSaveDisabled = true;
  ReaderUtils::showMessage(renderer, tr(/* A-7 */));
}
```

The latch is a session property, like `StudyStore::saveDisabled_` (`src/study/StudyStore.cpp:58-62`
explains why it survives `closePublication`). `loadCachedBookmarks()` is also called on return from
the progress-change flow (`:689`) — a `Failed` there latches too, and the latch is never cleared.

### `EpubReaderActivity::addBookmark()` — refuse, then roll back

Current shape: mutate `cachedBookmarks`, set `currentPageBookmarked`, save, `LOG_ERR` on failure
(`:1771-1803`). A refusal therefore leaves the page flagged as bookmarked and the popup still
reading "Bookmark added" (`:1291`) for something that never reached the card. New shape, mirroring
`StudyStore::addPassage`'s *"saves synchronously and rolls back its own append on failure"*
(`StudyStore.cpp:226-230`, and `removePassage`'s backup/restore at `:233-241`):

1. `if (bookmarksSaveDisabled)` → toast, return. Mirrors `PassageSelectActivity.cpp:29-33`.
2. Decide add vs remove exactly as today (`:1765-1774`).
3. On the add path, `if (cachedBookmarks.size() >= BookmarkDoc::MAX_BOOKMARKS)` → toast, return,
   **before** mutating. Mirrors `HighlightDoc.cpp:56`.
4. Mutate, then save.
5. On anything but `Ok`: undo the insert or re-insert the removed entries at their index, restore
   `currentPageBookmarked`, and set the toast to the failure.

Step 5's rollback needs the pre-mutation state. The remove path erases *all* matching entries
(`:1767-1772`), so the rollback keeps the erased copies, not just an index.

**A-6 — `addBookmark()` sets the toast itself.** Today the three callers do it: the Confirm hold
(`:497-500`), the Home-key hold (`:527-531`), and the reader menu (`:877`) — which sets nothing, so
the menu path gives no feedback at all. Moving `showBookmarkMessage = true; bookmarkMessageTime =
millis();` into `addBookmark()` is what lets one place decide between five outcomes, and it gives
the menu path the feedback it is missing. The Home-key path's `if (!showBookmarkMessage)` guard
(`:526`) stays where it is. `bookmarkRemoved` (`EpubReaderActivity.h:46`, read only at `:1291`)
becomes an enum: `{Added, Removed, TooMany, SaveFailed, LoadDisabled}`.

### `EpubReaderBookmarksActivity`

- `onEnter` (`:33`) — on `Failed`, show the message and `finish()`, mirroring
  `PassageSelectActivity.cpp:29-33`. A list the user can delete from, backed by a doc we could not
  read, is the overwrite this issue is about.
- `deleteSelectedBookmark` (`:169-176`) — keep the erased entry, and on a non-`Ok` save re-insert it
  at its index, call `rebuildBookmarkRowItems()` a second time and show the failure. The rebuild
  ordering already has a documented reason (`:171-173`: rows must not alias erased storage), and a
  rollback has the same hazard in reverse.

---

## Error handling

| Condition | `BookmarkFile` | What the user sees |
|---|---|---|
| No file yet | `Empty` | nothing; empty list |
| `.tmp` is the only copy | `RecoveredFromTemp` | nothing; bookmarks are there |
| Unreadable / unparseable | `Failed` + `LOG_ERR` | load-failed toast once, then a refusal toast on every attempt |
| Read fine, over budget | `Loaded`; `save` → `TooLarge` | refusal toast; deletes still work |
| At `MAX_BOOKMARKS` | never reached — refused at the call site | "too many" toast, nothing mutated |
| `writeDocToFileAtomic` fails | `WriteFailed` + `LOG_ERR` | save-failed toast, in-memory state rolled back |

Every path is `LOG_ERR` + a false-ish return (CLAUDE.md error-handling rule 1). Nothing aborts,
nothing truncates, and no refusal creates or modifies a file — that is `highlightSaveAction`'s
contract (`HighlightFileAction.h:49-52`).

**A-7 — strings.** Three keys are needed and none exists:

| Key | Text | Modelled on |
|---|---|---|
| `STR_BOOKMARKS_LOAD_FAILED` | "Bookmarks unreadable - saving disabled this session" | `STR_HIGHLIGHTS_LOAD_FAILED`, `english.yaml:389` |
| `STR_BOOKMARKS_FULL` | "This book already has the maximum number of bookmarks" | `STR_TAG_PALETTE_FULL`, `english.yaml:398` |
| `STR_BOOKMARK_SAVE_FAILED` | "Could not save bookmark" | `STR_TAG_SAVE_FAILED`, `english.yaml:400` |

The brief forbids editing the YAML and says to use `STR_HIGHLIGHTS_TOO_LARGE` if it fits. It does
not: it reads "This book already has too many highlights" (`english.yaml:390`) and would tell a user
who pressed *Toggle Bookmark* about highlights. `STR_HIGHLIGHTS_SAVE_FAILED` and
`STR_HIGHLIGHTS_LOAD_FAILED` are wrong in the same way.

So all three ship pointing at `STR_ERROR_GENERAL_FAILURE` ("Error: General failure",
`english.yaml:201`) — noun-free and true — routed through one function,
`bookmarkToastString(BookmarkToast)`, so the swap is three lines in one file when the i18n task
lands the keys. The hand-back names the keys. **This is a knowingly poor message shipped to keep
criterion 3 met rather than silently dropped; a reviewer preferring "no popup until the key exists"
is choosing to let a refusal look like a success, which is the failure mode this issue is about.**

---

## Rejected alternatives

- **Stream the read** (`src/study/PassageFile.cpp:15-49`'s `HalFileReader` over `HalFile`). It does
  remove the 50,000-byte cap and would drop the load peak from ≈99 KB to the pool alone (≈54 KB at
  45 KB of JSON, research §5). Rejected because `HalFileReader` is a file-local class in
  `PassageFile.cpp`, so reusing it means either duplicating it into a second TU or promoting it to a
  shared header — and with A-2 in place a bookmark file written by this build never exceeds ~16 KB,
  so there is nothing for it to rescue. Revisit if `MAX_BOOKMARKS` is ever raised past ~150.
- **A tighter `SAVE_BYTE_BUDGET` (e.g. 18,000).** Rejected: A-3's dead end, and it does not reduce
  the read cost that motivated it.
- **Renaming `highlightLoadAction` to something store-neutral.** Rejected: `PassageFile.cpp:72`
  already reuses it under its store-flavoured name, so a rename is a cross-store refactor with its
  own test churn, in a data-loss fix.
- **A `BookmarkDoc` with its own `.tmp`/budget decision header.** Rejected: `HighlightFileAction.h`
  is already generic and already tested.

---

## Testing strategy

**Host — `test/bookmark_doc/`, modelled on `test/highlight_doc/`** (which links
`lib/Epub/Epub/HighlightDoc.cpp` and `lib/Utf8/Utf8.cpp` against `ArduinoJson` and
`GTest::gtest_main`). It compiles `src/util/BookmarkDoc.cpp` + `lib/Utf8/Utf8.cpp`, includes
`${REPO_ROOT}/src` (the precedent is `test/highlight_file/CMakeLists.txt:11-14`), and is registered
in `test/CMakeLists.txt` beside the other `add_subdirectory` lines:

1. Round-trip: every field survives `toJson` → `fromJson`, including absent `vo`.
2. `"v"` absent parses as v1 with all entries (A-4) — the legacy-file regression guard.
3. `"v": 2` is refused; `"v": 1` is accepted.
4. A set of `MAX_BOOKMARKS` plausible records measures under `SAVE_BYTE_BUDGET`, with the margin
   asserted, so raising the cap or adding a field to `BookmarkEntry` fails here first.
5. The genuine worst case — 16-deep xpath (`MAX_XPATH_DEPTH`, `ProgressMapper.cpp:141`), four-digit
   indices, a summary of characters JSON escapes — is measured and recorded, mirroring
   `HighlightDocTest.cpp:167-193`, which asserts its worst case does **not** fit precisely so the
   byte guard is understood as the safety mechanism.
6. `fromJson` on an over-budget document keeps every entry and still reports success (A-5).
7. `summary` longer than `MAX_SUMMARY_BYTES` in the file is re-bounded without splitting a UTF-8
   sequence.

**Already covered, deliberately not duplicated:** the `.tmp` and budget decisions are
`highlightLoadAction`/`highlightSaveAction`, exhaustively tested by
`test/highlight_file/HighlightFileActionTest.cpp`, and the constants by `test/save_budget/`.

**Not host-testable, and the plan says so rather than pretending:** `BookmarkFile.cpp` reaches
`Arduino.h` through `PersistableStore.h` (`src/util/HighlightFileAction.h:8-19` documents why there
is no stub), so its `Storage` call sequence is device-verified only. The reader's latch and rollback
live in an `Activity` and are equally device-only — `StudyStore`'s latch has no host suite either.

**Build gate:** `~/.platformio/penv/bin/pio run` once, after the last edit, and
`PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` over the whole tree.

**The human tester's list** (nothing below can be claimed from this branch):

1. Toggle a bookmark, pull the battery mid-write, reboot: bookmarks intact, or recovered from
   `.tmp` with a `RecoveredFromTemp` log line.
2. Hand-place a truncated `/.crosspoint/bookmarks/<name>.json` on the card, open the book, toggle a
   bookmark: the toast appears, the file is unchanged, and the bookmarks list refuses to open.
3. Add bookmark 65: refused, nothing written, page not flagged.
4. A pre-existing file with no `"v"` still loads every bookmark, and the next save adds `"v":1`.
5. `ESP.getFreeHeap()` before and after opening a book with 64 bookmarks — research §5's ≈35 KB
   transient figure is derived on the host, not measured on the device.

---

## Hand-back

- The three i18n keys in A-7, with the proposed text, for the task that owns the YAML.
- `STR_HIGHLIGHTS_TOO_LARGE` (`english.yaml:390`) is still unreferenced after this change; it is the
  highlights path's, not ours.
