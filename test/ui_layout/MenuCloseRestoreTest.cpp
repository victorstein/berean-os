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
