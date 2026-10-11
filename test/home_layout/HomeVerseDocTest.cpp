// The Home verse file's format (spec berean-o-7, A3-A15): what is written, what is refused, and
// the budget. The storage half is HomeVerseFileTest.

#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "activities/launcher/HomeVerseDoc.h"

namespace {

constexpr const char* BUILD = "1a2b3c4d";

}  // namespace

TEST(HomeVerseDoc, AUsableBuildIdIsShortAndNotAllZero) {
  EXPECT_TRUE(HomeVerseDoc::isUsableBuildId(BUILD));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId(nullptr));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId(""));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId("00000000")) << "an image whose SHA was never filled in";
  EXPECT_TRUE(HomeVerseDoc::isUsableBuildId(std::string(HomeVerseDoc::MAX_BUILD_ID_BYTES, 'a').c_str()));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId(std::string(HomeVerseDoc::MAX_BUILD_ID_BYTES + 1, 'a').c_str()));
}
