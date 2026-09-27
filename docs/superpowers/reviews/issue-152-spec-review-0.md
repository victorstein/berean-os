Tier: heavy

# Issue #152 spec review — pass 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-152-design.md` against `gh issue view 152` and
`docs/superpowers/research/2026-09-27-issue-152-research.md`, at `dc97292e` (still `origin/main`:
`git log --oneline -1 origin/main`).

## What was verified and holds

- **Item 1.** `HalFileReader::read()` is `file_.read()` (`PersistableStore.cpp:23`). On device each
  `HalFile::read` overload takes `StorageLock` (`lib/hal/HalStorage.cpp:138-141,159-160`). ArduinoJson
  v7.4.2's generic `Reader<TSource>` forwards `read()`/`readBytes()` as ordinary members of a class
  template (`build/test/_deps/arduinojson-src/src/ArduinoJson/Deserialization/Reader.hpp:15-32`), and
  the JSON parser pulls one char per `reader_.read()` (`Json/Latch.hpp:38`). `BufferedFileReader`
  exposes only `read(void*, size_t)`, returns 0 at EOF or error (`BufferedFile.h:81-108`, `:96`), and
  its OOM passthrough is still `file.read(p, len)` (`:83-88`), so A2 is exact. The proposed
  `JsonFileReader` maps 0 to -1, which ArduinoJson treats as end of input like before
  (`Latch.hpp:38-43`).
- **Item 1 test.** The fake's `HalFile::read()` goes through `read(void*, size_t)`
  (`test/stubs/HalStorageFake.cpp`, `int HalFile::read()` → `read(&byte, 1)`), `Impl` already
  carries `path` (it is used by `writeLimit`). `AtomicWrite.ADocumentPastTheReadCapIsStreamedWholeAndReadBackWhole`
  writes 60,008 bytes (`AtomicWriteTest.cpp:87-95`). A counter-based red test is feasible:
  60,008 / 512 → about 118 buffered reads against about 60,008 today. `HalStorageFake.cpp` is
  already in `StorageIoTest` (`test/storage_io/CMakeLists.txt:18`).
- **Item 2.** All nine `mkdir` lines are where the spec says they are, and each creates exactly the
  parent of the path that `writeDocToFileAtomic` then writes. `ensureParentDirectory` is at
  `PersistableStore.cpp:56-62,104`, and `mkdir` defaults to `pFlag = true`
  (`lib/hal/HalStorage.h:34`). `getBookmarksDir()`/`highlightsDir()` have a trailing `/`
  (`BookmarkUtil.cpp:11`, `HighlightFile.cpp:14`), which names the same directory
  `ensureParentDirectory` makes. None of the six non-host call sites is in any host `CMakeLists.txt`
  (`grep` over `test/**/CMakeLists.txt` finds only "no host suite yet" comments).
- **Item 3.** `grep -rn -e wasTapInRect -e rowTouch -e colTouch -e getPressedFrontButton -e RowTouch src lib test freeink-sdk`
  outside `src/MappedInputManager.*` returns nothing. `wasScreenTouchDown` keeps live callers after
  the deletion (`UiAppHelpers.h:187`, `PassageSelectActivity.cpp:436`), so removing `rowTouch` and
  `colTouch` leaves no new dead code behind.
- **Item 4 arithmetic.** Today: 18 + 9 + 10 × (52 + 2 × 224 + 512 + 128) = 11,427
  (`RecentBooksDocTest.cpp:50`). New entry overhead `{"path":"","title":"","author":""}` = 34 bytes,
  so 18 + 9 + 10 × (34 + 448 + 512) = 9,967, and the doc-overhead test becomes 18 + 9 + 340 = 367.
  All three figures in the spec are correct. `updateBook` and `getDataFromBook` have no callers.
  The no-arg `Epub::getThumbBmpPath()` is used only at `EpubReaderActivity.h:167`,
  `EpubReaderActivity.cpp:452` and `RecentBooksStore.cpp:125`, while the `(int)` overload keeps
  `LauncherActivity.cpp:251` and `PublicationsActivity.cpp:85`. Both `updatePath` callers keep
  their cache-path locals for the directory rename (`PublicationDownloader.cpp:112-118`,
  `EpubReaderActivity.cpp:143-148`).
- **A9 (settled d1).** The reasoning holds against the code: `fromJson` reads by key
  (`RecentBooksDoc.cpp:70-73`), and an old build reads a missing key as `""`. No other code reads
  `coverBmpPath` from `recent.json`: the only other identifier is a local at
  `SleepActivity.cpp:820`, and nothing under `data/`, `scripts/` or `test/` outside
  `recent_books_doc` reads it.
- **Item 5.** `HomeMenuItem` appears only in `main.cpp`, `Activity.{h,cpp}` and
  `ActivityManager.{h,cpp}`. Every `goHome(`/`onGoHome(` call other than `main.cpp:578` takes no
  arguments.
- **Items 7 and 9.** `STR_BIBLE: "Biblia"` is at `spanish.yaml:8`, and `SAVE_BYTE_BUDGET` is at
  `HighlightFile.h:39`. `writeDocToFile` is at `PersistableStore.cpp:92` and is still `String`-based
  (`:94-96`). `CLAUDE.md -> AGENTS.md` is confirmed with `ls -la`. The amendment model is at
  `2026-09-16-issue-30-design.md:170-172`.

## Findings

### MAJOR-1 — Item 6 would write an unreachable route into the user guide

**Claim.** "`:236-238` §12 → the file browser is reached from the reader's Back (short press goes
home, a long press opens the file browser by default — `ReaderUtils.h:240-266`) or from the Bible tile
when no Bible is on the card."

**Problem.** On this device, a long press of Back cannot happen, so the first half of the sentence
tells users to do something that does nothing.

- The reader ignores the Back *gesture* outright: `if (mappedInput.wasBackGesture()) return false;`
  (`ReaderUtils.h:255-257`).
- The only other source of `Button::Back` is the synthesised Left-key hold. It is never held
  (`HalGPIO.cpp:179-180`), and the moment it fires, `getHeldTime()` reports
  `SYNTHETIC_HELD_MS` (`HalGPIO.cpp:236-238`), which is 40 ms (`lib/Input/Input/NavKeyGestures.h:68`).
  That is always below `GO_BACK_OR_HOME_MS = 1000` (`ReaderUtils.h:17-18`). The comment at
  `HalGPIO.cpp:229-235` says so explicitly: "without this substitution a Back held for 900 ms arrives
  … as a long press".
- `longPress != SETTINGS.backShortToFileBrowser` (`ReaderUtils.h:262`) therefore always takes the
  `goHome` branch. `backShortToFileBrowser` defaults to 0 (`CrossPointSettings.h:337`), and the spec's
  own N5 establishes that no `SettingInfo` exposes it.
- The sentence also contradicts the guide it edits: `USER_GUIDE.md:38` says "There is no Back button",
  and `:48` defines Back as the swipe the reader ignores.

The only reachable `goToFileBrowser` callers are `LauncherActivity.cpp:551` (the Bible tile with no
Bible found), `ReaderUtils.h:263` (unreachable, as above) and `BmpViewerActivity.cpp:260` (entered
from the browser itself).

**Fix.** In §12, say only that the file browser opens from the **Bible** tile when no Bible has
been found (`LauncherActivity.cpp:547-555`). Drop the reader-Back route. Add a PR-body follow-up
beside N5: once a Bible is on the card, the device has no reachable file-browser entry. Fix inline;
this changes no decision or scope.

### MAJOR-2 — Item 8's replacement text is false against the tree

**Claim.** "`docs/contributing/touch-and-ui.md:7` → the legacy helpers are gone; the reader page is
the one remaining hand-rolled surface … `:130`'s 'Direct use only in the two legacy surfaces' → 'the
reader page'."

**Problem.** Both replacement sentences are wrong. This is the same defect the issue asks to fix: a
doc naming the wrong users of the touch helpers.

- The launcher that replaced the home screen is itself hand-rolled. It is a plain `Activity`
  (`LauncherActivity.h:24`), and its tap handling is a manual `rects[i].contains(touchX, touchY)`
  loop (`LauncherActivity.cpp:578-587`). The `contains()` checks named on line 7 are exactly this.
- Raw `wasScreenTapped` / `wasScreenTouchDown` calls outside the FUI snapshot builder are in 12
  activity files besides `ReaderUtils.h`: `grep -rln "wasScreenTapped\|wasScreenTouchDown\|isScreenTouchHeld" src/activities`
  → `CatalogSearchActivity`, `CrashActivity`, `LauncherActivity`, `MeetingDownloadActivity`,
  `WifiSelectionActivity`, `PassageSelectActivity`, `ClearCacheActivity`, `ClockSyncActivity`,
  `FontDownloadActivity`, `OtaUpdateActivity`, `SdFirmwareUpdateActivity`, `KeyboardEntryActivity`.
  `PassageSelectActivity.cpp:436` reads `wasScreenTouchDown` directly.

**Fix.**

- Line 7: `rowTouch`, `colTouch` and `wasTapInRect` are deleted. Manual `contains()` hit-testing
  survives in the launcher (`LauncherActivity.cpp:582`) and the reader page, and must not spread.
- Line 130: drop "Direct use only in …". Say instead that several non-FUI screens still read these
  accessors directly for dismiss-on-tap and prompts, and that new code must not.

Fix inline.

### MINOR-1 — Item 6 misses a sibling "Home ->" route

**Claim.** The `:198` rewrite is justified as "the same stale 'Home ->' path; fixing §4 without it
would leave a dead route".

**Problem.** `USER_GUIDE.md:251` reads "**Home -> Settings**, four tabs." It still works, because the
Settings tile exists (`LauncherActivity.cpp:507`). But §4 is being renamed "Launcher" and the other
"Home screen" mentions are being reworded, so this line is left as the one place that names the
screen differently.

**Fix.** Add `:251` to the reword list: "**Launcher -> Settings**", or "the **Settings** tile".

### MINOR-2 — Item 4's table misses two comments the removal makes stale

**Problem.**

- `src/RecentBooksStore.h:24` says "path and coverBmpPath carry explicit allowances". After the
  change, `COVER_PATH_BUDGET_ALLOWANCE` no longer exists.
- If `<Epub.h>` is dropped from `RecentBooksStore.cpp` (table row 7 allows it), the header of
  `test/recent_books_doc/RecentBooksDocTest.cpp:3-4` still says that file "also includes Epub.h and
  HalStorage.h".

**Fix.** Add both lines to the item 4 table.

### MINOR-3 — Item 5 keeps a sentence that is itself stale

**Claim.** Delete the justification comment at `ActivityManager.cpp:241-248`, "keeping its first
sentence about the launcher".

**Problem.** That sentence reads "bereanOS's home is the launcher: Bible, Meetings, Buscar, Tags and
settings, plus a resume strip" (`ActivityManager.cpp:242-243`). The launcher has no Tags tile: the
tiles are Bible, Meetings, Publications, Settings and Continue Reading
(`LauncherActivity.cpp:500-513`). The spec explicitly preserves a wrong line.

**Fix.** Keep a sentence, but one that matches `LauncherActivity.cpp:500-513`.

### MINOR-4 — A7 does not record that it reverses an earlier keep decision

**Problem.** The #102 plan deliberately kept `rowTouch`/`colTouch` "until the input-layer replacement
lands" (`docs/superpowers/plans/2026-09-27-issue-102-plan.md:336-339`). That wording is still in
`touch-and-ui.md:132`. The issue authorises the deletion ("if nothing on this device needs them"),
so the outcome is right. But A7 argues only from `getPressedFrontButton`'s comment and never names
the earlier decision it overturns.

**Fix.** Add one line to A7: the #102 "keep until replacement" choice is superseded because #152
item 3 asks for the deletion, and git keeps the bodies.

### MINOR-5 — A3 adds a method the spec says nothing calls

**Problem.** A3 keeps `JsonFileReader::readBytes` while stating that the JSON parser never calls it.
`Reader<T>::readBytes` is instantiated only if used (`Reader.hpp:26-28`), so nothing requires it.
That makes it new dead code in a PR whose stated goal is removing dead code (Goal, bullet 2).
Mirroring the writer is not a reason: the writer's two-argument `write` is called by
`serializeJson`.

**Fix.** Drop `readBytes` from `JsonFileReader`, or give it a real reason in A3. Either is fine; do
not keep it on symmetry alone.

VERDICT: CLEAR
