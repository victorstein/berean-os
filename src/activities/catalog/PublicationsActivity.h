#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Everything on the card, with catalog search as an action inside it.
//
// Buscar could acquire publications and nothing could show them: the launcher's
// tiles reach the Bible, the week's meeting publications and settings, so a book
// downloaded through search had nowhere to be opened from. Continue Reading is
// no help either -- it lists books that have been OPENED, which is the one thing
// a freshly downloaded book has not been.
//
// The library is the noun and searching the catalog is a verb inside it, rather
// than a search screen that lists your books when the query happens to be empty.
class PublicationsActivity final : public UiListActivity {
 public:
  explicit PublicationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  struct Entry {
    std::string path;
    std::string label;
    std::string subtitle;
  };

  // The search row, then one row per publication.
  int listCount() const override { return 1 + static_cast<int>(entries_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void refresh();
  void openSearch();

  std::vector<Entry> entries_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
