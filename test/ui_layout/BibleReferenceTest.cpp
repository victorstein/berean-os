#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "util/BibleReference.h"

using BibleReference::format;
using BibleReference::Verses;

namespace {

std::string formatInto(const size_t bytes, const std::string_view book, const Verses& verses) {
  char out[80];
  EXPECT_LE(bytes, sizeof(out));
  memset(out, 'X', sizeof(out));
  format(out, bytes, book, verses);
  return std::string(out);
}

}  // namespace

TEST(BibleReference, AppendsAKnownChapter) { EXPECT_EQ(format("Isaiah", Verses{40}), "Isaiah 40"); }

TEST(BibleReference, UnknownChapterLeavesTheBookAlone) {
  EXPECT_EQ(format("Isaiah", Verses{}), "Isaiah");
  EXPECT_EQ(formatInto(64, "Isaiah", Verses{}), "Isaiah");
}

TEST(BibleReference, KeepsUtf8BookNames) { EXPECT_EQ(format("\xC3\x89xodo", Verses{5}), "\xC3\x89xodo 5"); }

TEST(BibleReference, FormatsTypedReferenceShapes) {
  EXPECT_EQ(formatInto(64,
                       "Isa\xC3\xAD"
                       "as",
                       Verses{40, 31}),
            "Isa\xC3\xAD"
            "as 40:31");
  EXPECT_EQ(formatInto(64, "Isaiah", Verses{40}), "Isaiah 40");
  EXPECT_EQ(formatInto(64, "Juan", Verses{3, 16, 18}), "Juan 3:16-18");
}

TEST(BibleReference, NumbersOnlyWithoutABook) {
  EXPECT_EQ(formatInto(64, "", Verses{1, 3}), "1:3");
  EXPECT_EQ(format("", Verses{40, 31}), "40:31");
  EXPECT_EQ(format("", Verses{40}), "40");
  EXPECT_EQ(format("", Verses{}), "");
  EXPECT_EQ(formatInto(64, "", Verses{}), "");
}

TEST(BibleReference, NeverOverrunsTheBuffer) {
  EXPECT_EQ(formatInto(6, "El Cantar de los Cantares", Verses{2, 1}), "El Ca");
}

TEST(BibleReference, CutsOnACodepointBoundary) {
  EXPECT_EQ(formatInto(3, "A\xC3\x89x", Verses{5}), "A");
  EXPECT_EQ(formatInto(4, "A\xC3\x89x", Verses{5}), "A\xC3\x89");
  EXPECT_EQ(formatInto(3, "\xC3\x89xodo", Verses{5}), "\xC3\x89");
}

TEST(BibleReference, NeverLeavesALoneLeadByteAtTheStart) {
  EXPECT_EQ(formatInto(2, "\xC3\x89xodo", Verses{5}), "");
  EXPECT_EQ(formatInto(3, "\xE2\x80\x94x", Verses{5}), "");
}

TEST(BibleReference, TinyBuffers) {
  char untouched = 'X';
  format(&untouched, 0, "Isaiah", Verses{1});
  EXPECT_EQ(untouched, 'X');
  EXPECT_EQ(formatInto(1, "Isaiah", Verses{1}), "");
}

TEST(BibleReference, TheLargestNumbersFit) {
  EXPECT_EQ(formatInto(BibleReference::MAX_NUMBERS_BYTES, "", Verses{65535, 65535, 65535}), "65535:65535-65535");
}

TEST(BibleReference, BothOverloadsAgree) {
  const Verses cases[] = {Verses{}, Verses{1}, Verses{40, 31}, Verses{3, 16, 18}};
  for (const auto& verses : cases) {
    for (const char* book : {"", "Juan", "G\xC3\xA9nesis"}) {
      EXPECT_EQ(formatInto(64, book, verses), format(book, verses));
    }
  }
}
