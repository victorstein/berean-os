# Issue #101 — PR #123 code-quality review, pass 0

Scope: `gh pr diff 123`, head `c8fc6816`. The code under review is
`lib/Serialization/{FormatVersion.h,PersistableStore.h}`, the four stores (`CrossPointSettings`,
`CrossPointState`, `WifiCredentialStore`, `RecentBooksStore` / `RecentBooksDoc`), `src/main.cpp`,
`src/activities/SettingsSave.h`, and the `format_version`, `persistable_store` and
`recent_books_doc` test suites. The planning documents under `docs/superpowers/` were read only
where the code points to a decision recorded there.

Verification I ran myself: I ran the three touched suites' binaries from the worktree. The results
were `PersistableStoreTest` `[  PASSED  ] 5 tests.`, `FormatVersionTest` `[  PASSED  ] 9 tests.`,
and `RecentBooksDocTest` `[  PASSED  ] 22 tests.` The two `add_subdirectory` lines are uncommitted
in `test/CMakeLists.txt`, as the PR body says.

## What holds up

- **It mirrors the existing version rule and does not invent a new one.**
  - Every store reads `doc["v"] | FORMAT_VERSION` and refuses `<= 0` or `> FORMAT_VERSION`. That
    is exactly `BookmarkDoc.cpp:31-32`, the inherited-store precedent the spec names (A1).
  - The newer `/.berean/` stores (`TagPalette.cpp:78`, `ChapterCompletion.cpp:111`) default to
    `| 0`. That deliberately does not apply here, because these four files exist on cards with no
    `"v"`.
  - Each constant is placed like its sibling: a class-level `static constexpr` for the three
    classes, and a namespace `inline constexpr` for `RecentBooksDoc` (`RecentBooksDoc.h:22`),
    matching `BookmarkDoc.h:23`.
- **The new header matches its siblings' shape.** `persist::isKnownFormatVersion` and
  `persist::loadRefusedAfter` are `constexpr`, free of Arduino and ArduinoJson, and kept in the
  `persist` namespace. That is the same shape as `persist::fitsBudget` (`SaveBudget.h:26`) and
  `classifyDocRead` (`DocReadStatus.h:17`). The exhaustive `switch` with a trailing `default:`
  (`FormatVersion.h:22-31`) follows the form `TempAdoption.h:39,58` already uses.
- **Error handling follows the established shape.**
  - The save guard logs under the existing `"PERSIST"` tag and returns `false`, in the same form
    as the budget refusal beside it (`PersistableStore.h:188-191`). No new error channel is added.
  - UI reporting reuses the existing paths: the "save failed" popup in `SettingsSave.h`, and the
    web API's 500.
  - Each store's refusal log uses that store's existing tag (`"CPS"`, `"STATE"`, `"WCS"`,
    `"RBS"`).
- **The refusal comes before any mutation.**
  - `RecentBooksDoc::fromJson` now checks the version before `books.clear()`
    (`RecentBooksDoc.cpp:56-59`), and a test pins this directly
    (`RecentBooksDocTest.cpp` `ARefusedDocumentLeavesTheListUntouched`).
  - The three class stores check before their first member assignment.
- **The tests are well designed, not just present.**
  - `persistable_store` runs the real template and the real `PersistableStore.cpp` against the #121
    fake. It asserts card bytes rather than call sequences, including that no `.tmp` file is
    written before the refusal.
  - The fixture resets the process-wide singleton through its own public contract: a load on an
    empty card returns Missing and clears the flag. It does not reach into private state for this.
  - The unparseable-file pair checks both sides of the "unchanged corrupt-file handling" claim:
    one where the flag stays set, one where it stays clear.
  - `FormatVersionTest` includes the header the same way `DocReadStatusTest.cpp:3` does.
  - The two `static_assert`s keep the helpers `constexpr`.
- **Comments explain the reason, not the mechanism.**
  - `main.cpp:415-417` says why `wifi.json` loads at boot.
  - `PersistableStore.h:49-52` states who writes the flag.
  - `RecentBooksDoc.h:83-88` corrects the stale "nothing to refuse" contract instead of leaving
    it.
  - No comment restates the next line, and there is no commented-out or dead code.

## Findings

### MINOR 1 — the extracted rule now has two spellings, and the original precedent keeps the inline one

`persist::isKnownFormatVersion` (`FormatVersion.h:13-15`) is the rule
`version > 0 && version <= newestKnown`. The store this PR modelled itself on still writes it out
by hand: `src/util/BookmarkDoc.cpp:32`, `if (version <= 0 || version > FORMAT_VERSION) return false;`.

After this PR, the inherited-store pattern (`| FORMAT_VERSION` plus a refusal) appears five times:
four call the helper and one open-codes it. A later edit to the rule, such as accepting a
migration range, would have to find both spellings.

Fix inline: change `BookmarkDoc.cpp:32` to
`if (!persist::isKnownFormatVersion(version, FORMAT_VERSION)) return false;` and add
`#include <FormatVersion.h>`. `BookmarkDocTest.cpp` already covers the behaviour. The `/.berean/`
stores should stay as they are, because their `| 0` default is a different rule.

### MINOR 2 — `RecentBooksStore` recomputes the version for its log differently from its three siblings

The other three stores log the `version` they tested (for example `CrossPointSettings.cpp:110-114`).
`RecentBooksStore.cpp:16-19` instead reads the version again as `doc["v"] | 0`, after
`RecentBooksDoc::fromJson` returned `false`. That has two effects:

- The default differs from the one the check used (`| FORMAT_VERSION`).
- The message assumes that `false` can only mean "unknown version".

Both hold today, since `RecentBooksDoc.cpp:58` is the only `false` return. But the store now
depends, silently, on an invariant held in another file. If a second refusal reason is added to
`RecentBooksDoc::fromJson`, it will be logged as a version error.

Fix inline: use `doc["v"] | RecentBooksDoc::FORMAT_VERSION` so the value logged is the one that
was tested. Alternatively, log a neutral "refused by RecentBooksDoc" and leave the version detail
to the doc layer. This is minor, because the log is correct for every input the code can currently
receive.

### MINOR 3 — the save guard is duplicated verbatim in both save paths

The same four lines, including the log string, appear in `saveToFile()` and `saveToFileAtomic()`
(`PersistableStore.h:160-163` and `:181-184`). These are the only two writers, and both need the
guard, so the duplication is small and contained. A private helper would still keep the message in
one place:

`bool refuseSaveIfLoadRefused() const`

Treat this as optional. I would not hold the PR for it.

### MINOR 4 — a second "real store I/O against the fake" suite beside `storage_io`

`test/storage_io/CMakeLists.txt:1-4` states that it is the suite that "runs the real store I/O --
PersistableStore.cpp's atomic write and adopting read ... against the in-memory Storage fake".
`test/persistable_store/` compiles the same `PersistableStore.cpp`, `HalStorageFake.cpp` and
`ObfuscationUtilsStub.cpp` into a separate executable, with its own CMake file and its own
`add_subdirectory` append point.

`PersistableStoreTest.cpp` could be added as one more source in `StorageIoTest`, next to
`AtomicWriteTest.cpp`. That removes the new directory and one of the two orchestrator-appended
lines. The separate suite is not wrong: it isolates the singleton `ProbeStore`, and the name is
clear. The cost is structural drift, not correctness.

## Nothing found at MAJOR or BLOCKER

- No decision is reversed, and the scope matches the spec.
- The helper, the flag and the tests do not duplicate anything that already existed.
- The one partial duplication, the inline rule in `BookmarkDoc`, is MINOR 1.

VERDICT: CLEAR
