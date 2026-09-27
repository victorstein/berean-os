# Issue #109 — research

Issue #109 asks for one RAII `WifiSession` that records whether Wi-Fi was already up on entry and
restores that state in its destructor. Five activities would use it, and the meeting prefetch inside
`WifiSelectionActivity` is named as a candidate. This note records how Wi-Fi bring-up and teardown
behave today. Every claim was read or run in this worktree at `dc97292e`.

## Files that own the behaviour

| Concern | Location |
|---|---|
| Clock sync | `src/activities/settings/ClockSyncActivity.cpp:19-58`; flag at `ClockSyncActivity.h:23` |
| OTA update | `src/activities/settings/OtaUpdateActivity.cpp:76-101`; success reboot at `:209`, `:239-240` |
| Catalog search (Buscar) | `src/activities/catalog/CatalogSearchActivity.cpp:77-86` (exit), `:328-352` (`ensureWifi`) |
| Meeting download | `src/activities/network/MeetingDownloadActivity.cpp:37-90` |
| Font download | `src/activities/settings/FontDownloadActivity.cpp:39-54` |
| Connect UI, auto-connect, meeting prefetch | `src/activities/network/WifiSelectionActivity.cpp` (onExit `:175-190`, prefetch `:639-695`) |
| "Silent restart" | declared `src/SilentRestart.h:1-8`, defined `src/main.cpp:160-201` |
| A sixth activity with the same teardown, not named in the issue | `src/activities/network/CrossPointWebServerActivity.cpp:109-130` |
| Deep-sleep Wi-Fi teardown | `src/main.cpp:266-285` |
| Why Wi-Fi mode costs battery | `lib/hal/HalPowerManager.cpp:39-43` — any `WiFi.getMode() != WIFI_MODE_NULL` forces power saving off |

`grep -rn -E 'WiFi\.|esp_wifi|WIFI_OFF|WIFI_STA|WIFI_AP' src lib` finds no other activity that
changes Wi-Fi mode. The issue's line ranges are accurate to within a few lines; FontDownload's is
`:39-54` rather than `:39-48`.

## The teardown is not "disconnect" — it is `silentRestart()`, and on this board that does not restart

Every one of the five activities (and the web-server activity) ends its session the same way:

```cpp
if (WiFi.getMode() != WIFI_MODE_NULL) {
  WiFi.disconnect(false);
  delay(30);
  silentRestart();
}
```

`silentRestart()` (`main.cpp:175-190`) was introduced by upstream commit `7acc31bc` ("silent-reboot on
wifi activity exit to clear heap fragmentation") to `ESP.restart()` and clear LWIP/netif heap
fragmentation. But it first calls `finishWifiSessionWithoutRestart()` under `#if FREEINK_CAP_TOUCH`
(`main.cpp:160-173`), which returns `true` whenever `BoardConfig::hasTouch()`:

```cpp
if (esp_sntp_enabled()) esp_sntp_stop();
WiFi.mode(WIFI_OFF);
delay(100);
return true;
```

On the X4 Pro:

- `FREEINK_CAP_TOUCH` is derived true from `FREEINK_DEVICE_X4PRO`
  (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:175-178`), and both envs pass
  `-DFREEINK_DEVICE_X4PRO=1` (`platformio.ini:169`, `:187`).
- The X4 Pro board entry configures a GT911 (`BoardConfig.h:1411`), so `hasTouch()`
  (`BoardConfig.h:1622`) is true.

So on this device the "restart" never happens. The effective teardown is
`disconnect(false)` → 30 ms → stop SNTP → `WiFi.mode(WIFI_OFF)` → 100 ms, and `onExit()` then
returns normally. The `ESP.restart()`, the `STR_LOADING_POPUP` overlay and the RTC reboot flag are
dead code for this build. Any design that keeps `silentRestart()` in the teardown is keeping a
function whose only live path here is "turn Wi-Fi off".

`silentRestart()` also returns immediately while `deepSleepInProgress` is set (`main.cpp:176`);
`enterDeepSleep()` sets it before `goToSleep()` runs the outgoing `onExit()` (`main.cpp:268-271`)
and then turns Wi-Fi off itself with `disconnect(true)` + `mode(WIFI_OFF)` (`main.cpp:282-285`).

## Current control flow, activity by activity

"Entry state" below is what the activity checks; "brings up" is what it does itself before handing
to `WifiSelectionActivity`; "tears down when" is the guard around the block above.

| Activity | Entry check | Brings up | Tears down when | Notes |
|---|---|---|---|---|
| ClockSync | `WiFi.status() == WL_CONNECTED` (`:24`) → skip selection | nothing itself; `WifiSelectionActivity` (`autoConnect=true`, `meetingPrefetch=false`, `:45-47`) | `shouldTearDownWifiOnExit && mode != NULL` (`:36`) | The only one that leaves Wi-Fi alone if it was already connected on entry. The flag is set only when it launched selection (`:29`). |
| OtaUpdate | none | `WiFi.mode(WIFI_STA)` unconditionally in `onEnter` (`:81`) | `mode != NULL` (`:96`) | Success path never reaches `onExit`: `SHUTTING_DOWN` → plain `ESP.restart()` in `loop()` (`:209`, `:239-240`). |
| CatalogSearch | lazily, per action: `ensureWifi` returns true if `WL_CONNECTED` (`:329`) | `WiFi.mode(WIFI_STA)` only when an action needs the network (`:333`) | `mode != NULL` (`:81`) | Browsing the cached index never turns the radio on. Tears down even if the radio was already up on entry. |
| MeetingDownload | `WiFi.status() == WL_CONNECTED` (`:58`) → run straight away | nothing itself; `WifiSelectionActivity` (`meetingPrefetch=false`, `:67-69`) | `mode != NULL` (`:75`) | **Tears down even when Wi-Fi was already connected on entry** — the opposite of ClockSync. |
| FontDownload | none | `WiFi.mode(WIFI_STA)` unconditionally in `onEnter` (`:41`) | `mode != NULL` (`:49`) | |
| CrossPointWebServer (not in the issue) | none | `WiFi.mode(WIFI_STA)` for Join (`:156`) or `WIFI_AP` for hotspot (`:199`) | `mode != NULL` (`:119`), with `softAPdisconnect(true)` instead of `disconnect(false)` in AP mode (`:120-124`) | Also stops DNS and mDNS first (`:115-116`). |

So the "slightly different" teardowns the issue mentions are really three differences:

1. **Whose Wi-Fi it is.** ClockSync respects a pre-existing connection; MeetingDownload and
   CatalogSearch check for one on entry but tear it down anyway; OTA and FontDownload never look.
2. **When the radio comes on.** OTA, FontDownload and web-server Join switch to STA in their own
   code; ClockSync and MeetingDownload rely on `WifiSelectionActivity` to do it
   (`startWifiScan` `WifiSelectionActivity.cpp:206`, `attemptConnection` `:482`); CatalogSearch waits
   for the first network action.
3. **AP versus STA** — only the web-server activity has an AP to take down.

The body of the teardown itself is byte-identical across the five.

### Can Wi-Fi actually be up when one of these is entered?

Launch sites (`grep -rn 'make_unique<(ClockSync|OtaUpdate|CatalogSearch|MeetingDownload|FontDownload)Activity>' src`):

- ClockSync ← `StatusBarSettingsActivity.cpp:183`
- OtaUpdate, FontDownload ← `SettingsActivity.cpp:322`, `:328`
- CatalogSearch ← `PublicationsActivity.cpp:239`
- MeetingDownload ← `MeetingsActivity.cpp:150` and **`CrossPointWebServerActivity.cpp:148`**

Every Wi-Fi activity turns the radio off in its own `onExit()`, so from the settings, publications
and meetings screens Wi-Fi is off on entry in practice. The one reachable nested case is the web
server: after a cancelled Join, `onWifiSelectionComplete(false)` (`CrossPointWebServerActivity.cpp:188-190`)
returns to the mode list with the radio still in `WIFI_STA`, not connected. Choosing Meeting
Publications then enters `MeetingDownloadActivity` with `getMode() == WIFI_STA` and
`status() != WL_CONNECTED`; it launches selection and on exit turns Wi-Fi fully off, then the parent
shows the mode list again (`:149`). The parent's comment at `:145-147` ("Its onExit restarts the
device once Wi-Fi has been used, so the mode list only comes back when it exits before Wi-Fi starts")
is false on this board, for the reason in the previous section.

This matters for the spec: "restore what was there on entry" has to define what "was up" means.
`getMode() != WIFI_MODE_NULL` and `status() == WL_CONNECTED` give different answers in exactly the
nested case above, and the current code uses `status()` for the entry check but `getMode()` for the
teardown guard.

## Activity lifecycle — where a destructor would run

`ActivityManager::loop()` (`src/activities/ActivityManager.cpp:72-167`):

- **Pop** (`finish()`): `exitActivity(lock)` → `onExit()` then `currentActivity.reset()`
  (`:176-182`), under a `RenderLock`. The parent's result handler runs afterwards with the lock
  released (`:118-126`).
- **Replace** (`onGoHome()`, `goToSleep()`, Home gesture → `goHome()` at `:75-79`): the current
  activity gets `onExit()` + destroy, then every stacked activity gets `onExit()` and is destroyed
  by `pop_back()`, child first (`:145-149`).

So a member RAII object in one of these activities is destroyed **after** that activity's
`onExit()` body, under the render lock, and in the Replace case after its child's teardown. Home
from inside `WifiSelectionActivity` (including the prefetch's Home at
`WifiSelectionActivity.cpp:582-588`) still reaches the parent's `onExit()` through the stack loop, so
teardown is not skipped today on that path.

Activities are constructed fresh for each launch (`std::make_unique<...>` at every launch site), so
per-instance entry state has no reuse problem.

## The meeting prefetch

`prefetchMeetingWeekIfDue()` (`WifiSelectionActivity.cpp:639-665`) runs inside
`checkConnectionStatus()` once the link is up and resolves a name (`:543-590`), before the activity
reports success to its parent. It never changes the Wi-Fi mode: `src/network/MeetingWeekPrefetch.cpp`
has no `WiFi` reference (`grep -n 'WiFi\|wifi'` returns nothing), and `WifiSelectionActivity::onExit()`
deliberately leaves the connection alone (`:185-187`) because the parent owns it. The radio the
prefetch uses belongs to whichever parent launched selection — which is why ClockSync and
MeetingDownload pass `meetingPrefetch=false`. The prefetch has no teardown of its own to replace; the
only link is that it borrows the parent's session. `MeetingWeekPrefetch::record` writes through
`MeetingWeekCache` (`MeetingWeekPrefetch.cpp:97`), which another task in this batch owns and this
task must not edit.

## Installed versions

```
$ /Volumes/stein/.platformio/penv/bin/pio --version
PlatformIO Core, version 6.1.19
$ grep '^platform' platformio.ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
$ grep -m1 '"version"' ~/.platformio/packages/framework-arduinoespressif32/package.json
  "version": "3.3.7",
$ grep -m1 '"version"' ~/.platformio/packages/framework-arduinoespressif32-libs/package.json
  "version": "5.5.0+sha.87912cd291"
```

The Wi-Fi calls in use exist with these signatures in that core
(`framework-arduinoespressif32/libraries/WiFi/src/`): `static bool mode(wifi_mode_t)` and
`static wifi_mode_t getMode()` (`WiFiGeneric.h:106-107`); `bool disconnect(bool wifioff = false,
bool eraseap = false, unsigned long timeoutLength = 100)` and `wl_status_t status()`
(`WiFiSTA.h:166`, `:182`). `WiFi.disconnect(false)` therefore means "drop the association, leave the
radio on"; the radio is turned off only by `silentRestart()`'s `WiFi.mode(WIFI_OFF)`.

## Nearest existing examples

- **RAII scope helpers in this tree**: `HalPowerManager::Lock` (`lib/hal/HalPowerManager.h:47-64`,
  ctor/dtor `HalPowerManager.cpp:141-164`) records whether it took the lock (`valid`) and releases
  only what it took — the same shape as "record whether Wi-Fi was up, restore only what you changed".
  Non-copyable and non-movable. `RenderLock` (`src/activities/RenderLock.h`) is the other; neither
  lives in `src/network`.
- **The one activity that already does "restore what you found"**: ClockSync's
  `shouldTearDownWifiOnExit` (`ClockSyncActivity.cpp:24-30`, `:36`).
- **Host-testing a device-bound behaviour**: nothing includes `WiFi.h` on the host;
  `test/stubs/` has `Arduino.h`, `HalDisplay.h`, `HalStorage*`, `Logging.h` and no Wi-Fi stub. The
  repo's pattern for testing the logic behind a device-bound helper is to split a pure core out of
  it: `src/activities/PostedMessageQueue.h` is the Arduino-free core of `PostedMessage.cpp`, tested
  by `test/posted_message/PostedMessageQueueTest.cpp` and wired in `test/CMakeLists.txt:79`. The
  net-dev guide names `PubMediaJson` / `WolWeekScan` as the same idea for `src/network`. A
  `WifiSession` that calls `WiFi.*` directly could only be tested on the device; its entry/exit
  decision could be host-tested if it is expressed as a pure function of the recorded entry state.
- **The last change of this kind**: `7acc31bc` added the identical `getMode()` → `disconnect` →
  `silentRestart()` block to each Wi-Fi activity's `onExit()` separately, which is the duplication
  this issue removes.

## Scope and tier

The helper touches `src/activities` (five activity files, ui surface) plus wherever `WifiSession`
lives, and changes no on-disk format, store, or cross-surface contract. The review tier is already
`heavy`, the highest, so no tier change is needed. `test/CMakeLists.txt` is a shared append point;
if a host test is added, its `add_subdirectory` line goes in the PR description for the
orchestrator to apply.

## Questions the spec has to answer

1. **What "was up" means.** `status() == WL_CONNECTED` (what the entry checks use) or
   `getMode() != WIFI_MODE_NULL` (what the teardown guards use). They differ in the web-server nested
   case above.
2. **MeetingDownload's behaviour change.** Adopting "restore the entry state" makes MeetingDownload
   stop tearing down a connection it found already up. That case is unreachable from today's launch
   sites except through the web server's STA-not-connected path, but it is still a change to list.
3. **Keep `silentRestart()` or call `WiFi.mode(WIFI_OFF)` directly.** On the X4 Pro they do the same
   thing plus/minus the SNTP stop; keeping `silentRestart()` preserves the deep-sleep short-circuit and
   the SNTP stop in one place.
4. **Whether the web-server activity joins.** It has the same block but is not named in the issue,
   and its AP teardown differs.
5. **Where the helper lives.** `src/network` (this surface) or `src/activities` next to `RenderLock`.
