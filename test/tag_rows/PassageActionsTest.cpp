#include <gtest/gtest.h>

#include <vector>

#include "activities/reader/PassageActions.h"

using PassageActions::Action;
using PassageActions::LinkMark;

namespace {

std::vector<Action> actionsOf(const PassageActions::Menu& menu) {
  return std::vector<Action>(menu.actions, menu.actions + menu.count);
}

}  // namespace

TEST(PassageActions, AnUnlinkedPassageWithNothingMarkedOffersToBeMarked) {
  EXPECT_EQ(actionsOf(PassageActions::menuFor(true, false, LinkMark::None)),
            (std::vector<Action>{Action::EditTags, Action::MarkAsLinkSource, Action::Delete, Action::Cancel}));
}

TEST(PassageActions, AnotherPassageMarkedOffersToLinkToIt) {
  EXPECT_EQ(actionsOf(PassageActions::menuFor(true, false, LinkMark::OtherPassage)),
            (std::vector<Action>{Action::EditTags, Action::LinkToMarked, Action::MarkAsLinkSource, Action::Delete,
                                 Action::Cancel}));
}

TEST(PassageActions, TheMarkedPassageIsNotOfferedToLinkToItself) {
  const auto actions = actionsOf(PassageActions::menuFor(true, true, LinkMark::ThisPassage));
  EXPECT_EQ(actions, (std::vector<Action>{Action::EditTags, Action::ShowLinks, Action::Delete, Action::Cancel}));
}

TEST(PassageActions, EveryActionFitsAtOnce) {
  const auto menu = PassageActions::menuFor(true, true, LinkMark::OtherPassage);
  EXPECT_EQ(menu.count, PassageActions::Menu::MAX_ACTIONS) << "no action may be dropped off the end";
  EXPECT_EQ(menu.actions[menu.count - 1], Action::Cancel);
}

TEST(PassageActions, ASaveRefusingStoreStillLetsLinksBeFollowed) {
  EXPECT_EQ(actionsOf(PassageActions::menuFor(false, true, LinkMark::OtherPassage)),
            (std::vector<Action>{Action::ShowLinks, Action::Delete, Action::Cancel}));
  EXPECT_EQ(actionsOf(PassageActions::menuFor(false, false, LinkMark::None)),
            (std::vector<Action>{Action::Delete, Action::Cancel}))
      << "matches the two-row chooser the refusing store showed before links existed";
}
