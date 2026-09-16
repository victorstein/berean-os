#include <gtest/gtest.h>

#include "activities/launcher/LauncherRefresh.h"

// The launcher's first paint after a wake has to be non-differential: the panel
// is still showing the sleep screen, and a differential refresh only drives the
// pixels that differ from a baseline that does not contain it. Every later paint
// in the same entry is an ordinary fast refresh.
//
// The second argument is "this entry has already painted", not "this is the
// first paint". That polarity is the one thing a caller can silently invert, and
// the launcher reads it before setting its own latch, so it is pinned here.

TEST(LauncherRefresh, FirstPaintOfAFlaggedEntryNeedsACleanPaint) { EXPECT_TRUE(launcherNeedsCleanPaint(true, false)); }

TEST(LauncherRefresh, LaterPaintsOfAFlaggedEntryDoNot) { EXPECT_FALSE(launcherNeedsCleanPaint(true, true)); }

TEST(LauncherRefresh, AnUnflaggedEntryNeverDoes) {
  EXPECT_FALSE(launcherNeedsCleanPaint(false, false));
  EXPECT_FALSE(launcherNeedsCleanPaint(false, true));
}

TEST(LauncherRefresh, ResolvesAtCompileTime) {
  static_assert(launcherNeedsCleanPaint(true, false));
  static_assert(!launcherNeedsCleanPaint(false, false));
  SUCCEED();
}
