#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/TagDisplayOrder.h"

namespace {

std::vector<uint16_t> rawIds(const study::DisplayOrderedTags& ordered) {
  std::vector<uint16_t> out;
  out.reserve(ordered.count);
  for (size_t i = 0; i < ordered.count; ++i) out.push_back(study::toRaw(ordered.ids[i]));
  return out;
}

TEST(TagDisplayOrder, OrdersAPassagesTagsByName) {
  study::TagPalette p;
  const auto gamma = *p.add("gamma");
  const auto alfa = *p.add("alfa");
  const auto beta = *p.add("beta");
  EXPECT_EQ(rawIds(study::inDisplayOrder(p, {gamma, alfa, beta})),
            (std::vector<uint16_t>{study::toRaw(alfa), study::toRaw(beta), study::toRaw(gamma)}));
}

TEST(TagDisplayOrder, PlacesARetiredTagByItsName) {
  study::TagPalette p;
  const auto gamma = *p.add("gamma");
  const auto beta = *p.add("beta");
  const auto alfa = *p.add("alfa");
  p.retire(beta);
  EXPECT_EQ(rawIds(study::inDisplayOrder(p, {gamma, beta, alfa})),
            (std::vector<uint16_t>{study::toRaw(alfa), study::toRaw(beta), study::toRaw(gamma)}))
      << "a passage still carrying a retired tag must list it in place";
}

TEST(TagDisplayOrder, PutsUnknownIdsLastByRawId) {
  study::TagPalette p;
  const auto alfa = *p.add("alfa");
  EXPECT_EQ(rawIds(study::inDisplayOrder(p, {study::toTagId(900), alfa, study::toTagId(800)})),
            (std::vector<uint16_t>{study::toRaw(alfa), 800, 900}));
}

TEST(TagDisplayOrder, LeavesTheStoredOrderAlone) {
  study::TagPalette p;
  const auto gamma = *p.add("gamma");
  const auto alfa = *p.add("alfa");
  const std::vector<study::TagId> stored{gamma, alfa};

  study::inDisplayOrder(p, stored);
  EXPECT_EQ(stored, (std::vector<study::TagId>{gamma, alfa})) << "the sleep screen reads a passage's first stored tag";
}

TEST(TagDisplayOrder, ConsidersAtMostTheTagCap) {
  study::TagPalette p;
  std::vector<study::TagId> nine;
  for (int i = 0; i < 9; ++i) nine.push_back(*p.add("t" + std::to_string(i)));
  EXPECT_EQ(study::inDisplayOrder(p, nine).count, study::PassageDoc::MAX_TAGS_PER_PASSAGE)
      << "PassageDoc never stores more, and the result has no room for more";
}

}  // namespace
