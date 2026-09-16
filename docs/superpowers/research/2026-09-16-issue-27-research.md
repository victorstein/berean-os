# Investigation — atomic, budgeted saves for the four inherited stores

Issue #27, branch `fix/27-atomic-store-saves`. Read on `ded48d17`.

Everything below was read or run in this worktree. Where this note contradicts
the issue body, the issue body is what is wrong.

## Confirmed as stated

- `saveToFileAtomic()` is `lib/Serialization/PersistableStore.h:143-155`, exactly
  as cited: `measureJson` → `persist::fitsBudget` → `LOG_ERR` + `return false`, or
  `writeDocToFileAtomic`.
- It has **zero callers**. `grep -rn saveToFileAtomic src lib test` returns only
  its own definition (`:143`) and the comment naming it (`:125`).
- `persist::DEFAULT_SAVE_BUDGET = 45000`, `SD_READ_TRUNCATION_CAP = 50000`
  (`lib/Serialization/SaveBudget.h:19,23`).
- The four stores all live in `/.crosspoint/`, so `writeDocToFileAtomic`'s
  hardcoded `Storage.mkdir("/.crosspoint")` (`PersistableStore.cpp:23`) is correct
  for all four: `settings.json` (`CrossPointSettings.h:386`), `state.json`
  (`CrossPointState.h:32`), `wifi.json` (`WifiCredentialStore.h:44`),
  `recent.json` (`RecentBooksStore.h:29`).

## Three corrections the spec needs

### 1. There are 55 call sites, not 27

`grep -rn 'saveToFile()' --include='*.cpp' src | wc -l` → **55**. The issue's table
lists 27. Per store: `CrossPointSettings` **35** (not 10), `CrossPointState` **9**
(not 7), `WifiCredentialStore` 5, `RecentBooksStore` **6** (not 5).

Not in the issue's table:

| Store | Additional sites |
|---|---|
| `CrossPointSettings` | `WifiSelectionActivity.cpp:555`, `ButtonRemapActivity.cpp:54,88`, `ClockOffsetActivity.cpp:85`, `ClockSyncActivity.cpp:76`, `SettingsActivity.cpp:230,266,284,305,329,347,361,399`, `StatusBarSettingsActivity.cpp:161,168,174,184,203`, `TextSettingsActivity.cpp:381,389,400,449`, `BmpViewerActivity.cpp:228`, `FrontlightPanelActivity.cpp:69`, `CrossPointWebServer.cpp:1320` |
| `CrossPointState` | `SleepActivity.cpp:446`, `LauncherActivity.cpp:107` |
| `RecentBooksStore` | `RecentBooksActivity.cpp:72` |

`CrossPointWebServer.cpp:1320` is the second writer the `storeMutex` comment at
`PersistableStore.h:27-30` was written for — it is real, not hypothetical.

### 2. `APP_STATE.saveToFile()` is not on the reader's hot path

The brief's research directive assumes `EpubReaderActivity.cpp:151` is a progress
save at page-turn frequency. It is not. `:151` sits inside
`moveFinishedBookToReadFolder` (`EpubReaderActivity.cpp:133-153`) and fires once,
when a finished book is renamed into `/Read`.

Reading progress does not go through any `PersistableStore`. It is a 10-byte
binary record written by `ProgressFile::writeAtomic` (`ProgressFile.h:32`, called
from `EpubReaderUtils.h:35`) — **already atomic, already separate**.

The remaining `CrossPointState` writes are all once-per-transition:
`ReaderActivity.cpp:59` (reader entry), `:69` (reader exit), `main.cpp:263`
(sleep entry), `:521` (splashless-wake re-arm), `:577` (boot-to-reader),
`SleepActivity.cpp:446` (one sleep-image pick per sleep),
`PublicationDownloader.cpp:123` (download rename). `LauncherActivity.cpp:107` is
already guarded by a change check.

So the "measure whether three SD operations at page-turn frequency is
acceptable" question does not arise, and no debounce is needed. The real cost
delta is smaller than the brief implies: each `Storage.*` call takes
`storageMutex` once (`HalStorage.cpp:56,89,93,94`), so the legacy path is 2
acquisitions (`mkdir` + `writeFile`) and the atomic path is 4 (`mkdir` +
`writeFile` + `remove` + `rename`) — two extra directory operations, not two
extra file writes.

The frequency question, if it applies anywhere, applies to
`CrossPointSettings`: `TextSettingsActivity.cpp:337,345` and
`StatusBarSettingsActivity.cpp:161-203` save on each row activation while a user
scrubs through options.

### 3. 52 of the 55 sites discard the return value, not three

Only three consume it: `RecentBooksStore.cpp:85` (logs), and
`WifiCredentialStore.cpp:134,149` (`return saveToFile();`, so a refusal would
surface as `addCredential`/`removeCredential` returning false).

That matters twice over:

- Acceptance criterion 3 names `RecentBooksStore.cpp:62,74,102` "at minimum". The
  honest figure is 52 sites. Handling a refusal individually at 52 sites is a very
  different change from the one the brief describes.
- `saveToFileAtomic` already logs the refusal itself
  (`PersistableStore.h:150-151`, `LOG_ERR("PERSIST", "Refusing to save %s: %u
  bytes exceeds budget %u", ...)`). A refusal is therefore *not* silent at any
  call site today. What "must not become a silent no-op" means beyond that — a
  user-visible message, and on which screens — is a decision the brief does not
  make and the spec has to.

Also worth flagging: `WifiCredentialStore.cpp:134,149` already return false for a
*different* reason (`MAX_NETWORKS` reached, `:124-126`; SSID not found, `:142-143`),
and the only two callers of `addCredential`
(`WifiSelectionActivity.cpp:67,691`) discard that bool. Making a budget refusal
"visible" through that return value inherits a path nothing reads.

## Nearest existing example — there isn't one for a store

The disciplined code the issue praises is real but is a **different shape**. None
of it is a `PersistableStore` subclass; all of it is free-function file modules
that open-code the same two steps:

```cpp
// src/study/TagPaletteFile.cpp:70-76
if (measureJson(json) > persist::DEFAULT_SAVE_BUDGET) {
  LOG_ERR(MODULE, "Tag palette exceeds the save budget; not written");
  return SaveResult::TooLarge;
}
Storage.mkdir(BEREAN_DIR);
return PersistableStoreBase::writeDocToFileAtomic(PATH, json) ? SaveResult::Ok : SaveResult::WriteFailed;
```

Same at `PubKeyRegistry.cpp:36-42`, `MeetingWeekCache.cpp:50`,
`MigrationRunner.cpp:119` (which reserves 2,048 bytes of headroom),
`PassageFile.cpp:106`. Note what they return: a typed result
(`SaveResult::TooLarge` vs `WriteFailed`), not a bool. `saveToFileAtomic` returns
bool, so "too large" and "SD write failed" are indistinguishable to a caller.

Two consequences:

- **This change would be the first use of `saveToFileAtomic()` in the tree.** No
  call-site precedent exists to copy.
- **No store declares `static constexpr size_t SAVE_BUDGET`.** `grep -rn
  SAVE_BUDGET src lib` finds only the `saveBudget()` machinery itself
  (`PersistableStore.h:109-117`) and `SAVE_BYTE_BUDGET`, a differently named
  constant belonging to the free-function modules (`HighlightFile.h:42`,
  `PassageDoc.h:26`). The `if constexpr (requires { T::SAVE_BUDGET; })` branch at
  `PersistableStore.h:112` has therefore **never been compiled with the branch
  taken**. Declaring the first one is the first test of it.

## No host test can cover this

`PersistableStore.h:3` includes `<Arduino.h>` unconditionally, so nothing that
instantiates a `PersistableStore` subclass is host-buildable. This is already
documented where it bit: `test/highlight_file/CMakeLists.txt:1-4` — "HighlightFile.cpp
itself pulls Arduino.h transitively through PersistableStore.h and cannot be
built here."

`test/save_budget/SaveBudgetTest.cpp` covers `persist::fitsBudget` as pure
arithmetic (5 cases, including `HonoursAPerStoreBudget` at `:32`). That is the
whole of what the host suite can reach. **A new host test cannot prove this
change.** Verification is `pio run` plus the device, and the spec should say so
rather than promise a test.

## Budget evidence, per store

Measured from the serialising code, not guessed.

| Store | Bound | Serialised estimate |
|---|---|---|
| `CrossPointState` | fixed shape | ~500 B |
| `CrossPointSettings` | fixed shape | ~1.5 KB |
| `WifiCredentialStore` | `MAX_NETWORKS = 8` (`WifiCredentialStore.h:35`) | ~1.8 KB |
| `RecentBooksStore` | `MAX_RECENT_BOOKS = 10` (`RecentBooksStore.h:21`) | unbounded in principle — see below |

- **`CrossPointState`** (`CrossPointState.cpp:43-57`): two path strings, two
  16-element `uint16_t` arrays, seven scalars. Nothing here grows with use.
- **`CrossPointSettings`** (`CrossPointSettings.cpp:63-105`): the generic loop
  writes one key per `SettingsList.h` row. `grep -oE '"[A-Za-z][A-Za-z0-9_]*"'
  src/SettingsList.h | sort -u` → 53 keys, 898 characters of key name; plus 9
  hand-written keys (136 characters) at the tail of `toJson`. Values are almost
  all single bytes. The only sizeable fields are `downloadFolder[64]`
  (`CrossPointSettings.h:274`) and `sdFontFamilyName[32]` (`:293`).
- **`WifiCredentialStore`** (`WifiCredentialStore.cpp:9-24`): 8 entries × (`ssid`
  ≤ 32 B by 802.11, `password_obf` = base64 of the password, `password_len`,
  `password_crc32`) + `lastConnectedSsid`. **Corrected after review pass 0.** The original text here claimed no
  write-side cap existed; that was wrong and the spec inherited it. There are
  three producers: the **UI caps at 64** (`WifiSelectionActivity.cpp:352`, and 32
  for a hidden SSID at `:375`, both enforced at `KeyboardEntryActivity.cpp:250`);
  the **web server does not cap at all** (`CrossPointWebServer.cpp:1376` checks
  only that the SSID is non-empty, `:1385` takes the password unbounded, and
  `:1406,1408,1418` pass it to `addCredential`, which does not check either,
  `WifiCredentialStore.cpp:112`); and the **load path bounds the steady state**,
  discarding any credential with `password_len > MAX_PASSWORD_LENGTH` and
  requesting a resave (`WifiCredentialStore.cpp:52-56`). So an oversized
  web-written password survives only until the next boot, and the on-disk worst
  case is computable at ~1.7 KB.
- **`RecentBooksStore`** (`RecentBooksStore.cpp:11-20`): 10 entries × four
  strings — `path`, `title`, `author`, `coverBmpPath`. The count is bounded
  (`RecentBooksStore.cpp:57-59`, `MAX_RECENT_BOOKS` trim); the strings are not. `title` and `author` come
  straight from EPUB metadata via `ReaderActivity.cpp:60` with no truncation on
  the way in. This is the one store where a tight budget can refuse a *legitimate*
  save, and where refusing means recents silently stops updating — logged, but
  invisible on screen.

## `CrossPointSettings.h:354` does not block this change

The comment (`:350-354`) is about `StatusBarSpec`, and says the opposite of a
constraint on the write path: the *snapshot* is deliberately built without
`storeMutex`, because locking there would put a mutex on the render path and
"stall it behind the SD write inside `saveToFile()`". `saveToFileAtomic` takes the
same `storeMutex` in the same place (`PersistableStore.h:144` vs `:127`), so
nothing about that comment changes.

The second-order effect is worth stating in the spec though: the atomic path
holds `storeMutex` across four `storageMutex` acquisitions instead of two, so the
hold is roughly twice as long. Nothing waits on it today — that is precisely what
`PersistableStore.h:31-36` and `CrossPointSettings.h:350-354` are protecting — but
the margin for a future reader that takes it gets narrower, not wider.

## Environment — resolved 2026-09-16, after this note was written

At the time of research the worktree could not build: `freeink-sdk` was
uninitialised and there was no `.venv`. **Both were fixed outside this branch**
and the worktree builds. What remains true, and still bites:

- `~/.platformio/penv/bin/pio` is the pio entry point; the bare `pio` is not on
  `PATH`.
- The format wrapper needs `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix`.
  `bin/clang-format-fix:4-12` exits 1 when no binary is found and `:26-31` exits 1
  below clang-format 21, so a skipped prefix fails loudly rather than passing
  falsely.
