Tier: heavy

# Issue #109 spec review, pass 1

Spec: `docs/superpowers/specs/2026-09-27-issue-109-design.md` (at `edc0cf97`)
Research: `docs/superpowers/research/2026-09-27-issue-109-research.md`
Issue: `gh issue view 109 --repo victorstein/berean-os`
Previous pass: `docs/superpowers/reviews/issue-109-spec-review-0.md`

`git diff --stat dc97292e HEAD -- src lib test` is empty, so the spec's `dc97292e` line references
apply to the tree as checked out.

## Did the pass-0 fixes land correctly?

This pass focused first on whether the pass-0 fixes were applied correctly and in full.

- **BLOCKER 1 → A12 (Settings → Wi-Fi networks holds a session).** This is correctly applied.
  - `SettingsActivity.cpp:308-309` is the only `nullptr`-handler launch of `WifiSelectionActivity`.
    `grep -rn 'WifiSelectionActivity>' src` lists seven launch sites. The other six belong to the
    five activities and the web server, and each of those has its own teardown.
  - `grep -rn 'WiFi\.\(mode\|begin\|…\)' src lib` finds no other code that brings the radio up.
    The only other code that reads Wi-Fi state is `HalPowerManager.cpp:39` and
    `HalClock.cpp:84`, and neither is an owner.
  - So once A12 is in, A9's reachability argument holds. I checked the web-server leg too.
    `onWifiSelectionComplete(false)` (`CrossPointWebServerActivity.cpp:188-190`) is reached only
    through `isCancelled`. `WifiSelectionActivity` reports a cancel only from SCANNING (`:702`),
    AUTO_CONNECTING (`:719`) and NETWORK_LIST (`:865`), never from SAVE_PROMPT or any other
    connected state. So the mode list cannot come back with a live association.
- **A12 lifecycle.** The spec's description matches the manager's code:
  - The Pop path runs `exitActivity(lock)` at `ActivityManager.cpp:106`, then restores the parent
    at `:116-117`, then moves the handler and calls it unlocked at `:126-127`.
  - The Replace path runs `exitActivity` at `:145`, then for each stacked activity
    `stackActivities.back()->onExit(); stackActivities.pop_back();` at `:148-149`.
  - `WifiSelectionActivity` does not override `handleHomeGesture`, so Home inside the picker is a
    `goHome()` Replace (`:75-79`).
  - In the deep-sleep case, `goToSleep` runs `loop()` synchronously (`:230-233`) after
    `deepSleepInProgress` is set (`main.cpp:270-271`). The Settings session's `silentRestart()`
    therefore short-circuits exactly as the five activities' teardowns do today.
  - `rowItems_` is the last non-static data member of `SettingsActivity` (`SettingsActivity.h:173`).
    Only static members follow it.
- **MAJOR 2 → the three-input policy.** This is correctly applied.
  - The cancel paths leave the radio in STA and not associated: `startWifiScan` at
    `WifiSelectionActivity.cpp:206-207` and `attemptConnection` at `:482-483`.
  - The core's `status()` reports that state. It returns the cached `_status`
    (`framework-arduinoespressif32/libraries/WiFi/src/STA.cpp:241-243`), which the STA
    disconnect, stop and got-IP events keep up to date (`STA.cpp:113-177`). As a result, row 6
    (`EndSession`) is what a backed-out picker produces.
  - The rule text at spec :214-215 and the eight-row table at :217-226 agree row for row.
- **MINOR 3 (eight explicit cases)** and **MINOR 4 (the `SettingsActivity.cpp:313-314` follow-up)**
  are both applied (spec :315-317, :363-364).

## Other claims checked and found sound

- **The five teardown blocks.** Each is at the cited lines and is the last statement of its
  `onExit()`. Each `.cpp` includes `SilentRestart.h` only for that block: `grep -rn silentRestart src`
  finds one call per file, plus the web server's.
- **Destructors (A4).** No class in any affected hierarchy declares a destructor. That covers
  `Activity.h:30` (`= default`), `UiListActivity`, `UiTabListActivity`, `UiAppHost` and all six
  leaf classes. So a last-declared `std::optional` runs right after `onExit()`, before every other
  derived member and before the bases.
- **A3.** `silentRestart()` on this board reaches `finishWifiSessionWithoutRestart()`
  (`main.cpp:160-173`, `:175-178`). The SNTP stop is at `:165-167`. The deep-sleep short-circuit is
  at `:176`.
- **SNTP and the A12 early teardown.** `HalClock::syncFromNTP` (`HalClock.cpp:81-95`) is
  synchronous and runs inside `checkConnectionStatus` (`WifiSelectionActivity.cpp:566-570`), before
  the picker finishes. Ending the Settings session in the result handler therefore cannot cut off a
  first-connection clock sync. The same holds for the meeting prefetch (`:582`), as the spec says.
- **Launch sites (A9).** The command
  `grep -rnE 'make_unique<(ClockSync|OtaUpdate|CatalogSearch|MeetingDownload|FontDownload)Activity>' src`
  returns exactly the six sites the spec lists.
- **Test wiring.** `add_subdirectory(posted_message)` is at `test/CMakeLists.txt:79`, and
  `test/posted_message/CMakeLists.txt` has the shape the spec describes. `test/stubs/` has no
  `WiFi.h`, so the split into a pure policy core is necessary.
- **Remaining citations.**
  - `LOG_LEVEL=0` is at `platformio.ini:190`.
  - `"Going to low-power mode"` is at `HalPowerManager.cpp:50`.
  - `"WiFi stopped without restart on touch device"` is at `main.cpp:170`.
  - The OTA success path, `ESP.restart()` in `loop()`, is at `OtaUpdateActivity.cpp:239-240`.
  - The `std::optional` precedent is at `WifiCredentialStore.h:65-66`.

## Findings

### 1. MINOR: the research note's "Questions" section still says the connected-on-entry case is unreachable

**Claim.** The spec's revision history (:13) says "The research note's two wrong claims are
corrected in place."

**Problem.** The two paragraphs that review 0 named (research :23-28 and :107-118) were corrected.
A third copy of the same wrong claim was not. It sits in "Questions the spec has to answer", item 2,
at research :212-214: "That case is unreachable from today's launch sites except through the web
server's STA-not-connected path". There are two problems with that sentence:

- Before A12, the case was reachable through Settings → Wi-Fi networks. This was review 0's
  BLOCKER.
- The web-server path is not an example of the case at all, because it enters MeetingDownload
  unconnected (research :113-115).

So the research note now contradicts itself. The spec itself is not affected.

**Evidence.** Research :212-214, compared with the corrected text at research :107-111.

**Concrete fix.** Rewrite item 2 to say the case was reachable only through Settings → Wi-Fi
networks, which A12 closes, and drop the web-server clause.

### 2. MINOR: A12's `ActivityManager` line references are one line off

**Claim.** A12 (spec :178-179) cites the picker's onExit + destroy as `ActivityManager.cpp:103-106`
and Settings' handler as `:118-126`.

**Problem.** The handler block runs from `:119` (`if (currentActivity->resultHandler)`) to `:127`
(`handler(pendingResult);`). The unlock is at `:126`, and the spec's own next bullet cites that
correctly. Lines `:103-104` are `ActivityResult pendingResult = …` and a blank line. The mechanism
the spec describes is right; only the ranges are off.

**Evidence.** `grep -n "exitActivity(lock)\|lock.unlock\|handler(pendingResult)" src/activities/ActivityManager.cpp`
gives `106`, `111`, `126`, `127`, `145`, `159`.

**Concrete fix.** Cite `:106` for the pop's `exitActivity` and `:119-127` for the handler call.

VERDICT: CLEAR
