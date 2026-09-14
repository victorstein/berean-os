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
