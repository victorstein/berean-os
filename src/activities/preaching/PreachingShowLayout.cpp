#include "PreachingShowLayout.h"

namespace preaching::show {

int verseCount(const Deck& deck) {
  int verses = 0;
  for (size_t i = 0; i < deck.count; ++i) {
    if (deck.cards[i].kind == CardKind::Verse) ++verses;
  }
  return verses;
}

const CardInput* verseCard(const Deck& deck, const int ordinal) {
  if (ordinal < 0) return nullptr;
  int seen = 0;
  for (size_t i = 0; i < deck.count; ++i) {
    if (deck.cards[i].kind != CardKind::Verse) continue;
    if (seen == ordinal) return &deck.cards[i];
    ++seen;
  }
  return nullptr;
}

bool step(Cursor& cursor, const int pagesInVerse, const int verses, const int direction) {
  if (direction > 0) {
    if (cursor.page + 1 < pagesInVerse) {
      ++cursor.page;
      return true;
    }
    if (cursor.verse + 1 < verses) {
      ++cursor.verse;
      cursor.page = 0;
      return true;
    }
    return false;
  }
  if (cursor.page > 0) {
    --cursor.page;
    return true;
  }
  if (cursor.verse > 0) {
    --cursor.verse;
    cursor.page = 0;
    return true;
  }
  return false;
}

}  // namespace preaching::show
