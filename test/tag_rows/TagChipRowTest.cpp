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

namespace {

constexpr int GAP = 4;
constexpr int MORE = 40;

TagChips::Layout layoutOf(const std::vector<int>& widths, const int lineWidth, const int more = MORE) {
  TagChips::Layout out;
  TagChips::layout(widths.data(), static_cast<int>(widths.size()), more, lineWidth, GAP, out);
  return out;
}

}  // namespace

TEST(TagChipLayout, NoChipsPlaceNothing) {
  const auto layout = layoutOf({}, 400);
  EXPECT_EQ(layout.placedCount, 0);
  EXPECT_EQ(layout.lines, 0);
  EXPECT_FALSE(layout.overflow);
}

TEST(TagChipLayout, AllAndUnlabelledShareOneLine) {
  const auto layout = layoutOf({50, 90}, 400);
  ASSERT_EQ(layout.placedCount, 2);
  EXPECT_EQ(layout.placed[0].x, 0);
  EXPECT_EQ(layout.placed[1].x, 54);
  EXPECT_EQ(layout.placed[1].line, 0);
  EXPECT_EQ(layout.lines, 1);
  EXPECT_FALSE(layout.overflow);
}

TEST(TagChipLayout, WrapsOntoASecondLineWithoutEllipsis) {
  const auto layout = layoutOf({150, 150, 150, 150}, 400);
  ASSERT_EQ(layout.placedCount, 4);
  EXPECT_EQ(layout.placed[2].line, 1);
  EXPECT_EQ(layout.placed[2].x, 0);
  EXPECT_EQ(layout.lines, 2);
  EXPECT_FALSE(layout.overflow);
}

TEST(TagChipLayout, OverflowPutsEllipsisLastOnLineTwo) {
  const auto layout = layoutOf({150, 150, 150, 150, 150, 150}, 400);
  ASSERT_EQ(layout.placedCount, 5);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(layout.placed[i].chip, i) << "chips are the input prefix, in order";
  EXPECT_EQ(layout.placed[4].chip, -1);
  EXPECT_EQ(layout.placed[4].line, 1);
  EXPECT_EQ(layout.placed[4].x, 308);
  EXPECT_TRUE(layout.overflow);
  EXPECT_EQ(layout.lines, 2);
}

TEST(TagChipLayout, EllipsisDropsItsNeighbourWhenItDoesNotFit) {
  const auto layout = layoutOf({190, 190, 190, 190, 190, 190}, 400);
  ASSERT_EQ(layout.placedCount, 4);
  EXPECT_EQ(layout.placed[2].chip, 2);
  EXPECT_EQ(layout.placed[3].chip, -1);
  EXPECT_EQ(layout.placed[3].line, 1);
  EXPECT_EQ(layout.placed[3].x, 194);
  EXPECT_LE(layout.placed[3].x + layout.placed[3].width, 400);
}

TEST(TagChipLayout, ChipWiderThanTheLineIsClampedToIt) {
  const auto layout = layoutOf({1000}, 400);
  ASSERT_EQ(layout.placedCount, 1);
  EXPECT_EQ(layout.placed[0].width, 400);
}

TEST(TagChipLayout, NeverPlacesMoreThanMaxChips) {
  const std::vector<int> narrow(30, 10);
  const auto layout = layoutOf(narrow, 400, 20);
  EXPECT_LE(layout.placedCount, TagChips::MAX_CHIPS + 1);
  EXPECT_TRUE(layout.overflow);
}

// chips_ holds at most MAX_CHIPS + 1 candidates; a full list must still show the ellipsis.
TEST(TagChipLayout, TruncatedCandidatesStillOverflow) {
  const std::vector<int> narrow(TagChips::MAX_CHIPS + 1, 10);
  EXPECT_TRUE(layoutOf(narrow, 2000, 20).overflow);
}

TEST(TagChipLayout, InvariantsHoldForEveryCandidateCount) {
  for (int n = 0; n <= 60; ++n) {
    std::vector<int> widths;
    for (int i = 0; i < n; ++i) widths.push_back(20 + (i * 37) % 120);
    const auto layout = layoutOf(widths, 300, 30);
    EXPECT_LE(layout.lines, TagChips::MAX_LINES) << n;
    EXPECT_LE(layout.placedCount, TagChips::MAX_CHIPS + 1) << n;
    int expectedChip = 0;
    for (int i = 0; i < layout.placedCount; ++i) {
      const auto& p = layout.placed[i];
      EXPECT_GE(p.x, 0) << n;
      EXPECT_LE(p.x + p.width, 300) << n;
      EXPECT_LT(p.line, TagChips::MAX_LINES) << n;
      if (p.chip >= 0) {
        EXPECT_EQ(p.chip, expectedChip++) << n;
      }
    }
    if (layout.overflow) {
      EXPECT_EQ(layout.placed[layout.placedCount - 1].chip, -1) << n;
    } else {
      EXPECT_EQ(layout.placedCount, n) << n;
    }
  }
}

TEST(TagChipGeometry, BandHeightCoversEveryLine) {
  EXPECT_EQ(TagChips::bandHeight(0, 30, GAP), 0);
  EXPECT_EQ(TagChips::bandHeight(1, 30, GAP), 30);
  EXPECT_EQ(TagChips::bandHeight(2, 30, GAP), 64);
  EXPECT_EQ(TagChips::lineTop(1, 30, GAP), 34);
}

// A11b: hit rects are the visual rects grown by hitPadding; they must never overlap, or a tap on
// one chip filters by its neighbour.
TEST(TagChipGeometry, HitRectsNeverIntersect) {
  constexpr int chipHeight = 30;
  for (const int gap : {3, 4, 5, 8}) {
    for (int n = 1; n <= 40; ++n) {
      std::vector<int> widths;
      for (int i = 0; i < n; ++i) widths.push_back(30 + (i * 53) % 140);
      TagChips::Layout layout;
      TagChips::layout(widths.data(), n, 30, 320, gap, layout);

      struct Box {
        int left, top, right, bottom;
      };
      std::vector<Box> boxes;
      for (int i = 0; i < layout.placedCount; ++i) {
        const auto& p = layout.placed[i];
        const auto pad = TagChips::hitPadding(layout, i, gap);
        const int top = TagChips::lineTop(p.line, chipHeight, gap);
        boxes.push_back({p.x - pad.left, top - pad.top, p.x + p.width + pad.right, top + chipHeight + pad.bottom});
      }
      for (size_t a = 0; a < boxes.size(); ++a) {
        for (size_t b = a + 1; b < boxes.size(); ++b) {
          const bool overlap = boxes[a].left < boxes[b].right && boxes[b].left < boxes[a].right &&
                               boxes[a].top < boxes[b].bottom && boxes[b].top < boxes[a].bottom;
          EXPECT_FALSE(overlap) << "gap " << gap << ", n " << n << ", chips " << a << " and " << b;
        }
      }
    }
  }
}

TEST(TagChipGeometry, NeighboursShareTheGapExactly) {
  const auto layout = layoutOf({100, 100, 100, 100}, 250);
  const auto first = TagChips::hitPadding(layout, 0, GAP);
  const auto second = TagChips::hitPadding(layout, 1, GAP);
  EXPECT_EQ(first.left, 0);
  EXPECT_EQ(first.right + second.left, GAP);
  const auto below = TagChips::hitPadding(layout, 2, GAP);
  EXPECT_EQ(first.bottom + below.top, GAP);
  EXPECT_EQ(below.bottom, 0);
}
