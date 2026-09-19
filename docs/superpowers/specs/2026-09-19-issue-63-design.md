# Honouring the read status in the two read-modify-write paths

Design for issue #63, on `fix/63-honour-read-status` at `f185a131`.
Research: `docs/superpowers/research/2026-09-19-issue-63-research.md`.

Modelled on `docs/superpowers/specs/2026-09-17-issue-51-design.md` — the spec for
`6156de32` (PR #64), which touched these same two files and explicitly left this
defect to #63. Same shape: a pure decision predicate in `lib/Serialization/`,
host-tested beside its siblings; mechanical edits at the call sites; a
device-only verification recipe.

---

## What changed after the pass-0 review, and why

`docs/superpowers/reviews/issue-63-spec-review-0.md` returned **CLEAR** — no
BLOCKERs, no MAJORs, all twelve assumptions standing — with six MINORs. All six
are applied here:

| Finding | Change |
|---|---|
| A-9 showed only one of `appendLedger`'s two call sites | §A-9 now instructs `MigrationRunner.cpp:204` (the `LoadResult::Empty` arm) explicitly, not just `:335` |
| "`break` bounds the damage to the one file already saved" overstated | §A-9 now says *per run, not per card*, and prices the persistent-failure case against `PassageDoc::add`'s lack of dedup |
| The decision table's `Missing` + unusable-`.tmp` row routes back into Goal 2's hazard, unacknowledged | §Decision table now states why that residue is accepted |
| Pseudocode dropped the `MigrationRunner::` qualifier on `LEDGER_PATH` | restored in both snippets; it would not have compiled as written |
| Non-goal 5 named `readDocFromFileChecked` as the function whose clear is confined | corrected to `readDocFromFileAdopting`, with `PersistableStore.cpp:95` |
| `ExactlyTheTwoSafeStatusesAllowAWrite` was tautological | dropped; §A-1's fifth-enumerator property is now a `static_assert` block instead, and the expected test count corrected to five |

The review also verified every `file:line` citation in pass 0 and found none
wrong, re-ran the 593-test baseline, and confirmed `mayOverwriteAfterRead` exists
nowhere in the tree — so neither "this already exists" nor "this is already
tested" bites.

Two things it corrected in the spec's *favour*, carried into §Error handling and
§Shared-file report respectively: the refusal `LOG_ERR`s do reach the release
build (`platformio.ini:187-188` sets `-DENABLE_SERIAL_LOG -DLOG_LEVEL=1`;
CLAUDE.md's "`LOG_LEVEL=0`, no serial logging" is the stale claim), and the
`record` refusal also rescues an orphaned `pubkeys.json.tmp`, closing the
issue body's "together the two bugs turn a recoverable interruption into total
loss of the registry" scenario.

---

## Problem

`readDocFromFileAdopting` returns a `DocReadStatus` so a caller can tell
`Missing` (safe to overwrite) from `Unreadable` / `ParseError` (never
overwrite). `DocReadStatus.h:5-7` states that as a requirement on callers that
own user data. Twelve call sites exist outside `PersistableStore`; ten obey it;
the two that do not are exactly the two that then write the file back.

`PubKeyRegistry::record` (`src/study/PubKeyRegistry.cpp:19-43`):

```cpp
  PersistableStoreBase::readDocFromFileAdopting(PATH, doc);    // :23 status discarded
  const int version = doc["v"] | 0;                            // :24 → 0 on a failed read
  if (version > FORMAT_VERSION) { ... }                        // :25 → 0 passes
  const auto entry = doc["p"][bookPath].to<JsonObject>();      // :31
  return PersistableStoreBase::writeDocToFileAtomic(PATH, doc);// :42
```

A corrupt or unreadable `pubkeys.json` therefore becomes a **one-entry**
registry, written atomically, with no torn file to notice. Every other
registered publication loses its symbol, falls back to a path-derived key, and
orphans its tags the moment the file moves — which is the whole reason the
registry exists (`src/study/PubKeyRegistry.h:10-16`).

`MigrationRunner::appendLedger` (`src/study/MigrationRunner.cpp:72-83`) is the
same shape against `/.berean/migration-ledger.json`, and research §4 settles what
that costs: `PassageDoc::add`
(`lib/StudyStore/StudyStore/PassageDoc.cpp`) is a plain `push_back` with a
budget pop-back and no identity check, so a lost ledger re-migrates and
**duplicates every passage in the file**. This is duplicate user data, not
redundant work.

Research §4 also found the larger half of that hazard: `readLedger`
(`MigrationRunner.cpp:52-63`) *does* check the status, but returns an empty
`done` list for `Unreadable` and `ParseError` alike. So a corrupt ledger already
causes the duplication on every boot, regardless of what `appendLedger` does.

Two further facts shape the design:

- **`doc` is not guaranteed empty after a failed read** (research §6).
  `readDocFromFileChecked` returns `ParseError` without clearing
  (`lib/Serialization/PersistableStore.cpp:55-59`), and
  `readDocFromFileAdopting` clears only on the `DeleteTempReportEmpty` arm
  (`:89-101`), deliberately. So `record` can today merge its entry onto *half* a
  registry. Any fix must refuse **before** touching `doc`.
- **A refusal is currently unobservable** (research §3). `record`'s `bool` is
  discarded at `src/network/PublicationDownloader.cpp:184` and `:239`;
  `appendLedger`'s is discarded at `MigrationRunner.cpp:204` and `:335`, with
  `ledger.push_back(name)` unconditional immediately after, and `allOk`
  (`:161`, returned `:341`) never set false by a ledger write failure.

Second, related gap the issue folds in: `appendLedger` has no serialised-byte
budget gate, against CLAUDE.md storage rule 2 (`CLAUDE.md:289-290`) and
`.claude/agents/data-dev.md:41`, while `PubKeyRegistry.cpp:36` gates the
identical shape. Research §5 measures the exposure: ≤200 rows
(`MigrationRunner.cpp:45`, `Storage.listFiles(LEGACY_DIR, 200)`) × ≤146 bytes
(the SDK reads names into `char name[128]`,
`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp`) ≈ **29.2 KB**
against a 45,000-byte budget (`lib/Serialization/SaveBudget.h:23`). So this half
is rule compliance and insurance, **not** a reachable overflow, and the spec
does not claim otherwise.

## Goal

1. Neither read-modify-write path writes a file it could not read. `Missing`
   remains "start a new document"; `Unreadable` and `ParseError` refuse and
   `LOG_ERR`.
2. A corrupt migration ledger stops the migration instead of silently
   re-running it and duplicating the user's passages.
3. `appendLedger` gates on the save budget, like every other store this project
   writes.
4. The ledger carries a format version a future build refuses rather than
   reinterprets (CLAUDE.md:292, `data-dev.md:52`).
5. All of it host-tested where the logic is pure, with **no edit to any shared
   file** (§Shared-file report).

## Non-goals

- **The user-visible surface for a refusal — #39.** No `lib/I18n/translations/`
  edits, no UI, no new toast. A refusal is `LOG_ERR` and a `false` return.
- **`src/network/PublicationDownloader.cpp`.** net-dev's surface, and A-6
  argues no change is wanted there anyway.
- **Deduplicating `PassageDoc::add`.** Research §4 establishes the duplication;
  fixing it is a different change to `lib/StudyStore/`, and #63 does not ask for
  it. Goal 2 removes the trigger this issue owns.
- **`writeReport`'s truncation** (`MigrationRunner.cpp:117-122`). A deliberate,
  documented choice for a diagnostic file nothing reads back; the issue says
  explicitly to leave it alone.
- **`readDocFromFileAdopting` clearing `doc` on its `ReportFailed` arm.** #51's
  spec (A-10) confined the `doc.clear()` to `DeleteTempReportEmpty`
  (`lib/Serialization/PersistableStore.cpp:95`, with the reason at `:89-94`) on
  purpose; `readDocFromFileChecked` never clears on any path
  (`PersistableStore.cpp:46-61`). Refusing before touching `doc` (A-2) makes the
  clear unnecessary here; widening it is a change to `lib/Serialization`
  semantics that ten other callers would inherit.
- **Pruning or repairing a corrupt `/.berean/` file.** Refusing is recoverable;
  deleting the user's bytes to unblock ourselves is not.

---

## Assumptions, for the review to attack

| | Assumption | Decided in |
|---|---|---|
| **A-1** | The rule goes in **`lib/Serialization/DocReadStatus.h`** as a new `constexpr bool mayOverwriteAfterRead(DocReadStatus)`, not a new header and not duplicated at the two call sites. | §A-1 |
| **A-2** | Both sites refuse **before** reading or modifying `doc`, rather than relying on `doc` being empty after a failed read. | §Control flow |
| **A-3** | `readLedger` returns `std::optional<std::vector<std::string>>`; `std::nullopt` means "could not be read", distinct from an empty ledger. | §A-3 |
| **A-4** | `runIfPending` **refuses the whole run** when the ledger is unreadable, mirroring the tag-palette refusal at `MigrationRunner.cpp:166-169`. | §A-4 |
| **A-5** | `pending()` returns **true** on an unreadable ledger, so `runIfPending` is entered, logs, and refuses — rather than failing silently. | §A-5 |
| **A-6** | `PubKeyRegistry::record`'s new refusal is `LOG_ERR` + `return false`, and **`PublicationDownloader` is not changed**. A verified download is never discarded because an unrelated index file is corrupt. | §A-6 |
| **A-7** | The ledger gains `"v": 1` and a `> FORMAT_VERSION` refusal on read, mirroring `PubKeyRegistry.cpp:24-29,53,79`. Extends the issue; justified because a future-format ledger read as empty re-migrates and duplicates. | §A-7 |
| **A-8** | `appendLedger` gates on `persist::DEFAULT_SAVE_BUDGET`, copied verbatim in shape from `PubKeyRegistry.cpp:36-39`. No shrink exception (`BookmarkSaveAction.h:14-18`): the ledger only ever appends. | §A-8 |
| **A-9** | A failed `appendLedger` **stops the run** (`break`), unlike the per-file failures around it which `continue`. A ledger write failure is global, not per-file. | §A-9 |
| **A-10** | No new `#include <DocReadStatus.h>` in either `.cpp`. Both already use `DocReadStatus::Ok` through `PersistableStore.h:10`, and no `src/` file in the tree includes it directly. | §A-10 |
| **A-11** | Tests extend the existing `test/doc_read_status/` suite. **No new test directory, therefore no `test/CMakeLists.txt` line, therefore no shared-file edit anywhere in this change.** | §Testing |
| **A-12** | Research §5's ≤29.2 KB worst case is recorded as a code comment on the budget gate, not as a host test. A test would need a new ArduinoJson-linking suite and would cost the shared-file line A-11 avoids. | §Testing |

---

## Architecture

### A-1: the predicate lives in `DocReadStatus.h`

The rule is: *may a read-modify-write caller proceed to write, given this read
status?* `Ok` and `Missing` yes; `Unreadable` and `ParseError` no.

That is the caller-side half of the contract `DocReadStatus.h:5-7` already
states in prose, and its existing suite already asserts it informally —
`test/doc_read_status/DocReadStatusTest.cpp:21-27`, `OnlyMissingIsSafeToOverwrite`,
whose comment is the rule word for word. Putting the predicate anywhere else
would separate a rule from the contract and the test that already describe it.

It also matches the shape of every sibling in `lib/Serialization/`: a tiny,
`constexpr`, Arduino-free function in a header, host-tested in its own suite.
`fitsBudget` (`SaveBudget.h:26`) is one comparison; `classifyDocRead`
(`DocReadStatus.h:17-22`) is three. Smallness is the pattern here, not an
argument against it.

**Rejected: a new `lib/Serialization/RewriteGuard.h`.** It would hold one
two-term predicate over a type defined in the file next door, and would need a
new `test/` directory and therefore a `test/CMakeLists.txt` line — a shared
file this change otherwise never touches (A-11). #51's PR body records what that
line costs when it goes missing: 12 tests silently unrun with CI green.

**Rejected: `if (status != DocReadStatus::Ok && status != DocReadStatus::Missing)`
at each call site.** Two copies of one rule, in the two files least able to
afford getting it wrong, and untestable on the host because neither `.cpp`
builds there (research §7).

### A-3, A-4, A-5: the ledger is read once, and a bad read stops the run

Goal 2 needs more than `appendLedger` checking its own read, because research §4
showed the duplication is already reachable through `readLedger`. Three changes,
all inside `src/study/MigrationRunner.cpp`:

**A-3.** `readLedger` returns `std::optional<std::vector<std::string>>`. This
matches the repo's existing vocabulary for "this lookup may have no answer" —
`PubKeyRegistry::lookup` and `findBySymbol` (`PubKeyRegistry.h:23,28`) and
`BookPathIndex::resolve` (`src/study/BookPathIndex.cpp:21`) all return
`std::optional`. An empty vector keeps its current meaning: a ledger that exists
and records nothing.

**A-4.** `runIfPending` refuses when `readLedger` returns `nullopt`. The nearest
in-file precedent is two lines away and is the same reasoning:

```cpp
  const auto paletteLoad = TagPaletteFile::load(palette);            // :165
  if (paletteLoad == TagPaletteFile::LoadResult::Failed) {           // :166
    LOG_ERR(MODULE, "Tag palette unreadable; refusing to migrate over it");
    return false;                                                    // :168
  }
```

The ledger refusal goes immediately after the existing `readLedger()` call at
`:160`, before the palette load, preserving today's read order.

**A-5.** `pending()` (`:146-154`) returns `true` on `nullopt`. The alternative —
`false` — makes a corrupt ledger a silent no-op: no migration, no error, no
trace. CLAUDE.md's error protocol is "a failed fetch leaves the previous data in
place **and says so**". The cost is honest and small: on a card with legacy
highlights and a corrupt ledger, every boot paints the migration screen
(`src/main.cpp:474,484`) and then immediately logs the refusal and continues
booting. The user-visible half of that is #39's.

**Trade-off, stated for the review.** A-4 + A-5 mean a corrupt ledger blocks
migration until the user removes the file, forever. The alternative is today's
behaviour: silent re-migration duplicating every passage, compounding on every
boot. Blocking is recoverable; duplicating is not — the user cannot tell an
original passage from its duplicate.

### A-6: the downloader does not change

`record` gains a third refusal arm. It already has two
(`PubKeyRegistry.cpp:25-28` future format, `:36-39` over budget), both
`LOG_ERR` + `return false`, and both discarded by the two callers
(`PublicationDownloader.cpp:184,239`). The new arm is consistent with those.

Failing the download instead would be worse: at `:239` the file is downloaded,
MD5-verified (`PublicationDownloader.cpp:215-222`) and already renamed into
place, and at `:184` it was already on the card. Refusing the *download* because
an index file is corrupt destroys verified work to report an unrelated fault.

The real consequence of the refusal is that the publication stays unregistered,
so `findBySymbol` will not offer it as the week's meeting publication. That is
user-visible, and it is exactly the surface #39 owns. It is also strictly better
than today, where the refusal does not happen and the *other* publications lose
their registrations instead.

`src/network/` is net-dev's surface (`.claude/agents/data-dev.md`), so changing
it here would also cross the surface boundary this task is scoped to.

### A-7: the ledger gets a format version

CLAUDE.md storage rule 4 (`CLAUDE.md:292`) and `data-dev.md:52` require every
store this project writes to carry a version a future build refuses rather than
reinterprets. The ledger has none; `PubKeyRegistry` next door does
(`PubKeyRegistry.cpp:13,24-29,53,79`).

This is not decoration. Without it, a future ledger format read by this build
yields an empty `done` list, which re-migrates everything and duplicates
passages — the same failure A-3/A-4 exist to close, reached by a different door.

Compatibility is safe in both directions: a ledger written before this change
has no `"v"`, reads as `0`, and `0 > 1` is false, so it loads; a ledger written
by this change carries `"v": 1`, which older builds ignore because they never
read the key.

A future version is reported as `nullopt` from `readLedger` — "could not be
read" — so A-4 and A-5 carry it the rest of the way.

### A-8, A-9: the budget gate, and what a failed append means

**A-8.** The gate is `PubKeyRegistry.cpp:36-39` transplanted:

```cpp
  if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Migration ledger exceeds the save budget; not written");
    return false;
  }
```

`<SaveBudget.h>` is already included (`MigrationRunner.cpp:10`).

No shrink exception. `BookmarkSaveAction.h:14-18` needs one because a bookmark
file inherited from a budget-less build must be deletable back under the ceiling
or it freezes read-only; the ledger only ever appends rows, so the shrinking
case does not exist.

**A-9.** A failed `appendLedger` stops the run:

```cpp
    if (!appendLedger(name, report.written)) {
      LOG_ERR(MODULE, "Could not record %s as migrated; stopping", name.c_str());
      report.drops.push_back("ledger not updated");
      reports.push_back(report);
      allOk = false;
      break;
    }
    ledger.push_back(name);
```

**The `LoadResult::Empty` arm gets the identical guard.** `appendLedger` has two
call sites, not one: `MigrationRunner.cpp:204` (`appendLedger(name, 0)`, for a
legacy file that parsed but held nothing) and `:335`. Both discard the result
today; both stop the run on `false`, with the same five lines. `:204`'s `report`
carries no `written` count, so its drop reads the same
`report.drops.push_back("ledger not updated")`. Leaving `:204` alone would be
cheap in passages — an Empty source writes none, so re-processing duplicates
nothing — but it would leave the in-memory `ledger` diverging from disk, which
is the divergence A-9 exists to stop.

Every other failure arm in the loop `continue`s (`:190`, `:201`, `:233`,
`:324`) because each is specific to one source file. A ledger write failure is
not: all three of its causes — unreadable ledger, over budget, write failed —
are properties of the ledger or the card, so the next file will fail the same
way while still writing its passages, each one becoming a duplicate on the next
boot.

**What `break` does and does not bound.** It bounds the damage to one file *per
run*, not per card. A ledger that is persistently unwritable still re-migrates
that same first unrecorded file on every boot, and because `PassageDoc::add`
does not deduplicate, its passages accumulate until the passage file's own
`SAVE_BYTE_BUDGET` starts refusing them ("store full", `:307`). Both causes of a
persistent failure are close to unreachable — research §5 puts the ledger ~15 KB
under its budget, and a card too broken to write the ledger fails
`PassageFile::save` first at `:317-325`, which already `continue`s without
recording. It is still a strict improvement on today, where the run continues
and duplicates *every* file rather than one. Stated precisely because Goal 2 is
exactly about not duplicating passages, and an unqualified "bounds the damage"
would overstate it.

`writeReport` (`:340`) still runs after the `break`, so the report records what
happened, and `allOk = false` reaches `main.cpp:502-503`'s
`LOG_ERR("MAIN", "Migration incomplete; legacy store untouched")`.

### Files touched

| File | Change |
|---|---|
| `lib/Serialization/DocReadStatus.h` | **add** `mayOverwriteAfterRead` (A-1) |
| `src/study/PubKeyRegistry.cpp` | `:23` honour the status (A-2, A-6) |
| `src/study/MigrationRunner.cpp` | `readLedger` → `optional` + version guard (A-3, A-7); `pending()` (A-5); `runIfPending` ledger refusal (A-4) and `appendLedger` result checks (A-9); `appendLedger` honour the status, version, budget (A-2, A-7, A-8) |
| `src/study/MigrationRunner.h` | `runIfPending`'s contract comment (`:38-39`) gains the unreadable-ledger refusal |
| `test/doc_read_status/DocReadStatusTest.cpp` | **add** the `mayOverwriteAfterRead` cases (A-11) |

No shared file is touched: not `test/CMakeLists.txt`, not
`lib/I18n/translations/`, not `src/main.cpp`.

---

## Data and control flow

### `DocReadStatus.h`

```cpp
// Whether a read-modify-write caller may go on to write, given the status its
// read returned. Ok and Missing are both safe -- Missing legitimately means
// "start a new document". Unreadable and ParseError are not: the bytes are
// still on the card, and an atomic write replaces them cleanly, leaving
// nothing torn to notice.
constexpr bool mayOverwriteAfterRead(const DocReadStatus status) {
  return status == DocReadStatus::Ok || status == DocReadStatus::Missing;
}
```

The predicate is total over the enum and has no `default:` to go stale: a fifth
status added later is `false` — refuse — which is the safe direction.

**A-2: when it returns true, `doc` is trustworthy.** `Ok` means the primary or
a promoted `.tmp` parsed. `Missing` arrives either with `doc` never written to,
or explicitly cleared on the unparseable-`.tmp` arm
(`PersistableStore.cpp:95`). The partial-document hazard of research §6 lives
only behind `Unreadable` / `ParseError`, and both call sites now return before
reading `doc` at all.

### `PubKeyRegistry::record`

```cpp
  JsonDocument doc;
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(PATH, doc);
  if (!mayOverwriteAfterRead(status)) {
    LOG_ERR(MODULE, "Registry unreadable; refusing to overwrite it");
    return false;
  }
  const int version = doc["v"] | 0;
  ... unchanged from :25 onward
```

`:20`'s early `return false` for an empty path already establishes `false` as
this function's refusal channel, so no signature changes.

### `MigrationRunner`

```cpp
constexpr int LEDGER_FORMAT_VERSION = 1;   // beside MODULE/BEREAN_DIR, :27-29

std::optional<std::vector<std::string>> readLedger() {
  JsonDocument doc;
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);
  if (status == DocReadStatus::Missing) return std::vector<std::string>{};   // never migrated
  if (status != DocReadStatus::Ok) return std::nullopt;                      // bytes exist, unusable
  if ((doc["v"] | 0) > LEDGER_FORMAT_VERSION) return std::nullopt;           // A-7
  ... existing :58-61 loop
}
```

Note the deliberate split of today's single `!= Ok` check at `:55`: `Missing`
must stay "nothing migrated yet" (the first-boot case), while the other two
become `nullopt`.

```cpp
bool appendLedger(const std::string& name, const uint16_t passages) {
  JsonDocument doc;
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);
  if (!mayOverwriteAfterRead(status)) {
    LOG_ERR(MODULE, "Migration ledger unreadable; refusing to overwrite it");
    return false;
  }
  if ((doc["v"] | 0) > LEDGER_FORMAT_VERSION) {
    LOG_ERR(MODULE, "Refusing to rewrite a newer ledger format");
    return false;
  }
  doc["v"] = LEDGER_FORMAT_VERSION;
  ... existing :75-79 rows
  // Bounded: <=200 sources (:45) x <=146 bytes/row (the SDK reads names into
  // char name[128]) is ~29.2 KB, well under the budget. The gate is the rule,
  // not a reachable ceiling.
  if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Migration ledger exceeds the save budget; not written");
    return false;
  }
  ... existing :81-82
}
```

`runIfPending`, at `:160`:

```cpp
  auto ledgerRead = readLedger();
  if (!ledgerRead) {
    LOG_ERR(MODULE, "Migration ledger unreadable; refusing to migrate over it");
    return false;
  }
  auto ledger = std::move(*ledgerRead);
```

`pending()`, at `:149`:

```cpp
  const auto ledger = readLedger();
  if (!ledger) return true;                      // A-5: let runIfPending report it
  for (const auto& name : sources) {
    if (!ledgerContains(*ledger, name)) return true;
  }
```

`<optional>` joins the includes at `MigrationRunner.cpp:12-14`.

### Decision table

| Read status of the target file | `record` | `appendLedger` | `readLedger` |
|---|---|---|---|
| `Ok` | merge, budget, write | merge, budget, write | the recorded list |
| `Ok`, future `"v"` | refuse (`:25`, today) | refuse (A-7) | `nullopt` (A-7) |
| `Missing` (incl. unusable `.tmp`) | new document, write | new document, write | empty list |
| `Unreadable` | **refuse, `LOG_ERR`** | **refuse, `LOG_ERR`** | `nullopt` |
| `ParseError` | **refuse, `LOG_ERR`** | **refuse, `LOG_ERR`** | `nullopt` |

Bold cells are what this change adds. Everything else is today's behaviour.

**The `Missing` + unusable-`.tmp` row is a knowingly accepted residue of Goal 2.**
For `readLedger` it yields an empty list, so `pending()` is true and every source
re-migrates — the duplication Goal 2 closes, reached through a third door.
Accepted for three reasons: the `Missing` there is #51's deliberate semantics
(`TempAdoption.h:39-40` → `:53-55`, "nothing there"), not something this change
invents; reaching it needs a failed rename in `writeDocToFileAtomic`
(`PersistableStore.cpp:38-42`) followed by a *second* `appendLedger` in the same
run whose `.tmp` write dies partway, and A-9's `break` removes that second
append; and treating it as `nullopt` instead would make a first boot with any
stray `.tmp` refuse to migrate at all, which trades a rare duplication for a
common refusal.

### Concurrency

No new locking, and no new hazard. `PersistableStore.h:80-87` records the
constraint #51 introduced: `readDocFromFileAdopting` renames without a lock and
is safe only because every reader and writer of `/.berean/pubkeys.json` and
`migration-ledger.json` runs on the Arduino loop task. This change adds no read
and no write to either file — it only *declines* to write, on paths that already
ran there. `MigrationRunner` runs at boot from `src/main.cpp:502`, before any
activity exists.

### Memory

Nothing new on the heap. `readLedger`'s `std::optional<std::vector<...>>` wraps
the vector it already returned — one `bool` of engaged state, moved out at the
call site, not copied. The refusal paths return before the `JsonDocument` is
populated, so they allocate strictly less than today. No new local exceeds the
256-byte stack rule (CLAUDE.md, the resource protocol).

---

## Error handling

CLAUDE.md's protocol, case 1: `LOG_ERR` + return false. No exceptions, no
`abort()`, always log before an error return.

| Condition | Behaviour |
|---|---|
| `record`, registry `Unreadable`/`ParseError` | `LOG_ERR(MODULE, "Registry unreadable; refusing to overwrite it")`, `return false`. File untouched. Caller discards the `bool` (A-6). |
| `record`, registry `Missing` | unchanged: a fresh one-entry registry is correct. |
| `appendLedger`, ledger `Unreadable`/`ParseError` | `LOG_ERR`, `return false`. Caller stops the run (A-9). |
| `appendLedger`, future `"v"` | `LOG_ERR(MODULE, "Refusing to rewrite a newer ledger format")`, `return false`. |
| `appendLedger`, over budget | `LOG_ERR(MODULE, "Migration ledger exceeds the save budget; not written")`, `return false`. |
| `readLedger`, ledger `Unreadable`/`ParseError`/future `"v"` | `nullopt`. `readDocFromFileAdopting` has already logged the parse failure (`PersistableStore.cpp:57`); `runIfPending` adds the decision it took. |
| `runIfPending`, ledger unreadable | `LOG_ERR(MODULE, "Migration ledger unreadable; refusing to migrate over it")`, `return false` before any file is read. `main.cpp:503` logs "Migration incomplete". |
| `pending()`, ledger unreadable | `true`, so the refusal above is reached and logged (A-5). |

Log level: `LOG_ERR` throughout, which compiles in at every level
(`lib/Logging/Logging.h:45-46`). These are refusals to write user data — the one class
of event a field report must carry. No `LOG_INF` and no user-facing string:
that is #39's.

**No file is created, removed or renamed on any refusal path.** Every refusal
returns before `Storage.mkdir` (`PubKeyRegistry.cpp:41`,
`MigrationRunner.cpp:81`), so a refusal cannot even leave a directory behind.

---

## Testing strategy

### Host tests

The two `.cpp` files cannot be built on the host: both include
`<PersistableStore.h>`, whose `:3` is an unconditional `#include <Arduino.h>`,
and `test/stubs/HalStorage.h` has no `Storage` singleton — documented at
`src/util/HighlightFileAction.h:10-15`. So the automated coverage is the pure
predicate, exactly as for `tempAdoptionAction`, `fitsBudget` and
`classifyDocRead`.

**A-11: the cases go in `test/doc_read_status/DocReadStatusTest.cpp`**, beside
`OnlyMissingIsSafeToOverwrite` (`:21-27`), which already states the rule in
prose. No new directory, so no `test/CMakeLists.txt` line, so no shared file.

TDD order — the red comes first and is a **compile failure**, which is how
`fitsBudget` and `tempAdoptionAction` were built before it:

1. Add the new `TEST`s referencing `mayOverwriteAfterRead`. Run
   `cmake --build build/test --target DocReadStatusTest` → fails, undeclared
   identifier. That is the red.
2. Add the predicate to `DocReadStatus.h`. Rebuild → green.
3. `ctest --test-dir build/test --output-on-failure -j` → the full suite.

Cases, modelled on `TempAdoptionTest.cpp`'s exhaustive sweep (`:70-92`):

| Test | Asserts |
|---|---|
| `OkMayOverwrite` | `Ok` → true |
| `MissingMayOverwrite` | `Missing` → true, with the "start a new document" reason in the failure message |
| `UnreadableMayNotOverwrite` | `Unreadable` → false |
| `ParseErrorMayNotOverwrite` | `ParseError` → false |
| `AgreesWithClassifyDocRead` | composes `classifyDocRead(exists, empty, parseFailed)` over all 2×2×2 inputs and asserts only `!exists` and the clean read permit a write — ties the predicate to the classifier that feeds it |

**Deliberately not written: a sweep asserting the predicate equals
`status == Ok || status == Missing`.** That expression *is* the predicate, so
the test would assert `f(x) == f(x)` and pass through any edit that changed both
in lockstep. The four literal-table cases above already pin all four enumerators
exhaustively, and `AgreesWithClassifyDocRead` is the one new case that composes
two functions and can genuinely fail — which is what makes
`ANonMissingPrimaryIsNeverReportedMissing`
(`test/temp_adoption/TempAdoptionTest.cpp:70-92`) worth its lines.

The "a fifth status is `false`" safety property of §A-1 is guarded at
**compile time** instead, beside the predicate:

```cpp
static_assert(mayOverwriteAfterRead(DocReadStatus::Ok));
static_assert(mayOverwriteAfterRead(DocReadStatus::Missing));
static_assert(!mayOverwriteAfterRead(DocReadStatus::Unreadable));
static_assert(!mayOverwriteAfterRead(DocReadStatus::ParseError));
```

**Baseline to beat:** 593 tests passing (research §9). The suite grows by
**five** `TEST`s and must stay at 100%.

**A-12: the ≤29.2 KB ledger bound is a comment, not a test.** Asserting it would
need `measureJson`, so a new ArduinoJson-linking suite, so the
`test/CMakeLists.txt` line A-11 exists to avoid. The arithmetic is recorded in
research §5 and on the gate itself, and the gate makes the bound non-load-bearing.

### Build and format gates

- `pio run` once after the last edit. `pio` is **not** on `PATH`; it is at
  `/Volumes/stein/.platformio/penv/bin/pio` (research §9).
- `pio check --fail-on-defect low --fail-on-defect high`.
- `./bin/clang-format-fix` over the **whole tree**, not `-g`: this change adds
  lines to files Git already reports as modified, but CI runs the unsuffixed
  form (CLAUDE.md, "Formatting"). Needs `.venv/bin` on `PATH`
  (`clang-format 21.1.8`, research §9).
- `git diff --exit-code` after formatting.

### What only the human tester can verify

The SD-card I/O around the predicate is device-only. Four cases, all on a debug
`x4pro` build with serial attached:

1. **Corrupt registry, `ParseError`.** Write `{"v":1,"p":{` over
   `/.berean/pubkeys.json`, note its bytes, download a publication.
   **Expected:** the download succeeds, serial shows
   `ERR PUBKEYS Registry unreadable; refusing to overwrite it`, and the file is
   **byte-identical** afterwards — not a one-entry document.
2. **Empty registry, `Unreadable`.** Truncate `pubkeys.json` to zero bytes and
   repeat. **Expected:** the same refusal. (This is the arm `classifyDocRead`
   reaches through `Storage.readFile` returning empty,
   `PersistableStore.cpp:51-54`.)
3. **Absent registry, `Missing` — the regression guard.** Delete
   `pubkeys.json` *and* `pubkeys.json.tmp`, download a publication.
   **Expected:** a new registry with exactly one entry, no error. The fix must
   not turn first-boot into a refusal.
4. **Corrupt ledger.** With at least one un-migrated file in
   `/.crosspoint/highlights/`, write `{"done":[` over
   `/.berean/migration-ledger.json` and reboot. **Expected:** the migration
   screen paints, then serial shows
   `ERR MIGRATE Migration ledger unreadable; refusing to migrate over it` and
   `ERR MAIN Migration incomplete; legacy store untouched`; `/.berean/passages/`
   is unchanged; rebooting again repeats the refusal and still does not
   duplicate. Replace the ledger with `{"v":9,"done":[]}` for the A-7 arm:
   the same refusal.

`ESP.getFreeHeap()` before and after, unchanged within noise — the refusal paths
allocate strictly less than today.

---

## Shared-file report (for the PR description)

**None.** This change touches no file on `.claude/agents/data-dev.md:22-27`'s
list — not `test/CMakeLists.txt`, not `lib/I18n/translations/*.yaml`, not
`src/main.cpp`. A-1 and A-11 were chosen partly to keep it that way.

What the PR description carries instead:

1. **#39** owns the user-visible surface for a refusal. This change logs and
   returns `false`; nothing reaches the screen.
2. **The `record` refusal closes the issue body's compound scenario.** #63's text
   notes that with #51 unfixed, an interrupted write leaves `pubkeys.json`
   missing and `pubkeys.json.tmp` holding the real registry, and the next
   `record()` destroys both. #51 fixed the first half by adopting the `.tmp`;
   this change fixes the second, so a corrupt-or-unreadable primary no longer
   takes the surviving copy with it.
3. **Duplicate passages are still possible** by other routes, because
   `PassageDoc::add` (`lib/StudyStore/StudyStore/PassageDoc.cpp`) does not
   deduplicate. This change removes the trigger #63 owns — a lost or
   reinterpreted ledger — but does not make re-migration itself safe. Worth a
   follow-up issue against `lib/StudyStore/`; noted, not filed here.
4. **A-5's cost:** a card with legacy highlights and a corrupt ledger now paints
   the migration screen on every boot and refuses. Deliberate (§A-4/A-5), and
   the honest alternative to silently duplicating the user's passages.
