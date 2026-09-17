# Issue #60 — two input-layer findings: research

Date: 2026-09-17 · Branch: `fix/60-input-layer-traps` · Surface: `ui` (with two
files outside it — see *Scope note*)

Issue #60 carries two independent findings. They are researched separately below.

---

## Finding 1 — `STR_LONG_PRESS_BEHAVIOR` promises a button that cannot reach it

### Who owns the behaviour

| Concern | File:line |
|---|---|
| The setting entry and its option list | `src/SettingsList.h:349-358` |
| The setting's storage + enum | `src/CrossPointSettings.h` (`longPressButtonBehavior`) |
| The `700 ms` threshold | `src/activities/reader/ReaderUtils.h:18` (`SKIP_HOLD_MS = 700`) |
| Branch site — EPUB reader | `src/activities/reader/EpubReaderActivity.cpp:605-627` |
| Branch site — plain reader | `src/activities/reader/ReaderActivity.cpp:144-146` |
| Held time, as the reader sees it | `src/MappedInputManager.cpp:322-328` → `lib/hal/HalGPIO.cpp:236-239` |
| The synthesis that decides it | `lib/Input/Input/NavKeyGestures.{h,cpp}` |
| The touch path that still works | `src/activities/reader/ReaderUtils.h:120` (`result.heldMs = gpio.lastTouchHeldMs()`) |

`SettingsList.h:349-358` already carries the `#if BEREAN_CAP_ROTATION` split the
issue describes: the `_ORIENTATION` option is compiled out on this board, so the
live option list is **Off / Chapter skip** only. Verified by reading the lines.

### Current control flow, verified

`HalGPIO::update()` (`lib/hal/HalGPIO.cpp:141-159`) feeds the raw `BTN_UP` /
`BTN_DOWN` pin states into `NavKeyGestures`. Every raw nav edge is then withheld:
`wasPressed` and `wasReleased` for `BTN_UP`/`BTN_DOWN` return `synthesisedEdge()`
and nothing else (`HalGPIO.cpp:210-225`).

`NavKeyGestures::updateKey` (`NavKeyGestures.cpp:22-31`) resolves a key **on
release**, into exactly one of two events:

- `elapsed >= HOLD_MS (850)` → `NavEvent::Synth` → emitted as `BTN_BACK` (left)
  or `BTN_CONFIRM` (right) (`HalGPIO.cpp:193-201`). **No page event is emitted at
  all.**
- `elapsed < 850` → `NavEvent::Page` → emitted as `BTN_UP` / `BTN_DOWN`.

`reportingSyntheticHeldTime()` (`NavKeyGestures.cpp:60-62`) returns true when
**either** key produced **any** event — `Page` included, not just `Synth`. So
`HalGPIO::getHeldTime()` (`HalGPIO.cpp:236-239`) substitutes
`SYNTHETIC_HELD_MS = 40` (`NavKeyGestures.h:68`) on every nav-key release,
whether it was a tap or a hold.

**The consequence is stronger than the issue states.** It is not merely that 40
is never > 700. There is *no* nav-key hold duration that produces a page turn
with a held time above `SKIP_HOLD_MS`:

- hold < 850 ms → page turn, but `getHeldTime()` is forced to 40;
- hold ≥ 850 ms → Back/Confirm, and no page turn happens to skip from.

Chapter skip on the nav buttons is therefore **structurally** unreachable on this
board, not just numerically.

### What the setting still does

1. **Touch long-press still reaches it.** `EpubReaderActivity.cpp:605` and
   `ReaderActivity.cpp:144` both prefer `touch.heldMs` when the turn came from a
   tap zone, and `ReaderUtils.h:120` fills that from `gpio.lastTouchHeldMs()` —
   a real duration, never substituted. Confirmed by reading all three.
2. **It silently changes press-vs-release paging.** `ReaderUtils.h:53`:
   `usePress = (longPressButtonBehavior == OFF)`. Setting it to Chapter skip
   flips the whole reader from `wasPressed` to `wasReleased`. On this board that
   is a no-op — `HalGPIO` emits both edges of the synthetic pair on the same tick
   (`HalGPIO.cpp:207-225`) — but it is a real second effect of the setting, and
   any future change to the synthesis would expose it.

### Would "route the synthesised path to report a real hold duration" work?

Researched, because the issue leaves it open. Three findings say no:

1. **The reachable window would be 700–850 ms, 150 ms wide.** Anything longer is
   consumed by the Back/Confirm gesture before a page event exists. A 150 ms
   target between two other gestures is not a usable affordance on a device whose
   full refresh is ~1.7 s.
2. **Widening it means lowering `HOLD_MS` below 700, which is already forbidden
   by a landed test.** `test/nav_key_gestures/NavKeyGesturesTest.cpp:141-144`
   pins `HOLD_MS > 700` ("above SKIP_HOLD_MS") and `< 1000`. The constant's own
   comment (`NavKeyGestures.h:39-41`) states the same constraint.
3. **Reporting a real duration for `Page` events re-opens the bug
   `NavKeyGestures` was built to close.** `HalGPIO.cpp:229-235` enumerates the
   branch sites. Re-checked each one against the button it actually reads:

   | Site | Button | Threshold | Would a real Page duration reach it? |
   |---|---|---|---|
   | `EpubReaderActivity.cpp:496,506` | Confirm | 400 | No — `Synth` path |
   | `EpubReaderBookmarksActivity.cpp:158` | Confirm | 700 | No |
   | `HighlightsActivity.cpp:361,367` | Confirm | 700 | No |
   | `TagPickerActivity.cpp:308` | Confirm | 700 | No |
   | `RecentBooksActivity.cpp:104,106` | Confirm | — | No |
   | `FileBrowserActivity.cpp:275` | Confirm (via `activateSelected`) | 1000 | No |
   | `FileBrowserActivity.cpp:340,365` | Back | 1000 | No |
   | `KeyboardEntryActivity.cpp:546,620,652,659` | **Up / Down** | 700–1000 | **Yes** |
   | `ButtonNavigator.cpp:70` | held state | 500 | Guarded by `isPressed` |

   `KeyboardEntryActivity` is the live collision: it branches on
   `isPressed(Button::Up) && getHeldTime() > LONG_PRESS_MS`
   (`KeyboardEntryActivity.cpp:544-546`). It is inert today only because
   `HalGPIO::isPressed` suppresses the held nav state past
   `HOLD_STATE_SUPPRESS_MS = 250` (`HalGPIO.cpp:181-182`, `NavKeyGestures.h:48`).
   Any change that hands a real duration to the nav path has to be reasoned about
   against that file too.

`ButtonNavigator.cpp:70` is worth recording separately: continuous list scrolling
needs `isPressed` to stay true past 500 ms, and the same 250 ms suppression kills
it. That is a **pre-existing** consequence of the `NavKeyGestures` design, not
part of #60, and is noted here only so a later phase does not rediscover it as
new.

### Nearest existing example of this kind of change

Two, both landed:

- **`d8e92208` — "fix: gate screen rotation behind a portrait-only capability
  (#45)"** (closes #37). The structural model: a `BEREAN_CAP_*` macro in
  `src/CrossPointSettings.h:17-23` with an `#error` for an unhandled device, and
  a `#if` around the option list in `src/SettingsList.h`. Touched 3 source files,
  no translations, no host test.
- **`02d9106a` — "fix: label the launcher's fourth tile Settings (#53)"**. The
  model for changing a user-facing label: it **reused an existing `STR_*` key
  already present in all 32 languages** rather than introducing a new English
  string that 31 languages would fall back on. Its commit body makes that the
  explicit justification. `lib/I18n/translations/english.yaml` +
  `spanish.yaml`, 1 line each, plus the two call-site files.

### i18n facts, measured

- `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` run on a clean
  tree: **32 languages, 420 string keys, 1 never used**, `Code generation
  complete!`. The one unused key is `STR_HIGHLIGHTS_TOO_LARGE`, kept on purpose
  per `70041d90`'s commit body (issue #39 needs it).
- The usage scan is a **raw regex over file text**:
  `re.compile(r"\bSTR_[A-Za-z0-9_]+\b")` at `scripts/gen_i18n.py:267`, applied to
  every `.cpp`/`.h`/`.c` under `src` and `lib` (`gen_i18n.py:272-285`). Comments
  are not excluded. Practical consequence for this issue: **a retired `STR_*`
  name left behind in a comment still counts as used**, so the unused-key report
  will not catch it. The generated files are skipped via `_GENERATED_FILENAMES`.
- The existing English strings are
  `STR_LONG_PRESS_BEHAVIOR: "Long-press button behavior"`,
  `_OFF: "OFF"`, `_SKIP: "Chapter skip"`, `_ORIENTATION: "Orientation change"`
  (`lib/I18n/translations/english.yaml:93-96`). All four exist in all 32 YAML
  files (checked by grep; `italian.yaml:204-207` orders them differently,
  `danish.yaml:74-77` holds Spanish text, both pre-existing).

---

## Finding 2 — `getPressedFrontButton()` can never return LEFT or RIGHT

### Who owns it

`src/MappedInputManager.cpp:378-393`, declared at `src/MappedInputManager.h:104`.

### Current control flow, verified

The function scans `gpio.wasPressed()` over `BTN_BACK`(0), `BTN_CONFIRM`(1),
`BTN_LEFT`(2), `BTN_RIGHT`(3) — indices at `lib/hal/HalGPIO.h:137-143`.

- `HalGPIO::wasPressed` returns `synthesisedEdge()` for `BTN_BACK`/`BTN_CONFIRM`
  (`HalGPIO.cpp:211`), which fires from an 850 ms nav-key hold. **Those two arms
  are reachable.**
- `BTN_LEFT` and `BTN_RIGHT` fall through to `inputMgr.wasPressed()`
  (`HalGPIO.cpp:215`), whose state comes from
  `isDigitalPressed(BoardConfig::ACTIVE.input.left / .right)`
  (`InputManager.cpp:257-258`). `isDigitalPressed` is
  `pin >= 0 && digitalRead(pin) == LOW` (`InputManager.cpp:246`).
- The X4 Pro profile sets back/confirm/left/right to `PIN_UNASSIGNED`
  (`BoardConfig.h:1396`), and `PIN_UNASSIGNED = -1` (`BoardConfig.h:406`). So
  `pin >= 0` is false and those bits are never set. **Confirmed: both arms are
  unreachable, and nothing else synthesises them** — `HalGPIO::synthesisedEdge`
  (`HalGPIO.cpp:186-205`) has cases only for `BTN_BACK`, `BTN_CONFIRM`,
  `BTN_UP`, `BTN_DOWN`.

### Reachability of the caller — confirmed dormant

Grepped the whole `src` tree for `getPressedFrontButton`, `ButtonRemapActivity`
and `RemapFrontButtons`. Exactly one call site:
`src/activities/settings/ButtonRemapActivity.cpp:71`. Exactly one construction:
`src/activities/settings/SettingsActivity.cpp:309`, reached only from the
`SettingAction::RemapFrontButtons` entry appended at
`SettingsActivity.cpp:75-78`, which is inside `if (!BoardConfig::hasTouch())`.
`BoardConfig::hasTouch()` is `ACTIVE.touch.controller != TouchController::None`
(`BoardConfig.h:1622`), and the X4 Pro profile declares a GT911
(`BoardConfig.h:1411`). So the entry is never appended and the activity is never
constructed on this board. This is trap-proofing, not a bug fix — as the issue
says.

### Is a board-aware assert affordable? — yes, and it can be compile-time

- `BoardConfig.h` is already included by `src/MappedInputManager.cpp:3`.
- `constexpr BoardProfile XTEINK_X4_PRO` (`BoardConfig.h:1363`) and
  `constexpr BoardProfile DEFAULT_DEVICE = XTEINK_X4_PRO` (`BoardConfig.h:1520`,
  selected by `-DFREEINK_DEVICE_X4PRO=1`, `platformio.ini:166,184`) are both
  compile-time constants. A `static_assert` over `DEFAULT_DEVICE.input.left` is
  therefore possible and costs zero bytes.
- `BoardConfig::ACTIVE` is **not** constexpr — `inline BoardProfile ACTIVE`
  (`BoardConfig.h:1532`), mutable so `selectDevice()` can swap it at runtime. A
  `static_assert` on `ACTIVE` will not compile; only a runtime `assert` can read
  it. This build has one device, so `ACTIVE == DEFAULT_DEVICE` unless
  `selectDevice()` is called.

The repo already uses both forms: `static_assert` for invariants
(`src/fontIds.h:18-28`, `src/CrossPointSettings.cpp:374`,
`src/network/OtaBootSwitch.h:25`) and runtime `assert` for impossible states
(`src/activities/ActivityManager.cpp:41,325-331`,
`src/activities/UiTabListActivity.cpp:27`). `CLAUDE.md` restricts runtime
`assert(false)` to "fatal impossible states" and requires `constexpr` first.

---

## Scope note

`src/MappedInputManager.cpp` and `lib/hal/HalGPIO.cpp` sit outside the `ui`
surface as `.claude/agents/ui-dev.md` defines it, and that file says not to touch
the input layer "without an explicit instruction". Issue #60 names
`MappedInputManager.cpp:377-393` directly, which is that instruction. Finding 2
is confined to that one function; Finding 1's preferred shape (see spec) touches
only `src/SettingsList.h` and `lib/I18n/translations/`.

`lib/I18n/translations/*.yaml` is listed as report-do-not-edit in
`ui-dev.md`, but the batch instruction for this task states the #31 key purge
(`70041d90`) has landed and the directory is now free to edit. Taking the newer
instruction.

## Environment, measured

| Tool | Version | How |
|---|---|---|
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` (not on the default `PATH` in this worktree) |
| Python | 3.14.7 | `python3 --version` |
| CMake | 4.4.2 | `cmake --version` |
| clang-format | 21.1.8 | main checkout's `.venv/bin/clang-format --version` |
| googletest | v1.17.0 | `test/CMakeLists.txt:18` |
| ArduinoJson | v7.4.2 | `test/CMakeLists.txt:31` |
| `freeink-sdk` submodule | `310ec615` | `git submodule status` (initialised this session) |

Host test layout: 58 suites under `test/`, each its own directory with a
`CMakeLists.txt` and an `add_subdirectory` line in `test/CMakeLists.txt`. The
nearest suite to this issue is `test/nav_key_gestures/` (14 `TEST`s,
`NavKeyGesturesTest.cpp`), which links only
`lib/Input/Input/NavKeyGestures.cpp` — no Arduino, no HAL. `test/CMakeLists.txt`
is a shared append point per `ui-dev.md`.
