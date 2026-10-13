Tier: heavy

# PR review 0: btrial-1, the tag name rank in TagPalette and its StudyStore consumers

Range: `origin/batch/20261012-tag-chip-picker-2l3m...origin/feature/tag-order-rank` (head `b984a3ef`,
merge base `07580bc0`, which is the batch tip, so the branch is current with the batch). Checked
against the brief (`hpipe brief --task t1`), the batch spec
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (D1, D2, §1, §6), the task spec
`docs/superpowers/specs/2026-10-12-btrial-1-design.md` and the plan
`docs/superpowers/plans/2026-10-12-btrial-1-plan.md`. The range has no worker reply commits, so this
is the first review of the change.

What I ran or read myself:

- **Host suite.** `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`
  gives `100% tests passed out of 1736`. The four targets that compile `TagPalette.cpp` run 46 / 14 /
  97 / 69 (`TagPaletteTest`, `MigrationPlannerTest`, `PassageDocTest`, `StorageIoTest`).
  `ctest -N -R TagDisplayOrder` lists 5 tests. The 32 new tests are all registered.
- **Mutation run.** I applied 23 mutants in a scratch copy, relinked them against the built test
  objects, and left the worktree untouched: 19 on `TagPalette.cpp` and 4 on `TagDisplayOrder.h`. All
  23 were killed. The table is under Intent.
- **Firmware.** I did not run `pio run`, per the reviewer's brief. I read the worker's post-commit
  log instead. `verify-pio-run.log` is from 18:01, after the last commit at 17:58, with a clean tree.
  - The build reports `x4pro SUCCESS`. Its only warning is the pre-existing
    `WebSocketsClient.cpp:573` deprecation, and no warning names a touched file.
  - `.pio/build/x4pro/firmware.map` resolves `BibleSearch::foldAppend` from
    `libc28/BibleSearch/BibleSearch/Fold.cpp.o` for `libbd9/StudyStore/StudyStore/TagPalette.cpp.o`.
    So spec A22 holds, with no `library.json` and no LDF change.
  - `x4pro` sets `-DLOG_LEVEL=2` (`platformio.ini:174`), so the new `LOG_ERR` expands to `logPrintf`
    with `format(printf, 3, 4)` (`lib/Logging/Logging.h:42,46`). `-Wformat` therefore checked the
    `%zu` / `size_t` pair, and it compiled clean.
- **Static analysis.** The worker's `btrial-1-pio-check.log` reads `No defects found`.
- **Format.** I copied the nine changed C/C++ files, `.clang-format` and `bin/clang-format-fix` into
  a scratch git repo and ran the wrapper. It changed nothing.

## Intent

### Acceptance criteria

| Criterion (brief) | Met | Evidence |
|---|---|---|
| `activeIdsByName()` in D2 order: case- and accent-insensitive, ñ→n, one leading `¿`/`¡` ignored, raw-name then id tiebreak, retired excluded | yes | Mark skip `TagPalette.cpp:27-33`, fold `:35-39`, comparator `:136-142`, active filter `:101-103`. Tests `TagPaletteTest.cpp:208-294` |
| `nameRank()` orders retired ids; `UINT16_MAX` for `UNLABELLED` and unknown ids | yes | `TagPalette.cpp:107-112`; every entry gets a rank at `:144-147`. Tests `:296-311` |
| Rebuilt after an appending `add` and after `fromJson`; `retire` and a revive leave it unchanged | yes | Rebuild calls only at `:57` and `:189`. The revive branch `:46-50` and `retire` `:61-65` do not call it. Tests `:313-326`, `:340-353`, `:355-414` |
| OOM falls back to allocation order and logs `LOG_ERR` | yes | `:119-127`. Tests `:416-445`. The log line is checked by inspection and `-Wformat`, as above |
| `activeTags()` returns display order; `tagNamesFor()` joins in display order without changing stored order | yes | `StudyStore.cpp:160-166` and `:187-198`. `inDisplayOrder` copies the ids (`TagDisplayOrder.h:22-36`). `TagDisplayOrderTest.cpp` `LeavesTheStoredOrderAlone` |
| `activeIds()` unchanged | yes | `TagPalette.cpp:89-96` and its comment `TagPalette.h:57-58` are untouched. Test `LeavesActiveIdsInAllocationOrder` |
| §6 `test/tag_palette` bullets, including two entries loaded with one name, and OOM through the replaced nothrow `operator new[]` with `-fno-builtin` | yes | `BreaksANameTieOnTheId` (`:263`) and `BreaksATruncatedNameTieOnTheId` (`:268`). Injector `test/tag_palette/FailingNothrowNew.cpp`, `-fno-builtin` at `test/tag_palette/CMakeLists.txt:23` |
| Four host targets build with `Fold.cpp`; full suite passes | yes | All four `CMakeLists.txt` add `Fold.cpp` and `lib/BibleSearch`; 1736/1736 |
| `pio run` with no new warnings; formatter leaves the tree clean | yes | See the firmware and format bullets above |

### Spec requirements beyond the acceptance list

- **Invariant I1 holds on every path.** `fromJson` has no return between `entries_.clear()` (`:167`)
  and the rebuild (`:189`). Both the rejected-document returns (`:163`, `:165`) come before the clear.
  The fallback and the normal rebuild both write every `rank` and every `byName_` slot (`:122-125`,
  `:144-147`). Copy and move carry `entries_` and `byName_` together. So `entries_[index]` in
  `activeIdsByName` (`:102`) can never index past the end.
- **A9:** an empty palette returns before allocating (`:117`).
- **A15** holds on the load path too. `PassageDoc.cpp:329` normalises loaded tags through
  `normaliseTags`, which caps at 8 (`:21`). So the cap in `inDisplayOrder` never drops a stored id.
- **A18 has an owner.** The interim window, in which today's picker commits in display order, is
  recorded in the spec and the plan, and repeated in the worker's implement report. `t5`'s brief
  restores palette-order commits (lines 55-56 and acceptance line 83). The picker keys selection on
  ids, not row positions (`TagPickerActivity.cpp:38-49`, `:216-225`), so the new order cannot attach
  a selection to the wrong tag in the interim. Its comment at `:218-219` ("Reported in palette
  order") is false only for that window, and `t5` rewrites the file.

### Scope

- Nothing under `src/activities/` changes.
- `Fold.h` and `Fold.cpp` are untouched.
- No format version, file layout or stored order moves.
- Only per-suite `CMakeLists.txt` files change; the root `test/CMakeLists.txt` does not.
- Three departures from the brief's literal shape are all recorded under the spec's "Deviations from
  the batch spec":
  - `TagDisplayOrder.h` (spec A14) does exactly the brief's "local `{rank, id}` array, sorted, then
    joined", behind a host-testable seam.
  - The key buffer is `SortKey` at 26 B per entry, not 24.
  - `byName_` holds the inverse order.
- The `activeTags()` reserve (A23) is CLAUDE.md resource rule 7, not new scope.

### Plan conformance

`TagPalette.cpp`, `TagPalette.h`, `TagDisplayOrder.h`, the `StudyStore` hunks and the CMake edits match
the plan's literal blocks (Task 2 Step 5, Task 4, Task 5 Steps 1-4, Task 1). The only divergence is the
suite total, 1736 rather than the plan's 1710. Sibling tests arrived with merge `6569f4a5`, and the
worker's report explains the difference. Nothing diverges unexplained.

### Do the tests exercise behaviour?

Yes. Every mutant was killed:

| Mutant | Killed by |
|---|---|
| no rebuild in `add` / in `fromJson` | 17 / 5 tests, including `IsRebuiltByAnAppendingAdd`, `IsRebuiltByFromJson` |
| a rebuild added to `retire` / to the revive branch | `RetireDoesNotRebuild` / `ReviveDoesNotRebuild`, `ReAddingAnActiveNameDoesNotRebuild` |
| no mark skip / skip every leading mark | `SkipsOneLeadingInvertedMark` / `SkipsOnlyTheFirstMark` |
| raw-name tiebreak removed / reversed | `IgnoresCase` |
| id tiebreak removed | `BreaksANameTieOnTheId`, `BreaksATruncatedNameTieOnTheId` |
| no fold | `IgnoresAccents`, `FoldsEnyeIntoN`, `SkipsOneLeadingInvertedMark` |
| signed instead of unsigned key compare | `SkipsOnlyTheFirstMark` |
| retired not filtered / unknown rank 0 / rank never written | `ExcludesRetiredTags` / `IsMaxForUnlabelledAndUnknownIds` / `OrdersRetiredIds` and two `TagDisplayOrder` tests |
| fallback sets only `rank` / only `byName_` / a reversed order | the `TagPaletteOom` tests |
| empty palette allocates / a second nothrow allocation | `AnEmptyPaletteAllocatesNothing` / both `ExactlyOneNothrowAllocation` tests |
| `inDisplayOrder`: no sort / id-only sort / no id tie / no cap | all five `TagDisplayOrder` tests between them |

The `TagPaletteRebuild` group detects a rebuild by counting nothrow allocations, which ties it to how
the rebuild allocates. That is acceptable here for two reasons:

- The brief makes "rebuild only when…" a requirement in its own right.
- A rebuild over unchanged names is otherwise unobservable (spec, Testing strategy).

The helper's comment (`TagPaletteTest.cpp:56-58`) states the assumption, so a future second nothrow
allocation fails loudly rather than silently. The `StudyStore` glue has no host target. It is covered
through `inDisplayOrder` and the owner's device check, as spec Step 4 says.

## Quality

- **Mirrors existing patterns.**
  - The rebuild mirrors `IndexBuilder.cpp`'s sort over flat keys with a null-checked buffer.
  - The OOM branch is CLAUDE.md's "`LOG_ERR` + fallback" shape.
  - `nameRank` is `name()`'s linear scan with a sentinel.
  - `TagDisplayOrder.h` is a header-only `inline` helper like `PassageSpan.h:23,43` in the same
    library.
  - The CMake plumbing, including its per-line include comments, follows
    `test/bible_search_index/CMakeLists.txt`.
  - The `-fno-builtin` block is word for word `test/ditherers/CMakeLists.txt:16-19`.
- **Naming and structure.**
  - `rank` and `byName_` sit beside `entries_` and `nextRaw_` in the same style.
  - `KEY_BYTES` is a `constexpr` derived from `MAX_TAG_NAME_BYTES`, not a second magic 24.
  - The helpers are camelCase in the anonymous namespace.
  - The literal `"TAGS"` module avoids a host `-Wunused-const-variable`, the plan's stated reason. The
    worker's report notes that warning firing for `MODULE` in `TagPaletteFile.cpp` and
    `PassageFile.cpp` on the host.
- **Comments.** Each new comment carries a reason or a field's meaning:
  - why 24 bytes suffices (`TagPalette.cpp:17`);
  - why the mark skip exists (`:25-26`);
  - why only two callers rebuild (`TagPalette.h:83-84`);
  - why the passage order is a copy (`TagDisplayOrder.h:14-15`).

  None restates the next line, and there is no dead or commented-out code.
- **Resources.**
  - There is one transient nothrow buffer (`SortKey[n]`, 1,560 B at 60 entries).
  - The scratch string is reserved once and reused.
  - `inDisplayOrder` keeps 32 B locally plus about 24 B of result on the stack.
  - `byName_` is resident at 2 B per entry.
  - A lookup stays an id compare plus a field read, with no fold on the hot path.
  - The per-name rebuild during migration (A17) is bounded and one-time.
- **Concurrency.** The locking is unchanged. Today's picker reads `activeTags()` on the render task
  under `RenderLock`, and its mutations that rebuild run under the same lock (spec data-flow).
- **Duplication.** `test/tag_palette/FailingNothrowNew.{h,cpp}` duplicates an existing file. See
  MINOR 1.

## Findings

1. **MINOR: the OOM injector is a second byte-identical copy.**
   - `diff test/ditherers/FailingNothrowNew.cpp test/tag_palette/FailingNothrowNew.cpp` and the
     same for the `.h` print nothing: 64 + 23 lines duplicated.
   - Spec A20 chose to copy because the header confines the injector to one suite
     (`test/ditherers/FailingNothrowNew.h:5-7`). But that confinement is about linking it into a
     single executable, and one shared source listed by each suite's `add_executable` would keep it.
   - As things stand, a fix to the injector, such as adding the aligned `operator new` forms, has to
     be made twice, and the next OOM suite will likely make a third copy.
   - It has no behavioural effect, and hoisting the file would edit `test/ditherers` (another
     suite's files). So no code change is asked of this branch. File it with
     `hpipe discover --task t1`, as a follow-up to move the injector to one shared test-support
     location that each OOM suite lists in its own `add_executable`.

Owner device checks still apply, as the spec lists them:

- Highlights passage rows list a passage's tags alphabetically.
- Serial shows no `TAGS` OOM line when a publication opens.
- `ESP.getFreeHeap()` is flat across a publication open and close.
- The picker's alphabetical listing is checked on the full batch, after `t5`.

VERDICT: CLEAR
