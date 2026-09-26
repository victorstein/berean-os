# PR #134 review: intent (round 0)

Reviewed against issue #133, the spec `docs/superpowers/specs/2026-09-26-issue-133-design.md`, and the
plan `docs/superpowers/plans/2026-09-26-issue-133-plan.md`. The PR diff (`gh pr diff 134 --name-only`)
touches one code file, `test/persistable_store/PersistableStoreTest.cpp`. Everything else is under
`docs/superpowers/` (research, spec, plan, and the spec and plan reviews).

## Findings

None at BLOCKER, MAJOR or MINOR.

## Evidence

### Issue acceptance criteria

| Criterion (issue #133) | Where it is met |
|---|---|
| Future-version `.tmp`, primary missing: `.tmp` promoted, read `Ok`, `fromJson` refuses, `loadRefused` set | `PersistableStoreTest.cpp:117-123`. The only seed is `putFile(TMP_PATH, NEWER)` (`:118`). After the load, the test asserts that `PATH == NEWER` and `.tmp` is absent (`:122-123`), which is only true if the rename at `PersistableStore.cpp:87` ran. |
| ...so the next `saveToFileAtomic()` returns false and the promoted file survives byte-for-byte | `:125-129`. Both saves are false, `PATH == NEWER`, and no `.tmp` is left. |
| Garbage `.tmp`, primary missing: read returns `Missing`, which clears `loadRefused` | `:132-145`. The test starts with the refusal active (`:135-136`), so the save at `:145` can only succeed through the `Missing -> false` arm (`FormatVersion.h:25-26`). If the ParseError arm kept the flag (`:27-30`), the save would stay blocked. |
| `.tmp` kept on disk by the load | `:141`, `bytesOn(TMP_PATH) == TORN` |
| A subsequent save succeeds | `:145-147`, which also checks the exact bytes written and that the `.tmp` is gone |
| Add the tests to `PersistableStoreTest.cpp` using the #99 fake | Both are in the existing `PersistableStoreGuard` fixture. The fake is used through `storage_fake::putFile`/`fileBytes` (`:45-48`). |
| Assert on-disk state, not only return values | `:122-123`, `:128-129`, `:141-142`, `:146-147` |
| Test-only; no production change | The PR's file list has no `lib/` or `src/` file. After my own mutation check, `git status --short` was clean. |

### Spec requirements

- A-1 (one file, existing fixture and helpers): met. The new pieces are the `TORN` constant (`:43`, which A-7 requires) and the two tests.
- A-2 (header comment gains the #98 clause): met at `:1-4`.
- A-3 (card checked after the load and after the save): met at `:122-123` and `:128-129`.
- A-4 (both `saveToFileAtomic` and `saveToFile` are refused): met at `:126-127`.
- A-5 (`value == 0` after the refused load, then `value = 42` before saving): met at `:121` and `:125`.
- A-6 (garbage test starts refused): met at `:135-137`. The comment at `:133-134` gives the non-obvious reason, which the project's comment rules allow.
- A-7 (torn prefix as the named constant `TORN`): met at `:43`.
- A-8 (card checked at both points): met at `:141-142` and `:146-147`.
- A-9 (test names): these match the spec exactly.
- A-10 (red shown by mutation): the PR body has the M1 to M4 excerpts. Each excerpt contains only failures its own mutation can cause, and the line-number offset for M1 and M4 is explained. I re-ran M2 myself: I added `Storage.remove(tmpPath)` on `KeepTempReportEmpty` in `readAdopting`, deleted the object files and rebuilt. Only `AGarbageTempIsKeptByTheLoadAndStillLiftsTheRefusal` failed, at `:141`, which matches the PR excerpt. After reverting, all 7 tests pass.

### Scope

- Nothing is missing. The spec's non-goals match the issue: rename-failure promotion, `loadAdopting` stores, CMake, and the unreadable `.tmp` case that shares the same arm.
- Nothing extra. The additions beyond the issue's minimum (the post-load card check, `saveToFile()` refused as well, and the refused start in test 2) are all declared in the spec as A-3, A-4 and A-6. Each one makes a stated assertion able to fail, and none of them tests unrelated behaviour.

### Tests exercise behaviour

Both tests drive the public `loadFromFile` and `saveTo*` API of the real template and the real
`PersistableStore.cpp`, then observe the fake card. They do not restate the implementation. The
mutation evidence shows that each assertion group guards a distinct production property: the
refusal flag (M1), promotion (M4), keeping the `.tmp` (M2), and `Missing` reporting (M3).

### Plan conformance

The committed test code is character-for-character what plan steps 2a, 2b, 6a and 6b specify
(plan `:85-117`, `:196-222`). There is no divergence that would need an explanation.

Verification: `find build/test/persistable_store -name '*.o' -delete`, build `PersistableStoreTest`,
and run it. Result: `[  PASSED  ] 7 tests.`

VERDICT: CLEAR
