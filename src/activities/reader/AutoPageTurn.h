#pragma once

#include <cstdint>
#include <iterator>

// Auto page turn's rate and timing, free of firmware includes so the arithmetic
// can be tested on the host. The last-turn timestamp is not held here: the
// reader also uses it to debounce manual turns.
class AutoPageTurn {
 public:
  // Pages per minute for each reader-menu option; option 0 is off.
  static constexpr int RATES[] = {1, 1, 3, 6, 12};

  bool start(const uint8_t option) {
    if (option == 0 || option >= std::size(RATES)) {
      active_ = false;
      return false;
    }
    durationMs_ = (1UL * 60 * 1000) / RATES[option];
    active_ = true;
    return true;
  }

  void stop() { active_ = false; }
  bool active() const { return active_; }

  // Unsigned subtraction, so a millis() wrap between the two stamps still yields the elapsed time.
  bool due(const unsigned long nowMs, const unsigned long lastTurnMs) const {
    return (nowMs - lastTurnMs) >= durationMs_;
  }

  unsigned long pagesPerMinute() const { return 60 * 1000 / durationMs_; }

 private:
  bool active_ = false;
  unsigned long durationMs_ = 0;
};
