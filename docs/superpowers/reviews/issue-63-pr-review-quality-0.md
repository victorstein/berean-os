# PR #72 — stage-2 code quality review (pass 0)

Branch `fix/63-honour-read-status`. Scope: `lib/Serialization/DocReadStatus.h`,
`test/doc_read_status/DocReadStatusTest.cpp`, `src/study/PubKeyRegistry.cpp`,
`src/study/MigrationRunner.cpp`, `src/study/MigrationRunner.h`. Stage 1 settled
intent; this pass asks only whether the code is written the way this codebase is
already written.

**Gates run in this worktree**: `pio run` → SUCCESS (RAM 19.5%, flash 81.1%);
`ctest --test-dir build/test -j8` → 598/598 pass; `./bin/clang-format-fix` over
the whole tree → no diff.

No BLOCKERs, no MAJORs. Four MINORs, all fixable inline, three of them in
comments.

---

## MINOR 1 — the rationale comment on `mayOverwriteAfterRead` is factually wrong

`lib/Serialization/DocReadStatus.h:30-32`:

```
// A comparison rather than a switch on purpose: a status added later is false
// -- refuse -- which is the safe direction, and the asserts below force that
// choice to be made deliberately rather than inherited.
```

The first clause is true. The second is not, and it is the clause that justifies
the shape. I copied the header to a scratch file, added a fifth enumerator
`Corrupt` after `ParseError`, and compiled at `-std=c++2a -Wall -Wextra`: it
builds clean, all four `static_assert`s at `DocReadStatus.h:37-40` still pass,
and `mayOverwriteAfterRead(DocReadStatus::Corrupt)` silently returns `false`. The
asserts enumerate the four values that exist today; nothing about them reacts to
a fifth. They force no deliberation at all.

The "comparison rather than a switch" framing also answers a divergence that
isn't there: both siblings in `lib/Serialization/TempAdoption.h` — `tempAdoptionAction`
(`TempAdoption.h:31`) and `adoptedReadStatus` (`TempAdoption.h:49`) — are
`switch`es **with** a `default:` label, so they would not produce a `-Wswitch`
warning on a new enumerator either. There is no sibling shape being deliberately
departed from.

CLAUDE.md's Comments rule is that a comment earns its place on a non-obvious
*why*. A *why* that is wrong is worse than none: the next reader will trust it.
Drop the second sentence entirely (the fail-safe-default point in the first
clause stands on its own), or drop the paragraph.

## MINOR 2 — the `static_assert` block is a third copy of four facts already asserted twice

`lib/Serialization/DocReadStatus.h:37-40` assert exactly what
`test/doc_read_status/DocReadStatusTest.cpp:32-45` assert — `Ok` true,
`Missing` true, `Unreadable` false, `ParseError` false — and those four tests are
in turn fully subsumed by `AgreesWithClassifyDocRead`
(`DocReadStatusTest.cpp:47-59`), which walks all eight `classifyDocRead` inputs
and therefore reaches all four statuses. Four facts, three layers.

The usual argument for the assert layer — "the host suite does not run in CI" —
does not hold here: `.github/workflows/ci.yml:168-194` configures `test/`, builds
it and runs `ctest`, and `test-status` (`ci.yml:198-206`) gates on it.

The repo does use `static_assert`, but for a different job and in a different
form. Every non-generated instance carries a message and guards a binary-layout
invariant no host test can reach: `lib/EpdFont/SdCardFont.cpp:14-17`
(`"EpdGlyph must be 16 bytes to match .cpfont file layout"`),
`lib/EpdFont/SdCardFont.h:179`, `lib/InflateReader/InflateReader.cpp:11`. Four
message-less asserts over a two-comparison `constexpr` predicate are not that
pattern.

Pick one layer. Keeping the asserts and deleting the four literal `EXPECT`
tests (leaving `AgreesWithClassifyDocRead`, which is the one that tests
something the asserts do not — that the classifier and the predicate agree) is
the tighter outcome. Keeping the tests and deleting the asserts is equally
defensible. Keeping all three is not.

## MINOR 3 — the same rationale is now written at three sites, and its lead clause misdescribes the code

The "a ParseError leaves the partially parsed document behind" argument appears
in `lib/Serialization/DocReadStatus.h:24-28`, again at
`src/study/PubKeyRegistry.cpp:23-24`, and again at
`src/study/MigrationRunner.cpp:91-92` — the last two differing only in
"merging onto it" vs "appending to it". That is exactly the drift surface
CLAUDE.md's Comments section warns about: the predicate is the single place the
rule belongs, and it is already there.

Separately, both call-site comments open with "Refuse BEFORE reading doc" and sit
directly above the line that performs the read:

```cpp
// Refuse BEFORE reading doc: a ParseError leaves the partially parsed
// document behind, so appending to it would write back half a ledger.
const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);
```

"read" means the file read on the next line and "reading doc" means consuming
the `JsonDocument`, one line apart. Cut both call-site comments; the header
carries the reason and `mayOverwriteAfterRead(status)` reads as its own
explanation at the call site.

## MINOR 4 — one named format version and one literal, four lines of on-disk format apart

The PR adds `constexpr int LEDGER_FORMAT_VERSION = 1;`
(`src/study/MigrationRunner.cpp:32`), correctly mirroring `FORMAT_VERSION` in
`src/study/PubKeyRegistry.cpp:13` and `src/network/MeetingWeekCache.cpp:13`. In
the same translation unit, `writeReport` still stamps a bare `doc["v"] = 1;`
(`MigrationRunner.cpp:138`) for the report's own format. The literal is
pre-existing and the report has no version-gated reader
(`src/network/CrossPointWebServer.cpp:1865-1871` serves the bytes), so the risk
is nil — but the PR is what puts a named constant beside an unnamed one in the
same file. A one-line `REPORT_FORMAT_VERSION` next to it closes it; leaving it is
also acceptable.

---

## What was checked and found sound

These are the specific suspicions this pass was asked to press, all of which came
back clean. Recording them so they are not re-litigated.

- **`mayOverwriteAfterRead` does not duplicate anything.** `tempAdoptionAction`
  (`TempAdoption.h:29`) maps *primary status + temp state → action*, and
  `adoptedReadStatus` (`TempAdoption.h:48`) maps *action → reported status*.
  Neither answers "given this status, may I write?", and `classifyDocRead`
  (`DocReadStatus.h:17`) answers the opposite question. The rule existed only as
  prose in `DocReadStatus.h:5-7`; the PR turns prose into a callable, which is
  the right direction.
- **`std::optional` for `readLedger` is not a second way to do `LoadResult`.**
  The `LoadResult` + `switch (tempAdoptionAction(...))` shape
  (`TagPaletteFile.cpp:38-61`, and the same in `BookmarkFile.cpp:41`,
  `HighlightFile.cpp:29`, `PassageFile.cpp`) exists for *public* `load()` APIs
  that must report four outcomes including `RecoveredFromTemp`. `readLedger`
  (`MigrationRunner.cpp:62`) is a file-static helper in an anonymous namespace
  with exactly two outcomes, and `std::optional` read helpers are already the
  idiom in this same directory — `PubKeyRegistry::lookup` and
  `PubKeyRegistry::findBySymbol` (`PubKeyRegistry.cpp:56,80`),
  `BookPathIndex::resolve` (used at `MigrationRunner.cpp:225`). Adopting a
  four-value enum here would have been the inconsistency.
- **The budget gate mirrors the sibling idiom exactly.**
  `measureJson(doc) > persist::DEFAULT_SAVE_BUDGET` (`MigrationRunner.cpp:113`)
  is what `PubKeyRegistry.cpp:42`, `TagPaletteFile.cpp:69` and
  `MeetingWeekCache.cpp:50` all write. `persist::fitsBudget`
  (`SaveBudget.h:26`) is used only from the `PersistableStore` template
  (`PersistableStore.h:171`); no free-function store calls it. The PR followed
  the right one of the two.
- **Log wording matches its siblings phrase for phrase.** "Migration ledger
  exceeds the save budget; not written" (`MigrationRunner.cpp:114`) against
  "Registry exceeds the save budget; not written" (`PubKeyRegistry.cpp:43`) and
  "Tag palette exceeds the save budget; not written" (`TagPaletteFile.cpp:70`);
  "Refusing to rewrite a newer ledger format" (`:99`) against
  `PubKeyRegistry.cpp:32`; "Refusing to read a newer ledger format" (`:70`)
  against `MeetingWeekCache.cpp:25`; "Migration ledger unreadable; refusing to
  migrate over it" (`:203`) against the adjacent, pre-existing "Tag palette
  unreadable; refusing to migrate over it" (`:213`). Error shape is
  CLAUDE.md pattern 1 throughout — `LOG_ERR` then `return false`, always logged
  before the error return.
- **No read-modify-write site was left inconsistent.** Sweeping every
  `readDocFromFileAdopting` / `readDocFromFileChecked` caller outside
  `PersistableStore`, the only other read-modify-write is
  `MeetingWeekCache::record` (`MeetingWeekCache.cpp:59-68`), which opts out
  explicitly and correctly — it is a derived, re-fetchable cache, and says so in
  a comment. Nothing else needed the predicate and did not get it.
- **Naming tracks its neighbours.** `ledgerRead` for the optional at
  `MigrationRunner.cpp:201` mirrors `paletteLoad` ten lines below at `:211`.
  `LEDGER_FORMAT_VERSION` is `FORMAT_VERSION` prefixed because this file writes
  two documents, which is the reason to prefix.
- **The one comment making a numeric claim is verifiable, and verifies.**
  `MigrationRunner.cpp:109-112` claims 200 sources at ~146 bytes a row ≈ 29 KB:
  `legacySources` passes `200` (`MigrationRunner.cpp:48`), and
  `SDCardManager::listFiles` reads each name into `char name[128]`
  (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:175`).
  A `{"f":"<≤128>","p":65535},` row is ≤147 bytes; ×200 ≈ 29 KB, clear of the
  45,000 budget. This is the model for what the other three comments are not.
- **Resource protocol holds.** `std::move(*ledgerRead)` (`:206`) avoids copying
  the vector; `return done;` from a `std::optional<std::vector<std::string>>`
  (`:79`) moves under C++20 implicit move; no new heap allocation inside a loop;
  no bare `new`; no `std::function`; no new locals near the 256-byte stack rule.
- **No dead or commented-out code** in the diff, and every new branch is reached
  by a caller.

VERDICT: CLEAR
