#pragma once

namespace input {

// A digital level that changes only once two consecutive samples agree, so a
// single glitch sample never reaches the caller.
class StableLevel {
 public:
  void begin(const bool level) {
    stableLevel = level;
    lastSample = level;
  }

  bool update(const bool level) {
    if (level == lastSample) stableLevel = level;
    lastSample = level;
    return stableLevel;
  }

  bool stable() const { return stableLevel; }

 private:
  bool stableLevel = false;
  bool lastSample = false;
};

}  // namespace input
