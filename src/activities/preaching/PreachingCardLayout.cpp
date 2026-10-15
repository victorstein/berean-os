#include "PreachingCardLayout.h"

namespace preaching {
namespace {

bool isBreak(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

size_t nextCodepoint(const std::string_view text, size_t at) {
  ++at;
  while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) ++at;
  return at;
}

Face bodyFace(const CardKind kind) {
  switch (kind) {
    case CardKind::Title:
      return Face::Title;
    case CardKind::Heading:
      return Face::Heading;
    case CardKind::Verse:
      return Face::Quote;
    case CardKind::Text:
      return Face::Body;
    case CardKind::NextVisit:
      return Face::Label;
  }
  return Face::Body;
}

Face referenceFace(const CardKind kind) { return kind == CardKind::Verse ? Face::Reference : Face::Body; }

bool isHeader(const CardKind kind) { return kind == CardKind::Title || kind == CardKind::Heading; }

struct CardLines {
  size_t first = 0;
  size_t count = 0;
};

class Paginator {
 public:
  Paginator(const CardInput* cards, const std::vector<CardLines>& cardLines, const Metrics& metrics, Layout& out)
      : cards(cards), cardLines(cardLines), metrics(metrics), out(out) {
    newPage();
  }

  void run(const size_t count) {
    size_t first = 0;
    while (first < count) {
      // A title or heading travels with the card after it, so it never ends a page.
      size_t last = first;
      while (last + 1 < count && isHeader(cards[last].kind)) ++last;

      int groupHeight = 0;
      for (size_t card = first; card <= last; ++card) {
        groupHeight += cardHeight(card) + (card > first ? metrics.cardGap : 0);
      }

      if (groupHeight <= metrics.pageHeight - y - gapBefore()) {
        for (size_t card = first; card <= last; ++card) placeWhole(card);
      } else if (groupHeight <= metrics.pageHeight) {
        newPage();
        for (size_t card = first; card <= last; ++card) placeWhole(card);
      } else {
        // Taller than a page. A page holding only titles and headings counts as empty here, so
        // they share their page with the tall card's first lines.
        if (!pageOnlyHeaders) newPage();
        for (size_t card = first; card < last; ++card) placeWhole(card);
        placeSplit(last);
      }
      first = last + 1;
    }
  }

 private:
  const CardInput* cards;
  const std::vector<CardLines>& cardLines;
  const Metrics& metrics;
  Layout& out;
  int y = 0;
  bool pageEmpty = true;
  bool pageOnlyHeaders = true;

  void newPage() {
    out.pageFirstSegment.push_back(static_cast<uint16_t>(out.segments.size()));
    y = 0;
    pageEmpty = true;
    pageOnlyHeaders = true;
  }

  int gapBefore() const { return pageEmpty ? 0 : metrics.cardGap; }

  int lineHeight(const size_t line) const { return metrics.lineHeight[static_cast<size_t>(out.lines[line].face)]; }

  int padding(const size_t card) const { return isBoxed(cards[card].kind) ? metrics.cardPadding : 0; }

  int cardHeight(const size_t card) const {
    int height = 2 * padding(card);
    const CardLines& lines = cardLines[card];
    for (size_t line = lines.first; line < lines.first + lines.count; ++line) height += lineHeight(line);
    return height;
  }

  void push(const size_t card, const size_t firstLine, const size_t lineCount, const int top, const int height,
            const bool opens, const bool closes) {
    Segment segment;
    segment.card = static_cast<uint16_t>(card);
    segment.firstLine = static_cast<uint16_t>(firstLine);
    segment.lineCount = static_cast<uint16_t>(lineCount);
    segment.y = static_cast<int16_t>(top);
    segment.height = static_cast<int16_t>(height);
    segment.opensCard = opens;
    segment.closesCard = closes;
    out.segments.push_back(segment);
    y = top + height;
    pageEmpty = false;
    pageOnlyHeaders = pageOnlyHeaders && isHeader(cards[card].kind);
  }

  void placeWhole(const size_t card) {
    const int height = cardHeight(card);
    if (!pageEmpty && height > metrics.pageHeight - y - gapBefore()) newPage();
    const CardLines& lines = cardLines[card];
    push(card, lines.first, lines.count, y + gapBefore(), height, true, true);
  }

  void placeSplit(const size_t card) {
    const CardLines& lines = cardLines[card];
    if (lines.count == 0) {
      placeWhole(card);
      return;
    }
    const int pad = padding(card);
    const size_t end = lines.first + lines.count;
    size_t line = lines.first;
    bool opens = true;
    while (true) {
      const int top = y + gapBefore();
      const int room = metrics.pageHeight - top;
      int used = opens ? pad : 0;
      size_t take = line;
      while (take < end) {
        const int closingPad = take + 1 == end ? pad : 0;
        if (used + lineHeight(take) + closingPad > room) break;
        used += lineHeight(take);
        ++take;
      }
      if (take == line) {
        if (!pageEmpty) {
          newPage();
          continue;
        }
        // Not even one line fits an empty page: place it anyway so the layout always advances.
        used += lineHeight(take);
        ++take;
      }
      // A Verse's reference never starts a page alone: its last quote line goes with it.
      if (take < end && take > line + 1 && out.lines[take].firstReference) {
        --take;
        used -= lineHeight(take);
      }
      const bool closes = take == end;
      if (closes) used += pad;
      push(card, line, take - line, top, used, opens, closes);
      if (closes) return;
      line = take;
      opens = false;
      newPage();
    }
  }
};

}  // namespace

void wrapText(const std::string_view text, const Face face, const int width, const MeasureFn measure,
              const void* const ctx, std::vector<Span>& out) {
  char scratch[LINE_BUFFER_BYTES];
  auto fits = [&](const size_t begin, const size_t end) {
    const size_t length = end - begin;
    if (length >= LINE_BUFFER_BYTES) return false;
    // PreachingTopicActivity::drawLine draws every byte below 0x20 as a space, so measure it that way.
    for (size_t i = 0; i < length; ++i) {
      const char c = text[begin + i];
      scratch[i] = static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    }
    scratch[length] = '\0';
    return measure(ctx, face, scratch) <= width;
  };
  auto wordEndFrom = [&](size_t at) {
    while (at < text.size() && !isBreak(text[at])) ++at;
    return at;
  };
  auto skipBreaks = [&](size_t at) {
    while (at < text.size() && isBreak(text[at])) ++at;
    return at;
  };

  size_t pos = skipBreaks(0);
  while (pos < text.size()) {
    size_t lineEnd = pos;
    size_t cursor = pos;
    while (cursor < text.size()) {
      const size_t wordEnd = wordEndFrom(cursor);
      if (!fits(pos, wordEnd)) break;
      lineEnd = wordEnd;
      cursor = skipBreaks(wordEnd);
    }
    if (lineEnd == pos) {
      // The word does not fit alone: break it at the last codepoint that fits, taking at least one.
      const size_t wordEnd = wordEndFrom(pos);
      size_t cut = nextCodepoint(text, pos);
      while (cut < wordEnd) {
        const size_t next = nextCodepoint(text, cut);
        if (!fits(pos, next)) break;
        cut = next;
      }
      lineEnd = cut;
      cursor = cut;
    }
    out.push_back({static_cast<uint32_t>(pos), static_cast<uint8_t>(lineEnd - pos)});
    pos = cursor;
  }
}

bool layoutCards(const CardInput* const cards, const size_t count, const Metrics& metrics, const MeasureFn measure,
                 const void* const ctx, Layout& out) {
  out = Layout{};
  if (count > UINT16_MAX) return false;

  std::vector<CardLines> cardLines;
  cardLines.reserve(count);
  out.cardVerse.reserve(count);
  out.lines.reserve(count * 4);
  std::vector<Span> spans;
  spans.reserve(16);
  int16_t verses = 0;

  for (size_t card = 0; card < count; ++card) {
    const CardInput& input = cards[card];
    CardLines lines;
    lines.first = out.lines.size();

    spans.clear();
    const Face body = bodyFace(input.kind);
    wrapText(input.body, body, metrics.width, measure, ctx, spans);
    for (const Span span : spans) {
      out.lines.push_back({static_cast<uint16_t>(card), span.begin, span.length, body, false, false});
    }

    if (!input.reference.empty()) {
      spans.clear();
      const Face reference = referenceFace(input.kind);
      const int width = input.kind == CardKind::Verse ? metrics.width - metrics.referenceIndent : metrics.width;
      wrapText(input.reference, reference, width, measure, ctx, spans);
      for (size_t i = 0; i < spans.size(); ++i) {
        out.lines.push_back({static_cast<uint16_t>(card), spans[i].begin, spans[i].length, reference, true, i == 0});
      }
    }

    lines.count = out.lines.size() - lines.first;
    cardLines.push_back(lines);
    out.cardVerse.push_back(input.kind == CardKind::Verse ? verses++ : static_cast<int16_t>(-1));
    if (out.lines.size() > UINT16_MAX || verses < 0) {
      out = Layout{};
      return false;
    }
  }

  out.segments.reserve(count + 8);
  out.pageFirstSegment.reserve(count / 4 + 2);
  Paginator(cards, cardLines, metrics, out).run(count);

  out.verseFirstPage.assign(static_cast<size_t>(verses), 0);
  for (int page = 0; page < out.pageCount(); ++page) {
    for (size_t s = out.pageFirstSegment[static_cast<size_t>(page)]; s < out.segmentsEnd(page); ++s) {
      const Segment& segment = out.segments[s];
      const int16_t verse = out.cardVerse[segment.card];
      if (segment.opensCard && verse >= 0) out.verseFirstPage[static_cast<size_t>(verse)] = static_cast<uint16_t>(page);
    }
  }
  return true;
}

}  // namespace preaching
