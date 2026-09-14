#include "ParagraphAnchors.h"

#include <expat.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "VisibleOffsetCounter.h"
#include "htmlEntities.h"

namespace ParagraphAnchors {
namespace {

struct State {
  VisibleOffsetCounter counter;
  std::vector<ParagraphAnchor> anchors;
};

void XMLCALL onText(void* userData, const XML_Char* text, const int len) {
  static_cast<State*>(userData)->counter.onCharacterData(text, len);
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<State*>(userData);
  self->counter.onStartElement(name);
  if (!self->counter.insideBody) return;

  for (int i = 0; atts && atts[i]; i += 2) {
    // Exact match: data-rel-pid sits beside data-pid on the same elements and
    // is a reference to another paragraph, not this one's address.
    if (strcmp(atts[i], "data-pid") != 0) continue;
    unsigned pid = 0;
    char tail = '\0';
    // The %c catches trailing junk, so "7x" is not a paragraph id.
    if (sscanf(atts[i + 1], "%u%c", &pid, &tail) == 1 && pid <= UINT16_MAX) {
      self->anchors.push_back({self->counter.offset, static_cast<uint16_t>(pid)});
    }
    break;
  }
}

void XMLCALL onEnd(void* userData, const XML_Char* name) { static_cast<State*>(userData)->counter.onEndElement(name); }

// See VerseAnchors.cpp: under XML_GE=0 every undeclared entity arrives here
// rather than at the character handler, and an uncounted `&nbsp;` shifts every
// later offset by one -- enough to name the previous paragraph.
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

}  // namespace

Scanner::Scanner() {
  auto* state = new (std::nothrow) State();
  if (!state) return;
  // Measured over 180 real documents: median 55, p95 111, max 413 (a lff_S page
  // of 321 gen-field boxes plus 66 legends). Sized for the p95 rather than the
  // max -- 448 entries would be 3.5 KB held per scan for a case that occurs
  // twice in 180 documents.
  state->anchors.reserve(112);

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    delete state;
    return;
  }
  XML_SetUserData(parser, state);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  XML_SetDefaultHandlerExpand(parser, onDefault);

  parser_ = parser;
  state_ = state;
}

Scanner::~Scanner() {
  if (parser_) XML_ParserFree(static_cast<XML_Parser>(parser_));
  delete static_cast<State*>(state_);
}

bool Scanner::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!parser_ || failed_) return false;
  const XML_Status status =
      XML_Parse(static_cast<XML_Parser>(parser_), chunk, static_cast<int>(length), isFinal ? 1 : 0);
  if (status == XML_STATUS_ERROR) {
    failed_ = true;
    return false;
  }
  return true;
}

std::vector<ParagraphAnchor> Scanner::take() {
  if (failed_ || !state_) return {};
  return std::move(static_cast<State*>(state_)->anchors);
}

std::vector<ParagraphAnchor> scan(const char* xhtml, const size_t length) {
  Scanner scanner;
  if (!scanner.valid()) return {};
  if (!scanner.feed(xhtml, length, true)) return {};
  return scanner.take();
}

const ParagraphAnchor* find(const std::vector<ParagraphAnchor>& anchors, const uint32_t offset) {
  const ParagraphAnchor* best = nullptr;
  for (const auto& a : anchors) {
    if (a.offset > offset) break;
    best = &a;
  }
  return best;
}

}  // namespace ParagraphAnchors
