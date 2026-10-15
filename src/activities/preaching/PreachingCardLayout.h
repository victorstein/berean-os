#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Lays out a Predicación topic -- a question's blocks or a tag's passages -- as cards on pages:
// lines measured once, pages broken between cards where they can be, and a card taller than a
// page continued on the next. Nothing is cut. Free of Arduino and the renderer -- widths come
// through MeasureFn -- so test/ui_layout runs it on the host (modelled on StudySleepFit.h).
namespace preaching {

enum class CardKind : uint8_t { Title, Heading, Verse, Text, NextVisit };
enum class Face : uint8_t { Title, Heading, Quote, Body, Reference, Label };
inline constexpr size_t FACE_COUNT = 6;

// A line is copied into a buffer of this size, NUL included, to be drawn.
inline constexpr size_t LINE_BUFFER_BYTES = 128;

// Pixel width of `text` set in `face`.
using MeasureFn = int (*)(const void* ctx, Face face, const char* text);

// `body` and `reference` are views: whatever owns the strings must outlive the Layout.
struct CardInput {
  CardKind kind = CardKind::Text;
  std::string_view body;
  // Verse: the scripture reference. NextVisit: the next question. Empty otherwise.
  std::string_view reference;
};

struct Metrics {
  int width = 0;            // the text column, px
  int pageHeight = 0;       // the body band a page fills, px
  int cardPadding = 0;      // inside a boxed card, above its first line and below its last
  int cardGap = 0;          // between cards on a page
  int referenceIndent = 0;  // room a Verse reference keeps on its left for the "›" mark
  int lineHeight[FACE_COUNT] = {};
};

struct Line {
  uint16_t card = 0;
  uint32_t begin = 0;  // byte offset into the card's body, or its reference when `reference`
  uint8_t length = 0;
  Face face = Face::Body;
  bool reference = false;
  bool firstReference = false;
};

// The part of one card that sits on one page.
struct Segment {
  uint16_t card = 0;
  uint16_t firstLine = 0;
  uint16_t lineCount = 0;
  int16_t y = 0;  // from the top of the page's body band
  int16_t height = 0;
  bool opensCard = false;
  bool closesCard = false;
};

struct Layout {
  std::vector<Line> lines;
  std::vector<Segment> segments;
  std::vector<uint16_t> pageFirstSegment;  // page p holds [pageFirstSegment[p], pageFirstSegment[p + 1])
  std::vector<int16_t> cardVerse;          // per card: its Verse ordinal, -1 for any other kind
  std::vector<uint16_t> verseFirstPage;    // per Verse ordinal: the page where that card begins

  int pageCount() const { return static_cast<int>(pageFirstSegment.size()); }
  size_t segmentsEnd(int page) const {
    return page + 1 < pageCount() ? pageFirstSegment[static_cast<size_t>(page) + 1] : segments.size();
  }
};

constexpr bool isBoxed(const CardKind kind) { return kind == CardKind::Verse || kind == CardKind::NextVisit; }

// False, with `out` cleared, when a count would not fit the layout's index types. An empty input
// is one empty page.
bool layoutCards(const CardInput* cards, size_t count, const Metrics& metrics, MeasureFn measure, const void* ctx,
                 Layout& out);

// The lines of `text` no wider than `width` in `face`, as [begin, begin + length) byte spans,
// appended to `out`. Breaks at U+0020 (and ASCII tab, CR, LF, which the draw path shows as spaces)
// only: U+00A0 and U+202F keep their words together, as GfxRenderer::wrappedText does. A word
// wider than the column, or a run of LINE_BUFFER_BYTES or more, breaks at a codepoint boundary.
struct Span {
  uint32_t begin;
  uint8_t length;
};
void wrapText(std::string_view text, Face face, int width, MeasureFn measure, const void* ctx, std::vector<Span>& out);

}  // namespace preaching
