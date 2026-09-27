#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include "CrossPointState.h"

namespace {

void resetState() {
  JsonDocument empty;
  empty["v"] = 1;
  ASSERT_TRUE(APP_STATE.fromJson(empty.as<JsonVariantConst>()));
}

}  // namespace

TEST(CrossPointStateStudyRing, AnOlderStateFileLoadsWithAnEmptyRing) {
  JsonDocument older;
  older["v"] = 1;
  older["recentSleepPos"] = 3;
  ASSERT_TRUE(APP_STATE.fromJson(older.as<JsonVariantConst>()));
  EXPECT_EQ(APP_STATE.recentStudySleepFill, 0);
  EXPECT_EQ(APP_STATE.recentStudySleepPos, 0);
}

TEST(CrossPointStateStudyRing, PushWrapsAtCapacity) {
  resetState();
  for (uint32_t key = 1; key <= CrossPointState::SLEEP_RECENT_COUNT + 1u; ++key) APP_STATE.pushRecentStudySleep(key);
  EXPECT_EQ(APP_STATE.recentStudySleepFill, CrossPointState::SLEEP_RECENT_COUNT);
  EXPECT_EQ(APP_STATE.recentStudySleepPos, 1);
  EXPECT_EQ(APP_STATE.recentStudySleep[0], CrossPointState::SLEEP_RECENT_COUNT + 1u);
  EXPECT_EQ(APP_STATE.recentStudySleep[1], 2u);
}

TEST(CrossPointStateStudyRing, RoundTripsFullWidthKeys) {
  resetState();
  APP_STATE.pushRecentStudySleep(0xDEADBEEFu);
  APP_STATE.pushRecentStudySleep(7u);
  JsonDocument saved;
  APP_STATE.toJson(saved);
  resetState();
  ASSERT_TRUE(APP_STATE.fromJson(saved.as<JsonVariantConst>()));
  EXPECT_EQ(APP_STATE.recentStudySleep[0], 0xDEADBEEFu);
  EXPECT_EQ(APP_STATE.recentStudySleep[1], 7u);
  EXPECT_EQ(APP_STATE.recentStudySleepPos, 2);
  EXPECT_EQ(APP_STATE.recentStudySleepFill, 2);
}

TEST(CrossPointStateStudyRing, ClampsACorruptCursor) {
  JsonDocument corrupt;
  corrupt["v"] = 1;
  const JsonArray keys = corrupt["recentStudySleep"].to<JsonArray>();
  keys.add(5);
  keys.add(6);
  corrupt["recentStudySleepPos"] = 99;
  corrupt["recentStudySleepFill"] = 9;
  ASSERT_TRUE(APP_STATE.fromJson(corrupt.as<JsonVariantConst>()));
  EXPECT_EQ(APP_STATE.recentStudySleepPos, 99 % CrossPointState::SLEEP_RECENT_COUNT);
  EXPECT_EQ(APP_STATE.recentStudySleepFill, 2);
}
