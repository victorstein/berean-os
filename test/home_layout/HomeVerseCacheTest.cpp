// The Home verse cache's rules (issue #203, spec A8/A9/A11): when a held pick
// still answers, why an empty scan was empty, and how fitted lines are packed.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "activities/launcher/HomeVerseCache.h"

using home_verse::Key;

namespace {

Key datedKey(const int day, const uint32_t bytes) {
  Key key;
  key.dated = true;
  key.day.year = 2026;
  key.day.month = 9;
  key.day.day = static_cast<uint8_t>(day);
  key.fileExists = true;
  key.fileBytes = bytes;
  return key;
}

home_verse::Entry heldFor(const Key& key) {
  home_verse::Entry entry;
  entry.valid = true;
  entry.key = key;
  return entry;
}

}  // namespace

TEST(HomeVerseCache, AnswersForTheSameDayAndFile) {
  EXPECT_TRUE(home_verse::validFor(heldFor(datedKey(30, 1000)), datedKey(30, 1000)));
}

TEST(HomeVerseCache, ANewDayRescans) {
  EXPECT_FALSE(home_verse::validFor(heldFor(datedKey(30, 1000)), datedKey(31, 1000)));
}

TEST(HomeVerseCache, ATagEditThatChangesTheFileRescans) {
  EXPECT_FALSE(home_verse::validFor(heldFor(datedKey(30, 1000)), datedKey(30, 1040)));
}

TEST(HomeVerseCache, TheFileAppearingRescans) {
  Key missing = datedKey(30, 0);
  missing.fileExists = false;
  EXPECT_FALSE(home_verse::validFor(heldFor(missing), datedKey(30, 0)));
}

TEST(HomeVerseCache, UndatedKeysMatchWhateverDayTheyCarry) {
  Key a;
  a.fileExists = true;
  a.fileBytes = 10;
  Key b = a;
  b.day.day = 4;
  EXPECT_TRUE(home_verse::validFor(heldFor(a), b));
  EXPECT_FALSE(home_verse::validFor(heldFor(a), datedKey(30, 10)));
}

TEST(HomeVerseCache, AnInvalidEntryNeverAnswers) {
  home_verse::Entry entry = heldFor(datedKey(30, 1000));
  entry.valid = false;
  EXPECT_FALSE(home_verse::validFor(entry, datedKey(30, 1000)));
}

TEST(HomeVerseCache, TheFallbackSeedIsFixed) { EXPECT_EQ(home_verse::FALLBACK_SEED, 0x48564653u); }

TEST(HomeVerseCache, ThePrefilterIsCardSized) { EXPECT_EQ(home_verse::PREFILTER_BYTES, 512u); }

TEST(HomeVerseCache, RowsTooLongForTheCardSaySo) {
  EXPECT_EQ(home_verse::emptyReason(2, 0), home_verse::Empty::TooLong);
  EXPECT_EQ(home_verse::emptyReason(0, 1), home_verse::Empty::TooLong);
  EXPECT_EQ(home_verse::emptyReason(0, 0), home_verse::Empty::NoPassages);
}

TEST(HomeVerseCache, PacksFittedLinesEachTerminated) {
  home_verse::Pick pick;
  ASSERT_TRUE(home_verse::packLines({"For God loved", "the world so much"}, pick));
  EXPECT_EQ(pick.lineCount, 2);
  EXPECT_STREQ(pick.line(0), "For God loved");
  EXPECT_STREQ(pick.line(1), "the world so much");
}

TEST(HomeVerseCache, RefusesMoreLinesThanTheCardHolds) {
  home_verse::Pick pick;
  EXPECT_FALSE(home_verse::packLines({"a", "b", "c", "d", "e"}, pick));
  EXPECT_FALSE(home_verse::packLines({}, pick));
  EXPECT_EQ(pick.lineCount, 0);
}

TEST(HomeVerseCache, RefusesMoreTextThanTheBufferHolds) {
  home_verse::Pick pick;
  const std::string half(home_verse::LINE_BUFFER_BYTES / 2, 'x');
  EXPECT_FALSE(home_verse::packLines({half, half}, pick)) << "two terminators push it over";
  EXPECT_TRUE(home_verse::packLines({std::string(home_verse::PREFILTER_BYTES, 'x')}, pick));
}
