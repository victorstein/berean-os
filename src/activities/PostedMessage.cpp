#include "PostedMessage.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <mutex>

#include "components/UITheme.h"

namespace {

constexpr size_t CAPACITY = 2;

std::mutex queueMutex;
const char* queued[CAPACITY] = {};
size_t queuedCount = 0;

const char* takeOldest() {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (queuedCount == 0) return nullptr;
  const char* oldest = queued[0];
  for (size_t i = 1; i < queuedCount; ++i) queued[i - 1] = queued[i];
  queued[--queuedCount] = nullptr;
  return oldest;
}

}  // namespace

namespace PostedMessage {

void post(const char* message) {
  if (message == nullptr || message[0] == '\0') return;
  std::lock_guard<std::mutex> lock(queueMutex);
  if (queuedCount == CAPACITY) {
    LOG_ERR("MSG", "Message queue full; dropping: %s", message);
    return;
  }
  queued[queuedCount++] = message;
}

void drawNext(const GfxRenderer& renderer) {
  const char* message = takeOldest();
  if (message != nullptr) GUI.drawPopup(renderer, message);
}

}  // namespace PostedMessage
