#pragma once

#include <algorithm>

#include "PreachingCardLayout.h"

// Left/Right and swipes over a laid-out topic. Only Verse cards are selectable; buttons still reach
// every page, so a page of prose is read by stepping through it. Pure, for test/ui_layout.
namespace preaching {

struct CardCursor {
  int page = 0;
  int selected = -1;  // Verse ordinal, -1 for none
};

namespace card_nav_detail {

struct VerseRange {
  int first;
  int end;
};

// The Verse cards that begin on `page`: contiguous, because cards are laid out in order.
inline VerseRange versesBeginningOn(const Layout& layout, const int page) {
  const auto& pages = layout.verseFirstPage;
  const auto first = std::lower_bound(pages.begin(), pages.end(), page);
  const auto end = std::upper_bound(first, pages.end(), page);
  return {static_cast<int>(first - pages.begin()), static_cast<int>(end - pages.begin())};
}

}  // namespace card_nav_detail

// `page`, with the first Verse card that begins on it selected, or none.
inline CardCursor atPage(const Layout& layout, const int page) {
  const auto range = card_nav_detail::versesBeginningOn(layout, page);
  return {page, range.first < range.end ? range.first : -1};
}

// The page where `verse` begins, with it selected; out of range selects the first Verse card.
inline CardCursor selectVerse(const Layout& layout, const int verse) {
  const int verses = static_cast<int>(layout.verseFirstPage.size());
  if (verse >= 0 && verse < verses) return {layout.verseFirstPage[static_cast<size_t>(verse)], verse};
  if (verses > 0) return {layout.verseFirstPage[0], 0};
  return {0, -1};
}

// Right: the next Verse card beginning on this page, else the next page. False at the end.
inline bool nextCard(const Layout& layout, CardCursor& cursor) {
  const auto range = card_nav_detail::versesBeginningOn(layout, cursor.page);
  const bool onPage = cursor.selected >= range.first && cursor.selected < range.end;
  const int candidate = onPage ? cursor.selected + 1 : range.first;
  if (candidate < range.end) {
    cursor.selected = candidate;
    return true;
  }
  if (cursor.page + 1 >= layout.pageCount()) return false;
  cursor = atPage(layout, cursor.page + 1);
  return true;
}

// Left: the previous Verse card beginning on this page, else the previous page with its last
// Verse card selected. False at the start.
inline bool previousCard(const Layout& layout, CardCursor& cursor) {
  const auto range = card_nav_detail::versesBeginningOn(layout, cursor.page);
  const bool onPage = cursor.selected >= range.first && cursor.selected < range.end;
  if (onPage && cursor.selected > range.first) {
    --cursor.selected;
    return true;
  }
  if (cursor.page == 0) return false;
  const int page = cursor.page - 1;
  const auto previous = card_nav_detail::versesBeginningOn(layout, page);
  cursor = {page, previous.first < previous.end ? previous.end - 1 : -1};
  return true;
}

// A vertical swipe: `direction` > 0 for the next page. False past either end.
inline bool turnPage(const Layout& layout, CardCursor& cursor, const int direction) {
  const int page = cursor.page + (direction > 0 ? 1 : -1);
  if (page < 0 || page >= layout.pageCount()) return false;
  cursor = atPage(layout, page);
  return true;
}

}  // namespace preaching
