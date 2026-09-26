# Issue #133 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-26-issue-133-design.md`
Research: `docs/superpowers/research/2026-09-26-issue-133-research.md`
Tree: `5bcab19f` (branch `test/persistable-store-adoption-refusal`)

## How this was checked

I didn't take the spec's tables on trust. I appended both tests exactly as the spec describes them
to `test/persistable_store/PersistableStoreTest.cpp`, with a `TORN = R"({"v":1,"val)"` constant
beside `NEWER`, a tag on each assertion, and `ASSERT_*` on the priming steps of test 2. I built
`PersistableStoreTest` and ran it against the unmodified tree. Then I applied M1 to M4 one at a time
as the spec words them, rebuilt, ran the suite and reverted each one. Afterwards I restored the test
file with `git checkout`, and `git status --short` was clean apart from this review.

Results against the unmodified tree: `[  PASSED  ] 7 tests.` Both new tests pass.

| Mutation | Spec says it must fail | Observed |
|---|---|---|
| M1 (`FormatVersion.h:24` → `return false;`) | test 1 steps 4 and 5; `ARefusedLoadBlocks…` too | test 1 S4a, S4b, S5a; `ARefusedLoadBlocks…` (`:70-72`); **also** `AnUnparseableFileAfterARefusalKeepsSavesBlocked` (`:103-104`) |
| M2 (remove `.tmp` on `KeepTempReportEmpty`, after `PersistableStore.cpp:83`) | test 2 step 4 | test 2 G4a only (`"<absent>"` vs `{"v":1,"val`) |
| M3 (`TempAdoption.h:60-61`: `KeepTempReportEmpty` → `ParseError`) | test 2 step 5 | test 2 G5 (`Actual: false`), G6a and G6b; nothing else |
| M4 (`PersistableStore.cpp:87` rename condition → `false`) | test 1 step 3 | test 1 S3b, S3c, **and** S5a, S5b |

The failures the spec names all occur, so every mutation guards what the spec says it guards.

I also checked these claims directly and found them correct:

- The flow citations: `PersistableStore.h:183`, `:201`, `:214-234` and `:222-224`;
  `PersistableStore.cpp:48-49`, `:56-59`, `:70-97`, `:87`, `:101-113` and `:23-45`;
  `TempAdoption.h:26-31`, `:46`, `:57-58` and `:60-61`; `FormatVersion.h:23-30`.
- The fake's behaviour: `writeFile` erases and then writes, and `rename` refuses an existing
  target (`HalStorageFake.cpp:96-103`, `:128-133`).
- The torn-prefix precedent: `AdoptingReadTest.cpp:62` seeds `{"v":`.
- The non-goal's base-level coverage: `AdoptingReadTest.cpp:52-59`.
- The CMake wiring: `test/persistable_store/CMakeLists.txt:3-8` already links
  `PersistableStore.cpp` and `HalStorageFake.cpp`.
- `hpipe status` lists this worktree as task `t2`, which matches `hpipe decide --task t2`.
- The concurrent task t1 (#132) changes store call sites, not `FormatVersion.h:24` or this test
  file, so the M1 line citation will not drift under it (`gh issue view 132`).

The labelled assumptions hold under attack:

- **A-6 is sound.** The priming refusal is what makes M3 fail. Without it, SetUp's `Missing` load
  leaves `loadRefused` false, and M3's `ParseError` keeps it false (`FormatVersion.h:27-30`).
- **A-3 is sound.** Under M4 the load-time check (S3b) fires as well as the save-time one (S5a),
  so the fault is located.
- **A-5 is sound.** Under M1 the leaked write is `{"v":1,"value":42}`, which is distinct from
  `NEWER`.

## Findings

### MAJOR 1: The A-10 mutation loop gives stale results on this machine's build tool

**Claim.** The spec's "Commands" section and the A-10 loop are: apply a mutation, run
`cmake --build build/test --target PersistableStoreTest -j8`, confirm the named failure, and revert
with `git checkout`.

**Problem.** The local generator is GNU Make 3.81 (`make --version`; the research note's tools
table). When a mutate, build, revert, mutate cycle runs quickly, Make can decide the object files
are up to date and skip the rebuild. The binary then still carries the previous mutation, or none.
The verification the spec relies on is then wrong in either direction:

- A stale green binary looks like "the mutation did not turn the test red". The spec's
  error-handling rule then tells the implementer to "fix the test, not the mutation", which means
  changing a correct test.
- A stale red binary produces failure excerpts in the PR body that the named mutation did not
  cause.

**Evidence.** My first run of M1 to M4 did the edit, build, run and `git checkout` back to back.
It printed failures for M2, M3 and M4 that were identical to M1's. They included
`PersistableStoreTest.cpp:70: Failure … saveToFileAtomic() Actual: true`, which neither M2 nor M3
nor M4 can cause, and `[  PASSED  ] 4 tests.` each time. After I deleted
`build/test/persistable_store/**/*.o` before each build, the results were the distinct and correct
ones in the table above.

**Fix (inline, no decision needed).** In "Commands" and in A-10, force a recompile before every run
of the mutation loop, including the final run after each revert. For example:

```
find build/test/persistable_store -name '*.o' -delete
```

The alternative is to `touch` the mutated source after editing it. Also require that each mutation's
excerpt contains only failures that mutation can cause. An M2 excerpt that fails `:70` is a stale
binary, not a result.

### MINOR 1: The M1 row understates what else fails

**Claim.** The M1 row names only `ARefusedLoadBlocks…` as the existing test that also fails.

**Problem.** `AnUnparseableFileAfterARefusalKeepsSavesBlocked` also fails under M1, at `:103` and
`:104`. An implementer who compares the output to the spec will see a discrepancy that the spec
calls unexpected.

**Evidence.** The M1 run above lists 3 failed tests.

**Fix.** Name both existing tests in the M1 row.

### MINOR 2: The M4 row understates what fails

**Claim.** The M4 row lists only test 1 step 3.

**Problem.** Step 5 fails too: `PATH` is absent and the `.tmp` still holds `NEWER`. This is
consistent with the reasoning in A-3, but the row does not list it.

**Evidence.** The M4 run above: S3b, S3c, S5a and S5b.

**Fix.** Write "Test 1 step 3 (and consequently step 5)".

## Verdict

The design is sound, and it is scoped exactly as issue #133 asks. The spec's step tables match the
code line for line. Both tests pass on the current tree, and each mutation catches the regression
its row names. The single MAJOR concerns how red is demonstrated on this machine, not what is
tested. It is fixed inline and reverses no decision.

VERDICT: CLEAR
