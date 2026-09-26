#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>
#include <SdPaths.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  // Cover thumbnail the sleep screen paints, cached by the launcher when it
  // resolves the Bible tile. Kept here so the sleep path never has to repeat
  // the "which book is the Bible" search, let alone open an EPUB: it runs while
  // the device is shutting down.
  std::string bibleCoverPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentSleepPos = 0;
  uint8_t recentSleepFill = 0;
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;

  // Fixed key set: 12 keys, two 16-element uint16_t arrays, eight scalars, and
  // two SD path strings assumed <= 255 B each. ~1,190 B worst case.
  static constexpr size_t SAVE_BUDGET = 2048;
  static constexpr int FORMAT_VERSION = 1;

  static const char* getFilePath() { return sdpaths::STATE_FILE; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);
};

#define APP_STATE CrossPointState::getInstance()
