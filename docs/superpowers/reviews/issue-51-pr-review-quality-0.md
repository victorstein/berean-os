# PR #64 code-quality review — issue #51, adopting an orphaned `.tmp`

Reviewed `gh pr diff 64` against `main`, in the worktree at
`/Volumes/stein/.herdr/worktrees/berean-os/fix-51-adopt-orphaned-tmp` (branch
`fix/51-adopt-orphaned-tmp`). Scope, per the dispatch brief: code quality only — pattern reuse,
naming/structure consistency, dead/commented-out code, comment hygiene, error-handling shape, test
design, duplication. Spec, plan and PR intent are already `CLEAR` from earlier stages and are not
re-litigated here; A-12 (the new function deliberately does not delete an unusable `.tmp`, unlike
its four `src/` siblings) is treated as an accepted design decision, not a deviation to flag.

## What was verified, and how

- **Build.** `/Volumes/stein/.platformio/penv/bin/pio run` — `x4pro` links, `SUCCESS`, RAM 19.5%,
  Flash 81.1%.
- **Host tests.** `cmake -S test -B build/test && cmake --build build/test -j8 && ctest --test-dir
  build/test --output-on-failure -j` — 569/569 passed, including all 12 `test/temp_adoption/` cases
  and the 3 retained `HighlightSaveAction` cases in `test/highlight_file/`.
- **Format.** `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` over the whole tree — no diff
  produced; `git status --short` clean before and after.
- **Tree hygiene.** `git status --short --ignored` shows only the standard gitignored artifacts
  (`.pio/`, `build/`, `lib/I18n/*.generated`, `src/network/html/*.generated.h`, etc.) — nothing
  stray staged or left behind, and the required `test/CMakeLists.txt:97`
  `add_subdirectory(temp_adoption)` line is committed on the branch (not left as an uncommitted
  scratch edit, matching the design's A-11 procedure).
- **Cross-file grep.** `grep -rn "HighlightLoadAction\|highlightLoadAction"` across `src/`, `lib/`,
  `test/` returns zero hits — the rename left no stale reference to the old type/function name.
- **Cited mechanism, independently confirmed.** A-12's "the delete buys nothing" argument rests on
  `SDCardManager::writeFile` removing the destination before recreating it, and `readFile` capping
  at 50,000 bytes. Read directly:
  `freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:281-282` (`vol().remove(path)`
  before `openFileForWrite`) and `:200` (`constexpr size_t maxSize = 50000;`) — both match the
  comment in `PersistableStore.cpp` that cites them.

## Pattern reuse and structure

`lib/Serialization/TempAdoption.h` is `src/util/HighlightFileAction.h`'s load half moved verbatim
(`tempAdoptionAction`'s body is byte-for-byte `highlightLoadAction`'s), with `adoptedReadStatus`
added beside it. `readDocFromFileAdopting` (`lib/Serialization/PersistableStore.cpp:63-107`) is
declared next to `readDocFromFileChecked` in `PersistableStore.h:44-84`, reproducing the existing
`writeDocToFile` / `writeDocToFileAtomic` plain/safe pairing the write side already has — the same
shape, not a new one.

The four existing adopters (`HighlightFile.cpp:24-70`, `BookmarkFile.cpp:32-83`,
`TagPaletteFile.cpp:23-59`, `PassageFile.cpp:53-95`) are changed mechanically: one include swapped
for `<TempAdoption.h>`, one call renamed, five `case` labels retyped. Enumerator names, control
flow and log-message wording (`"Failed to promote %s into place"`) are byte-identical to before —
confirmed against the diff hunks, not just the design's claim.

`readDocFromFileAdopting` reuses the caller's single `JsonDocument doc` for both the primary and
`.tmp` reads, instead of the two separate `primaryJson`/`tempJson` documents the four `src/`
siblings use. This is a real structural difference, but it is deliberate and documented at both call
sites: `PersistableStore.h:74-75` ("Note the read of the `.tmp` writes into the caller's `doc`.
That is safe because the branch is only reached when `primary == Missing`") and confirmed against
`readDocFromFileChecked` (`PersistableStore.cpp:46-48`), which returns on `Missing` before touching
`doc` at all. The two shapes exist for a reason: the CRTP path returns a status and defers
`fromJson()` to `loadFromFile` (`PersistableStore.h:178-197`), while the four `src/` adopters call
`fromJson`/`toJson` inline per `case`. Not a "second way to do the same thing" — a narrower
interface used where it fits, with the invariant that makes it safe written down.

## Comments, naming, error handling

- No dead code and no commented-out code anywhere in the diff. `HighlightFileAction.h` was reduced
  to only the surviving `HighlightSaveAction` half, not left with a stale load-half stub — verified
  by reading the full post-diff file (`src/util/HighlightFileAction.h:1-24`).
- Comments explain non-obvious "why," not "what": the `DeleteTempReportEmpty` arm in
  `PersistableStore.cpp:87-97` states why the clear is confined to that arm and not `ReportFailed`
  (would make a read-modify-write caller overwrite a corrupt-but-present file), and why the `.tmp`
  is left on disk (delete buys nothing; a transient SD failure is indistinguishable from an empty
  file). Neither restates the adjacent line.
- The signposting update in `src/util/HighlightFile.h:9-14` was checked against the design's "files
  touched" table, which calls out that two *other* headers naming `HighlightFileAction.h`
  (`BookmarkSaveAction.h:9`, `test/bookmark_save_action/BookmarkSaveActionTest.cpp:5`) should be
  left alone because they cite its no-host-stub reasoning, not the load rule. Both are untouched in
  the diff, matching that call.
- Error handling follows the repo's established shape throughout: `LOG_ERR` + fall through on a
  failed rename (document still returned as `Ok`, `.tmp` survives for retry — matches the four
  existing adopters' identical choice), `LOG_INF` only for the new recovery event, no exceptions, no
  `abort()`.
- Naming is consistent with sibling pure-decision headers (`DocReadStatus.h`'s `classifyDocRead`,
  `SaveBudget.h`'s `fitsBudget`): `TempAdoptionAction`/`tempAdoptionAction`, both `constexpr`, same
  by-value `const` parameter style.

## Tests

`test/temp_adoption/TempAdoptionTest.cpp` is not a copy-paste of the old suite: 6 tests moved
unchanged (`TempAdoptionAction.*`), and 6 are new for `adoptedReadStatus`, including
`ANonMissingPrimaryIsNeverReportedMissing`, which exhaustively drives all 4×2×2 inputs through both
functions composed together and asserts the exact `DocReadStatus.h:5-7` contract
("Missing... MUST distinguish from Unreadable/ParseError") rather than just re-checking individual
branches. That is a property test on the composition, not a restatement of the branch tests already
above it — genuinely well designed, not merely present. The `EXPECT_EQ` inside the nested loop is
correctly braced (`TempAdoptionTest.cpp`, matching commit `d9e3b87f`) to avoid `-Wdangling-else` on
GCC.

`test/highlight_file/HighlightFileActionTest.cpp` was trimmed to keep exactly the `HighlightSaveAction`
tests that still belong to that file, with a header comment and CMakeLists comment rewritten to say
where the load half went, rather than left stale.

## Duplication

The one rule this PR could have duplicated — the five-way `.tmp` decision — was moved, not copied.
`grep` confirms no residual definition or reference to the old `HighlightLoadAction` name anywhere
in the tree.

## Findings

None at MAJOR or BLOCKER severity. No MINOR findings worth recording either — the two candidate
observations above (the shared-`doc` reuse, the unmodified `BookmarkFile.cpp` "PassageFile and
TagPaletteFile" comment that doesn't name `HighlightFile`) were checked and are either explicitly
justified in-code or pre-existing and unchanged by this PR, not something this diff introduced.

VERDICT: CLEAR
