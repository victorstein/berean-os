#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PreachingCardLayout.h"

// Show mode's layout: one Verse card, as large as it fits -- the first of three rungs (the screen's
// Noto Serif 18, 14, 12) at which the quote and its reference fit one page with no word split --
// and paged at the last rung past that. Nothing is cut. Free of Arduino and the renderer -- widths
// come through MeasureFn -- so test/ui_layout runs it on the host (modelled on PreachingCardLayout.h
// and PreachingCardNav.h).
namespace preaching::show {

inline constexpr uint8_t RUNG_COUNT = 3;
inline constexpr uint8_t PAGING_RUNG = RUNG_COUNT - 1;

// A topic's cards, viewed: whatever owns them outlives every use of the Deck.
struct Deck {
  const CardInput* cards = nullptr;
  size_t count = 0;
};

int verseCount(const Deck& deck);
// The Verse card with this ordinal among the deck's Verse cards; nullptr out of range.
const CardInput* verseCard(const Deck& deck, int ordinal);

// Pixel width of `text` at `rung`, set in Face::Quote or Face::Reference.
using MeasureFn = int (*)(const void* ctx, uint8_t rung, Face face, const char* text);

struct Metrics {
  int width = 0;         // the text column, px
  int height = 0;        // the box the quote and its reference share, px
  int referenceGap = 0;  // above the reference, on the page that also holds a quote line
  int lineHeight[RUNG_COUNT] = {};
};

// The quote's lines, then the reference's, numbered as one column from 0.
struct Layout {
  uint8_t rung = 0;
  std::vector<Span> quote;              // into the card's body
  std::vector<Span> reference;          // into the card's reference
  std::vector<uint16_t> pageFirstLine;  // page p holds lines [pageFirstLine[p], pageEnd(p))

  int lineCount() const { return static_cast<int>(quote.size() + reference.size()); }
  int pageCount() const { return static_cast<int>(pageFirstLine.size()); }
  int pageEnd(int page) const;
  bool isReferenceLine(const int line) const { return line >= static_cast<int>(quote.size()); }
  // The page's lines, plus the gap when it holds both quote and reference lines.
  int pageHeight(int page, const Metrics& metrics) const;
};

// Clears and refills `out`'s vectors rather than reassigning `out`, so capacity reserved by the
// caller survives every verse change. Always at least one page.
void layOut(const CardInput& card, const Metrics& metrics, MeasureFn measure, const void* ctx, Layout& out);

struct Cursor {
  int verse = 0;
  int page = 0;
};

// direction > 0: the next page, else the next verse at its first page. direction < 0: the previous
// page, else the previous verse at its first page. False, with `cursor` unchanged, at either end.
bool step(Cursor& cursor, int pagesInVerse, int verses, int direction);

}  // namespace preaching::show
