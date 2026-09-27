Tier: heavy

# Issue #109 — plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-27-issue-109-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-109-design.md`
Tree: `3d74f729` (`src/` and `test/` identical to `dc97292e` = `origin/main`).

## What was checked, and held

- **Spec → step coverage.** Policy header + 8-row test (spec Architecture, Testing) → Tasks 1-2;
  `WifiSession` RAII type, non-copyable/non-movable, `.cpp`-only `<WiFi.h>`/`SilentRestart.h`
  (spec :232-251) → Task 3; the five activities per A4/A5 → Tasks 4-8; A12 Settings → Wi-Fi
  networks → Task 9; format/build/PR material incl. behaviour differences and follow-ups → Task 10
  and "PR description material". OTA comment rewrite (spec :261-262) is in Task 5; the
  `SettingsActivity.cpp:313-314` comment is explicitly left alone (plan :848), matching spec A7.
  No spec requirement is unmapped.
- **Old blocks match the source.** Every "Old" block was compared with the file:
  `ClockSyncActivity.h:1-3,20-23` and `.cpp:19-41`; `OtaUpdateActivity.h:1-5,29-31` and
  `.cpp:76-78,89-101`; `CatalogSearchActivity.h:3-8,123-125` and `.cpp:60-62,76-86`;
  `MeetingDownloadActivity.h:3-8,80-85` and `.cpp:72-80`; `FontDownloadActivity.h:3-8,95-98` and
  `.cpp:39-54`; `SettingsActivity.h:4-10,172-174` and `.cpp:308-310`. All match textually.
- **"Last data member" holds in all six.** OTA: `confirmPopup` (`OtaUpdateActivity.h:31`) is the
  last data member; the public section has none. CatalogSearch: `goHomeAfterCancel` (`:124`).
  MeetingDownload: `goHomeAfterCancel` (`:83`), only a member function follows. FontDownload:
  `rowsDirty_` (`:97`). Settings: `rowItems_` (`:173`); only `static` members follow (`:176-177`).
  ClockSync: the flag it replaces (`:23`).
- **Destruction order.** `ActivityManager::exitActivity` is `onExit()` then `reset()`
  (`ActivityManager.cpp:172-178`); Replace unwinds the stack with `onExit()` then `pop_back()`
  (`:145-149`); Pop runs the parent's handler after the child is destroyed, with the lock released
  (`:106`, `:119-127`). This is exactly what A4 and A12 rely on.
- **Types and names are consistent** step to step: `WifiSessionPolicy::ExitAction
  {EndSession, LeaveConnected, Nothing}` and `onExit(bool, bool, bool)` are identical in the spec
  (:207-211), the test (plan :80-115) and the header (plan :150-159); `std::optional<WifiSession>
  wifiSession` everywhere; `startActivityForResult(std::unique_ptr<Activity>&&,
  ActivityResultHandler)` (`Activity.h:60`) accepts the Task 9 lambda.
- **Compiles as written.** `LOG_DBG(origin, format, ...)` uses `##__VA_ARGS__`
  (`lib/Logging/Logging.h:58`), so zero-argument calls are fine; `src/network/*.cpp` already
  include their own header as `"network/X.h"` (`MeetingLibrary.cpp:1`); `SilentRestart.h` is at
  `src/SilentRestart.h`; each `.cpp` keeps `<WiFi.h>` for its remaining `WiFi.*` calls
  (`ClockSyncActivity.cpp:7`, `OtaUpdateActivity.cpp:5`, `CatalogSearchActivity.cpp:16`,
  `MeetingDownloadActivity.cpp:8`, `FontDownloadActivity.cpp:8`). The test CMake mirrors
  `test/posted_message/CMakeLists.txt` and `REPO_ROOT`/`crosspoint_test_common` exist
  (`test/CMakeLists.txt:38-41`).
- **Structural checks are correct.** After Tasks 4-8 the only `silentRestart()` left under
  `src/activities` is `CrossPointWebServerActivity.cpp:126`, so "exactly one line" (plan :899-900)
  is right; no other `WiFi.disconnect` exists in the five, so the per-task greps will be empty.
- **FILES lock.** Every committed file appears on a column-0 `FILES:` line outside a fence
  (plan :17-24), repo-relative or as the `test/wifi_session/` prefix. The local-only
  `test/CMakeLists.txt` edit is deliberately undeclared and restored in Task 10, which is the
  convention this pipeline already cleared (`2026-09-26-issue-108-plan.md:16`); the plan states it
  (plan :26-28). Formatter spill-over outside the list is reverted (plan :871-872).

## Findings

**MINOR 1 — The device-test log wording in the spec does not match the strings the plan writes.**
- Claim: the tester can follow "the six device steps from the spec" (plan :936).
- Problem: spec step 2 tells the tester to look for the "radio never on" log (spec :341-342) and
  A11 names the outcomes "session ended / connection left up / radio never on" (spec :169-170). The
  plan logs `"Session closed; radio already off"` and `"Session closed; leaving the connection it
  found"` (plan :230, :233). A tester searching serial output for "radio never on" finds nothing.
  The plan's wording is the more accurate one (row 5 of the policy is "was connected, radio now
  off", not "never on"), so the strings should stay.
- Fix: in the PR description, write the device steps out rather than referencing the spec, quoting
  the three exact `WIFI` strings from Task 3.

**MINOR 2 — Task 10's "unchanged files" check diffs against a moving ref.**
- Claim: `git diff origin/main -- src/main.cpp src/network/MeetingWeekCache.* ...
  CrossPointWebServerActivity.cpp WifiSelectionActivity.cpp` prints nothing (plan :896).
- Problem: worktrees share refs, and sibling tasks in this batch touch two of those files
  (`2026-09-26-issue-132-plan.md:24` → `src/network/MeetingWeekCache.cpp`;
  `2026-09-26-issue-108-plan.md:13` → `CrossPointWebServerActivity.cpp`). Once one merges and any
  worktree fetches, `origin/main` moves and this check prints a diff this branch did not make — a
  false failure an implementer may try to "fix".
- Fix: `git diff --stat "$(git merge-base origin/main HEAD)" HEAD -- <same paths>`.

**MINOR 3 — Tasks 3-9 are not red-first.**
- Claim: every step starts with a failing test.
- Problem: only Tasks 1-2 are TDD. That is spec-sanctioned (`WifiSession` cannot be host-built,
  spec :328-329), but each activity task's "Check" grep (plan :358, :461, :566, :669, :767) is only
  run after the edit, so it never demonstrates red.
- Fix: in each of Tasks 4-8, run the task's grep before editing (expect the teardown lines) and
  again after (expect nothing).

**MINOR 4 — One line reference is off by one.**
- `MeetingDownloadActivity.cpp` "Old (lines 37-39)" (plan :632) is actually lines 38-40; line 37 is
  blank. The block text matches, so an implementer is not misled. Fix: `38-40`.

## Verdict

The plan implements the cleared spec completely and literally. Every edit is shown in full against
source it matches, names and signatures are consistent, and the file lock covers every committed
path. The four MINORs can be fixed inline.

VERDICT: CLEAR
