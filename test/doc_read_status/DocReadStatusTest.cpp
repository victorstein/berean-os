#include <gtest/gtest.h>

#include "Serialization/DocReadStatus.h"

TEST(DocReadStatus, AbsentFileIsMissing) {
  EXPECT_EQ(classifyDocRead(false, false, false), DocReadStatus::Missing);
  EXPECT_EQ(classifyDocRead(false, true, true), DocReadStatus::Missing)
      << "absence dominates: nothing else was observed";
}

TEST(DocReadStatus, EmptyContentIsUnreadable) {
  EXPECT_EQ(classifyDocRead(true, true, false), DocReadStatus::Unreadable);
}

TEST(DocReadStatus, ParseFailureIsReported) {
  EXPECT_EQ(classifyDocRead(true, false, true), DocReadStatus::ParseError);
}

TEST(DocReadStatus, GoodReadIsOk) { EXPECT_EQ(classifyDocRead(true, false, false), DocReadStatus::Ok); }

TEST(DocReadStatus, OnlyMissingIsSafeToOverwrite) {
  // The rule the caller depends on: exactly one failure status means "no data
  // was ever there". The other two mean data may exist and must be preserved.
  EXPECT_EQ(classifyDocRead(false, false, false), DocReadStatus::Missing);
  EXPECT_NE(classifyDocRead(true, true, false), DocReadStatus::Missing);
  EXPECT_NE(classifyDocRead(true, false, true), DocReadStatus::Missing);
}

// The caller-side half of the DocReadStatus.h:5-7 contract: given the status a
// read returned, may a read-modify-write caller go on to write?

TEST(MayOverwriteAfterRead, OkMayOverwrite) { EXPECT_TRUE(mayOverwriteAfterRead(DocReadStatus::Ok)); }

TEST(MayOverwriteAfterRead, MissingMayOverwrite) {
  EXPECT_TRUE(mayOverwriteAfterRead(DocReadStatus::Missing))
      << "Missing means no data was ever there -- start a new document";
}

TEST(MayOverwriteAfterRead, UnreadableMayNotOverwrite) {
  EXPECT_FALSE(mayOverwriteAfterRead(DocReadStatus::Unreadable)) << "the bytes are still on the card";
}

TEST(MayOverwriteAfterRead, ParseErrorMayNotOverwrite) {
  EXPECT_FALSE(mayOverwriteAfterRead(DocReadStatus::ParseError)) << "the bytes are still on the card";
}

TEST(MayOverwriteAfterRead, AgreesWithClassifyDocRead) {
  // Ties the predicate to the classifier that feeds it: of the eight observable
  // read outcomes, only an absent file and a clean read permit a write.
  for (const bool exists : {false, true}) {
    for (const bool contentEmpty : {false, true}) {
      for (const bool parseFailed : {false, true}) {
        const bool permitted = mayOverwriteAfterRead(classifyDocRead(exists, contentEmpty, parseFailed));
        const bool expected = !exists || (!contentEmpty && !parseFailed);
        EXPECT_EQ(permitted, expected) << "exists=" << exists << " empty=" << contentEmpty
                                       << " parseFailed=" << parseFailed;
      }
    }
  }
}
