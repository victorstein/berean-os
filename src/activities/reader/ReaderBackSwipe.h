#pragma once

#include <cstdint>

// What a short Back -- the left-edge swipe or a held Left key -- does on the reading surface.
// Free of firmware includes so the host suite can exercise it (test/ui_layout).
namespace ReaderBackSwipe {

struct Swipe {
  bool shortBackRelease = false;  // Back released under ReaderUtils::GO_BACK_OR_HOME_MS
  int returnDepth = 0;            // returnStack.count()
  bool hasOrigin = false;         // the reader was opened from Predicación
};

// PassOn leaves the release to ReaderUtils::handleBackNavigation, which ignores the swipe and sends a
// button Back Home or to the file browser, as its settings say.
enum class Action : uint8_t { PassOn, PopReturn, ToPreaching };

constexpr Action decide(const Swipe& swipe) {
  if (!swipe.shortBackRelease) return Action::PassOn;
  if (swipe.returnDepth > 0) return Action::PopReturn;
  if (swipe.hasOrigin) return Action::ToPreaching;
  return Action::PassOn;
}

}  // namespace ReaderBackSwipe
