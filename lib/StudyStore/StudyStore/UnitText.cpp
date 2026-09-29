#include "StudyStore/UnitText.h"

#include <expat.h>
#include <strings.h>

#include <cstring>
#include <new>
#include <string_view>

#include "Epub/VisibleOffsetCounter.h"
#include "Epub/htmlEntities.h"
#include "StudyStore/UnitFingerprint.h"

namespace study {
namespace {

// Captures visible codepoints in [lo, hi) and, independently, a CRC over every
// visible codepoint. Both walks share one traversal so they cannot disagree
// with each other or with the offsets VerseAnchors produced.
struct State {
  VisibleOffsetCounter counter;
  uint32_t lo = 0;
  uint32_t hi = 0;
  std::string captured;
  uint32_t crc = crc32Begin();

  bool filtering = false;
  CaptureFilter filter;
  uint16_t skipDepth = 0;
  bool skippingNumber = false;
  bool dropNextSeparator = false;
  bool extensionDone = false;
};

enum class Skip : uint8_t { None, Element, Number };

const char* attribute(const XML_Char** atts, const char* key) {
  if (atts == nullptr) return nullptr;
  for (size_t i = 0; atts[i] != nullptr && atts[i + 1] != nullptr; i += 2) {
    if (strcmp(atts[i], key) == 0) return atts[i + 1];
  }
  return nullptr;
}

bool hasToken(const char* list, const std::string_view token) {
  if (list == nullptr) return false;
  std::string_view rest(list);
  while (!rest.empty()) {
    const size_t space = rest.find(' ');
    if (rest.substr(0, space) == token) return true;
    if (space == std::string_view::npos) break;
    rest.remove_prefix(space + 1);
  }
  return false;
}

Skip skipOf(const char* name, const XML_Char** atts, const bool verseDocument) {
  if (strcasecmp(name, "aside") == 0) return Skip::Element;
  if (strcasecmp(name, "a") == 0 && hasToken(attribute(atts, "epub:type"), "noteref")) return Skip::Element;
  if (!verseDocument) return Skip::None;
  if (strcasecmp(name, "sup") == 0) return Skip::Number;
  if (strcasecmp(name, "span") == 0 && hasToken(attribute(atts, "class"), "w_ch")) return Skip::Number;
  if (strcasecmp(name, "p") == 0) {
    const char* classes = attribute(atts, "class");
    if (hasToken(classes, "ss") || hasToken(classes, "sd")) return Skip::Element;
  }
  return Skip::None;
}

bool isBlockElement(const char* name) {
  static constexpr const char* BLOCKS[] = {"p", "div", "li", "h1", "h2", "h3", "h4", "h5", "h6"};
  for (const char* block : BLOCKS) {
    if (strcasecmp(name, block) == 0) return true;
  }
  return false;
}

// The space a verse number is followed by: U+202F in the NWT's markup, sometimes
// U+00A0 or a plain space.
bool isNumberSeparator(const std::string_view codepoint) {
  return codepoint == " " || codepoint == "\xC2\xA0" || codepoint == "\xE2\x80\xAF";
}

bool isAsciiSpace(const std::string_view codepoint) {
  return codepoint.size() == 1 &&
         (codepoint[0] == ' ' || codepoint[0] == '\n' || codepoint[0] == '\t' || codepoint[0] == '\r');
}

bool shouldCapture(State& state, const std::string_view codepoint) {
  const uint32_t offset = state.counter.offset;
  if (!state.filtering) return offset >= state.lo && offset < state.hi;
  if (state.skipDepth > 0) return false;

  const bool droppedSeparator = state.dropNextSeparator && isNumberSeparator(codepoint);
  state.dropNextSeparator = false;
  if (droppedSeparator) return false;

  if (offset < state.lo) return false;
  if (offset < state.hi) return true;
  if (!state.filter.extendToWordEnd || state.extensionDone) return false;
  if (isAsciiSpace(codepoint)) {
    state.extensionDone = true;
    return false;
  }
  return true;
}

void XMLCALL onText(void* userData, const XML_Char* text, const int len) {
  auto* self = static_cast<State*>(userData);
  if (!self->counter.counting()) return;

  const auto* p = reinterpret_cast<const unsigned char*>(text);
  int i = 0;
  while (i < len) {
    int width = 1;
    while (i + width < len && (p[i + width] & 0xC0) == 0x80) ++width;

    const std::string_view codepoint(text + i, static_cast<size_t>(width));
    self->crc = crc32Update(self->crc, codepoint);
    if (shouldCapture(*self, codepoint)) self->captured.append(codepoint);

    self->counter.offset++;
    i += width;
  }
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<State*>(userData);
  self->counter.onStartElement(name);
  if (!self->filtering) return;
  if (self->skipDepth > 0) {
    ++self->skipDepth;
    return;
  }
  const Skip skip = skipOf(name, atts, self->filter.verseDocument);
  if (skip == Skip::None) return;
  self->skipDepth = 1;
  self->skippingNumber = skip == Skip::Number;
}

void XMLCALL onEnd(void* userData, const XML_Char* name) {
  auto* self = static_cast<State*>(userData);
  self->counter.onEndElement(name);
  if (!self->filtering) return;
  if (self->skipDepth > 0) {
    if (--self->skipDepth == 0 && self->skippingNumber) self->dropNextSeparator = true;
    return;
  }
  if (!isBlockElement(name)) return;
  // A word never runs on past its block.
  if (self->counter.offset >= self->hi) self->extensionDone = true;
  if (!self->captured.empty() && self->captured.back() != ' ') self->captured.push_back(' ');
}

// Mirrors VerseAnchors::onDefault. Under XML_GE=0 every undeclared entity
// arrives here rather than at the character handler; leaving it uncounted would
// shift every later offset and leave the captured text one codepoint short.
void XMLCALL onDefault(void* userData, const XML_Char* s, const int len) {
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (value != nullptr) {
      onText(userData, value, static_cast<int>(strlen(value)));
      return;
    }
    onText(userData, s, len);
  }
}

XML_Parser makeParser(State* state) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) return nullptr;
  XML_SetUserData(parser, state);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  XML_SetDefaultHandlerExpand(parser, onDefault);
  return parser;
}

bool walk(const char* xhtml, const size_t length, State& state) {
  XML_Parser parser = makeParser(&state);
  if (!parser) return false;
  const XML_Status status = XML_Parse(parser, xhtml, static_cast<int>(length), 1);
  XML_ParserFree(parser);
  return status != XML_STATUS_ERROR;
}

}  // namespace

UnitTextScanner::UnitTextScanner() {
  auto* state = new (std::nothrow) State();
  if (!state) return;
  XML_Parser parser = makeParser(state);
  if (!parser) {
    delete state;
    return;
  }
  parser_ = parser;
  state_ = state;
}

UnitTextScanner::~UnitTextScanner() {
  if (parser_) XML_ParserFree(static_cast<XML_Parser>(parser_));
  delete static_cast<State*>(state_);
}

void UnitTextScanner::setRange(const uint32_t from, const uint32_t to) {
  if (!state_) return;
  auto* state = static_cast<State*>(state_);
  state->lo = from;
  state->hi = to;
}

void UnitTextScanner::setFilter(const CaptureFilter& filter) {
  if (!state_) return;
  auto* state = static_cast<State*>(state_);
  state->filtering = true;
  state->filter = filter;
}

bool UnitTextScanner::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!parser_ || failed_) return false;
  const XML_Status status =
      XML_Parse(static_cast<XML_Parser>(parser_), chunk, static_cast<int>(length), isFinal ? 1 : 0);
  if (status == XML_STATUS_ERROR) {
    failed_ = true;
    return false;
  }
  return true;
}

std::string UnitTextScanner::take() {
  if (failed_ || !state_) return {};
  return std::move(static_cast<State*>(state_)->captured);
}

std::string extractRangeText(const char* xhtml, const size_t length, const uint32_t from, const uint32_t to) {
  if (to <= from) return {};
  State state;
  state.lo = from;
  state.hi = to;
  if (!walk(xhtml, length, state)) return {};
  return std::move(state.captured);
}

std::string extractPassageText(const char* xhtml, const size_t length, const uint32_t from, const uint32_t to,
                               const CaptureFilter& filter) {
  if (to <= from) return {};
  State state;
  state.lo = from;
  state.hi = to;
  state.filtering = true;
  state.filter = filter;
  if (!walk(xhtml, length, state)) return {};
  return std::move(state.captured);
}

std::string extractUnitText(const char* xhtml, const size_t length, const DocumentUnits& units,
                            const UnitAnchor& anchor) {
  if (units.anchors.empty()) return {};

  size_t index = SIZE_MAX;
  for (size_t i = 0; i < units.anchors.size(); ++i) {
    if (units.anchors[i].offset == anchor.offset) {
      index = i;
      break;
    }
  }
  if (index == SIZE_MAX) return {};
  return extractRangeText(xhtml, length, anchor.offset, unitEndOffset(units, index));
}

uint32_t documentVisibleCrc(const char* xhtml, const size_t length) {
  State state;  // lo == hi == 0 captures nothing; the CRC covers everything
  if (!walk(xhtml, length, state)) return 0;
  return crc32End(state.crc);
}

}  // namespace study
