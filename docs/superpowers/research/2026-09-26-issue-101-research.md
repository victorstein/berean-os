# Issue #101 — research: four inherited stores carry no format version

Branch `fix/101-inherited-store-format-version`, base `2f303f6f` (release 1.16.0).
Every claim below cites a line read in this worktree or a command run in it.

## 1. Which files own the behaviour

All four are CRTP singletons on `PersistableStore<T>`; none writes or reads a `"v"`.

| Store | File on SD | `toJson` / `fromJson` | Budget | Host test today |
|---|---|---|---|---|
| `CrossPointSettings` | `/.crosspoint/settings.json` (`src/CrossPointSettings.h:431`) | `src/CrossPointSettings.cpp:63-105` / `:107-236` | 4096 (`.h:429`) | none |
| `CrossPointState` | `/.crosspoint/state.json` (`src/CrossPointState.h:36`) | `src/CrossPointState.cpp:43-57` / `:59-96` | 2048 (`.h:34`) | none |
| `WifiCredentialStore` | `/.crosspoint/wifi.json` (`src/WifiCredentialStore.h:47`) | `src/WifiCredentialStore.cpp:11-26` / `:28-112` | 8192 (`.h:45`) | none (only `WifiCredentialEdit`, `CredentialIntegrity`) |
| `RecentBooksStore` | `/.crosspoint/recent.json` (`src/RecentBooksStore.h:27`) | shell `src/RecentBooksStore.cpp:11-23`; format `src/util/RecentBooksDoc.cpp:42-74` | `RecentBooksDoc::SAVE_BUDGET` = 11421 | `test/recent_books_doc/RecentBooksDocTest.cpp` |

`grep -rln 'CrossPointSettings\|CrossPointState\|WifiCredentialStore' test` returns only
`test/credential_integrity/*` — Settings, State and the Wifi store's `fromJson` have no host
coverage, and their `.cpp` files include Arduino-dependent code. `RecentBooksDoc` is the only
one of the four whose format is already split out Arduino-free for host testing
(`src/util/RecentBooksDoc.h:14-18`).

Shared machinery: `lib/Serialization/PersistableStore.h` (template), `PersistableStore.cpp`
(read/write), `DocReadStatus.h`, `TempAdoption.h`, `SaveBudget.h`.

No settings key collides with `"v"`: `grep -rnE '"v"' src/SettingsList.h src/*.cpp src/util/*.cpp`
matches only `src/util/BookmarkDoc.cpp:8,29,31`.

## 2. Current control flow

### Load

`PersistableStore<T>::loadFromFile()` (`PersistableStore.h:179-199`):

1. Locks `storeMutex`, reads through `readDocFromFileAdopting` (`:186`). Any status other
   than `Ok` → `return false` with **the in-memory object untouched** (`:186-188`).
2. Calls `T::fromJson(doc)` (`:189`). Its bool is returned as-is.
3. If `fromJson` returned true **and** called `requestResave()`, saves after releasing the
   lock (`:195`). A `false` from `fromJson` never triggers the resave.

So a `fromJson` that checks the version **first** and returns `false` leaves the object in
whatever state it had before the call, and the load path itself writes nothing. All four
`fromJson`s today return `true` unconditionally (`CrossPointSettings.cpp:235`,
`CrossPointState.cpp:95`, `WifiCredentialStore.cpp:111`, `RecentBooksDoc.cpp:73`).

Mutation order matters for the "fall back to defaults" requirement: each `fromJson` starts
overwriting members immediately (`CrossPointState.cpp:60`, `RecentBooksDoc.cpp:54`,
`WifiCredentialStore.cpp:30,34`, Settings loop at `CrossPointSettings.cpp:113`). A version
check must precede the first assignment.

### Who loads, and what "defaults" means at that moment

`grep -rn 'loadFromFile()' src lib`:

- `src/main.cpp:411-413` — `SETTINGS`, `APP_STATE`, `RECENT_BOOKS` at boot. The singletons
  hold their struct-initialiser defaults here, so a refused load *is* "fall back to defaults".
  The return value is ignored at all three.
- `src/activities/launcher/LauncherActivity.cpp:97`, `src/activities/catalog/PublicationsActivity.cpp:45`
  — `RECENT_BOOKS` **reloaded** mid-session. A refused reload here keeps the previous
  in-memory list, not defaults (only relevant if the file changed under a running build).
- `src/activities/network/WifiSelectionActivity.cpp:106` — `WIFI_STORE`, on entering WiFi
  selection. Same: a refusal keeps whatever was in memory (empty on first entry).

### Save — the part the issue's "without overwriting the file" depends on

Refusing at load is not enough on its own. Every store is written back unconditionally later,
from its in-memory state, by `saveToFileAtomic()` (`PersistableStore.h:165-177`), which has
no knowledge of what the last load saw. Call sites (`grep -rn 'saveToFileAtomic()' src`):

- `APP_STATE`: `src/main.cpp:263` (every `enterDeepSleep`), `:521`, `:577`;
  `ReaderActivity.cpp:59,69`; `EpubReaderActivity.cpp:153`; `LauncherActivity.cpp:130`;
  `SleepActivity.cpp:446`; `network/PublicationDownloader.cpp:123`.
- `SETTINGS`: `main.cpp:222` (power double-click frontlight toggle); `SdCardFontSystem.cpp:21`;
  `network/CrossPointWebServer.cpp:1292`; `activities/SettingsSave.h:16`;
  `ClockSyncActivity.cpp:78`; `BmpViewerActivity.cpp:230`; `WifiSelectionActivity.cpp:568`.
- `RECENT_BOOKS`: `RecentBooksStore.cpp:47,62,75,92`; `RecentBooksActivity.cpp:72`.
- `WIFI_STORE`: `WifiCredentialStore.cpp:125,152,175,240,254,263`.

Consequence: with only a load-side refusal, `state.json` from a newer build would be replaced
by defaults on the first sleep (`main.cpp:263`). Meeting the issue's "without overwriting the
file" needs a save-side guard too. **No such guard exists in `PersistableStore` today**; see §4
for the pattern this repo uses elsewhere.

## 3. Installed versions

| Tool / package | Version | Evidence |
|---|---|---|
| ArduinoJson (firmware) | 7.4.2 | `platformio.ini:151` `bblanchon/ArduinoJson @ 7.4.2` |
| ArduinoJson (host tests) | v7.4.2 | `test/CMakeLists.txt:28-32` `GIT_TAG v7.4.2` |
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on PATH) |
| CMake | 4.4.2 | `cmake --version` |

ArduinoJson 7 `operator|` semantics this relies on, as used and tested in-repo: the default
is returned when the key is absent **or unconvertible**, and a present value is kept
(`BookmarkDoc.cpp:28-31`; `test/bookmark_doc/BookmarkDocTest.cpp:81-115` pins absent→1,
present 0→refused, `FORMAT_VERSION+1`→refused). Corollary: a non-integer `"v"` such as
`"2"` also reads as the default.

## 4. Nearest existing examples

### Version stamping and refusal — two idioms coexist

- **`BookmarkDoc`** (`src/util/BookmarkDoc.cpp:8,31-32`, `.h:22-23`) — the exact semantics
  the issue asks for: `doc["v"] | FORMAT_VERSION` (absent → 1), refuse `version <= 0 ||
  version > FORMAT_VERSION`, `FORMAT_VERSION` a public `inline constexpr int` in an
  Arduino-free header, host-tested. Added in PR #47 (`git log -S'doc["v"] = FORMAT_VERSION'`
  → `d043fd84`). This is the closest model: it retrofitted a version onto an existing
  unversioned file.
- `lib/StudyStore/StudyStore/{ChapterCompletion,TagPalette,PassageDoc}.cpp` use
  `doc["v"] | 0` and refuse `<= 0` — they were born versioned, so an absent `"v"` is invalid
  there. Not the right model for inherited files.
- `src/study/PubKeyRegistry.cpp:28-33`, `src/network/MeetingWeekCache.cpp:24`,
  `src/study/MigrationRunner.cpp:69,96` — refuse only `> FORMAT_VERSION`, with the
  `FORMAT_VERSION` a file-local `constexpr int` in an anonymous namespace.

### Refusing to write after a refused read

- In-function: `PubKeyRegistry::record` (`PubKeyRegistry.cpp:23-32`) reads, and refuses to
  write when the on-disk `"v"` is newer. Possible because it is read-modify-write in one call.
- Caller-held flag: `EpubReaderActivity.cpp:1770-1775` sets `bookmarksSaveDisabled` when
  `BookmarkFile::load` returns `Failed`, and saving stays disabled while the book is open;
  `EpubReaderBookmarksActivity.cpp:35-45` refuses to open a list that could save.
  `docs/file-formats.md:363-364` documents the same rule for completion: "refuses the file and
  records nothing for the session rather than overwriting it".

Neither maps directly onto a CRTP singleton with ~30 save call sites. A "saves disabled
after a refused load" flag inside `PersistableStore<T>` (alongside the existing
`resaveRequested`, `PersistableStore.h:44-46`) would be the natural home, but it is a new
cross-cutting behaviour in `lib/Serialization` affecting every store — a spec-phase
decision, not something to settle here.

### Host-testable decision logic

`classifyDocRead` (`DocReadStatus.h:17-22`), `tempAdoptionAction` (`TempAdoption.h:29-42`),
`fitsBudget` (`SaveBudget.h:26`) are `constexpr`, Arduino-free, and each has a host suite
(`test/doc_read_status`, `test/temp_adoption`, `test/save_budget`). A version-acceptance
predicate shared by the four stores would follow this shape; it is the only way to host-test
Settings/State/Wifi, whose `.cpp`s cannot compile on the host.

## 5. Constraints the change will hit

- **`RecentBooksDoc` budget is exact.** `SAVE_BUDGET = worstCaseBytes()` "fits by exactly
  zero bytes" (`RecentBooksDoc.h:66-70`), `DOC_WRAPPER_BYTES = 12` models `{"books":[]}`
  (`:46-47`), and the test pins `11421u` (`RecentBooksDocTest.cpp:50`) and
  `EXPECT_EQ(measured, SAVE_BUDGET)` (`:283`). Adding `"v":1,` adds 6 bytes, so the wrapper
  constant, the pinned number and `RecentBooksStore.cpp:125-129`'s `static_assert`s must move
  together. `RecentBooksDoc.h:84-86` also says "there is no format version to police" — that
  comment becomes false.
- **Other budgets have headroom.** State ~1,180 B worst case vs 2048 (`CrossPointState.h:32-34`);
  Settings ~1,600 vs 4096 (`CrossPointSettings.h:427-429`); Wifi ~1,700 vs 8192
  (`WifiCredentialStore.h:41-45`). `CrossPointState.h:32` counts "11 keys" — becomes 12.
- **Persisted enums keep their numeric values** (`.claude/agents/data-dev.md`); a version
  field does not touch them.
- **Resave interaction.** Each `fromJson` may `requestResave()` (Settings `:228-231`, Wifi
  `:106-109`, RecentBooks `RecentBooksStore.cpp:20`). A v1-absent file that loads fine will
  gain `"v":1` only on the next save that happens for another reason, unless the absent case
  also requests a resave — a design choice for the spec.
- **`docs/file-formats.md`** documents only `book.bin`, `section.bin`,
  `/.berean/completion/<pubkey>.json` (`:350-375`) and `/.berean/search/bible.idx` (`:377-`).
  None of the four `/.crosspoint/*.json` files is there yet; the completion entry is the
  JSON-file template to mirror.
- **Shared files.** Per `.claude/agents/data-dev.md`, `test/CMakeLists.txt` is an append
  point this surface must not edit; a new host suite needs its `add_subdirectory` line handed
  to the orchestrator in the PR description.
