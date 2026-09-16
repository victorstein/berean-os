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
   gated on `if (!turnOff)` (`Ssd1677Driver.cpp:428`). On an SSD1677 unit with
   `fadingFix` persisted to 1 — default 0 (`src/CrossPointSettings.h:285`),
   hidden from the X4 Pro settings UI
   (`src/activities/settings/SettingsActivity.cpp:54`), still writable through
   the web settings API — the first post-wake paint stays a true differential
   and cannot clear the glass. Research §3.
3. **Nothing in `src/` asks for the clean.** The firmware gets one because
   three driver internals happen to arrange it. `freeink-sdk` is a submodule
   this repo does not own, the property is stated nowhere in its public API, and
   `HalDisplay::begin`'s own comment claims the opposite for the seamless path
   (`lib/hal/HalDisplay.cpp:22-26`, and `src/main.cpp:453-456`). Making the
   request explicit does not remove every dependence on those internals — see
   A1 on UC8279 — but it puts the intent where the intent is held, and on
   SSD1677 with `fadingFix` it is what makes the paint clean at all.

## Goal

The launcher requests the clean refresh itself on the wake path, so the request
is local and explicit rather than an accident of three driver internals, and the
paint is clean on every panel batch — including the one configuration where it
is not clean today. `cleanInitialRefresh` means something again.

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
- **The `BootResume::Silent` route (`src/main.cpp:566`).** It reaches the
  launcher in the same state the flag describes — the code's own comment says
  the panel "keeps showing the pre-reboot popup until that first paint lands"
  (`src/main.cpp:512-514`) — and it passes no flag. Its sibling branch three
  lines earlier already gets the safe default: `goToReader` defaults
  `allowFastInitialRefresh` to `false` (`ActivityManager.h:88`), which leaves
  `pagesUntilFullRefresh` at 0 (`ReaderActivity.cpp:19-22`). Passing `true` at
  `src/main.cpp:566` would reuse this change's mechanism exactly, but it is an
  edit to `src/main.cpp`, which A7 defers; it rides with that follow-up. Raised
  by review pass 0, MAJOR 2.
- **A host-test harness for activities.** See A10.

---

## Assumptions

Each is a behavioural choice. Attack them by number.

**A1 — `HALF_REFRESH`, not `FULL_REFRESH`, for the first render.**
Mirrors the reference (`HomeActivity.cpp:306`). It is also what the SSD1677's
own one-shot resolves a promoted first paint to (`Ssd1677Driver.cpp:439`), and
on UC8179 a `HALF` and a `FULL` run the same OTP waveform and differ only in the
OLD-plane seed (research §4, `Uc8179Driver.cpp:292-308`). The equivalence is
narrower on UC8279_X4, which excludes only `Full` from its partial path
(`Uc8279X4Driver.cpp:179`): there a `HALF` is a clean exactly while
`_oldPlaneValid` is false — true on the first paint after a chip-reset wake
(`Uc8279X4Driver.h:112`), and set true again by every `displayFinish`
(`Uc8279X4Driver.cpp:246`). On that batch this change therefore keeps, rather
than removes, a dependence on a driver internal. `HALF` is still the right
request: it is clean on all three drivers on the path the flag covers, and
`FULL_REFRESH` buys nothing, because `Ssd1677Driver.cpp:439` collapses a
requested `Full` to `Half` anyway and both UC drivers white-seed identically
while `_oldPlaneValid` is false. Rejected: `FULL_REFRESH`, which reads as a
bigger hammer than the reference used and changes nothing on any of the three.

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
disproves: five call sites, one of which passes anything. The replacement states the verified fact instead: no caller passes anything but
`HomeMenuItem::NONE`, and the parameter survives so that `Activity::onGoHome`'s
20 call sites (`src/activities/Activity.h:69`) and the default argument at
`ActivityManager.h:93` need not change.

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
`firstPaint` is "this paint is the first", i.e. the negation of `firstRenderDone`
— it is read before the latch is set, so the field name must not be `first` and
the argument must not be `firstRenderDone ? 1 : 0`. Every expectation in Testing
depends on that polarity.
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
(parsers, geometry, stores — `test/CMakeLists.txt:51-110`); none constructs an
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
  LOG_DBG(MODULE, "Paint: clean=%d firstPaint=%d mode=%s", cleanInitialRefresh ? 1 : 0,
          firstRenderDone ? 0 : 1, mode == HalDisplay::HALF_REFRESH ? "HALF" : "FAST");
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
  // No caller passes anything but HomeMenuItem::NONE; it stays in the
  // signature so onGoHome's call sites and the default argument need not change.
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
`false` and is unchanged — correctly for the two gesture routes, which paint
over a frame the user was already looking at, and with the silent-reboot
exception recorded in Non-goals. The Quick Resume route is also unchanged: it paints the
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
  SSD1677 first-paint promotion, this change is what keeps the wake paint
  correct there. That is the point of it, and it is why the ternary must not
  later be "simplified" away on the grounds that the driver already cleans. The
  cover is not total: on UC8279_X4 a `HALF` is a clean only while
  `_oldPlaneValid` is false (A1), so a bump that seeded that baseline across a
  wake would need `FULL_REFRESH` here — the one mode that driver always excludes
  from its partial path (`Uc8279X4Driver.cpp:179`).

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
   `[DBG] LAUNCH Paint: clean=1 firstPaint=1 mode=HALF`, and exactly one `Paint:`
   line for that entry. Other `LAUNCH` lines are normal and expected —
   `resolveTargets` logs the meeting publication and any generated thumbnail
   (`LauncherActivity.cpp:124,136,219`), and `LOG_INF` survives at both log
   levels (`lib/Logging/Logging.h:51-55`).
2. Press a nav button on the launcher. Expect `clean=1 firstPaint=0 mode=FAST` — the
   latch held.
3. Sleep screen set to Quick Resume. Sleep, wake. Expect
   `clean=0 firstPaint=1 mode=FAST`: the loading-icon `HALF_REFRESH` already cleaned the panel
   (`src/main.cpp:528-536`).
4. From the reader, use the home gesture to return to the launcher. Expect
   `clean=0 firstPaint=1 mode=FAST`.
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

## Review pass 0 — what changed and why

Reviewed in `docs/superpowers/reviews/issue-30-spec-review-0.md`:
`VERDICT: CLEAR`, 0 blockers, 2 majors, 4 minors. All six are applied above; the
design itself is unchanged.

- **MAJOR 1** — A1 claimed `HALF` and `FULL` are equivalent on "UC8179/UC8279".
  True on UC8179, false on UC8279_X4, whose partial path excludes only `Full`
  (`Uc8279X4Driver.cpp:179`). A1 now states the narrower fact, and the Goal and
  Problem §3 no longer claim the change removes every dependence on SDK
  internals — on that batch it keeps one. The decision (`HALF`, not `FULL`)
  stands, for the reasons now written into A1.
- **MAJOR 2** — the spec asserted every other route to the launcher was
  correctly unchanged. The `BootResume::Silent` route (`src/main.cpp:566`) is in
  the same stale-frame state the flag describes, while its sibling
  `goToReader` branch already defaults to a clean first paint. Now an explicit
  Non-goal, deferred to A7's follow-up because closing it means editing
  `src/main.cpp`. Whether to close it here instead is a scope call; the
  recommendation is the follow-up.
- **MINOR 1** — the log field named `first` printed the negation of
  `firstRenderDone`. Renamed to `firstPaint`, with the polarity stated in A8,
  because every Testing expectation reads that field.
- **MINOR 2** — Testing step 1 said "nothing else from `LAUNCH`"; `resolveTargets`
  logs under the same tag. Re-worded to the property actually checked: one
  `Paint:` line per entry.
- **MINOR 3** — A5's replacement comment said the parameter stays "for the
  callers that pass it". None pass anything but `HomeMenuItem::NONE`. The comment
  now says that.
- **MINOR 4** — `Ssd1677Driver.cpp:427` → `:428` (427 is a comment line), and
  `test/CMakeLists.txt:51-89` → `:51-110`, the real end of the
  `add_subdirectory` block. The same `:427` slip is in research §3 and is
  corrected there in the same commit.

## Open questions

Both were answered in review pass 0 and are recorded here as decided, not open.

1. **A7 — correcting `src/main.cpp:453-456`.** Follow-up, not this change.
   Correcting it properly means importing the research note's driver analysis
   into `main.cpp`, and the brief scopes this change to the launcher and the
   manager. MAJOR 2's one-token fix rides with it.
2. **Scope — implement, do not close #30.** `goHome`'s `cleanInitialRefresh` is
   a parameter that lies about its effect, which is a defect independent of any
   panel; the symptom is reachable today on an SSD1677 unit with `fadingFix`
   persisted through the web settings API (`src/SettingsList.h:277` keeps the
   key in the shared list even though `SettingsActivity.cpp:54-56` hides the
   row); and the change is six lines with no allocation.

   **Carry this into the issue and the PR description**, not just this file:
   #30's stated symptom ("so the sleep screen ghosts") was written from code
   archaeology and is not reproducible on a default unit. Without that sentence
   in the issue, someone later verifies optically, sees no change, and concludes
   the fix did nothing.
