# Two read-modify-write paths discard the read status — investigation

Investigated 2026-09-19 on `f185a131` (`fix/63-honour-read-status`, identical to
`main` at the time of writing), for issue #63.

**Verdict: the issue is correct, both citations survive #51's rename, and the
mechanism the fix needs already exists and is already tested.** The two call
sites call `readDocFromFileAdopting`, which returns a `DocReadStatus` that
already keeps `Unreadable` and `ParseError` distinct from `Missing`; they
discard it. Nothing new has to be invented — the change is to read a value that
is already correct.

Three things the issue leaves open are settled below: `PassageDoc::add` does
**not** deduplicate (§4), the unbudgeted ledger cannot actually overflow the
budget (§5), and `appendLedger`'s return value is discarded at both of its call
sites, so a refusal would currently be invisible (§3).

---

## 1. The reported defect, confirmed line by line

`PubKeyRegistry::record` (`src/study/PubKeyRegistry.cpp:19-43`):

```cpp
  JsonDocument doc;
  PersistableStoreBase::readDocFromFileAdopting(PATH, doc);   // :23 — status discarded
  const int version = doc["v"] | 0;                           // :24 — 0 on a failed read
  if (version > FORMAT_VERSION) { ... }                       // :25 — 0 passes
  ...
  const auto entry = doc["p"][bookPath].to<JsonObject>();     // :31
  if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET) { ... }// :36 — budget IS checked
  Storage.mkdir(BEREAN_DIR);                                  // :41
  return PersistableStoreBase::writeDocToFileAtomic(PATH, doc);// :42
```

`MigrationRunner::appendLedger` (`src/study/MigrationRunner.cpp:72-83`):

```cpp
  JsonDocument doc;
  PersistableStoreBase::readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);  // :74 — discarded
  if (!doc["done"].is<JsonArray>()) doc["done"].to<JsonArray>();                     // :75
  const auto row = doc["done"].as<JsonArray>().add<JsonObject>();                    // :77
  Storage.mkdir(BEREAN_DIR);                                                         // :81
  return PersistableStoreBase::writeDocToFileAtomic(MigrationRunner::LEDGER_PATH, doc);// :82
```

Both are read-modify-write onto an atomic write. On `Unreadable` or
`ParseError` the document handed back is empty (or partially parsed — see §6),
so the write lands a one-entry registry / one-row ledger over a file whose bytes
were still on the card, cleanly and with nothing torn to notice.

**The call-site census in the issue is accurate.** `grep -rn
"readDocFromFileChecked\|readDocFromFileAdopting\|readDocFromFile(" src lib`,
excluding `lib/Serialization/PersistableStore.*`, returns 14 lines; two are
prose in `src/util/RecentBooksDoc.h:84` and `src/util/HighlightFileAction.h:8`,
leaving **exactly the 12 call sites the issue tabulates**. (The four CRTP stores
reach the same read through `PersistableStore.h:186`, inside the excluded file.)
The only two that discard the return value are `PubKeyRegistry.cpp:23` and
`MigrationRunner.cpp:74`. The pure reads beside them —
`PubKeyRegistry.cpp:52`, `:78`, `MigrationRunner.cpp:55`,
`MeetingWeekCache.cpp:23` — all compare `!= DocReadStatus::Ok`.

## 2. The status being discarded is already correct and already tested

`adoptedReadStatus` (`lib/Serialization/TempAdoption.h:48-59`) maps
`TempAdoptionAction::ReportFailed` back to the primary's own status, with the
reason stated on `:44-47`: `DocReadStatus.h:6-7` requires callers to tell
`Missing` (safe to overwrite) from `Unreadable`/`ParseError` (never overwrite).

`test/temp_adoption/TempAdoptionTest.cpp` covers this exhaustively — in
particular `ANonMissingPrimaryIsNeverReportedMissing` (`:70-92`) sweeps all
4 × 2 × 2 inputs and asserts a non-`Missing` primary is never reported
`Missing`.

So the three-way distinction these sites need is delivered to them today. This
is a "read the value" change, not a "compute the value" change.

## 3. What a refusal means for the caller — currently, nothing

Both callers of `PubKeyRegistry::record` discard its `bool`:

- `src/network/PublicationDownloader.cpp:184` (the already-on-card skip path)
- `src/network/PublicationDownloader.cpp:239` (after a verified download)

Both callers of `appendLedger` discard its `bool` too, and unconditionally
record the file as migrated in memory right afterwards:

```cpp
    appendLedger(name, 0);            // MigrationRunner.cpp:204
    ledger.push_back(name);           // :205
...
    appendLedger(name, report.written);// :335
    ledger.push_back(name);           // :336
```

`runIfPending`'s `allOk` (`:161`, returned at `:341`) is never set false by a
ledger write failure, so `main.cpp:502`'s `LOG_ERR("MAIN", "Migration
incomplete; ...")` does not fire for one either.

Consequence for the design phase: making these functions *return* false is not
by itself observable. Whether the refusal should propagate — and how far — is a
spec decision, and the user-visible half is #39's.

## 4. Re-migration **does** duplicate user data — the issue's open question, settled

`PassageDoc::add` (`lib/StudyStore/StudyStore/PassageDoc.cpp`) is a plain
append with a budget pop-back and no identity check:

```cpp
bool PassageDoc::add(TaggedPassage passage) {
  if (passage.tags.empty()) return false;
  ...
  passages_.push_back(std::move(passage));
  if (measureBytes() > SAVE_BYTE_BUDGET) { passages_.pop_back(); return false; }
  return true;
}
```

There is no dedup by address, snippet or reference anywhere on the path:
`runIfPending` loads the existing `PassageDoc` (`MigrationRunner.cpp:228`), adds
every planned passage (`:306`), and saves (`:317`). So a lost ledger means the
same legacy file is migrated twice and **every one of its passages appears
twice in the user's store**. This is duplicate user data, not redundant work.

Two follow-on observations the spec should take into account:

- **`readLedger` collapses `Failed` into empty too** (`MigrationRunner.cpp:52-63`):
  it checks the status, but returns an empty `done` list for `Unreadable` and
  `ParseError` alike. So with a corrupt ledger, `pending()` (`:146-154`) returns
  true and `runIfPending` re-migrates — *regardless of what `appendLedger` does*.
  Honouring the status in `appendLedger` alone stops the ledger being destroyed;
  it does not stop the duplication that a corrupt ledger already causes.
- The migration already has the right reflex elsewhere: an unreadable tag
  palette refuses the whole run (`:166-169`) and an unreadable destination
  passages file refuses that file (`:228-234`). The ledger is the odd one out.

## 5. The ledger's missing budget gate is a rule violation, not a reachable overflow

The issue is right that `appendLedger` has no `measureJson` gate while
`PubKeyRegistry.cpp:36` gates the identical shape, and right that `writeReport`
(`:119`) is a deliberate, documented exception that should be left alone.

But the ledger is hard-bounded, and the arithmetic clears the budget:

- Sources come from `Storage.listFiles(LEGACY_DIR, 200)` (`MigrationRunner.cpp:45`),
  and `HalStorage::listFiles`'s default/passed cap is honoured by the SDK loop
  (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp`,
  `for (... && count < maxFiles ...)`). **≤ 200 rows.**
- That same loop reads each name into `char name[128]`, so a filename is
  **≤ 127 bytes** regardless of `USE_UTF8_LONG_NAMES`.
- A row serialises as `{"f":"<name>","p":<n>}` — 13 bytes of structure, ≤ 127 of
  name, ≤ 5 of `uint16_t`, plus one separating comma ⇒ **≤ 146 bytes**. FAT
  filenames cannot contain `"` or `\`, and ArduinoJson emits UTF-8 raw, so no
  escape expansion applies.
- Worst case: `200 × 146 + len("{\"done\":[]}")` ≈ **29.2 KB**, against
  `persist::DEFAULT_SAVE_BUDGET` = 45,000 (`lib/Serialization/SaveBudget.h:23`)
  and the 50,000-byte read cap (`:19`).

So adding the gate is compliance with CLAUDE.md's storage rule 2 and cheap
insurance if either cap ever moves — it is not closing an observed data-loss
path. Worth saying plainly in the spec so the fix is not over-sold.

## 6. `doc` is not guaranteed empty after a failed read

`readDocFromFileChecked` (`lib/Serialization/PersistableStore.cpp:55-59`) calls
`deserializeJson(doc, json)` and returns `ParseError` on failure **without
clearing `doc`** — ArduinoJson leaves the partially parsed document behind.
`readDocFromFileAdopting` clears only on the `DeleteTempReportEmpty` arm
(`:95`), and the comment at `:90-94` says the omission on `ReportFailed` is
deliberate.

So on `ParseError` today, `record` may merge its entry onto *half* a registry
and write that. Any fix must refuse before touching `doc`, not rely on it being
empty.

## 7. Host-testability

`src/study/PubKeyRegistry.cpp` and `src/study/MigrationRunner.cpp` cannot be
built on the host: both include `<PersistableStore.h>`, whose `:3` is an
unconditional `#include <Arduino.h>`, and `test/stubs/HalStorage.h` is a bare
`HalFile` with no `Storage` singleton. This is documented in
`src/util/HighlightFileAction.h:10-15`.

The established workaround is a pure, Arduino-free decision header beside the
`.cpp`, host-tested in its own `test/` suite:

| Decision header | Suite |
| --- | --- |
| `src/util/HighlightFileAction.h` | `test/highlight_file/` |
| `src/util/BookmarkSaveAction.h` | `test/bookmark_save_action/` |
| `lib/Serialization/TempAdoption.h` | `test/temp_adoption/` |
| `lib/Serialization/SaveBudget.h` | `test/save_budget/` |
| `lib/Serialization/DocReadStatus.h` | `test/doc_read_status/` |

`test/bookmark_save_action/CMakeLists.txt` is the closest template for a new
`src/`-side decision suite (it adds `${REPO_ROOT}/src` and
`${REPO_ROOT}/lib/Serialization` to the include path and links
`crosspoint_test_common` + `GTest::gtest_main`); `test/temp_adoption/CMakeLists.txt`
is the `lib/`-only variant.

`test/CMakeLists.txt` is on data-dev's **report-do-not-edit** list
(`.claude/agents/data-dev.md`), and #51's PR body records that omitting the line
left 12 new tests silently unrun. Any new suite must ship the exact
`add_subdirectory(...)` line in the PR body.

## 8. The nearest existing example of this change

`6156de32` — *"fix: adopt an orphaned .tmp so an interrupted atomic write is
recoverable (#64)"*, which closed #51 and touched both of these files. Its shape
is the template: a pure decision header in `lib/Serialization/`, an exhaustive
host suite, a mechanical edit at each call site, the `test/CMakeLists.txt` line
reported rather than committed, and a device-only verification recipe in the PR
body. It also explicitly names #63 as out of its scope and states it changed
neither site — confirmed by `git show --stat 6156de32`, which shows
`PubKeyRegistry.cpp` +6/-6 and `MigrationRunner.cpp` +4/-4, i.e. the rename only.

In-file precedent for the refusal itself is `MigrationRunner.cpp:166-169` (tag
palette unreadable ⇒ refuse the run) and `:228-234` (destination passages
unreadable ⇒ refuse that file), both `LOG_ERR` + skip.

## 9. Environment, as measured in this worktree

```
$ uname -s                       Darwin (25.5.0)
$ cmake --version | head -1      cmake version 4.4.2
$ ctest --version | head -1      ctest version 4.4.2
$ c++ --version | head -1        Apple clang version 21.0.0 (clang-2100.0.123.102)
$ .venv/bin/clang-format --version
                                 clang-format version 21.1.8
$ /Volumes/stein/.platformio/penv/bin/pio --version
                                 PlatformIO Core, version 6.1.19
$ python3 --version              Python 3.14.7
$ git --version                  git version 2.54.0
```

`pio` is **not** on `PATH` in this worktree — it lives at
`/Volumes/stein/.platformio/penv/bin/pio`. `clang-format` comes from the
`.venv` symlink (`.venv -> /Volumes/stein/Documents/development/personal/berean-os/.venv`);
`./bin/clang-format-fix` needs `.venv/bin` prepended to `PATH`.

ArduinoJson is pinned identically on both sides: `platformio.ini:151`
(`bblanchon/ArduinoJson @ 7.4.2`) and `test/CMakeLists.txt:31` (`GIT_TAG v7.4.2`).
GoogleTest is `v1.17.0` (`test/CMakeLists.txt:17`).

**Baseline host suite, measured:**

```
$ cmake -S test -B build/test && cmake --build build/test -j8
$ ctest --test-dir build/test -j8
100% tests passed out of 593
Total Test time (real) =   0.53 sec
```

593 passing before any change. Not built for the device in this phase.

## 10. What is not in scope

- **The user-visible surface for a refusal** — issue #39. No edits to
  `lib/I18n/translations/` and no UI.
- **`writeReport`'s truncation** (`MigrationRunner.cpp:117-122`) — a deliberate,
  documented choice for a diagnostic file nothing reads back.
- **#51 itself** — landed in `6156de32`; nothing here reopens it.
- **Deduplicating `PassageDoc::add`** — §4 establishes the duplication exists,
  but fixing it is a different change to a different layer (`lib/StudyStore/`),
  not data-dev's `src/study/` storage shells, and is not what #63 asks for.
