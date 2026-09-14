#include <gtest/gtest.h>

#include "Input/NavKeyGestures.h"

namespace {

using input::NavEvent;
using input::NavKey;
using input::NavKeyGestures;

// 50 ms is the loop tick once the device has been idle three seconds
// (IDLE_POWER_SAVING_MS = 3000, main.cpp's delay(50)) -- the common case.
void run(NavKeyGestures& g, const bool left, const bool right, const uint32_t from, const uint32_t until,
         const uint32_t tickMs = 50) {
  for (uint32_t t = from; t <= until; t += tickMs) g.update(left, right, t);
}

TEST(NavKeyGestures, ATapResolvesToAPageTurnOnRelease) {
  NavKeyGestures g;
  run(g, true, false, 0, 200);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::None) << "nothing may fire while the key is still down";
  g.update(false, false, 250);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::Page);
}

TEST(NavKeyGestures, AHeldLeftKeyResolvesToBackOnRelease) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::None) << "the decision belongs to the release edge";
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::Synth);
}

TEST(NavKeyGestures, AHeldRightKeyResolvesToConfirmOnRelease) {
  NavKeyGestures g;
  run(g, false, true, 0, NavKeyGestures::HOLD_MS + 100);
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_EQ(g.eventFor(NavKey::Right), NavEvent::Synth);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::None);
}

// The whole point of deciding on release: exactly one thing happens per press,
// so a hold can never also have turned a page on the way.
TEST(NavKeyGestures, AHoldNeverAlsoProducesAPage) {
  NavKeyGestures g;
  int pages = 0;
  int synths = 0;
  for (uint32_t t = 0; t <= NavKeyGestures::HOLD_MS + 200; t += 50) {
    g.update(t <= NavKeyGestures::HOLD_MS + 100, false, t);
    if (g.eventFor(NavKey::Left) == NavEvent::Page) pages++;
    if (g.eventFor(NavKey::Left) == NavEvent::Synth) synths++;
  }
  EXPECT_EQ(pages, 0);
  EXPECT_EQ(synths, 1);
}

TEST(NavKeyGestures, AnEventIsReportedForExactlyOneTick) {
  NavKeyGestures g;
  run(g, true, false, 0, 200);
  g.update(false, false, 250);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::Page);
  g.update(false, false, 300);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::None) << "an edge that repeats is acted on twice";
}

// The screenshot combo latches the instant POWER and the right key are both
// down, so the raw held state must survive the first ticks.
TEST(NavKeyGestures, RawStateSurvivesLongEnoughForTheScreenshotCombo) {
  NavKeyGestures g;
  g.update(false, true, 0);
  EXPECT_FALSE(g.suppressState(NavKey::Right));
  g.update(false, true, NavKeyGestures::HOLD_STATE_SUPPRESS_MS - 50);
  EXPECT_FALSE(g.suppressState(NavKey::Right));
}

// ...but is withheld before continuous list scrolling could start at 500 ms,
// which would otherwise scroll the list and then activate a row on the release.
TEST(NavKeyGestures, RawStateIsWithheldBeforeContinuousScrollCouldStart) {
  NavKeyGestures g;
  run(g, false, true, 0, NavKeyGestures::HOLD_STATE_SUPPRESS_MS + 50);
  EXPECT_TRUE(g.suppressState(NavKey::Right));
  EXPECT_LT(NavKeyGestures::HOLD_STATE_SUPPRESS_MS, 500u) << "must precede ButtonNavigator's continuousStartMs";
}

TEST(NavKeyGestures, SuppressionClearsOnRelease) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS);
  EXPECT_TRUE(g.suppressState(NavKey::Left));
  g.update(false, false, NavKeyGestures::HOLD_MS + 50);
  EXPECT_FALSE(g.suppressState(NavKey::Left));
}

TEST(NavKeyGestures, AKeyHeldFromTheVeryFirstTickResolvesToNothingAndIsNeverSuppressed) {
  // Recovery firmware mode holds the right key through startup (main.cpp:387)
  // and reads isPressed on it directly.
  NavKeyGestures g;
  g.beginWithKeysDown(false, true, 0);
  run(g, false, true, 50, NavKeyGestures::HOLD_MS * 2);
  EXPECT_FALSE(g.suppressState(NavKey::Right)) << "recovery reads the raw state and must still see it";
  g.update(false, false, NavKeyGestures::HOLD_MS * 2 + 50);
  EXPECT_EQ(g.eventFor(NavKey::Right), NavEvent::None) << "a key held through boot was never our press";
}

TEST(NavKeyGestures, AFreshPressAfterAStaleOneWorksNormally) {
  NavKeyGestures g;
  g.beginWithKeysDown(false, true, 0);
  run(g, false, true, 50, 400);
  g.update(false, false, 450);
  run(g, false, true, 500, 600);
  g.update(false, false, 650);
  EXPECT_EQ(g.eventFor(NavKey::Right), NavEvent::Page) << "staleness must clear, or the key is dead forever";
}

TEST(NavKeyGestures, TheTwoKeysAreIndependent) {
  NavKeyGestures g;
  run(g, true, true, 0, NavKeyGestures::HOLD_MS + 100);
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::Synth);
  EXPECT_EQ(g.eventFor(NavKey::Right), NavEvent::Synth) << "holding both is not special";
}

TEST(NavKeyGestures, SurvivesAClockWrap) {
  NavKeyGestures g;
  g.update(true, false, UINT32_MAX - 20);
  g.update(false, false, 10);  // wrapped: 30 ms elapsed
  EXPECT_EQ(g.eventFor(NavKey::Left), NavEvent::Page) << "a wrap must read as a short press, not a hold";
}

// Every downstream branch on getHeldTime -- delete prompts at 700, go-home at
// 1000, bookmark at 400 -- must see a synthesised Back or Confirm as an
// ordinary press.
TEST(NavKeyGestures, ASynthesisedEventReportsAShortHeldTime) {
  NavKeyGestures g;
  run(g, true, false, 0, NavKeyGestures::HOLD_MS + 100);
  EXPECT_FALSE(g.reportingSyntheticHeldTime());
  g.update(false, false, NavKeyGestures::HOLD_MS + 150);
  EXPECT_TRUE(g.reportingSyntheticHeldTime());
  EXPECT_LT(NavKeyGestures::SYNTHETIC_HELD_MS, 400u) << "must clear BOOKMARK_HOLD_MS, the lowest branch";
}

TEST(NavKeyGestures, ThresholdsSitBetweenTheReadersOwnHolds) {
  EXPECT_GT(NavKeyGestures::HOLD_MS, 700u) << "above SKIP_HOLD_MS";
  EXPECT_LT(NavKeyGestures::HOLD_MS, 1000u) << "below GO_BACK_OR_HOME_MS";
}

}  // namespace
