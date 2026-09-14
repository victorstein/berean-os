# bereanOS Phase 2a — the input model

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give this device a Back and a Confirm that exist in hardware, replace `MappedInputManager`'s four-front-button model with one built for two keys and a capacitive Home, and do it without ever leaving the user on a screen they cannot leave.

**Architecture:** A pure gesture state machine takes the two nav keys' pressed/released state plus a clock and emits logical events (Back, Confirm, Launcher, Previous, Next). It is host-tested exhaustively because it is the part that can strand the user. A thin firmware adapter feeds it from `HalGPIO` and presents the same `Button` vocabulary the 121 existing call sites already use, so `MappedInputManager` is replaced rather than ripped out.

**Tech Stack:** C++20, GoogleTest on the host, PlatformIO/ESP-IDF on the device, FreeRTOS.

---

## Why Phase 2 is three plans, not one

The spec's Phase 2 bundles the launcher shell, the four sections, the new input
model, two-tap selection, and deleting `MappedInputManager`. Those are different
subsystems with different risk, and one of them gates the others:

| | Plan | Independently shippable? |
|---|---|---|
| **2a** | The input model — this plan | Yes. No screen changes; every existing activity keeps working, with a Back that exists. |
| **2b** | Launcher shell and the four sections | Yes, once 2a lands — a launcher you cannot back out of is not shippable. |
| **2c** | Long-press-to-anchor, tap-to-finish selection | Yes. Confined to the reader. |

2a comes first because **this device currently has no Back on any physical
button.** That is not a design opinion; it is what the board config says.

---

## What the hardware actually reports

`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1396`, the X4 Pro's
digital-button row, commented *confirmed on hardware*:

```c
// {back, confirm, left, right, up, down, power, powerActiveHigh}
{PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 7, 3, false},
```

Read that carefully, because three things follow and the spec states two of them
imprecisely:

1. **The two physical nav keys are `BTN_UP` and `BTN_DOWN`, not `BTN_LEFT` and
   `BTN_RIGHT`.** Physically they sit left (GPIO0) and right (GPIO7); the board
   config wires them to the reader's page pair. Every mention of "the Left+Right
   chord" in the spec means `BTN_UP` + `BTN_DOWN` in code. A plan that greps for
   `BTN_LEFT` finds nothing on this board.

2. **`BTN_BACK`, `BTN_CONFIRM`, `BTN_LEFT` and `BTN_RIGHT` are all
   `PIN_UNASSIGNED` (-1).** `InputManager::begin` skips any pin below zero
   (`InputManager.cpp:101-105`), so they are never configured and never read as
   pressed. `MappedInputManager::mapButton` routes `Button::Back` through
   `SETTINGS.frontButtonBack` into one of those dead indices
   (`MappedInputManager.cpp:59-62`).

   **So `Button::Back` from a physical button is already dead on this device.**
   Today's Back comes only from a left-edge touch swipe
   (`MappedInputManager.cpp:266,301`) and the GT911 capacitive Home key. Both die
   with the touch controller. That is the hole this plan closes, and it is a real
   one rather than a hypothetical: Phase 0 deleted the button-remap settings
   screen, which was the recovery path.

3. **`getHeldTime()` takes no button.** It measures from `buttonPressStart` — set
   when the first button of a press goes down — to release
   (`InputManager.cpp:482-489`). With two keys held it times the chord, not
   either key. Any per-key long-press must keep its own clock.

`isPressed` reads a bitmask (`InputManager.cpp:472`), so both keys can be
observed down at once. That is what makes a chord detectable at all.

---

## The model this implements

| Input | Everywhere | In the reader |
|---|---|---|
| Tap | activates what you touched | outer thirds page, centre opens the menu |
| Left key / Right key, short | previous / next in sequence | previous / next page |
| **Right key, long** | **Confirm** | — |
| **Both keys, short** | **Back** | Back |
| **Both keys, held** | **launcher** | launcher |
| Home, short | Back | closes overlay, else Back |
| Home, long | launcher | launcher |
| Power | sleep | sleep |

The three-zone tap in the reader is untouched — it is the most frequent
interaction on the device and the spec is explicit that it stays.

**Why the chord is Back and not something rarer:** Back must not depend on the
touch controller. Home is a GT911 capacitive bit, so a controller that NAKs after
an ESD event takes Home *and* the left-edge swipe with it. The chord routes
through GPIO only and needs no configuration, so it is the recovery path that
survives.

**Why long-press on the Right key is Confirm:** neither key has any other
long-press meaning, so it costs nothing. The first draft of the spec had no
Confirm at all while claiming buttons were a one-handed fallback; they were a
scrollbar.

---

## File structure

**New, host-testable, no Arduino:**

| File | Responsibility |
|---|---|
| `lib/Input/Input/GestureState.h` / `.cpp` | The state machine: two keys plus a clock in, logical events out |
| `lib/Input/Input/HomeLadder.h` / `.cpp` | What Home means right now, given what is on screen |

**New, firmware-only:**

| File | Responsibility |
|---|---|
| `src/input/BereanInput.{h,cpp}` | Feeds `GestureState` from `HalGPIO`, exposes the `Button` vocabulary the activities already speak |

**Modified:** `src/activities/Activity.h` (the base-class reference type),
`src/main.cpp` (construction and the per-tick feed), and
`src/MappedInputManager.{h,cpp}` — which is **reduced to a shim over
`BereanInput`, not deleted in this plan.** See the migration note below.

---

## `MappedInputManager` is shimmed, then emptied

`CLAUDE.md` states the constraint plainly: it sits in the `Activity` base-class
constructor, spans 418 references across 121 files, and *implements* this
device's Back. It may only be removed in the same change that lands its
replacement.

This plan lands the replacement and points `MappedInputManager`'s methods at it,
leaving the 418 call sites untouched and compiling. That keeps every step of this
plan shippable. Deleting the shim is bookkeeping for 2b, once the launcher owns
navigation and the remaining call sites can be swept in one pass with a working
device at every commit.

A rename that touches 121 files and an input-semantics change in one commit is
not reviewable, and this is the subsystem where an unreviewable mistake means a
device you cannot navigate.

---

## Task 1: The gesture state machine

The whole reason this is a separate, pure unit: it is the code that decides
whether the user can leave a screen. It must be exhaustively testable without a
device, and its edge cases — a chord where one key lands a frame late, a chord
that becomes a hold, a key released while the other is still down — are exactly
the ones a human tester will not think to try.

**Files:**
- Create: `lib/Input/Input/GestureState.h`, `lib/Input/Input/GestureState.cpp`
- Create: `test/gesture_state/GestureStateTest.cpp`, `test/gesture_state/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/gesture_state/GestureStateTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "Input/GestureState.h"

namespace {

using input::Gesture;
using input::GestureState;

// Drives the machine to `until` ms with a fixed key state, returning the last
// gesture emitted. Ticks every 10 ms, like the firmware loop.
Gesture run(GestureState& s, const bool left, const bool right, const uint32_t from, const uint32_t until) {
  Gesture last = Gesture::None;
  for (uint32_t t = from; t <= until; t += 10) {
    const Gesture g = s.update(left, right, t);
    if (g != Gesture::None) last = g;
  }
  return last;
}

TEST(GestureState, AShortLeftPressIsPrevious) {
  GestureState s;
  run(s, true, false, 0, 100);
  EXPECT_EQ(s.update(false, false, 110), Gesture::Previous);
}

TEST(GestureState, AShortRightPressIsNext) {
  GestureState s;
  run(s, false, true, 0, 100);
  EXPECT_EQ(s.update(false, false, 110), Gesture::Next);
}

TEST(GestureState, ALongRightPressIsConfirm) {
  GestureState s;
  const Gesture g = run(s, false, true, 0, GestureState::LONG_PRESS_MS + 50);
  EXPECT_EQ(g, Gesture::Confirm) << "Confirm fires while held, so the user sees it without lifting";
}

TEST(GestureState, ALongRightPressDoesNotAlsoEmitNextOnRelease) {
  GestureState s;
  run(s, false, true, 0, GestureState::LONG_PRESS_MS + 50);
  EXPECT_EQ(s.update(false, false, GestureState::LONG_PRESS_MS + 60), Gesture::None)
      << "one physical action must not produce both Confirm and a page turn";
}

TEST(GestureState, ALongLeftPressEmitsNothing) {
  GestureState s;
  const Gesture g = run(s, true, false, 0, GestureState::LONG_PRESS_MS + 50);
  EXPECT_EQ(g, Gesture::None) << "only the right key carries Confirm; the left one has no long meaning";
}

TEST(GestureState, BothKeysReleasedTogetherIsBack) {
  GestureState s;
  run(s, true, true, 0, 100);
  EXPECT_EQ(s.update(false, false, 110), Gesture::Back);
}

// The case a human tester will never reproduce deliberately: two mechanical
// keys do not close on the same scan. The second must still join the chord.
TEST(GestureState, AChordIsRecognisedWhenTheSecondKeyLandsLate) {
  GestureState s;
  s.update(true, false, 0);
  run(s, true, true, 10, 120);
  EXPECT_EQ(s.update(false, false, 130), Gesture::Back)
      << "a key landing within the join window joins the chord rather than paging";
}

TEST(GestureState, AKeyLandingAfterTheJoinWindowIsNotAChord) {
  GestureState s;
  run(s, true, false, 0, GestureState::CHORD_JOIN_MS + 50);
  run(s, true, true, GestureState::CHORD_JOIN_MS + 60, GestureState::CHORD_JOIN_MS + 120);
  EXPECT_NE(s.update(false, false, GestureState::CHORD_JOIN_MS + 130), Gesture::Back)
      << "holding one key and later adding the other is not a deliberate chord";
}

TEST(GestureState, HoldingBothKeysOpensTheLauncher) {
  GestureState s;
  const Gesture g = run(s, true, true, 0, GestureState::CHORD_HOLD_MS + 50);
  EXPECT_EQ(g, Gesture::Launcher);
}

TEST(GestureState, AChordThatBecameALauncherDoesNotAlsoEmitBack) {
  GestureState s;
  run(s, true, true, 0, GestureState::CHORD_HOLD_MS + 50);
  EXPECT_EQ(s.update(false, false, GestureState::CHORD_HOLD_MS + 60), Gesture::None);
}

// Releasing one key of a chord must not page. The user is mid-chord, and a page
// turn here is the most visible possible wrong answer on an e-ink panel.
TEST(GestureState, ReleasingOneKeyOfAChordEmitsNothing) {
  GestureState s;
  run(s, true, true, 0, 100);
  EXPECT_EQ(s.update(false, true, 110), Gesture::None);
  EXPECT_EQ(s.update(false, false, 120), Gesture::None)
      << "the chord already resolved; the trailing key is not a fresh press";
}

TEST(GestureState, APressAfterAResolvedChordWorksNormally) {
  GestureState s;
  run(s, true, true, 0, 100);
  s.update(false, false, 110);
  run(s, false, true, 200, 260);
  EXPECT_EQ(s.update(false, false, 270), Gesture::Next) << "the machine must not latch";
}

TEST(GestureState, AnIdleMachineEmitsNothing) {
  GestureState s;
  EXPECT_EQ(s.update(false, false, 0), Gesture::None);
  EXPECT_EQ(s.update(false, false, 5000), Gesture::None);
}

// millis() wraps after ~49 days of uptime. A machine that reads a negative
// elapsed time as "held forever" would fire Launcher on the next key press.
TEST(GestureState, SurvivesAClockWrap) {
  GestureState s;
  const uint32_t nearMax = UINT32_MAX - 50;
  s.update(false, true, nearMax);
  s.update(false, true, 10);  // wrapped
  EXPECT_EQ(s.update(false, false, 20), Gesture::Next)
      << "elapsed time must be computed with unsigned wrap arithmetic";
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target GestureStateTest
```

Expected: FAIL — `Input/GestureState.h: No such file or directory`.

- [ ] **Step 3: Write `lib/Input/Input/GestureState.h`**

```cpp
#pragma once

#include <cstdint>

// What the two nav keys mean, as a pure state machine.
//
// This device has exactly two digital nav keys and no Back or Confirm button
// (BoardConfig.h:1396 leaves both PIN_UNASSIGNED), so every logical action
// beyond paging has to come from a combination. Getting that wrong strands the
// user on a screen with no way out, which is why this is a dependency-free unit
// with a host suite rather than logic scattered through an activity.
//
// Feed it the two keys' CURRENT pressed state plus a monotonic millisecond
// clock, once per loop tick. It returns at most one gesture per call.
namespace input {

enum class Gesture : uint8_t {
  None,
  Previous,  // left key, short
  Next,      // right key, short
  Confirm,   // right key, held
  Back,      // both keys, short
  Launcher,  // both keys, held
};

class GestureState {
 public:
  // Held past this, the right key is Confirm rather than a page turn. Matches
  // the threshold the reader already treats as "a hold, not a tap" elsewhere.
  static constexpr uint32_t LONG_PRESS_MS = 700;
  // Two mechanical keys never close on the same scan. A second key arriving
  // within this window joins the first rather than being a separate press.
  static constexpr uint32_t CHORD_JOIN_MS = 120;
  // Both keys held past this is the launcher, not Back.
  static constexpr uint32_t CHORD_HOLD_MS = 900;

  // `nowMs` may wrap; elapsed time is computed with unsigned arithmetic so a
  // wrap reads as a small positive interval rather than a huge one.
  Gesture update(bool leftPressed, bool rightPressed, uint32_t nowMs);

  void reset();

 private:
  enum class Phase : uint8_t {
    Idle,
    OneKeyDown,   // a single key is down and could still become a chord
    ChordDown,    // both keys down, not yet resolved
    Resolved,     // a gesture already fired; waiting for all keys to lift
  };

  Phase phase_ = Phase::Idle;
  bool firstWasLeft_ = false;
  uint32_t downAtMs_ = 0;
};

}  // namespace input
```

- [ ] **Step 4: Write `lib/Input/Input/GestureState.cpp`**

```cpp
#include "Input/GestureState.h"

namespace input {
namespace {

// Unsigned subtraction, so a millis() wrap yields the true short interval
// rather than ~49 days. Reading a wrap as "held forever" would fire Launcher on
// the first key press after 49 days of uptime.
uint32_t elapsed(const uint32_t from, const uint32_t now) { return now - from; }

}  // namespace

void GestureState::reset() {
  phase_ = Phase::Idle;
  firstWasLeft_ = false;
  downAtMs_ = 0;
}

Gesture GestureState::update(const bool leftPressed, const bool rightPressed, const uint32_t nowMs) {
  const bool anyDown = leftPressed || rightPressed;
  const bool bothDown = leftPressed && rightPressed;

  switch (phase_) {
    case Phase::Idle:
      if (!anyDown) return Gesture::None;
      phase_ = bothDown ? Phase::ChordDown : Phase::OneKeyDown;
      firstWasLeft_ = leftPressed && !rightPressed;
      downAtMs_ = nowMs;
      return Gesture::None;

    case Phase::OneKeyDown:
      // A second key within the join window makes this a chord, timed from the
      // FIRST key so the hold threshold measures the whole gesture.
      if (bothDown) {
        if (elapsed(downAtMs_, nowMs) <= CHORD_JOIN_MS) {
          phase_ = Phase::ChordDown;
          return Gesture::None;
        }
        // Too late to be deliberate: treat it as the original key still held.
        phase_ = Phase::Resolved;
        return Gesture::None;
      }
      if (!anyDown) {
        phase_ = Phase::Idle;
        return firstWasLeft_ ? Gesture::Previous : Gesture::Next;
      }
      // Still one key down. Only the right key carries a hold meaning.
      if (!firstWasLeft_ && elapsed(downAtMs_, nowMs) >= LONG_PRESS_MS) {
        phase_ = Phase::Resolved;
        return Gesture::Confirm;
      }
      return Gesture::None;

    case Phase::ChordDown:
      if (elapsed(downAtMs_, nowMs) >= CHORD_HOLD_MS) {
        phase_ = Phase::Resolved;
        return Gesture::Launcher;
      }
      if (!bothDown) {
        // One or both lifted before the hold threshold. Either way the chord is
        // over; a trailing single key must not page.
        phase_ = anyDown ? Phase::Resolved : Phase::Idle;
        return Gesture::Back;
      }
      return Gesture::None;

    case Phase::Resolved:
      if (!anyDown) phase_ = Phase::Idle;
      return Gesture::None;
  }
  return Gesture::None;
}

}  // namespace input
```

- [ ] **Step 5: Register the test**

`test/gesture_state/CMakeLists.txt`:

```cmake
add_executable(GestureStateTest
  GestureStateTest.cpp
  ${REPO_ROOT}/lib/Input/Input/GestureState.cpp
)

target_include_directories(GestureStateTest PRIVATE ${REPO_ROOT}/lib/Input)

target_link_libraries(GestureStateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GestureStateTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(gesture_state)
```

> The nested `lib/Input/Input/` layout is not a typo. PlatformIO puts
> `lib/<Name>` on the include path and never `lib` itself, so
> `#include "Input/GestureState.h"` only resolves with the extra level — the
> same reason `lib/Epub/Epub/` and `lib/StudyStore/StudyStore/` are shaped that
> way. The host suite adds `lib` directly and so hides the mistake until the
> first `pio run`.

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target GestureStateTest && ./build/gesture_state/GestureStateTest
```

Expected: `[  PASSED  ] 14 tests.`

- [ ] **Step 7: Commit**

```bash
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
git add lib/Input test/gesture_state test/CMakeLists.txt
git commit -m "feat: decide what the two nav keys mean, as a testable machine"
```

> `clang-format-fix` prints a warning and **exits 0** when clang-format is not on
> PATH, so without the venv prefix it silently does nothing and CI fails on work
> that looked clean. This has now cost this project two red builds.

---

## Task 2: What Home means right now

"Home is always Back" has several claimants and they conflict. The spec's ladder,
highest priority first, with the reasoning that makes each non-obvious:

1. **A modal overlay is open** → dismiss it. Anything else discards the overlay's
   context along with it.
2. **A gesture is in progress** (a half-made two-tap selection) → cancel the
   gesture only. Leaving the screen mid-selection loses the anchor silently.
3. **A destructive operation is in flight** (download, OTA) → Home is **inert**.
   OTA especially: `enterDeepSleep` mid-write bricks the partition.
4. **The return stack is non-empty** → go back to where the citation was followed
   from.
5. **Otherwise** → up one level.

**Files:**
- Create: `lib/Input/Input/HomeLadder.h`, `lib/Input/Input/HomeLadder.cpp`
- Create: `test/home_ladder/HomeLadderTest.cpp`, `test/home_ladder/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/home_ladder/HomeLadderTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "Input/HomeLadder.h"

namespace {

using input::HomeAction;
using input::ScreenContext;

ScreenContext ctx() { return ScreenContext{}; }

TEST(HomeLadder, AnOpenOverlayIsDismissedFirst) {
  ScreenContext c = ctx();
  c.overlayOpen = true;
  c.gestureInProgress = true;
  c.returnStackDepth = 2;
  EXPECT_EQ(input::resolveHome(c), HomeAction::DismissOverlay);
}

TEST(HomeLadder, AGestureIsCancelledBeforeAnythingElseMoves) {
  ScreenContext c = ctx();
  c.gestureInProgress = true;
  c.returnStackDepth = 2;
  EXPECT_EQ(input::resolveHome(c), HomeAction::CancelGesture);
}

TEST(HomeLadder, HomeIsInertDuringADestructiveOperation) {
  ScreenContext c = ctx();
  c.destructiveOperationInFlight = true;
  c.returnStackDepth = 2;
  EXPECT_EQ(input::resolveHome(c), HomeAction::Ignore)
      << "leaving mid-OTA is how a partition gets half-written";
}

TEST(HomeLadder, AnOverlayStillWinsOverADestructiveOperation) {
  ScreenContext c = ctx();
  c.overlayOpen = true;
  c.destructiveOperationInFlight = true;
  EXPECT_EQ(input::resolveHome(c), HomeAction::DismissOverlay)
      << "dismissing a progress popup must not be confused with cancelling the work";
}

TEST(HomeLadder, ANonEmptyReturnStackReturns) {
  ScreenContext c = ctx();
  c.returnStackDepth = 1;
  EXPECT_EQ(input::resolveHome(c), HomeAction::PopReturnStack);
}

TEST(HomeLadder, OtherwiseItGoesUpOneLevel) {
  EXPECT_EQ(input::resolveHome(ctx()), HomeAction::Up);
}

TEST(HomeLadder, ALongHomeIsAlwaysTheLauncherExceptMidDestructiveWork) {
  ScreenContext c = ctx();
  c.overlayOpen = true;
  c.returnStackDepth = 3;
  EXPECT_EQ(input::resolveHomeHold(c), HomeAction::Launcher);

  c.destructiveOperationInFlight = true;
  EXPECT_EQ(input::resolveHomeHold(c), HomeAction::Ignore);
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail.** Expected: `Input/HomeLadder.h: No such file or directory`.

- [ ] **Step 3: Write `lib/Input/Input/HomeLadder.h`**

```cpp
#pragma once

#include <cstdint>

// What Home means, given what is on screen. Separated from the activities so
// the precedence is stated once and tested, rather than re-derived — slightly
// differently — in each screen that handles Home.
namespace input {

struct ScreenContext {
  bool overlayOpen = false;
  bool gestureInProgress = false;
  bool destructiveOperationInFlight = false;
  uint8_t returnStackDepth = 0;
};

enum class HomeAction : uint8_t {
  Ignore,
  DismissOverlay,
  CancelGesture,
  PopReturnStack,
  Up,
  Launcher,
};

HomeAction resolveHome(const ScreenContext& context);
HomeAction resolveHomeHold(const ScreenContext& context);

}  // namespace input
```

- [ ] **Step 4: Write `lib/Input/Input/HomeLadder.cpp`**

```cpp
#include "Input/HomeLadder.h"

namespace input {

HomeAction resolveHome(const ScreenContext& context) {
  // An overlay outranks even a download: dismissing a progress popup is a
  // display decision, not a decision to abandon the transfer.
  if (context.overlayOpen) return HomeAction::DismissOverlay;
  if (context.gestureInProgress) return HomeAction::CancelGesture;
  if (context.destructiveOperationInFlight) return HomeAction::Ignore;
  if (context.returnStackDepth > 0) return HomeAction::PopReturnStack;
  return HomeAction::Up;
}

HomeAction resolveHomeHold(const ScreenContext& context) {
  if (context.destructiveOperationInFlight) return HomeAction::Ignore;
  return HomeAction::Launcher;
}

}  // namespace input
```

- [ ] **Step 5: Register the test** — sources are `HomeLadder.cpp`; include dir
      `${REPO_ROOT}/lib/Input`; append `add_subdirectory(home_ladder)` to
      `test/CMakeLists.txt`. Same shape as Task 1's CMakeLists.

- [ ] **Step 6: Run and watch it pass.** Expected: `[  PASSED  ] 7 tests.`

- [ ] **Step 7: Commit**

```bash
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
git add lib/Input/Input/HomeLadder.h lib/Input/Input/HomeLadder.cpp test/home_ladder test/CMakeLists.txt
git commit -m "feat: state the Home precedence ladder once, and test it"
```

---

## Task 3: The firmware adapter, and the shim

**Files:**
- Create: `src/input/BereanInput.{h,cpp}`
- Modify: `src/MappedInputManager.{h,cpp}`, `src/main.cpp`

- [ ] **Step 1: Write `src/input/BereanInput.h`**

```cpp
#pragma once

#include <HalGPIO.h>

#include "Input/GestureState.h"
#include "Input/HomeLadder.h"

// Feeds GestureState from the two digital nav keys and exposes the result.
//
// The keys are HalGPIO::BTN_UP (GPIO0, physically the LEFT key) and
// HalGPIO::BTN_DOWN (GPIO7, physically the RIGHT key). BoardConfig.h:1396
// leaves back/confirm/left/right PIN_UNASSIGNED on this board, so those indices
// are never configured and never read as pressed -- reading them is not wrong,
// it is just permanently false.
class BereanInput {
 public:
  explicit BereanInput(HalGPIO& gpio) : gpio_(gpio) {}

  // Called once per loop tick, before anything consumes a gesture.
  void update(uint32_t nowMs);

  // At most one per tick; consuming it clears it.
  input::Gesture takeGesture();

  bool homeTapped() const;
  bool homeHeld() const;

 private:
  HalGPIO& gpio_;
  input::GestureState machine_;
  input::Gesture pending_ = input::Gesture::None;
};
```

- [ ] **Step 2: Write `src/input/BereanInput.cpp`**

```cpp
#include "BereanInput.h"

void BereanInput::update(const uint32_t nowMs) {
  const input::Gesture g =
      machine_.update(gpio_.isPressed(HalGPIO::BTN_UP), gpio_.isPressed(HalGPIO::BTN_DOWN), nowMs);
  // Latch rather than overwrite: a consumer that runs after a screen transition
  // would otherwise lose the gesture that caused the transition.
  if (g != input::Gesture::None) pending_ = g;
}

input::Gesture BereanInput::takeGesture() {
  const input::Gesture g = pending_;
  pending_ = input::Gesture::None;
  return g;
}

bool BereanInput::homeTapped() const { return gpio_.hasHomeKey() && gpio_.wasHomeKeyTapped(); }

bool BereanInput::homeHeld() const { return gpio_.hasHomeKey() && gpio_.wasHomeKeyLongPressed(); }
```

- [ ] **Step 3: Point `MappedInputManager` at it.** Add a `BereanInput*` member,
      set from `main.cpp`, and rewrite exactly four methods. Leave every other
      method, and all 418 call sites, untouched.

```cpp
// In MappedInputManager::wasReleased, ahead of the existing mapping:
//
// Back and Confirm no longer come from a GPIO — BoardConfig leaves both
// PIN_UNASSIGNED on this board, so the old mapping through
// SETTINGS.frontButtonBack could only ever return false. They come from the
// chord and the right key's hold instead.
case Button::Back:
  return lastGesture_ == input::Gesture::Back;
case Button::Confirm:
  return lastGesture_ == input::Gesture::Confirm;
case Button::PageBack:
case Button::Up:
  return lastGesture_ == input::Gesture::Previous;
case Button::PageForward:
case Button::Down:
  return lastGesture_ == input::Gesture::Next;
```

  `lastGesture_` is filled once per tick in `update()` by calling
  `berean_->takeGesture()`, so every `wasReleased` call within one tick sees the
  same value — matching the edge-event semantics the call sites already assume.

- [ ] **Step 4: Keep the left-edge swipe as a second Back.** Do not remove it.
      It is the touch-side Back that already works, and having two independent
      routes is the entire point: the chord survives a dead GT911, the swipe
      survives a stuck key.

- [ ] **Step 5: Construct and feed it in `src/main.cpp`**, beside the existing
      `mappedInputManager`, and call `bereanInput.update(millis())` in the same
      place `mappedInputManager.update()` is already called.

- [ ] **Step 6: Build, analyse, and commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
/Volumes/stein/.platformio/penv/bin/pio check -e x4pro
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
git add src/input src/MappedInputManager.h src/MappedInputManager.cpp src/main.cpp
git commit -m "feat: give the device a Back and a Confirm that exist in hardware"
```

  `pio check` fails CI on `low` severity, not just errors — a raw loop it wants
  as `std::find_if` is enough to turn the build red.

---

## Task 4: The `ReturnStack` capacity decision

The spec defers this to Phase 2 and it comes due now, because Back is about to
become the device's primary navigation rather than a reader convenience.

`ReturnStack.h:16` sets `CAPACITY = 3` and silently evicts the oldest on push.
The existing comment states the trade deliberately: *"trading the article origin
for every individual Back being one correct step back."*

**Decision: keep the eviction, raise the capacity to 8, and expose the depth.**

- Keeping eviction is right, and the existing comment argues it correctly. The
  alternative — refusing the push when full — means the *newest* citation is the
  one you cannot return from, which is worse than losing the oldest.
- Raising to 8 costs 64 bytes (`8 × sizeof(SavedPosition)`) and moves "Back walks
  a path I did not take" from a four-citation chain to a nine-citation one. Four
  is reachable in normal study; nine is not.
- Exposing `depth()` is what Task 2's ladder needs — `ScreenContext::returnStackDepth`
  is already in its interface — and it is what lets 2b's chrome distinguish
  *Return* from *Back*, which was the spec's other suggested remedy.

**Files:**
- Modify: `src/activities/reader/ReturnStack.h`
- Modify: `test/return_stack/ReturnStackTest.cpp`

- [ ] **Step 1: Add the failing tests** to `test/return_stack/ReturnStackTest.cpp`

```cpp
TEST(ReturnStack, ReportsItsDepth) {
  ReturnStack s;
  EXPECT_EQ(s.depth(), 0);
  s.push({1, 1});
  s.push({2, 2});
  EXPECT_EQ(s.depth(), 2);
  SavedPosition out;
  s.pop(out);
  EXPECT_EQ(s.depth(), 1);
}

TEST(ReturnStack, DepthSaturatesAtCapacityRatherThanCounting) {
  ReturnStack s;
  for (int i = 0; i < ReturnStack::CAPACITY + 4; ++i) s.push({i, i});
  EXPECT_EQ(s.depth(), ReturnStack::CAPACITY);
}

TEST(ReturnStack, HoldsAWholeStudySessionOfCitations) {
  // Four citations in a chain is reachable in normal study; the old capacity of
  // three meant the fourth Back walked a path the user never took.
  ReturnStack s;
  for (int i = 1; i <= 8; ++i) s.push({i, i});
  SavedPosition out;
  for (int i = 8; i >= 1; --i) {
    ASSERT_TRUE(s.pop(out)) << "at depth " << i;
    EXPECT_EQ(out.spineIndex, i);
  }
  EXPECT_FALSE(s.pop(out));
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
cd test && cmake --build build --target ReturnStackTest && ./build/return_stack/ReturnStackTest
```

Expected: FAIL — no member `depth`, and the eight-citation walk stops after three.

- [ ] **Step 3: Change `src/activities/reader/ReturnStack.h`**

```cpp
  // Eight, not three. Back is this device's primary navigation now, and at
  // three a fourth citation in a chain silently evicted the origin -- so the
  // fourth Back landed somewhere the user had never been. Eight costs 64 bytes
  // and puts that past any realistic chain.
  static constexpr int CAPACITY = 8;

  // Entries currently held, for the Home ladder and for chrome that needs to
  // tell "Return" apart from "Back".
  int depth() const { return count_; }
```

- [ ] **Step 4: Run and watch them pass.** Expected: `[  PASSED  ]` with the
      existing wrap tests still green — they are parameterised on `CAPACITY` and
      must not need editing. If one hardcodes 3, that is a test bug this change
      correctly exposes; fix it to use `ReturnStack::CAPACITY`.

- [ ] **Step 5: Commit**

```bash
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix
git add src/activities/reader/ReturnStack.h test/return_stack/ReturnStackTest.cpp
git commit -m "feat: deepen the return stack now that Back is primary navigation"
```

---

## Task 5: Device verification

Human-tester scope, and this phase has a failure mode worth naming: **the way
this breaks is that you cannot leave a screen.** Test the escape routes before
anything else.

- [ ] **Step 1: Side-load the dev build** (OTA installs the release build, which
      has `LOG_LEVEL=0` and no serial output):

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
curl -H "Expect:" -F "file=@.pio/build/x4pro/firmware.bin" "http://<device-ip>/upload?path=/"
```

  Then *Settings → SD firmware update*. The `Expect:` suppression is required —
  the ESP32 web server never answers `100-continue` and the transfer hangs.

- [ ] **Step 2: The escape routes, from a settings sub-screen**

  | Gesture | Expected |
  |---|---|
  | Both keys, short | back one level |
  | Both keys, held ~1 s | launcher / home |
  | Home, short | back one level |
  | Home, long | launcher / home |
  | Left-edge swipe | back one level |

  **Four independent routes must work.** If only the touch ones do, the chord is
  not reaching `wasReleased(Button::Back)` and the phase has not achieved its
  purpose — a device that strands you when the GT911 stops answering.

- [ ] **Step 3: The gestures that must NOT fire**

  | Action | Must not |
  |---|---|
  | Left key, short, in the reader | do anything but turn one page back |
  | Right key, short, in the reader | do anything but turn one page forward |
  | Right key, long | also turn a page when released |
  | Both keys, release one then the other | turn a page |
  | Both keys, held to the launcher | also fire Back on release |

  A stray page turn is the most visible possible wrong answer on e-ink, and
  every one of these is covered by a host test — so a failure here means the
  adapter, not the machine.

- [ ] **Step 4: Confirm still works where it is the only way through** — enter a
      settings row with a long right-key press and change a value.

- [ ] **Step 5: Report the table above**, filled in, rather than a summary claim.

---

## Self-review

**Spec coverage for 2a.** Input model table → Tasks 1 and 3. Home precedence
ladder → Task 2. "Back must not depend on the touch controller" → Task 3 Step 4,
verified in Task 5 Step 2. "There must be a Confirm" → Task 1. `ReturnStack`
capacity, the spec's named open item → Task 4.

**Deliberately not in 2a**, and each is a plan of its own:

- The launcher shell and the four sections → **2b**, which also deletes the
  `MappedInputManager` shim once the remaining call sites can be swept with a
  working device at every commit.
- Long-press-to-anchor, tap-to-finish selection → **2c**.
- Horizontal and vertical swipe in the reader → **2c**, with the selection
  gesture, since they share the touch classifier.

**Corrections to the spec this plan makes:**

1. The spec calls the chord "Left + Right". On this board those are `BTN_UP` and
   `BTN_DOWN`; `BTN_LEFT` and `BTN_RIGHT` are `PIN_UNASSIGNED` and never read.
2. The spec treats Back-from-a-button as something this phase removes. It is
   already dead — `SETTINGS.frontButtonBack` resolves to an unassigned pin — so
   this phase *adds* a physical Back rather than replacing one.
3. The spec says `MappedInputManager` is deleted in Phase 2. It is shimmed here
   and deleted in 2b, because a 121-file rename and an input-semantics change in
   one commit is not reviewable.

**Open item still open:** whether the launcher needs a visible "Return" affordance
distinct from "Back" when the return stack is non-empty. `depth()` now makes it
possible; 2b decides whether it is worth the chrome.
