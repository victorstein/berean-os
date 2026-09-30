#include <gtest/gtest.h>

#include "components/StatusBarProgress.h"

namespace {

StatusBarProgress::Inputs inputs(const bool showWholeBookProgress, const bool showBookPercent,
                                 const bool barTracksBook) {
  StatusBarProgress::Inputs in;
  in.showWholeBookProgress = showWholeBookProgress;
  in.showBookPercent = showBookPercent;
  in.barTracksBook = barTracksBook;
  return in;
}

}  // namespace

TEST(StatusBarProgress, NonBibleKeepsEverySettingCombination) {
  for (const bool percent : {false, true}) {
    for (const bool barTracksBook : {false, true}) {
      const auto view = StatusBarProgress::resolve(inputs(true, percent, barTracksBook));
      EXPECT_EQ(view.showBookPercent, percent) << "percent=" << percent << " bar=" << barTracksBook;
      EXPECT_EQ(view.barTracksBook, barTracksBook) << "percent=" << percent << " bar=" << barTracksBook;
    }
  }
}

TEST(StatusBarProgress, BibleNeverShowsWholeBookProgress) {
  for (const bool percent : {false, true}) {
    for (const bool barTracksBook : {false, true}) {
      const auto view = StatusBarProgress::resolve(inputs(false, percent, barTracksBook));
      EXPECT_FALSE(view.showBookPercent) << "percent=" << percent << " bar=" << barTracksBook;
      EXPECT_FALSE(view.barTracksBook) << "percent=" << percent << " bar=" << barTracksBook;
    }
  }
}

static_assert(
    !StatusBarProgress::resolve({.showWholeBookProgress = false, .showBookPercent = true, .barTracksBook = true})
         .showBookPercent);
