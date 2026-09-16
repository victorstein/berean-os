# The launcher asks for its own clean refresh on wake

**Date:** 2026-09-16
**Status:** Design v1
**Target:** bereanOS, `x4pro` build target (ESP32-S3, 8 MB PSRAM; SSD1677, UC8179
or UC8279 panel by production batch)
**Delivery:** `origin` (victorstein/berean-os), branch `fix/30-launcher-wake-refresh`
**Issue:** #30
**Builds on:** `docs/superpowers/research/2026-09-16-issue-30-research.md` (`2b71d23a`)
**Modelled on:** `docs/superpowers/specs/2026-09-13-bible-chapter-status-and-hint-design.md`
— the nearest existing spec in shape: a small, two-file defect fix with a named
reference implementation already in the tree.

## Problem

`ActivityManager::goHome` takes a `cleanInitialRefresh` argument and discards it
(`src/activities/ActivityManager.cpp:252-253`). The one caller that sets it is
the wake path: with no Quick Resume frame on the card, `main.cpp` sets
`needsWakeRefresh = true` and records why — "the panel still physically shows the
sleep image" (`src/main.cpp:538-541`) — then passes it at
`src/main.cpp:571`. `LauncherActivity::render` ends with a bare
`renderer.displayBuffer()` (`src/activities/launcher/LauncherActivity.cpp:473`),
which defaults to `FAST_REFRESH` (`lib/GfxRenderer/GfxRenderer.h:189`). The only
code that ever honoured the flag is `HomeActivity`
(`src/activities/home/HomeActivity.cpp:306`), which has zero instantiations —
dead code owned by #29.

So the firmware states an intent it does not carry out. Three things follow, and
the research note separates them because they have different weights:

1. **The ghosting the issue describes is not reachable in the default
   configuration.** All three X4 Pro panel drivers force the first paint after
   `begin()` to be non-differential regardless of the requested mode: SSD1677
   re-arms `_needsInitialFull` in `initController`
   (`freeink-sdk/.../Ssd1677Driver.cpp:220,427-440`), UC8179 and UC8279 gate their
   partial path on an `_oldPlaneValid` that is false after a chip-reset wake
   (`Uc8179Driver.cpp:293`, `Uc8179Driver.h:137`; `Uc8279X4Driver.cpp:179`,
   `Uc8279X4Driver.h:112`). Research §2.
2. **There is exactly one configuration where it is reachable.** `GfxRenderer`
   passes `fadingFix` as the driver's `turnOffScreen`
   (`lib/GfxRenderer/GfxRenderer.cpp:1721`) and the SSD1677 promotion block is
   gated on `if (!turnOff)` (`Ssd1677Driver.cpp:427`). On an SSD1677 unit with
   `fadingFix` persisted to 1 — default 0 (`src/CrossPointSettings.h:285`),
   hidden from the X4 Pro settings UI
   (`src/activities/settings/SettingsActivity.cpp:54`), still writable through
   the web settings API — the first post-wake paint stays a true differential
   and cannot clear the glass. Research §3.
3. **The correctness of that paint currently lives in a pinned submodule.**
   Nothing in `src/` requests the clean; the firmware gets one because three
   driver internals happen to arrange it. `freeink-sdk` is a submodule this
   repo does not own, and the property is stated nowhere in its public API —
   `HalDisplay::begin`'s own comment claims the opposite for the seamless path
   (`lib/hal/HalDisplay.cpp:22-26`, and `src/main.cpp:453-456`).

## Goal

The launcher requests the clean refresh itself on the wake path, so the intent
is local to the code that holds it, honoured on every panel batch, and does not
depend on undocumented behaviour of a pinned dependency. `cleanInitialRefresh`
means something again.

## Non-goals

- **Changing `goHome`'s signature.** Out of scope by the brief, and unnecessary:
  the parameter is already there.
- **Making `initialMenuItem` mean something.** It has no counterpart on the
  launcher and is never passed a non-default value anywhere in the firmware
  (research §5).
- **Touching `HomeActivity`.** It is the reference to read, not to edit; its
  removal is #29.
- **Touching the sleep path, the Quick Resume frame, or `freeink-sdk`.**
- **Correcting the stale comment at `src/main.cpp:453-456`.** See A7.
- **A host-test harness for activities.** See A10.

---

## Assumptions

Each is a behavioural choice. Attack them by number.

**A1 — `HALF_REFRESH`, not `FULL_REFRESH`, for the first render.**
Mirrors the reference (`HomeActivity.cpp:306`). It is also what the SSD1677's
own one-shot resolves a promoted first paint to (`Ssd1677Driver.cpp:439`), and
on UC8179/UC8279 a `HALF` and a `FULL` run the same OTP waveform — only the
OLD-plane seed differs (research §4). Rejected: `FULL_REFRESH`, which buys no
extra cleaning on any of the three drivers and reads as a bigger hammer than the
reference used.

**A2 — the flag is consumed on the first render only, latched by a
`firstRenderDone` member.** Mirrors `HomeActivity.h:17` and
`HomeActivity.cpp:306,308-309`. Rejected: consuming it in `onEnter`, which would
decide the refresh mode in a different function from the one that performs it.

**A3 — the launcher does NOT copy `HomeActivity`'s follow-up `requestUpdate()`
(`HomeActivity.cpp:308-311`).** That exists because the old home renders a
skeleton first and loads recents afterwards. The launcher resolves everything in
`onEnter` before its first render (`LauncherActivity.cpp:65-72`) and has no
second pass; copying the call would add a second full-screen refresh to **every**
launcher entry. This is the single most likely copy-paste error in this change.

**A4 — the flag arrives as a constructor parameter defaulted to `false`, held as
a `const bool` member.** Mirrors `HomeActivity.h:70-73,31`. The default keeps
today's behaviour at the other four `goHome` call sites (`src/main.cpp:566`,
`ActivityManager.cpp:81,114`, `src/activities/Activity.cpp:13`) and at any future
`std::make_unique<LauncherActivity>`. A `const` member makes the activity
non-assignable; activities are heap-allocated and never assigned
(`ActivityManager.cpp:186-198`), and `HomeActivity` already does this.

**A5 — `goHome` keeps discarding `initialMenuItem` with `(void)`, and the comment
at `ActivityManager.cpp:248-251` is rewritten to explain only that half.**
The current comment explains why *both* are ignored and would be false after this
change. It also repeats a miscount — "~20 call sites pass it" — that research §5
disproves: five call sites, one of which passes anything. The replacement states
the fact without the number.

**A6 — `firstRenderDone` latches on every render, not only when
`cleanInitialRefresh` is true.** One less conditional to reason about, and it
matches the reference. The two booleans are independent: the latch records "this
instance has painted", the flag records "the panel is showing something this
activity did not draw".

**A7 — `src/main.cpp` is not edited.** Its comment at 453-456 ("the first paint
is FAST_REFRESH (~500ms) over the retained frame") is true for the X3 and wrong
for all three X4 Pro drivers (research §2), but the brief scopes this change to
the launcher and the manager, and the correction is already recorded in the
research note. Recommend a separate docs/comment change. Attack this if you
think a comment that re-creates this whole investigation is worth the scope
breach.

**A8 — a `LOG_DBG` records the flag, the latch and the mode on every render.**
On a default unit there is no optical difference to observe (research §6), so
without a log line this change has no observable behaviour at all and cannot be
verified by the human tester. `LOG_DBG` compiles out at `LOG_LEVEL=1`
(`lib/Logging/Logging.h:57-60`), which is the release env
(`platformio.ini:188`), so the shipped build pays nothing. Rejected: `LOG_INF`,
which would ship.

**A9 — `#include <HalDisplay.h>` is added to `LauncherActivity.cpp`.** It
currently compiles the name transitively through `GfxRenderer.h`; the reference
file includes it directly (`HomeActivity.cpp:7`).

**A10 — no host test is added.** Every suite under `test/` covers a pure unit
(parsers, geometry, stores — `test/CMakeLists.txt:51-89`); none constructs an
activity, because that needs the renderer, the theme, `HalStorage` and `Epub`,
none of which exist on the host. Testing a two-boolean ternary would mean
extracting it into a free function the reference implementation does not have —
inventing a pattern for one line. Verification is the build plus the serial
check in A8. Attack this if you would rather have the helper.

**A11 — no interaction with night mode.** `display.setInverted` is applied per
render by the render task (`ActivityManager.cpp:56-60`), and a polarity change
already promotes `FAST_REFRESH` to `HALF_REFRESH` inside the SDK
(`freeink-sdk/.../FreeInkDisplay.cpp:573-575`). This change neither depends on
that nor disturbs it.

---

## Architecture

Three edits, two files plus one header. No new class, no new file, no
allocation.

### 1. `src/activities/launcher/LauncherActivity.h`

The constructor gains the parameter and the class gains two members, placed with
the existing private state:

```cpp
  explicit LauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool cleanInitialRefresh = false)
      : Activity("Launcher", renderer, mappedInput), cleanInitialRefresh(cleanInitialRefresh) {}
```

```cpp
  // Set when the panel is still showing a frame this activity did not draw --
  // the sleep screen, after a wake with no Quick Resume frame. The first paint
  // must then be non-differential; every later one is an ordinary fast refresh.
  const bool cleanInitialRefresh;
  bool firstRenderDone = false;
```

### 2. `src/activities/launcher/LauncherActivity.cpp`

`#include <HalDisplay.h>` joins the sorted block (A9), and `render`'s tail
(`LauncherActivity.cpp:473`) becomes (both statements fit the 120-column limit
in `.clang-format:133`, so this is what the formatter will leave):

```cpp
  const auto mode = cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  LOG_DBG(MODULE, "Paint: clean=%d first=%d mode=%s", cleanInitialRefresh ? 1 : 0, firstRenderDone ? 0 : 1,
          mode == HalDisplay::HALF_REFRESH ? "HALF" : "FAST");
  renderer.displayBuffer(mode);
  firstRenderDone = true;
```

`MODULE` is the file's existing tag, `"LAUNCH"` (`LauncherActivity.cpp:39`).
No `requestUpdate()` follows it — A3.

### 3. `src/activities/ActivityManager.cpp`

```cpp
void ActivityManager::goHome(HomeMenuItem initialMenuItem, bool cleanInitialRefresh) {
  // bereanOS's home is the launcher: Bible, Meetings, Buscar, Tags and
  // settings, plus a resume strip. HomeMenuItem describes the old file-centric
  // home (browser / recents / transfer / settings) and has no counterpart here;
  // it stays in the signature for the callers that pass it, and is ignored.
  (void)initialMenuItem;
  replaceActivity(std::make_unique<LauncherActivity>(renderer, mappedInput, cleanInitialRefresh));
}
```

## Control and data flow

The wake path, end to end, with the new step in bold:

1. `enterDeepSleep` clears `showBootScreen` (`src/main.cpp:261`) and writes a
   sleep frame **only** for Quick Resume (`src/main.cpp:255,270-274`).
2. On wake, a power-button boot with the flag clear resolves to
   `BootResume::SplashlessWake` (`src/main.cpp:461-463`);
   `setupDisplayAndFonts(seamless=true)` runs `display.begin()`
   (`src/main.cpp:467`, `lib/hal/HalDisplay.cpp:13-27`) and
   `activityManager.begin()` has already started the render task
   (`src/main.cpp:307`, `ActivityManager.cpp:28-41`).
3. No sleep frame → `needsWakeRefresh = true` (`src/main.cpp:538-541`).
4. `goHome(HomeMenuItem::NONE, needsWakeRefresh)` (`src/main.cpp:571`).
5. **`goHome` forwards the flag into the `LauncherActivity` constructor**
   (`ActivityManager.cpp:254`). No activity exists yet on this path, so
   `replaceActivity` takes its immediate branch and calls `onEnter()` inside
   `setup()` (`ActivityManager.cpp:193-196`).
6. `onEnter` lays out, resolves targets, and calls `requestUpdate()`
   (`LauncherActivity.cpp:65-72`). That is the deferred form
   (`ActivityManager.h:110`, `ActivityManager.cpp:296-305`), so the render task
   is notified at the tail of the first `ActivityManager::loop()` after `setup()`
   returns (`ActivityManager.cpp:169-175`).
7. The render task takes `RenderLock`, sets polarity, and calls `render()`
   (`ActivityManager.cpp:52-61`). **`render()` sees `cleanInitialRefresh &&
   !firstRenderDone` and asks for `HALF_REFRESH`**, then latches.
8. Any later render in the same instance — a button press
   (`LauncherActivity.cpp:544,548`), a result callback
   (`LauncherActivity.cpp:510,517`) — takes `FAST_REFRESH`.

Every other route to the launcher (`src/main.cpp:566`, the home gesture and the
pop-to-home at `ActivityManager.cpp:81,114`) constructs it with the default
`false` and is unchanged. The Quick Resume route is also unchanged: it paints the
loading icon with `HALF_REFRESH` before routing (`src/main.cpp:522-536`), so the
flag is correctly false there.

**Data.** One `const bool` and one `bool` on an activity that is already
heap-allocated. No buffer, no heap operation, no flash table, no new string.

## Error handling and failure modes

There is no I/O, no allocation and no fallible call on this path, so the repo's
error protocol has nothing to report: no `LOG_ERR`, no `return false`. What
matters is that both ways of being wrong are benign and bounded.

- **Flag false when it should be true** (a future caller forgets it): today's
  behaviour exactly — `FAST_REFRESH`, which the drivers still promote to an
  absolute paint on this hardware (research §2). Degrades to the status quo, not
  to a defect.
- **Flag true when it should be false**: one extra non-differential refresh on
  the first paint of one activity entry — order 1–2 s on this panel, once, and
  only on a wake. No state is written, so nothing can be corrupted.
- **Latch and thread safety**: `render()` is called only by the render task,
  under `RenderLock` (`ActivityManager.cpp:52-61`); `firstRenderDone` needs no
  synchronisation and no `volatile`.
- **Lifetime**: navigation deletes the activity and constructs the next one
  (`ActivityManager.cpp:178-184,186-198`), so the latch cannot survive into a
  later entry and a stale `true` is impossible.
- **The SDK moving underneath us**: if a future `freeink-sdk` bump changes the
  first-paint promotion, this change is what keeps the wake paint correct. That
  is the point of it, and it is why the ternary must not later be "simplified"
  away on the grounds that the driver already cleans.

## Testing strategy

**Build.** `~/.platformio/penv/bin/pio run` once, after the last edit — the bare
`pio` is not on PATH in a non-login shell. The dev env is `LOG_LEVEL=2`
(`platformio.ini:170`), which is what makes A8's line visible.

**Format.** `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` over the whole
tree before committing — `-g` alone misses a file once it is committed, and
without the PATH prefix the wrapper exits 1 rather than passing falsely, so a
skipped prefix reads as a failure.

Both commands assume the worktree bootstrap is done: `freeink-sdk` initialised
and `.venv` present (clang-format 21.1.8). It was not, at the time the research
note was written; that note's bootstrap paragraph is now historical.

**Host.** None, per A10.

**Serial (dev build) — the only check that can prove the wiring.** With
`python3 scripts/debugging_monitor.py` attached:

1. Sleep screen set to anything but Quick Resume (default is `DARK`,
   `src/CrossPointSettings.h:207`). Sleep, wake. Expect
   `[DBG] LAUNCH Paint: clean=1 first=1 mode=HALF` once, and nothing else from
   `LAUNCH` until an input.
2. Press a nav button on the launcher. Expect `clean=1 first=0 mode=FAST` — the
   latch held.
3. Sleep screen set to Quick Resume. Sleep, wake. Expect
   `clean=0 first=1 mode=FAST`: the loading-icon `HALF_REFRESH` already cleaned the panel
   (`src/main.cpp:528-536`).
4. From the reader, use the home gesture to return to the launcher. Expect
   `clean=0 first=1 mode=FAST`.
5. Watch the paint count: exactly one refresh per launcher entry. A second
   full-screen refresh on entry means A3 was violated.

**Device / optical — outstanding, and cannot be closed by the default unit.**
On an SSD1677 or UC8179 unit in its default configuration there will be **no
visible difference** before and after this change, because the driver already
cleans the first paint. That is the expected result, not a failed fix. The only
configuration where the before/after is visible is an SSD1677 batch with
`fadingFix` set to 1 through the web settings API; if the tester wants to see the
defect this fix closes, that is how. Anyone reporting "no ghosting after the fix"
without having checked the unedited build on the same unit has measured nothing.

**Heap.** Unchanged by inspection — no allocation is added. No measurement asked
of the tester.

## Risks

- **A3 by copy-paste.** The reference implementation's next four lines are a
  `requestUpdate()` the launcher must not have. This is the one way this change
  can regress every launcher entry rather than improve one.
- **A later "simplification".** Someone re-derives research §2, concludes the
  ternary is dead, and deletes it. The comment on `cleanInitialRefresh` in the
  header must say what it is for, and the research note is the long form.
- **`fadingFix` on the X4 Pro is hidden but live.** This change makes the first
  wake paint correct with it on; ordinary launcher renders with it on are still
  differential-with-power-down and are out of scope.
- **The parameter is still only honoured by one of `goHome`'s two arguments.**
  That asymmetry is deliberate (A5) and will look like an oversight to the next
  reader; the comment is what prevents that.

## Open questions

1. **A7** — whether correcting `src/main.cpp:453-456` belongs in this change or a
   follow-up. Recommended: follow-up, to keep the diff inside the brief's scope.
2. **Scope, given the research.** The issue was written from code archaeology and
   its stated symptom is not reproducible on a default device. This spec
   implements it anyway, for the reasons in Problem §2 and §3. If the reviewer
   thinks the honest outcome is to close #30 as "not a defect, comment corrected",
   that is a decision to take now rather than after the code lands.
