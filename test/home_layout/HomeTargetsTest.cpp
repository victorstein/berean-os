// Every Home target's route (issue #203, spec A1-A3, A12, A14, A15, A18, A19a):
// the host half of "each lands on the right screen".

#include <gtest/gtest.h>

#include <vector>

#include "activities/launcher/HomeTargets.h"

using HomeTargets::Action;
using HomeTargets::State;
using HomeTargets::Target;
using Kind = ReaderEntryIntent::Kind;

namespace {

State withBible(const bool hasPlace, const uint8_t recentCount, const bool hasPick) {
  return State{true, hasPlace, recentCount, hasPick};
}

std::vector<Target> ringOf(const State& state) {
  Target targets[static_cast<size_t>(Target::COUNT)];
  const size_t count = HomeTargets::ring(state, targets, static_cast<size_t>(Target::COUNT));
  return std::vector<Target>(targets, targets + count);
}

}  // namespace

TEST(HomeTargets, ContinueOpensTheNewestPlaceWithAFastFirstPage) {
  const auto route = HomeTargets::route(Target::Continue, withBible(true, 0, false));
  EXPECT_EQ(route.action, Action::OpenReader);
  EXPECT_EQ(route.intent, Kind::OpenAt);
  EXPECT_TRUE(route.allowFastInitialRefresh);
}

TEST(HomeTargets, ContinueWithNoPlaceOpensTheBibleWhereItWasLeft) {
  const auto route = HomeTargets::route(Target::Continue, withBible(false, 0, false));
  EXPECT_EQ(route.action, Action::OpenReader);
  EXPECT_EQ(route.intent, Kind::None);
  EXPECT_TRUE(route.allowFastInitialRefresh);
}

TEST(HomeTargets, GoToTagsAndSearchOpenTheirIntents) {
  const State state = withBible(true, 3, true);
  EXPECT_EQ(HomeTargets::route(Target::GoTo, state).intent, Kind::BookGrid);
  EXPECT_EQ(HomeTargets::route(Target::Tags, state).intent, Kind::Tags);
  EXPECT_EQ(HomeTargets::route(Target::Search, state).intent, Kind::Search);
  for (const Target target : {Target::GoTo, Target::Tags, Target::Search}) {
    EXPECT_EQ(HomeTargets::route(target, state).action, Action::OpenReader);
    EXPECT_FALSE(HomeTargets::route(target, state).allowFastInitialRefresh);
  }
}

TEST(HomeTargets, RecentRowsAndTheVerseOpenAtTheirPlace) {
  const State state = withBible(true, 3, true);
  for (const Target target : {Target::Recent0, Target::Recent1, Target::Recent2, Target::Verse}) {
    const auto route = HomeTargets::route(target, state);
    EXPECT_EQ(route.action, Action::OpenReader);
    EXPECT_EQ(route.intent, Kind::OpenAt);
    EXPECT_FALSE(route.allowFastInitialRefresh);
  }
}

TEST(HomeTargets, EmptyRecentSlotsAreNotTargets) {
  const State state = withBible(true, 1, false);
  EXPECT_TRUE(HomeTargets::isActive(Target::Recent0, state));
  EXPECT_FALSE(HomeTargets::isActive(Target::Recent1, state));
  EXPECT_FALSE(HomeTargets::isActive(Target::Recent2, state));
}

TEST(HomeTargets, AVerseCardWithoutAPickIsNotATarget) {
  EXPECT_FALSE(HomeTargets::isActive(Target::Verse, withBible(true, 3, false)));
}

TEST(HomeTargets, WithNoBibleTheReaderTargetsDownloadIt) {
  const State state{false, true, 3, true};
  EXPECT_EQ(HomeTargets::route(Target::Continue, state).action, Action::DownloadBible);
  EXPECT_EQ(HomeTargets::route(Target::Tags, state).action, Action::DownloadBible);
  EXPECT_EQ(HomeTargets::route(Target::Search, state).action, Action::DownloadBible);
  EXPECT_FALSE(HomeTargets::isActive(Target::GoTo, state));
  EXPECT_FALSE(HomeTargets::isActive(Target::Recent0, state));
  EXPECT_FALSE(HomeTargets::isActive(Target::Verse, state));
}

TEST(HomeTargets, MeetingsPublicationsAndSettingsNeedNoBible) {
  for (const State& state : {State{false, false, 0, false}, withBible(true, 3, true)}) {
    EXPECT_EQ(HomeTargets::route(Target::Meetings, state).action, Action::OpenMeetings);
    EXPECT_EQ(HomeTargets::route(Target::Publications, state).action, Action::OpenPublications);
    EXPECT_EQ(HomeTargets::route(Target::Settings, state).action, Action::OpenSettings);
  }
}

TEST(HomeTargets, TheRingWalksActiveTargetsInReadingOrder) {
  EXPECT_EQ(ringOf(withBible(true, 3, true)),
            (std::vector<Target>{Target::Continue, Target::GoTo, Target::Recent0, Target::Recent1, Target::Recent2,
                                 Target::Verse, Target::Meetings, Target::Tags, Target::Search, Target::Publications,
                                 Target::Settings}));
  EXPECT_EQ(ringOf(withBible(false, 0, false)),
            (std::vector<Target>{Target::Continue, Target::GoTo, Target::Meetings, Target::Tags, Target::Search,
                                 Target::Publications, Target::Settings}));
  EXPECT_EQ(ringOf(State{false, false, 0, false}),
            (std::vector<Target>{Target::Continue, Target::Meetings, Target::Tags, Target::Search, Target::Publications,
                                 Target::Settings}));
}

TEST(HomeTargets, TheRingStopsAtItsCapacity) {
  Target targets[2];
  EXPECT_EQ(HomeTargets::ring(withBible(true, 3, true), targets, 2), 2u);
  EXPECT_EQ(targets[1], Target::GoTo);
}
