#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "activities/reader/SpineSearch.h"

// Go to resolved every book and chapter by walking the spine from item 0, which in the NWT is up
// to ~1,344 SD reads per open. These pin the walk that replaces it: where it starts, that it wraps
// rather than misses, and that it stops as soon as it has everything. Filenames are synthetic.

namespace {

struct FakeSpine {
  std::vector<std::string> hrefs;
  mutable int reads = 0;
  mutable int lowestRead = INT_MAX;
};

std::string_view hrefAt(const void* ctx, const int spineIndex, std::string& scratch) {
  const auto* spine = static_cast<const FakeSpine*>(ctx);
  spine->reads++;
  spine->lowestRead = std::min(spine->lowestRead, spineIndex);
  scratch = spine->hrefs[static_cast<size_t>(spineIndex)];
  return scratch;
}

FakeSpine makeSpine(const int count) {
  FakeSpine spine;
  for (int i = 0; i < count; i++) spine.hrefs.push_back("OEBPS/item" + std::to_string(i) + ".xhtml");
  return spine;
}

void resolve(const FakeSpine& spine, const std::vector<std::string>& targets, std::vector<int>& out, const int first) {
  SpineSearch::resolveFrom(targets.data(), out.data(), static_cast<int>(targets.size()), first,
                           static_cast<int>(spine.hrefs.size()), hrefAt, &spine);
}

}  // namespace

TEST(SpineSearch, TargetsAfterTheStartAreFoundWithoutReadingBelowIt) {
  const FakeSpine spine = makeSpine(20);
  const std::vector<std::string> targets = {"item12.xhtml", "item15.xhtml"};
  std::vector<int> out(2, -1);
  resolve(spine, targets, out, 10);
  EXPECT_EQ(out, (std::vector<int>{12, 15}));
  EXPECT_EQ(spine.lowestRead, 10);
  EXPECT_EQ(spine.reads, 6);  // 10..15, then stops
}

TEST(SpineSearch, ATargetBeforeTheStartIsFoundThroughTheWrap) {
  const FakeSpine spine = makeSpine(20);
  const std::vector<std::string> targets = {"item3.xhtml"};
  std::vector<int> out(1, -1);
  resolve(spine, targets, out, 10);
  EXPECT_EQ(out[0], 3);
  EXPECT_EQ(spine.reads, 14);  // 10..19, then 0..3
}

TEST(SpineSearch, APresetSlotIsNeverOverwritten) {
  const FakeSpine spine = makeSpine(20);
  const std::vector<std::string> targets = {"item2.xhtml", "item4.xhtml"};
  std::vector<int> out = {7, -1};
  resolve(spine, targets, out, 0);
  EXPECT_EQ(out, (std::vector<int>{7, 4}));
  EXPECT_EQ(spine.reads, 5);  // 0..4: the preset slot is not searched for
}

TEST(SpineSearch, AnAbsentTargetStaysUnresolvedAfterOneFullPass) {
  const FakeSpine spine = makeSpine(20);
  const std::vector<std::string> targets = {"missing.xhtml"};
  std::vector<int> out(1, -1);
  resolve(spine, targets, out, 5);
  EXPECT_EQ(out[0], -1);
  EXPECT_EQ(spine.reads, 20);
}

TEST(SpineSearch, AStartOutsideTheSpineBehavesAsZero) {
  for (const int first : {-5, 20, 99}) {
    const FakeSpine spine = makeSpine(20);
    const std::vector<std::string> targets = {"item1.xhtml"};
    std::vector<int> out(1, -1);
    resolve(spine, targets, out, first);
    EXPECT_EQ(out[0], 1) << "first=" << first;
    EXPECT_EQ(spine.lowestRead, 0) << "first=" << first;
    EXPECT_EQ(spine.reads, 2) << "first=" << first;
  }
}

TEST(SpineSearch, DuplicateTargetsBothResolve) {
  const FakeSpine spine = makeSpine(20);
  const std::vector<std::string> targets = {"item4.xhtml", "item4.xhtml"};
  std::vector<int> out(2, -1);
  resolve(spine, targets, out, 0);
  EXPECT_EQ(out, (std::vector<int>{4, 4}));
}

TEST(SpineSearch, NothingToResolveReadsNothing) {
  const FakeSpine spine = makeSpine(20);
  const std::vector<std::string> targets = {"item4.xhtml"};
  std::vector<int> out = {4};
  resolve(spine, targets, out, 0);
  EXPECT_EQ(spine.reads, 0);
}

TEST(SpineSearch, AnEmptySpineReadsNothing) {
  const FakeSpine spine = makeSpine(0);
  const std::vector<std::string> targets = {"item4.xhtml"};
  std::vector<int> out(1, -1);
  resolve(spine, targets, out, 0);
  EXPECT_EQ(out[0], -1);
  EXPECT_EQ(spine.reads, 0);
}

TEST(TakeTocSpine, ABasePrefixedTocHrefMatchesTheBareTarget) {
  // TocNavParser stores hrefs base-prefixed; biblebooknav.xhtml yields them bare.
  const std::vector<std::string> targets = {"biblechapternav2.xhtml"};
  std::vector<int> out(1, -1);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 1, "OEBPS/biblechapternav2.xhtml", 81, 100);
  EXPECT_EQ(out[0], 81);
}

TEST(TakeTocSpine, TheFirstMatchingEntryWins) {
  const std::vector<std::string> targets = {"biblechapternav2.xhtml"};
  std::vector<int> out(1, -1);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 1, "OEBPS/biblechapternav2.xhtml", 81, 100);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 1, "OEBPS/biblechapternav2.xhtml", 90, 100);
  EXPECT_EQ(out[0], 81);
}

TEST(TakeTocSpine, ASpineOutsideTheSpineCountIsIgnored) {
  const std::vector<std::string> targets = {"biblechapternav2.xhtml"};
  std::vector<int> out(1, -1);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 1, "OEBPS/biblechapternav2.xhtml", -1, 100);
  EXPECT_EQ(out[0], -1);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 1, "OEBPS/biblechapternav2.xhtml", 100, 100);
  EXPECT_EQ(out[0], -1);
}

TEST(TakeTocSpine, DuplicateTargetsBothTakeTheSpine) {
  const std::vector<std::string> targets = {"biblechapternav2.xhtml", "biblechapternav2.xhtml"};
  std::vector<int> out(2, -1);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 2, "OEBPS/biblechapternav2.xhtml", 81, 100);
  EXPECT_EQ(out, (std::vector<int>{81, 81}));
}

TEST(TakeTocSpine, AnOutlineEntryForAnotherFileDoesNotMatch) {
  const std::vector<std::string> targets = {"biblechapternav2.xhtml"};
  std::vector<int> out(1, -1);
  SpineSearch::takeTocSpine(targets.data(), out.data(), 1, "OEBPS/1001061102.xhtml", 80, 100);
  EXPECT_EQ(out[0], -1);
}
