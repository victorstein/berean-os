#include "StudyStore/UnitText.h"

#include <expat.h>

#include <cstring>
#include <new>

#include "StudyStore/UnitFingerprint.h"
#include "VisibleOffsetCounter.h"
#include "htmlEntities.h"

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
};

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
    if (self->counter.offset >= self->lo && self->counter.offset < self->hi) self->captured.append(codepoint);

    self->counter.offset++;
    i += width;
  }
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char**) {
  static_cast<State*>(userData)->counter.onStartElement(name);
}

void XMLCALL onEnd(void* userData, const XML_Char* name) { static_cast<State*>(userData)->counter.onEndElement(name); }

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

bool walk(const char* xhtml, const size_t length, State& state) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) return false;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  XML_SetDefaultHandlerExpand(parser, onDefault);

  const XML_Status status = XML_Parse(parser, xhtml, static_cast<int>(length), 1);
  XML_ParserFree(parser);
  return status != XML_STATUS_ERROR;
}

}  // namespace

std::string extractRangeText(const char* xhtml, const size_t length, const uint32_t from, const uint32_t to) {
  if (to <= from) return {};
  State state;
  state.lo = from;
  state.hi = to;
  if (!walk(xhtml, length, state)) return {};
  return std::move(state.captured);
}

std::string extractUnitText(const char* xhtml, const size_t length, const DocumentUnits& units,
                            const UnitAnchor& anchor) {
  if (units.anchors.empty()) return {};

  uint32_t next = UINT32_MAX;
  for (const auto& a : units.anchors) {
    if (a.offset > anchor.offset && a.offset < next) next = a.offset;
  }
  return extractRangeText(xhtml, length, anchor.offset, next);
}

uint32_t documentVisibleCrc(const char* xhtml, const size_t length) {
  State state;  // lo == hi == 0 captures nothing; the CRC covers everything
  if (!walk(xhtml, length, state)) return 0;
  return crc32End(state.crc);
}

}  // namespace study
