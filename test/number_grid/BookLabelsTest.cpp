#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "activities/reader/BookLabels.h"

// The Recent chips read a book's abbreviation from these rows. A row left holding a previous
// label, or cut mid-sequence, shows the wrong book or a replacement glyph on a chip.

namespace {

constexpr size_t ROW_BYTES = 16;

}  // namespace

TEST(BookLabels, CopiesLabelsIntoRowsInOrder) {
  char rows[3][ROW_BYTES] = {};
  const std::vector<std::string> labels = {"Gén.", "Éx.", "Le."};
  copyLabelRows(&rows[0][0], ROW_BYTES, 3, labels.data(), labels.size());
  EXPECT_STREQ(rows[0], "Gén.");
  EXPECT_STREQ(rows[1], "Éx.");
  EXPECT_STREQ(rows[2], "Le.");
}

TEST(BookLabels, RowsPastTheLastLabelAreEmptiedNotKept) {
  char rows[3][ROW_BYTES];
  for (auto& row : rows) std::strcpy(row, "old");
  const std::vector<std::string> labels = {"Gén."};
  copyLabelRows(&rows[0][0], ROW_BYTES, 3, labels.data(), labels.size());
  EXPECT_STREQ(rows[0], "Gén.");
  EXPECT_STREQ(rows[1], "");
  EXPECT_STREQ(rows[2], "");
}

TEST(BookLabels, AnEmptyLabelGivesAnEmptyRow) {
  char rows[1][ROW_BYTES];
  std::strcpy(rows[0], "old");
  const std::vector<std::string> labels = {""};
  copyLabelRows(&rows[0][0], ROW_BYTES, 1, labels.data(), labels.size());
  EXPECT_STREQ(rows[0], "");
}

TEST(BookLabels, TruncationNeverSplitsAUtf8Sequence) {
  char row[4];
  // "ÉÉ" is four bytes; three fit, and the third is the lead byte of the second É.
  copyUtf8Truncated(row, sizeof(row), "\xC3\x89\xC3\x89");
  EXPECT_STREQ(row, "\xC3\x89");
}

TEST(BookLabels, NoLabelsEmptiesEveryRow) {
  char rows[2][ROW_BYTES];
  for (auto& row : rows) std::strcpy(row, "old");
  copyLabelRows(&rows[0][0], ROW_BYTES, 2, nullptr, 0);
  EXPECT_STREQ(rows[0], "");
  EXPECT_STREQ(rows[1], "");
}
