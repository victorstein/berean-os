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

}  // namespace preaching::show
