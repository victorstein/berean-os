# Adopting an orphaned `.tmp` on the `PersistableStore` read path

**Date:** 2026-09-17 (revised after `reviews/issue-51-spec-review-0.md`)
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

## What changed in pass 1, and why

Pass 0 was returned `CLEAR` with 0 blockers, 3 majors and 8 minors. **All eleven are accepted and
applied**; I re-verified each against the code before accepting it, and every one held. None
reverses the design — A-1's decision, A-2's move and A-3's mapping all survive. What failed was the
supporting argument underneath three of them.

| Review finding | Change |
|---|---|
| **MAJOR 1** — A-4's corollary "every writer measures" is false: `MigrationRunner::appendLedger` (`:72-83`) has no budget gate, and the ledger is a file this change newly adopts. *(The review also named `writeReport`; that half is wrong — it gates at `MigrationRunner.cpp:117-121`, stopping rows at `DEFAULT_SAVE_BUDGET - 2048` and setting `truncated`. Corrected here, not carried forward.)* | Corollary replaced with the claim that is actually load-bearing (§Why promotion is safe). The missing gate is covered by #63. The second half — that the new delete arm would *remove* an over-cap orphan that survives today — is fixed by **A-12**. |
| **MAJOR 2** — A-1's second reason is spent by the design's own call-site list, and cites `storeMutex` for a `storageMutex` rule | A-1 reason #2 rewritten: the `.tmp.tmp` problem carries the decision alone, and the three direct callers now get a positive reason to adopt. Citation corrected to `.claude/agents/data-dev.md:51`. |
| **MAJOR 3** — the Concurrency section analyses an unreachable race; there is no download task | §Concurrency replaced with the measured task map and a single-task **invariant**. I found one more stale claim than the review did: there is no web server task either (see below). |
| **MINOR 1** — the `LOG_INF` is *not* compiled out of release | A-8 rewritten. `platformio.ini:188` is `-DLOG_LEVEL=1`; the line ships, deliberately. |
| **MINOR 2** — A-4's "three ways" table is self-contradicted by its own second row | Premise replaced with the `writeFile` remove-then-recreate mechanism. |
| **MINOR 3** — the shim include contradicts "one include per call site" | Shim dropped; the three non-highlight files move to `<TempAdoption.h>` outright. |
| **MINOR 4** — five stale signposts are not on the files-touched list | Added. |
| **MINOR 5** — six citation drifts | Fixed throughout. |
| **MINOR 6** — a transient `.tmp` read failure deletes the only surviving copy | **A-12**: the new function does not delete at all. |
| **MINOR 7** — A-10's rationale over-reaches | A-10 now says explicitly why the clear is confined to one arm, with the ArduinoJson citation the review supplied. |
| **MINOR 8** — the given `ctest` line cannot show red or green | §Testing says to add the `add_subdirectory` line locally, revert before committing, and what it costs CI until the orchestrator applies it. |

**One correction the review did not make.** MAJOR 3 says to credit the web server task as a second
writer of `settings.json`, citing `PersistableStore.h:29-30`. **There is no web server task.**
`grep -rn xTaskCreate src` returns exactly one hit — the render task at
`src/activities/ActivityManager.cpp:34` — and `handleClient()` is called from activity `loop()` on
the loop task (`src/activities/network/CrossPointWebServerActivity.cpp:366`,
`src/activities/network/CalibreConnectActivity.cpp:127`). `PersistableStore.h:29-30`'s "the web
server task saves settings while the main task can too" is stale in the same way CLAUDE.md's
`LOG_LEVEL=0` is. This strengthens MAJOR 3's invariant rather than weakening it, and the stale
comment joins the follow-up list.

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
`test/highlight_file/HighlightFileActionTest.cpp`, and switched on by `HighlightFile.cpp:40`,
`BookmarkFile.cpp:55`, `TagPaletteFile.cpp:39` and `PassageFile.cpp:72`. **The `PersistableStore`
path never learned it.** This change wires it in; it invents nothing.

## Goal

An interrupted atomic write no longer loses the file. On the next load, a `path` that is absent
beside a `path.tmp` that parses is promoted into place and used; a `path.tmp` that does not parse is
left alone (A-12) and the load reports "nothing there", exactly as today.

## Non-goals

- **A user-visible refusal or recovery message.** Issue #39 owns that surface. Nothing here edits
  `lib/I18n/translations/*.yaml` or adds a UI path (brief constraint). The recovery is a `LOG_INF`
  line only — see A-8.
- **Refactoring the four existing adopters onto the new shared helper.** See A-6.
- **Fixing `PubKeyRegistry::record` and `MigrationRunner::appendLedger` ignoring `Unreadable` /
  `ParseError`.** See A-9. A different trigger, and already filed as **#63**.
- **Deleting a `.tmp` at all, from the new function.** See A-12. A `.tmp` beside a *present*
  primary is never even looked at — see A-5.
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
| **A-9** | `PubKeyRegistry::record` and `MigrationRunner::appendLedger` keep ignoring a non-`Ok` read status. This change fixes their `.tmp` case and nothing else. Already filed as **#63**. | §Call sites |
| **A-10** | After a `.tmp` that fails to parse, `doc` is explicitly cleared before returning `Missing`, so a caller that ignores the status cannot read a half-parsed document. | §Error handling |
| **A-11** | The six load-side tests move to a new `test/temp_adoption/` suite; the three save-side tests stay in `test/highlight_file/`. The `add_subdirectory(temp_adoption)` line in `test/CMakeLists.txt` is **reported in the PR description, not committed** (`.claude/agents/data-dev.md:22-27`, "Shared files — report, do not edit"). | §Testing |
| **A-12** | **`readDocFromFileAdopting` never deletes the `.tmp`.** It diverges here from the four existing adopters, which do. New in pass 1. | §A-12 |

---

## Architecture

### A-1: why a new opt-in function, not the shared read and not per store

The issue asks whether adoption belongs in `readDocFromFileChecked` or per store. Both are rejected.

**Not inside `readDocFromFileChecked`,** for two reasons found in research §4:

1. **It is already used to read the `.tmp` itself.** `HighlightFile.cpp:36`, `BookmarkFile.cpp:49`
   and `TagPaletteFile.cpp:35` call it with `tmpPath.c_str()`. Adoption inside it would make those
   calls stat `<path>.tmp.tmp`, and — worse — would promote the `.tmp` before the caller's own
   `switch` had decided to, duplicating a rescue those three implement deliberately.
2. **Some callers must keep reading exactly the path they name.** The three `.tmp` readers above are
   the clear case, and they are enough on their own.

   Pass 0 argued this second point as "it is currently a pure read, on paths the launcher takes",
   and that argument was wrong twice over. The design widens those exact call sites anyway — the
   files-touched table below puts `PubKeyRegistry.cpp:23,52,78` and `MeetingWeekCache.cpp:23` on the
   adopting read — so the purity is spent either way, just explicitly. And the mutex half was a
   non-argument: `readDocFromFileChecked` already takes `storageMutex` twice per call
   (`Storage.exists` at `PersistableStore.cpp:47`, `Storage.readFile` at `:50`, each a
   `HAL_STORAGE_WRAPPED_CALL`, `lib/hal/HalStorage.cpp:92-96`); adoption adds two more on the cold
   branch and introduces no lock the read path did not already hold. The rule pass 0 reached for is
   `.claude/agents/data-dev.md:51` ("Never lock `storageMutex` on a read path the renderer sits
   behind"), not `PersistableStore.h:27-36`, which is about the store-level `std::mutex` at `:37`.
   §Concurrency shows none of these three callers is on the render task.

**Why the three direct callers *should* adopt**, stated positively: `readDocFromFileChecked` is the
only read path `PubKeyRegistry` and `MeetingWeekCache` have. Without adoption, `findBySymbol`
(`PubKeyRegistry.cpp:50`, reading at `:52`) and `lookup` (reading at `:78`) return `nullopt`, and
`MeetingWeekCache::load` (`:23`) returns `false`, while the data sits intact in the `.tmp`. Those are
the launcher's "is a Watchtower on this card?" question and the meeting week table — user-visible
loss, from a file that is on the card.

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
(`BookmarkFile.cpp:55`, `TagPaletteFile.cpp:39`, `PassageFile.cpp:72`), so the name already misleads
today; carrying a wrong name into a neutral home would be strictly worse than the status quo. The
enumerator names do not change, so each call site's `switch` body is untouched — only the type name
on the `case` labels and the one call.

Cost, stated plainly so the review can price it: **four `src/` files change mechanically**
(one include, one call, five `case` labels each) for zero behavioural change. That churn is the
price of not duplicating the rule.

`src/util/HighlightFileAction.h` keeps only the save half (`:52-56`). **No compatibility shim.**
After the move `highlightSaveAction` has exactly one user — `HighlightFile.cpp:76`; `BookmarkFile.cpp:98`
uses `bookmarkSaveAction` from `BookmarkSaveAction.h`, and `TagPaletteFile.cpp:70` and
`PassageFile.cpp:106` measure inline. So the includes land like this:

| File | Include change |
|---|---|
| `src/util/BookmarkFile.cpp:12` | `"HighlightFileAction.h"` → `<TempAdoption.h>` |
| `src/study/TagPaletteFile.cpp:11` | `"util/HighlightFileAction.h"` → `<TempAdoption.h>` |
| `src/study/PassageFile.cpp:8` | `"util/HighlightFileAction.h"` → `<TempAdoption.h>` |
| `src/util/HighlightFile.cpp:9` | keeps `"HighlightFileAction.h"` (for the save half), **adds** `<TempAdoption.h>` |

Exactly one include moves per call site, which is what A-2's naming argument wants: three files that
have nothing to do with highlights stop including a highlights header.

### Files touched

| File | Change |
|---|---|
| `lib/Serialization/TempAdoption.h` | **new** — `TempAdoptionAction`, `tempAdoptionAction` (moved), `adoptedReadStatus` (new) |
| `src/util/HighlightFileAction.h` | load half removed; save half unchanged. **No shim** — it does not include `<TempAdoption.h>` |
| `lib/Serialization/PersistableStore.h` | declare `readDocFromFileAdopting`; `loadFromFile` calls it |
| `lib/Serialization/PersistableStore.cpp` | define `readDocFromFileAdopting` |
| `src/util/HighlightFile.cpp`, `src/util/BookmarkFile.cpp`, `src/study/TagPaletteFile.cpp`, `src/study/PassageFile.cpp` | rename-only |
| `src/study/PubKeyRegistry.cpp` (`:23`, `:52`, `:78`), `src/study/MigrationRunner.cpp` (`:55`, `:74`), `src/network/MeetingWeekCache.cpp` (`:23`) | switch to the adopting read |
| `test/temp_adoption/` | **new** suite |
| `test/highlight_file/HighlightFileActionTest.cpp` | load tests removed; header comment (`:1-12`) no longer describes a `.tmp` decision it does not contain |
| `test/highlight_file/CMakeLists.txt:1-4` | comment names the load decision; reword to the save half |
| `src/util/HighlightFile.h:12` | the one signpost naming `util/HighlightFileAction.h` as the home of the **load rule**; repoint to `Serialization/TempAdoption.h`. `src/util/BookmarkSaveAction.h:9` and `test/bookmark_save_action/BookmarkSaveActionTest.cpp:5` also name that header but point at its *no-host-stub* reasoning, which stays there — leave both. `HighlightFile.h:33`'s `readDocFromFile` mention is not stale either (A-7 keeps the function) |

That edit is comment-only, but CLAUDE.md requires comments written for the merged state, so leaving
it is not an option: it would send the next reader to a file that no longer holds the rule. The
neighbouring signposts are deliberately **not** repointed — see the table.

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
      doc.clear();  // A-10. The .tmp is left on the card -- A-12.
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

### A-12: the new function does not delete the `.tmp`

The four existing adopters call `Storage.remove` on an unusable `.tmp`
(`HighlightFile.cpp:63`, `BookmarkFile.cpp:79`, `TagPaletteFile.cpp:57`, `PassageFile.cpp:93`).
`readDocFromFileAdopting` deliberately does not. **This is the one place the new function diverges
from the pattern it otherwise mirrors**, and it is new in pass 1. Three independent reasons, each
verified:

1. **The delete buys nothing.** `SDCardManager::writeFile` removes the destination before
   re-creating it (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:282-284`), and
   every writer of these eight files writes `<path>.tmp` through it (`PersistableStore.cpp:30`). The
   next save reclaims the bytes regardless.
2. **It can destroy a good file.** `SDCardManager::readFile` returns `""` when the card is not
   initialised (`:190-194`) and when the open fails (`:197-199`), indistinguishable from an empty
   file. Both give `classifyDocRead(true, true, false)` → `Unreadable` → `tempParsed == false` →
   the delete arm. A transient SD hiccup would remove the only surviving copy of `settings.json` or
   `wifi.json` — precisely the loss this issue exists to stop.
3. **It would silently remove an orphan that survives today.** An over-cap `.tmp` reads back
   truncated at `SDCardManager.cpp:202` and cannot parse, so it lands in the same arm. Nothing
   removes it today; this change should not start.

Cost of not deleting: one failed read on the cold branch of each subsequent boot, until a write
reclaims the file. That is the whole price.

The enum keeps `DeleteTempReportEmpty` because the four adopters still act on it. The new function
honours the "report empty" half and declines the "delete" half; the comment in the switch says so,
so the divergence is visible at the point it happens rather than only here.

### A-4: why promotion is safe without an integrity check

Promotion trusts the `.tmp` to be complete. The ways a `.tmp` can sit beside a `Missing` primary:

| How | State of the `.tmp` |
|---|---|
| Power lost between `PersistableStore.cpp:38` and `:39` | complete — `writeFile` at `:30` had already returned |
| A previous `rename` at `:39` returned false | complete, same reason |
| Power lost *during* `writeFile` at `:30`, first-ever save | **partial** |
| Power lost *during* `writeFile` at `:30`, rewriting a `.tmp` left by either row above | **partial** |

Pass 0 claimed there was no partial-and-parseable window "because the primary still exists
throughout `writeFile` in every case except the first-ever save". That was false: row 2 leaves the
primary `Missing` with a `.tmp` present, and the error table below deliberately produces that state
again, so row 4 exists. The conclusion survives on a better mechanism:

**`SDCardManager::writeFile` removes the destination before re-creating it**
(`SDCardManager.cpp:282-284`). An interrupted write therefore leaves a *prefix* of the serialised
document and never a stale tail. A prefix of a JSON object is missing its closing `}`, so
`deserializeJson` fails, `tempParsed` is false, and the action is `DeleteTempReportEmpty` — report
empty, leave the bytes alone (A-12). A zero-byte `.tmp` reads as `Unreadable`
(`PersistableStore.cpp:51-54`) and lands in the same arm.

So there is no window in which a `.tmp` is partial *and* parseable, for any of the four rows.

**On the budget.** Pass 0 claimed the adopted document is within the save budget "because every
writer measures". That is false: `MigrationRunner::appendLedger` (`:72-83`) has no `measureJson` and
no `persist::fitsBudget` between its read at `:74` and its `writeDocToFileAtomic` at `:82`, and the
ledger is a file this change newly adopts. (`MigrationRunner::writeReport` *does* gate — it stops
adding rows once `measureJson(doc)` passes `DEFAULT_SAVE_BUDGET - 2048` and declares
`doc["truncated"]`, `MigrationRunner.cpp:117-121` — so it is one exposed writer, not two.) The true and sufficient
statement is narrower: **a `.tmp` larger than `SDCardManager::readFile`'s 50,000-byte cap
(`SDCardManager.cpp:202`) reads back truncated and cannot parse, so it can never be promoted.** No
budget re-check is needed on the read side because an over-budget `.tmp` is unreachable from the
promote arm, not because every writer gates. `appendLedger`'s missing gate is the same class of defect
`lib/Serialization/SaveBudget.h:5-14` exists to prevent, and is covered by **#63**.

### Concurrency

Pass 0 analysed an interleaving between `PubKeyRegistry::record` "on the download task" and
`findBySymbol` "on the main task". **There is no download task.** The measured map:

| Task | Created at | Touches the eight files? |
|---|---|---|
| Arduino loop | — | **yes** — every reader and writer below |
| `ActivityManagerRender` | `src/activities/ActivityManager.cpp:34` | no — rendering only |
| `fi_input`, `audio_play`, `ble-conn` | `freeink-sdk/.../InputManager.cpp:179`, `AudioManager.cpp:313`, `BleKeyboardHost.cpp:421` | no |

`grep -rn xTaskCreate src` returns exactly one hit, the render task. Downloads are synchronous —
`src/activities/meetings/MeetingDownloadActivity.h:49-50`: *"The transfer blocks the loop task for
its whole duration"* — so `PubKeyRegistry::record`'s only callers
(`src/network/PublicationDownloader.cpp:184,239`) are loop-task. So are every reader:
`findBySymbol` from `src/activities/launcher/LauncherActivity.cpp:124` and
`src/network/MeetingLibrary.cpp:44`; `lookup` from `src/study/StudyStore.cpp:34`,
`src/study/MigrationRunner.cpp:223` and `src/activities/catalog/PublicationsActivity.cpp:59`. The
web server is loop-task too: `handleClient()` is called from activity `loop()`
(`src/activities/network/CrossPointWebServerActivity.cpp:366`,
`src/activities/network/CalibreConnectActivity.cpp:127`), so `PersistableStore.h:29-30`'s "the web
server task saves settings while the main task can too" is stale.

**The invariant, stated so it can be re-checked rather than re-derived:**

> Every reader and every writer of the eight files in scope runs on the Arduino loop task. The four
> CRTP stores are additionally serialised by `storeMutex` (`PersistableStore.h:144,161`), which
> covers them even if that stops being true. The three direct callers — `/.berean/pubkeys.json`,
> `/.berean/migration-ledger.json`, `/.berean/meeting-weeks.json` — have **no lock at all** and rely
> on the single-task property alone.

**What breaks it.** Moving the download to its own task — the natural fix for a transfer that blocks
the loop — opens a data-loss path that does not exist today. With the primary `Missing`, an adopting
read on task A can rename the `.tmp` that `writeFile` on task B is in the middle of producing, after
which B's own rename fails and its entry is lost. Today the same interleaving is harmless because
the reader only reads. **This change is what makes a read path mutate, so this invariant is its
responsibility.** Any PR that introduces a background task touching `/.berean/` must either give
these three files a mutex or revert them to `readDocFromFileChecked`.

`readDocFromFileAdopting` acquires `storageMutex` up to four times (read, exists, read, rename)
where `readDocFromFileChecked` acquires it twice (`PersistableStore.cpp:47,50`). Only the `Missing`
branch pays the extra two, and `Missing` is the cold path. It introduces no lock the read path did
not already hold — see A-1.
## Error handling

Following CLAUDE.md's protocol: `LOG_ERR` + fall through; no exceptions, no `abort()`.

| Condition | Behaviour |
|---|---|
| Primary parses | `Ok`. `.tmp` untouched (A-5). |
| Primary `Unreadable` / `ParseError` | that status, unchanged. `.tmp` untouched. Never overwritten. |
| Primary `Missing`, no `.tmp` | `Missing`. Unchanged from today. |
| Primary `Missing`, `.tmp` parses | rename, `LOG_INF`, `Ok`. |
| Primary `Missing`, `.tmp` parses, **rename fails** | `LOG_ERR`, still `Ok` — the document is in hand and the `.tmp` survives for the next boot to retry. |
| Primary `Missing`, `.tmp` unparseable | `doc.clear()`, `Missing`. The `.tmp` is **left on the card** — A-12. |

**A-10, the clear.** ArduinoJson's `deserializeJson` leaves `doc` holding whatever it parsed before
the error — confirmed in the pinned source, where `doDeserialize` calls `dst.clear()` up front and
then returns the error with the partial document intact
(`build/test/_deps/arduinojson-src/src/ArduinoJson/Deserialization/deserialize.hpp:44-56`, v7.4.2 per
`test/CMakeLists.txt:31`). `PubKeyRegistry::record:23` and `MigrationRunner::appendLedger:74` both
discard the status and use `doc` immediately, so a half-parsed `.tmp` would become the base of the
document they write back. The explicit `doc.clear()` closes that.

**The clear is confined to `DeleteTempReportEmpty` on purpose. Do not extend it to `ReportFailed`.**
The same partial-document argument is true of that arm, and "finishing the job" by clearing on every
non-`Ok` return would be strictly worse until A-9 lands: `record` would then write a one-entry
registry over a corrupt-but-present `pubkeys.json` instead of merging onto the entries that *did*
parse. The `ReportFailed` arm belongs to A-9's follow-up, which must fix the ignored status and the
partial document together or not at all.

**A-8, the log line.** `LOG_INF("PERSIST", "Recovered %s from an interrupted write", path)`, and it
**ships in release**. `platformio.ini:188` sets `-DLOG_LEVEL=1 ; Set log level to info for release
builds` alongside `-DENABLE_SERIAL_LOG` at `:187`, and `lib/Logging/Logging.h:51-52` compiles
`LOG_INF` in at `LOG_LEVEL >= 1`. That is wanted: a silent recovery from data loss is exactly the
event a field report should carry, it costs one `printf` on a cold path, and `LOG_DBG`
(`Logging.h:57`, `LOG_LEVEL >= 2`) would hide it. It is still not a user-facing notice — that is
#39's.

(CLAUDE.md's "`x4pro-gh_release` — production, `LOG_LEVEL=0`, no serial logging" is stale against
those same two lines. Pass 0 inherited the error from it. The doc fix joins the follow-up list.)

**A-9, what stays broken.** `PubKeyRegistry::record:23` and `MigrationRunner::appendLedger:74` will
still read-modify-write over a primary that exists but is `Unreadable` or `ParseError`. That is a
different trigger — a corrupt file, not an interrupted write — and fixing it changes when a
download is allowed to record. Out of scope, and **already filed as #63**; the PR description
references it rather than proposing it.

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
| 7-12 | the six `HighlightLoadAction` cases from `test/highlight_file/HighlightFileActionTest.cpp:18-51`, moved and renamed | unchanged behaviour after A-2's move |

Items 3 and 4 together carry A-12: `DeleteTempReportEmpty` still maps to `Missing`, and it is the
*caller* that declines the delete — the decision function is unchanged.

`test/highlight_file/` keeps its three `HighlightSaveAction` tests (`:52-66`) and drops the load
half. Counting, in `TEST` blocks: `HighlightFileActionTest` goes 9 → 3, `TempAdoptionTest` arrives at
12 (six moved, six new), so the suite gains 6 net and loses no coverage.

`test/temp_adoption/CMakeLists.txt` mirrors `test/save_budget/CMakeLists.txt` exactly — a single
source, `${REPO_ROOT}/lib/Serialization` on the include path, `crosspoint_test_common` and
`GTest::gtest_main`.

```
cmake -S test -B build/test
cmake --build build/test -j8
ctest --test-dir build/test --output-on-failure -j
```

**A-11 has a cost the implementer must not paper over.** `cmake -S test -B build/test` only
configures the directories `test/CMakeLists.txt` names, so until
`add_subdirectory(temp_adoption)` exists there, the command above cannot show the red *or* the
green. And CI does run the host suite — `.github/workflows/ci.yml:188-194` configures, builds and
runs `ctest`. So a merge before the orchestrator applies that line leaves CI green while the 12 new
tests never execute.

The procedure: **add the line locally to drive TDD, and revert it in the same step that commits the
suite.** Every commit stays honest about what it contains, and the PR description carries the line
as a blocking item, not a footnote.

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
5. Repeat with the `.tmp` hand-truncated: expected `Missing`, defaults, and the `.tmp` **still on
   the card** — A-12.
6. `ESP.getFreeHeap()` before and after boot load, unchanged within noise — this change adds one
   short-lived `std::string` on the `Missing` branch only.

The delay is a scratch edit for the test and must not be committed.

---

## Shared-file report (for the PR description, not the commit)

`test/CMakeLists.txt` needs one line, after `add_subdirectory(save_budget)` (`:96`):

```cmake
add_subdirectory(temp_adoption)
```

Per `.claude/agents/data-dev.md:22-27`, the orchestrator applies it. **This is blocking, not
cosmetic:** CI configures and runs the host suite (`.github/workflows/ci.yml:188-194`), so without
this line CI passes while the 12 new tests never run.

What the PR description carries besides the CMake line:

1. **#63** covers both halves of the `MigrationRunner` / `PubKeyRegistry` gap this work found: the
   read-status defect (`PubKeyRegistry.cpp:23`, `MigrationRunner.cpp:74` discarding the status and
   overwriting what they could not read) and `appendLedger`'s missing budget gate (`:72-83`). It
   also records that `writeReport` is **not** an instance and should be left alone. One reference;
   nothing further to file.
2. Two stale comments this work verified: CLAUDE.md's `LOG_LEVEL=0` claim for `x4pro-gh_release`
   (`platformio.ini:187-188` says otherwise) and `PersistableStore.h:29-30`'s "web server task"
   (there is none — `handleClient()` is loop-task).
