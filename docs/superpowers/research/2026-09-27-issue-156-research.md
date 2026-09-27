# Issue #156 — research: how the Bible tile finds a Bible today

Base: `d956cc8b` (main, includes #155 / issue #109's `WifiSession`). Every
claim below cites a line read in this worktree or a command run on 2026-09-27.

## 1. Which files own the behaviour

| Concern | File | Lines |
|---|---|---|
| Bible lookup on launcher entry | `src/activities/launcher/LauncherActivity.cpp` | `resolveTargets` 98-157, Bible block 108-129 |
| Card scan for a CDN-named copy | `src/activities/launcher/LauncherActivity.cpp` | `findBibleOnCard` 183-189 |
| Tile subtitle for a found Bible | `src/activities/launcher/LauncherActivity.cpp` | `bibleTitleFor` 195-201, `applyChaptersReadSubtitle` 82-96 |
| Tap on the tile | `src/activities/launcher/LauncherActivity.cpp` | `activate` 525-545, `openBible` 547-555 |
| Refresh after a download | `src/activities/launcher/LauncherActivity.cpp` | `openPublications` 562-568 |
| Filename rule + `BIBLE_SYMBOL = "nwt"` | `src/activities/launcher/LauncherBible.h` | 11, 18-41 |
| Card listing (download folder + `/`, no recursion) | `src/util/CardBooks.cpp` | `collect` 20-29, `list` 35-44 |
| Registry (path → symbol/issue/language) | `src/study/PubKeyRegistry.{h,cpp}` | `record` 20-48, `findBySymbol` 55-78, `lookup` 80-97 |
| Download + registration | `src/network/PublicationDownloader.{h,cpp}` | `download`; `record` calls at `.cpp:184` (skip path) and `.cpp:239` (success); `failureMessage` `.cpp:245-260` |
| Reader's "is this the Bible" signal | `lib/Epub/Epub.cpp` 981-986 | `getBibleBookNavSpineIndex()` resolves `biblebooknav.xhtml` (`lib/Epub/Epub/BibleNavScanner.h:19`) |
| Where the reader opens a book | `src/activities/reader/EpubReaderActivity.cpp` | `loadBook` 178-264; `STUDY.openPublication` at 249 |
| Wi-Fi lifetime helper (#109) | `src/network/WifiSession.h` 1-22 | |
| Download screen to model on | `src/activities/network/MeetingDownloadActivity.{h,cpp}` | whole files (90 + 407 lines) |

`src/study/` belongs to another task in this batch (`PubKeyRegistry`,
`StudyStore`); this issue may only *call* it.

## 2. Current control flow

### Lookup (`LauncherActivity.cpp:98-129`)

`onEnter` → `computeLayout` → `resolveTargets` (`:73-80`). The Bible block:

1. `biblePath.clear(); bibleSubtitle = tr(STR_BIBLE_SUBTITLE_NONE);` (`:113-114`)
2. `PubKeyRegistry::findBySymbol({BIBLE_SYMBOL})` (`:115`) — reads
   `/.berean/pubkeys.json`, returns the first entry whose `s == "nwt"` and whose
   file still exists (`PubKeyRegistry.cpp:64-76`). Exact match only, so an entry
   recorded as `nwtsty` is not a hit.
3. else `findBibleOnCard()` (`:116`) → `CardBooks::list()` then the first path
   where `isCdnNamedCopyOf(path, "nwt")` (`:183-189`). `CardBooks::list()` lists
   `SETTINGS.downloadFolder` then `/`, one level each, capped at 400 entries
   per directory (`CardBooks.cpp:18,35-44`). `isCdnNamedCopyOf` accepts only
   `nwt_<A-Z0-9, 1-8 chars>.epub` in the basename (`LauncherBible.h:18-41`).
4. Found → subtitle from recents title, else file stem (unless CDN-named), then
   chapters-read overrides it; cover thumbnail generated; `APP_STATE.bibleCoverPath`
   saved when changed (`:118-129`).

Recents is deliberately not consulted (comment `:108-112`). The pre-#104
fallback was removed in `08d98a9f` (PR #142); `git show 08d98a9f --
src/activities/launcher/LauncherActivity.cpp` shows it was:

```cpp
const auto looksLikeABible = [](const RecentBook& book) {
  return book.path.find("nwt") != std::string::npos || book.title.find("Nuevo Mundo") != std::string::npos ||
         book.title.find("New World") != std::string::npos;
};
```

That commit's message records the regression as accepted: "an unregistered
Bible sideloaded under an arbitrary filename is no longer found."

So a Bible is invisible when it is (a) not in the registry **and** (b) not named
`nwt_<LANG>.epub` directly in the download folder or `/`. The registry only gets
`nwt` entries from `publication::download` (`PublicationDownloader.cpp:184,239`)
— the grep `grep -rn "PubKeyRegistry::" src lib` finds no other `record` caller.

### Tap (`LauncherActivity.cpp:547-555`)

`biblePath.empty()` → `activityManager.goToFileBrowser()`; else
`activityManager.goToReader(biblePath)`. No download path exists today.

### Opening a book — where the Bible is already recognised

`ReaderActivity::onEnter` (`src/activities/reader/ReaderActivity.cpp:41-61`)
→ `loadBook` → `EpubReaderActivity::loadBook` (`EpubReaderActivity.cpp:178`),
which at `:249` calls `STUDY.openPublication(epub, renderer)` (PSRAM builds
only, `#if BOARD_HAS_PSRAM` `:244-261`). Then `RECENT_BOOKS.addBook` at
`ReaderActivity.cpp:60`. None of this runs on the render path.

The one signal every Bible-aware site in the codebase uses is
`epub->getBibleBookNavSpineIndex() >= 0`:

- `StudyStore::openPublication` — `src/study/StudyStore.cpp:34` (`isBible`),
  and it already calls `PubKeyRegistry::lookup(bookPath)` at `:40`.
- `BibleSearchStore::resolveDocuments` — `src/study/BibleSearchStore.cpp:124`
  ("is not an NWT-shaped Bible").
- `BibleNavigationActivity` — `BibleNavigationActivity.cpp:61`.
- Reader menu `isBible` — `EpubReaderActivity.cpp:296`; status-bar chapter
  number — `:1618`; `MigrationRunner.cpp:272`.

It is cached on the `Epub` (`Epub.cpp:982-985`), so calling it again after load
costs one int compare. It is filename-based on `biblebooknav.xhtml`, which is an
NWT-family trait, not a strictly-`nwt` one: a Study Bible (`nwtsty`) EPUB would
presumably carry the same file. Not verified — no `nwtsty` EPUB is available in
this worktree. The spec must decide whether registering such a book as `nwt` is
acceptable.

Registering the Bible under `nwt` does **not** change its study key:
`resolvePubKey` returns `BIBLE_PUB_KEY` whenever `isBible && canonVerified`,
before it looks at `registered` (`lib/StudyStore/StudyStore/PubKey.cpp:25-27`),
and `canonVerified = isBible` (`StudyStore.cpp:38`). So no tag or completion
data moves.

`PubKeyRegistry::record` **overwrites** any entry for the path
(`PubKeyRegistry.h:21`, `.cpp:36-39`). Idempotency therefore has to come from
the caller: `lookup(path)` first and skip when anything is recorded, which also
avoids clobbering a Buscar entry with a different symbol. `record` already does
atomic write, byte budget and version refusal (`.cpp:24-47`); it does not take a
lock of its own beyond `HalStorage`'s mutex on each call.

Language for a registration made on open: the registry stores the JW code
(`"S"`, `"E"`; `PubKey.h:20`, downloader passes
`CrossPointSettings::langWritten(...)`, `CrossPointSettings.h:72`).
`Epub::getLanguage()` (`lib/Epub/Epub.h:59`) returns the OPF's `dc:language`,
which is a different vocabulary. The registered language is only consumed by
`resolvePubKey` for non-Bible keys (`PubKey.cpp:30`) and is not displayed
(`PublicationsActivity.cpp:60-63` shows symbol and issue only), so for the
Bible it is informational.

### Download machinery the enhancement would reuse

- `publication::download(Request, Hooks, outPath)` —
  `PublicationDownloader.h:62`. `Request{symbol, issue, language, folder,
  force}` (`:28-36`). Results: `Ok, AlreadyOnCard, Cancelled, NoMediaLink,
  DownloadFailed, ChecksumMismatch, OutOfMemory` (`:18-26`).
- `publication::failureMessage` already maps results to `tr()` strings:
  `NoMediaLink → STR_PUBLICATION_UNAVAILABLE`, `ChecksumMismatch →
  STR_CHECKSUM_MISMATCH`, `DownloadFailed/OutOfMemory → STR_DOWNLOAD_FAILED`,
  `Ok/AlreadyOnCard/Cancelled → nullptr` (`PublicationDownloader.cpp:245-260`).
  The issue's "each maps to a user-facing message" is therefore mostly already
  true; `Cancelled` is deliberately silent.
- A first download writes in place; a replacement is staged via `.part` and
  only swapped after the MD5 check (`PublicationDownloader.cpp:199-231`). A
  failed or cancelled first download leaves nothing behind: `downloadToFile`
  removes `destPath` itself on any non-`OK` result and on a zero-byte body
  (`src/network/HttpDownloader.cpp:300-308`), and a checksum mismatch is removed
  by the caller (`PublicationDownloader.cpp:218-221`). "A failed download leaves
  the card untouched" already holds without new code.
- The file is named from the API's `pubName` (`MeetingFilename.cpp:55-58`), so
  a Buscar-style NWT download will not be CDN-named; it is found through the
  registry entry written at `:239`.
- A pre-existing `nwt_S.epub` in the folder is migrated to the new name
  (`migrateCdnNamedCopy`, `:179`).
- Publication language: `SETTINGS.publicationLanguage`, `PUB_LANG_SPANISH = 0`
  (default) / `PUB_LANG_ENGLISH = 1`, mapped by `langWritten`
  (`CrossPointSettings.h:69-72,338`). `MeetingDownloadActivity.cpp:269` is the
  call to copy.

Measured NWT sizes (command below, 2026-09-27):

```
$ curl -s ".../GETPUBMEDIALINKS?pub=nwt&langwritten=S&fileformat=EPUB"   # and E
La Biblia. Traducción del Nuevo Mundo (revisión del 2019)
https://cfp2.jw-cdn.org/a/03d7c7/3/o/nwt_S.epub 14945282
New World Translation of the Holy Scriptures (2013&nbsp;Revision)
https://cfp2.jw-cdn.org/a/8b30f9/4/o/nwt_E.epub 15652378
```

≈15 MB in either language. The exact size is only known after the resolve
request; a pre-download confirmation must either show an approximate constant
or resolve first. Note the English `pubName` carries a literal `&nbsp;`, which
feeds `publicationFilename` — worth a check when the spec names the file.

### `WifiSession` (#109)

`src/network/WifiSession.h:3-9`: hold as `std::optional<WifiSession>` declared
last, `emplace()` in `onEnter()`; destructor ends the session unless a link
that was up on entry is still up. Alternatively, a screen that does not use
the link itself may `emplace()` around one child launch and `reset()` in the
result handler — `SettingsActivity.cpp:309-311` does exactly that. Download
screens use the member form: `MeetingDownloadActivity.h:87` / `.cpp:39`,
`CatalogSearchActivity.h:130` / `.cpp:61`.

### Nearest existing example of this kind of change

- **Download screen:** `MeetingDownloadActivity` — states `WIFI_SELECTION,
  RESOLVING, DOWNLOADING, FINISHED, FAILED` (`.h:24`); Wi-Fi via
  `WifiSelectionActivity(autoConnect=true)` (`.cpp:64-69`); progress hooks that
  pump input and throttle repaints (`.cpp:234-258`); download call
  (`.cpp:260-310`). Gaps vs. the issue: Wi-Fi cancel just `finish()`es
  (`.cpp:74-78`) and the FAILED screen's only action is tap/Back →
  `finish()` (`.cpp:103-107`) — **there is no retry** in this model; the
  issue asks for one.
- **Confirmation before a costly action:** `ConfirmationActivity`
  (`src/activities/util/ConfirmationActivity.h:9-32`), launched with
  `startActivityForResult(... , [](const ActivityResult& r){ if (r.isCancelled) ...})`
  in `FontDownloadActivity.cpp:420-427`, `SdFirmwareUpdateActivity.cpp:128`,
  `FileBrowserActivity.cpp:310`. Heading + body strings.
- **Refresh the launcher after a child download:** `openPublications`
  (`LauncherActivity.cpp:562-568`) — `startActivityForResult(...,
  [this](...) { resolveTargets(); requestUpdate(); })`.
- **Host-testable launcher logic:** header-only, firmware-free helpers beside
  the activity — `LauncherBible.h`, `LauncherRefresh.h` — tested by
  `test/launcher_bible/LauncherBibleTest.cpp` (6 tests, `TEST(LauncherBible, ...)`)
  whose `CMakeLists.txt` adds only `${REPO_ROOT}/src`; registered at
  `test/CMakeLists.txt:121`. The resolution-order and "offer only after three
  misses" tests fit this shape: a pure decision function over the three
  lookups' results, with the activity supplying the I/O.
- **Pre-#104 recents matcher:** the `looksLikeABible` lambda quoted above
  (`git show 08d98a9f`).

## 3. Installed versions

```
$ /Volumes/stein/.platformio/penv/bin/pio --version
PlatformIO Core, version 6.1.19
$ grep -n "^platform\|ArduinoJson" platformio.ini
15:platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
154:  bblanchon/ArduinoJson @ 7.4.2
$ cmake --version
cmake version 4.4.2
```

## 4. Constraints that shape the design

- `lib/I18n/translations/*.yaml` and `test/CMakeLists.txt` are shared append
  points: ui-dev reports the lines in the PR rather than editing them
  (`.claude/agents/ui-dev.md`, "Shared files"). New strings needed at least:
  the no-Bible download invitation, the confirmation heading/body, and a retry
  label if none exists. Existing reusable ones: `STR_DOWNLOAD_FAILED`,
  `STR_PUBLICATION_UNAVAILABLE`, `STR_CHECKSUM_MISMATCH`, `STR_DOWNLOADING`,
  `STR_CANCEL`, `STR_DONE`, `STR_CONNECTING`.
- A registration on open adds a second read of `pubkeys.json` on every book
  open (StudyStore already reads it at `StudyStore.cpp:40`). A write happens at
  most once per path. Doing the check in `EpubReaderActivity::loadBook` next to
  `:249` keeps it off the render path; a registry-side "record if absent" API
  would be cleaner but needs a decision, since `src/study/` is another task's.
- `moveFinishedBookToReadFolder` (`EpubReaderActivity.cpp:150-172`) updates
  recents and `APP_STATE` but not the registry, so a moved Bible's entry goes
  stale (`findBySymbol` then skips it, `PubKeyRegistry.cpp:74`). Register-on-open
  re-records it at the new path the next time it opens; out of scope otherwise.
- Tier: the change stays on the `ui` surface (launcher, reader, network
  activities) plus string and test-list lines reported to the orchestrator. No
  new on-disk format, no registry schema change. `heavy` is sufficient; not
  raised.
