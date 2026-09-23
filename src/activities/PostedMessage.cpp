#include "PostedMessage.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <mutex>

#include "PostedMessageQueue.h"
#include "components/UITheme.h"

namespace {

std::mutex queueMutex;
PostedMessageQueue queue;

}  // namespace

namespace PostedMessage {

void post(const char* message) {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (queue.post(message, millis()) == PostedMessageQueue::PostResult::Full) {
    LOG_ERR("MSG", "Message queue full; dropping: %s", message);
  }
}

void drawNext(const GfxRenderer& renderer) {
  const char* message;
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    message = queue.next(millis());
  }
  if (message != nullptr) GUI.drawPopup(renderer, message);
}

}  // namespace PostedMessage
