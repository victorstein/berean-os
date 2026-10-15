#include <gtest/gtest.h>

#include "activities/reader/ReaderBackSwipe.h"

namespace {

using ReaderBackSwipe::Action;
using ReaderBackSwipe::Swipe;

Swipe shortRelease(const int depth, const bool origin) {
  return {.shortBackRelease = true, .returnDepth = depth, .hasOrigin = origin};
}

}  // namespace

static_assert(ReaderBackSwipe::decide({}) == Action::PassOn);
static_assert(ReaderBackSwipe::decide({.shortBackRelease = true, .returnDepth = 1, .hasOrigin = true}) ==
              Action::PopReturn);
static_assert(ReaderBackSwipe::decide({.shortBackRelease = true, .returnDepth = 0, .hasOrigin = true}) ==
              Action::ToPreaching);
static_assert(ReaderBackSwipe::decide({.shortBackRelease = true, .returnDepth = 0, .hasOrigin = false}) ==
              Action::PassOn);

TEST(ReaderBackSwipe, NoShortReleasePassesOnWhateverTheState) {
  for (const int depth : {0, 1, 16}) {
    for (const bool origin : {false, true}) {
      EXPECT_EQ(ReaderBackSwipe::decide({.shortBackRelease = false, .returnDepth = depth, .hasOrigin = origin}),
                Action::PassOn)
          << "depth " << depth << ", origin " << origin;
    }
  }
}

TEST(ReaderBackSwipe, TheReturnStackWinsOverTheOrigin) {
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(1, false)), Action::PopReturn);
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(1, true)), Action::PopReturn);
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(16, true)), Action::PopReturn);
}

TEST(ReaderBackSwipe, AnEmptyStackWithAnOriginGoesToPreaching) {
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(0, true)), Action::ToPreaching);
}

TEST(ReaderBackSwipe, AnEmptyStackWithoutAnOriginPassesOn) {
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(0, false)), Action::PassOn);
}
