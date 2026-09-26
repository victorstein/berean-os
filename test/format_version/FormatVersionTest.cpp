#include <gtest/gtest.h>

#include "Serialization/FormatVersion.h"

static_assert(persist::isKnownFormatVersion(1, 1), "must stay constexpr: stores may use it in static_asserts");
static_assert(!persist::loadRefusedAfter(DocReadStatus::Missing, false, true), "must stay constexpr");

TEST(FormatVersion, TheCurrentVersionIsKnown) { EXPECT_TRUE(persist::isKnownFormatVersion(1, 1)); }

TEST(FormatVersion, AnOlderVersionIsKnown) {
  EXPECT_TRUE(persist::isKnownFormatVersion(1, 2));
  EXPECT_TRUE(persist::isKnownFormatVersion(2, 2));
}

TEST(FormatVersion, ZeroIsRefused) {
  EXPECT_FALSE(persist::isKnownFormatVersion(0, 1)) << "an absent \"v\" reads as 1; a written 0 no build wrote";
}

TEST(FormatVersion, ANegativeVersionIsRefused) { EXPECT_FALSE(persist::isKnownFormatVersion(-1, 1)); }

TEST(FormatVersion, ANewerVersionIsRefused) {
  EXPECT_FALSE(persist::isKnownFormatVersion(2, 1)) << "a rolled-back build must refuse, not reinterpret";
}

TEST(LoadRefusedAfter, AnAcceptedLoadClearsIt) {
  EXPECT_FALSE(persist::loadRefusedAfter(DocReadStatus::Ok, true, true));
  EXPECT_FALSE(persist::loadRefusedAfter(DocReadStatus::Ok, true, false));
}

TEST(LoadRefusedAfter, ARejectedLoadSetsIt) {
  EXPECT_TRUE(persist::loadRefusedAfter(DocReadStatus::Ok, false, false));
  EXPECT_TRUE(persist::loadRefusedAfter(DocReadStatus::Ok, false, true));
}

TEST(LoadRefusedAfter, AMissingFileClearsIt) {
  EXPECT_FALSE(persist::loadRefusedAfter(DocReadStatus::Missing, false, true)) << "nothing left to protect";
  EXPECT_FALSE(persist::loadRefusedAfter(DocReadStatus::Missing, false, false));
}

TEST(LoadRefusedAfter, AnUnreadableOrUnparseableFileLeavesItAlone) {
  for (const DocReadStatus status : {DocReadStatus::Unreadable, DocReadStatus::ParseError}) {
    EXPECT_TRUE(persist::loadRefusedAfter(status, false, true));
    EXPECT_FALSE(persist::loadRefusedAfter(status, false, false))
        << "corrupt-file handling is unchanged by #101: such a file stays overwritable";
  }
}
