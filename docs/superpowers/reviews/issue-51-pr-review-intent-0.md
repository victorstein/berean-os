# Adversarial review — issue #51 PR #64, intent, pass 0

**Target:** PR #64, branch `fix/51-adopt-orphaned-tmp`, base `main`.
**Measured against:** issue #51, `docs/superpowers/specs/2026-09-17-issue-51-design.md` (pass 1),
`docs/superpowers/plans/2026-09-17-issue-51-plan.md`, and the two cleared earlier reviews
(`issue-51-spec-review-0.md`, `issue-51-plan-review-0.md`).
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/fix-51-adopt-orphaned-tmp`.

## What was checked, and how

Every gate was run here rather than taken from the PR body:

| Gate | Result |
|---|---|
| `cmake -S test -B build/test` (after `rm -rf build/test`), `cmake --build build/test -j8`, `ctest -j4` | **569/569 passed**; `ctest -R "TempAdoptionAction\|AdoptedReadStatus"` → **12/12** |
| `/Volumes/stein/.platformio/penv/bin/pio run` | **SUCCESS**, 48.3 s; RAM 19.5%, Flash 81.1% |
| `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` | **No defects found** (this is the exact CI line, `.github/workflows/ci.yml:95`) |
| `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` (unsuffixed, whole tree) | exit 0, no files changed, tree still clean |

Every code claim below was read in the tree at the cited line, not inferred from the diff.

**Note on the brief.** It says the PR deliberately omits `add_subdirectory(temp_adoption)` from
`test/CMakeLists.txt`. That is now stale: commit `534dc24f` ("test: wire the temp_adoption suite
into the host build", authored by the orchestrator) landed on this branch during the review and adds
the line at `test/CMakeLists.txt:97`. The suite is therefore configured and CI will run it. I made
no local edit to that file beyond a duplicate append that I immediately reverted; the tree is clean.

---

## MINOR 1 — the PR body's blocking "shared file" section is now false

PR #64's body still carries:

> ### ⚠️ Shared file — needs the orchestrator
> `test/CMakeLists.txt` is on data-dev's report-do-not-edit list, so this PR does not contain the
> line the new suite needs.

The line is in the PR, at `test/CMakeLists.txt:97`, added by `534dc24f` on this branch. Left as-is,
the body tells a reviewer that CI is green while 12 tests never run — the opposite of the truth, and
the one item the body marks as blocking. **Fix:** replace that section with a one-line note that the
orchestrator applied the line in `534dc24f`, or drop it. Nothing in the code changes.

## MINOR 2 — the recovery `LOG_INF` fires even when the promotion failed

`lib/Serialization/PersistableStore.cpp:80-83`:

```cpp
if (!Storage.rename(tmpPath.c_str(), path)) {
  LOG_ERR("PERSIST", "Failed to promote %s into place", tmpPath.c_str());
}
LOG_INF("PERSIST", "Recovered %s from an interrupted write", path);
```

Returning `Ok` after a failed rename is correct and is exactly what the spec's error table
prescribes — the document is in hand and the `.tmp` survives for the next boot. The unconditional
`LOG_INF` is not. A field report from a card that cannot be renamed on reads
`ERR PERSIST Failed to promote /.crosspoint/settings.json.tmp into place` immediately followed by
`INF PERSIST Recovered /.crosspoint/settings.json from an interrupted write`, and the spec's own
hardware procedure (spec §"What only the human tester can verify", step 4) treats that `INF` line as
the *confirmation* that the file was renamed and the `.tmp` is gone. It is the sole ship-in-release
output this change adds (`platformio.ini:188`, `-DLOG_LEVEL=1`), so its precision is the whole
observability budget. **Fix:** move it into an `else`, or reword to "…recovered in memory". The
plan's code (plan:610-614) has the same shape, so this is a carried-through nit, not a divergence.

---

## Acceptance criteria — issue #51

| Issue requirement | Where it lands | Verdict |
|---|---|---|
| "On load, when `path` is absent but `path.tmp` exists and parses, adopt the `.tmp`" | `PersistableStore.cpp:63-101`; promote arm at `:77-84` | **met** |
| "If it does not parse … fall through to defaults as today" | `:85-99` → `adoptedReadStatus` → `Missing`; `loadFromFile` (`PersistableStore.h:186-188`) returns false | **met** |
| "…remove it" | **deliberately not done** — A-12. See "Divergences that are explained", below | **explained, not silent** |
| Scope: `CrossPointSettings`, `CrossPointState`, `WifiCredentialStore`, `RecentBooksStore` | one line, `PersistableStore.h:186`; all four reach it through `loadFromFile` — verified each declares `getFilePath()` (`CrossPointSettings.h:408`, `CrossPointState.h:36`, `RecentBooksStore.h:37`, `WifiCredentialStore.h:50`) and that `loadFromFile` is their only load entry (`main.cpp:411-413`, `LauncherActivity.cpp:77`, `PublicationsActivity.cpp:45`, `WifiSelectionActivity.cpp:95`) | **met** |
| "plus bookmarks once #28 lands" | `BookmarkFile.cpp:55` already adopts through its own switch; now on the shared `tempAdoptionAction` | **met** |
| "Worth deciding whether adoption happens in `readDocFromFileChecked` … or per store" | decided and argued in spec §A-1; implemented as a third function | **met** |

**The scope is wider than the issue asked for, and correctly so.** I re-derived the exposed set
independently: `grep -rn "writeDocToFile"` gives exactly nine atomic writers —
`PassageFile.cpp:112`, `MigrationRunner.cpp:82,137`, `PubKeyRegistry.cpp:42`,
`TagPaletteFile.cpp:75`, `BookmarkFile.cpp:107`, `HighlightFile.cpp:86`, `MeetingWeekCache.cpp:56`,
plus `saveToFileAtomic` for the four CRTP stores. Every one of those files now has an adopting read
or its own promote switch, except `MigrationRunner.cpp:137` (`REPORT_PATH`), which is written and
never read — a stated non-goal. No writer is left with an unrecoverable read path.

## Spec requirements — implemented, not half-implemented

Every row of the spec's files-touched table (`:212-225`) is present:

- `lib/Serialization/TempAdoption.h` — new, 60 lines: `TempAdoptionAction` (`:21-27`),
  `tempAdoptionAction` (`:29-42`, body identical to the retired `highlightLoadAction`),
  `adoptedReadStatus` (`:48-59`, new).
- `src/util/HighlightFileAction.h` — load half gone, save half untouched, `<DocReadStatus.h>`
  dropped, **no shim** (spec MINOR 3 / A-2). Confirmed: it does not include `<TempAdoption.h>`.
- `PersistableStore.h:69-89` declares `readDocFromFileAdopting` with the single-task invariant in
  the comment (this is plan-review MAJOR 2's fix, and it is really there, not only in the PR body);
  `:186` switches `loadFromFile`.
- `PersistableStore.cpp:63-101` defines it, matching the spec's pseudocode line for line.
- The four adopters are rename-only: `HighlightFile.cpp:42-67`, `BookmarkFile.cpp:56-82`,
  `TagPaletteFile.cpp:39-59`, `PassageFile.cpp:72-95` — five `case` labels and one call each, every
  arm's body byte-identical to before.
- Six direct call sites converted: `PubKeyRegistry.cpp:23,52,78`, `MigrationRunner.cpp:55,74`,
  `MeetingWeekCache.cpp:23`.
- `test/temp_adoption/` new (CMakeLists mirrors `save_budget`'s); `HighlightFileActionTest.cpp`
  goes 9 → 3 `TEST` blocks and its header comment is rewritten; `test/highlight_file/CMakeLists.txt:1-4`
  reworded; `HighlightFile.h:10-15` repointed.
- `grep -rn "highlightLoadAction\|HighlightLoadAction" src lib test docs` → **no output**. The
  rename is complete; nothing dangles.
- `grep -rn "readDocFromFileChecked" src` → exactly six hits: `HighlightFile.cpp:29,37`,
  `BookmarkFile.cpp:41,49`, `TagPaletteFile.cpp:26,34`. Precisely the primary+`.tmp` pairs that must
  keep reading the literal path (an adopting read there would stat `<path>.tmp.tmp`). `PassageFile`
  is correctly absent — it goes through its own streaming `readInto` (`PassageFile.cpp:31-47`).

## Earlier review findings — really applied

| Finding | Evidence in the tree |
|---|---|
| plan MAJOR 1 (delete the donor tests *before* emptying the header, or the commit does not compile) | commit order is `1dd18a3a` (test move) → `19627b67` (header retire) |
| plan MAJOR 2 (the single-task invariant must appear in a step, not only in the spec) | `PersistableStore.h:80-88` |
| plan MINOR 3 (do not repoint the two signposts whose referent did not move) | `src/util/BookmarkSaveAction.h:9` and `test/bookmark_save_action/BookmarkSaveActionTest.cpp:5` are untouched in the diff |
| plan MINOR 4 (`HighlightFile.h:34` has no content to edit) | only `:10-15` changed |
| plan MINOR 6 (`pio check` never run; CI fails on `low`) | re-run here: no defects |
| plan MINOR 7 (the `(Ok, false, true)` assertion was silently dropped from the "verbatim" move) | restored — `TempAdoptionTest.cpp:15`; the donor's four assertions are all present at `:14-18` |
| spec MINOR 3 (no compatibility shim) | `HighlightFileAction.h` has no `<TempAdoption.h>` include |
| spec MINOR 6 → A-12 (do not delete the `.tmp`) | `PersistableStore.cpp:85-99` clears `doc` and returns; no `Storage.remove` anywhere in the function |
| spec MAJOR 2's corrected citation | `.claude/agents/data-dev.md:51` is indeed "Never lock `storageMutex` on a read path the renderer sits behind" |
| spec MAJOR 3's task map | `grep -rn xTaskCreate src` → one hit, `ActivityManager.cpp:34`, the render task |

## The load-bearing safety claim, re-derived rather than trusted

The change makes a read path mutate the filesystem, and its only protection is "one task". I did not
take that from the spec. `ActivityManager::renderTaskLoop` (`ActivityManager.cpp:48-60`) calls
`currentActivity->render()` and nothing else, and every caller of an adopting read is on
`onEnter`/`refresh`/`activate`, i.e. the loop task:

- `PubKeyRegistry::findBySymbol` ← `LauncherActivity.cpp:124`, inside `resolveTargets()`, whose only
  two callers are `onEnter()` (`:72`) and `openPublications()`'s neighbourhood (`:523`) — never
  `render()` (`:452`).
- `PubKeyRegistry::lookup` ← `StudyStore.cpp:34`, `MigrationRunner.cpp:223`,
  `PublicationsActivity.cpp:59` (inside `refresh()`, called from `onEnter`).
- `MeetingWeekCache::load` ← `LauncherActivity.cpp:150` (via `thisWeeksMeetingPublication`) and
  `MeetingsActivity.cpp:43` (inside `refresh()`, called from `onEnter()` at `:34`).

The invariant holds, and the constraint it imposes on any future background task is recorded where
the next implementer will hit it (`PersistableStore.h:80-88`), not only in the PR body.

## Divergences that are explained

- **The issue says "remove it"; the code does not.** Spec §A-12 gives three verified mechanisms (the
  next `writeFile` reclaims the bytes anyway; `SDCardManager::readFile` returns `""` for both an
  uninitialised card and a failed open, so a transient hiccup would delete the only surviving copy;
  an over-cap `.tmp` that survives today would start being removed). It was raised by the spec review
  as MINOR 6, adopted into the spec in pass 1, and is the third bolded section of the PR body. This
  is the loudest possible deviation, not a silent one, and it makes the change strictly safer than
  the issue's literal instruction. Recorded here so the human sees it named, not to contest it.
- **`PersistableStore.h:77-78` says "the three study files"** where the plan's draft comment said
  four. Three is right: `HighlightFile`, `BookmarkFile` and `TagPaletteFile` pass a `.tmp` to
  `readDocFromFileChecked`; `PassageFile` uses its own `readInto`. An unannounced correction, and the
  correct one.

## Checked and sound — not findings

- **`doc` aliasing across the two reads is safe.** The `.tmp` is read into the caller's `doc`
  (`PersistableStore.cpp:72`), reached only when `primary == Missing`, and
  `readDocFromFileChecked` returns at `:47-48` *before* touching `doc` on a missing file. Verified in
  the source, not assumed.
- **A-10's clear is confined to the right arm.** `doc.clear()` at `:91` only on
  `DeleteTempReportEmpty`; the `ReportFailed` arm falls to `default:` and leaves the partial document
  alone, because clearing there would make `PubKeyRegistry::record` (`:23`) write a one-entry
  registry over a corrupt-but-present file rather than merge onto what parsed. The comment at
  `:86-90` says exactly this at the point it happens.
- **`loadFromFile` is behaviour-preserving at the boundary.** `readDocFromFile` was
  `readDocFromFileChecked(...) == Ok` (`PersistableStore.cpp:103-105`), so `!= Ok` is the same
  predicate plus adoption. `readDocFromFile` now has zero callers and is kept, which is A-7 and
  mirrors `saveToFile`'s stated rationale (`PersistableStore.h:145-146`).
- **Lock ordering is unchanged.** `loadFromFile` already held `storeMutex` across a
  `storageMutex`-taking read; adoption adds two more `storageMutex` acquisitions (`exists`,
  `rename`) on the `Missing` branch only, and introduces no lock the read path did not already hold.
- **No new `.tmp` naming collision.** `grep -rn '\.tmp' src lib` shows the only other `.tmp`
  producers are `FontDownloadActivity.cpp:86`, `ProgressFile.h:34`, `Epub.cpp:319` and
  `BookMetadataCache.cpp:16` — different directories, none on an adopting path.
- **`pio check` passes with the new `switch`/`default:` arms**, which plan step 12 specifically
  worried about. Confirmed, not assumed.
- **The tests exercise behaviour, not the implementation's shape.**
  `ANonMissingPrimaryIsNeverReportedMissing` (`TempAdoptionTest.cpp:70-88`) is a genuine property
  test: it composes `tempAdoptionAction` into `adoptedReadStatus` over all 4 × 2 × 2 inputs and
  asserts the `DocReadStatus.h:6-7` contract, which is the invariant that stops a read inventing
  "safe to overwrite" about a file whose bytes are on the card. The rest are case tables, which is
  what a five-way pure decision deserves.
- **The untested half is declared, not hidden.** `readDocFromFileAdopting`'s `Storage` sequence has
  no host coverage, because `PersistableStore.h:3` includes `<Arduino.h>` and `test/stubs/HalStorage.h`
  has no `Storage` singleton. The spec's §Testing weighs extending `test/pagination`'s link-time-fake
  pattern and rejects it as a new cross-cutting mechanism that `.claude/agents/data-dev.md` says to
  escalate rather than invent; the suite header (`TempAdoptionTest.cpp:1-7`) and the PR body both say
  so, and the PR hands the human a reproducible 6-second-delay procedure instead of claiming device
  verification. That is the honest handling, and the spec review already cleared it.
- **No scope expansion.** Nothing touches `lib/I18n/translations/*.yaml`, no UI surface, no new
  `DocReadStatus` value, `PubKeyRegistry::record` and `MigrationRunner::appendLedger` still discard
  their read status (#63's, and the PR says so). The only non-code additions are the research, spec,
  plan and review documents this repo's workflow already keeps under `docs/superpowers/`.
- **Release hygiene.** Title `fix: adopt an orphaned .tmp so an interrupted atomic write is
  recoverable` is conventional, base is `main`, nothing gitignored is staged, and the twelve
  implementation commits follow the plan's step order one for one.

## Verdict reasoning

Both findings are MINOR and neither touches behaviour: one is a stale paragraph in the PR
description, the other is a log line that overstates a failed rename. Neither reverses a decision,
changes scope, nor needs a judgment only the human can make. The substance holds up under
independent re-derivation — the scope is complete against the writers rather than merely against the
issue's list, the single-task safety claim is true in the tree, the one deviation from the issue's
literal text is argued and prominently disclosed, and all four gates pass when run here rather than
quoted.

Fix MINORs 1-2 inline, then proceed.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
