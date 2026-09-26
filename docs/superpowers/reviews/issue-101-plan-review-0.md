# Issue #101 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-26-issue-101-plan.md`
Spec: `docs/superpowers/specs/2026-09-26-issue-101-design.md`

## What I checked, and what held

I checked every "Current text" block the plan quotes against the tree, and all of them match:

- `PersistableStore.h:46,148-153,165-177,179-199`
- `RecentBooksDoc.h:19,46-47,80-87` and `RecentBooksDoc.cpp:3,42-55`
- `RecentBooksStore.cpp:13-23`
- `CrossPointState.h:32-34` and `CrossPointState.cpp:1-4,43,59-60`
- `CrossPointSettings.h:427-429` and `CrossPointSettings.cpp:3-4,63-66,107-108`
- `WifiCredentialStore.h:45` and `WifiCredentialStore.cpp:3-4,11-13,28-30`
- `SettingsSave.h:8-11`
- `main.cpp:29-30,411-414`

Other things I confirmed:

- **Includes resolve.** `WIFI_STORE` is defined at `WifiCredentialStore.h:83`, and `main.cpp` does not include that header yet, so the include added in Task 6.3 is needed. `FormatVersion.h`'s relative `#include "DocReadStatus.h"` resolves on the host under `${REPO_ROOT}/lib` (`test/CMakeLists.txt:38-41`). `RecentBooksDocTest` already has `lib/Serialization` on its include path (`test/recent_books_doc/CMakeLists.txt:10`), so it needs no CMake edit.
- **Budget figures are correct.** `DOC_WRAPPER_BYTES` becomes 18. The overhead sum is 18 + 9 + 520 = 547, and the budget moves to 11427. That is still under `DEFAULT_SAVE_BUDGET` = 45000 (`SaveBudget.h:23`), so the static_asserts at `RecentBooksStore.cpp:125-129` hold.
- **The Task 1 test count is right.** There are 9 tests.
- **The new `loadFromFile` is equivalent to the old one on every non-`Ok` path.** It returns `false` without touching members, and the resave still runs outside the lock.
- **The Missing-clears case holds.** The `DeleteTempReportEmpty` arm maps to `Missing` (`TempAdoption.h:53-55`), as the spec says.
- **The Wi-Fi boot load is safe.** The obfuscation key comes from eFuse (`ObfuscationUtils.cpp:23`, `esp_efuse_mac_get_default`), not from Wi-Fi. A boot-time `WIFI_STORE.loadFromFile()` therefore decodes passwords correctly, and it cannot trigger a destructive "integrity failed" resave.
- **Spec coverage is complete.** Every spec item maps to a task: A1-A3, A5-A10, d1, d2, the budget change, the five comment rewrites, the log wording and the docs. The self-check table (`plan:1263-1276`) is accurate. A4 and A8 are rationale only, and A8's "written first" holds in all four `toJson` edits.
- **The FILES lock covers every committed path.** `test/CMakeLists.txt` is edited locally and never committed. The plan excludes it deliberately (`plan:17-20`), which matches the spec (`spec:225-227`) and data-dev's "report, do not edit" rule (`.claude/agents/data-dev.md:22-27`). No committed file falls outside the FILES lines, so this is not a lock breach. It does cause the rebase problem in MAJOR-1.

## MAJOR

### MAJOR-1: Task 8.0's `git rebase origin/main` fails as written

- **Claim.** Task 8.0 says: "All three listed: rebase (`git rebase origin/main`) and continue with 8.1" (`plan:976`).
- **Problem.** By then the working tree has an unstaged modification to `test/CMakeLists.txt`, appended in Task 0 (`plan:74-76`) and deliberately never staged. `git rebase` refuses to start on a dirty tree ("cannot rebase: You have unstaged changes"). Worse, `origin/main` will change `test/CMakeLists.txt` in the same region once the orchestrator applies #99's own `add_subdirectory` line. `git log origin/main -- test/CMakeLists.txt` shows it is routinely edited on main (`5c385845`, `32f66774`, `99bcc73a`). An autostash would conflict there. The environment also warns against touching the shared stash stack. An implementer following the plan literally stops at this step.
- **Evidence.** `plan:71-76` appends and does not stage. `plan:976` rebases. `plan:1183` and `plan:952` confirm the file is meant to stay modified.
- **Fix.** In 8.0, replace the rebase instruction with:

  ```bash
  git checkout -- test/CMakeLists.txt
  git rebase origin/main
  grep -q 'add_subdirectory(format_version)' test/CMakeLists.txt || printf 'add_subdirectory(format_version)\n' >> test/CMakeLists.txt
  ```

  8.1 then appends `persistable_store` as it does now.

### MAJOR-2: Task 8.3 formats before it stages, so the new suite is never formatted

- **Claim.** 8.3 runs `./bin/clang-format-fix`, then `git add test/persistable_store/...` (`plan:1181-1182`).
- **Problem.** The wrapper formats only what `git ls-files` lists (`bin/clang-format-fix:48`, with no `--others`). The two new files are untracked when it runs, so they are skipped and committed unformatted. No later format step exists, so the CI format job (`CLAUDE.md` "Formatting") fails on the PR. The spec requires the order the other way round: "`./bin/clang-format-fix` over the whole tree after `git add` of the new files" (`spec:326-327`). The project memory records the same trap ("skips untracked files — `git add` new files first").
- **Evidence.** `plan:1180-1184` and `bin/clang-format-fix:48-54`.
- **Fix.** Reorder 8.3 as below. Task 7.3 is unaffected, because everything it needs is committed by then.

  ```bash
  git add test/persistable_store/CMakeLists.txt test/persistable_store/PersistableStoreTest.cpp
  ./bin/clang-format-fix
  git add test/persistable_store/CMakeLists.txt test/persistable_store/PersistableStoreTest.cpp
  ```

## MINOR

### MINOR-1: The #99 gate is checked late, not "when `implement` starts"

- **Claim.** The spec says: "If #99 has not landed when `implement` starts, that is surfaced as a decision then" (`spec:321-322`).
- **Problem.** The plan checks only at Task 8.0, after seven commits. As of now `origin/main` has no fake. `test/stubs/` holds only `Arduino.h`, `HalDisplay.h`, `HalStorage.h` and `Logging.h`, so the decision is certain to fire. Doing Tasks 1-7 first is productive, but the orchestrator learns about the wait later than the spec promised.
- **Fix.** Run the same `git ls-tree` probe in Task 0. If #99 is missing, file the `hpipe decide` there with the same recommendation, then carry on with Tasks 1-7. At Task 8.0, re-probe and act on the answer.

### MINOR-2: The docs layout departs from the spec's "one section per file"

- **Claim.** The spec asks for "one section per file ... following its layout: owner files, `### Version 1`, a one-line example, the `v` rule, and the save budget. The `v` bullet, in each" (`spec:339-343`).
- **Problem.** The plan uses one umbrella `##` with four `###` subsections titled "… — Version 1". It states the `v` rule once at the top, not as a bullet in each file (`plan:868-941`). The content is accurate. I checked it against `CrossPointSettings.cpp:86-104,174-213`, `CrossPointState.cpp:59-95` and `WifiCredentialStore.cpp:32-111`. The completion section it sits beside uses `## <path>` with `### Version 1` (`docs/file-formats.md:350-360`).
- **Fix.** Either make each file its own `## \`/.crosspoint/<name>.json\`` with `### Version 1` and a `v` bullet, or keep the umbrella and add a one-line note to the spec's Documentation section recording the consolidation. Both are cosmetic.

### MINOR-3: A Task 7.1 build fix would land in a `docs:` commit

- **Claim.** 7.1 says: "If there is an error, fix it in the file it names and rebuild once" (`plan:859-860`).
- **Problem.** Such a fix is swept into the `docs: document the four /.crosspoint JSON stores` commit by `git add -u lib/ src/` (`plan:958`). Commits are squash-merged, so the release is unaffected. The branch history would then misdescribe a code fix.
- **Fix.** Add: "commit any build fix on its own (`fix: …`) before 7.2."

## Verdict

The plan follows the spec closely and its quoted anchors are exact. Both MAJOR findings are mechanical ordering bugs with inline fixes. Neither reverses a decision or changes scope, and neither needs a human judgment.

VERDICT: CLEAR
