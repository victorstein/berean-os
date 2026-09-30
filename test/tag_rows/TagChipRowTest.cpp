#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "activities/reader/TagChipRow.h"

using study::TagId;
using study::toTagId;
using study::UNLABELLED;

namespace {

const TagId HOPE = toTagId(1);
const TagId MINISTRY = toTagId(2);
const TagId NAME = toTagId(3);

}  // namespace

TEST(TagChipFilter, NoFilterMatchesEveryPassage) {
  EXPECT_TRUE(TagChips::passageMatches({HOPE}, std::nullopt));
  EXPECT_TRUE(TagChips::passageMatches({UNLABELLED}, std::nullopt));
}

TEST(TagChipFilter, TagFilterMatchesPassagesCarryingIt) {
  EXPECT_TRUE(TagChips::passageMatches({HOPE, MINISTRY}, MINISTRY));
  EXPECT_FALSE(TagChips::passageMatches({HOPE}, NAME));
}

TEST(TagChipFilter, UnlabelledFilterMatchesOnlyUnlabelledPassages) {
  EXPECT_TRUE(TagChips::passageMatches({UNLABELLED}, UNLABELLED));
  EXPECT_FALSE(TagChips::passageMatches({HOPE}, UNLABELLED));
}

namespace {

struct FakePassage {
  std::vector<TagId> tags;
};

// Mirrors PassageDoc's normaliseTags: no duplicates, UNLABELLED only alone.
std::vector<TagId> normalised(std::vector<TagId> tags) {
  std::vector<TagId> out;
  for (const TagId id : tags) {
    if (id == UNLABELLED) continue;
    if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  }
  if (out.empty()) out.push_back(UNLABELLED);
  return out;
}

}  // namespace

TEST(TagChipCounts, AllIsThePassageCount) {
  const std::vector<FakePassage> passages{{{HOPE}}, {{MINISTRY}}, {{UNLABELLED}}};
  EXPECT_EQ(TagChips::count(passages, {HOPE, MINISTRY}).all, 3u);
}

TEST(TagChipCounts, MultiTagPassageCountsUnderEachTag) {
  const std::vector<FakePassage> passages{{{HOPE, MINISTRY}}, {{HOPE}}};
  const auto counts = TagChips::count(passages, {HOPE, MINISTRY});
  ASSERT_EQ(counts.perTag.size(), 2u);
  EXPECT_EQ(counts.perTag[0], 2);
  EXPECT_EQ(counts.perTag[1], 1);
  EXPECT_EQ(counts.unlabelled, 0u);
}

TEST(TagChipCounts, UnlabelledCountsOnlyUnderUnlabelled) {
  const std::vector<FakePassage> passages{{{UNLABELLED}}, {{UNLABELLED}}, {{HOPE}}};
  const auto counts = TagChips::count(passages, {HOPE});
  EXPECT_EQ(counts.unlabelled, 2u);
  EXPECT_EQ(counts.perTag[0], 1);
}

TEST(TagChipCounts, PassageCarryingOnlyARetiredTagCountsUnderAllAlone) {
  const TagId retired = toTagId(9);
  const std::vector<FakePassage> passages{{{retired}}, {{HOPE}}};
  const auto counts = TagChips::count(passages, {HOPE});
  EXPECT_EQ(counts.all, 2u);
  EXPECT_EQ(counts.unlabelled, 0u);
  EXPECT_EQ(counts.perTag[0], 1);
  EXPECT_LE(counts.unlabelled, counts.all);
}

TEST(TagChipCounts, ZeroPassagesCountNothing) {
  const std::vector<FakePassage> passages;
  const auto counts = TagChips::count(passages, {HOPE, MINISTRY});
  EXPECT_EQ(counts.all, 0u);
  EXPECT_EQ(counts.unlabelled, 0u);
  EXPECT_EQ(counts.perTag, (std::vector<uint16_t>{0, 0}));
}

TEST(TagChipCounts, PerTagFollowsTheActiveIdOrderNotIdOrder) {
  const TagId late = toTagId(5);
  const std::vector<FakePassage> passages{{{late}}, {{late}}, {{MINISTRY}}};
  const auto counts = TagChips::count(passages, {late, MINISTRY});
  EXPECT_EQ(counts.perTag, (std::vector<uint16_t>{2, 1}));
}

// Criterion 1: each chip's count is exactly the number of rows its filter shows.
TEST(TagChipCounts, EveryCountEqualsTheRowsItsFilterShows) {
  const std::vector<TagId> active{toTagId(1), toTagId(2), toTagId(3), toTagId(5)};  // 4 and 6 are retired
  uint32_t seed = 12345;
  const auto next = [&seed] {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7fff;
  };
  std::vector<FakePassage> passages;
  for (int i = 0; i < 300; ++i) {
    std::vector<TagId> tags;
    const int n = static_cast<int>(next() % 4);
    for (int k = 0; k < n; ++k) tags.push_back(toTagId(static_cast<uint16_t>(1 + next() % 6)));
    passages.push_back({normalised(tags)});
  }

  const auto shown = [&passages](const std::optional<TagId> filter) {
    size_t rows = 0;
    for (const auto& p : passages) rows += TagChips::passageMatches(p.tags, filter) ? 1 : 0;
    return rows;
  };

  const auto counts = TagChips::count(passages, active);
  EXPECT_EQ(counts.all, shown(std::nullopt));
  EXPECT_EQ(counts.unlabelled, shown(UNLABELLED));
  for (size_t slot = 0; slot < active.size(); ++slot) {
    EXPECT_EQ(static_cast<size_t>(counts.perTag[slot]), shown(active[slot])) << "slot " << slot;
  }
}
