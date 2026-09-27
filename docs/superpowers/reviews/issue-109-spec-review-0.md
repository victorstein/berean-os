Tier: heavy

# Issue #109 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-27-issue-109-design.md`
Research: `docs/superpowers/research/2026-09-27-issue-109-research.md`
Issue: `gh issue view 109 --repo victorstein/berean-os`
Code read at `HEAD` (`b2872f94`); `git diff --stat dc97292e HEAD -- src lib` is empty, so the spec's
`dc97292e` line references apply unchanged.

## Findings

### 1. BLOCKER: A9 is false. A connection is up on entry on a reachable path, so the Goal's "does exactly what it does now" does not hold

**Claim.** A9 (spec :136-142): "No launch site reaches them with a connection up … every Wi-Fi
activity already turns the radio off in its `onExit()`." The Goal (:43-44) says "On every path
reachable today the device does exactly what it does now." Research :23 and :104-105 say the same.

**Problem.** One Wi-Fi activity does not turn the radio off. Settings → System → *Wi-Fi networks*
launches a standalone `WifiSelectionActivity`. That activity connects and deliberately leaves the
connection to its parent. Here the parent is `SettingsActivity`, which has no teardown at all. The
user comes back to Settings with `WiFi.status() == WL_CONNECTED` and the radio in STA. From that
state, every one of the four activities A9 calls unreachable can be entered while connected:

- *Check for updates* (OTA) and *Manage fonts* (FontDownload) sit on the same Settings screen.
- Leaving Settings through Home does not tear down Wi-Fi either, so Meetings → download
  (MeetingDownload) and Publications → Buscar (CatalogSearch) are also entered connected.
- So is File transfer → Meeting Publications.

Today all four turn the radio off when they exit. Under the spec, `connectedOnEntry == true`
resolves to `LeaveConnected`, so the radio stays on. `HalPowerManager` keeps CPU power saving off
until deep sleep or some later session ends it. The spec lists this as behaviour difference #1 and
calls it "unreachable from current launch sites". It is reachable, and it is the only thing that
currently turns off a connection left by Settings → Wi-Fi networks. So the change removes the
incidental cleanup that bounds that pre-existing leak. Nothing in the device test plan exercises it.

This is also a scope and intent question only the human can settle. The issue asks the helper to
"restore that state", which taken literally means leaving that connection up. The issue's own
motivation is battery ("Any activity that forgets to leave Wi-Fi as it found it costs battery").
Settings → Wi-Fi networks is exactly that kind of forgetting activity, a sixth site the research
missed. Its grep at research :23 only matched direct `WiFi.` calls, and `SettingsActivity` has none.

**Evidence.**
- `src/activities/settings/SettingsActivity.cpp:75`: `systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));`. The entry is unconditional.
- `SettingsActivity.cpp:308-309`: `case SettingAction::Network: startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), nullptr);`. There is no result handler.
- `SettingsActivity.cpp:178-179`: `onExit()` is only `Activity::onExit();`. `grep -n "WiFi\|silentRestart" SettingsActivity.cpp` finds no Wi-Fi calls.
- `src/activities/network/WifiSelectionActivity.cpp:185-187`: "We do NOT disconnect WiFi here - the parent activity … manages WiFi connection state."
- A successful connect finishes with the link up: `WifiSelectionActivity.cpp:601`, `:782`, `:785`, `:77` all call `onComplete(true)`, which leads to `finish()` (`:1239-1246`).
- Launches from the same screen: `SettingsActivity.cpp:322` (OTA) and `:328` (FontDownload). Other launches: `MeetingsActivity.cpp:150`, `PublicationsActivity.cpp:239`, `CrossPointWebServerActivity.cpp:148`.
- Power-saving effect: `lib/hal/HalPowerManager.cpp:39-43`.

**Concrete fix.** Get the human's decision, then make the spec match it. There are three options:

- **(a)** Close the leak at its source. Give the standalone Settings → Wi-Fi networks launch its own
  `WifiSession`, for example opened by `SettingsActivity` around the `SettingAction::Network` launch
  and ended in its result handler. Then "connected on entry" really is unreachable for the four
  activities and A9 becomes true. This widens scope to a sixth site, so it needs sign-off. It also
  has to answer whether Wi-Fi networks is meant to leave the device online.
- **(b)** Accept the literal issue semantics. Rewrite A9 and behaviour difference #1 as a
  *reachable* change, say that Wi-Fi then stays on until deep sleep, and add a device step:
  Settings → Wi-Fi networks → connect → Check for updates → back; observe the radio state and
  whether "Going to low-power mode" appears.
- **(c)** Keep unconditional teardown for OTA, FontDownload, CatalogSearch and MeetingDownload, so
  that only ClockSync keeps "leave it alone". This departs from the issue's "restore" wording.

Whichever is chosen, correct research :23 and :104-105 and spec A9 so they no longer claim that
every Wi-Fi activity turns the radio off.

### 2. MAJOR: the `connectedOnEntry == true → LeaveConnected` row ignores the state at exit and leaves a half-up radio, the state A2 says must not be kept

**Claim.** In the policy table (spec :170-174), `connectedOnEntry = true` with `radioOnAtExit = any`
gives `LeaveConnected`. A2 (:69-73) says a "radio on, not connected" state must not be preserved,
because it keeps power saving off for no purpose.

**Problem.** OTA and FontDownload do not check for a connection on entry. They always launch
`WifiSelectionActivity`, and `WifiSelectionActivity` always drops an existing association before it
does anything else:

- With saved credentials it auto-connects through `attemptConnection()`, which calls
  `WiFi.disconnect(true, true)`.
- Otherwise it calls `startWifiScan()`, which calls `WiFi.disconnect()`.

If the user then backs out, the parent calls `finish()` and the session sees
`connectedOnEntry == true`, so it returns `LeaveConnected`. The radio is left in `WIFI_STA`,
**not connected**, with power saving disabled. The user can back out in three places: Back while
scanning, Back during auto-connect, or Back from the network list. Today that radio is turned off.

The case is reachable through the Finding 1 path unless option (a) or (c) removes it. Under option
(b) it is a live defect. Either way, "leave it alone" should mean "a connection is still there to
leave".

**Evidence.**
- `OtaUpdateActivity.cpp:81-86` and `FontDownloadActivity.cpp:41-43`: unconditional `WiFi.mode(WIFI_STA)` and a `WifiSelectionActivity` launch.
- `WifiSelectionActivity.cpp:158-167`: auto-connect → `tryAutoConnectCredential` (`:408-423`) → `attemptConnection` (`:474-483`, `WiFi.disconnect(true, true);  // Abort any in-progress SDK auto-connect…`).
- `:194-207`: `startWifiScan` runs `WiFi.mode(WIFI_STA); WiFi.disconnect();`.
- Cancels: `:700-702` (Back while scanning), `:717-719` (Back during auto-connect, after `WiFi.disconnect()`), `:864-865` (Back from the network list).
- Parent response: `OtaUpdateActivity.cpp:16-19` and `FontDownloadActivity.cpp:57-59` both `finish()`.

**Concrete fix.**
- Make the exit decision also depend on whether the link is still up. For example:
  `onExit(bool connectedOnEntry, bool connectedAtExit, bool radioOnAtExit)` returns
  `LeaveConnected` only when `connectedOnEntry && connectedAtExit`. Otherwise it returns
  `EndSession` when `radioOnAtExit`, and `Nothing` in every other case.
- Read `WiFi.status() == WL_CONNECTED` in the destructor for `connectedAtExit`.
- Extend the host test to every input combination, including "connected on entry, dropped by
  selection, radio still on", which must give `EndSession`.
- ClockSync is unaffected: when connected it never launches selection (`ClockSyncActivity.cpp:24-27`).

### 3. MINOR: the testing strategy's "all four rows" does not match a three-row table

**Claim.** Spec :247-248: "covering all four rows of the table".

**Problem.** The table at :170-174 has three rows, one of them the `any` wildcard. The test means
four input combinations, but a TDD reader could write three cases. After the Finding 2 fix, the
input space is eight combinations.

**Evidence.** Spec :170-174 against :247-248.

**Concrete fix.** List the input combinations the test enumerates explicitly, and state that the
wildcard row is expanded.

### 4. MINOR: a second stale "reboots" comment sits next to the path this issue touches

**Claim.** A7 (:126-127) lists the false comment at `CrossPointWebServerActivity.cpp:145-147` as a
follow-up.

**Problem.** `SettingsActivity.cpp:313-314` makes the same false claim ("Once WiFi is up the
activity's onExit reboots instead"). On this board, `silentRestart()` returns through
`finishWifiSessionWithoutRestart()` (`src/main.cpp:160-178`).

**Concrete fix.** Add it to the same follow-up line in the PR. Do not change it in this diff, which
keeps it consistent with A7.

## Checked and sound

- **Teardown block locations.** The five blocks match exactly: `ClockSyncActivity.cpp:36-40`,
  `OtaUpdateActivity.cpp:96-100`, `CatalogSearchActivity.cpp:81-85`,
  `MeetingDownloadActivity.cpp:75-79`, `FontDownloadActivity.cpp:49-53`. Each block is the last
  statement of its `onExit()`.
- **`silentRestart()` on the X4 Pro.** On this board it only turns the radio off, with no reboot:
  `main.cpp:175-178` and `:160-172`, and `BoardConfig.h:175-178` shows `FREEINK_CAP_TOUCH` covers
  `FREEINK_DEVICE_X4PRO`. The deep-sleep short-circuit is at `main.cpp:176` and `:268-271`. A3's
  reasons for keeping `silentRestart()` hold.
- **A4 lifecycle ordering.** `ActivityManager::exitActivity` runs `onExit()` and then `reset()`
  (`ActivityManager.cpp:176-182`). The Replace path runs `onExit()` and then `pop_back()` for each
  stacked activity (`:145-149`). No activity in any of the five hierarchies declares a destructor;
  the only one is `Activity.h:30`, which is `= default`. So a last-declared `std::optional` member
  is destroyed right after `onExit()` and before every other member, under the same `RenderLock`.
  No other code calls `onExit()`: `grep` finds only `ActivityManager.cpp:148` and `:179`.
- **A4 rationale for `emplace()`.** `pushActivity` can discard a pending activity that was never
  entered (`ActivityManager.cpp:253-257`). `emplace()` on a non-copyable, non-movable type is
  well-formed.
- **Calling `WiFi.status()` before Wi-Fi was ever started.** This is safe on the installed core, as
  CatalogSearch, OTA and FontDownload would now do at entry. `STAClass::status()` returns the cached
  `_status` (`framework-arduinoespressif32/libraries/WiFi/src/STA.cpp:241-243`), initialised to
  `WL_STOPPED` (`:231`) and set back to `WL_STOPPED` on STA stop (`:118`). It makes no driver call.
- **A8.** `MeetingWeekPrefetch.cpp` has no `WiFi.*` reference. The prefetch's Home path is
  `WifiSelectionActivity.cpp:582-587`.
- **Other citations.** The A10/A11 placement, the test wiring (`test/CMakeLists.txt:79`,
  `test/posted_message/CMakeLists.txt`) and the lack of a `WiFi.h` stub in `test/stubs/` all match
  the tree.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
