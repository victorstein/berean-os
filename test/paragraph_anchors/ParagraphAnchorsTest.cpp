#include <gtest/gtest.h>

#include <cstring>

#include "ParagraphAnchors.h"

namespace {

// The shape real Watchtower documents use: body paragraphs interleaved with
// high-numbered study-question boxes, and a data-rel-pid attribute alongside.
const char* kDoc =
    "<html><head><title>skipme</title></head><body>"
    "<p id=\"p6\" data-pid=\"6\">alpha</p>"
    "<div class=\"gen-field\" id=\"p40\" data-pid=\"40\">bravo</div>"
    "<p id=\"p7\" data-pid=\"7\" data-rel-pid=\"[39]\">charlie</p>"
    "<h2 data-pid=\"10\">delta</h2>"
    "<legend data-pid=\"11\">echo</legend>"
    "<p>unnumbered</p>"
    "</body></html>";

TEST(ParagraphAnchorsScan, FindsEveryElementCarryingThePidAttribute) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_EQ(a.size(), 5u) << "p, div, h2 and legend all carry data-pid in real books";
  EXPECT_EQ(a[0].pid, 6);
  EXPECT_EQ(a[1].pid, 40);
  EXPECT_EQ(a[2].pid, 7);
  EXPECT_EQ(a[3].pid, 10);
  EXPECT_EQ(a[4].pid, 11);
}

TEST(ParagraphAnchorsScan, IsAscendingByOffsetNotByPid) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_EQ(a.size(), 5u);
  for (size_t i = 1; i < a.size(); ++i) EXPECT_LT(a[i - 1].offset, a[i].offset);
  EXPECT_GT(a[1].pid, a[2].pid) << "pid order is not document order; this is the normal case";
}

TEST(ParagraphAnchorsScan, DoesNotMatchDataRelPid) {
  const char* doc = "<html><body><p data-rel-pid=\"[39]\">alpha</p></body></html>";
  EXPECT_TRUE(ParagraphAnchors::scan(doc, strlen(doc)).empty());
}

TEST(ParagraphAnchorsScan, DoesNotCountTextOutsideBody) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_FALSE(a.empty());
  EXPECT_EQ(a[0].offset, 0u) << "<title> sits in <head> and must not advance the count";
}

TEST(ParagraphAnchorsScan, RejectsANonNumericPid) {
  const char* doc = "<html><body><p data-pid=\"7x\">alpha</p></body></html>";
  EXPECT_TRUE(ParagraphAnchors::scan(doc, strlen(doc)).empty());
}

TEST(ParagraphAnchorsFind, ReturnsTheGreatestAnchorAtOrBelowTheOffset) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_EQ(a.size(), 5u);
  const auto* found = ParagraphAnchors::find(a, a[2].offset + 2);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->pid, 7);
}

TEST(ParagraphAnchorsFind, ReturnsNullBelowTheFirstAnchor) {
  const std::vector<ParagraphAnchors::ParagraphAnchor> a{{10, 5}};
  EXPECT_EQ(ParagraphAnchors::find(a, 4), nullptr);
}

TEST(ParagraphAnchorsScan, ReturnsEmptyOnMalformedMarkup) {
  const char* doc = "<html><body><p data-pid=\"1\">alpha";
  EXPECT_TRUE(ParagraphAnchors::scan(doc, strlen(doc)).empty())
      << "a truncated list would resolve later passages to a stale anchor";
}

}  // namespace
