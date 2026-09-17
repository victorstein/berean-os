# Bound `RecentBooksStore`'s strings, then tighten its budget — issue #40

Design for `fix/40-bound-recent-book-strings`. Research note:
`docs/superpowers/research/2026-09-17-issue-40-research.md`.

Modelled on **`src/util/BookmarkDoc.{h,cpp}` + `src/util/BookmarkFile.cpp` +
`test/bookmark_doc/`** — the format rules, their caps and their budget live in an
Arduino-free "Doc" pair that the host suite compiles; the store is the shell that
moves bytes. `study::PassageDoc::add` (`lib/StudyStore/StudyStore/PassageDoc.cpp:22-38`)
is the model for the truncate-at-add half specifically.

---

## Problem

`RecentBooksStore` looks bounded and is not.

`MAX_RECENT_BOOKS = 10` (`src/RecentBooksStore.h:21`) caps the entry *count*. Each
entry is four bare `std::string`s (`src/RecentBooksStore.h:8-15`), and `title` and
`author` arrive straight from EPUB metadata with nothing in between:

- `src/activities/reader/ReaderActivity.cpp:60` — `addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath())`
- `src/activities/reader/EpubReaderActivity.cpp:435` — same shape, from `epub->getTitle()`/`getAuthor()`

The origin is `BookMetadataCache::BookMetadata` (`lib/Epub/Epub/BookMetadataCache.h:15-16`),
filled from the publication's OPF. Nothing bounds it.

So `SAVE_BUDGET` is stuck at the shared default, and the comment at
`src/RecentBooksStore.h:29-35` says so in as many words, with
`src/RecentBooksStore.cpp:139-140` pinning it by `static_assert` and citing
"the spec's A5" — `docs/superpowers/specs/2026-09-16-issue-27-design.md:415-434`,
which accepted the default *on condition that the truncation be filed as an
issue*. This is that issue.

Two things the issue does not carry, both established in the research note (§4):

1. **`path` and `coverBmpPath` are unbounded too.** Measured against the pinned
   ArduinoJson 7.4.2, ten entries at 255-byte path and 255-byte cover already
   measure 5,641 bytes with empty titles. "Bound `title` and `author` → the worst
   case is computable" is not true on its own.
2. **JSON escaping is a multiplier.** 128 bytes of `"` or `\` serialise to 256. A
   byte cap on a string does not bound its serialised length by itself.

### Why a refusal is the thing to avoid

`saveToFileAtomic` measures and refuses before writing (`lib/Serialization/PersistableStore.h:170-175`).
A refusal here is silent to the user — one `LOG_ERR` on a serial port nobody is
watching (`PersistableStore.h:172`), and recents simply stops updating. That is
exactly the failure mode #27 declined to risk. A tightened budget is only
defensible if it cannot be reached by a real publication on a real card.

---

## Goal

In the order the issue sets, because it is the order that makes the second step safe:

1. Bound `title` and `author` at every path that writes them, on a UTF-8-safe
   boundary, at a documented maximum picked against what the recents list can
   actually render.
2. With the fields bounded, derive the store's worst-case serialised size from
   named constants and tighten `SAVE_BUDGET` to that derived figure.
3. Make both halves fail loudly in CI if a later change invalidates them.

---

## Non-goals

- **A user-visible refusal message.** That is issue #39. `lib/I18n/translations/`
  is not touched by this change.
- **A format version for `recent.json`.** See **A9**.
- **Truncating `path`.** It is the store's key: `pruneMissing`
  (`src/RecentBooksStore.cpp:113-117`) deletes any entry whose path does not
  resolve, so a truncated path deletes itself on the next boot. See **A6**.
- **Removing `getDataFromBook`** (`src/RecentBooksStore.cpp:119-137`). It has no
  callers — `grep -rn getDataFromBook src` finds only its declaration and
  definition — but deleting dead code is not this change.
- **The unreachable 3-line home cards.** `HomeActivity` is never instantiated
  (research §3); `BaseTheme.cpp:715`, `lyra/LyraTheme.cpp:469` and
  `lyra/Lyra3CoversTheme.cpp:91` wrap a title to three lines and no user reaches
  them. They must not drive the cap.
- **Changing what `updatePath` writes** (`src/RecentBooksStore.cpp:95-109`).

---

## Assumptions

Each is a behavioural decision. Attack them individually.

**A1 — `MAX_TITLE_BYTES = 128`.**
*Why:* the cap must sit above what any reachable surface can display, so a real
publication never renders differently. The widest reachable surface is the
`RecentBooksActivity` row, a deliberately single-line label
(`src/activities/home/RecentBooksActivity.cpp:39`, and the comment at `:174-175`:
*"No maxLines=2 here"*) that `FreeInkUIGfxRenderer.h:177-179` hands to
`renderer.truncatedText`, which ellipsises at the row width
(`lib/GfxRenderer/GfxRenderer.cpp:1777-1788`). Every other surface already caps
the same field far lower — 48 bytes at `src/activities/launcher/LauncherActivity.cpp:83`
and `src/activities/catalog/PublicationsActivity.cpp:56`, 40 at `:103`, 30 at `:132`.
Real publication titles in this device's scope measure 39-72 bytes
("Traducción del Nuevo Mundo de las Santas Escrituras (revisión de 2019)" = 72).
128 is ~1.8× the longest of those, and ~2.7× the largest cap any other surface
applies.
*Attack it:* the row is ~700 px wide (800 px screen, `contentSidePadding = 20` in
all three themes — `src/components/themes/BaseTheme.h:142`, `lyra/LyraTheme.h:17`,
`roundedraff/RoundedRaffTheme.h:20` — less a 32 px icon,
`RecentBooksActivity.cpp:41`). **This spec does not measure the row's capacity in
bytes**; doing so needs the `EpdFont` tables and a renderer, which the host suite
cannot build. A title of 150 narrow ASCII characters could plausibly fit the row
today and would be clipped 22 bytes earlier afterwards. The issue accepts this
class of change explicitly ("a long title will render differently in the recents
list afterwards"); if the reviewer disagrees, 160 or 192 costs 320 or 640 bytes
of budget and changes nothing else.

**A2 — `MAX_AUTHOR_BYTES = 96`.**
*Why:* same reasoning, one surface. `author` renders only as the row subtitle
(`RecentBooksActivity.cpp:40`), truncated at the same width. Real values measure
38-52 bytes ("Watchtower Bible and Tract Society of New York, Inc." = 52); 96 is
~1.8× the longest, symmetric with A1.
*Attack it:* a multi-author sideloaded EPUB could exceed 96. The visible result is
an ellipsis in a subtitle that was already being ellipsised.

**A3 — the truncation helper is `utf8SafeSummary`, not `utf8SafeTruncateBuffer`.**
*Why:* the issue names `utf8SafeTruncateBuffer` as "the existing helper" and
`utf8SafeSummary` as "the nearest precedent". `utf8SafeTruncateBuffer`
(`lib/Utf8/Utf8.cpp:148-165`) indexes `buf[len - 1]` after checking only
`len <= 0`; it never checks the buffer is `len` bytes long, and
`test/utf8_summary/Utf8SummaryTest.cpp:33-36` exists specifically to pin that
`utf8SafeSummary` clamps before calling it. Going through `utf8SafeSummary`
(`lib/Utf8/Utf8.cpp:185-201`, guard at `:198`) gets that clamp for free, and it is
what every comparable site in this repo already does: `PassageDoc.cpp:28-29`
(with the reasoning spelled out at `:24-26` — *"utf8SafeSummary, never resize()"*),
`HighlightDoc.cpp:62-63,159-160`, `BookmarkDoc.cpp:43`.
*Attack it:* `utf8SafeSummary` also normalises — it collapses whitespace runs,
strips `\n` and trims. `"Awake!   2026"` is stored as `"Awake! 2026"` even though
it is under the cap. That is a second behaviour change beyond truncation. It is
the right one: the launcher already applies exactly this transform to this exact
field (`LauncherActivity.cpp:83`), so the normalised form is what the user
already sees there, and a newline in an OPF title renders as a box today.
Note the known limit — `utf8SafeSummary`'s whitespace predicate is `std::isspace`,
which is ASCII-only, so U+00A0 and U+202F survive (see
`memory/highlight-passage-extraction-differs-offline-vs-device.md`). They count
against the byte cap as 2-3 bytes each. Harmless here; worth knowing.

**A4 — the caps are applied at three write paths, not the one the issue names.**
*Why:* the issue names `addBook` (`src/RecentBooksStore.cpp:42`). There are two
more. `updateBook` (`:67-80`) assigns `book.title = title; book.author = author;`
from `HomeActivity.cpp:75`. `fromJson` (`:31-34`) reads both back off the SD card
with no bound at all. Bounding only `addBook` leaves the store unbounded and the
derived budget false. `BookmarkDoc.h:31-32` states the load-side rule directly —
*"A file on an SD card is not a trusted input, so the load path re-bounds it"* —
and `BookmarkDoc.cpp:43` implements it.
*Attack it:* this is scope the issue did not ask for. It is not optional: without
the load-side bound, a `recent.json` written by 1.9.10 with ten 4,000-byte titles
loads in full and then refuses every subsequent save forever, which is the
"legacy file cannot shrink" trap already recorded for bookmarks
(`memory/bookmark-save-budget-shrink-exception.md`, issue #28).

**A5 — a load that truncated anything calls `requestResave()`.**
*Why:* A4 shrinks over-long entries in memory at load. Without a resave the file
on disk stays over budget, and the in-memory copy silently disagrees with it until
something else writes. `requestResave()` exists for precisely this — *"fromJson()
implementations call this when the on-disk JSON used a legacy shape that was
upgraded in memory"* (`lib/Serialization/PersistableStore.h:40-44`), performed
after the lock is released (`:194-197`).
*Attack it:* it costs one SD write on the first boot after upgrade for any user
with an over-long title, and zero after that, because the flag is set only when a
string actually changed. A blanket resave every boot would be wrong; this is not
that.

**A6 — `path` is not truncated. Its contribution to the budget is an explicit
allowance of 512 bytes, `PATH_BUDGET_ALLOWANCE`.**
*Why:* `path` is the store's key — `addBook` matches on it (`:49`), `removeByPath`
(`:84`), `updatePath` (`:97-98`), and `pruneMissing` (`:115`) erases any entry
whose path does not resolve via `Storage.exists` (`:111`). A truncated path
resolves to nothing and the entry deletes itself on the next boot. So the budget
must *allow* for path rather than bound it. 512 bytes is two full FAT LFN
components (255 each, `SdFat/src/common/FsStructs.h:108-111`); a realistic path
on this device — `/Publicaciones/Atalaya/Edición de estudio/2026/w_S_202601.epub`
— is under 80.
*Attack it:* this is the one genuinely unenforced bound in the design. A user who
nests a book under more than ~512 bytes of directory names gets a save refusal
with no message, which is the exact failure #27 refused to accept — just moved to
a far less reachable trigger. The alternatives considered and rejected:
(a) refuse the *entry* at `addBook` when its path exceeds the allowance — bounds
the store completely, but invents a silent per-book behaviour this repo has no
precedent for; (b) keep `persist::DEFAULT_SAVE_BUDGET` — safe, and abandons half
the issue. If the reviewer wants more margin, raising this constant to 1024 costs
5,120 bytes of budget and nothing else.

**A7 — `coverBmpPath`'s allowance is 128 bytes, `COVER_PATH_BUDGET_ALLOWANCE`.**
*Why:* unlike `path`, this field is bounded *by construction*. Both `addBook` call
sites pass `getThumbBmpPath()` (`ReaderActivity.cpp:60`,
`EpubReaderActivity.cpp:435`), which is `cachePath + "/thumb_[HEIGHT].bmp"`
(`lib/Epub/Epub.cpp:653`), and `cachePath` is
`cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(filepath))`
(`lib/Epub/Epub.h:48`). With `cacheDir = "/.crosspoint"` (`RecentBooksStore.cpp:132`)
that is 18 + at most 20 digits + 19 = **57 bytes maximum**. 128 is 2.2× it.
*Attack it:* `PublicationDownloader.cpp:120` and `EpubReaderActivity.cpp:148` call
`updatePath(..., oldCachePath, newCachePath)`, which rewrites the prefix
(`RecentBooksStore.cpp:103-104`). A future caller passing a long `newCachePath`
would break the 57-byte derivation; today both pass a `/.crosspoint`-shaped path.

**A8 — the escape multiplier is 2 for `title`/`author` and 1 for `path`/`coverBmpPath`.**
*Why:* measured against ArduinoJson 7.4.2 (research §4): 128 bytes of `"` or `\`
serialise to 256, 128 bytes of 4-byte UTF-8 serialise to 128, and 128 bytes of
`0x01` serialise to 128 — this version does **not** `\u`-escape control
characters, so 2 is the true worst case and `"`/`\` are the only characters that
reach it. Neither can appear in a path: `lfnReservedChar`
(`SdFat/src/common/FsStructs.h:108-111`, SdFat 2.3.1 per its
`library.properties`) rejects `"`, `\` and everything below `0x20` for both exFAT
names and FAT LFN, so a path read off this card cannot contain one.
*Attack it:* this couples the arithmetic to an ArduinoJson version. It is pinned
in two places — `platformio.ini:151` and `test/CMakeLists.txt:28-32` both say
7.4.2 — and the worst-case test (see Testing) measures a real document rather
than trusting the multiplier, so a serialiser change fails the test rather than
corrupting the budget.

**A9 — no `FORMAT_VERSION` is added to `recent.json`.**
*Why:* `CLAUDE.md` requires a format version a future build refuses rather than
reinterprets, and `BookmarkDoc.h:23` has one. But this change makes **no
structural change**: the same four keys carry the same meanings, and only the
*lengths* of two values shrink. Every build that has ever read this file reads
those strings verbatim, so an old build reads a new file correctly and a new build
reads an old file correctly (re-bounding it per A4). Introducing a `"v"` key now
would make 1.9.10 and earlier read a file they would otherwise handle, for no
gain.
*Attack it:* the next change to this store's shape will have to add versioning
*and* handle the unversioned legacy file, exactly as `BookmarkDoc.cpp:28-32` does
("an absent `v` reads as 1"). That cost is deferred, not avoided.

**A10 — `SAVE_BUDGET` is a derived `constexpr`, not a rounded magic number.**
*Why:* the issue asks for "a real figure". A figure a reader can recompute from
named constants is auditable; `12000` is not. The derivation is in **Architecture**
below and yields **11,421**. `BookmarkDoc.h:35-42` chose the other way
(`MAX_RECORD_BYTES = 420` against a measured 386) and had to explain the 34 bytes
of slack in a comment; deriving avoids that conversation entirely because every
allowance is already generous and named.
*Attack it:* an odd-looking constant invites someone to "tidy" it. The
`static_assert` and the test comments have to say it is derived.

**A11 — the shape moves to `src/util/RecentBooksDoc.{h,cpp}` and `RecentBook` to
`src/RecentBook.h`; `RecentBooksStore` becomes the shell.**
*Why:* `RecentBooksStore.cpp` cannot be built on the host — it reaches
`<Arduino.h>` through `PersistableStore.h:3`, and also includes `<Epub.h>` and
`<HalStorage.h>`. `src/util/HighlightFileAction.h:10-15` states the repo's
settled reasoning for that wall. A test that only pins arithmetic would duplicate
the JSON shape and therefore *not* catch the thing that matters — a new
string-shaped field on `RecentBook`. `BookmarkDocTest.cpp:142-160` catches it
precisely because it calls the real `BookmarkDoc::toJson`. This mirrors that split
one-for-one: `src/BookmarkEntry.h` → `src/RecentBook.h`,
`src/util/BookmarkDoc.{h,cpp}` → `src/util/RecentBooksDoc.{h,cpp}`,
`src/util/BookmarkFile.cpp` (shell) → `src/RecentBooksStore.cpp` (shell),
`test/bookmark_doc/` → `test/recent_books_doc/`.
*Attack it:* it is a refactor riding on a "good first issue". It is four small
files and zero behaviour of its own, and without it the budget guard is arithmetic
checking arithmetic.

**A12 — `RecentBooksDoc` never calls `measureJson` or `serializeJson`.**
*Why:* `PersistableStore.h:14-22` exists to keep those templates out of per-store
translation units — GCC emits `.isra` clones per TU at ~0.5 KB each.
`BookmarkDoc.h:16-18` records the same rule and obeys it. The measure stays in
`saveToFileAtomic` (`PersistableStore.h:170`).
*Attack it:* the host test *does* call `measureJson`, which is fine — it is not
firmware.

**A13 — `test/CMakeLists.txt` is not edited by this change; its one added line
goes in the PR body.**
*Why:* `.claude/agents/data-dev.md` names it a shared append point and says to
report the line rather than edit it.
*Attack it:* repo history goes the other way — #47 (`d043fd84`), #64 (`6156de32`)
and #65 (`5bf4cb9b`) each committed their own `add_subdirectory(...)` line. Until
that line lands the new suite does not run in CI, which makes the whole guard
inert. This is the assumption most likely to be overturned, and overturning it
costs one line.

---

## Architecture

### New: `src/RecentBook.h`

`RecentBook` moves here verbatim from `src/RecentBooksStore.h:8-15`, unchanged.
Arduino-free (`<string>` only). Models `src/BookmarkEntry.h`.

### New: `src/util/RecentBooksDoc.h`

Models `src/util/BookmarkDoc.h`. Holds the caps, the arithmetic and the JSON shape.

```cpp
namespace RecentBooksDoc {

// Entry-count cap, moved from RecentBooksStore.h:21 so the worst case is
// derivable in one place. size_t, not int: RecentBooksStore.cpp:58 compares it
// against vector::size().
inline constexpr size_t MAX_RECENT_BOOKS = 10;

// Display caps. See the spec's A1/A2: both sit above every surface that renders
// these fields, so a real publication is unaffected.
inline constexpr size_t MAX_TITLE_BYTES = 128;
inline constexpr size_t MAX_AUTHOR_BYTES = 96;

// path is the store's key and is NEVER truncated (A6) -- pruneMissing would
// delete a shortened one on the next boot. It gets an allowance instead.
inline constexpr size_t PATH_BUDGET_ALLOWANCE = 512;
// coverBmpPath is bounded by construction at 57 bytes (Epub.h:48, Epub.cpp:653);
// this is 2.2x that (A7).
inline constexpr size_t COVER_PATH_BUDGET_ALLOWANCE = 128;

// {"books":[]}
inline constexpr size_t DOC_WRAPPER_BYTES = 12;
// {"path":"","title":"","author":"","coverBmpPath":""}
inline constexpr size_t ENTRY_OVERHEAD_BYTES = 52;
// Worst-case JSON escape expansion, measured against the pinned ArduinoJson
// 7.4.2: " and \ serialise to two bytes each and are the only characters that
// do. Neither can occur in a path -- SdFat rejects both in a FAT LFN and an
// exFAT name (FsStructs.h:108-111) -- so only title and author pay it (A8).
inline constexpr size_t ESCAPE_FACTOR = 2;

constexpr size_t worstCaseBytes() {
  return DOC_WRAPPER_BYTES + (MAX_RECENT_BOOKS - 1) /* commas */
       + MAX_RECENT_BOOKS * (ENTRY_OVERHEAD_BYTES
                             + ESCAPE_FACTOR * (MAX_TITLE_BYTES + MAX_AUTHOR_BYTES)
                             + PATH_BUDGET_ALLOWANCE
                             + COVER_PATH_BUDGET_ALLOWANCE);
}

// Derived, not rounded (A10). Recompute it, do not tidy it.
inline constexpr size_t SAVE_BUDGET = worstCaseBytes();

// UTF-8-safe normalisation of the display fields. Returns true when it changed
// anything, which is what drives the load-side resave (A5). path and
// coverBmpPath are left alone.
bool normalise(RecentBook& book);

void toJson(const std::vector<RecentBook>& books, JsonDocument& doc);

// Fills `books`, capping at MAX_RECENT_BOOKS and re-bounding every entry.
// Sets `needsResave` when normalise() changed anything -- the out-param shape
// mirrors PersistableStoreBase::extractPassword (PersistableStore.cpp:112).
bool fromJson(JsonVariantConst doc, std::vector<RecentBook>& books, bool& needsResave);

}  // namespace RecentBooksDoc
```

The derivation, spelled out:

| Term | Bytes |
|---|---|
| `{"books":[]}` | 12 |
| 9 commas between 10 entries | 9 |
| 10 × entry overhead (52) | 520 |
| 10 × 2 × `MAX_TITLE_BYTES` (128) | 2,560 |
| 10 × 2 × `MAX_AUTHOR_BYTES` (96) | 1,920 |
| 10 × `PATH_BUDGET_ALLOWANCE` (512) | 5,120 |
| 10 × `COVER_PATH_BUDGET_ALLOWANCE` (128) | 1,280 |
| **`SAVE_BUDGET`** | **11,421** |

The first three rows are not arithmetic-on-arithmetic: `measureJson` on ten
entries with four empty strings returns exactly **541** against ArduinoJson 7.4.2
(research §4), and `12 + 9 + 520 = 541`. A test pins it.

11,421 is **3.9× tighter** than the 45,000 it replaces and 4.4× under
`persist::SD_READ_TRUNCATION_CAP` (`lib/Serialization/SaveBudget.h:19`).

### Changed: `src/RecentBooksStore.h`

- `RecentBook` removed; `#include "RecentBook.h"`.
- `MAX_RECENT_BOOKS` removed; uses `RecentBooksDoc::MAX_RECENT_BOOKS`.
- `SAVE_BUDGET = RecentBooksDoc::SAVE_BUDGET`, and the comment at `:29-35` is
  rewritten for the merged state — it currently documents the *opposite*
  decision and would be a lie.

### Changed: `src/RecentBooksStore.cpp`

`toJson`/`fromJson` become one-line delegations, as `BookmarkFile.cpp:57,90` does.
`addBook` and `updateBook` normalise before storing. The `static_assert` at
`:139-140` becomes:

```cpp
static_assert(RecentBooksStore::saveBudget() == RecentBooksDoc::SAVE_BUDGET,
              "RecentBooksStore's budget is RecentBooksDoc::worstCaseBytes() -- derived from the "
              "field caps, not a round number to be tidied");
static_assert(RecentBooksDoc::SAVE_BUDGET < persist::DEFAULT_SAVE_BUDGET,
              "a store with bounded fields must claim less than the shared default");
```

### Ownership after the change

| Concern | Owner |
|---|---|
| The four field caps and the escape factor | `src/util/RecentBooksDoc.h` |
| The JSON shape, both directions | `src/util/RecentBooksDoc.cpp` |
| The derived budget | `RecentBooksDoc::worstCaseBytes()` |
| Measure-and-refuse | `PersistableStore.h:165-177` (unchanged) |
| Atomic write, `.tmp` adoption | `PersistableStore.cpp:22-44,63-106` (unchanged, #51) |
| Mutation, pruning, persistence triggers | `src/RecentBooksStore.cpp` |
| Rendering and any display-side cap | the activities (unchanged) |

**Nothing about the save or load mechanics changes.** #51's
`readDocFromFileAdopting` (`PersistableStore.cpp:63-106`) sits underneath
untouched; it only adds `.tmp` promotion when the primary path is `Missing`.

---

## Data and control flow

### Add — the path the issue names

```
ReaderActivity::onEnter                              ReaderActivity.cpp:60
  └─ RecentBooksStore::addBook                       RecentBooksStore.cpp:42
       ├─ pruneMissing()                                              :45
       ├─ erase any entry with the same path                       :48-52
       ├─ NEW: RecentBook e{path, title, author, cover};
       │       RecentBooksDoc::normalise(e);      ← title/author capped here
       ├─ insert e at the front                                       :55
       ├─ resize to MAX_RECENT_BOOKS                               :58-60
       └─ saveToFileAtomic()                                          :62
            ├─ toJson  → RecentBooksDoc::toJson
            ├─ measureJson                          PersistableStore.h:170
            ├─ fitsBudget(n, 11421)                                  :171
            └─ writeDocToFileAtomic                                  :176
```

`updateBook` (`:67-80`) gains the same `normalise` call before it assigns.

### Load — where a legacy file is rescued

```
RECENT_BOOKS.loadFromFile()          main.cpp:413, LauncherActivity.cpp:77,
                                     PublicationsActivity.cpp:45
  └─ PersistableStore<T>::loadFromFile          PersistableStore.h:179
       ├─ readDocFromFileAdopting                                   :186
       ├─ RecentBooksStore::fromJson
       │    └─ RecentBooksDoc::fromJson(doc, books, needsResave)
       │         └─ per entry: cap at MAX_RECENT_BOOKS, normalise()
       ├─ if (needsResave) requestResave()          PersistableStore.h:44
       └─ (lock released) saveToFileAtomic()                    :194-197
```

A `recent.json` written by 1.9.10 with ten 4,000-byte titles therefore: loads in
full, shrinks in memory, and is rewritten once — never refused. Without A4+A5 it
would load at ~80 KB, already past `SD_READ_TRUNCATION_CAP`, and be unsavable
forever.

### What the user sees

| Before | After |
|---|---|
| A 300-byte title: stored whole, ellipsised by the row | Stored as 128 bytes, ellipsised by the row |
| `"Awake!   2026"` | `"Awake! 2026"` (A3) |
| 10 entries, long titles: saved at up to 45,000 B | Refused above 11,421 B — unreachable with the caps applied |
| A legacy over-long file | Shrunk and rewritten once on first load |

---

## Error handling

Per `CLAUDE.md`'s "`LOG_ERR` + return false" default, and unchanged from today:

- **Over budget.** `saveToFileAtomic` logs and returns false *before touching any
  file* (`PersistableStore.h:171-175`). `addBook`/`updateBook` already add their
  own `LOG_ERR` with the path (`RecentBooksStore.cpp:63,77`) — landed by #27's
  A6a. No new handling. The user-visible message is #39.
- **`normalise` cannot fail.** It takes a `RecentBook&` and shortens two strings;
  there is no allocation that can fail and no error to report. It returns *changed*,
  not *ok*.
- **A malformed `books` array.** `fromJson` keeps today's behaviour: a missing or
  non-array `books` key is tolerated as an empty list, and only a JSON parse error
  (handled upstream at `PersistableStore.cpp:55-59`) is fatal. The comment at
  `RecentBooksStore.cpp:23-24` carries the reasoning and moves with the code.
  This deliberately differs from `BookmarkDoc::fromJson`, which returns false for
  a non-object (`BookmarkDoc.cpp:26`) — it has a version to police; this store
  has none (A9).
- **An over-budget file on load.** Never a load failure. It loads in full and is
  shrunk, following `BookmarkDoc`'s rule (`BookmarkDoc.h:46-50`,
  `BookmarkDocTest.cpp:162-182`: *"not one entry may be dropped for size"*) and
  deliberately not `PassageDoc::fromJson`'s, which ends `return measureBytes() <= SAVE_BYTE_BUDGET`
  and would latch saving off.
- **Resave failure after a load-time shrink.** `loadFromFile` already logs it and
  still returns the load's own result (`PersistableStore.h:195-197`). The
  in-memory list is correct either way; the next `addBook` retries the write.

---

## Testing strategy

### Host — `test/recent_books_doc/`, modelled on `test/bookmark_doc/`

Its `CMakeLists.txt` mirrors `test/bookmark_doc/CMakeLists.txt` exactly:
compile `RecentBooksDocTest.cpp`, `src/util/RecentBooksDoc.cpp` and
`lib/Utf8/Utf8.cpp`; include `src`, `lib/Utf8`, `lib/Serialization`; link
`ArduinoJson` and `GTest::gtest_main`.

TDD order — each test is written and seen to fail before the code that passes it:

1. `RoundTripsEveryField` — four fields survive `toJson`→`fromJson`. Mirrors
   `BookmarkDocTest.cpp:33-59`. *(Fails first: the file does not exist.)*
2. `NormaliseCapsTitleOnACodepointBoundary` — 60 × `世` (180 bytes) is cut to
   ≤ 128 with `size % 3 == 0`. Mirrors `BookmarkDocTest.cpp:118-140`, which is
   the test that proves a raw byte cut was not used.
3. `NormaliseCapsAuthor` — the same for 96.
4. `NormaliseLeavesRealTitlesAlone` — the six real publication titles from A1
   (39-72 bytes) and three real authors (38-52) come back byte-identical. This is
   the test that makes A1/A2 a *display* decision rather than a number; if a cap
   is lowered carelessly it fails here first.
5. `NormaliseNeverTouchesPathOrCoverBmpPath` — a 600-byte path survives whole
   (A6). Without this, "truncate the strings" invites someone to truncate all four
   and silently delete entries via `pruneMissing`.
6. `NormaliseReportsWhetherItChangedAnything` — false for an already-short entry,
   true for an over-long one. This is what gates A5's resave.
7. `FromJsonReBoundsAnOverlongTitleFromTheCard` and sets `needsResave` (A4, A5).
8. `FromJsonCapsTheEntryCount` — 25 entries in the file yield 10, preserving
   today's `RecentBooksStore.cpp:29` behaviour.
9. `FromJsonToleratesAMissingBooksKey` — pins the comment at `:23-24`.
10. `DocumentOverheadMatchesTheMeasuredConstants` — builds ten entries with four
    empty strings and asserts `measureJson(doc) == DOC_WRAPPER_BYTES + (N-1) + N * ENTRY_OVERHEAD_BYTES`
    (= 541). A renamed JSON key or a new field fails here, which is the point.
11. `AWorstCaseDocumentFitsTheDerivedBudget` — the real guard, modelled on
    `BookmarkDocTest.cpp:142-160`. Ten entries, each with `MAX_TITLE_BYTES` of `"`,
    `MAX_AUTHOR_BYTES` of `\`, a `PATH_BUDGET_ALLOWANCE`-byte path and a
    `COVER_PATH_BUDGET_ALLOWANCE`-byte cover, built through the **real**
    `RecentBooksDoc::toJson`, then `EXPECT_LE(measureJson(doc), SAVE_BUDGET)`.
    A string-shaped field added to `RecentBook` fails this.
12. `TheBudgetIsTighterThanTheDefaultAndClearOfTheReadCap` —
    `SAVE_BUDGET < persist::DEFAULT_SAVE_BUDGET < persist::SD_READ_TRUNCATION_CAP`.
    Pins that the issue's second half actually happened.
13. `ARealisticStoreUsesAFractionOfTheBudget` — ten entries with real titles,
    real authors and ~70-byte paths measures well under a quarter of
    `SAVE_BUDGET`, with the measured figure in the failure message. Mirrors
    `BookmarkDocTest.cpp:184-209`'s "the budget holds N records" style. This is
    the test that says a refusal is unreachable in the field, which is the whole
    argument for tightening.

### Firmware gates

- The two `static_assert`s in `RecentBooksStore.cpp` — compile-time, and the
  existing one already fails the build the moment `SAVE_BUDGET` moves.
- `pio run` once after the last edit (`~/.platformio/penv/bin/pio`; `pio` is not
  on `PATH` in this worktree).
- `./bin/clang-format-fix` over the whole tree, with the main checkout's
  `.venv/bin` on `PATH` (issue #61). The `-g` form would miss the four new files
  once committed.

### What only the human tester can verify — flag these in the PR

1. A publication with a genuinely long title still shows a sensible, ellipsised
   row in **Recents** and a sensible resume tile on the **launcher**. This is the
   A1/A2 judgement and no host test can make it.
2. Upgrading over an existing `/.crosspoint/recent.json`: the list survives, and
   serial shows at most one resave (A5), not one per boot.
3. Opening ten books in a row leaves `recent.json` well under 11,421 bytes — check
   the file size on the card.
4. No `PERSIST` refusal line appears in normal use.

### Known gap

Until the `add_subdirectory(recent_books_doc)` line reaches `test/CMakeLists.txt`
(A13), none of the above runs in CI. The line is in the PR body.

---

## Risks

| Risk | Mitigation |
|---|---|
| A cap clips a title the row could have shown (A1) | Caps sit ~1.8× above real titles and ~2.7× above every other surface's cap; test 4 pins real titles; the issue accepts this class of change |
| A >512-byte path makes saves fail silently (A6) | ~7× realistic paths; test 13 shows the headroom; raising the constant is a one-line change |
| `utf8SafeSummary` normalises whitespace as well as truncating (A3) | The launcher already applies the identical transform to this field (`LauncherActivity.cpp:83`) |
| The derived 11,421 gets "tidied" to a round number | Two `static_assert`s and a comment saying it is derived |
| The new suite never runs (A13) | Called out as the known gap; one line resolves it |
| The doc split churns a "good first issue" | One-for-one mirror of `BookmarkDoc`; no behaviour of its own |

---

## Open questions for the review

1. **A6 is the weakest point.** Is a 512-byte path allowance the right trade, or
   should `PATH_BUDGET_ALLOWANCE` be 1024 (budget → 16,541, still 2.7× tighter
   than today) to buy margin the user can never notice?
2. **A1's cap is unmeasured against the row.** Is 128 right, or should this wait
   on an actual `getTextWidth` measurement on device before the number is fixed?
3. **A13** — commit the `test/CMakeLists.txt` line as #47, #64 and #65 all did,
   or hold to `.claude/agents/data-dev.md`?
