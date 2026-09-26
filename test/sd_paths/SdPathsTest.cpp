#include <gtest/gtest.h>

#include "SdPaths.h"

// The literals here are the on-card contract, not the header. Every path is one
// a shipped build already reads or writes; a changed value strands the user's
// data at the old location, so these are pinned by hand rather than derived.

TEST(SdPaths, RootsMatchTheCard) {
  EXPECT_STREQ(sdpaths::CROSSPOINT_DIR, "/.crosspoint");
  EXPECT_STREQ(sdpaths::BEREAN_DIR, "/.berean");
}

TEST(SdPaths, CrossPointPathsMatchTheCard) {
  EXPECT_STREQ(sdpaths::SETTINGS_FILE, "/.crosspoint/settings.json");
  EXPECT_STREQ(sdpaths::STATE_FILE, "/.crosspoint/state.json");
  EXPECT_STREQ(sdpaths::RECENT_BOOKS_FILE, "/.crosspoint/recent.json");
  EXPECT_STREQ(sdpaths::WIFI_FILE, "/.crosspoint/wifi.json");
  EXPECT_STREQ(sdpaths::SLEEP_FRAME_FILE, "/.crosspoint/sleep_frame.bin");
  EXPECT_STREQ(sdpaths::HIGHLIGHTS_DIR, "/.crosspoint/highlights");
  EXPECT_STREQ(sdpaths::BOOKMARKS_DIR, "/.crosspoint/bookmarks");
}

TEST(SdPaths, BereanPathsMatchTheCard) {
  EXPECT_STREQ(sdpaths::PUBKEYS_FILE, "/.berean/pubkeys.json");
  EXPECT_STREQ(sdpaths::TAGS_FILE, "/.berean/tags.json");
  EXPECT_STREQ(sdpaths::MEETING_WEEKS_FILE, "/.berean/meeting-weeks.json");
  EXPECT_STREQ(sdpaths::MIGRATION_REPORT_FILE, "/.berean/migration-report.json");
  EXPECT_STREQ(sdpaths::MIGRATION_LEDGER_FILE, "/.berean/migration-ledger.json");
  EXPECT_STREQ(sdpaths::SEARCH_DIR, "/.berean/search");
  EXPECT_STREQ(sdpaths::SEARCH_INDEX_FILE, "/.berean/search/bible.idx");
  EXPECT_STREQ(sdpaths::SEARCH_CHECKPOINT_FILE, "/.berean/search/bible.partial");
  EXPECT_STREQ(sdpaths::PASSAGES_DIR, "/.berean/passages");
  EXPECT_STREQ(sdpaths::UNITS_DIR, "/.berean/units");
  EXPECT_STREQ(sdpaths::COMPLETION_DIR, "/.berean/completion");
}

TEST(SdPaths, IsUnderAcceptsAChild) { EXPECT_TRUE(sdpaths::isUnder("/.berean/tags.json", "/.berean")); }

TEST(SdPaths, IsUnderRejectsTheRootItself) { EXPECT_FALSE(sdpaths::isUnder("/.berean", "/.berean")); }

TEST(SdPaths, IsUnderRejectsAnEmptyTail) { EXPECT_FALSE(sdpaths::isUnder("/.berean/", "/.berean")); }

// A plain prefix test would accept this and file the data under the wrong root.
TEST(SdPaths, IsUnderRejectsASiblingSharingAPrefix) {
  EXPECT_FALSE(sdpaths::isUnder("/.bereanx/tags.json", "/.berean"));
}

TEST(SdPaths, IsUnderRejectsAnotherRoot) { EXPECT_FALSE(sdpaths::isUnder("/.crosspoint/settings.json", "/.berean")); }
