# Adversarial review — `2026-09-19-issue-63-design.md`, pass 0

Reviewed against issue #63 (`gh issue view 63 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-19-issue-63-research.md`, and the tree at
`fix/63-honour-read-status` (`e86e5646`).

## What I verified, and what held

Every `file:line` citation in the spec was opened and checked. All of them are correct. That is
unusual and worth stating plainly, because the rest of this review is a short list of small things.

| Spec claim | Verified against | Result |
|---|---|---|
| `record` discards the status at `:23`; budget gated at `:36` | `src/study/PubKeyRegistry.cpp:23,36-39` | correct |
| `appendLedger` discards at `:74`, no budget gate, `:72-83` | `src/study/MigrationRunner.cpp:72-83` | correct |
| `readLedger` collapses `Unreadable`/`ParseError` into an empty list | `MigrationRunner.cpp:52-63` | correct |
| Refusal is unobservable at `:184`, `:239`, `:204`, `:335`; `allOk` at `:161`/`:341` | `PublicationDownloader.cpp:184,239` (grep), `MigrationRunner.cpp:204,335,161,341` | correct |
| Every other failure arm `continue`s at `:190,:201,:233,:324` | `MigrationRunner.cpp` | correct |
| Palette refusal precedent at `:166-169` | `MigrationRunner.cpp:166-169` | correct |
| `doc` not cleared on `ReportFailed`; clear confined to `DeleteTempReportEmpty` at `:95` | `lib/Serialization/PersistableStore.cpp:89-101`, `:95` | correct |
| `ParseError` arm and its `LOG_ERR` at `:55-59`, `:57`; empty arm at `:51-54` | `PersistableStore.cpp` | correct |
| Concurrency note at `PersistableStore.h:80-87`; CRTP read at `:186` | `PersistableStore.h` | correct |
| 12 call sites outside `PersistableStore`, 10 obey | `grep -rn "readDocFromFileChecked\|readDocFromFileAdopting\|readDocFromFile(" src lib` → 14 lines, 2 prose (`RecentBooksDoc.h:84`, `HighlightFileAction.h:8`) | correct |
| A-10: no `src/` file includes `DocReadStatus.h` directly | `grep -rn 'include.*DocReadStatus' src lib test` → only `PersistableStore.h:10`, `TempAdoption.h:5` | correct |
| A-11: `test/doc_read_status/` already registered, so no shared-file edit | `test/CMakeLists.txt:62` `add_subdirectory(doc_read_status)`; its `CMakeLists.txt` already adds `${REPO_ROOT}/lib` | correct |
| `OnlyMissingIsSafeToOverwrite` at `:21-27`; `ANonMissingPrimaryIsNeverReportedMissing` at `:70-92` | `test/doc_read_status/DocReadStatusTest.cpp`, `test/temp_adoption/TempAdoptionTest.cpp` | correct |
| ≤200 rows × ≤146 B ≈ 29.2 KB vs 45,000 | `HalStorage.h:18` (default 200), `SDCardManager.cpp:174-176` (`char name[128]`, `count < maxFiles`), `SaveBudget.h:23` | correct; the 13-byte row structure and the 11-byte envelope both re-derive |
| `PassageDoc::add` does not deduplicate | `lib/StudyStore/StudyStore/PassageDoc.cpp:22-38` — `push_back` + budget `pop_back`, no identity check | correct |
| `<SaveBudget.h>` already included at `:10`; `<memory>/<string>/<vector>` at `:12-14`; `MODULE`/`BEREAN_DIR` at `:27-29` | `MigrationRunner.cpp` | correct |
| Baseline 593 tests | `ctest --test-dir build/test -j8` → `100% tests passed out of 593` | correct |
| `LOG_ERR` compiles in at every level, `Logging.h:45-46` | `lib/Logging/Logging.h:44-48` | correct — and stronger than the spec says: `platformio.ini:187-188` gives `x4pro-gh_release` `-DENABLE_SERIAL_LOG -DLOG_LEVEL=1`, so the refusal reaches the release build and `getLastLogs()` too. (`AGENTS.md`'s "release has `LOG_LEVEL=0`, no serial logging" is the stale claim here, not the spec's.) |

I also checked the two things most likely to be wrong per `AGENTS.md`: **"this already exists"** —
`mayOverwriteAfterRead` does not exist anywhere in the tree; and **"this is already tested"** — the
five existing `DocReadStatus.*` tests (`ctest -N`) all test `classifyDocRead`, none test a
caller-side predicate. Both claims survive.

Three design checks the spec does not make, which I made and which come out in its favour:

- **A-2 is sound on the `Missing` arm.** `tempAdoptionAction(Missing, true, false)` →
  `DeleteTempReportEmpty` → `doc.clear()` (`PersistableStore.cpp:95`), and
  `adoptedReadStatus(..., DeleteTempReportEmpty)` → `Missing` (`TempAdoption.h:54-55`). So every
  path that reports `Missing` leaves `doc` empty, and `mayOverwriteAfterRead` returning true really
  does imply `doc` is trustworthy.
- **The `record` refusal also rescues an orphaned `.tmp`.** With a corrupt primary and a good
  `pubkeys.json.tmp`, `tempAdoptionAction` returns `ReportFailed` without consulting the `.tmp`
  (`TempAdoption.h:34-36`). Today `record` overwrites the primary *and* `writeDocToFileAtomic`
  clobbers the `.tmp`; refusing preserves both. That is the exact "together the two bugs turn a
  recoverable interruption into total loss" scenario from the issue body, and the fix closes it.
  Worth adding to the PR description as a second-order win.
- **A-9's `break` is reachable without stranding the run on a full card.** If the SD is unwritable,
  `PassageFile::save` (`MigrationRunner.cpp:317`) fails first and takes the `:320-325` `continue`,
  so `appendLedger` is never reached. The `break` only fires on a ledger-specific failure.

Findings below: **0 BLOCKER, 0 MAJOR, 6 MINOR.**

---

## MINOR 1 — A-9's instruction does not say what happens at the *other* `appendLedger` call site

**Claim.** §Problem (spec `:62-65`) names both discard sites: "`appendLedger`'s is discarded at
`MigrationRunner.cpp:204` and `:335`, with `ledger.push_back(name)` unconditional immediately
after." §Files touched says the change adds "`appendLedger` result checks (A-9)".

**Problem.** §A-9's code block shows only the `:335` shape (`appendLedger(name, report.written)`).
`:204` — `appendLedger(name, 0)` on the `HighlightFile::LoadResult::Empty` arm — is never shown or
named again. An implementer working from the code block will patch one of the two sites the spec
itself identified, and the spec cannot adjudicate which is intended.

**Evidence.**

```
src/study/MigrationRunner.cpp:203-208
    if (loadResult == HighlightFile::LoadResult::Empty) {
      appendLedger(name, 0);
      ledger.push_back(name);
      reports.push_back(report);
      continue;
    }
```

versus `:335-337`. Both discard; only the second appears in A-9.

Consequence of leaving `:204` unguarded is small — an Empty source writes no passages, so
re-processing it next boot duplicates nothing — but it leaves the in-memory `ledger` diverging from
disk, which is precisely the divergence A-9 exists to stop.

**Fix.** One sentence in §A-9: state that `:204` gets the same guard (its `report` has no `drops` or
`written`, so the arm is `LOG_ERR` + `report.drops.push_back("ledger not updated")` +
`reports.push_back(report)` + `allOk = false` + `break`), or state explicitly that it is left as-is
and why. Either is fine; silence is not.

---

## MINOR 2 — "`break` bounds the damage to the one file already saved" is true per run, not per card

**Claim.** §A-9, spec `:279`: "`break` bounds the damage to the one file already saved."

**Problem.** It bounds it to one file *per boot*. It does not bound the total. If `appendLedger`
fails for a persistent reason — an already-over-budget ledger, or a write that fails for this path
specifically — then every boot re-migrates the same first unrecorded file, and because
`PassageDoc::add` does not deduplicate, that file's passages are appended again each time. The
accumulation stops only when the passage file hits its own `SAVE_BYTE_BUDGET` and `add` starts
returning false ("store full").

**Evidence.** `MigrationRunner.cpp:228` loads the existing `PassageDoc`, `:306` adds every planned
passage, `:317` saves. `lib/StudyStore/StudyStore/PassageDoc.cpp:32-36` is `push_back` + budget
`pop_back`, no identity check. With the file absent from the ledger, `:172`'s
`if (ledgerContains(ledger, name)) continue;` does not skip it on the next boot.

This is still a strict improvement on today (today the run continues and duplicates *every* file),
and research §5's arithmetic makes the over-budget door unreachable, so this is a precision problem
in the rationale rather than a defect in the design. But Goal 2 is specifically about not
duplicating the user's passages, so an unqualified "bounds the damage" is the wrong sentence to
leave in a spec about this issue.

**Fix.** Rewrite the sentence as: `break` bounds the damage to one file *per run*; a persistently
unwritable ledger still re-migrates that file on every boot, and because `PassageDoc::add` does not
deduplicate, its passages accumulate until the passage budget refuses them. Note that research §5
makes the over-budget cause unreachable and `:320-325` makes the write-failure cause nearly so.
Optionally close the reachable half by refusing at entry when the ledger already measures over
budget, alongside A-4's `nullopt` refusal — `readLedger` has the document in hand there anyway.

---

## MINOR 3 — the decision table routes `Missing` + unusable `.tmp` straight back into Goal 2's hazard, without saying so

**Claim.** §Decision table, spec `:414`: "`Missing` (incl. unusable `.tmp`) | new document, write |
new document, write | **empty list**".

**Problem.** For `readLedger`, "empty list" means `pending()` returns true and `runIfPending`
re-migrates every source — the exact duplication Goal 2 exists to close, reached by a third door.
The table states the routing correctly but the surrounding prose never acknowledges that this row
is the hazard, or why it is accepted.

**Evidence.** `tempAdoptionAction(Missing, true, false)` → `DeleteTempReportEmpty`
(`TempAdoption.h:39-40`) → `adoptedReadStatus` → `Missing` (`TempAdoption.h:53-55`). The spec's
`readLedger` then returns `std::vector<std::string>{}`, and `MigrationRunner.cpp:172`'s
`ledgerContains` skip never fires.

Reachability is low and A-9 lowers it further: reaching "primary absent, `.tmp` present but
unparseable" needs a failed rename in `writeDocToFileAtomic` (`PersistableStore.cpp:37-42`, after
`Storage.remove(finalPath)`) followed by a second `appendLedger` in the same run whose `.tmp` write
dies partway — and A-9's `break` removes that second append. It is also inherited from #51's
deliberate semantics, not invented here.

**Fix.** One line under §A-3/§A-4 or beside the table: this row is accepted because #51's
`DeleteTempReportEmpty` arm deliberately reports "nothing there", A-9's `break` removes the
sequence that reaches it, and treating it as `nullopt` instead would make a first boot with any
stray `.tmp` refuse to migrate.

---

## MINOR 4 — the pseudocode drops the `MigrationRunner::` qualifier on `LEDGER_PATH`

**Claim.** §Data and control flow, spec `:348` and `:363`:
`readDocFromFileAdopting(LEDGER_PATH, doc)`.

**Problem.** `readLedger` and `appendLedger` live in the file-scope anonymous namespace
(`MigrationRunner.cpp:25-142`), while `LEDGER_PATH` is declared in `namespace MigrationRunner` in
the header (`MigrationRunner.h:47`). Unqualified lookup does not find it; the existing code
qualifies it at all three uses.

**Evidence.**

```
src/study/MigrationRunner.cpp:55   ... readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc) != DocReadStatus::Ok
src/study/MigrationRunner.cpp:74   ... readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);
src/study/MigrationRunner.cpp:82   return ... writeDocToFileAtomic(MigrationRunner::LEDGER_PATH, doc);
```

**Fix.** Restore the qualifier in both snippets. Cosmetic, but the spec's snippets are being read as
the implementation instruction and this one does not compile as written.

---

## MINOR 5 — Non-goal 5 names the wrong function

**Claim.** §Non-goals, spec `:105`: "**`readDocFromFileChecked` clearing `doc` on `ReportFailed`.**"

**Problem.** `readDocFromFileChecked` never clears `doc` on any path, and `ReportFailed` is a
`TempAdoptionAction`, not something `readDocFromFileChecked` knows about. The `doc.clear()` and the
comment that confines it live in `readDocFromFileAdopting`.

**Evidence.** `PersistableStore.cpp:46-60` is `readDocFromFileChecked` — no `clear()`.
`PersistableStore.cpp:89-101` is the `DeleteTempReportEmpty` case of `readDocFromFileAdopting`, with
`doc.clear()` at `:95` and the "Deliberately NOT extended to the ReportFailed arm" comment at
`:90-94`. The spec's own §Problem (`:56-59`) gets this right, so it is an inconsistency inside the
document.

**Fix.** Change the non-goal heading to `readDocFromFileAdopting` and cite `PersistableStore.cpp:95`.

---

## MINOR 6 — `ExactlyTheTwoSafeStatusesAllowAWrite` restates the implementation and cannot fail

**Claim.** §Testing, spec `:499`: the test "sweeps all four enumerators and asserts the predicate
agrees with `status == Ok || status == Missing` — the contract at `DocReadStatus.h:5-7`,
exhaustively".

**Problem.** The predicate as specified (§Data and control flow, spec `:310-312`) *is*
`status == DocReadStatus::Ok || status == DocReadStatus::Missing`. A test asserting agreement with
that expression asserts `f(x) == f(x)`: it passes for any edit that changes both in lockstep, and it
does not guard A-1's stated safety property either (a fifth enumerator would be `false` on both
sides, so the test stays green whether or not that was intended). The four preceding tests
(`OkMayOverwrite`, `MissingMayOverwrite`, `UnreadableMayNotOverwrite`, `ParseErrorMayNotOverwrite`)
already pin the literal table exhaustively, so this test adds a row to the count and nothing to the
coverage. Contrast the sibling it is modelled on — `ANonMissingPrimaryIsNeverReportedMissing`
(`test/temp_adoption/TempAdoptionTest.cpp:70-92`) — which composes two functions and can genuinely
fail.

`AgreesWithClassifyDocRead` is the one new case with real content, and it is correctly specified: of
the eight `classifyDocRead` inputs, the four with `exists == false` all yield `Missing`,
`(true,false,false)` yields `Ok`, and the remaining three yield `Unreadable`/`ParseError`
(`DocReadStatus.h:17-22`), so "only `!exists` and the clean read permit a write" is exactly right.

**Fix.** Drop `ExactlyTheTwoSafeStatusesAllowAWrite`, or turn it into something that can fail — e.g.
a `static_assert` table of the four literal pairs, so a future enumerator forces a compile-time
choice rather than silently inheriting `false`. Then restate §Testing's "the suite must grow by the
number of `TEST`s added" with the corrected count.

---

## Assumptions attacked, and why they stand

- **A-1** (predicate in `DocReadStatus.h`). Both rejected alternatives are argued from evidence that
  checks out: a new `RewriteGuard.h` would need a `test/CMakeLists.txt` line, and `test/CMakeLists.txt`
  is on `.claude/agents/data-dev.md:22-27`'s report-do-not-edit list. The header already carries the
  prose contract at `:5-7` and two `constexpr` siblings at `:17-22`. No objection.
- **A-2** (refuse before touching `doc`). Verified sound on every arm, including the one the spec
  relies on (`DeleteTempReportEmpty` → `doc.clear()` → reported `Missing`). No objection.
- **A-3** (`std::optional` return). Matches `PubKeyRegistry.h:23,28` and `BookPathIndex.cpp:21`.
  Only two callers exist (`MigrationRunner.cpp:149,160`), so the change is contained. No objection.
- **A-4 / A-5** (refuse the run; `pending()` returns true). The interaction is consistent:
  `pending()` short-circuits on `sources.empty()` at `:148` *before* `readLedger`, so a corrupt
  ledger on a card with no legacy highlights stays silent, and `runIfPending`'s own
  `if (sources.empty()) return true;` at `:158` agrees. The stated cost — a migration screen painted
  and abandoned on every boot (`main.cpp:474,484`, then `:502-503`) — is real, permanent until the
  user deletes the file, and invisible to the user until #39 lands. The spec surfaces it twice
  (§A-4/A-5 trade-off, §Shared-file report item 3) and argues it correctly against the alternative
  (silent unbounded duplication). Accepted as a made and disclosed decision, not escalated.
- **A-6** (downloader unchanged). Both call sites verified to discard today
  (`PublicationDownloader.cpp:184,239`), both existing refusal arms verified to be discarded the same
  way, and the argument that refusing a checksum-verified, already-renamed download over an unrelated
  index file is worse holds. `src/network/` is net-dev's surface. No objection.
- **A-7** (`"v": 1` on the ledger). Compatibility re-derived in both directions and correct.
  `docs/file-formats.md` covers only `book.bin` and `section.bin`, and `pubkeys.json`'s existing
  `"v"` is undocumented there too, so not updating it is consistent with the tree. No objection.
- **A-8** (budget gate, no shrink exception). `BookmarkSaveAction.h:14-18` needs its exception
  because a delete must be able to shrink an inherited over-budget file; the ledger only appends, so
  the case does not arise. The gate's `>` form matches `PubKeyRegistry.cpp:36` and is semantically
  identical to `fitsBudget` (`SaveBudget.h:26`). No objection.
- **A-9** (`break`, not `continue`). See MINOR 1 and MINOR 2 — the decision is right, the coverage
  and the bound are imprecise.
- **A-10** (no new include). Verified: `DocReadStatus.h` reaches both `.cpp` files through
  `PersistableStore.h:10`, and no `src/` file includes it directly. No objection.
- **A-11 / A-12** (tests in the existing suite; the 29.2 KB bound as a comment). `add_subdirectory(doc_read_status)`
  is already at `test/CMakeLists.txt:62` and the suite's own `CMakeLists.txt` already puts
  `${REPO_ROOT}/lib` on the include path, so the "no shared file" claim genuinely holds. The A-12
  argument — that asserting the bound needs `measureJson`, hence an ArduinoJson-linking suite, hence
  the line A-11 avoids — is correct. No objection.

## Verification recipe

The four device cases were traced through the code and all four produce the stated result: case 1
refuses before `Storage.mkdir` (`PubKeyRegistry.cpp:41`) so the file really is byte-identical; case 2
reaches `Unreadable` through `PersistableStore.cpp:51-54` as claimed; case 3 is the correct
first-boot regression guard; case 4's two arms (`{"done":[` → `ParseError`, `{"v":9,...}` → future
version) both land on `nullopt` and both reach `main.cpp:503`. The build and format gates name the
right paths — `pio` at `/Volumes/stein/.platformio/penv/bin/pio` and the unsuffixed
`./bin/clang-format-fix`, which is the form CI runs.

## Verdict rationale

Six MINORs, all fixable inline by the spec author: one missing instruction (MINOR 1), one
overstated bound (MINOR 2), one unacknowledged accepted hazard (MINOR 3), two citation/pseudocode
slips (MINOR 4, 5), and one zero-information test (MINOR 6). None reverses a decision, changes
scope, or needs a judgment only the human can make. The one genuinely load-bearing judgment —
A-5's permanent migration-screen-and-refuse on a corrupt ledger — is made, argued, and disclosed to
the PR description, which is the right place for it.

VERDICT: CLEAR
