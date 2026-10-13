#include <StudyStore/PassageDoc.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
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

// Passages 0-2 are in the chapter the "Tags here" view shows; 3-5 are elsewhere in the publication.
const std::vector<FakePassage> PUBLICATION{{{HOPE}}, {{HOPE, MINISTRY}}, {{UNLABELLED}},
                                           {{HOPE}}, {{NAME}},           {{MINISTRY}}};
const std::vector<size_t> CHAPTER{2, 1, 0};

}  // namespace

TEST(TagChipScopedCounts, PublicationWideCountsEveryPassage) {
  const auto counts = TagChips::count(PUBLICATION, {HOPE, MINISTRY, NAME});
  EXPECT_EQ(counts.all, 6u);
  EXPECT_EQ(counts.perTag, (std::vector<uint16_t>{3, 2, 1}));
  EXPECT_EQ(counts.unlabelled, 1u);
}

TEST(TagChipScopedCounts, ChapterScopeCountsOnlyItsPassages) {
  const auto counts = TagChips::countIn(PUBLICATION, CHAPTER, {HOPE, MINISTRY, NAME});
  EXPECT_EQ(counts.all, CHAPTER.size());
  EXPECT_EQ(counts.perTag[0], 2);
  EXPECT_EQ(counts.perTag[1], 1);
  EXPECT_EQ(counts.unlabelled, 1u);
}

TEST(TagChipScopedCounts, TagAbsentFromTheChapterCountsZero) {
  const auto counts = TagChips::countIn(PUBLICATION, CHAPTER, {HOPE, MINISTRY, NAME});
  EXPECT_EQ(counts.perTag[2], 0);
}

TEST(TagChipScopedCounts, OutOfRangeScopeIndicesAreSkipped) {
  const auto counts = TagChips::countIn(PUBLICATION, {0, 99}, {HOPE});
  EXPECT_EQ(counts.all, 1u);
  EXPECT_EQ(counts.perTag[0], 1);
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

namespace {

TagChips::Counts countsOf(const size_t all, const size_t unlabelled, std::vector<uint16_t> perTag) {
  TagChips::Counts counts;
  counts.all = all;
  counts.unlabelled = unlabelled;
  counts.perTag = std::move(perTag);
  return counts;
}

std::vector<TagChips::Candidate> candidatesOf(const TagChips::Counts& counts, const std::vector<TagId>& active,
                                              const std::optional<TagId> filter, const size_t cap = SIZE_MAX) {
  std::vector<TagChips::Candidate> out;
  TagChips::candidates(counts, active, filter, cap, out);
  return out;
}

}  // namespace

TEST(TagChipCandidates, AllThenTagsInActiveOrderThenUnlabelled) {
  const auto out = candidatesOf(countsOf(6, 1, {3, 2, 1}), {HOPE, MINISTRY, NAME}, std::nullopt);
  ASSERT_EQ(out.size(), 5u);
  EXPECT_EQ(out[0].kind, TagChips::Kind::All);
  for (uint16_t slot = 0; slot < 3; ++slot) {
    EXPECT_EQ(out[1 + slot].kind, TagChips::Kind::Tag);
    EXPECT_EQ(out[1 + slot].slot, slot);
  }
  EXPECT_EQ(out[4].kind, TagChips::Kind::Unlabelled);
}

TEST(TagChipCandidates, ZeroCountTagIsDroppedUnlessItIsTheFilter) {
  const auto unfiltered = candidatesOf(countsOf(3, 0, {3, 0}), {HOPE, MINISTRY}, std::nullopt);
  ASSERT_EQ(unfiltered.size(), 2u);
  EXPECT_EQ(unfiltered[1].slot, 0);

  const auto filtered = candidatesOf(countsOf(3, 0, {3, 0}), {HOPE, MINISTRY}, MINISTRY);
  ASSERT_EQ(filtered.size(), 3u);
  EXPECT_EQ(filtered[2].kind, TagChips::Kind::Tag);
  EXPECT_EQ(filtered[2].slot, 1);
}

TEST(TagChipCandidates, ZeroUnlabelledIsDroppedUnlessItIsTheFilter) {
  EXPECT_EQ(candidatesOf(countsOf(1, 0, {1}), {HOPE}, std::nullopt).size(), 2u);
  const auto filtered = candidatesOf(countsOf(1, 0, {1}), {HOPE}, UNLABELLED);
  ASSERT_EQ(filtered.size(), 3u);
  EXPECT_EQ(filtered[2].kind, TagChips::Kind::Unlabelled);
}

TEST(TagChipCandidates, CapStopsTheListAndUncappedKeepsEveryCandidate) {
  std::vector<TagId> active;
  std::vector<uint16_t> perTag;
  for (uint16_t raw = 1; raw <= 200; ++raw) {
    active.push_back(toTagId(raw));
    perTag.push_back(1);
  }
  const auto counts = countsOf(200, 2, perTag);
  EXPECT_EQ(candidatesOf(counts, active, std::nullopt, TagChips::MAX_CHIPS + 1).size(),
            static_cast<size_t>(TagChips::MAX_CHIPS + 1));
  EXPECT_EQ(candidatesOf(counts, active, std::nullopt).size(), 202u);
  EXPECT_TRUE(candidatesOf(counts, active, std::nullopt, 0).empty());
}

TEST(TagChipCandidates, ChapterScopedCountsDecideWhichTagsAppear) {
  const std::vector<TagId> active{HOPE, MINISTRY, NAME};
  const auto out = candidatesOf(TagChips::countIn(PUBLICATION, CHAPTER, active), active, std::nullopt);
  ASSERT_EQ(out.size(), 4u) << "All, hope, ministry, Unlabelled; name is absent from the chapter";
  EXPECT_EQ(out[1].slot, 0);
  EXPECT_EQ(out[2].slot, 1);
  EXPECT_EQ(out[3].kind, TagChips::Kind::Unlabelled);
}

// Replaces the FilterRows guard: a long-press retires only a Kind::Tag chip, so no other kind may
// ever be one, and every Tag slot must name a real palette entry.
TEST(TagChipCandidates, OnlyTagCandidatesNameAPaletteSlot) {
  const std::vector<TagId> active{HOPE, MINISTRY, NAME};
  for (const std::optional<TagId> filter : {std::optional<TagId>{}, std::optional<TagId>{UNLABELLED},
                                            std::optional<TagId>{NAME}, std::optional<TagId>{toTagId(42)}}) {
    const auto out = candidatesOf(countsOf(5, 2, {2, 0, 0}), active, filter);
    int tags = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      if (out[i].kind != TagChips::Kind::Tag) continue;
      ++tags;
      ASSERT_LT(out[i].slot, active.size());
    }
    EXPECT_EQ(out.front().kind, TagChips::Kind::All);
    EXPECT_EQ(tags, filter == NAME ? 2 : 1);
  }
}

// The rank task passes name-ordered ids; All and Unlabelled must stay pinned around them, and a
// zero-count filter keeps its name-order slot.
TEST(TagChipCandidates, NameOrderedIdsKeepAllFirstAndUnlabelledLast) {
  const std::vector<TagId> nameOrdered{NAME, HOPE, MINISTRY};
  const std::vector<FakePassage> passages{{{NAME}}, {{NAME, MINISTRY}}, {{UNLABELLED}}};
  const TagChips::Counts counts = TagChips::count(passages, nameOrdered);
  EXPECT_EQ(counts.perTag, (std::vector<uint16_t>{2, 0, 1}));

  const auto out = candidatesOf(counts, nameOrdered, HOPE);
  ASSERT_EQ(out.size(), 5u);
  EXPECT_EQ(out[0].kind, TagChips::Kind::All);
  for (uint16_t slot = 0; slot < 3; ++slot) {
    EXPECT_EQ(out[1 + slot].kind, TagChips::Kind::Tag);
    EXPECT_EQ(out[1 + slot].slot, slot);
  }
  EXPECT_EQ(out[4].kind, TagChips::Kind::Unlabelled);
}

TEST(TagChipGeometry, ChipHeightIsNeverBelowTheTouchMinimum) {
  EXPECT_EQ(TagChips::chipHeight(20, 8, 44), 44) << "20 + 16 is short of 44";
  EXPECT_EQ(TagChips::chipHeight(30, 8, 44), 46) << "text plus padding wins once it is taller";
  for (int line = 0; line <= 60; ++line) EXPECT_GE(TagChips::chipHeight(line, 8, 44), 44) << line;
}

TEST(TagChipGeometry, ArrayHitPaddingMatchesTheLayoutOverload) {
  const auto layout = layoutOf({100, 100, 100, 100, 100}, 250);
  for (int i = 0; i < layout.placedCount; ++i) {
    const auto viaLayout = TagChips::hitPadding(layout, i, GAP);
    const auto viaArray = TagChips::hitPadding(layout.placed, layout.placedCount, layout.lines, i, GAP);
    EXPECT_EQ(viaArray.top, viaLayout.top) << i;
    EXPECT_EQ(viaArray.right, viaLayout.right) << i;
    EXPECT_EQ(viaArray.bottom, viaLayout.bottom) << i;
    EXPECT_EQ(viaArray.left, viaLayout.left) << i;
  }
}

namespace {

// Lyra-like portrait body: 480 wide less a 20 px inset each side; chips at the 44 px touch minimum.
constexpr int GRID_LINE = 440;
constexpr int GRID_GAP = 5;
constexpr int GRID_CHIP = 44;
constexpr int GRID_BODY = 600;

std::vector<int> variedWidths(const int n) {
  std::vector<int> widths;
  widths.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) widths.push_back(44 + (i * 53) % 180);
  return widths;
}

}  // namespace

TEST(TagChipGrid, LinesPerPageIsNeverBelowOne) {
  EXPECT_EQ(TagChips::linesPerPage(GRID_BODY, GRID_CHIP, GRID_GAP), 12);
  EXPECT_EQ(TagChips::linesPerPage(10, GRID_CHIP, GRID_GAP), 1);
  EXPECT_EQ(TagChips::linesPerPage(0, 0, GRID_GAP), 1);
}

// The owner's case: 24 chips like "sabiduria 9" (~140 px) fit one portrait screen.
TEST(TagChipGrid, OwnersTwentyFourChipsFitOnePage) {
  const std::vector<int> widths(24, 140);
  const int lines = TagChips::linesPerPage(GRID_BODY, GRID_CHIP, GRID_GAP);
  EXPECT_EQ(TagChips::pageCountOf(widths.data(), 24, GRID_LINE, GRID_GAP, lines), 1);
  TagChips::GridPage page;
  TagChips::layoutPage(widths.data(), 24, 0, GRID_LINE, GRID_GAP, lines, page);
  EXPECT_EQ(page.placedCount, 24);
  EXPECT_EQ(page.first, 0);
  EXPECT_EQ(page.lines, 8) << "three 140 px chips per 440 px line";
}

TEST(TagChipGrid, NoChipsGiveNoPages) {
  EXPECT_EQ(TagChips::pageCountOf(nullptr, 0, GRID_LINE, GRID_GAP, 12), 0);
  TagChips::GridPage page;
  TagChips::layoutPage(nullptr, 0, 0, GRID_LINE, GRID_GAP, 12, page);
  EXPECT_EQ(page.placedCount, 0);
  EXPECT_EQ(page.lines, 0);
}

TEST(TagChipGrid, AFullPaletteSplitsIntoPagesThatCoverEveryChipOnce) {
  constexpr int count = 202;
  const auto widths = variedWidths(count);
  const int lines = TagChips::linesPerPage(GRID_BODY, GRID_CHIP, GRID_GAP);
  const int pages = TagChips::pageCountOf(widths.data(), count, GRID_LINE, GRID_GAP, lines);
  EXPECT_GT(pages, 1);

  int expected = 0;
  int seenPages = 0;
  for (int first = 0; first < count;
       first = TagChips::nextPageStart(widths.data(), count, first, GRID_LINE, GRID_GAP, lines)) {
    TagChips::GridPage page;
    TagChips::layoutPage(widths.data(), count, first, GRID_LINE, GRID_GAP, lines, page);
    ++seenPages;
    ASSERT_GT(page.placedCount, 0) << "a page always makes progress";
    EXPECT_LE(page.placedCount, TagChips::GRID_MAX_CHIPS);
    EXPECT_LE(page.lines, lines);
    for (int k = 0; k < page.placedCount; ++k) {
      const auto& p = page.placed[k];
      EXPECT_EQ(p.chip, expected++) << "chips in order, each exactly once";
      EXPECT_GE(p.line, 0);
      EXPECT_LT(p.line, page.lines) << "lines count from the top of the page";
      EXPECT_LE(p.x + p.width, GRID_LINE);
    }
  }
  EXPECT_EQ(expected, count);
  EXPECT_EQ(seenPages, pages);
}

TEST(TagChipGrid, NarrowChipsStopAtTheInteractionCap) {
  const std::vector<int> widths(202, 10);
  TagChips::GridPage page;
  TagChips::layoutPage(widths.data(), 202, 0, GRID_LINE, GRID_GAP, 12, page);
  EXPECT_EQ(page.placedCount, TagChips::GRID_MAX_CHIPS);
  EXPECT_EQ(TagChips::nextPageStart(widths.data(), 202, 0, GRID_LINE, GRID_GAP, 12), TagChips::GRID_MAX_CHIPS);
}

TEST(TagChipGrid, AChipWiderThanTheLineStillGetsAPage) {
  const std::vector<int> widths{2000, 2000};
  EXPECT_EQ(TagChips::nextPageStart(widths.data(), 2, 0, GRID_LINE, GRID_GAP, 1), 1);
  EXPECT_EQ(TagChips::pageCountOf(widths.data(), 2, GRID_LINE, GRID_GAP, 1), 2);
  TagChips::GridPage page;
  TagChips::layoutPage(widths.data(), 2, 1, GRID_LINE, GRID_GAP, 1, page);
  ASSERT_EQ(page.placedCount, 1);
  EXPECT_EQ(page.placed[0].chip, 1);
  EXPECT_EQ(page.placed[0].width, GRID_LINE);
}

TEST(TagChipGrid, PageHoldingAgreesWithTheLaidOutPages) {
  constexpr int count = 202;
  const auto widths = variedWidths(count);
  const int lines = 5;
  int page = 0;
  for (int first = 0; first < count; ++page) {
    const int next = TagChips::nextPageStart(widths.data(), count, first, GRID_LINE, GRID_GAP, lines);
    for (int index = first; index < next; ++index) {
      const auto span = TagChips::pageHolding(widths.data(), count, index, GRID_LINE, GRID_GAP, lines);
      EXPECT_EQ(span.first, first) << index;
      EXPECT_EQ(span.next, next) << index;
      EXPECT_EQ(span.page, page) << index;
    }
    first = next;
  }
  const auto clampedHigh = TagChips::pageHolding(widths.data(), count, 9999, GRID_LINE, GRID_GAP, lines);
  EXPECT_EQ(clampedHigh.next, count);
  EXPECT_EQ(clampedHigh.page, page - 1);
  EXPECT_EQ(TagChips::pageHolding(widths.data(), count, -5, GRID_LINE, GRID_GAP, lines).first, 0);
}

// Taking a strip out of the body can only keep a paged grid paged (spec A9's two-step pagination).
TEST(TagChipGrid, ShorterBodyNeverHasFewerPages) {
  const auto widths = variedWidths(120);
  for (int body = 100; body <= 700; body += 25) {
    const int tall = TagChips::linesPerPage(body, GRID_CHIP, GRID_GAP);
    const int shorter = TagChips::linesPerPage(body - 50, GRID_CHIP, GRID_GAP);
    EXPECT_GE(TagChips::pageCountOf(widths.data(), 120, GRID_LINE, GRID_GAP, shorter),
              TagChips::pageCountOf(widths.data(), 120, GRID_LINE, GRID_GAP, tall))
        << body;
  }
}

TEST(TagChipGrid, GridHitRectsNeverIntersect) {
  const auto widths = variedWidths(202);
  for (const int gap : {3, 4, 5, 8}) {
    for (int first = 0; first < 202; first = TagChips::nextPageStart(widths.data(), 202, first, GRID_LINE, gap, 6)) {
      TagChips::GridPage page;
      TagChips::layoutPage(widths.data(), 202, first, GRID_LINE, gap, 6, page);
      struct Box {
        int left, top, right, bottom;
      };
      std::vector<Box> boxes;
      boxes.reserve(static_cast<size_t>(page.placedCount));
      for (int i = 0; i < page.placedCount; ++i) {
        const auto& p = page.placed[i];
        const auto pad = TagChips::hitPadding(page.placed, page.placedCount, page.lines, i, gap);
        const int top = TagChips::lineTop(p.line, GRID_CHIP, gap);
        boxes.push_back({p.x - pad.left, top - pad.top, p.x + p.width + pad.right, top + GRID_CHIP + pad.bottom});
      }
      for (size_t a = 0; a < boxes.size(); ++a) {
        for (size_t b = a + 1; b < boxes.size(); ++b) {
          const bool overlap = boxes[a].left < boxes[b].right && boxes[b].left < boxes[a].right &&
                               boxes[a].top < boxes[b].bottom && boxes[b].top < boxes[a].bottom;
          EXPECT_FALSE(overlap) << "gap " << gap << ", page at " << first << ", chips " << a << " and " << b;
        }
      }
    }
  }
}

namespace {

constexpr size_t CAP = study::PassageDoc::MAX_TAGS_PER_PASSAGE;

}  // namespace

static_assert(TagChips::pickerState(TagChips::Kind::NewTag, false, CAP, CAP) == TagChips::ChipState::Action);

TEST(TagChipPickerState, BelowTheCapUnappliedTagsAreInactive) {
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP - 1, CAP), TagChips::ChipState::Inactive);
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, true, CAP - 1, CAP), TagChips::ChipState::Active);
}

TEST(TagChipPickerState, AtTheCapUnappliedTagsAreDisabled) {
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP, CAP), TagChips::ChipState::Disabled);
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, true, CAP, CAP), TagChips::ChipState::Active);
}

// At the cap one applied tag is removed: the chip that was Disabled and the chip just removed are
// both unapplied with one slot free, so both are Inactive.
TEST(TagChipPickerState, ADeselectAtTheCapMakesUnappliedTagsInactiveAgain) {
  ASSERT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP, CAP), TagChips::ChipState::Disabled);
  ASSERT_EQ(TagChips::pickerState(TagChips::Kind::Tag, true, CAP, CAP), TagChips::ChipState::Active);
  const size_t afterDeselect = CAP - 1;
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, afterDeselect, CAP), TagChips::ChipState::Inactive);
}

TEST(TagChipPickerState, OverTheCapUnappliedTagsStayDisabled) {
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP + 1, CAP), TagChips::ChipState::Disabled);
}

TEST(TagChipPickerState, NewTagIsAnActionAtAnyCount) {
  for (const size_t applied : {size_t{0}, CAP - 1, CAP}) {
    EXPECT_EQ(TagChips::pickerState(TagChips::Kind::NewTag, false, applied, CAP), TagChips::ChipState::Action)
        << applied << " applied";
  }
}

namespace {

// findChip reads only .kind and .id, so a plain struct stands in for TagChipView::ChipEntry.
struct FakeChip {
  TagChips::Kind kind;
  TagId id;
};

const std::vector<FakeChip> CHIPS{{TagChips::Kind::NewTag, UNLABELLED},
                                  {TagChips::Kind::All, UNLABELLED},
                                  {TagChips::Kind::Tag, HOPE},
                                  {TagChips::Kind::Tag, NAME},
                                  {TagChips::Kind::Unlabelled, UNLABELLED}};

}  // namespace

TEST(TagChipFindChip, FindsATagById) {
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Tag, HOPE), 2);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Tag, NAME), 3);
}

TEST(TagChipFindChip, FindsTheSingletonKindsByKindAlone) {
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::NewTag, UNLABELLED), 0);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::All, UNLABELLED), 1);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Unlabelled, UNLABELLED), 4);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::All, HOPE), 1) << "the id is ignored for All";
}

TEST(TagChipFindChip, ARetiredTagOrAnEmptyListIsNotFound) {
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Tag, MINISTRY), -1);
  EXPECT_EQ(TagChips::findChip(std::vector<FakeChip>{}, TagChips::Kind::Tag, HOPE), -1);
  EXPECT_EQ(TagChips::findChip(std::vector<FakeChip>{}, TagChips::Kind::All, UNLABELLED), -1);
}

TEST(TagChipFindChip, TheKindMustMatch) {
  const std::vector<FakeChip> tagOnly{{TagChips::Kind::Tag, UNLABELLED}};
  EXPECT_EQ(TagChips::findChip(tagOnly, TagChips::Kind::Unlabelled, UNLABELLED), -1);
  const std::vector<FakeChip> unlabelledOnly{{TagChips::Kind::Unlabelled, UNLABELLED}};
  EXPECT_EQ(TagChips::findChip(unlabelledOnly, TagChips::Kind::Tag, UNLABELLED), -1);
}

TEST(TagChipReselect, FollowsATagThatMovedInARebuild) {
  const std::vector<FakeChip> resorted{{TagChips::Kind::All, UNLABELLED},
                                       {TagChips::Kind::Tag, NAME},
                                       {TagChips::Kind::Tag, MINISTRY},
                                       {TagChips::Kind::Tag, HOPE}};
  EXPECT_EQ(TagChips::reselect(resorted, TagChips::ChipKey{TagChips::Kind::Tag, HOPE}, 1), 3);
}

TEST(TagChipReselect, ARetiredTagClampsThePreviousIndex) {
  // CHIPS with NAME (index 3) retired.
  const std::vector<FakeChip> afterRetire{{TagChips::Kind::NewTag, UNLABELLED},
                                          {TagChips::Kind::All, UNLABELLED},
                                          {TagChips::Kind::Tag, HOPE},
                                          {TagChips::Kind::Unlabelled, UNLABELLED}};
  EXPECT_EQ(TagChips::reselect(afterRetire, TagChips::ChipKey{TagChips::Kind::Tag, NAME}, 3), 3)
      << "the chip that slid into its slot";

  const std::vector<FakeChip> lastRetired{{TagChips::Kind::All, UNLABELLED}, {TagChips::Kind::Tag, HOPE}};
  EXPECT_EQ(TagChips::reselect(lastRetired, TagChips::ChipKey{TagChips::Kind::Tag, NAME}, 2), 1)
      << "past the end: the last chip";
}

TEST(TagChipReselect, SingletonKindsAreFoundByKindAlone) {
  EXPECT_EQ(TagChips::reselect(CHIPS, TagChips::ChipKey{TagChips::Kind::NewTag, UNLABELLED}, 4), 0);
  EXPECT_EQ(TagChips::reselect(CHIPS, TagChips::ChipKey{TagChips::Kind::All, UNLABELLED}, 4), 1);
  EXPECT_EQ(TagChips::reselect(CHIPS, TagChips::ChipKey{TagChips::Kind::Unlabelled, UNLABELLED}, 0), 4);
}

TEST(TagChipReselect, WithoutAKeyThePreviousIndexIsClamped) {
  EXPECT_EQ(TagChips::reselect(CHIPS, std::nullopt, 2), 2);
  EXPECT_EQ(TagChips::reselect(CHIPS, std::nullopt, 9), 4);
  EXPECT_EQ(TagChips::reselect(CHIPS, std::nullopt, -1), 0);
}

TEST(TagChipReselect, AnEmptyListSelectsZero) {
  EXPECT_EQ(TagChips::reselect(std::vector<FakeChip>{}, TagChips::ChipKey{TagChips::Kind::Tag, HOPE}, 3), 0);
  EXPECT_EQ(TagChips::reselect(std::vector<FakeChip>{}, std::nullopt, 0), 0);
}

static_assert(TagChips::filterKey(std::nullopt).kind == TagChips::Kind::All);
static_assert(TagChips::filterKey(UNLABELLED).kind == TagChips::Kind::Unlabelled);
static_assert(TagChips::filterKey(study::toTagId(7)).kind == TagChips::Kind::Tag);

TEST(TagChipFilterKey, NoFilterIsTheAllChip) {
  const TagChips::ChipKey key = TagChips::filterKey(std::nullopt);
  EXPECT_EQ(key.kind, TagChips::Kind::All);
  EXPECT_EQ(TagChips::findChip(CHIPS, key.kind, key.id), 1);
}

TEST(TagChipFilterKey, UnlabelledIsTheUnlabelledChip) {
  const TagChips::ChipKey key = TagChips::filterKey(UNLABELLED);
  EXPECT_EQ(key.kind, TagChips::Kind::Unlabelled);
  EXPECT_EQ(TagChips::findChip(CHIPS, key.kind, key.id), 4);
}

TEST(TagChipFilterKey, ATagIsThatTagsChip) {
  const TagChips::ChipKey key = TagChips::filterKey(NAME);
  EXPECT_EQ(key.kind, TagChips::Kind::Tag);
  EXPECT_EQ(key.id, NAME);
  EXPECT_EQ(TagChips::findChip(CHIPS, key.kind, key.id), 3);
}

// candidates keeps a zero-count tag while it is the filter; the grid's entry selection must land on
// it at its name-order slot. Candidates carry a slot, not an id, so they are mapped the way
// TagChipView::buildEntries maps them.
TEST(TagChipFilterKey, AZeroCountFilterIsSelectedAtItsNameOrderSlot) {
  const std::vector<TagId> nameOrdered{NAME, HOPE, MINISTRY};
  const auto picked = candidatesOf(countsOf(3, 1, {2, 0, 1}), nameOrdered, HOPE);
  std::vector<FakeChip> chips;
  chips.reserve(picked.size());
  for (const TagChips::Candidate& candidate : picked) {
    chips.push_back(
        FakeChip{candidate.kind, candidate.kind == TagChips::Kind::Tag ? nameOrdered[candidate.slot] : UNLABELLED});
  }
  ASSERT_EQ(chips.size(), 5u) << "All, name, hope, ministry, Unlabelled";

  EXPECT_EQ(TagChips::reselect(chips, TagChips::filterKey(HOPE), 0), 2);
  EXPECT_EQ(TagChips::reselect(chips, TagChips::filterKey(std::nullopt), 3), 0);
  EXPECT_EQ(TagChips::reselect(chips, TagChips::filterKey(UNLABELLED), 0), 4);
}

namespace {

std::vector<uint16_t> rawIdsOf(const std::vector<TagId>& ids) {
  std::vector<uint16_t> out;
  out.reserve(ids.size());
  for (const TagId id : ids) out.push_back(study::toRaw(id));
  return out;
}

}  // namespace

TEST(TagChipSelectedInOrder, FollowsTheOrderNotTheSelection) {
  const std::vector<TagId> allocation{HOPE, MINISTRY, NAME};
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(allocation, {NAME, HOPE})), (std::vector<uint16_t>{1, 3}));
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(allocation, {HOPE, NAME})), (std::vector<uint16_t>{1, 3}));
}

TEST(TagChipSelectedInOrder, DropsIdsTheOrderDoesNotList) {
  const std::vector<TagId> active{HOPE, NAME};  // MINISTRY retired
  const TagId unknown = toTagId(99);
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(active, {MINISTRY, NAME, unknown, UNLABELLED})),
            (std::vector<uint16_t>{3}));
}

TEST(TagChipSelectedInOrder, EmitsEachIdOnce) {
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder({HOPE, NAME}, {NAME, HOPE, NAME})), (std::vector<uint16_t>{1, 3}));
}

TEST(TagChipSelectedInOrder, AnEmptySelectionOrOrderGivesNothing) {
  EXPECT_TRUE(TagChips::selectedInOrder({HOPE, NAME}, {}).empty());
  EXPECT_TRUE(TagChips::selectedInOrder({}, {HOPE, NAME}).empty());
}

TEST(TagChipSelectedInOrder, AFullSelectionComesBackInPaletteOrder) {
  std::vector<TagId> palette;
  palette.reserve(48);
  for (uint16_t raw = 1; raw <= 48; ++raw) palette.push_back(toTagId(raw));
  const std::vector<TagId> picked{toTagId(40), toTagId(3),  toTagId(17), toTagId(48),
                                  toTagId(1),  toTagId(25), toTagId(9),  toTagId(33)};
  ASSERT_EQ(picked.size(), CAP);
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(palette, picked)), (std::vector<uint16_t>{1, 3, 9, 17, 25, 33, 40, 48}));
}
