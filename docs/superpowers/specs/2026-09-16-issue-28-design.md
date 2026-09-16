# Bookmarks: a save budget, an atomic write, and a load that cannot be overwritten

**Date:** 2026-09-16 (revised after `reviews/issue-28-spec-review-0.md`, then after
`reviews/issue-28-spec-review-1.md`)
**Status:** SPEC — not implemented
**Issue:** #28, "Bookmarks have no save budget and no atomic write"
**Target:** bereanOS, `x4pro` (ESP32-S3, 8 MB PSRAM)
**Branch:** `fix/28-bookmark-save-budget`
**Modelled on:** `src/util/HighlightFile.{h,cpp}` for the storage shell,
`lib/StudyStore/StudyStore/PassageDoc.cpp` for the format unit, and `src/study/StudyStore.cpp` for
the caller-side latch and rollback.
**Builds on:** `docs/superpowers/research/2026-09-16-issue-28-research.md` (§5 of which this pass
corrects — see below).

---

## What changed in pass 1, and why

Pass 0 was returned `BLOCKER` with 2 blockers, 1 major and 4 minors. All are accepted; none is
half-applied.

| Review finding | Change |
|---|---|
| **BLOCKER 1** — A-5 promised an over-budget file stays "recoverable by deleting", but the byte guard refused the shrinking write too, freezing a 219–242-record legacy file read-only | **A-3 rewritten.** `save()` now allows a write that is *strictly smaller than what is on disk* and still under the read cap, through a new bookmark-local `bookmarkSaveAction()`. The reviewer's options (b) and (c) are rejected below, with reasons. |
| **BLOCKER 2** — the ≈99 KB internal-SRAM premise is false on this build, and `MAX_BOOKMARKS` was a user-facing limit no one asked for | **A-2 deleted.** No record cap. The budget alone bounds the store, at ≈218 bookmarks. The memory figures are corrected here and in the research note. |
| **MAJOR 3** — the prescribed `onEnter` bail would abort the firmware | **A-9 added.** The bail sets `isCancelled` before `finish()`. |
| **MINOR 4** — A-4's prose and code disagreed on `"v": 0` | Prose corrected: only *absent* is read as 1. |
| **MINOR 5** — "enforced the same way" was untrue of a cap living outside the doc class | Moot: the cap is gone. |
| **MINOR 6** — test item 5 inverted its donor's point | Rewritten, and the byte guard's real job is now stated. |
| **MINOR 7** — six citation drifts | Fixed throughout. |

### Applied from pass 1's review (verdict `CLEAR`: 1 MAJOR, 7 MINORs, 0 BLOCKERs)

Pass 1 confirmed both pass-0 blockers and the major as genuinely fixed, having traced each through
the code. Its own findings are applied here:

| Review finding | Change |
|---|---|
| **MAJOR 1** — pass 0's test rewording was written for a spec that still had `MAX_BOOKMARKS`; applied after the cap was dropped it left `test/bookmark_doc/` asserting a worst-case *document* that no longer exists and could never fail | §Testing items 5–6 replaced with a per-record ceiling and the ≈218-record figure A-2 rests on, plus an explicit statement of why there is no worst-case-document test and why `MAX_XPATH_DEPTH` does not bound the stored field |
| **MINOR 2** — "exactly at the cap writes" contradicts `bookmarkSaveAction` | Item 6 reworded: at the cap only the shrink arm writes, and that arm is unreachable because such a file loads `Failed` |
| **MINOR 3** — A-1 refuses to spend the margin, A-3 spends all of it | Both sides now say so: the margin is headroom for future record-shape changes, and A-3 explains why the write path itself needs none |
| **MINOR 4** — the (b) rejection claimed the only recovery is pulling the SD card | Corrected: the file browser can delete the whole file with hidden files shown, but that loses every bookmark and is unreachable while the launcher finds a Bible |
| **MINOR 5** — the new stat-then-write is two `storageMutex` acquisitions | Single-writer premise stated on `BookmarkFile.h`, with the owning task named |
| **MINOR 6** — the load-failed toast would re-fire on every return through `loadCachedBookmarks()` | Guarded on the latch transition, not the result |
| **MINOR 7** — two supporting claims did not check out | Both corrected: `highlightSaveAction` has one user (its load-side sibling has three), and `PassageDoc.cpp` is not evidence of flash-neutrality because `measureBytes()` calls `measureJson` |
| **MINOR 8** — CLAUDE.md storage rule 3 was declined without being named | Named in the rejection, with why declining it is still right |

**The research note is amended, not left to rot.** Research §5 asserted ≈99 KB of *internal* SRAM
at the budget. This build sets `CONFIG_SPIRAM_USE_MALLOC=y` with
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
(`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2146-2147`; nothing in
`platformio.ini`'s `custom_sdkconfig` block, `:91-113`, touches a `SPIRAM_*` key), and
`heap_caps_realloc_default` routes anything over that limit to `MALLOC_CAP_SPIRAM`
(`framework-espidf/components/heap/heap_caps.c`, the `size <= malloc_alwaysinternal_limit` branch).
The 45 KB Arduino `String` is grown by `realloc` (`framework-arduinoespressif32/cores/esp32/WString.cpp:212`),
so it lives in **PSRAM**. The `JsonDocument` pool does not: each variant pool is at most 4,096 bytes
(`ArduinoJson/Configuration.hpp:112-120`) and string nodes are individually smaller, so every block
takes the internal branch. **Corrected figure: ≈54 KB internal + ≈45 KB PSRAM, not ≈99 KB internal.**
The research note has been amended in the same commit as this spec.

---

## Problem

`BookmarkFile::save` writes through `PersistableStoreBase::writeDocToFile`
(`src/util/BookmarkFile.cpp:62`) — non-atomic, and measuring nothing. `BookmarkFile::load` collapses
"no file yet" and "file unreadable" into one `false` (`src/util/BookmarkFile.cpp:17-19`), and the
reader discards even that (`src/activities/reader/EpubReaderActivity.cpp:1747`). The chain is
confirmed, not theorised (research §2): a set past `SDCardManager::readFile`'s 50,000-byte cap
(`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202-210`) comes back truncated,
fails `deserializeJson` with `IncompleteInput`, loads as empty, and the next save writes the empty
list over the user's file.

It is reachable. A bookmark costs ≈206 serialised bytes and the set reaches 45,000 at **219 records**
(research §1), all in one file, because the bookmark file is keyed on the book path
(`src/util/BookmarkUtil.cpp:10-12`) and this device's central book is one EPUB containing all 66
Bible books (`src/activities/launcher/LauncherActivity.cpp:94-98`).

## Goal

A bookmark save can no longer destroy the bookmark file, a bookmark that was not written is never
shown as if it had been, and a file that is already too big can still be deleted back down.

## Non-goals

- **Rescuing an already-truncated file.** A file over 50,000 bytes cannot be read by any path this
  change touches. It reports `Failed`, saving latches off, and the bytes stay on the card. Nothing
  here deletes it.
- **A record cap.** See A-2.
- **A `BookmarkDoc` in `lib/`.** `HighlightDoc` lives in `lib/Epub` because Epub code uses it;
  nothing outside `src/` uses bookmarks. Relocating `BookmarkEntry` would touch
  `EpubReaderActivity.h:13` and `ActivityResult.h` for no behavioural gain.
- **Documenting the format in `docs/file-formats.md`.** Neither highlights nor passages nor the tag
  palette are there; that file covers the EPUB caches.
- **Editing `lib/I18n/translations/*.yaml`.** Another task owns them (brief constraint). See A-7.
- **Touching `lib/Serialization/PersistableStore.{h,cpp}`.** Brief constraint, and nothing here
  needs it.
- **Widening the shared `highlightSaveAction`.** Only one store uses it today
  (`src/util/HighlightFile.cpp:76`), but its load-side sibling `highlightLoadAction` has three
  (`HighlightFile.cpp:40`, `src/study/PassageFile.cpp:72`, `src/study/TagPaletteFile.cpp:39`), and
  the two live in one header. The shrink exception is a bookmark rule, not a store rule, so it stays
  bookmark-local rather than growing parameters onto a helper other stores include.

---

## Assumptions, for the review to attack

| | Assumption | Decided in |
|---|---|---|
| **A-1** | `SAVE_BYTE_BUDGET` stays at `persist::DEFAULT_SAVE_BUDGET` (45,000). The 5,000-byte margin under the read cap is deliberate and is not traded for simplicity. | §Budget |
| **A-2** | **No record cap.** The byte budget is the only limit; it lands at ≈218 bookmarks. | §Budget |
| **A-3** | `save()` refuses on bytes, **except** a write that is strictly smaller than the bytes on disk and still ≤ the read cap, which is allowed. | §Budget |
| **A-4** | `"v": 1` is added; **absent** is read as 1. A present `"v": 0` is refused, like `PassageDoc`. | §Format |
| **A-5** | Over-budget-on-load is `Loaded`, not `Failed` — deliberately unlike `PassageDoc::fromJson`. | §Load |
| **A-6** | `addBookmark()` takes ownership of the toast, which gives the reader-menu path feedback it does not have today. | §Reader |
| **A-7** | The three refusals ship pointing at `STR_ERROR_GENERAL_FAILURE` until the i18n task lands the real keys. | §Strings |
| **A-8** | `load()` returns `LoadResult`; both call sites change and the `bool` contract in `BookmarkFile.h:11-13` goes away. | §Load |
| **A-9** | Every new exit from `EpubReaderBookmarksActivity` sets a result before `finish()`. | §Bookmarks list |

---

## Architecture

| New/changed | Mirrors | Why |
|---|---|---|
| `src/util/BookmarkDoc.{h,cpp}` (new) | `lib/StudyStore/StudyStore/PassageDoc.cpp` | Format rules and JSON shape, free of `Arduino.h`, so it is host-testable. |
| `src/util/BookmarkSaveAction.h` (new) | `src/util/HighlightFileAction.h:49-56` | The one piece of genuinely new decision logic: the shrink exception, as a `constexpr` free function. |
| `src/util/BookmarkFile.{h,cpp}` | `src/util/HighlightFile.{h,cpp}` | Storage shell: `LoadResult`/`SaveResult`, the `.tmp` decision, the guard, atomic write. |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | `src/study/StudyStore.cpp:37-47, 208-241` | Session latch on `Failed`; rollback when a save refuses. |
| `src/activities/reader/EpubReaderBookmarksActivity.cpp` | `EpubReaderBookmarksActivity.cpp:185-188` (its own existing exits) | Bail on `Failed` *with a result*; restore the entry when a delete's save refuses. |
| `test/bookmark_save_action/`, `test/bookmark_doc/` (new) | `test/highlight_file/`, `test/highlight_doc/` | Host coverage for the new pure logic. |

**`BookmarkDoc.cpp` does not instantiate the JSON serializer.** It builds and reads a `JsonDocument`;
`serializeJson`/`deserializeJson` stay inside `PersistableStore.cpp`, which is that class's stated
purpose (`lib/Serialization/PersistableStore.h:13-21`). `HighlightDoc.cpp` is the precedent:
it builds and reads a document and never measures one. `PassageDoc.cpp` is **not** — its
`measureBytes()` calls `measureJson` (`PassageDoc.cpp:134-137`), pulling exactly the template
`PersistableStore.h:16-20` exists to keep out of per-store TUs. `BookmarkDoc` escapes that because
A-5 means `fromJson` never measures: the only `measureJson` call is in `BookmarkFile::save`, which
already includes `PersistableStore.h`.

**The `.tmp` decision logic is reused, not rewritten.** `highlightLoadAction()`
(`src/util/HighlightFileAction.h:34-47`) is already shared across stores: `src/study/PassageFile.cpp`
includes it (`:8`) and switches on it (`:72-97`) under its own `LoadResult`. Bookmarks do the same.
It is exhaustively tested (`test/highlight_file/HighlightFileActionTest.cpp`).

---

## The format

```jsonc
{"v":1,"bookmarks":[{"xpath":"…","percentage":0.4831,"summary":"…","si":26,"pc":31,"pp":14,"vo":41233}]}
```

Unchanged except for `"v"`. Field names, order and the conditional `"vo"` stay exactly as
`BookmarkFile.cpp:47-56` writes them, so a file written by this build is still readable by the
current one.

**A-4 — absent version means 1; a present `"v": 0` does not.** `PassageDoc::fromJson` refuses
`version <= 0` (`lib/StudyStore/StudyStore/PassageDoc.cpp:101-103`), right for a store with no files
in the field. Bookmarks have files in the field since CrossPoint, all without `"v"`. Refusing those
would report `Failed` for every existing user and look exactly like the bug this issue fixes. So:

```cpp
const int version = doc["v"] | BookmarkDoc::FORMAT_VERSION;   // ABSENT == legacy v1
if (version <= 0 || version > BookmarkDoc::FORMAT_VERSION) return false;
```

ArduinoJson's `operator|` yields the default only when the key is absent or unconvertible, so a
present `"v": 0` keeps its value and is refused — the same treatment `PassageDoc` gives it, and
nothing in the field emits it. A future version is still refused rather than reinterpreted, which is
what CLAUDE.md's storage rule 4 asks for.

`summary` is re-bounded on the load path with `utf8SafeSummary(v, MAX_SUMMARY_BYTES)`, mirroring
`PassageDoc.cpp:116-117`: the field is bounded at creation (`src/util/BookmarkUtil.cpp:14` →
`lib/Utf8/Utf8.h:32`, 72 bytes) but a file on an SD card is not a trusted input.
`MAX_SUMMARY_BYTES = 72` preserves the current figure. `xpath` is **not** bounded — truncating it
corrupts the position it addresses (`ProgressMapper::toCrossPoint` parses it back,
`lib/ProgressMapper/ProgressMapper.cpp:804-811`), and the one parser that consumes it already
rejects an over-long element name rather than overflowing (`ProgressMapper.cpp:176`).

---

## The budget

**A-1 — `SAVE_BYTE_BUDGET = persist::DEFAULT_SAVE_BUDGET` (45,000), and the margin stays.**
`src/study/TagPaletteFile.cpp:70` uses that constant directly;
`src/util/HighlightFile.h:38-42` arrives at the same 45,000 by hardcoding it, with the same stated
reason — headroom under the 50,000 read cap. The reviewer's option of raising the budget to ~49,000
so the loadable and saveable bands coincide is **rejected**: that 5,000-byte margin is what stands
between a miscount — a field added to `BookmarkEntry`, an escape-heavy summary, a serialiser
change — and silent truncation, which is unrecoverable. Simplicity is not worth trading for it.
The margin's job is headroom for *future* changes to the record shape, not for the write path
itself; A-3 spends it on one path for the reason given there.

**A-2 — no record cap.** Pass 0 proposed `MAX_BOOKMARKS = 64` on the strength of research §5's
≈99 KB internal-SRAM figure. That figure was wrong (see *What changed*), and neither issue #28 nor
its brief asks for a cap. The corrected cost of a full-budget document is:

| | internal SRAM | PSRAM |
|---|---|---|
| transient, during load | ≈54 KB (`JsonDocument` pool, ≤4 KB blocks) | ≈45 KB (the `String`) |
| resident for the session, ≈218 entries | ≈31 KB (two small `std::string` buffers per entry) | ≈17 KB (the vector's own block) |

The transient figure is the same one `HighlightFile` already accepts at the same budget
(`HighlightFile.h:42`), so this adds no new precedent. The resident figure is user data the user
asked for. A cap would add a hard product limit to a device whose central book is the whole Bible —
research §1 puts 219 bookmarks at one per 140 chapters — and `EpubReaderActivity.cpp:55`'s reserve
of 16 is an initial `reserve()`, not a ceiling. If the budget ever needs to be a count, that is a
product decision with its own issue.

**A-3 — refuse on bytes, but never refuse a shrink.** This is the pass-0 blocker. The guard must
stop a document *growing* past the budget, and must not stop one *shrinking* back under it, or a
legacy file between 45,000 and 50,000 bytes freezes: every delete measures over budget, is refused,
and the rollback puts the entry back, so the set can never shrink. New bookmark-local logic,
`src/util/BookmarkSaveAction.h`, shaped exactly like `highlightSaveAction`
(`HighlightFileAction.h:49-56`):

```cpp
enum class BookmarkSaveAction : uint8_t { Write, RefuseTooLarge };

// Growth stops at `budget`. A document already over it may still be written when it is strictly
// SHRINKING and stays readable, so an inherited over-budget file can be deleted back under the cap
// instead of freezing read-only. `bytesOnDisk` is 0 when no file exists yet.
constexpr BookmarkSaveAction bookmarkSaveAction(const size_t measuredBytes, const size_t bytesOnDisk,
                                                const size_t budget, const size_t readCap) {
  if (measuredBytes > readCap) return BookmarkSaveAction::RefuseTooLarge;
  if (measuredBytes <= budget) return BookmarkSaveAction::Write;
  return measuredBytes < bytesOnDisk ? BookmarkSaveAction::Write : BookmarkSaveAction::RefuseTooLarge;
}
```

`readCap` is `persist::SD_READ_TRUNCATION_CAP` (`lib/Serialization/SaveBudget.h:19`), used for the
job its own comment gives it: the point past which data disappears silently. A file at exactly
50,000 still reads whole — `SDCardManager.cpp:204`'s loop is `readSize < maxSize` — so `>` is the
correct comparison.

`bytesOnDisk` is read only when it can change the answer, i.e. only when `measuredBytes > budget`:

```cpp
size_t bytesOnDisk = 0;
if (measured > SAVE_BYTE_BUDGET) {
  HalFile existing;
  if (Storage.openFileForRead(MODULE, path, existing)) bytesOnDisk = existing.size();
}
```

`HalStorage.h:40-41` and `:77` are the API; the common path never opens the file, so a normal save
costs exactly what `HighlightFile::save` costs today.

**The two alternatives the reviewer offered are rejected, and one of them for a reason the review
did not have.**

- *(c) Raise the budget to ~49,000* — rejected under A-1.
- *(b) Make over-budget-on-load `Failed` and latch* — rejected. The review reasoned that a
  too-large file could then be removed over the web server, so read-only was survivable. **That
  escape hatch does not exist on this firmware.** `ActivityManager::goToFileTransfer()`
  (`src/activities/ActivityManager.cpp:200-202`) is the only thing that constructs
  `CrossPointWebServerActivity`, its only caller is `HomeActivity::onFileTransferOpen()`
  (`src/activities/home/HomeActivity.cpp:325`), and `HomeActivity` is never instantiated anywhere in
  `src/` — it survives only as the `isHomeActivity()` check at `ActivityManager.cpp:77`. That is
  issue #29, "The web server stack is unreachable, and the dev side-load path with it". The one
  on-device path that does exist is far worse than it sounds: Settings → show hidden files
  (`src/SettingsList.h:376`) → File Browser → delete the whole file
  (`src/activities/home/FileBrowserActivity.cpp:56, 170`). **That destroys every bookmark for the
  book**, and it is not reachable at all while the launcher finds a Bible, since the browser is
  only opened from `LauncherActivity::openBible()` when no Bible-looking book is in recents
  (`src/activities/launcher/LauncherActivity.cpp:498-503`). So under (b) a user whose Bible
  bookmarks file is over budget has no on-device recovery at all, and off-device means pulling the
  SD card. A-3 is preferred because it recovers one record at a time.

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
before renaming (`PersistableStore.cpp:38-39`), so a power loss there leaves `.tmp` as the only copy
(research §6).

**A-5 — over budget on load is `Loaded`, not `Failed`.** `PassageDoc::fromJson` returns
`measureBytes() <= SAVE_BYTE_BUDGET` (`PassageDoc.cpp:131`), folding "too big" into the same failure
as "unreadable". The two states are genuinely different: *unreadable* is unrecoverable and must
never be written over; *too big but read perfectly* is recoverable by deleting — and under A-3 that
is now true rather than merely asserted. `fromJson` never drops an entry for budget, for the reason
`PassageDoc.cpp:125-128` gives: dropping silently loses a file's tail and the next save makes it
permanent.

### `BookmarkFile::save(bookPath, bookmarks) -> SaveResult`

1. `BookmarkDoc::toJson(bookmarks, json)`, `measured = measureJson(json)`.
2. If `measured > SAVE_BYTE_BUDGET`, stat the existing file for `bytesOnDisk` (above).
3. `bookmarkSaveAction(measured, bytesOnDisk, SAVE_BYTE_BUDGET, persist::SD_READ_TRUNCATION_CAP)` →
   `RefuseTooLarge` returns `SaveResult::TooLarge` **before** `mkdir` or any write.
4. `Storage.mkdir(BookmarkUtil::getBookmarksDir())` — `writeDocToFileAtomic` only ensures
   `/.crosspoint` (`PersistableStore.cpp:23`), the subdirectory stays ours (`BookmarkFile.cpp:59-60`).
5. `writeDocToFileAtomic` → `Ok` / `WriteFailed`.

`SaveResult` is `{Ok, TooLarge, WriteFailed}`, copied from `HighlightFile.h:37`.

**Single-writer, and `BookmarkFile.h` must say so.** `HalStorage` serialises each *call*, not a
sequence: the `bytesOnDisk` probe takes and releases `storageMutex`, and so does every call inside
`writeDocToFileAtomic`. A second writer could therefore change the file between the measurement and
the write and make the shrink decision race. The owning task is the main/UI task — all four call
sites are reader-side activities (`EpubReaderActivity.cpp:1747, 1801`,
`EpubReaderBookmarksActivity.cpp:33, 175`). `BookmarkFile.h` carries `HighlightFile.h:16-18`'s note
verbatim, extended with this hazard, which is also what CLAUDE.md's storage rule 5 ("name its owning
task") asks for.

### `EpubReaderActivity` — the latch

`loadCachedBookmarks()` (`:1737-1749`) keeps the result and, on `Failed`, sets a new
`bookmarksSaveDisabled` member and surfaces it, mirroring what `onEnter` already does for the study
store nine lines later (`:240`, then `:247-250`):

```cpp
if (BookmarkFile::load(epub->getPath(), cachedBookmarks) == BookmarkFile::LoadResult::Failed &&
    !bookmarksSaveDisabled) {
  bookmarksSaveDisabled = true;                              // toast on the TRANSITION, not the result
  ReaderUtils::showMessage(renderer, tr(/* A-7 */));
}
```

The latch is a session property, like `StudyStore::saveDisabled_`, whose survival across
`closePublication` is explained at `src/study/StudyStore.cpp:58-62`. `loadCachedBookmarks()` is also
called on return from the progress-change flow (`:689`), which is why the toast is guarded on the
latch transition rather than on the result: `GUI.drawPopup` ends in `renderer.displayBuffer()`
(`src/components/themes/BaseTheme.cpp:853`), a full e-ink refresh, and under a persistent SD fault
an unguarded toast would fire on every return from the bookmarks list immediately before
`openReaderMenu()` repaints. The latch is never cleared, so the message is shown exactly once per
session.

### `EpubReaderActivity::addBookmark()` — refuse, then roll back

Current shape: mutate `cachedBookmarks`, set `currentPageBookmarked`, save, `LOG_ERR` on failure
(`:1765-1803`). A refusal leaves the page flagged as bookmarked and the popup reading "Bookmark
added" (`:1291`) for something that never reached the card. New shape, mirroring
`StudyStore::addPassage`'s *"saves synchronously and rolls back its own append on failure"*
(`StudyStore.cpp:226-230`, and `removePassage`'s backup/restore at `:233-241`):

1. `if (bookmarksSaveDisabled)` → toast, return. Mirrors `PassageSelectActivity.cpp:29-33`.
2. Decide add vs remove exactly as today (`:1765-1774`).
3. Mutate, then save.
4. On anything but `Ok`: undo the insert, or re-insert the removed entries at their indices, restore
   `currentPageBookmarked`, and set the toast to the failure.

The remove path erases *all* matching entries (`:1767-1772`), so the rollback keeps the erased
copies, not just an index.

**A-6 — `addBookmark()` sets the toast itself.** Today the three callers do it: the Confirm hold
(`:497-500`), the Home-key hold (`:527-531`), and the reader menu (`:877`) — which sets nothing, so
the menu path gives no feedback at all. Moving `showBookmarkMessage = true; bookmarkMessageTime =
millis();` into `addBookmark()` lets one place choose between five outcomes and fixes the menu
path's missing feedback. The Home-key path's `if (!showBookmarkMessage)` guard (`:526`) stays.
`bookmarkRemoved` (`EpubReaderActivity.h:46`, read only at `:1291`) becomes an enum:
`{Added, Removed, TooLarge, SaveFailed, LoadDisabled}`.

### `EpubReaderBookmarksActivity`

**A-9 — a bail must still set a result.** `onEnter` (`:33`) on `Failed` shows the message and
finishes, but *not* the way `PassageSelectActivity.cpp:29-33` does. That activity is launched with a
handler that ignores its result (`EpubReaderActivity.cpp:343`, `[this](const ActivityResult&) {
requestUpdate(); }`); this one is launched with `progressChangeResultHandler`
(`EpubReaderActivity.cpp:871-873`), which destructures on the not-cancelled branch —
`std::get<ProgressChangeResult>(result.data)` at `EpubReaderActivity.cpp:693`. `ActivityResult`
default-constructs to `isCancelled = false` and a `std::monostate` variant
(`src/activities/ActivityResult.h:88-96`), and `ActivityManager` moves the result out and calls the
handler unconditionally (`src/activities/ActivityManager.cpp:105, 122-129`). A bare `finish()` would
therefore take the `else` branch and `std::get` a `monostate` → `std::bad_variant_access` →
`std::terminate` under `-fno-exceptions`. Every existing exit already sets a result
(`EpubReaderBookmarksActivity.cpp:91-92, 136-137, 187-188`); the new one does too:

```cpp
ActivityResult result;
result.isCancelled = true;
setResult(std::move(result));
finish();
```

The activity does not currently include `ReaderUtils.h` (`showMessage` has no hits in that file), so
the toast needs that include; `ReaderUtils::showMessage` is `GUI.drawPopup` (`ReaderUtils.h:233`).

- `deleteSelectedBookmark` (`:169-176`) — keep the erased entry, and on a non-`Ok` save re-insert it
  at its index, call `rebuildBookmarkRowItems()` a second time and show the failure. The rebuild
  ordering already has a documented reason (`:171-173`: rows must not alias erased storage) and a
  rollback has the same hazard in reverse.

---

## Error handling

| Condition | `BookmarkFile` | What the user sees |
|---|---|---|
| No file yet | `Empty` | nothing; empty list |
| `.tmp` is the only copy | `RecoveredFromTemp` | nothing; bookmarks are there |
| Unreadable / unparseable | `Failed` + `LOG_ERR` | load-failed toast at open; then a refusal toast from Toggle Bookmark. The Bookmarks menu entry is already hidden, because `EpubReaderActivity.cpp:283` gates it on `!cachedBookmarks.empty()` and a `Failed` load leaves it empty |
| Read fine, over budget, growing | `Loaded`; `save` → `TooLarge` | refusal toast, nothing mutated on disk |
| Read fine, over budget, shrinking | `Loaded`; `save` → `Ok` | the delete persists; the set walks back under budget |
| `writeDocToFileAtomic` fails | `WriteFailed` + `LOG_ERR` | save-failed toast, in-memory state rolled back |

Every path is `LOG_ERR` + a false-ish return (CLAUDE.md error-handling rule 1). No refusal creates or
modifies a file — that is `bookmarkSaveAction`'s contract, inherited from
`HighlightFileAction.h:49-51`.

**A-7 — strings.** Three keys are needed and none exists:

| Key | Text | Modelled on |
|---|---|---|
| `STR_BOOKMARKS_LOAD_FAILED` | "Bookmarks unreadable - saving disabled this session" | `STR_HIGHLIGHTS_LOAD_FAILED`, `english.yaml:389` |
| `STR_BOOKMARKS_TOO_LARGE` | "This book already has too many bookmarks" | `STR_HIGHLIGHTS_TOO_LARGE`, `english.yaml:390` |
| `STR_BOOKMARK_SAVE_FAILED` | "Could not save bookmark" | `STR_TAG_SAVE_FAILED`, `english.yaml:400` |

The brief forbids editing the YAML and says to use `STR_HIGHLIGHTS_TOO_LARGE` if it fits. It does
not: it reads "This book already has too many highlights" and would tell a user who pressed *Toggle
Bookmark* about highlights. So all three ship pointing at `STR_ERROR_GENERAL_FAILURE` ("Error:
General failure", `english.yaml:201`) — noun-free and true. The reader's three go through one
function, `bookmarkToastString()`; the bookmarks list calls `tr()` directly at its two sites, because
it has no five-way toast to route. The swap is therefore three lines in `EpubReaderActivity` plus two
calls in `EpubReaderBookmarksActivity`. The hand-back names the keys. This
is a knowingly poor message shipped to keep criterion 3 met rather than silently dropped.

---

## Rejected alternatives

- **Stream the read** (`src/study/PassageFile.cpp:15-49`'s `HalFileReader` over `HalFile`). It
  removes the 50,000-byte cap entirely. **This declines CLAUDE.md's storage rule 3** — *"Stream, not
  `Storage.readFile`, if it can exceed ~40 KB"* — which at a 45,000-byte budget this store can, and
  which `PassageFile` follows. Declined deliberately: streaming removes the read cap but not the
  budget refusal, so the shrink exception (A-3) would be needed either way and the
  loadable-but-unsaveable band would still exist; with the corrected memory picture it also buys
  almost nothing in internal SRAM, because the `String` it eliminates is in PSRAM and the ≈54 KB
  pool it does not eliminate is the internal cost. It would mean duplicating a file-local class into
  a second TU or promoting it to a shared header, in a data-loss fix. Revisit whenever the budget
  moves, or with issue #29.
- **Raising `SAVE_BYTE_BUDGET` to ~49,000** — A-1.
- **Read-only over budget** — A-3, and issue #29 is why.
- **A record cap** — A-2.
- **Renaming `highlightLoadAction` to something store-neutral.** `PassageFile.cpp:72` already reuses
  it under that name; a rename is a cross-store refactor with its own test churn.

---

## Testing strategy

**Host — `test/bookmark_save_action/`**, modelled on `test/highlight_file/` (which puts
`${REPO_ROOT}/src` on the include path at `test/highlight_file/CMakeLists.txt:9-12` and needs no
`.cpp` beyond the test). This is where the genuinely new logic lives, so it is exhaustive:

1. Under budget always writes, whatever is on disk.
2. Over budget and growing (`measured >= bytesOnDisk`) refuses — including the equal case.
3. Over budget and shrinking writes — the pass-0 blocker, pinned: a 230-record document minus one
   entry must write.
4. Over the read cap refuses even when shrinking.
5. `bytesOnDisk == 0` (no file yet) refuses anything over budget.
6. Exactly at the budget writes whatever `bytesOnDisk` is. Exactly at the read cap writes **only**
   on the shrink arm (`bytesOnDisk > readCap`) and refuses otherwise — and that arm is unreachable
   in practice, because a file over the cap reads back truncated, loads `Failed` and latches saving
   off before `save()` is ever called. The boundary is pinned anyway, with that note in the test.

**Host — `test/bookmark_doc/`**, modelled on `test/highlight_doc/` (which compiles
`lib/Epub/Epub/HighlightDoc.cpp` and `lib/Utf8/Utf8.cpp` against `ArduinoJson` and
`GTest::gtest_main`). It compiles `src/util/BookmarkDoc.cpp` + `lib/Utf8/Utf8.cpp`:

1. Round-trip: every field survives `toJson` → `fromJson`, including absent `vo`.
2. `"v"` absent parses as v1 with all entries (A-4) — the legacy-file regression guard.
3. `"v": 0` and `"v": 2` are both refused; `"v": 1` is accepted.
4. `fromJson` on an over-budget document keeps every entry and reports success (A-5).
5. **Per-record cost, pinned.** One realistic-worst record — a deep xpath with four-digit indices, a
   fully escaped 72-byte summary, `vo` present — is measured and asserted under a pinned per-record
   ceiling, with the measured value in the failure message. A new field on `BookmarkEntry` fails
   here first, which is the one thing this suite can usefully guard.
6. **The figure A-2 rests on.** `SAVE_BYTE_BUDGET / bytesPerRecord` is still at least the ≈218
   records §Budget quotes. Dropping the record cap rests on that number and nothing else tests it.
7. `summary` longer than `MAX_SUMMARY_BYTES` in the file is re-bounded without splitting a UTF-8
   sequence.

There is deliberately **no** worst-case-*document* test here, and `HighlightDocTest.cpp:167-193` is
not a donor for one. That test builds a document at `HighlightDoc::MAX_HIGHLIGHTS`
(`HighlightDocTest.cpp:178`); a count cap is what makes "the worst case" a finite object, and A-2
removes it. A document assertion would carry ~49,500 bytes of slack and could never fail. The byte
bound is `bookmarkSaveAction`, pinned exhaustively by the suite above. Nor would `MAX_XPATH_DEPTH`
bound such a document: `parseXPathSteps` merely stops reading after 16 steps
(`ProgressMapper.cpp:168`) while `buildParagraphXPath` emits one segment per ancestor with no limit
(`lib/ProgressMapper/ChapterXPathResolver.cpp:55-57`) — which is exactly why §Format says the field
is unbounded.

Registered in `test/CMakeLists.txt` beside the other `add_subdirectory` lines.

**Already covered, deliberately not duplicated:** the `.tmp` decision is `highlightLoadAction`,
exhaustively tested by `test/highlight_file/HighlightFileActionTest.cpp`; the shared constants by
`test/save_budget/`.

**Not host-testable, and the plan says so rather than pretending:** `BookmarkFile.cpp` reaches
`Arduino.h` through `PersistableStore.h` (`src/util/HighlightFileAction.h:8-19` documents why there
is no stub), so its `Storage` call sequence — including the new `bytesOnDisk` stat — is
device-verified only. The reader's latch and rollback live in an `Activity` and are equally
device-only; `StudyStore`'s latch has no host suite either.

**Build gate:** `~/.platformio/penv/bin/pio run` once, after the last edit, and
`PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` over the whole tree.

**The human tester's list** (nothing below can be claimed from this branch):

1. Toggle a bookmark, pull the battery mid-write, reboot: bookmarks intact, or recovered from
   `.tmp` with a `RecoveredFromTemp` log line.
2. Hand-place a truncated `/.crosspoint/bookmarks/<name>.json` on the card, open the book, toggle a
   bookmark: the toast appears and the file is unchanged.
3. Hand-place a 47,000-byte bookmark file: it opens, adding is refused, and **deleting works and
   persists** — the pass-0 blocker, on real hardware.
4. A pre-existing file with no `"v"` still loads every bookmark, and the next save adds `"v":1`.
5. `ESP.getFreeHeap()` **and** `ESP.getFreePsram()` before and after opening a book with a large
   bookmark file. Both are needed: `getFreeHeap` is `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
   (`framework-arduinoespressif32/cores/esp32/Esp.cpp:163-165`), so on its own it cannot see the
   ≈45 KB the `String` takes from PSRAM, and a small reading would look like the model is wrong when
   it is only mis-attributed.

---

## Hand-back

- The three i18n keys in A-7, with the proposed text, for the task that owns the YAML.
- Research §5's memory figures were corrected in this pass; the amendment is in the research note.
- Issue #29 (the unreachable web server) is load-bearing for A-3's reasoning: if it is ever fixed,
  the rejected option (b) becomes survivable, though still worse.
