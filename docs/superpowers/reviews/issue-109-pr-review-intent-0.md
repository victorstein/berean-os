Tier: heavy

# PR #155 — intent review, pass 0

Reviewed: `git diff origin/main...HEAD -- src test` on `refactor/109-wifi-session` (HEAD `c910410e`),
against issue #109, the spec `docs/superpowers/specs/2026-09-27-issue-109-design.md`, and the plan
`docs/superpowers/plans/2026-09-27-issue-109-plan.md`.

## Summary

The PR does what the issue asks. It adds one RAII `WifiSession` that records the Wi-Fi state on entry
and restores it in its destructor, and all five named activities now use it in place of their
hand-written teardown. The spec's two widenings are both implemented in full: the exit check now
tests connectivity at both ends (review 0, MAJOR 2), and Settings → Wi-Fi networks gets a sixth
session (decision d1 / A12). The PR body lists and justifies both. The code matches the plan's
old/new blocks line for line. The only divergence I found is the PR title. I found no BLOCKER or
MAJOR.

## Acceptance criteria (issue #109)

| Criterion | Status | Evidence |
|---|---|---|
| One RAII `WifiSession` helper | Met | `src/network/WifiSession.h:8-20`, non-copyable and non-movable like `HalPowerManager::Lock` |
| Records whether Wi-Fi was up on entry | Met | `src/network/WifiSession.cpp:10` (`status() == WL_CONNECTED`, spec A1) |
| Restores that state in its destructor | Met, with a documented interpretation | `WifiSession.cpp:12-31` → `WifiSessionPolicy.h:15-18`. "Restore" means leave a connection alone that was up on entry and is still up, and otherwise turn off what the session left on. It never reconnects. Spec A2 decides this and argues it (`design.md:89-96`). |
| ClockSync uses it | Met | `ClockSyncActivity.cpp:20`, `.h:26`. The `shouldTearDownWifiOnExit` flag is gone (`grep -rn shouldTearDownWifiOnExit src` finds nothing). |
| OtaUpdate uses it | Met | `OtaUpdateActivity.cpp:77`, `.h:35` |
| CatalogSearch uses it | Met | `CatalogSearchActivity.cpp:61`, `.h:130` |
| MeetingDownload uses it | Met | `MeetingDownloadActivity.cpp:39`, `.h:87` |
| FontDownload uses it | Met | `FontDownloadActivity.cpp:40`, `.h:100` |
| Meeting prefetch ("a candidate too") | Considered and declined | Spec A8 (`design.md:149-152`): the prefetch never changes the Wi-Fi mode and runs on a radio its parent's session owns. See MINOR 2. |

## Spec requirements

- **A3, teardown body unchanged**: `WifiSession.cpp:23-28` has the same code as the removed blocks,
  still inside the `getMode() != WIFI_MODE_NULL` check (now `radioOnAtExit` in the policy).
- **A4, optional is the last data member**: I checked every header. `wifiSession` is the last
  non-static data member in all six. `ClockSyncActivity.h:26`, `OtaUpdateActivity.h:35`,
  `FontDownloadActivity.h:100`, `CatalogSearchActivity.h:130`, `MeetingDownloadActivity.h:87` and
  `SettingsActivity.h:178` are followed only by member functions or `static` members. It is therefore
  destroyed first, right after `onExit()`, as the spec requires.
- **A5, `emplace()` placement**: every call site matches the A5 table. Each `emplace()` is the first
  statement after the base `onEnter()` and comes before any `status()` check or `WiFi.mode(WIFI_STA)`.
- **A11, log lines**: one `LOG_DBG("WIFI", …)` per outcome (`WifiSession.cpp:17,20,23`).
- **A12, Settings**: `SettingsActivity.cpp:309-311` calls `emplace()` before
  `startActivityForResult`, and its handler calls `reset()`. I confirmed that the Pop path calls the
  handler after the picker is destroyed (`ActivityManager.cpp:106,119-127`). On a Replace, the
  engaged optional is destroyed along with Settings.
- **Architecture, pure policy header with no Arduino**: `WifiSessionPolicy.h`. The rule and the
  eight-row table match `design.md:215-227` exactly.
- **Header hygiene**: `WifiSession.h` does not include `<WiFi.h>`. `#include "SilentRestart.h"` is
  removed from all five `.cpp` files. The OTA `onExit()` comment now points to the session
  (`OtaUpdateActivity.cpp:89-90`).
- **Non-goals held**: the PR does not touch `main.cpp`, `MeetingWeekCache.*`,
  `CrossPointWebServerActivity.cpp` or `WifiSelectionActivity.cpp`. `grep -rn "silentRestart()"
  src/activities` finds only `CrossPointWebServerActivity.cpp:126`. The two "reboots" comments are
  left alone and listed as follow-ups, as A7 says.
- **Behaviour differences**: the PR body lists all four spec differences (`design.md:352-361`),
  worded to match.

## Scope

- **Nothing was reduced.** All five activities are converted, and the second half of the spec (A12)
  is implemented rather than deferred.
- **The only expansion is the Settings → Wi-Fi networks session.** Decision d1 authorised it
  (`design.md:13`), and the PR presents it as behaviour difference #1. The other new files are
  process documents under `docs/superpowers/`, which this pipeline produces for every PR.
- **`test/CMakeLists.txt` is not in the diff, as intended.** The one-line hand-off is in the PR body.
  The suite therefore does not run in CI until the orchestrator adds that line.

## Tests

`test/wifi_session/WifiSessionPolicyTest.cpp:11-43` has one case for each of the eight input
combinations, with no wildcard rows. The expected values come from the spec's table rather than from
the implementation. Each test is named for the behaviour it checks (for example
`ConnectionDroppedDuringSessionEndsTheHalfUpRadio`), and the review-0 regression (row 6) has its own
explaining comment. `WifiSession` itself cannot be built on the host because there is no `WiFi.h`
stub. The spec states this (`design.md:328-329`), and the wrapper is too thin to hold logic of its
own: it only reads the driver and dispatches on the policy's answer. The device steps in the PR body
cover the wrapper.

## Findings

### MINOR 1 — PR title differs from the plan's, with no explanation

The plan sets the title as `refactor: one Wi-Fi session helper for the network screens`
(`plans/2026-09-27-issue-109-plan.md`, "PR description material"). The PR is titled
`fix: leave wi-fi as each network screen found it`. The squash-merge title is what release-please
reads (CLAUDE.md, "CI and releases"), so this changes the changelog entry. It does not affect the
version bump, because `refactor` and `fix` both release here. `fix` is arguably the better type,
since d1 is a user-visible fix. However, the PR does not say the title was changed on purpose, and
the lowercase "wi-fi" does not match the "Wi-Fi" spelling used everywhere else, including every
commit subject. **Fix inline:** keep `fix:`, write "Wi-Fi", or return to the plan's title.

### MINOR 2 — the PR body does not say what happened to the meeting prefetch the issue named

Issue #109 names the `PREFETCHING_MEETINGS` state in `WifiSelectionActivity` as "a candidate too".
The spec declines it and gives reasons (A8, `design.md:149-152`), but the PR body never says so.
Someone checking the PR against the issue has to open the spec to see that this was a decision and
not an omission. **Fix inline:** add one line, for example: "Meeting prefetch: no session of its own.
It never changes the Wi-Fi mode and runs on its parent's session (spec A8)."

## Trailer

VERDICT: CLEAR
