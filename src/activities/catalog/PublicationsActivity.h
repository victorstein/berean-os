#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

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

  // Search row, a section header, then one row per publication. The header is
  // never selected or activated, but it occupies a row index like any other.
  static constexpr int SEARCH_ROW = 0;
  static constexpr int HEADER_ROW = 1;
  static constexpr int FIRST_BOOK_ROW = 2;

  int listCount() const override { return FIRST_BOOK_ROW + static_cast<int>(entries_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void render(RenderLock&&) override;
  const char* headerTitle() const override;

  // Row index -> entries_ index, or -1 for the search and header rows. Every
  // conversion goes through this: the rows and the publications stopped being
  // the same numbering the moment a header was inserted between them.
  int entryIndexForRow(int row) const;

  void refresh();
  void openSearch();

  void confirmDelete(size_t entryIndex);
  void deleteEntry(size_t entryIndex);

  std::vector<Entry> entries_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup confirmPopup_;
  bool confirmingDelete_ = false;
  size_t pendingDeleteEntry_ = 0;
};
