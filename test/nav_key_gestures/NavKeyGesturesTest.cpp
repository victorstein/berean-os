#include <gtest/gtest.h>

#include "Input/NavKeyGestures.h"

namespace {

using input::NavKey;
using input::NavKeyGestures;

// Drives the machine at a fixed key state, like the firmware loop. 50 ms is the
// tick once the device has been idle three seconds (HalPowerManager.h:33) --
// the common case, not the fast one.
void run(NavKeyGestures& g, const bool left, const bool right, const uint32_t from, const uint32_t until,
         const uint32_t tickMs = 50) {
  for (uint32_t t = from; t <= until; t += tickMs) g.update(left, right, t);
}

TEST(NavKeyGestures, AShortPressSynthesisesNothing) {
  NavKeyGestures g;
  run(g, true, false, 0, 200);
  g.update(false, false, 250);
  EXPECT_FALSE(g.backHeld());
  EXPECT_FALSE(g.confirmHeld());
  EXPECT_FALSE(g.backReleasedThisTick());
}

TEST(NavKeyGestures, AHeldLeftKeyBecomesBack) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.backHeld()) << "Back asserts WHILE held, so the user sees it without lifting";
  EXPECT_FALSE(g.confirmHeld());
}

TEST(NavKeyGestures, AHeldRightKeyBecomesConfirm) {
  NavKeyGestures g;
  run(g, false, true, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.confirmHeld());
  EXPECT_FALSE(g.backHeld());
}

TEST(NavKeyGestures, ReleasingAHeldKeyReportsTheReleaseExactlyOnce) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_TRUE(g.backReleasedThisTick());
  EXPECT_FALSE(g.backHeld());

  g.update(false, false, NavKeyGestures::HOLD_MS + 200);
  EXPECT_FALSE(g.backReleasedThisTick()) << "an edge that repeats is an edge a caller will act on twice";
}

TEST(NavKeyGestures, TheSynthesisedPressEdgeFiresExactlyOnce) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS - 50);
  EXPECT_FALSE(g.backPressedThisTick());
  g.update(true, false, NavKeyGestures::HOLD_MS + 10);
  EXPECT_TRUE(g.backPressedThisTick());
  g.update(true, false, NavKeyGestures::HOLD_MS + 60);
  EXPECT_FALSE(g.backPressedThisTick());
}

// The load-bearing one. ButtonNavigator starts continuous list scrolling at
// 500 ms (ButtonNavigator.h:20) while Back lands at 850 -- so without
// suppression a single hold scrolls the list AND then goes back. The caller
// hides the raw key entirely once it has become a hold.
TEST(NavKeyGestures, AKeyThatBecameAHoldIsSuppressedEntirely) {
  NavKeyGestures g;
  g.update(true, false, 0);
  EXPECT_FALSE(g.suppressRaw(NavKey::Left)) << "not yet a hold; short presses and scrolling must work";
  run(g, true, false, 50, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.suppressRaw(NavKey::Left));
}

TEST(NavKeyGestures, SuppressionClearsOnceTheKeyIsReleased) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_FALSE(g.suppressRaw(NavKey::Left)) << "the next press must page normally";
}

TEST(NavKeyGestures, HoldingOneKeyDoesNotSuppressTheOther) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.suppressRaw(NavKey::Left));
  EXPECT_FALSE(g.suppressRaw(NavKey::Right));
}

TEST(NavKeyGestures, TheTwoKeysAreIndependent) {
  NavKeyGestures g;
  run(g, true, true, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_TRUE(g.backHeld());
  EXPECT_TRUE(g.confirmHeld()) << "holding both is not special; each key means what it means";
}

// millis() wraps after ~49 days. A signed or naive subtraction reads the wrap as
// a huge elapsed time and asserts Back on the very first touch of a key.
TEST(NavKeyGestures, SurvivesAClockWrap) {
  NavKeyGestures g;
  g.update(true, false, UINT32_MAX - 20);
  g.update(true, false, 10);  // wrapped: 30 ms elapsed, nowhere near HOLD_MS
  EXPECT_FALSE(g.backHeld()) << "a wrap must read as a short interval, not as held-forever";
}

TEST(NavKeyGestures, AKeyHeldFromTheVeryFirstTickNeverSynthesises) {
  // Recovery firmware mode holds the right key through startup (main.cpp:387),
  // and boot deliberately absorbs an already-held key (main.cpp:562-569). Such
  // a key must never become a Confirm, and must never be suppressed -- recovery
  // reads isPressed on it.
  NavKeyGestures g;
  g.beginWithKeysDown(false, true, 0);
  run(g, false, true, 50, NavKeyGestures::HOLD_MS * 2);
  EXPECT_FALSE(g.confirmHeld());
  EXPECT_FALSE(g.suppressRaw(NavKey::Right));
}

TEST(NavKeyGestures, AFreshPressAfterAStaleOneWorksNormally) {
  NavKeyGestures g;
  g.beginWithKeysDown(false, true, 0);
  run(g, false, true, 50, NavKeyGestures::HOLD_MS * 2);
  g.update(false, false, NavKeyGestures::HOLD_MS * 2 + 50);
  run(g, false, true, NavKeyGestures::HOLD_MS * 2 + 100, NavKeyGestures::HOLD_MS * 3 + 200);
  EXPECT_TRUE(g.confirmHeld()) << "staleness must clear on release, or the key is dead forever";
}

TEST(NavKeyGestures, HoldThresholdClearsTheReadersOwnHolds) {
  // ReaderUtils.h:18-19 -- SKIP_HOLD_MS 700 and BOOKMARK_HOLD_MS 400 both fire
  // off the raw key, and GO_BACK_OR_HOME_MS is 1000.
  EXPECT_GT(NavKeyGestures::HOLD_MS, 700u);
  EXPECT_LT(NavKeyGestures::HOLD_MS, 1000u);
}

}  // namespace
