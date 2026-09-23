#pragma once

#include <cstddef>
#include <cstdint>

// The queue and timing rules behind PostedMessage, free of Arduino, the
// renderer and locking so they can be host-tested with an injected clock.
//
// A message stays on screen, redrawn by every render, for MIN_DISPLAY_MS after
// it is first drawn. Screens with a progress bar repaint every second or two,
// and a popup drawn once would be gone before it could be read. The hold
// matches the reader's bookmark toast (ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS).
//
// Messages are compared by pointer: callers post tr() strings or literals, so
// the same notice raised twice is the same pointer.
class PostedMessageQueue {
 public:
  static constexpr size_t CAPACITY = 2;
  static constexpr uint32_t MIN_DISPLAY_MS = 2500;

  enum class PostResult : uint8_t { Queued, Duplicate, Full, Ignored };

  PostResult post(const char* message, const uint32_t nowMs) {
    if (message == nullptr || message[0] == '\0') return PostResult::Ignored;
    if (message == showingNow(nowMs)) return PostResult::Duplicate;
    for (size_t i = 0; i < queuedCount; ++i) {
      if (queued[i] == message) return PostResult::Duplicate;
    }
    if (queuedCount == CAPACITY) return PostResult::Full;
    queued[queuedCount++] = message;
    return PostResult::Queued;
  }

  // The message to draw over the render that is finishing now, or nullptr.
  const char* next(const uint32_t nowMs) {
    if (const char* current = showingNow(nowMs)) return current;
    showing = takeOldest();
    shownAtMs = nowMs;
    return showing;
  }

 private:
  // Unsigned subtraction keeps the hold correct across millis() wrapping.
  const char* showingNow(const uint32_t nowMs) const {
    return (showing != nullptr && nowMs - shownAtMs < MIN_DISPLAY_MS) ? showing : nullptr;
  }

  const char* takeOldest() {
    if (queuedCount == 0) return nullptr;
    const char* oldest = queued[0];
    for (size_t i = 1; i < queuedCount; ++i) queued[i - 1] = queued[i];
    queued[--queuedCount] = nullptr;
    return oldest;
  }

  const char* queued[CAPACITY] = {};
  size_t queuedCount = 0;
  const char* showing = nullptr;
  uint32_t shownAtMs = 0;
};
