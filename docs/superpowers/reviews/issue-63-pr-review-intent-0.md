# PR #72 — intent review, pass 0

Stage 1. One question: does the PR do what issue #63 and
`docs/superpowers/specs/2026-09-19-issue-63-design.md` asked, completely, and
nothing more? Code quality is stage 2 and is not judged here.

Reviewed at `5acf8fde` on `fix/63-honour-read-status`, against issue #63,
the spec above, `docs/superpowers/plans/2026-09-19-issue-63-plan.md`, and the
research note `docs/superpowers/research/2026-09-19-issue-63-research.md`.

Every gate the PR body claims was re-run here, not trusted.

---

## Findings

No BLOCKERs. No MAJORs. Two MINORs, both in the PR body's prose, neither in the
code.

### MINOR 1 — the PR body's device recipe 4 says "instead" where it means "in addition"

`gh pr view 72`, testing section, case 4:

> Repeat with `{"v":9,"done":[]}` for the version arm, **which logs
> `Refusing to read a newer ledger format` instead.**

Trace what actually reaches serial for that file. `pending()`
(`src/study/MigrationRunner.cpp:183-195`) calls `readLedger`, which logs
`Refusing to read a newer ledger format` (`:70`) and returns `nullopt`, so
`pending()` returns `true` (`:189`). `main.cpp:474` therefore enters the block,
paints the screen, and calls `runIfPending` at `:502`, which calls `readLedger`
a **second** time — the same line logs again — then logs
`Migration ledger unreadable; refusing to migrate over it`
(`MigrationRunner.cpp:203`) and returns `false`, so `main.cpp:503` logs
`Migration incomplete; legacy store untouched`.

So the version arm emits four ERR lines: the version line twice, then both
lines case 4 already lists. "Instead" reads as replacing the expectations
stated two sentences earlier, and a tester who takes it literally would treat
the (correct) presence of `Migration ledger unreadable; refusing to migrate
over it` as a failure.

The spec's own copy of the recipe gets this right —
`specs/2026-09-19-issue-63-design.md:630-631`, "Replace the ledger with
`{"v":9,"done":[]}` for the A-7 arm: **the same refusal**" — as does the plan,
which prices the double log explicitly (`plans/2026-09-19-issue-63-plan.md:379-384`).
The PR body is the only place the wording drifted.

Fix inline: change "which logs `Refusing to read a newer ledger format`
instead" to "which additionally logs `Refusing to read a newer ledger format`,
twice — once from `pending()` and once from `runIfPending`." Body text only; no
code change.

### MINOR 2 — the ledger's new on-disk format version is not recorded in any format doc

`appendLedger` now stamps `"v": 1` into `/.berean/migration-ledger.json`
(`src/study/MigrationRunner.cpp:31,101`), and both readers refuse a higher value
(`:69-72`, `:98-101`). That is a new persisted format field.
`docs/file-formats.md` gains nothing.

This is deliberately *not* ranked higher, because the repo's existing state
makes it consistent rather than negligent: `docs/file-formats.md` documents no
`/.berean/` store at all — `grep -n "berean\|pubkeys" docs/file-formats.md`
returns nothing — and `PubKeyRegistry`'s own `FORMAT_VERSION`
(`src/study/PubKeyRegistry.cpp:13`) is equally absent from it. CLAUDE.md's
"document the change in `docs/file-formats.md`" sits in the EPUB-cache
versioning section and names `BOOK_CACHE_VERSION` / `SECTION_FILE_VERSION`;
storage-discipline rule 4, which is what spec Goal 4 cites, only requires that
the version exist and be refused, and it does. Recording it would be a useful
follow-up for the `/.berean/` stores as a set, not a defect this PR introduced.

---

## Acceptance criteria — issue #63

| Issue asks | Verdict | Evidence |
|---|---|---|
| `PubKeyRegistry::record` honours the status; `Missing` starts a new document, anything else refuses and `LOG_ERR`s | met | `src/study/PubKeyRegistry.cpp:25-29`. `Missing` falls through to the unchanged `:30` version read and the unchanged one-entry path. |
| `MigrationRunner::appendLedger` the same | met | `src/study/MigrationRunner.cpp:93-97`. |
| "a decision on what a refusal means for the caller … what the downloader does with it is the open question" | met, and answered | Spec §A-6 decides: `LOG_ERR` + `return false`, downloader unchanged, because at `PublicationDownloader.cpp:239` the file is already downloaded, MD5-verified and renamed into place. `src/network/` is untouched in the diff. The decision is argued in the spec and restated in the PR body, so it is a decision, not an omission. |
| "The user-visible half is #39's, not this issue's" | respected | No `lib/I18n/translations/` edit, no UI file in the diff; both refusals are `LOG_ERR` only. |
| Second gap: `appendLedger` has no `measureJson` budget gate | met | `src/study/MigrationRunner.cpp:113-116`, same shape as the `PubKeyRegistry.cpp:42-45` gate it was modelled on, `>` not `>=` to match. |
| "`writeReport` is *not* an instance of this … it should be left alone" | respected | `writeReport`'s truncation logic is not in the diff; `git diff main...HEAD` touches only the ledger path inside that file. |
| The issue's premise — twelve call sites, ten checking, two not | now twelve of twelve | `grep -rn "readDocFromFileAdopting\|readDocFromFileChecked" src/ lib/` (excluding `PersistableStore.*`) returns twelve call sites: `MigrationRunner.cpp:64,93`, `PubKeyRegistry.cpp:25,58,84`, `TagPaletteFile.cpp:26,34`, `BookmarkFile.cpp:41,49`, `HighlightFile.cpp:29,37`, `MeetingWeekCache.cpp:23`. Every one now binds or compares the status. No discarding site survives. |

The issue's compound-scenario claim ("together the two bugs turn a recoverable
interruption into total loss of the registry") is also closed: with #51's
adoption already landed, `record` no longer destroys a surviving
`pubkeys.json.tmp`, because it returns at `PubKeyRegistry.cpp:28` before
reaching `writeDocToFileAtomic` at `:48`.

## The twelve labelled assumptions

Each was checked against the code, not against the spec's own prose.

| | Assumption | Honoured | Evidence |
|---|---|---|---|
| A-1 | predicate in `lib/Serialization/DocReadStatus.h`, not a new header, not duplicated | yes | `lib/Serialization/DocReadStatus.h:24-40`. No `RewriteGuard.h` anywhere; `git diff --stat` shows no new header. |
| A-2 | both sites refuse **before** touching `doc` | yes | `PubKeyRegistry.cpp:25-29` precedes the first `doc[...]` read at `:30`. `MigrationRunner.cpp:93-97` precedes `:98`'s `doc["v"]`. `readLedger` also returns `nullopt` at `:66` before any `doc` access. |
| A-3 | `readLedger` → `std::optional<std::vector<std::string>>`, `nullopt` ≠ empty | yes | `MigrationRunner.cpp:62`; `Missing` → empty vector `:65`, other non-`Ok` → `nullopt` `:66`. |
| A-4 | `runIfPending` refuses the whole run on `nullopt`, mirroring the palette refusal, **before** the palette load | yes, including the ordering | refusal at `:201-205`; `TagPaletteFile::load` still at `:211`. Read order preserved, as the spec required at §A-4. |
| A-5 | `pending()` returns `true` on `nullopt` | yes | `:189`, with the reasoning comment `:186-188`. |
| A-6 | `LOG_ERR` + `return false`, `PublicationDownloader` unchanged | yes | `PubKeyRegistry.cpp:26-28`; `src/network/` absent from the diff entirely. |
| A-7 | ledger gains `"v": 1` and refuses a newer format | yes, on all three paths | constant `:31`; read refusal `:69-72`; rewrite refusal `:98-101`; stamp `:102`. Backward compatibility holds as claimed: a pre-change ledger has no `"v"`, `doc["v"] \| 0` is `0`, `0 > 1` is false. |
| A-8 | budget gate copied in shape from `PubKeyRegistry.cpp:36-39`, no shrink exception | yes | `:113-116`. No shrink allowance, correct for an append-only file. |
| A-9 | a failed `appendLedger` **stops** the run (`break`), at **both** call sites | yes, both | `:250-256` (the `LoadResult::Empty` arm the pass-0 spec review added) and `:390-396`. `grep -n appendLedger` returns exactly three hits — the definition and those two — so no third site was missed. `writeReport(summary, reports)` at `:399` still runs after the `break`, and `allOk = false` reaches `main.cpp:503`. |
| A-10 | no new `#include <DocReadStatus.h>` in either `.cpp` | yes | neither file's include block gained it; the diff adds only `<optional>` and `<utility>` to `MigrationRunner.cpp:13,15`. Both reach the predicate through `PersistableStore.h`, which both already included — and `pio run` proves it resolves. |
| A-11 | tests extend `test/doc_read_status/`; **no** `test/CMakeLists.txt` line, therefore no shared file | yes | `git diff --stat main...HEAD` lists ten files; `test/CMakeLists.txt`, `lib/I18n/translations/` and `src/main.cpp` are not among them. The PR's "Shared files: None" is accurate. |
| A-12 | the ≤29.2 KB bound is a code comment, not a host test | yes | `MigrationRunner.cpp:108-112`. Its two inputs check out: `Storage.listFiles(LEGACY_DIR, 200)` at `:48`, and `char name[128]` at `freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:175`. `DEFAULT_SAVE_BUDGET = 45000` at `lib/Serialization/SaveBudget.h:23`. |

## Undeclared departures — the PR body claims exactly one

The claimed one (a `LOG_ERR` on `readLedger`'s version arm, where the spec's
error table at `specs/…-design.md:518` has `readLedger` silent) is real, is
declared in the PR body, and was already declared in the plan
(`plans/…-plan.md:375-384`) with the double-log cost priced. Fine.

I looked for others and found none of substance. Every prescribed edit in plan
steps 1–7 is present verbatim, in the plan's order, under the plan's commit
messages (`git log --oneline main..HEAD`: six code commits, `679d7e83`,
`54d08d6f`, `671577c1`, `12975388`, `9b933a0c`, `5acf8fde`). The only textual
difference between the plan's blocks and the tree is two extra comment lines at
`MigrationRunner.cpp:67-68` explaining why the declared departure exists — an
annotation on a declared departure, not a new one.

Nothing in `src/` or `lib/` outside `lib/Serialization/DocReadStatus.h`,
`src/study/MigrationRunner.{h,cpp}` and `src/study/PubKeyRegistry.cpp` is
touched. The spec's "Files touched" table lists exactly five files; the diff
contains exactly those five plus the five workflow documents.

## Scope

**No reduction.** Every clause of the issue, including the folded-in second gap
and the explicit "leave `writeReport` alone", is discharged above.

**No expansion past what was sanctioned.** The change goes beyond the issue's
two literal lines in three places — `readLedger` returning `optional`,
`runIfPending`/`pending()` refusing, and the ledger format version — and all
three are argued in the spec (§A-3/A-4/A-5, §A-7) against the research finding
that makes them necessary: `PassageDoc::add`
(`lib/StudyStore/StudyStore/PassageDoc.cpp`) is a `push_back` with no identity
check, so a ledger read as empty duplicates every passage. Fixing only the two
named lines would have left the larger door open. Both earlier reviews CLEAR'd
that reasoning, and the PR body restates it under a heading that says so.

The two disclosed residues — `PassageDoc::add` still not deduplicating, and a
corrupt ledger now blocking migration on every boot — are both in the PR body
and both traceable to spec decisions (Non-goal 3, §A-4/A-5 trade-off). Neither
is a silent reduction.

## Tests: behaviour or restatement?

Five new `TEST`s at `test/doc_read_status/DocReadStatusTest.cpp:31-60`.

- The four literal cases (`OkMayOverwrite`, `MissingMayOverwrite`,
  `UnreadableMayNotOverwrite`, `ParseErrorMayNotOverwrite`) pin each enumerator
  against a literal expectation. That is the contract, not the implementation
  restated — the implementation is a disjunction, and a test that compared
  against that disjunction is precisely the one the spec dropped as tautological
  (`specs/…-design.md:567-574`). It is not present; I grepped for it.
- `AgreesWithClassifyDocRead` (`:48-60`) is the one that can genuinely fail. Its
  oracle, `expected = !exists || (!contentEmpty && !parseFailed)`, is an
  independent derivation over the three observable inputs, not a copy of either
  function's body. Flipping `Unreadable` to permitted, or reordering
  `classifyDocRead`'s guards, breaks it.

The four `static_assert`s at `DocReadStatus.h:37-40` cover the same four facts
as the four literal `TEST`s. That overlap is prescribed by the spec (§Testing,
"guarded at **compile time** instead", alongside the five cases) rather than
accidental, and the two mechanisms fail in different places — the asserts break
the firmware build, the tests break `ctest`. Not a finding.

## Gates — re-run, not trusted

| PR body claims | Measured here |
|---|---|
| `ctest` 598/598, up from 593 | **598/598 passed**, 0 failed. The five `MayOverwriteAfterRead` cases are tests #145–#149. `git show main:test/doc_read_status/DocReadStatusTest.cpp \| grep -c "^TEST("` = 5 vs 10 now, so exactly five added; 598 − 5 = 593 confirms the baseline arithmetically. |
| `pio run` SUCCESS | **SUCCESS**, 15.96 s. RAM 19.5%, Flash 81.1%. |
| zero warnings on both touched TUs | confirmed: `touch src/study/MigrationRunner.cpp src/study/PubKeyRegistry.cpp && pio run` recompiled both and `grep -iE "warning\|error"` matched nothing. |
| `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` → no defects, exit 0, `x4pro` named | **"No defects found"**, PASSED, `EXIT=0`, output names `x4pro` and `cppcheck`. |
| `./bin/clang-format-fix` whole tree then `git diff --exit-code` clean | ran the unsuffixed wrapper with `.venv/bin` on `PATH`: no output, exit 0, `git status --short` empty afterwards. |

The worktree is clean apart from this review file; the `touch` above changed
mtimes only, and `git status --short` confirms no content change.

The device cases are correctly flagged as human-only, and the four of them
cover the three status arms plus the `Missing` regression guard — including
case 3, which is the one that would catch the obvious way to get this fix
wrong (turning first boot into a refusal). Only the wording in case 4 needs the
MINOR 1 correction.

---

Two MINORs, both PR-body prose, both fixable inline without touching code.

VERDICT: CLEAR
