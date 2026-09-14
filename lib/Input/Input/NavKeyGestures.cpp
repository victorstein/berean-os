#include "Input/NavKeyGestures.h"

namespace input {
namespace {

// Unsigned subtraction, so a millis() wrap yields the true short interval
// rather than ~49 days.
uint32_t elapsed(const uint32_t from, const uint32_t now) { return now - from; }

}  // namespace

void NavKeyGestures::updateKey(Key& key, const bool pressed, const uint32_t nowMs) {
  key.event = NavEvent::None;
  key.lastSeenMs = nowMs;

  if (pressed && !key.down) {
    key.down = true;
    key.downAtMs = nowMs;
    return;
  }

  if (!pressed && key.down) {
    key.down = false;
    const bool wasStale = key.stale;
    key.stale = false;
    // A key that was already down when we started looking resolves to nothing:
    // it was held through boot or a wake, and its press was never ours to read.
    if (!wasStale) {
      key.event = elapsed(key.downAtMs, nowMs) >= HOLD_MS ? NavEvent::Synth : NavEvent::Page;
    }
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
  if (leftPressed) left_.downAtMs = nowMs;
  right_.down = rightPressed;
  right_.stale = rightPressed;
  if (rightPressed) right_.downAtMs = nowMs;
}

NavEvent NavKeyGestures::eventFor(const NavKey key) const { return keyRef(key).event; }

bool NavKeyGestures::suppressState(const NavKey key) const {
  const Key& k = keyRef(key);
  if (!k.down || k.stale) return false;
  // Not from the first tick: the screenshot combo latches the instant POWER and
  // the right key are both down (main.cpp:653), and that must keep working.
  return elapsed(k.downAtMs, k.lastSeenMs) >= HOLD_STATE_SUPPRESS_MS;
}

bool NavKeyGestures::reportingSyntheticHeldTime() const {
  return left_.event != NavEvent::None || right_.event != NavEvent::None;
}

}  // namespace input
