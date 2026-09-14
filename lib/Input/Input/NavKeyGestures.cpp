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
