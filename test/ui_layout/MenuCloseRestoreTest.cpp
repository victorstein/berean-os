#include <gtest/gtest.h>

#include <iterator>
#include <set>
#include <string>

#include "activities/reader/MenuCloseRestore.h"

namespace {

using MenuCloseRestore::Close;
using MenuCloseRestore::Verdict;

// A cancelled close that changed nothing: the one case that restores.
Close quietCancel() {
  Close close;
  close.cancelled = true;
  return close;
}

}  // namespace

TEST(MenuCloseRestoreDecide, QuietCancelRestores) {
  EXPECT_EQ(MenuCloseRestore::decide(quietCancel()), Verdict::Restore);
}

TEST(MenuCloseRestoreDecide, EachChangeAloneBlocksWithItsOwnReason) {
  Close confirmed = quietCancel();
  confirmed.cancelled = false;
  EXPECT_EQ(MenuCloseRestore::decide(confirmed), Verdict::Action);

  Close endOfBook = quietCancel();
  endOfBook.atEndOfBook = true;
  EXPECT_EQ(MenuCloseRestore::decide(endOfBook), Verdict::EndOfBook);

  Close rotated = quietCancel();
  rotated.orientationChanged = true;
  EXPECT_EQ(MenuCloseRestore::decide(rotated), Verdict::Orientation);

  Close autoTurnStopped = quietCancel();
  autoTurnStopped.autoTurnBefore = true;
  EXPECT_EQ(MenuCloseRestore::decide(autoTurnStopped), Verdict::AutoTurn);

  Close autoTurnStarted = quietCancel();
  autoTurnStarted.autoTurnAfter = true;
  EXPECT_EQ(MenuCloseRestore::decide(autoTurnStarted), Verdict::AutoTurn);

  Close nightTurnedOff = quietCancel();
  nightTurnedOff.nightModeBefore = true;
  EXPECT_EQ(MenuCloseRestore::decide(nightTurnedOff), Verdict::NightMode);

  Close nightTurnedOn = quietCancel();
  nightTurnedOn.nightModeAfter = true;
  EXPECT_EQ(MenuCloseRestore::decide(nightTurnedOn), Verdict::NightMode);
}

TEST(MenuCloseRestoreDecide, ConfirmedActionReportsActionWhateverElseChanged) {
  Close everything;
  everything.cancelled = false;
  everything.orientationChanged = true;
  everything.autoTurnBefore = true;
  everything.autoTurnAfter = true;
  everything.nightModeBefore = true;
  everything.nightModeAfter = true;
  everything.atEndOfBook = true;
  EXPECT_EQ(MenuCloseRestore::decide(everything), Verdict::Action);
}

TEST(MenuCloseRestoreDecide, EveryVerdictHasADistinctName) {
  const Verdict all[] = {Verdict::Restore,     Verdict::Action,   Verdict::EndOfBook,
                         Verdict::Orientation, Verdict::AutoTurn, Verdict::NightMode};
  std::set<std::string> names;
  for (const Verdict verdict : all) {
    const char* label = MenuCloseRestore::name(verdict);
    ASSERT_NE(label, nullptr);
    EXPECT_STRNE(label, "");
    names.insert(label);
  }
  EXPECT_EQ(names.size(), std::size(all));
}

namespace {

using MenuCloseRestore::CapturedPage;
using MenuCloseRestore::Fit;
using MenuCloseRestore::Position;

constexpr int SPINE = 12;
constexpr int PAGE = 3;
constexpr Position HERE{true, SPINE, PAGE};

// B/W only, as a page shown without anti-aliasing.
CapturedPage flatPage() {
  CapturedPage page;
  page.beginPage(SPINE, PAGE, false);
  return page;
}

// Every gray step done, as the render that saved it ran them.
CapturedPage grayPage() {
  CapturedPage page;
  page.beginPage(SPINE, PAGE, true);
  page.markPlane(true);
  page.markPlane(false);
  page.markGrayShown();
  return page;
}

}  // namespace

TEST(MenuCloseRestoreFit, NothingSavedIsEmpty) { EXPECT_EQ(MenuCloseRestore::fit(CapturedPage{}, HERE), Fit::Empty); }

TEST(MenuCloseRestoreFit, FlatPageAtTheSamePlaceFitsWithoutGrays) {
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), HERE), Fit::Fits);
  EXPECT_FALSE(MenuCloseRestore::restoresGray(flatPage()));
}

TEST(MenuCloseRestoreFit, NoSectionIsRefused) {
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), Position{false, SPINE, -1}), Fit::NoSection);
}

TEST(MenuCloseRestoreFit, AnotherSpineOrPageHasMoved) {
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), Position{true, SPINE + 1, PAGE}), Fit::Moved);
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), Position{true, SPINE, PAGE + 1}), Fit::Moved);
}

TEST(MenuCloseRestoreFit, GrayPageFitsOnlyWithBothPlanesShown) {
  EXPECT_EQ(MenuCloseRestore::fit(grayPage(), HERE), Fit::Fits);
  EXPECT_TRUE(MenuCloseRestore::restoresGray(grayPage()));

  CapturedPage oneMissing;
  oneMissing.beginPage(SPINE, PAGE, true);
  oneMissing.markPlane(true);
  oneMissing.markGrayShown();
  EXPECT_EQ(MenuCloseRestore::fit(oneMissing, HERE), Fit::GrayMissing);

  CapturedPage notShown;
  notShown.beginPage(SPINE, PAGE, true);
  notShown.markPlane(true);
  notShown.markPlane(false);
  EXPECT_EQ(MenuCloseRestore::fit(notShown, HERE), Fit::GrayMissing);

  CapturedPage shownUnsaved;
  shownUnsaved.beginPage(SPINE, PAGE, true);
  shownUnsaved.markGrayShown();
  EXPECT_EQ(MenuCloseRestore::fit(shownUnsaved, HERE), Fit::GrayMissing);
}

TEST(MenuCloseRestoreFit, ResetForgetsEverything) {
  CapturedPage page = grayPage();
  page.reset();
  EXPECT_EQ(MenuCloseRestore::fit(page, HERE), Fit::Empty);

  page.beginPage(SPINE, PAGE, true);
  page.markGrayShown();
  EXPECT_EQ(MenuCloseRestore::fit(page, HERE), Fit::GrayMissing);
}

TEST(MenuCloseRestoreFit, ANewPageNeverInheritsThePreviousPlanes) {
  CapturedPage page = grayPage();
  page.beginPage(SPINE, PAGE + 1, true);
  page.markGrayShown();
  EXPECT_FALSE(page.grayLsb);
  EXPECT_FALSE(page.grayMsb);
  EXPECT_EQ(MenuCloseRestore::fit(page, Position{true, SPINE, PAGE + 1}), Fit::GrayMissing);
}

TEST(MenuCloseRestoreFit, EveryFitHasADistinctName) {
  const Fit all[] = {Fit::Fits, Fit::Empty, Fit::NoSection, Fit::Moved, Fit::GrayMissing};
  std::set<std::string> names;
  for (const Fit result : all) {
    const char* label = MenuCloseRestore::name(result);
    ASSERT_NE(label, nullptr);
    EXPECT_STRNE(label, "");
    names.insert(label);
  }
  EXPECT_EQ(names.size(), std::size(all));
}
