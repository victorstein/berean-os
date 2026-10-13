Tier: heavy

# Plan review 0: btrial-1, a name rank in TagPalette and its StudyStore consumers

Reviewed: `docs/superpowers/plans/2026-10-12-btrial-1-plan.md` (commit `2cb2182d`), checked against
`docs/superpowers/specs/2026-10-12-btrial-1-design.md`, the brief (`hpipe brief --task t1`), the
sibling briefs for `t4` and `t5`, and the code on `feature/tag-order-rank`. I did not apply or build
the plan. Probes are in `.../scratchpad/plan-review0/`.

## Verdict in one paragraph

The plan is sound and complete against the spec. Every spec requirement maps to a step. The
`FILES:` lock covers every path a step writes. Every find-string matches today's source. The code
it prescribes matches the spec's comparator, rebuild triggers, fallback and invariant I1. One
verification gap is real. No test in the plan, and no mutation in Task 3 Step 6, can detect losing
the comparator's middle level, the raw-name tiebreak. D2 and the brief's acceptance both require
that level, and the plan says it is tested. That is a MAJOR, fixed inline below by swapping two
lines of one test and adding one mutation. There is also one MINOR. Nothing here is a BLOCKER.

## Findings

### MAJOR 1: no test can see the raw-name tiebreak, though the plan says one does

- **Claim.** `TagPaletteOrder.IgnoresCase` adds `beta`(1), `Alfa`(2), `alfa`(3) and expects
  `[2, 3, 1]`, with the message "equal folded keys tie on the raw bytes, and 'A' sorts before 'a'"
  (plan lines 372-379). The self-review maps "then raw name, then id" to tests 2 and 3, M4 and M5
  (plan line 1475).
- **Problem.** `Alfa` is allocated before `alfa`, so the id tiebreak alone also gives `[2, 3, 1]`.
  Delete line 726 of the plan's comparator, `if (left.name != right.name) return left.name <
  right.name;`, and every test in the plan stays green.
  - `IgnoresCase` is the only test whose names fold to equal keys while their raw bytes differ.
  - Each of the other ordering inputs has either distinct folded keys or byte-identical names
    (`fe`/`fe`, and the truncated pair).
  - M5 removes the id level, not this one, and Task 3 Step 6 has no mutation for this level.

  So D2's "ties break on the raw name bytes, then the raw id" (spec lines 44-45, 151-158) and the
  brief's acceptance "ties broken by raw name bytes then id" would be reported as tested when
  nothing tests them.

  The case is reachable and user-visible. `add` matches case exactly, so `amor` and `Amor` coexist
  (`test/tag_palette/TagPaletteTest.cpp:31-34`). If a user creates `amor` first, the order
  `Amor, amor` depends on exactly this level.
- **Evidence.** `plan-review0/tiebreak_all.cpp` copies the plan's key builder and comparator and
  links the real `lib/BibleSearch/BibleSearch/Fold.cpp`. It runs every ordering input from Tasks 2
  and 3 through two comparators, one with the raw-name level and one without it.
  - Every input gives the same order both ways: "tests that detect the mutant: 0".
  - `plan-review0/tiebreak.cpp` shows the fix works. `beta`(1), `alfa`(2), `Alfa`(3) gives
    `[3 2 1]` with the level and `[2 3 1]` without it.
- **Fix (inline, keeps every test count unchanged).**
  1. In Task 2 Step 2, replace the `IgnoresCase` body with:
     ```cpp
     TEST(TagPaletteOrder, IgnoresCase) {
       study::TagPalette p;
       p.add("beta");
       p.add("alfa");
       p.add("Alfa");
       EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{3, 2, 1}))
           << "equal folded keys tie on the raw bytes, so 'Alfa' precedes 'alfa' though allocated later";
     }
     ```
     This departs from the literal row at spec line 357, with the same names in a different
     allocation order. It meets that row's stated assertion, "equal keys tie on raw bytes,
     `A` < `a`", which the literal row cannot test. Note the change in the plan's spec mapping.
  2. In Task 3 Step 6, after the M5 line, add a mutation (same shape as the others, same restore):
     ```bash
     echo "M6 no raw-name tiebreak:"; mut 'if (left.name != right.name) return left.name < right.name;' ''; run; cp /private/tmp/claude-501/-Volumes-stein--herdr-worktrees-berean-os-feature-tag-order-rank/e21478f5-7c04-4cd9-8eaa-c3582eb41c8a/scratchpad/TagPalette.cpp.good $F
     ```
     Add its expected line: `- M6: TagPaletteOrder.IgnoresCase`.
     - M1 to M5 keep their expected lists.
     - The new `IgnoresCase` is settled at the raw-name level, so M5 cannot reach it.
     - M1 to M4 do not involve this test's path.
  3. In the self-review table (plan line 1475), change "(M4, M5)" to "(M4, M5, M6)".

### MINOR 1: the formatter step asserts that only declared files can change, and has no fallback

- **Claim.** Task 6 Step 3 says that if `./bin/clang-format-fix` prints files, "they can only be
  files this plan declares", then tells the implementer to stage "only the paths git status listed"
  (plan lines 1424-1431).
- **Problem.** The wrapper formats the whole tree. A local clang-format that differs from CI's
  could reformat a file this change never touched. Followed faithfully, the step would then stage
  and commit a path outside the `FILES:` lock (plan lines 22-25).
- **Evidence.** `bin/clang-format-fix:21`: only `-g` limits the run to modified files. The
  unsuffixed form, which the plan runs, formats everything.
- **Fix.** Append to that paragraph: "If `git status` lists a path not on a `FILES:` line, do not
  stage it. Restore it with `git checkout -- <path>` and report it as a discovery."

## What I checked and found to hold

- **File lock.** Four `FILES:` lines sit at column 0, outside any fence, with repo-relative paths
  (plan lines 22-25). They cover every path a step writes:
  - the three `lib/StudyStore` files;
  - both `src/study` files;
  - the five `test/tag_palette` files, including the copied injector;
  - the three other `CMakeLists.txt`.

  Mutations, logs and the pristine copy go to the scratch directory. `test/CMakeLists.txt` needs no
  edit, because `add_subdirectory(tag_palette)` is already at `:115`. The A22 escape, a manifest or
  `lib_ldf_mode`, is routed to a decision rather than an out-of-lock edit (plan lines 1364-1365).
- **Find-strings.** Each one matches today's source exactly:
  - `TagPalette.h:57-58`, `:68-75`;
  - `TagPalette.cpp:1-10`, `:24-26`, `:57-64`, `:104-105`, and the M3 target in `retire` at `:31-32`;
  - `TagPaletteTest.cpp:1-6`, `:142-150`;
  - `StudyStore.h:42`, `:59-60`;
  - `StudyStore.cpp:11-12`, `:159-163`, `:184-194`;
  - the four `CMakeLists.txt`.
- **Spec coverage.** Every requirement maps to a step:
  - the API signatures, exact, as `t4` consumes them (`hpipe brief --task t4`, line 59);
  - `Entry::rank` placed last, `byName_`, and I1, with both paths writing all of `rank` and
    `byName_`;
  - the mark skip on exact bytes, the fold, and the zero-padded `memcmp`;
  - rebuilds only after an appending `add` and an accepted `fromJson`;
  - the `n == 0` early return;
  - the fallback, which is not sticky;
  - `activeIds()` untouched, comment included;
  - `inDisplayOrder` with its cap;
  - `activeTags()` reserving before its loop;
  - the two comment edits;
  - all 32 tests in the spec's three tables, by name;
  - the `ctest -R TagDisplayOrder` five-test guard;
  - `pio run`, `pio check` with CI's flags, and the full-tree format;
  - the A18 hand-off and the device checks.

  No step pushes, opens a PR or runs `hpipe submit`, and Task 6 Step 4 stops explicitly.
- **Mutation proof (Task 3 Step 6).** M1-M5 each target a real behaviour, and I traced each
  expected failure list against the test bodies.
  - Under M2, for example, `byName_` grows to `[1, 0, 0]` and the add-fallback test sees
    `[mu, zeta, zeta]`.
  - The counting premise is self-guarding. If the key buffer moved to a throwing allocator, the
    "DoesNotRebuild" tests would pass vacuously, but both `…MakesExactlyOneNothrowAllocation` tests
    would fail at `guard(1)`.
- **OOM premise.** `makeUniqueNoThrow<T[]>` uses `new (std::nothrow) Elem[count]()`
  (`lib/Memory/Memory.h:33`), which reaches the injector's replaced form
  (`test/ditherers/FailingNothrowNew.cpp:57`). `-fno-builtin` is target-wide, and it covers
  `TagPalette.cpp` because the target compiles that file itself.
- **Logging.** The plan logs with the literal `"TAGS"` rather than a `MODULE` constant, and gives a
  reason: the constant would warn on the host. That reason holds. `plan-review0/module.cpp` warns
  `unused variable 'MODULE' [-Wunused-const-variable]` under Apple clang 21 with the stub
  `LOG_ERR`. On the device, `LOG_ERR` at `LOG_LEVEL >= 0` is `logPrintf(..., format "\n", ...)`
  (`lib/Logging/Logging.h:46`), and `-Wformat` is a global build flag (`platformio.ini:70-72`), so
  `%zu` against `count * sizeof(SortKey)` is checked.
- **Firmware link.** `lib/BibleSearch/library.json` carries only a name and a version, with no
  `build` or `includeDir` override. `<BibleSearch/Fold.h>` therefore resolves from the library
  root, as `src/` code already uses it. `pio run` remains the proof, as the plan says.
- **Paths the tests cannot reach.**
  - *Interim picker.* Its selection is by id (`TagPickerActivity.cpp:38-49`, `:132`, `:261`), so a
    reorder cannot toggle or retire the wrong tag. Every interim palette mutation runs under
    `RenderLock` (`:164-166`, `:270-272`). `TagFilterActivity::retireTag` only flips `active`.
  - *Other loaders.* The sleep screen (`StudySleepScreen.cpp:169`) and `MigrationRunner.cpp:210`
    load into local palettes.
  - *Missing palette file.* `loadAdopting` never calls `fromJson` when the file is missing
    (`PersistableStore.cpp:222-229`), so `palette_` keeps its last consistent state.
  - *Persisted bytes.* No path persists display order apart from the A18 window. The plan carries
    that window into the hand-off notes (plan lines 1455-1461), and `t5`'s brief commits in
    `activeIds()` order (lines 55-56, 83).
- **Include shadowing on the case-insensitive host filesystem.** Adding `lib/Memory` to
  `migration_planner` and `passage_doc` could capture a `<memory.h>`. The only include of it in
  their sources is `lib/expat/winconfig.h`, which is Windows-only.
- **`TagDisplayOrder.LeavesTheStoredOrderAlone`.** It cannot fail, because `inDisplayOrder` takes
  `const std::vector<TagId>&`. The guarantee it states is real, but the type system enforces it,
  not the test. The spec requires the test, and it is harmless, so this is not a finding.

VERDICT: CLEAR
