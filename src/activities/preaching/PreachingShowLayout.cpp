#include "PreachingShowLayout.h"

#include <string_view>

namespace preaching::show {

namespace {

// wrapText's break bytes; PreachingCardLayout.cpp keeps its own copy file-local.
bool isBreak(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// wrapText ends a line inside a word only when that word is wider than the column.
bool splitsAWord(const std::string_view text, const std::vector<Span>& spans) {
  for (const Span span : spans) {
    const size_t end = static_cast<size_t>(span.begin) + span.length;
    if (end < text.size() && !isBreak(text[end])) return true;
  }
  return false;
}

struct RungMeasure {
  MeasureFn measure;
  const void* ctx;
  uint8_t rung;
};

int measureAtRung(const void* ctx, const Face face, const char* text) {
  const auto* rungMeasure = static_cast<const RungMeasure*>(ctx);
  return rungMeasure->measure(rungMeasure->ctx, rungMeasure->rung, face, text);
}

int linesHeight(const Layout& layout, const Metrics& metrics, const int first, const int end) {
  const int quoteLines = static_cast<int>(layout.quote.size());
  const bool holdsBoth = first < quoteLines && end > quoteLines;
  return (end - first) * metrics.lineHeight[layout.rung] + (holdsBoth ? metrics.referenceGap : 0);
}

// Every page takes at least one line. The quote's last line and the whole reference stay on one
// page whenever that block fits the box, so a reference neither starts a page alone nor splits.
void paginate(Layout& out, const Metrics& metrics) {
  out.pageFirstLine.push_back(0);
  const int lines = out.lineCount();
  const int quoteLines = static_cast<int>(out.quote.size());
  const int tailStart = quoteLines > 0 ? quoteLines - 1 : 0;
  const bool keepTail = !out.reference.empty() && linesHeight(out, metrics, tailStart, lines) <= metrics.height;
  const int pageLimit = keepTail ? tailStart : lines;
  int first = 0;
  while (linesHeight(out, metrics, first, lines) > metrics.height) {
    int end = first + 1;
    while (end < pageLimit && linesHeight(out, metrics, first, end + 1) <= metrics.height) ++end;
    if (end >= lines) return;
    first = end;
    out.pageFirstLine.push_back(static_cast<uint16_t>(first));
  }
}

}  // namespace

int Layout::pageEnd(const int page) const {
  return page + 1 < pageCount() ? pageFirstLine[static_cast<size_t>(page) + 1] : lineCount();
}

int Layout::pageHeight(const int page, const Metrics& metrics) const {
  return linesHeight(*this, metrics, pageFirstLine[static_cast<size_t>(page)], pageEnd(page));
}

void layOut(const CardInput& card, const Metrics& metrics, const MeasureFn measure, const void* const ctx,
            Layout& out) {
  for (uint8_t rung = 0; rung < RUNG_COUNT; ++rung) {
    out.rung = rung;
    out.quote.clear();
    out.reference.clear();
    out.pageFirstLine.clear();
    const RungMeasure rungMeasure{measure, ctx, rung};
    wrapText(card.body, Face::Quote, metrics.width, &measureAtRung, &rungMeasure, out.quote);
    wrapText(card.reference, Face::Reference, metrics.width, &measureAtRung, &rungMeasure, out.reference);
    const bool fitsOnePage = !splitsAWord(card.body, out.quote) && !splitsAWord(card.reference, out.reference) &&
                             linesHeight(out, metrics, 0, out.lineCount()) <= metrics.height;
    if (fitsOnePage || rung == PAGING_RUNG) break;
  }
  paginate(out, metrics);
}

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
