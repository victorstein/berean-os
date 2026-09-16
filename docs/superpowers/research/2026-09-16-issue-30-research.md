# Issue #30 — what the panel actually does on the first paint after a wake

Static reading of `src/`, `lib/` and `freeink-sdk/` on `ded48d17`. No device was
involved; every optical claim below is about what the driver sends on the wire,
not about what the glass looks like.

`freeink-sdk` is an uninitialised submodule in a fresh worktree. Nothing under
it can be read — or built — before
`git submodule update --init --depth 1 freeink-sdk`.

---

## 1. The flag's path is the ordinary wake

`needsWakeRefresh` is set in one place, the `else` of the Quick Resume branch
(`src/main.cpp:522,538-541`). The sleep frame file exists only when the sleep
mode was Quick Resume (`src/main.cpp:255,270-271`); `sleepScreen` defaults to
`DARK` (`src/CrossPointSettings.h:207`). So on a default device every wake takes
the `else` and the flag is **true** — this is not a rare path.

`enterDeepSleep` always clears `showBootScreen` (`src/main.cpp:261`), so a
power-button wake is always `BootResume::SplashlessWake`
(`src/main.cpp:461-463`).

On that path nothing paints between `display.begin()`
(`setupDisplayAndFonts`, `src/main.cpp:467`) and `LauncherActivity::render`'s
`renderer.displayBuffer()` (`src/activities/launcher/LauncherActivity.cpp:473`).
Checked: `Activity::onEnter` does not paint (`src/activities/Activity.cpp:5`);
neither `Epub::load` nor `generateThumbBmp` touches the renderer, so the
thumbnail path in `resolveTargets` paints nothing despite the comment at
`LauncherActivity.cpp:133-137`; the migration screen paints only when a
migration is pending (`src/main.cpp:474,484`). **The launcher's render is the
first panel paint after `begin()`.**

The panel is showing a clean frame, not residue: `SleepActivity` paints with
`HALF_REFRESH` (`src/activities/boot_sleep/SleepActivity.cpp:655,689,861,867`).

## 2. That first paint is already an absolute clean on all three X4 Pro drivers

`FREEINK_DEVICE_X4PRO` links SSD1677, UC8179 and UC8279_X4
(`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:101,129-130`); the
boot probe picks between them (`XteinkDetect.cpp:313-364`). In single-buffer
mode `FreeInkDisplay::displayBuffer` hands the driver `prev = nullptr`
(`FreeInkDisplay.cpp:578`).

**SSD1677 batch.** `initController` arms `_needsInitialFull =
(_cfg.fullSeqOverride != 0)` (`Ssd1677Driver.cpp:220`), and the X4 Pro takes
`ssd1677DefaultConfig`, whose `fullSeqOverride` is `0xF7` and `halfSeqOverride`
`0xD7` (`Ssd1677Driver.cpp:60-62`, board selection at `Ssd1677Driver.cpp:708-718`
— the fast-DU shortcut is opt-in and `platformio.ini` does not define it). The
one-shot rearms on **every** `begin()`, including a seamless wake:
`Ssd1677Driver` has no `skipInitialResync` override, so `HalDisplay::begin`'s
seamless call (`lib/hal/HalDisplay.cpp:25`) reaches the base no-op
(`PanelDriver.h:161`). The first paint then forces `RefreshMode::Half`
regardless of what the caller asked (`Ssd1677Driver.cpp:427-440`) — a
non-differential update that writes both RAM planes
(`Ssd1677Driver.cpp:463-465`). The SDK states the intent itself at
`Ssd1677Driver.cpp:686-690`: deep sleep discards controller RAM, "initController()
re-arms `_needsInitialFull`, so the first paint is an absolute clean anyway."

**UC8179 batch.** The partial path requires `_oldPlaneValid`
(`Uc8179Driver.cpp:293`), which is `false` on a freshly constructed driver
(`Uc8179Driver.h:137`) — wake from deep sleep is a chip reset, so it always is.
The non-fast branch seeds the OLD plane white and runs the OTP waveform with no
`PARTIAL_IN` (`Uc8179Driver.cpp:297-308,326,345`). `skipInitialResync` only
clears `_needFullClear` (`Uc8179Driver.cpp:391`), which is not what gates this.

**UC8279 batch.** Same shape: `fast = (mode != Full) && !_needFullClear &&
_oldPlaneValid` with `_oldPlaneValid` defaulting false
(`Uc8279X4Driver.cpp:179`, `Uc8279X4Driver.h:112`), white-seeded full flash
otherwise (`Uc8279X4Driver.cpp:182-190`).

**Consequence.** On this device, `renderer.displayBuffer()` with its
`FAST_REFRESH` default does **not** produce a differential paint over the sleep
image. The comment at `src/main.cpp:453-456` — "the first paint is FAST_REFRESH
(~500ms) over the retained frame" — describes the X3, whose initial-full-sync
counter `skipInitialResync` really does defuse (`Uc8253X3Driver.cpp:491`). It is
wrong for all three X4 Pro drivers.

## 3. The one configuration where the symptom is reachable

`GfxRenderer::displayBuffer` passes `fadingFix` as the driver's `turnOffScreen`
argument (`lib/GfxRenderer/GfxRenderer.cpp:1721`), and the SSD1677 promotion
block is gated on `if (!turnOff)` (`Ssd1677Driver.cpp:427`). With `turnOff`
true the one-shot is neither applied nor consumed, so a `FAST_REFRESH` stays a
true DU against a RED plane that holds nothing resembling the sleep frame: deep
sleep mode 0x03 discards controller RAM (`Ssd1677Driver.cpp:689-692`) and
`initController` overwrites both planes with the controller's auto-write pattern
(`Ssd1677Driver.cpp:208-214`). A DU cannot clear what is physically on the
glass. That is the issue's described failure, and it is the only way to reach it
here.

`fadingFix` defaults to 0 (`src/CrossPointSettings.h:285`) and is hidden from
the X4 Pro settings UI (`src/activities/settings/SettingsActivity.cpp:54`), but
it stays in the shared list for the web settings API, so a persisted 1 is
possible. `loop()` applies it (`src/main.cpp:610`) before
`activityManager.loop()` (`src/main.cpp:746`) drives the launcher's `onEnter` and
first render, so the value is live for that paint. UC8179/UC8279 are unaffected:
their gate ignores `turnOff`.

An explicit `HALF_REFRESH` is immune to this, because `mode != Fast` takes the
non-differential branch before any `turnOff` consideration.

## 4. What the change costs, per batch

- SSD1677: **nothing changes on the wire** in the default configuration. The
  first paint is forced to `Half` whatever the caller asks
  (`Ssd1677Driver.cpp:439`). The change only matters when `fadingFix` is on.
- UC8179 / UC8279: both branches run the full OTP waveform (`tsset` full, no
  `PARTIAL_IN`: `Uc8179Driver.cpp:326,345`). Only the OLD-plane seed differs:
  white (`Uc8179Driver.cpp:302-308`) versus the target's complement under `scrub`
  (`Uc8179Driver.cpp:298-301`). Same duration; no user-visible difference
  predicted, and none verifiable without the device.

Neither costs anything on other paths: `cleanInitialRefresh` is false everywhere
but `src/main.cpp:571`.

## 5. The "~20 call sites" constraint rests on a miscount

`goHome` has five call sites (`src/main.cpp:566,571`,
`src/activities/ActivityManager.cpp:81,114`, `src/activities/Activity.cpp:13`).
Exactly one passes either parameter: `src/main.cpp:571`. The ~20 figure is the
count of `Activity::onGoHome(...)` calls — 20 of them across the reader,
settings, catalog, web server and download activities — and **not one passes an
argument**, so `initialMenuItem` is `HomeMenuItem::NONE` at every call in the
firmware.

The constraint's conclusion (leave the signature alone) still stands on its own
merits; its stated reason does not. A single-call-site parameter could equally
be passed straight to the `LauncherActivity` constructor at
`ActivityManager.cpp:254`.

`HomeActivity` is confirmed dead: zero instantiations anywhere in `src/`
(only `isHomeActivity()` on the launcher, `LauncherActivity.h:31`).

## 6. Bearing on verification

The brief asks for a hardware check for ghosting behind the launcher. On a
default device that check cannot distinguish before from after, because the
driver already cleans the first paint either way. A difference is observable
only on an SSD1677 unit with `fadingFix` persisted to 1. Anyone reporting "no
ghosting after the fix" without also checking the unedited build has measured
nothing.
