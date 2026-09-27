# Issue #152 research — batch 2 follow-ups

Branch `fix/152-batch-2-follow-ups`, based on `dc97292e` (main). Every claim below was read
at that commit. The issue's source is the batch 2 branch review,
`docs/superpowers/reviews/berean-os-20260927-enhancements-batch-2-store-writer-dead-c-6vyi-branch-review-0.md`
(MINOR-2 through MINOR-6).

## Toolchain actually installed

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` |
| Arduino-ESP32 framework | 3.3.7 | `framework-arduinoespressif32/package.json` `"version"` |
| ArduinoJson (firmware) | 7.4.2 | `platformio.ini:154` `bblanchon/ArduinoJson @ 7.4.2` |
| ArduinoJson (host tests) | v7.4.2 | `test/CMakeLists.txt:28-33` (FetchContent `GIT_TAG v7.4.2`) |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:17` |
| CMake | 4.4.2 | `cmake --version` |

Side finding: `test/CMakeLists.txt:25` says the pin matches "`platformio.ini:141`"; the line is
now `:154`. Not in the issue.

Baseline: `cmake -S test -B build/test && cmake --build build/test --target StorageIoTest RecentBooksDocTest`
then running both — `StorageIoTest` 67/67 passed, `RecentBooksDocTest` 23/23 passed.

## Item 1 — the shared JSON reader is unbuffered

**Owner:** `lib/Serialization/PersistableStore.cpp`.

- `HalFileReader` (`PersistableStore.cpp:19-32`) exposes `int read() { return file_.read(); }`
  (`:23`) and `readBytes` (`:25-28`).
- `readDocFromFileStreamed` (`:140-160`) opens the file, rejects size 0 (`:148`), then
  `deserializeJson(doc, reader)` (`:153-154`).
- ArduinoJson 7.4.2's JSON path only ever calls `read()`: the parser pulls one character at a
  time through `Latch::load` → `reader_.read()`
  (`build/test/_deps/arduinojson-src/src/ArduinoJson/Json/Latch.hpp:38`). `readBytes` is only
  referenced by the MsgPack deserializer and the reader adaptors (`grep -rln readBytes` over the
  ArduinoJson tree). So `HalFileReader::readBytes` is dead for JSON, and every byte costs one
  `HalFile::read()`.
- `HalFile::read()` takes `storageMutex` every call: `HAL_FILE_WRAPPED_CALL` constructs a
  `HalStorage::StorageLock` (`lib/hal/HalStorage.cpp:138-141`, applied at `:159`); the lock is a
  recursive FreeRTOS mutex (`:20`, `:32-35`).
- The writer in the same file is already buffered for that reason: `JsonFileWriter` over a
  512-byte `serialization::BufferedFileWriter` (`PersistableStore.cpp:14-15`, `:34-54`, `:66-71`).
- The reader to wrap exists: `serialization::BufferedFileReader` (`lib/Serialization/BufferedFile.h:74-133`),
  `read(void*, size_t)` only — no single-byte `read()`. It degrades to passthrough on OOM
  (`:83-88`) and must be the file's only accessor while alive (`:23-24`). It is already included
  by `PersistableStore.cpp:3`.
- Callers of `readDocFromFileStreamed` today: `src/study/PassageFile.cpp:22` (as a `DocReader`
  for `loadAdopting`) and `test/storage_io/AtomicWriteTest.cpp:93`. `readAdopting` calls the same
  reader again for `<path>.tmp` (`PersistableStore.cpp:178`).

**Existing test for the issue's verify line:** `AtomicWrite.ADocumentPastTheReadCapIsStreamedWholeAndReadBackWhole`
(`test/storage_io/AtomicWriteTest.cpp:87-95`) already writes a 60,008-byte document and reads it
back through `readDocFromFileStreamed`. It passes with the unbuffered reader, so it cannot be the
TDD failing test for this item; it is the regression guard.

**Gap for a failing-first test:** the Storage fake has no way to observe how many reads happened.
Its controls are `reset`, `clearFailures`, `putFile`, `fileBytes`, `isDir`, `failReadsOf`,
`failRenamesFrom`, `failWritesTo`, `failWritesAfter` (`test/stubs/HalStorageFake.h:17-44`), and
`HalFile::read()` is a 1-byte `read(void*, size_t)` (`test/stubs/HalStorageFake.cpp:222-225`). A
test that proves buffering needs a new fake control (e.g. a `HalFile::read` call counter), modelled
on `failWritesAfter`. The fake is test-only code, so this is not a new firmware pattern.

**Memory:** a 512-byte reader buffer matches `WRITE_BUFFER_BYTES` and stays under the 4,096-byte
PSRAM auto-routing threshold noted at `PersistableStore.cpp:14`, i.e. internal SRAM, heap,
one allocation per read, freed at scope exit.

**Nearest example:** `lib/Epub/Epub/BookMetadataCache.cpp:191-192` constructs two
`BufferedFileReader`s over `HalFile`s (buffer `BUILD_IO_BUFFER_SIZE = 4096`, `:20`); the writer
adaptor at `PersistableStore.cpp:34-54` is the shape to mirror for an ArduinoJson reader.

## Item 2 — nine redundant per-store `mkdir` calls

`writeDocToFileAtomic` calls `ensureParentDirectory(path)` first (`PersistableStore.cpp:103-104`),
which `Storage.mkdir`s everything before the last `/` (`:56-62`). `HalStorage::mkdir` defaults to
`pFlag = true` (`lib/hal/HalStorage.h:34`), so a two-level parent such as `/.berean/passages` is
created in one call. The fake models the same (`test/stubs/HalStorageFake.cpp:108-117`), and
`AtomicWrite.CreatesTheTargetsOwnParentAndNotCrosspoint` covers it
(`test/storage_io/AtomicWriteTest.cpp:58-62`). This landed in #145 (`git log -S ensureParentDirectory`
→ `4597f8a8`).

Each call site, and the path the following write targets — in every case the `mkdir` creates
exactly that path's parent, so it is redundant:

| Call | Dir created | Written path |
|---|---|---|
| `src/study/PassageFile.cpp:36` | `PASSAGES_DIR` | `PASSAGES_DIR/<key>.json` (`:17`) |
| `src/study/ChapterCompletionFile.cpp:36` | `COMPLETION_DIR` | `COMPLETION_DIR/<key>.json` (`:17`) |
| `src/study/TagPaletteFile.cpp:36` | `BEREAN_DIR` | `TAGS_FILE` = `/.berean/tags.json` (`TagPaletteFile.h:28`, `SdPaths.h:23`) |
| `src/study/PubKeyRegistry.cpp:46` | `BEREAN_DIR` | `PUBKEYS_FILE` (`PubKeyRegistry.h:32`, `SdPaths.h:22`) |
| `src/study/MigrationRunner.cpp:118` | `BEREAN_DIR` | `MIGRATION_LEDGER_FILE` (`MigrationRunner.h:54`) |
| `src/study/MigrationRunner.cpp:173` | `BEREAN_DIR` | `MIGRATION_REPORT_FILE` (`MigrationRunner.h:53`) |
| `src/network/MeetingWeekCache.cpp:57` | `BEREAN_DIR` | `MEETING_WEEKS_FILE` (`MeetingWeekCache.h:33`) |
| `src/util/BookmarkFile.cpp:64` | `BOOKMARKS_DIR + "/"` | `getBookmarksDir() + <flat>.json` (`BookmarkUtil.cpp:11-14`) |
| `src/util/HighlightFile.cpp:40` | `HIGHLIGHTS_DIR + "/"` | `highlightsDir() + <flat>.json` (`HighlightFile.cpp:14-17`) |

History: spec #100 kept these deliberately — "redundant, not wrong; removing them touches three
surfaces for no behavioural gain" (`docs/superpowers/specs/2026-09-27-issue-100-design.md:88-92`).
#145 then deleted the two comments that explained the Bookmark/Highlight calls (they said
`writeDocToFileAtomic` only ensures `/.crosspoint`; `git show 4597f8a8 -- src`), so the review
flagged the calls as reading necessary when they are not. The calls span three directories
(`src/study`, `src/network`, `src/util`).

Other `mkdir`s in `src/` (`FontInstaller`, `BibleSearchIndexer:278`, `UnitIndexCache:92`,
`ScreenshotUtil`, `CatalogIndexStore:206`, `PublicationDownloader:135`, `EpubReaderActivity:118`,
`Epub.cpp:540`, `Section.cpp:275,356`) precede non-`writeDocToFileAtomic` writes and are out of scope.

## Item 3 — four `MappedInputManager` helpers with no callers

`grep -rn -e wasTapInRect -e rowTouch -e colTouch -e getPressedFrontButton src lib test docs/contributing`
returns only the declarations, definitions and docs:

- `wasTapInRect` — `src/MappedInputManager.h:66`, `.cpp:177-181`.
- `rowTouch` — `.h:75-76`, `.cpp:183-200`; `colTouch` — `.h:78`, `.cpp:202-218`. Both return the
  `RowTouch` enum (`.h:74`), which nothing outside `MappedInputManager` names
  (`grep -rn RowTouch src lib`), so the enum and its comment block (`.h:68-73`) go with them.
- `getPressedFrontButton` — `.h:103-104`, `.cpp:378-404` including a long comment that keeps it
  for "a future 'press any front button' flow" (`.cpp:389-390`). Its last caller was
  `ButtonRemapActivity.cpp:87`, deleted in #146 (review MINOR-3).

This file is not the `data` surface. Deleting helpers does not remove `MappedInputManager`
itself, so the CLAUDE.md rule that it may only go with its replacement does not bite.
`docs/contributing/touch-and-ui.md:7` and the table rows at `:131-132` describe these helpers and
must change with them (overlaps item 8).

## Item 4 — `RecentBook.coverBmpPath` is written, never read

**Owners:** `src/RecentBook.h:13` (field), `src/util/RecentBooksDoc.{h,cpp}` (format, host-tested
by `test/recent_books_doc/`), `src/RecentBooksStore.{h,cpp}` (storage).

Writers and movers of the field:

- `RecentBooksDoc::toJson` writes it (`RecentBooksDoc.cpp:51`); `fromJson` reads it (`:73`).
- `RecentBooksStore::addBook` takes it (`RecentBooksStore.cpp:30-31,43`); callers pass
  `getBookThumbBmpPath()` (`ReaderActivity.cpp:60`; virtual default `""` at `ReaderActivity.h:26`,
  override at `EpubReaderActivity.h:167`) and `epub->getThumbBmpPath()` (`EpubReaderActivity.cpp:452`).
- `RecentBooksStore::updateBook` sets it (`:57-65`) — **`updateBook` has no callers at all**
  (`grep -rn updateBook src lib`).
- `RecentBooksStore::updatePath` rewrites it when the cache dir moves (`:94-95`). Its
  `oldCachePath`/`newCachePath` parameters exist only for that rewrite; callers are
  `src/network/PublicationDownloader.cpp:120` and `src/activities/reader/EpubReaderActivity.cpp:150`.
- No reader: the only other `coverBmpPath` identifier in `src/` is an unrelated local in
  `SleepActivity.cpp:820-846`.
- The no-argument `Epub::getThumbBmpPath()` (`lib/Epub/Epub.cpp:668`, returns the
  `thumb_[HEIGHT]` template) is called only to feed this field (`EpubReaderActivity.cpp:452`,
  `.h:167`); the `(int height)` overload has live callers (`LauncherActivity.cpp:251`,
  `PublicationsActivity.cpp:85`).

Format facts that the removal touches:

- `FORMAT_VERSION = 1`; absent `"v"` reads as 1 (`RecentBooksDoc.h:21-22`, `.cpp:57`);
  `persist::isKnownFormatVersion(v, newest)` accepts `0 < v <= newest` (`lib/Serialization/FormatVersion.h:14-16`).
- The save budget is derived and "fits by exactly zero bytes"; it includes
  `COVER_PATH_BUDGET_ALLOWANCE = 128` and a 52-byte `ENTRY_OVERHEAD_BYTES` that counts the
  `"coverBmpPath":""` key (`RecentBooksDoc.h:44-52,63-73`). Removing the field changes
  `worstCaseBytes()` and the test that pins it.
- `docs/file-formats.md:457-470` documents version 1 with the field.
- Tests reference it: `test/recent_books_doc/RecentBooksDocTest.cpp:22,70,137,235,279`.

**Versioning precedent — this needs a decision in the spec.** The issue and the batch brief both
say to bump the version. The one JSON store that has bumped, `PassageDoc`, bumps only when the new
writer emits data an old build would silently drop: "A file with no link is still written as v1,
because it holds nothing a v1 build would drop" (`lib/StudyStore/StudyStore/PassageDoc.h:20-25`,
`PassageDoc.cpp:190`). Removing `coverBmpPath` is the opposite direction:

- a new build reading an old v1 file ignores the extra key (nothing reinterpreted);
- an old build reading a new file reads `coverBmpPath` as `""` (`| ""`, `RecentBooksDoc.cpp:73`) —
  the value it already writes when no thumbnail exists (`ReaderActivity.h:26`);
- bumping to v2 would make any older build (an OTA rollback) refuse `recent.json`, set
  `loadRefused`, and block every recents save until upgraded (`FormatVersion.h:18-33`).

So a bump is not required for safety and has a real rollback cost; not bumping follows the
PassageDoc rule. This is for the spec to settle, likely via `hpipe decide`, since it contradicts
the literal issue text.

**Nearest example of a field removal in a store:** none found in the JSON stores (`git log`
shows version bumps only for `PassageDoc` `0bbcd47d`, #86). Binary caches bump on any layout
change (`SECTION_FILE_VERSION`, `lib/Epub/Epub/Section.cpp:50`) — a different contract.

Scope note: carrying the removal through `updatePath`'s cache parameters and the dead no-arg
`Epub::getThumbBmpPath()` reaches `src/network` and `lib/Epub`. Leaving them keeps item 4 inside
`src/RecentBook*`, `src/util/RecentBooksDoc*` and the two `addBook` call sites.

## Item 5 — `HomeMenuItem`

- Declared `enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, FILE_TRANSFER, SETTINGS_MENU };`
  (`src/activities/ActivityManager.h:20`).
- `ActivityManager::goHome(HomeMenuItem initialMenuItem = NONE, bool cleanInitialRefresh = false)`
  (`.h:92`); the body discards it with `(void)initialMenuItem` and a comment that it "stays in the
  signature so onGoHome's call sites and the default argument need not change"
  (`ActivityManager.cpp:241-248`).
- `Activity::onGoHome(HomeMenuItem item = NONE)` (`src/activities/Activity.h:70`, marked
  "TODO: remove this in near future" at `:68-69`) forwards it (`Activity.cpp:13`).
- The only caller naming it: `src/main.cpp:578`, `activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);`.
  No `goHome(`/`onGoHome(` call anywhere passes a non-default item
  (`grep -rn "onGoHome([^)]\|goHome([^)]" src | grep -v "void \|HomeMenuItem::NONE"` → empty).

Removing it means `goHome(bool cleanInitialRefresh = false)`, `onGoHome()`, and `main.cpp:578`
becoming `goHome(needsWakeRefresh)`. The batch brief assigns `src/main.cpp` to this task.

## Item 6 — `USER_GUIDE.md` describes the deleted Home screen

- `USER_GUIDE.md:89` — "On a first boot you land on the Home screen."
- `:94-103` — "## 4. Home screen" listing **Browse files**, **Recent books**, **File transfer**,
  **Settings**, and "Selecting the cover resumes reading."
- `:238` — "**Home -> Browse files** walks the SD card."

What the home actually is now (`src/activities/launcher/LauncherActivity.cpp`): header
`tr(STR_BEREAN)` (`:498`), tiles Bible (`:500`), Meetings (`:502`), Publications (`:506`,
`STR_PUBLICATIONS: "Publications"`, `english.yaml:426`), Settings (`:507`), and a Continue Reading
resume tile when one exists (`:513`); `activate()` at `:525-545`. The file browser is still
reachable, just not from a Home entry: the Bible tile opens it when no Bible is on the card
(`:547-555`), and the reader's Back / Back-hold goes to it per `SETTINGS.backShortToFileBrowser`
(`src/activities/reader/ReaderUtils.h:243-266`). File Transfer is under Settings → System
(`src/activities/settings/SettingsActivity.cpp:76`). `FileBrowserActivity` still exists
(`src/activities/home/FileBrowserActivity.cpp`); `HomeActivity`/`RecentBooksActivity` do not
(`ls src/activities/home`).

## Items 7–9 — stale docs

- **Item 7:** `STR_BROWSE_FILES` is absent from `lib/`/`src/` (`grep -rln STR_BROWSE_FILES lib src`
  → empty), but `docs/i18n.md:75` (a Spanish YAML example) and `:191`
  (`renderer.drawText(font, x, y, tr(STR_BROWSE_FILES));`) still use it.
- **Item 8:** `docs/contributing/touch-and-ui.md:7` says the helpers "survive only for the two
  remaining hand-rolled surfaces (the theme-driven home screen and the reader page)"; `:131-132`
  list `wasTapInRect` and `rowTouch`/`colTouch`. Both change with item 3.
- **Item 9a:** `docs/superpowers/specs/2026-09-14-publication-download-design.md:138-140` cites the
  optimiser at `FilesPage.html:1570`, `:1750`, and `:158` says "The web-UI optimizer stays
  available". #144 (`b6aadb76`, "strip the EPUB optimiser and jszip from the file page") removed it;
  `grep -in optimi src/network/html/FilesPage.html` → empty.
- **Item 9b:** `CLAUDE.md` is a symlink to `AGENTS.md` (`ls -la`), so the edit lands in `AGENTS.md`.
  `AGENTS.md:279-281` says `PersistableStore::saveToFile` uses the non-atomic `writeDocToFile`
  "(`lib/Serialization/PersistableStore.cpp:11`)"; the function is at `:92-101`. Two nuances for
  the spec:
  - `writeDocToFile` itself **is** still `String`-based (`PersistableStore.cpp:94-96`); what #145
    changed is `writeDocToFileAtomic`, which now streams (`:103-121`, `:73-88`). The fix is the
    citation plus making clear the atomic path streams, not claiming `writeDocToFile` changed.
  - Nothing calls `saveToFile()` (`grep -rn "\.saveToFile()\|saveToFile();" src lib` → empty;
    `saveToFileAtomic()` has 27 call sites in `src`); `writeDocToFile` is reached only through
    `saveToFile` (`PersistableStore.h:193`).
  - Same block, `AGENTS.md:286` cites `src/util/HighlightFile.h:40-42` for `SAVE_BYTE_BUDGET = 45000`;
    it is now at `:39`. `SDCardManager.cpp:202` (`:277`) is still correct.
  - `docs/superpowers/specs/2026-09-13-berean-os-design.md:391` carries the same stale `:11`
    citation; the issue does not list it.

## Surfaces and tier

The issue spans `data` (items 1, 2, 4), `ui` (`src/MappedInputManager.*`,
`src/activities/ActivityManager.*`, `Activity.*`, `src/main.cpp` — items 3, 5), a line in
`src/network` (item 2's `MeetingWeekCache.cpp:57`; item 4's `updatePath` caller if its parameters
go), possibly `lib/Epub` (item 4's no-arg `getThumbBmpPath`), and docs. Item 4 touches a store
format. The task is already `heavy`, the highest tier, so no raise.

`test/CMakeLists.txt` needs no new suite: item 1's test fits `test/storage_io/` and item 4's fits
`test/recent_books_doc/`, both existing.
