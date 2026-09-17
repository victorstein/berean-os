# Adopting an orphaned `.tmp` on the `PersistableStore` read path

**Date:** 2026-09-17
**Status:** SPEC — not implemented
**Issue:** #51, "An orphaned `.tmp` is never adopted, so an interrupted atomic write still loses the file"
**Target:** bereanOS, `x4pro` (ESP32-S3, 8 MB PSRAM)
**Branch:** `fix/51-adopt-orphaned-tmp`
**Modelled on:** `src/util/HighlightFile.cpp:23-68` for the load-then-promote sequence,
`src/util/HighlightFileAction.h:34-47` for the decision function it switches on, and
`lib/Serialization/PersistableStore.h:124-131` (`saveToFile` vs `saveToFileAtomic`) for the
opt-in-safe-variant shape this change adds on the read side.
**Builds on:** `docs/superpowers/research/2026-09-17-issue-51-research.md`.

---

## Problem

`writeDocToFileAtomic` (`lib/Serialization/PersistableStore.cpp:22-44`) removes the destination at
`:38` before renaming the temp file over it at `:39`, because SdFat's rename will not overwrite
(`PersistableStore.cpp:35-37`; `HalStorage::rename` is a thin wrapper, `lib/hal/HalStorage.cpp:94-96`).
Between those two lines neither file exists.

Nothing on the read side recovers from that window. `readDocFromFileChecked`
(`PersistableStore.cpp:46-61`) stats only `path` at `:47`; the string `".tmp"` appears in the file
exactly four times, all inside the writer (`:25`, `:30`, `:31`, `:39`). `loadFromFile`
(`PersistableStore.h:157-177`) turns any non-`Ok` read into `return false` at `:164-165`, so the
store keeps its in-memory defaults and the next `saveToFileAtomic()` truncates the surviving `.tmp`
at `PersistableStore.cpp:30` before renaming defaults into place. The complete copy of the user's
data is on the card and is then discarded.

Research §3 widens the exposed set from the four stores the issue names to **eight files**, and
finds one case worse than "falls back to defaults": `PubKeyRegistry::record`
(`src/study/PubKeyRegistry.cpp:19-42`) is read-modify-write and discards the read status at `:23`,
so the first download after the lost window writes a **one-entry** registry over the `.tmp` — a
silent truncation of every other registered publication.

The fix already exists in this repo, for four other files. `highlightLoadAction`
(`src/util/HighlightFileAction.h:34-47`) is a `constexpr` five-way decision over
`(DocReadStatus, tempExists, tempParsed)`, host-tested in
`test/highlight_file/HighlightFileActionTest.cpp`, and switched on by `HighlightFile.cpp:39`,
`BookmarkFile.cpp:54`, `TagPaletteFile.cpp:40` and `PassageFile.cpp:72`. **The `PersistableStore`
path never learned it.** This change wires it in; it invents nothing.

## Goal

An interrupted atomic write no longer loses the file. On the next load, a `path` that is absent
beside a `path.tmp` that parses is promoted into place and used; a `path.tmp` that does not parse is
deleted and the load reports "nothing there", exactly as today.

## Non-goals

- **A user-visible refusal or recovery message.** Issue #39 owns that surface. Nothing here edits
  `lib/I18n/translations/*.yaml` or adds a UI path (brief constraint). The recovery is a `LOG_INF`
  line only — see A-8.
- **Refactoring the four existing adopters onto the new shared helper.** See A-6.
- **Fixing `PubKeyRegistry::record` and `MigrationRunner::appendLedger` ignoring `Unreadable` /
  `ParseError`.** See A-9; it is a different trigger and gets its own issue.
- **Deleting a stale `.tmp` that sits beside a *present* primary.** See A-5.
- **Reporting recovery to callers** via a new `DocReadStatus` value. See A-8.
- **Changing the write sequence** in `writeDocToFileAtomic`. The remove-then-rename order is forced
  by SdFat and is what makes adoption possible at all; A-4 depends on it.
- **`/.berean/migration-report.json`.** Written at `MigrationRunner.cpp:137`, never read. Nothing to
  recover.
- **Host-testing the I/O.** See §Testing, "the option not taken".

---

## Assumptions, for the review to attack

| | Assumption | Decided in |
|---|---|---|
| **A-1** | Adoption goes in a **new, opt-in** `PersistableStoreBase::readDocFromFileAdopting`, **not** inside `readDocFromFileChecked` and **not** per store. | §Architecture |
| **A-2** | The load-side decision (`HighlightLoadAction` / `highlightLoadAction`) **moves** from `src/util/HighlightFileAction.h` into a new `lib/Serialization/TempAdoption.h`, renamed to `TempAdoptionAction` / `tempAdoptionAction`. The save-side half stays where it is. | §Where the decision lives |
| **A-3** | A new pure `adoptedReadStatus(primary, action)` maps the action back to a `DocReadStatus`, preserving `Unreadable` and `ParseError` rather than flattening them to `Missing`. This is the new host-tested unit. | §Control flow |
| **A-4** | A `.tmp` found beside a **Missing** primary is, by construction, either a completed write or unparseable — never a plausible-but-partial document. Promotion is therefore safe without any extra integrity check. | §Why promotion is safe |
| **A-5** | A `.tmp` is **never** consulted, promoted or deleted while the primary exists, even when the primary is unreadable. Inherited unchanged from `HighlightFileAction.h:23-25`. | §Control flow |
| **A-6** | The four existing adopters (`HighlightFile`, `BookmarkFile`, `TagPaletteFile`, `PassageFile`) keep their own switch and are **not** migrated onto the new helper. Their `.tmp` reads stay on the non-adopting `readDocFromFileChecked`. | §Call sites |
| **A-7** | `readDocFromFile` (the `bool` wrapper, `PersistableStore.h:63`, `.cpp:63-65`) loses its only caller and is **kept**, mirroring `saveToFile`'s stated rationale at `PersistableStore.h:124-125`. | §Call sites |
| **A-8** | Recovery is reported by `LOG_INF` only. No new `DocReadStatus` value, no return-channel change, no toast. | §Error handling |
| **A-9** | `PubKeyRegistry::record` and `MigrationRunner::appendLedger` keep ignoring a non-`Ok` read status. This change fixes their `.tmp` case and nothing else. | §Call sites |
| **A-10** | After a `.tmp` that fails to parse, `doc` is explicitly cleared before returning `Missing`, so a caller that ignores the status cannot read a half-parsed document. | §Error handling |
| **A-11** | The six load-side tests move to a new `test/temp_adoption/` suite; the three save-side tests stay in `test/highlight_file/`. The `add_subdirectory(temp_adoption)` line in `test/CMakeLists.txt` is **reported in the PR description, not committed** (`.claude/agents/data-dev.md`, "Shared files — report, do not edit"). | §Testing |

---

## Architecture

### A-1: why a new opt-in function, not the shared read and not per store

The issue asks whether adoption belongs in `readDocFromFileChecked` or per store. Both are rejected.

**Not inside `readDocFromFileChecked`,** for two reasons found in research §4:

1. **It is already used to read the `.tmp` itself.** `HighlightFile.cpp:36`, `BookmarkFile.cpp:49`
   and `TagPaletteFile.cpp:35` call it with `tmpPath.c_str()`. Adoption inside it would make those
   calls stat `<path>.tmp.tmp`, and — worse — would promote the `.tmp` before the caller's own
   `switch` had decided to, duplicating a rescue those three implement deliberately.
2. **It is currently a pure read, on paths the launcher takes.** `PubKeyRegistry::findBySymbol`
   (`:78`) and `MeetingWeekCache::load` (`:23`) call it read-only. Adoption renames a file, and both
   `Storage.exists` and `Storage.rename` take `storageMutex` inside `HalStorage`. Turning every read
   into a potential write is the kind of widening `PersistableStore.h:27-36` warns about.

**Not per store,** because `loadFromFile` (`PersistableStore.h:157-177`) is the only shared entry
point the four CRTP stores have, and the four *worst* cases (research §3, rows 5-8) are not CRTP
stores at all — they call `readDocFromFileChecked` directly. Per-store adoption would leave
`PubKeyRegistry` exposed, which is the file with the most to lose.

**So: a third function beside the existing two.** This is the exact shape the write side already
has — `saveToFile` plain, `saveToFileAtomic` safe, the plain one "kept so that reaching for it has
to be deliberate" (`PersistableStore.h:124-125`). The read side gains the same pairing:

| | Plain | Safe |
|---|---|---|
| Write | `writeDocToFile` (`.cpp:11-20`) | `writeDocToFileAtomic` (`.cpp:22-44`) |
| Read | `readDocFromFileChecked` (`.cpp:46-61`) | **`readDocFromFileAdopting`** (new) |

The name is the issue's own word: an orphaned `.tmp` is *adopted*.

### A-2: where the decision lives

`PersistableStore.cpp` is in `lib/`. `highlightLoadAction` is in `src/util/`. A `lib/ → src/`
include is architecturally backwards; the only two instances in the tree are
`lib/Epub/Epub/blocks/TextBlock.cpp:11` and
`lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:15`, both reaching for `src/fontIds.h` through
`../../../../` — a wart, not a pattern to copy.

Duplicating the five-way rule in `lib/` is worse still: two copies of one decision, and the repo's
whole reason for having these pure headers is that there is exactly one.

So the decision **moves** to `lib/Serialization/TempAdoption.h`, beside its two siblings of the same
shape: `DocReadStatus.h:17` (`classifyDocRead`) and `SaveBudget.h:26` (`fitsBudget`).
`HighlightFileAction.h:16-19` already says it is "the same shape as `classifyDocRead` in
`Serialization/DocReadStatus.h`" — the file's own comment says where it belongs.

It is **renamed** in the move. Three of its four callers are not highlights
(`BookmarkFile.cpp:54`, `TagPaletteFile.cpp:40`, `PassageFile.cpp:72`), so the name already misleads
today; carrying a wrong name into a neutral home would be strictly worse than the status quo. The
enumerator names do not change, so each call site's `switch` body is untouched — only the type name
on the `case` labels and the one call.

Cost, stated plainly so the review can price it: **four `src/` files change mechanically**
(one include, one call, five `case` labels each) for zero behavioural change. That churn is the
price of not duplicating the rule.

`src/util/HighlightFileAction.h` keeps only the save half (`:52-56`), which has a single user
(`HighlightFile.cpp`), and gains `#include <TempAdoption.h>`.

### Files touched

| File | Change |
|---|---|
| `lib/Serialization/TempAdoption.h` | **new** — `TempAdoptionAction`, `tempAdoptionAction` (moved), `adoptedReadStatus` (new) |
| `src/util/HighlightFileAction.h` | load half removed; includes `<TempAdoption.h>`; save half unchanged |
| `lib/Serialization/PersistableStore.h` | declare `readDocFromFileAdopting`; `loadFromFile` calls it |
| `lib/Serialization/PersistableStore.cpp` | define `readDocFromFileAdopting` |
| `src/util/HighlightFile.cpp`, `src/util/BookmarkFile.cpp`, `src/study/TagPaletteFile.cpp`, `src/study/PassageFile.cpp` | rename-only |
| `src/study/PubKeyRegistry.cpp` (`:23`, `:52`, `:78`), `src/study/MigrationRunner.cpp` (`:55`, `:74`), `src/network/MeetingWeekCache.cpp` (`:23`) | switch to the adopting read |
| `test/temp_adoption/` | **new** suite |
| `test/highlight_file/HighlightFileActionTest.cpp` | load tests removed |

---

## Data and control flow

### `TempAdoption.h`

```cpp
enum class TempAdoptionAction : uint8_t {
  UseLoaded,              // primary parsed -- use it
  ReportEmpty,            // genuinely nothing on disk
  PromoteTempAndUseIt,    // .tmp is the only surviving copy; rescue it now
  DeleteTempReportEmpty,  // .tmp exists but is unusable; discard it
  ReportFailed,           // primary bytes exist but could not be read/parsed
};

constexpr TempAdoptionAction tempAdoptionAction(DocReadStatus primary, bool tempExists, bool tempParsed);

// What an adopting read reports. ReportFailed keeps the primary's own status:
// DocReadStatus.h:6-7 requires callers to tell Missing (safe to overwrite) from
// Unreadable/ParseError (never overwrite), so this mapping must not flatten them.
constexpr DocReadStatus adoptedReadStatus(DocReadStatus primary, TempAdoptionAction action);
```

`tempAdoptionAction`'s body is `highlightLoadAction`'s, verbatim
(`src/util/HighlightFileAction.h:34-47`). `adoptedReadStatus` (A-3) is new:

| Action | Reported status | Why |
|---|---|---|
| `UseLoaded` | `Ok` | a usable document was produced |
| `PromoteTempAndUseIt` | `Ok` | a usable document was produced |
| `ReportEmpty` | `Missing` | nothing on disk; safe to overwrite |
| `DeleteTempReportEmpty` | `Missing` | primary absent, `.tmp` was garbage and is gone; nothing to lose |
| `ReportFailed` | `primary`, unchanged | `Unreadable` and `ParseError` must survive |

**The invariant this exists to enforce:** a non-`Missing` primary can never be reported as `Missing`.
Without it, a flattened `ParseError` would tell `PubKeyRegistry::record` "safe to overwrite" about a
file whose bytes are still on the card.

### `readDocFromFileAdopting`

```cpp
DocReadStatus PersistableStoreBase::readDocFromFileAdopting(const char* path, JsonDocument& doc) {
  const DocReadStatus primary = readDocFromFileChecked(path, doc);

  bool tempExists = false;
  bool tempParsed = false;
  std::string tmpPath;
  if (primary == DocReadStatus::Missing) {
    tmpPath = std::string(path) + ".tmp";
    tempExists = Storage.exists(tmpPath.c_str());
    if (tempExists) tempParsed = readDocFromFileChecked(tmpPath.c_str(), doc) == DocReadStatus::Ok;
  }

  const TempAdoptionAction action = tempAdoptionAction(primary, tempExists, tempParsed);
  switch (action) {
    case TempAdoptionAction::PromoteTempAndUseIt:
      // Promote first: the rename is what rescues the only surviving copy. The
      // primary path is Missing, so nothing here can be overwritten.
      if (!Storage.rename(tmpPath.c_str(), path)) LOG_ERR("PERSIST", "Failed to promote %s", tmpPath.c_str());
      LOG_INF("PERSIST", "Recovered %s from an interrupted write", path);
      break;
    case TempAdoptionAction::DeleteTempReportEmpty:
      doc.clear();                       // A-10
      Storage.remove(tmpPath.c_str());
      break;
    default:
      break;
  }
  return adoptedReadStatus(primary, action);
}
```

Promote-before-validate mirrors `HighlightFile.cpp:49-60` and its comment at `:50-53`, which all
four existing adopters repeat. `std::string` for the path concatenation mirrors
`PersistableStore.cpp:24-25`, on a path that already builds a 45 KB `String` — CLAUDE.md's
"no `std::string` in hot paths" is about the render loop, and this runs at boot and on activity
entry.

Note the read of the `.tmp` writes into the **caller's** `doc`. That is safe because the branch is
only reached when `primary == Missing`, and on `Missing` `readDocFromFileChecked` returns at `:47-48`
without touching `doc`.

### A-4: why promotion is safe without an integrity check

Promotion trusts the `.tmp` to be complete. The three ways a `.tmp` can sit beside a `Missing`
primary:

| How | State of the `.tmp` |
|---|---|
| Power lost between `PersistableStore.cpp:38` and `:39` | complete — `writeFile` at `:30` had already returned |
| A previous `rename` at `:39` returned false | complete, same reason |
| Power lost *during* `writeFile` at `:30`, with no primary yet (first-ever save) | **partial** |

Only the third is dangerous, and it is caught by the `tempParsed` gate: a truncated serialised
JSON object is missing its closing `}`, so `deserializeJson` fails and the action is
`DeleteTempReportEmpty`. A zero-byte `.tmp` reads as `Unreadable` (`PersistableStore.cpp:51-54`) and
lands in the same arm.

There is no window in which a `.tmp` is *partial and parseable*, because the primary still exists
throughout `writeFile` in every case except the first-ever save, and A-5 stops adoption whenever the
primary exists.

**Corollary:** the adopted document is already within the save budget, because every writer measures
before calling `writeDocToFileAtomic` (`PersistableStore.h:148-153` for the CRTP stores;
`PubKeyRegistry.cpp:36-39`, `MeetingWeekCache.cpp:50-53`, `TagPaletteFile.cpp:70-73` for the direct
callers). No re-check on the read side.

### Concurrency

`loadFromFile` holds `storeMutex` (`PersistableStore.h:161`), so a concurrent `saveToFileAtomic` on
the same store cannot interleave with adoption. The three direct callers have no such lock, and
already do not: `PubKeyRegistry::record` runs on the download task while `findBySymbol` runs on the
main task.

The one new interleaving this change introduces: `record()` is between `:38` and `:39` when
`findBySymbol()` adopts. `findBySymbol` renames `tmp → primary`; `record`'s own rename then fails
and it returns false. **The bytes on the card are `record`'s new content either way** — it is the
same file — so the outcome is a false failure report, not data loss. Acceptable, and strictly better
than today, where the same interleaving loses the registry entirely.

`readDocFromFileAdopting` acquires `storageMutex` up to four times (read, exists, read, rename)
where `readDocFromFileChecked` acquires it twice. Only the `Missing` branch pays the extra two, and
`Missing` is the cold path.

---

## Error handling

Following CLAUDE.md's protocol: `LOG_ERR` + fall through; no exceptions, no `abort()`.

| Condition | Behaviour |
|---|---|
| Primary parses | `Ok`. `.tmp` untouched (A-5). |
| Primary `Unreadable` / `ParseError` | that status, unchanged. `.tmp` untouched. Never overwritten. |
| Primary `Missing`, no `.tmp` | `Missing`. Unchanged from today. |
| Primary `Missing`, `.tmp` parses | rename, `LOG_INF`, `Ok`. |
| Primary `Missing`, `.tmp` parses, **rename fails** | `LOG_ERR`, still `Ok` — the document is in hand and the `.tmp` survives for the next boot to retry. |
| Primary `Missing`, `.tmp` unparseable | `doc.clear()`, `Storage.remove`, `Missing`. |
| `Storage.remove` of a bad `.tmp` fails | ignored, as at `HighlightFile.cpp:63` and `BookmarkFile.cpp:79`. The next write truncates it anyway (`PersistableStore.cpp:30`). |

**A-10, the clear.** ArduinoJson's `deserializeJson` may leave `doc` holding whatever it parsed
before the error. `PubKeyRegistry::record:23` and `MigrationRunner::appendLedger:74` both discard
the status and use `doc` immediately, so a half-parsed `.tmp` would become the base of the document
they write back. The explicit `doc.clear()` closes that. The implementer must confirm ArduinoJson
7.4.2's actual post-failure state rather than assume it; the clear is cheap and correct either way.

**A-8, the log line.** `LOG_INF("PERSIST", "Recovered %s from an interrupted write", path)`. It is
compiled out of `x4pro-gh_release` (`LOG_LEVEL=0`, `platformio.ini`), which is correct: it is a
diagnostic for the human tester, not a user-facing notice. A user-facing one is #39's.

**A-9, what stays broken.** `PubKeyRegistry::record:23` and `MigrationRunner::appendLedger:74` will
still read-modify-write over a primary that exists but is `Unreadable` or `ParseError`. That is a
different trigger — a corrupt file, not an interrupted write — and fixing it changes when a
download is allowed to record. Out of scope; **the PR description files it as a follow-up issue.**

---

## Testing strategy

### Host tests — the only automated coverage available

`PersistableStore.cpp` cannot be built on the host. `PersistableStore.h:3` includes `<Arduino.h>`
unconditionally, `test/stubs/` holds only `Logging.h`, `HalDisplay.h` and a `HalStorage.h` that
declares a bare `HalFile` with no `Storage` singleton and no `exists`/`rename`/`remove`
(`test/stubs/HalStorage.h:15-21`). `test/highlight_file/CMakeLists.txt:1-4`,
`test/bookmark_save_action/CMakeLists.txt:1-2` and
`test/highlight_file/HighlightFileActionTest.cpp:1-12` all state this.

**The option not taken:** `test/pagination/CMakeLists.txt:1-10` shows the repo *can* build real code
against `test/stubs` with a link-time fake. Extending that to `PersistableStore.cpp` needs an
`<Arduino.h>` stub (`String`, `Print`, FreeRTOS) plus an in-memory `HalStorage` fake — a new
cross-cutting mechanism, which `.claude/agents/data-dev.md` says to escalate rather than invent. The
surface already has a way to do this (three pure-decision headers, three suites), so that way is
used. The `Storage` call sequence stays device-verified only.

**TDD order.** `test/temp_adoption/TempAdoptionTest.cpp` is written first and fails to compile
because `adoptedReadStatus` does not exist — the red. Then `TempAdoption.h`, then green.

| # | Test | Asserts |
|---|---|---|
| 1 | `AdoptedStatusIsOkWhenTheLoadedDocIsUsable` | `UseLoaded` → `Ok` |
| 2 | `AdoptedStatusIsOkWhenTheTempIsPromoted` | `PromoteTempAndUseIt` → `Ok` — **the defect this issue is about** |
| 3 | `AdoptedStatusIsMissingWhenNothingIsOnDisk` | `ReportEmpty` → `Missing` |
| 4 | `AdoptedStatusIsMissingAfterDiscardingABadTemp` | `DeleteTempReportEmpty` → `Missing` |
| 5 | `AdoptedStatusPreservesUnreadable` | `(Unreadable, ReportFailed)` → `Unreadable`, not `Missing` |
| 6 | `AdoptedStatusPreservesParseError` | `(ParseError, ReportFailed)` → `ParseError`, not `Missing` |
| 7 | `ANonMissingPrimaryIsNeverReportedMissing` | exhaustive over all 4 × 2 × 2 inputs, composing `tempAdoptionAction` into `adoptedReadStatus` — the `DocReadStatus.h:6-7` contract |
| 8-13 | the six `HighlightLoadAction` cases from `test/highlight_file/HighlightFileActionTest.cpp:18-51`, moved and renamed | unchanged behaviour after A-2's move |

`test/highlight_file/` keeps its three `HighlightSaveAction` tests (`:52-63`) and drops the load
half. The two suites together must still be 9 + 7 = 16 tests, so nothing is lost in the move.

`test/temp_adoption/CMakeLists.txt` mirrors `test/save_budget/CMakeLists.txt` exactly — a single
source, `${REPO_ROOT}/lib/Serialization` on the include path, `crosspoint_test_common` and
`GTest::gtest_main`.

```
cmake -S test -B build/test
cmake --build build/test -j8
ctest --test-dir build/test --output-on-failure -j
```

### Build and format gates

`pio run` once after the last edit (`pio` is at `/Volumes/stein/.platformio/penv/bin/pio` and is not
on `PATH`; research §6), and `./bin/clang-format-fix` over the **whole tree** — `-g` misses
`lib/Serialization/TempAdoption.h` and `test/temp_adoption/` once they are committed
(CLAUDE.md, "Formatting"). The clang-format venv is at
`/Volumes/stein/Documents/development/personal/berean-os/.venv/bin`.

### What only the human tester can verify

The window has **never been hit on hardware** — the issue says so, and research §7 repeats it. The
issue's own suggestion is the test:

1. On a debug `x4pro` build, insert `vTaskDelay(pdMS_TO_TICKS(6000));` between
   `PersistableStore.cpp:38` and `:39`.
2. Change a setting, and pull power during the 6-second window.
3. Confirm on the card that `/.crosspoint/settings.json` is gone and `settings.json.tmp` is present
   and complete.
4. Reboot. **Expected:** the setting survives, serial shows
   `INF PERSIST Recovered /.crosspoint/settings.json from an interrupted write`, and the `.tmp` is
   gone.
5. Repeat with the `.tmp` hand-truncated: expected `Missing`, defaults, `.tmp` deleted.
6. `ESP.getFreeHeap()` before and after boot load, unchanged within noise — this change adds one
   short-lived `std::string` on the `Missing` branch only.

The delay is a scratch edit for the test and must not be committed.

---

## Shared-file report (for the PR description, not the commit)

`test/CMakeLists.txt` needs one line, after `add_subdirectory(save_budget)` (`:96`):

```cmake
add_subdirectory(temp_adoption)
```

Per `.claude/agents/data-dev.md`, the orchestrator applies it.
