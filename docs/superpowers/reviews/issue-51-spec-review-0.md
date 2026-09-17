# Adversarial review — `2026-09-17-issue-51-design.md` (pass 0)

**Reviewed:** `docs/superpowers/specs/2026-09-17-issue-51-design.md` @ `6dba3b66`
**Against:** issue #51 (`gh issue view 51`), `docs/superpowers/research/2026-09-17-issue-51-research.md`
**Branch state:** docs only — `git diff main --stat` is the two markdown files, 600 insertions, no
code. There is no prior implementation pass to check for a half-applied fix.

The core of this spec holds up. I re-derived the defect from the source rather than the issue:
`writeDocToFileAtomic` removes at `PersistableStore.cpp:38` and renames at `:39`;
`readDocFromFileChecked` opens with `Storage.exists(path)` at `:47` and never mentions the temp path;
`loadFromFile` turns any non-`Ok` into `return false` at `PersistableStore.h:164-165`. A-1's decisive
argument is true and I checked it directly — `HighlightFile.cpp:36`, `BookmarkFile.cpp:49` and
`TagPaletteFile.cpp:35` all pass `tmpPath.c_str()` to `readDocFromFileChecked`, so adoption inside
that function really would stat `<path>.tmp.tmp`. The files-touched list is complete: `grep -rn
'writeDocToFileAtomic|saveToFileAtomic' src lib` returns exactly the four CRTP stores, the three
`/.berean/` direct callers, the write-only migration report, and the four study files that already
adopt — nothing else writes atomically. Host-testability is as described (`test/stubs/HalStorage.h:15-21`
is a bare `HalFile` with no `Storage`), the CMake mirror works (`test/save_budget/CMakeLists.txt:5-7`
puts `lib/Serialization` on the include path, which is what `TempAdoption.h`'s `#include <DocReadStatus.h>`
needs), `add_subdirectory(save_budget)` really is `test/CMakeLists.txt:96`, and the baseline suite is
what the spec assumes:

```
$ ./build/test/highlight_file/HighlightFileActionTest
[==========] 9 tests from 2 test suites ran. [  PASSED  ] 9 tests.
```

Boot order is fine too — `Storage.begin()` at `main.cpp:402` precedes `SETTINGS.loadFromFile()` at
`:411`, so the adopting read has a mounted card.

The problems are all in the supporting arguments, and three of them are in the assumptions the spec
nominates as its real choices: A-4's corollary, A-1's second reason, and the Concurrency section's
model of which task runs what. None of them reverses the design; all are fixable in the text.

---

## MAJOR 1 — A-4's corollary is false: two writers in scope have no budget gate, and the new read path deletes their oversized `.tmp`

**Claim.** §"Why promotion is safe", the corollary (spec `:263-266`): *"the adopted document is
already within the save budget, because **every writer measures** before calling
`writeDocToFileAtomic` (`PersistableStore.h:148-153` for the CRTP stores; `PubKeyRegistry.cpp:36-39`,
`MeetingWeekCache.cpp:50-53`, `TagPaletteFile.cpp:70-73` for the direct callers). No re-check on the
read side."*

**Problem.** The enumeration omits the two writers that do not measure, and both are in this change's
own scope. `MigrationRunner::appendLedger` reads the ledger at `MigrationRunner.cpp:74`, appends a
row at `:77-79`, and calls `writeDocToFileAtomic` at `:82` — there is no `measureJson` and no
`persist::fitsBudget` anywhere between them. `MigrationRunner::writeReport` does the same at `:137`.
The spec's own files-touched table (`:157`) switches `MigrationRunner.cpp:55` and `:74` to the
adopting read, so the ledger is precisely a file the corollary is being applied to.

The consequence is not a promoted oversized document — it is the other arm. `SDCardManager::readFile`
truncates at 50,000 bytes (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202-208`)
and returns the truncated string, so an over-cap ledger `.tmp` parses as garbage, `tempParsed` is
false, and the new `DeleteTempReportEmpty` arm **removes it**. Today nothing removes it. So the
read side is safe by accident, not because "every writer measures", and the change quietly converts
"an unreadable orphan sits on the card" into "the orphan is gone".

**Evidence.**
- `src/study/MigrationRunner.cpp:72-83` — `appendLedger`: read at `:74`, `writeDocToFileAtomic` at
  `:82`, no budget call in between. Contrast `src/study/PubKeyRegistry.cpp:36-39`, which does gate.
- `src/study/MigrationRunner.cpp:137` — `writeReport`, same.
- `freeink-sdk/.../SDCardManager.cpp:202` — `constexpr size_t maxSize = 50000;` with no error return.
- Spec `:157` puts both ledger reads on the adopting path.

**Fix.** Replace the corollary with what is actually true and load-bearing: *a `.tmp` larger than
`SDCardManager`'s 50,000-byte cap reads back truncated and cannot parse, so it can never be promoted —
it is deleted instead. That is why no budget re-check is needed on the read side.* Then add
`MigrationRunner`'s missing budget gate to the follow-up issue A-9 already commits the PR description
to filing; it is the same class of defect `SaveBudget.h:5-14` exists to prevent.

---

## MAJOR 2 — A-1's second reason for rejecting the shared read is discarded by the design's own call-site list, and its citation is about the other mutex

**Claim.** §Architecture, A-1 (spec `:98-101`): *"**It is currently a pure read, on paths the launcher
takes.** `PubKeyRegistry::findBySymbol` (`:78`) and `MeetingWeekCache::load` (`:23`) call it
read-only. Adoption renames a file, and both `Storage.exists` and `Storage.rename` take
`storageMutex` inside `HalStorage`. Turning every read into a potential write is the kind of widening
`PersistableStore.h:27-36` warns about."*

**Problem.** Three separate faults, and together they leave A-1 resting on one leg.

1. **The design does the widening anyway, to those exact call sites.** The files-touched table
   (`:157`) switches `src/study/PubKeyRegistry.cpp:23,52,78` and `src/network/MeetingWeekCache.cpp:23`
   to `readDocFromFileAdopting`. Whatever "it is currently a pure read" is worth, this change spends
   it — it just spends it explicitly instead of implicitly. The only reason that survives is reason
   #1, the `.tmp.tmp` problem, which is sound.
2. **`PersistableStore.h:27-36` is about `storeMutex`, not `storageMutex`, and says the opposite.**
   That comment reads *"It is deliberately held across the SD write. That is safe only because the
   read path does NOT take it … If you ever lock this mutex on a read path, you put it on the render
   path"*. It is a rule about the store-level `std::mutex` at `PersistableStore.h:37`. The rule the
   spec wants exists, but it is `.claude/agents/data-dev.md:51`, *"Never lock `storageMutex` on a read
   path the renderer sits behind."*
3. **The `storageMutex` half is a non-argument.** `readDocFromFileChecked` already takes
   `storageMutex` twice on every call — `Storage.exists` at `PersistableStore.cpp:47` and
   `Storage.readFile` at `:50`, each a `HAL_STORAGE_WRAPPED_CALL` / `StorageLock`
   (`lib/hal/HalStorage.cpp:92-96`). Adoption adds two more acquisitions on the cold branch; it does
   not introduce a lock the read path did not already hold. The spec says this correctly itself at
   `:280-283`, which contradicts `:99-101`.

**Fix.** Keep the decision — an opt-in third function is right, and reason #1 alone carries it. Cut
reason #2 down to the part that is true (`readDocFromFileChecked` has callers that must stay a
literal read of the path they name, including the four `.tmp` readers) and then state positively why
`findBySymbol`, `lookup` and `MeetingWeekCache::load` *should* adopt: they are the only read paths
those two files have, and without adoption they return `nullopt`/`false` while the data sits in the
`.tmp`. If a mutex argument is still wanted, cite `data-dev.md:51` and pair it with MAJOR 3's task
map, which shows none of these three is on the render task.

---

## MAJOR 3 — the Concurrency section's task model is wrong: the one interleaving it analyses cannot happen, and the hazard the change does introduce is never checked

**Claim.** §Concurrency (spec `:271-279`): *"The three direct callers have no such lock, and already
do not: `PubKeyRegistry::record` runs on the download task while `findBySymbol` runs on the main
task. The one new interleaving this change introduces: `record()` is between `:38` and `:39` when
`findBySymbol()` adopts … Acceptable, and strictly better than today."*

**Problem.** There is no download task. `grep -rn xTaskCreate src` returns exactly one hit — the
render task at `src/activities/ActivityManager.cpp:34` — and the download is synchronous:
`src/activities/network/MeetingDownloadActivity.h:49` says *"The transfer blocks the loop task for its
duration."* Its two entry points are `MeetingDownloadActivity.cpp:297` and
`CatalogSearchActivity.cpp:405`, both activity code on the loop task, and `PubKeyRegistry::record`'s
only callers are inside that synchronous call (`PublicationDownloader.cpp:184,239`). The readers are
loop-task too: `findBySymbol` from `LauncherActivity.cpp:124` (inside `resolveTargets`, reached from
`onEnter` at `:72` and `activate` at `:523`) and `MeetingLibrary.cpp:44`; `lookup` from
`StudyStore.cpp:34`, `MigrationRunner.cpp:223` and `PublicationsActivity.cpp:59`. `record` and
`findBySymbol` therefore cannot interleave at all, and the analysed race is not reachable.

That matters because this section is the only place the genuinely new property is priced — a *read
path that renames and removes files*. With the wrong task map, "acceptable, and strictly better than
today" is asserted rather than shown. The correct statement is an invariant, not an interleaving
analysis: **the three direct callers are safe only because every writer and every reader of
`/.berean/pubkeys.json`, `/.berean/migration-ledger.json` and `/.berean/meeting-weeks.json` runs on
the loop task.** Break that — move the download to its own task, which is the natural fix for a
transfer that blocks the loop — and a new data-loss path opens that today does not exist: with the
primary `Missing` (after a failed rename, or before the first-ever save), an adopting read on task A
can `Storage.remove` the partially-written `.tmp` that `writeFile` on task B is in the middle of
producing (`PersistableStore.cpp:30` → `SDCardManager.cpp:282-292`, which removes the destination and
re-creates it), after which `record`'s rename fails and its entry is lost. Today the same
interleaving is harmless, because the reader only reads.

**Fix.** Replace the paragraph with the measured task map; state the single-task invariant explicitly
and mark it as the thing to re-check if a background download task is ever introduced. One line is
also owed to the second store writer the base class names — the web server task
(`PersistableStore.h:29-30`, `src/network/CrossPointWebServer.cpp:1320`): it saves `settings.json`, it
is covered for adoption because `loadFromFile` and `saveToFileAtomic` share `storeMutex`
(`PersistableStore.h:144,161`), and it touches none of the three direct-caller files.

---

## MINOR 1 — A-8's reason the log line is harmless is false; the release build keeps it

Spec `:307-309` says the `LOG_INF` *"is compiled out of `x4pro-gh_release` (`LOG_LEVEL=0`,
`platformio.ini`)"*. It is not. `platformio.ini:187-188` for that env is `-DENABLE_SERIAL_LOG` and
`-DLOG_LEVEL=1 ; Set log level to info for release builds`, and `lib/Logging/Logging.h:51-52` compiles
`LOG_INF` in at `LOG_LEVEL >= 1`. (CLAUDE.md's "production, `LOG_LEVEL=0`, no serial logging" is stale
against the same two lines — worth knowing, since the spec inherited it.) The decision is still right;
only the justification is wrong. Either drop the sentence and say the line is cheap and wanted in
release, or use `LOG_DBG` (`Logging.h:57`, `LOG_LEVEL >= 2`) if "diagnostic for the human tester only"
was meant literally.

## MINOR 2 — A-4's "three ways" table is incomplete, and the spec's own error table creates the fourth

`:245-262` enumerates three ways a `.tmp` can sit beside a `Missing` primary and concludes *"There is
no window in which a `.tmp` is partial and parseable, because the primary still exists throughout
`writeFile` in every case except the first-ever save."* The second row of that very table — a previous
`rename` returned false — leaves the primary `Missing` and a `.tmp` present, and the next
`saveToFileAtomic` then runs `writeFile` against that `.tmp` (`PersistableStore.cpp:30`) with no
primary on the card. The error table at `:297` deliberately produces the same state ("still `Ok` —
the document is in hand and the `.tmp` survives for the next boot to retry"). So the premise is false
as written. The conclusion survives, but for a better reason that should be the one stated:
`SDCardManager::writeFile` removes the destination before re-creating it
(`SDCardManager.cpp:282-284`), so an interrupted rewrite can only leave a *prefix* of a JSON object —
there is no stale tail that could make a partial file parse.

## MINOR 3 — the compatibility include contradicts "one include per call site" and leaves three non-highlight files on a highlights header

`:141-143` prices the move as *"four `src/` files change mechanically (one include, one call, five
`case` labels each)"*, while `:145-146` has `src/util/HighlightFileAction.h` gain
`#include <TempAdoption.h>`. Only one of those is needed. After the move, `highlightSaveAction` has
exactly one user (`src/util/HighlightFile.cpp:76`) — `BookmarkFile.cpp:98` uses `bookmarkSaveAction`
from `BookmarkSaveAction.h`, and `TagPaletteFile.cpp:70` and `PassageFile` measure inline. So
`BookmarkFile.cpp:12`, `TagPaletteFile.cpp:11` and `PassageFile.cpp:8` should include
`<TempAdoption.h>` and drop `util/HighlightFileAction.h` outright, which is what "one include" implies
and what A-2's whole naming argument wants. The shim include then has no purpose and should not be
added.

## MINOR 4 — the comments that document the moved rule are not on the files-touched list

CLAUDE.md requires comments written for the merged state. The load rule's prose lives at
`src/util/HighlightFileAction.h:8-25` and travels with the load half; `test/highlight_file/CMakeLists.txt:1-4`
and `test/highlight_file/HighlightFileActionTest.cpp:1-12` describe *"the `.tmp` promotion decision"*
and *"the never-overwrite-on-failure rule"* that the suite will no longer contain; and
`src/util/HighlightFile.h:12,34`, `src/util/BookmarkSaveAction.h:9` and
`test/bookmark_save_action/BookmarkSaveActionTest.cpp:5` all point at `util/HighlightFileAction.h` as
the home of that reasoning. Add them to the files-touched table so the move does not leave five stale
signposts.

## MINOR 5 — citation drift

Small, but these are the citations the arguments hang on.
- `PubKeyRegistry.cpp:78` is `lookup`, not `findBySymbol` — `findBySymbol` is at `:50` and reads at
  `:52`. Spec `:98` and research §4.2 both mislabel it, and it is the call site A-1's second reason
  names.
- Three of the four `switch` lines are off by one: `HighlightFile.cpp:40` (spec `:40` says `:39`),
  `BookmarkFile.cpp:55` (spec `:41` says `:54`), `TagPaletteFile.cpp:39` (spec `:41` says `:40`).
  `PassageFile.cpp:72` is right.
- The three save tests are `HighlightFileActionTest.cpp:52-66`, not `:52-63` (spec `:349`).

## MINOR 6 — a transient `.tmp` read failure deletes the only surviving copy

`SDCardManager::readFile` returns `""` when the open fails (`:197-199`) or the card is not initialised
(`:191-194`), indistinguishable from an empty file, so `classifyDocRead(true,true,false)` →
`Unreadable` → `tempParsed == false` → `DeleteTempReportEmpty` → `Storage.remove`. The rule is
inherited unchanged from the four adopters (A-5/A-6) and is defensible on that basis, but the spec's
own error table says the delete buys nothing — *"The next write truncates it anyway"* (`:299`). It is
now the rule for `settings.json`, `wifi.json` and `pubkeys.json` too, so it deserves one sentence
pricing it rather than silent inheritance. Not deleting at all is the cheaper option and loses
nothing.

## MINOR 7 — A-10's rationale over-reaches, and acting on it literally would make things worse

`:301-305` justifies `doc.clear()` by *"`PubKeyRegistry::record:23` and `MigrationRunner::appendLedger:74`
both discard the status and use `doc` immediately"*. That is equally true of the `ReportFailed` arm,
where `doc` is left holding whatever ArduinoJson parsed before the error — confirmed in the pinned
source: `doDeserialize` calls `dst.clear()` up front and returns the error with the partial document
intact (`build/test/_deps/arduinojson-src/src/ArduinoJson/Deserialization/deserialize.hpp:44-56`, v7.4.2
per `test/CMakeLists.txt:31`). An implementer following A-10's reasoning would "finish the job" by
clearing on every non-`Ok` return, and that is strictly worse until A-9 lands: `record` would then
write a one-entry registry over a corrupt-but-present `pubkeys.json` instead of merging onto the
entries that did parse. Say this explicitly — the clear is confined to `DeleteTempReportEmpty` on
purpose, and the `ReportFailed` arm is A-9's. (The implementer note at `:304-305` asking for the
ArduinoJson behaviour to be confirmed can be closed with the citation above.)

## MINOR 8 — the verification commands as given will not run the new suite

A-11 reports `add_subdirectory(temp_adoption)` rather than committing it, per
`.claude/agents/data-dev.md:22-27`. Correct — but then `cmake -S test -B build/test` never configures
`test/temp_adoption`, and the `ctest` line at `:358-361` cannot show the red or the green. Say so in
§Testing: add the line locally to drive TDD, revert it before committing, and hand it to the
orchestrator.

---

## What I could not verify

- Nothing was compiled from this change — it adds no code. I did build and run the existing
  `HighlightFileActionTest` (9 passed) to confirm the baseline the move must preserve, and did not run
  `pio run`, which would only re-prove `main`.
- The device behaviour is unverified, as the issue and research both say. The 6-second-delay procedure
  at `:376-388` is the right test and remains the human's.
- MAJOR 3's failure mode is an argument from the task map (`grep -rn xTaskCreate src` plus the call
  graph), not an observed race. Its point is that the spec's analysis is of an unreachable
  interleaving, not that the reachable one has been seen.
- Whether an over-cap `/.berean/migration-ledger.json` is reachable in the field depends on how many
  legacy files a real card carries; MAJOR 1 is about the corollary being false, and the ledger's
  missing budget gate is a live gap either way.

---

BLOCKERS: 0
MAJORS: 3
MINORS: 8

The design itself is right: the seam choice, the promote-before-validate ordering, the decision
function's move into `lib/Serialization`, and the pure-unit test strategy all survive attack. Every
finding above is a correction to a supporting argument or a text omission — none reverses a decision,
changes the files touched, or needs a judgement only the human can make. Fix MAJORs 1–3 and MINORs
1–8 inline, then proceed.

VERDICT: CLEAR
