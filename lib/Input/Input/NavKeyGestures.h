#pragma once

#include <cstdint>

// Turns the two nav keys into a Back and a Confirm this board does not have.
//
// BoardConfig.h:1396 leaves back/confirm/left/right PIN_UNASSIGNED on the X4
// Pro, so Back today exists only as a left-edge touch swipe and a capacitive
// Home key -- both of which die with the GT911. This is the GPIO-only route
// that survives that.
//
// THE DECISION HAPPENS ON RELEASE, not mid-hold. An earlier design asserted
// Back the moment a key passed the threshold, which cannot work here: the
// reader pages on wasPressed by default (ReaderUtils.h:53), so the page turn
// had already fired 850 ms earlier, and getHeldTime() -- which is global and
// reports the whole press (InputManager.cpp:482-489) -- then told every
// downstream consumer the release was a long press. Nine live call sites branch
// on that, from the file browser's delete prompt to the reader's back
// destination.
//
// So: the raw edges are withheld, and on release exactly one synthetic
// press+release pair is emitted -- a page turn if the key was tapped, Back or
// Confirm if it was held. This is the model InputManager's own
// updateDigitalTwoButton uses (InputManager.cpp:400-407), implemented where it
// does not also blank isPressed and take recovery mode with it.
namespace input {

enum class NavKey : uint8_t { Left, Right };

// What a released key turned out to mean.
enum class NavEvent : uint8_t {
  None,
  Page,   // a tap: emit the raw button's own press+release
  Synth,  // a hold: emit BTN_BACK (left) or BTN_CONFIRM (right)
};

class NavKeyGestures {
 public:
  // Above the reader's SKIP_HOLD_MS (700) so a chapter skip and a Back are
  // distinguishable, and below GO_BACK_OR_HOME_MS (1000).
  static constexpr uint32_t HOLD_MS = 850;

  // Continuous list scrolling starts at 500 ms off isPressed
  // (ButtonNavigator.h:20). Holding for Back would scroll the list first and
  // then activate a row, so the raw held state is withheld past this point --
  // after the screenshot combo has already latched on its first tick, and
  // before continuous navigation can begin.
  static constexpr uint32_t HOLD_STATE_SUPPRESS_MS = 250;

  void update(bool leftPressed, bool rightPressed, uint32_t nowMs);

  // Starts with one or both keys already down, marking them stale so they can
  // neither synthesise nor be suppressed until released. Recovery firmware mode
  // holds a key through startup and reads isPressed on it directly.
  void beginWithKeysDown(bool leftPressed, bool rightPressed, uint32_t nowMs);

  // What this key resolved to on THIS tick, if anything. One tick only.
  NavEvent eventFor(NavKey key) const;

  // True while a non-stale key has been down long enough that reporting it as
  // physically pressed would start something that competes with the hold.
  bool suppressState(NavKey key) const;

  // The held time to report for a synthesised event. Deliberately short: every
  // consumer that branches on getHeldTime() -- delete prompts at 700, go-home
  // at 1000, bookmark at 400 -- must see a Back or Confirm as an ordinary
  // press, or one hold means two things.
  static constexpr uint32_t SYNTHETIC_HELD_MS = 40;

  // True on any tick where a synthetic event is being reported, so the caller
  // knows to substitute SYNTHETIC_HELD_MS for the hardware's held time.
  bool reportingSyntheticHeldTime() const;

 private:
  struct Key {
    bool down = false;
    bool stale = false;
    uint32_t downAtMs = 0;
    uint32_t lastSeenMs = 0;
    NavEvent event = NavEvent::None;
  };

  void updateKey(Key& key, bool pressed, uint32_t nowMs);
  const Key& keyRef(NavKey key) const { return key == NavKey::Left ? left_ : right_; }

  Key left_;
  Key right_;
};

}  // namespace input
