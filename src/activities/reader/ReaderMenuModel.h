#pragma once

#include <cstdint>

// Values are cast through MenuResult::action (an in-memory int), so new actions
// are appended, never inserted.
enum class ReaderMenuAction {
  SELECT_CHAPTER,
  SEARCH_BIBLE,
  FOOTNOTES,
  TEXT_SETTINGS,
  NIGHT_MODE,
  FRONTLIGHT,
  GO_TO_PERCENT,
  AUTO_PAGE_TURN,
  ROTATE_SCREEN,
  BOOKMARKS,
  TOGGLE_BOOKMARK,
  SCREENSHOT,
  GO_HOME,
  DELETE_CACHE,
  HIGHLIGHT_PASSAGE,
  HIGHLIGHTS,
  TAGS_HERE,
  OPEN_RECENT_PLACE
};

// Which reader-menu items exist, which are quick-action tiles and the order of
// the rows. Free of StrId, HAL and Arduino so the host suite can exercise it
// (test/ui_layout); the activity maps actions to labels.
namespace ReaderMenuModel {

constexpr int MAX_QUICK = 4;
constexpr int MAX_ROWS = 14;
constexpr int MAX_ITEMS = MAX_QUICK + MAX_ROWS;

// Where a tag action leads. Tags are Bible-only: outside the Bible the actions
// open the Bible's tag list, and with no Bible to open they are not offered.
enum class TagTarget : uint8_t { Hidden, ThisBook, BibleTags };

constexpr TagTarget tagTarget(const bool hasHighlights, const bool isBible, const bool bibleReachable) {
  if (!hasHighlights) return TagTarget::Hidden;
  if (isBible) return TagTarget::ThisBook;
  return bibleReachable ? TagTarget::BibleTags : TagTarget::Hidden;
}

struct Inputs {
  bool isBible = false;
  bool hasFootnotes = false;
  bool hasBookmarks = false;
  bool hasHighlights = false;
  // Another book on the card is the Bible. Only read outside a Bible.
  bool bibleReachable = false;
  bool hasFrontlight = false;
  bool hasRotation = false;
  int tagsHereCount = 0;
};

// Flat order: quick actions first, then rows. A flat index is what ACTION_ROW
// carries and what physical-button navigation walks.
struct Model {
  ReaderMenuAction items[MAX_ITEMS]{};
  int quickCount = 0;
  int rowCount = 0;
  bool overflowed = false;

  int count() const { return quickCount + rowCount; }
  ReaderMenuAction row(const int i) const { return items[quickCount + i]; }

  // Quick actions must all precede the rows, or row indices would shift.
  void addQuick(const ReaderMenuAction action) {
    if (rowCount > 0 || quickCount >= MAX_QUICK) {
      overflowed = true;
      return;
    }
    items[quickCount++] = action;
  }

  void addRow(const ReaderMenuAction action) {
    if (rowCount >= MAX_ROWS) {
      overflowed = true;
      return;
    }
    items[quickCount + rowCount++] = action;
  }
};

inline Model build(const Inputs& in) {
  const bool offerTags = tagTarget(in.hasHighlights, in.isBible, in.bibleReachable) != TagTarget::Hidden;
  using A = ReaderMenuAction;
  Model m;
  m.addQuick(A::SELECT_CHAPTER);
  if (in.isBible) m.addQuick(A::SEARCH_BIBLE);
  m.addQuick(A::TOGGLE_BOOKMARK);
  if (offerTags) m.addQuick(A::HIGHLIGHT_PASSAGE);

  if (in.hasBookmarks) m.addRow(A::BOOKMARKS);
  if (in.isBible && in.hasHighlights && in.tagsHereCount > 0) m.addRow(A::TAGS_HERE);
  if (offerTags) m.addRow(A::HIGHLIGHTS);
  if (in.hasFootnotes) m.addRow(A::FOOTNOTES);
  m.addRow(A::TEXT_SETTINGS);
  m.addRow(A::NIGHT_MODE);
  if (in.hasFrontlight) m.addRow(A::FRONTLIGHT);
  if (in.hasRotation) m.addRow(A::ROTATE_SCREEN);
  m.addRow(A::AUTO_PAGE_TURN);
  // A Bible is a reference, not a book to finish: no percentage jump.
  if (!in.isBible) m.addRow(A::GO_TO_PERCENT);
  m.addRow(A::SCREENSHOT);
  m.addRow(A::GO_HOME);
  m.addRow(A::DELETE_CACHE);
  return m;
}

}  // namespace ReaderMenuModel
