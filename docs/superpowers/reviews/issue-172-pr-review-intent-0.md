Tier: heavy

# PR #174 — intent review 0

Reviewed against issue #172, the spec `docs/superpowers/specs/2026-09-27-issue-172-design.md` and
the plan `docs/superpowers/plans/2026-09-27-issue-172-plan.md`. Base `7c080671`, head `3e6785b3`.

## What I checked, and how

- **Route groups are move-only.** I ran `git diff --color-moved=zebra --color-moved-ws=allow-indentation-change 7c080671 c5e93fe7 -- src/network`
  and read every line that was not coloured as moved. The non-moved lines are exactly the W7 set.
  - `server->` → `server.`
  - the `webroutes::` prefix
  - `sendHtmlContent(server.get(), …)` → `(&server, …)` in the three page handlers (`FileRoutes.cpp`, `SettingsRoutes.cpp`, `FontRoutes.cpp`)
  - `scanFiles` capturing `&server`
  - the guard `if (!serverRunning)` (`src/network/FileRoutes.cpp:270`)
  - the declarations, the includes and the three `registerRoutes` bodies

  Every status code, body string and protected-path check moved unchanged.
- **Route table.** `python3 build/route_table.py 7c080671` and `python3 build/route_table.py` on this branch give the same 21 `(method, path)` pairs.
  - The 17 API routes match the `###` headings in `docs/webserver-endpoints.md:27-376`.
  - The four pages match its page table (`:20-23`).
- **Registration order (W5).**
  - `src/network/CrossPointWebServer.cpp:110-112` calls the three `registerRoutes`.
  - `onNotFound` is at `:119` and `addHandler(webDavHandler)` at `:132`, so every group registers before WebDAV.
  - A comment at `:108-109` records why the order matters.
- **Ownership (W2, W3, W9).**
  - The groups are by-value members (`src/network/CrossPointWebServer.h:57-59`).
  - `fileRoutes{running}` is declared after `running` (`:52`), so the reference binds to a member that is already initialised.
  - The upload state lives in `FileRoutes` and `FontRoutes` (`FileRoutes.h:20-36`, `FontRoutes.h:14-26`).
  - The dead `formatFileSize` declaration is gone. No new heap allocation.
- **`setup()` stages.** I compared `setup()` at `7c080671` (`src/main.cpp:340-606`) with the new stages statement by statement (`src/main.cpp:329-637`).
  - Order is identical. The version log stays in `setup()` (`:630`).
  - The SD early return is kept: `if (!mountStorage(boot)) return;` (`:627`). It skips the same tail as before, including `allowSleepAt`.
  - `BootContext` (`:329-338`) holds exactly the issue's eight values, with the spec's defaults. It is a stack local (`:625`), so there are no new globals.
  - `restoreLightOn` and `isSleepWake` stay local to their stage (`:431`, `:470`).
  - The migration painter's statics stay function-local (`:490-491`).
- **Pure decisions (S6).**
  - `src/boot/BootDecisions.h` is byte-identical to the plan's Step 11 block.
  - `test/boot_decisions/BootDecisionsTest.cpp` is byte-identical to the plan's Step 10 block.
  - `chooseBootRoute` (`BootDecisions.h:58-71`) preserves the precedence of the old `if` chain, including the rule that a silent reboot never falls through to reader resume.
  - The `ResumeReader` side effects stay in the stage (`main.cpp:586-594`).
- **S7 eager `backHeld`.** `main.cpp:565` reads it before the route decision. This is the one ordering change, and the PR discloses it with the `const` call chain. The other inputs, `openEpubPath.empty()`, `lastSleepFromReader` and `readerActivityLoadCount`, are plain field reads, so reading them earlier is also free of side effects.
- **Tests.**
  - The 19 cases exercise the decision behaviour: one per branch, one per go-home condition, plus the silent-reboot and cold-boot edges. They do not restate the implementation.
  - `ctest` on this branch: `100% tests passed out of 1139`, the same count the PR reports.
- **Scope.**
  - Only files, settings and fonts leave the class. Core, `/migration`, Wi-Fi, WebSocket, UDP and WebDAV stay (spec non-goals).
  - The pre-existing oddities are left alone, as the spec says: the `const_cast`, the throwing `resize` and the missing `[[noreturn]]`.
  - `test/CMakeLists.txt` gains one line only.
  - The research, spec, plan and review documents are process artefacts, not scope creep.
- **Plan divergence.** The two artefacts that can be checked mechanically match the plan. The PR says no step deviated, and I found nothing to contradict that.

Not verified here: `pio run` (the brief forbids it) and the device checks. The PR lists both as outstanding human checks, which the issue's Verify section requires.

## Findings

### MINOR

1. **The PR's route-table command points to a path that does not exist.** The PR body says
   `python3 route_table.py 7c080671`, but the script is at `build/route_table.py` (plan Step 1,
   `docs/superpowers/plans/2026-09-27-issue-172-plan.md:74`), and `build/` is not committed. A
   reviewer following only the PR cannot rerun the before/after check. Fix: in the PR body, name
   `build/route_table.py` and point to plan Step 1 for its source.

No BLOCKER or MAJOR findings. Every acceptance criterion in the issue that can be checked off-device
is met. Every spec requirement W1–W9 and S1–S8 is implemented in full, with no silent scope
reduction and no scope expansion.

VERDICT: CLEAR
