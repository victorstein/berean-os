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

home_verse::Key datedKey(const uint8_t day, const uint32_t bytes) {
  home_verse::Key key;
  key.dated = true;
  key.day.year = 2026;
  key.day.month = 10;
  key.day.day = day;
  key.fileExists = true;
  key.fileBytes = bytes;
  return key;
}

// A pick as HomeVerse::scan leaves one.
home_verse::Entry pickEntry() {
  home_verse::Entry entry;
  entry.valid = true;
  entry.key = datedKey(11, 43864);
  entry.empty = home_verse::Empty::None;
  entry.hasPick = true;
  entry.pick.start = study::Unit{study::UnitKind::Verse, 43, 3, 16, 0};
  entry.pick.spine = 1201;
  snprintf(entry.pick.reference, sizeof(entry.pick.reference), "%s", "John 3:16");
  entry.pick.rung = 1;
  home_verse::packLines({"For God loved the world", "so much that he gave"}, entry.pick);
  return entry;
}

home_verse::Entry emptyEntry(const home_verse::Empty why, const home_verse::Key& key) {
  home_verse::Entry entry;
  entry.valid = true;
  entry.key = key;
  entry.empty = why;
  entry.hasPick = false;
  return entry;
}

std::string serialised(const home_verse::Entry& entry) {
  JsonDocument doc;
  HomeVerseDoc::toJson(entry, BUILD, doc);
  std::string json;
  serializeJson(doc, json);
  return json;
}

}  // namespace

TEST(HomeVerseDoc, AUsableBuildIdIsShortAndNotAllZero) {
  EXPECT_TRUE(HomeVerseDoc::isUsableBuildId(BUILD));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId(nullptr));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId(""));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId("00000000")) << "an image whose SHA was never filled in";
  EXPECT_TRUE(HomeVerseDoc::isUsableBuildId(std::string(HomeVerseDoc::MAX_BUILD_ID_BYTES, 'a').c_str()));
  EXPECT_FALSE(HomeVerseDoc::isUsableBuildId(std::string(HomeVerseDoc::MAX_BUILD_ID_BYTES + 1, 'a').c_str()));
}

TEST(HomeVerseDoc, APickIsWrittenInTheCardFormat) {
  EXPECT_EQ(serialised(pickEntry()),
            R"({"v":1,"b":"1a2b3c4d","k":{"dt":true,"y":2026,"m":10,"d":11,"f":true,"n":43864},"e":0,)"
            R"("p":{"u":"v:43:3:16:0","s":1201,"r":"John 3:16","rung":1,)"
            R"("l":["For God loved the world","so much that he gave"]}})");
}

TEST(HomeVerseDoc, AnEmptyResultHasItsCodeAndNoPick) {
  EXPECT_EQ(serialised(emptyEntry(home_verse::Empty::NoPassages, datedKey(11, 43864))),
            R"({"v":1,"b":"1a2b3c4d","k":{"dt":true,"y":2026,"m":10,"d":11,"f":true,"n":43864},"e":1})");
  EXPECT_EQ(serialised(emptyEntry(home_verse::Empty::TooLong, datedKey(11, 43864))),
            R"({"v":1,"b":"1a2b3c4d","k":{"dt":true,"y":2026,"m":10,"d":11,"f":true,"n":43864},"e":2})");
}

TEST(HomeVerseDoc, AnUndatedKeyWritesNoDate) {
  home_verse::Key undated;
  undated.day.year = 2026;
  undated.fileExists = false;
  EXPECT_EQ(serialised(emptyEntry(home_verse::Empty::NoPassages, undated)),
            R"({"v":1,"b":"1a2b3c4d","k":{"dt":false,"y":0,"m":0,"d":0,"f":false,"n":0},"e":1})");
}
