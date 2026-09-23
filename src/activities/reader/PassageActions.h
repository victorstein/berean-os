#pragma once

#include <cstdint>

// The actions HighlightsActivity offers on a long-pressed passage, in display
// order. Kept free of the UI so it can be tested on the host: the popup reports
// a row number, and a row dispatched to the wrong action deletes a passage the
// user meant to link.
namespace PassageActions {

enum class Action : uint8_t { EditTags, ShowLinks, LinkToMarked, MarkAsLinkSource, Delete, Cancel };

// Where the pending link source is, relative to the long-pressed passage.
enum class LinkMark : uint8_t { None, ThisPassage, OtherPassage };

struct Menu {
  static constexpr int MAX_ACTIONS = 6;
  Action actions[MAX_ACTIONS]{};
  int count = 0;

  constexpr void add(const Action action) {
    if (count < MAX_ACTIONS) actions[count++] = action;
  }
};

// `canEdit` is false while the store refuses every save: only reading the links
// remains, plus Delete, which reports the refusal itself.
constexpr Menu menuFor(const bool canEdit, const bool hasLinks, const LinkMark mark) {
  Menu menu;
  if (canEdit) menu.add(Action::EditTags);
  if (hasLinks) menu.add(Action::ShowLinks);
  if (canEdit && mark == LinkMark::OtherPassage) menu.add(Action::LinkToMarked);
  if (canEdit && mark != LinkMark::ThisPassage) menu.add(Action::MarkAsLinkSource);
  menu.add(Action::Delete);
  menu.add(Action::Cancel);
  return menu;
}

}  // namespace PassageActions
