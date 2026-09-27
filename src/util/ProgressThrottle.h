#pragma once

#include <cstddef>
#include <cstdint>

// When a download's progress hook should repaint: a full e-ink refresh per chunk
// would cost more than the transfer, so a repaint waits for another STEP_PERCENT
// or MIN_UPDATE_MS, whichever comes first, and the last chunk always paints.
class ProgressThrottle {
 public:
  static constexpr int STEP_PERCENT = 5;
  static constexpr uint32_t MIN_UPDATE_MS = 5000;

  static int percentOf(const size_t downloaded, const size_t total) {
    return total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
  }

  void reset() {
    lastPercent_ = -1;
    lastMs_ = 0;
  }

  // Times are uint32_t, not unsigned long: millis() is 32 bits on the device, and
  // the host suite has to wrap the subtraction where the device does.
  bool shouldRepaint(const size_t downloaded, const size_t total, const uint32_t nowMs) {
    const int percent = percentOf(downloaded, total);
    if (percent >= 100 || lastPercent_ < 0 || percent >= lastPercent_ + STEP_PERCENT ||
        nowMs - lastMs_ >= MIN_UPDATE_MS) {
      lastPercent_ = percent;
      lastMs_ = nowMs;
      return true;
    }
    return false;
  }

 private:
  int lastPercent_ = -1;
  uint32_t lastMs_ = 0;
};
