# bereanOS Phase 2a — the input model

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give this device a Back and a Confirm that exist in hardware, without breaking the raw key state that recovery mode, the screenshot combo and hold-to-scroll all depend on.

**Architecture:** A pure state machine turns the two nav keys plus a clock into synthesised `BTN_BACK` / `BTN_CONFIRM` events. `HalGPIO` — our code, wrapping the SDK — answers `isPressed`/`wasPressed`/`wasReleased` for those two indices from the machine and passes everything else straight through. Because `SETTINGS.frontButtonBack` already resolves to `BTN_BACK`, every existing call site works with no shim and no edits.

**Tech Stack:** C++20, GoogleTest on the host, PlatformIO/ESP-IDF, FreeRTOS.

---

## Revision note — 2026-09-14

The first draft of this plan was reviewed and came back with 23 defects, four of
them blockers. It has been rewritten. What changed and why:

- **It synthesised on top of `MappedInputManager`.** That layer sits above the
  code that actually reads buttons, so `wasPressed`, `isPressed` and the
  composed `NavNext`/`NavPrevious` all bypassed it. The reader pages on
  `wasPressed` by default (`ReaderUtils.h:53`, `longPressButtonBehavior`
  defaults to `OFF`), so every Back would have turned a page first; twelve sites
  including `KeyboardEntryActivity.cpp:643` read `wasPressed(Confirm)`, so you
  could not have typed a WiFi password. **Synthesis moves down into `HalGPIO`.**
- **The chord is gone.** Back is a held left key. A chord needs two keys to land
  inside a join window, and after three seconds of inactivity the loop drops to
  `delay(50)` (`HalPowerManager.h:33`), leaving a window narrower than a natural
  two-thumb squeeze — on the one gesture that exists to rescue a user.
- **Thresholds now dodge the live ones.** The draft claimed the keys had no
  other long-press meaning. Both do: `SKIP_HOLD_MS = 700` and
  `BOOKMARK_HOLD_MS = 400` (`ReaderUtils.h:18-19`), plus
  `GO_BACK_OR_HOME_MS = 1000` (`:255`).
- **`HomeLadder` is cut.** It was seven green tests that nothing called.
- **`ReturnStack` keeps `CAPACITY = 3`.** The draft raised it to 8 and asserted
  the existing tests were parameterised. They are not — `ReturnStackTest.cpp:45`
  hardcodes 3, and the eviction-semantics tests assert positions that a capacity
  change invalidates. Not worth breaking four assertions for a marginal gain.

## Why not the SDK's own two-button style

`InputStyle::DigitalTwoButton` (`InputManager.cpp:380`) already synthesises
`BTN_BACK` from a held up key, `BTN_CONFIRM` from a held down key, and
`BTN_POWER` from both, and ships on PaperMono today. Adopting it is one
assignment, because `BoardConfig::ACTIVE` is a mutable `inline` variable
(`BoardConfig.h:1532`), not `constexpr` — no fork needed.

**It is still the wrong choice, for one reason.** Under that style a short press
emits `BTN_UP`/`BTN_DOWN` as *events only*, with no held state
(`InputManager.cpp:401-406`), so `isPressed(BTN_UP)` and `isPressed(BTN_DOWN)`
are permanently false. Three things read exactly that:

| Site | What breaks |
|---|---|
| `main.cpp:387-388` | **Recovery firmware mode** — DOWN + POWER held at boot |
| `main.cpp:653` | The screenshot combo (minor; a serial `SCREENSHOT` command exists) |
| `ButtonNavigator.cpp:57-58` | Hold-to-scroll in lists (minor on a touch device) |

The first is disqualifying. Recovery mode is what you hold when the device will
not boot, and this phase's entire justification is not stranding the user.

Synthesising in `HalGPIO` gives the same Back and Confirm **in addition to** the
raw state rather than instead of it, so all three keep working.

---

## What the hardware reports

`BoardConfig.h:1396`, marked *confirmed on hardware*:

```c
// {back, confirm, left, right, up, down, power, powerActiveHigh}
{PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 7, 3, false},
```

- The two physical nav keys are **`BTN_UP` (GPIO0, physically left)** and
  **`BTN_DOWN` (GPIO7, physically right)**. `BTN_LEFT`/`BTN_RIGHT` do not exist
  here; a plan grepping for them finds nothing.
- `BTN_BACK` and `BTN_CONFIRM` are unassigned pins, skipped by
  `InputManager::begin` (`InputManager.cpp:101-105`), so they are never
  configured and always read false. **Their indices are free for us to drive.**
- `SETTINGS.frontButtonBack` defaults to `FRONT_HW_BACK = 0` = `BTN_BACK`, and
  `frontButtonConfirm` to `1` = `BTN_CONFIRM` (`CrossPointSettings.h:82-83,242-243`).
  So `mapButton` already routes `Button::Back` to the index we are about to
  start driving. **That is why no call site needs touching.**
- `getHeldTime()` takes no button — it times from the first key down to all
  released (`InputManager.cpp:482-489`) — so per-key timing must be our own.

## The model

| Input | Result |
|---|---|
| Left key, short | `BTN_UP` (page back / previous) — unchanged |
| Right key, short | `BTN_DOWN` (page forward / next) — unchanged |
| **Left key, held** | **`BTN_BACK`** |
| **Right key, held** | **`BTN_CONFIRM`** |
| Home, short | Back — unchanged |
| Home, long | launcher — unchanged, and what 2b hangs the launcher on |
| Power | sleep — unchanged |
| Both keys + POWER at boot | recovery mode — unchanged |

Raw `BTN_UP`/`BTN_DOWN` state is untouched throughout.

## File structure

| File | Responsibility |
|---|---|
| `lib/Input/Input/NavKeyGestures.h` / `.cpp` | Pure: two keys plus a clock in, synthesised Back/Confirm edges out |
| `test/nav_key_gestures/` | Its host suite |

**Modified:** `lib/hal/HalGPIO.{h,cpp}` only. Nothing in `src/`.

---

## Task 1: The nav-key gesture machine

**Files:**
- Create: `lib/Input/Input/NavKeyGestures.h`, `lib/Input/Input/NavKeyGestures.cpp`
- Create: `test/nav_key_gestures/NavKeyGesturesTest.cpp`, `test/nav_key_gestures/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/nav_key_gestures/NavKeyGesturesTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "Input/NavKeyGestures.h"

namespace {

using input::NavKeyGestures;

// Drives the machine at a fixed key state, like the firmware loop. `tickMs`
// defaults to 50 because that is what the loop runs at once the device has been
// idle three seconds (HalPowerManager.h:33) -- the common case, not the fast one.
void run(NavKeyGestures& g, const bool left, const bool right, const uint32_t from, const uint32_t until,
         const uint32_t tickMs = 50) {
  for (uint32_t t = from; t <= until; t += tickMs) g.update(left, right, t);
}

TEST(NavKeyGestures, AShortPressSynthesisesNothing) {
  NavKeyGestures g;
  run(g, true, false, 0, 200);
  g.update(false, false, 250);
  EXPECT_FALSE(g.backHeld());
  EXPECT_FALSE(g.confirmHeld());
  EXPECT_FALSE(g.backReleasedThisTick());
  EXPECT_FALSE(g.confirmReleasedThisTick());
}

TEST(NavKeyGestures, AHeldLeftKeyBecomesBack) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.backHeld()) << "Back asserts WHILE held, so the user sees it without lifting";
  EXPECT_FALSE(g.confirmHeld());
}

TEST(NavKeyGestures, AHeldRightKeyBecomesConfirm) {
  NavKeyGestures g;
  run(g, false, true, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.confirmHeld());
  EXPECT_FALSE(g.backHeld());
}

TEST(NavKeyGestures, ReleasingAHeldKeyReportsTheReleaseExactlyOnce) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_TRUE(g.backReleasedThisTick());
  EXPECT_FALSE(g.backHeld());

  g.update(false, false, NavKeyGestures::HOLD_MS + 200);
  EXPECT_FALSE(g.backReleasedThisTick()) << "an edge that repeats is an edge a caller will act on twice";
}

// The page turn and the Back must never both happen. The reader pages on
// wasPressed by default, so this is the property that keeps a held key from
// turning a page on the way to Back.
TEST(NavKeyGestures, AKeyThatBecameAHoldSuppressesItsRawPress) {
  NavKeyGestures g;
  g.update(true, false, 0);
  EXPECT_FALSE(g.suppressRaw(input::NavKey::Left)) << "not yet a hold; a short press must still page";
  run(g, true, false, 50, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.suppressRaw(input::NavKey::Left));
}

TEST(NavKeyGestures, SuppressionClearsOnceTheKeyIsReleased) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_FALSE(g.suppressRaw(input::NavKey::Left)) << "the next press must page normally";
}

TEST(NavKeyGestures, TheTwoKeysAreIndependent) {
  NavKeyGestures g;
  run(g, true, true, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.backHeld());
  EXPECT_TRUE(g.confirmHeld()) << "holding both is not special; each key means what it means";
}

TEST(NavKeyGestures, HoldingOneKeyDoesNotSuppressTheOther) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.suppressRaw(input::NavKey::Left));
  EXPECT_FALSE(g.suppressRaw(input::NavKey::Right));
}

// millis() wraps after ~49 days. A signed or naive subtraction reads the wrap as
// a huge elapsed time and asserts Back on the very first touch of a key.
TEST(NavKeyGestures, SurvivesAClockWrap) {
  NavKeyGestures g;
  const uint32_t nearMax = UINT32_MAX - 20;
  g.update(true, false, nearMax);
  g.update(true, false, 10);  // wrapped: 30 ms elapsed, nowhere near HOLD_MS
  EXPECT_FALSE(g.backHeld()) << "a wrap must read as a short interval, not as held-forever";
}

TEST(NavKeyGestures, AKeyHeldFromTheVeryFirstTickIsIgnoredUntilReleased) {
  // Boot and wake deliberately absorb an already-held key (main.cpp:562-569),
  // and recovery mode holds DOWN through startup. A key that was already down
  // when we started looking must not become a Confirm.
  NavKeyGestures g;
  g.beginWithKeysDown(false, true, 0);
  run(g, false, true, 50, NavKeyGestures::HOLD_MS + 200);
  EXPECT_FALSE(g.confirmHeld());

  g.update(false, false, NavKeyGestures::HOLD_MS + 250);
  run(g, false, true, NavKeyGestures::HOLD_MS + 300, NavKeyGestures::HOLD_MS * 2 + 400);
  EXPECT_TRUE(g.confirmHeld()) << "a fresh press after the release works normally";
}

TEST(NavKeyGestures, HoldThresholdClearsTheReadersOwnHolds) {
  // ReaderUtils.h:18-19 -- SKIP_HOLD_MS 700 and BOOKMARK_HOLD_MS 400 both fire
  // off the raw key. Synthesised Back/Confirm must land clear of them so one
  // physical action does not mean two things.
  EXPECT_GT(NavKeyGestures::HOLD_MS, 700u);
  EXPECT_LT(NavKeyGestures::HOLD_MS, 1000u) << "GO_BACK_OR_HOME_MS is 1000; stay under it";
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target NavKeyGesturesTest
```

Expected: FAIL — `Input/NavKeyGestures.h: No such file or directory`.

- [ ] **Step 3: Write `lib/Input/Input/NavKeyGestures.h`**

```cpp
#pragma once

#include <cstdint>

// Turns the two nav keys into a Back and a Confirm this board does not have.
//
// BoardConfig.h:1396 leaves back/confirm/left/right PIN_UNASSIGNED on the X4
// Pro, so Back today exists only as a left-edge touch swipe and a capacitive
// Home key -- both of which die with the GT911. This is the GPIO-only route
// that survives that.
//
// Feed it both keys' CURRENT pressed state plus a monotonic millisecond clock,
// once per loop tick. It reports state and one-tick edges; it does not consume
// anything, so several callers may read it within the same tick.
namespace input {

enum class NavKey : uint8_t { Left, Right };

class NavKeyGestures {
 public:
  // Above the reader's own SKIP_HOLD_MS (700) so a held key means one thing,
  // and below GO_BACK_OR_HOME_MS (1000) so a delivered Back still reads as a
  // short press to the reader's back-destination branch.
  static constexpr uint32_t HOLD_MS = 850;

  // Both keys' state, and the clock. `nowMs` may wrap.
  void update(bool leftPressed, bool rightPressed, uint32_t nowMs);

  // Starts with one or both keys already down, marking them stale so they
  // cannot synthesise anything until released. Boot and wake absorb an
  // already-held key deliberately, and recovery mode holds one through startup.
  void beginWithKeysDown(bool leftPressed, bool rightPressed, uint32_t nowMs);

  bool backHeld() const { return left_.synthesised; }
  bool confirmHeld() const { return right_.synthesised; }
  bool backReleasedThisTick() const { return left_.releasedEdge; }
  bool confirmReleasedThisTick() const { return right_.releasedEdge; }
  bool backPressedThisTick() const { return left_.pressedEdge; }
  bool confirmPressedThisTick() const { return right_.pressedEdge; }

  // True once a key has become a hold, so the caller can hide that key's raw
  // press. Without it a held key pages AND opens Back -- the reader pages on
  // wasPressed by default (ReaderUtils.h:53).
  bool suppressRaw(NavKey key) const;

 private:
  struct Key {
    bool down = false;
    bool stale = false;  // was already down when we started looking
    bool synthesised = false;
    bool pressedEdge = false;
    bool releasedEdge = false;
    uint32_t downAtMs = 0;
  };

  void updateKey(Key& key, bool pressed, uint32_t nowMs);

  Key left_;
  Key right_;
};

}  // namespace input
```

- [ ] **Step 4: Write `lib/Input/Input/NavKeyGestures.cpp`**

```cpp
#include "Input/NavKeyGestures.h"

namespace input {
namespace {

// Unsigned subtraction, so a millis() wrap yields the true short interval
// rather than ~49 days. Reading a wrap as "held forever" would assert Back on
// the first key touched after 49 days of uptime.
uint32_t elapsed(const uint32_t from, const uint32_t now) { return now - from; }

}  // namespace

void NavKeyGestures::updateKey(Key& key, const bool pressed, const uint32_t nowMs) {
  key.pressedEdge = false;
  key.releasedEdge = false;

  if (pressed && !key.down) {
    key.down = true;
    key.downAtMs = nowMs;
    return;
  }

  if (!pressed && key.down) {
    key.down = false;
    key.stale = false;  // a real release clears staleness; the next press is genuine
    if (key.synthesised) {
      key.synthesised = false;
      key.releasedEdge = true;
    }
    return;
  }

  if (pressed && key.down && !key.stale && !key.synthesised && elapsed(key.downAtMs, nowMs) >= HOLD_MS) {
    key.synthesised = true;
    key.pressedEdge = true;
  }
}

void NavKeyGestures::update(const bool leftPressed, const bool rightPressed, const uint32_t nowMs) {
  updateKey(left_, leftPressed, nowMs);
  updateKey(right_, rightPressed, nowMs);
}

void NavKeyGestures::beginWithKeysDown(const bool leftPressed, const bool rightPressed, const uint32_t nowMs) {
  left_ = Key{};
  right_ = Key{};
  left_.down = leftPressed;
  left_.stale = leftPressed;
  left_.downAtMs = nowMs;
  right_.down = rightPressed;
  right_.stale = rightPressed;
  right_.downAtMs = nowMs;
}

bool NavKeyGestures::suppressRaw(const NavKey key) const {
  return key == NavKey::Left ? left_.synthesised : right_.synthesised;
}

}  // namespace input
```

- [ ] **Step 5: Register the test**

`test/nav_key_gestures/CMakeLists.txt`:

```cmake
add_executable(NavKeyGesturesTest
  NavKeyGesturesTest.cpp
  ${REPO_ROOT}/lib/Input/Input/NavKeyGestures.cpp
)

target_include_directories(NavKeyGesturesTest PRIVATE ${REPO_ROOT}/lib/Input)

target_link_libraries(NavKeyGesturesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NavKeyGesturesTest)
```

Append `add_subdirectory(nav_key_gestures)` to `test/CMakeLists.txt`.

> The nested `lib/Input/Input/` layout is deliberate: PlatformIO puts
> `lib/<Name>` on the include path and never `lib` itself, so
> `#include "Input/NavKeyGestures.h"` needs the extra level. Same reason
> `lib/Epub/Epub/` and `lib/StudyStore/StudyStore/` are shaped that way. The
> host suite adds `lib` directly and hides the mistake until the first `pio run`.

- [ ] **Step 6: Run and watch it pass.** Expected: `[  PASSED  ] 11 tests.`

- [ ] **Step 7: Commit**

```bash
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
git add lib/Input test/nav_key_gestures test/CMakeLists.txt
git commit -m "feat: synthesise Back and Confirm from the two nav keys"
```

> `clang-format-fix` warns and **exits 0** without the venv on PATH, doing
> nothing. That has cost this project two red builds already.

---

## Task 2: Wire it into `HalGPIO`

This is the whole integration. `HalGPIO` is our code (`lib/hal/`), it already
wraps every button read, and `SETTINGS.frontButtonBack` already resolves to
`BTN_BACK` — so driving those two indices here reaches all 623 call sites with
no edits anywhere else.

**Files:**
- Modify: `lib/hal/HalGPIO.h`, `lib/hal/HalGPIO.cpp`

- [ ] **Step 1: Add the machine to the header**

```cpp
#include "Input/NavKeyGestures.h"
```

and, as private members:

```cpp
  // BTN_BACK and BTN_CONFIRM are PIN_UNASSIGNED on this board, so the SDK never
  // drives them and their indices are ours. Synthesising here rather than in
  // MappedInputManager is what makes wasPressed, isPressed and the composed
  // NavNext/NavPrevious all agree -- patching the layer above would have left
  // every one of them reading the raw pins.
  input::NavKeyGestures navGestures;
  bool navGesturesPrimed = false;
```

- [ ] **Step 2: Feed it in `HalGPIO::update()`**

```cpp
void HalGPIO::update() {
  inputMgr.update();

  const bool left = inputMgr.isPressed(BTN_UP);
  const bool right = inputMgr.isPressed(BTN_DOWN);
  if (!navGesturesPrimed) {
    // A key already down on the first tick is absorbed, never synthesised:
    // main.cpp:562-569 deliberately lets a button held at startup settle
    // without an edge, and recovery mode (main.cpp:387) holds DOWN through it.
    navGestures.beginWithKeysDown(left, right, millis());
    navGesturesPrimed = true;
  } else {
    navGestures.update(left, right, millis());
  }

  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}
```

- [ ] **Step 3: Answer the three queries for the synthesised indices**

Each existing method gains a branch ahead of its passthrough. `BTN_UP` and
`BTN_DOWN` keep reporting the raw pins unchanged — recovery mode, the screenshot
combo and hold-to-scroll all read those.

```cpp
bool HalGPIO::isPressed(const uint8_t buttonIndex) const {
  if (buttonIndex == BTN_BACK) return navGestures.backHeld();
  if (buttonIndex == BTN_CONFIRM) return navGestures.confirmHeld();
  return inputMgr.isPressed(buttonIndex);
}

bool HalGPIO::wasPressed(const uint8_t buttonIndex) const {
  if (buttonIndex == BTN_BACK) return navGestures.backPressedThisTick();
  if (buttonIndex == BTN_CONFIRM) return navGestures.confirmPressedThisTick();
  // A key that became a hold must not ALSO report its raw press: the reader
  // pages on wasPressed by default (ReaderUtils.h:53), so without this a held
  // key turns a page on its way to Back.
  if (buttonIndex == BTN_UP && navGestures.suppressRaw(input::NavKey::Left)) return false;
  if (buttonIndex == BTN_DOWN && navGestures.suppressRaw(input::NavKey::Right)) return false;
  return inputMgr.wasPressed(buttonIndex);
}

bool HalGPIO::wasReleased(const uint8_t buttonIndex) const {
  if (buttonIndex == BTN_BACK) return navGestures.backReleasedThisTick();
  if (buttonIndex == BTN_CONFIRM) return navGestures.confirmReleasedThisTick();
  if (buttonIndex == BTN_UP && navGestures.suppressRaw(input::NavKey::Left)) return false;
  if (buttonIndex == BTN_DOWN && navGestures.suppressRaw(input::NavKey::Right)) return false;
  return inputMgr.wasReleased(buttonIndex);
}
```

> **Suppression covers `isPressed` too — the asymmetry in the first draft of
> this task was wrong.** `ButtonNavigator` starts continuous list scrolling at
> `continuousStartMs = 500` (`ButtonNavigator.h:20`) off `isPressed`, and Back
> lands at 850. Leaving `isPressed` unsuppressed means one hold scrolls the list
> *and then* goes back — one physical action, two meanings, which is exactly
> what this task exists to prevent.
>
> **Recovery mode is safe anyway, and not by luck.** It holds the key from boot,
> so `beginWithKeysDown` marks it stale; a stale key never synthesises, so it is
> never suppressed and `isPressed` reports it normally. The screenshot combo is
> safe for a different reason: it latches on the first tick both keys are down,
> long before 850 ms.
>
> So `isPressed` is suppressed as well:
>
> ```cpp
> bool HalGPIO::isPressed(const uint8_t buttonIndex) const {
>   if (buttonIndex == BTN_BACK) return navGestures.backHeld();
>   if (buttonIndex == BTN_CONFIRM) return navGestures.confirmHeld();
>   if (buttonIndex == BTN_UP && navGestures.suppressRaw(input::NavKey::Left)) return false;
>   if (buttonIndex == BTN_DOWN && navGestures.suppressRaw(input::NavKey::Right)) return false;
>   return inputMgr.isPressed(buttonIndex);
> }
> ```

- [ ] **Step 4: Build and check with CI's own flags**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
/Volumes/stein/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

`pio check` fails CI on `low`, so a bare `pio check` is not the same gate.

- [ ] **Step 5: Commit**

```bash
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
git add lib/hal/HalGPIO.h lib/hal/HalGPIO.cpp
git commit -m "feat: drive the unassigned Back and Confirm indices from the nav keys"
```

---

## Task 3: Device verification

The way this breaks is that you cannot leave a screen, or cannot recover a
device that will not boot. Test those first.

- [ ] **Step 1: Side-load the dev build** — OTA installs the release build with
      `LOG_LEVEL=0` and no serial.

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
curl -H "Expect:" -F "file=@.pio/build/x4pro/firmware.bin" "http://<device-ip>/upload?path=/"
```

Then *Settings → SD firmware update*. The `Expect:` suppression is required or
the transfer hangs.

- [ ] **Step 2: Recovery mode still works — do this before anything else**

  Power on holding the **right** key (DOWN) + POWER. Expect the recovery
  firmware log line. If this fails, stop and revert: it is the escape hatch.

- [ ] **Step 3: The escapes, from a settings sub-screen**

  | Gesture | Expected |
  |---|---|
  | Left key, held ~1 s | back one level |
  | Home, short | back one level |
  | Left-edge swipe | back one level |

- [ ] **Step 4: What must NOT happen**

  | Action | Must not |
  |---|---|
  | Left key, short, in the reader | anything but one page back |
  | Right key, short, in the reader | anything but one page forward |
  | Left key, held | turn a page on the way to Back, or on release |
  | Right key, held | turn a page on the way to Confirm, or on release |

  A page turn here means the `suppressRaw` wiring is wrong.

- [ ] **Step 5: The screens the last review found uncovered**

  | Screen | Check |
  |---|---|
  | WiFi password entry | type and submit a password with buttons only |
  | Settings list | hold a key to scroll continuously — must still work |
  | A tabbed settings screen | hold to step tabs — must not double-step |
  | Reader, `longPressButtonBehavior = CHAPTER_SKIP` | chapter skip still reachable |
  | Reader, `longPressMenuFunction = LP_MENU_BOOKMARK` | the reader menu still reachable |

- [ ] **Step 6: Report the tables filled in**, not a summary claim.

---

## Self-review

**Spec coverage.** "There must be a Confirm" → Task 1. "Back must not depend on
the touch controller" → Tasks 1–2, verified in Task 3 Step 3. The input-model
table → Task 2.

**Deliberately deferred:**

- The launcher and the four sections → **2b**, which hangs the launcher on the
  existing Home-long and is where `MappedInputManager` finally goes.
- Long-press-to-anchor selection and reader swipes → **2c**.
- `ReturnStack` capacity: **left at 3.** The tests are not parameterised and the
  gain is marginal; revisit if 2b's chrome wants a return indicator.

**Corrections to the spec this plan makes:**

1. The chord it describes is not implementable safely at a 50 ms tick. Back is a
   held left key instead.
2. `MappedInputManager` is not deleted here. It is untouched — synthesising
   below it means it needs no changes at all. It goes in 2b.
3. Home-long already opens the launcher; the spec presents it as new work.
