#include "VerseTextScanner.h"

#include <Logging.h>
#include <Memory.h>
#include <expat.h>

#include <cstdio>
#include <cstring>
#include <utility>

#include "Epub/VerseAnchors.h"
#include "Epub/VisibleOffsetCounter.h"
#include "Epub/htmlEntities.h"

namespace BibleSearch {
namespace {

// Psalm 119, the longest chapter, so a growth never fragments DRAM.
constexpr size_t VERSES_PER_DOCUMENT = 176;

// A link is held back only until it is clearly more than a lone "*" or "+".
constexpr size_t LINK_PROBE_BYTES = 8;

// Classes whose whole subtree is not verse text, all from the NWT markup: the
// navigation line, the chapter number opening verse 1, psalm superscriptions,
// acrostic and section headings, and the editorial note closing Malachi.
constexpr const char* SKIPPED_CLASSES[] = {"w_navigation", "w_ch", "sw", "ss", "sd"};
constexpr const char* SKIPPED_ELEMENTS[] = {"sup", "header", "h1", "h2", "h3", "h4", "h5", "h6"};
constexpr const char* BLOCK_ELEMENTS[] = {"p",  "div", "br", "li", "blockquote", "tr", "td", "dt",
                                          "dd", "h1",  "h2", "h3", "h4",         "h5", "h6"};

bool isOneOf(const char* name, const char* const* list, const size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(name, list[i]) == 0) return true;
  }
  return false;
}

template <size_t N>
bool isOneOf(const char* name, const char* const (&list)[N]) {
  return isOneOf(name, list, N);
}

bool hasToken(const char* list, const char* token) {
  const size_t tokenLength = strlen(token);
  const char* at = list;
  while (*at) {
    while (*at == ' ') at++;
    const char* end = at;
    while (*end && *end != ' ') end++;
    if (static_cast<size_t>(end - at) == tokenLength && strncmp(at, token, tokenLength) == 0) return true;
    at = end;
  }
  return false;
}

const char* attribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Byte length of a Unicode space starting at `at`, or 0. The NWT puts U+202F
// after every verse number and U+00A0 between many words; both collapse like
// ASCII whitespace so the text reads, and tokenizes, the same either way.
size_t unicodeSpaceLength(const char* text, const size_t length, const size_t at) {
  const auto* p = reinterpret_cast<const unsigned char*>(text) + at;
  const size_t left = length - at;
  if (left >= 2 && p[0] == 0xC2 && p[1] == 0xA0) return 2;                                    // U+00A0
  if (left >= 3 && p[0] == 0xE2 && p[1] == 0x80 && (p[2] <= 0x8A || p[2] == 0xAF)) return 3;  // U+2000..200A, U+202F
  if (left >= 3 && p[0] == 0xE2 && p[1] == 0x81 && p[2] == 0x9F) return 3;                    // U+205F
  if (left >= 3 && p[0] == 0xE3 && p[1] == 0x80 && p[2] == 0x80) return 3;                    // U+3000
  return 0;
}

}  // namespace

struct VerseTextScannerState {
  VisibleOffsetCounter visibility;
  uint16_t skipDepth = 0;
  uint16_t linkDepth = 0;
  bool linkProbing = false;
  bool footnotesOpen = false;

  std::string continuation;
  bool continuationCapped = false;

  // Until pairing in take(), anchorOffset holds the verse's marker ordinal.
  VerseText current{};
  bool currentOpen = false;
  bool currentCapped = false;
  std::vector<VerseText> completed;

  uint32_t markerCount = 0;
  std::vector<uint32_t> anchorOffsets;

  bool pendingSpace = false;
  std::string linkProbe;

  std::string* target() {
    if (footnotesOpen) return nullptr;
    return currentOpen ? &current.text : &continuation;
  }
  bool& targetCapped() { return currentOpen ? currentCapped : continuationCapped; }

  void appendText(const char* text, const size_t length) {
    std::string* out = target();
    if (!out) return;
    bool& capped = targetCapped();
    for (size_t i = 0; i < length && !capped; i++) {
      const char c = text[i];
      if (isAsciiSpace(c)) {
        pendingSpace = true;
        continue;
      }
      const size_t spaceLength = unicodeSpaceLength(text, length, i);
      if (spaceLength > 0) {
        pendingSpace = true;
        i += spaceLength - 1;
        continue;
      }
      if (pendingSpace && !out->empty()) appendByte(*out, capped, ' ');
      pendingSpace = false;
      appendByte(*out, capped, c);
    }
  }

  static void appendByte(std::string& out, bool& capped, const char c) {
    if (capped) return;
    if (out.size() < MAX_VERSE_TEXT_BYTES) {
      out.push_back(c);
      return;
    }
    // Full. A continuation byte arriving now means the last codepoint is
    // incomplete, so drop its partial bytes rather than keep a broken tail.
    if ((static_cast<unsigned char>(c) & 0xC0) == 0x80) {
      size_t cut = out.size();
      while (cut > 0 && (static_cast<unsigned char>(out[cut - 1]) & 0xC0) == 0x80) cut--;
      if (cut > 0 && (static_cast<unsigned char>(out[cut - 1]) & 0x80)) cut--;
      out.resize(cut);
    }
    capped = true;
  }

  void closeVerse() {
    if (!currentOpen) return;
    completed.push_back(std::move(current));
    current = VerseText{};
    currentOpen = false;
    currentCapped = false;
    pendingSpace = false;
  }

  void openVerse(const uint16_t chapter, const uint16_t verse) {
    closeVerse();
    current.chapter = chapter;
    current.verse = verse;
    current.anchorOffset = markerCount++;
    currentOpen = true;
    pendingSpace = false;
  }

  void flushLinkProbe() {
    linkProbing = false;
    appendText(linkProbe.data(), linkProbe.size());
    linkProbe.clear();
  }
};

namespace {

using State = VerseTextScannerState;

void onText(State* self, const char* text, const int length) {
  if (!self->visibility.counting() || self->skipDepth > 0 || length <= 0) return;
  if (self->linkProbing) {
    self->linkProbe.append(text, static_cast<size_t>(length));
    if (self->linkProbe.size() > LINK_PROBE_BYTES) self->flushLinkProbe();
    return;
  }
  self->appendText(text, static_cast<size_t>(length));
}

void XMLCALL onCharacterData(void* userData, const XML_Char* text, const int length) {
  onText(static_cast<State*>(userData), text, length);
}

// Mirrors VerseAnchors' default handler: under XML_GE=0 expat reports each
// undeclared entity here, and dropping it would lose a character of text.
void XMLCALL onDefault(void* userData, const XML_Char* s, const int length) {
  if (length >= 3 && s[0] == '&' && s[length - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(length));
    if (value != nullptr) {
      onCharacterData(userData, value, static_cast<int>(strlen(value)));
      return;
    }
    onCharacterData(userData, s, length);
  }
}

// Must match VerseAnchors' onStart exactly -- inside <body>, the first `id`
// attribute only, the same grammar -- or the ordinal pairing in take() drifts.
bool verseMarker(const State* self, const XML_Char** atts, uint16_t& chapter, uint16_t& verse) {
  if (!self->visibility.insideBody) return false;
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], "id") != 0) continue;
    unsigned parsedChapter = 0;
    unsigned parsedVerse = 0;
    char tail = '\0';
    if (sscanf(atts[i + 1], "chapter%u_verse%u%c", &parsedChapter, &parsedVerse, &tail) == 2 &&
        parsedChapter <= UINT16_MAX && parsedVerse <= UINT16_MAX) {
      chapter = static_cast<uint16_t>(parsedChapter);
      verse = static_cast<uint16_t>(parsedVerse);
      return true;
    }
    return false;
  }
  return false;
}

bool opensFootnotes(const XML_Char** atts) {
  const char* cls = attribute(atts, "class");
  if (cls && hasToken(cls, "groupFootnote")) return true;
  const char* type = attribute(atts, "epub:type");
  return type && hasToken(type, "footnote");
}

bool isSkipped(const XML_Char* name, const XML_Char** atts) {
  if (isOneOf(name, SKIPPED_ELEMENTS)) return true;
  if (strcmp(name, "a") == 0) {
    const char* type = attribute(atts, "epub:type");
    if (type && hasToken(type, "noteref")) return true;
  }
  const char* cls = attribute(atts, "class");
  if (!cls) return false;
  for (const char* skipped : SKIPPED_CLASSES) {
    if (hasToken(cls, skipped)) return true;
  }
  return false;
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<State*>(userData);
  self->visibility.onStartElement(name);

  uint16_t chapter = 0;
  uint16_t verse = 0;
  if (verseMarker(self, atts, chapter, verse)) {
    if (self->linkProbing) self->flushLinkProbe();
    self->openVerse(chapter, verse);
  }

  if (!self->footnotesOpen && self->visibility.insideBody && opensFootnotes(atts)) {
    if (self->linkProbing) self->flushLinkProbe();
    self->closeVerse();
    self->footnotesOpen = true;
  }

  if (self->skipDepth > 0) {
    self->skipDepth++;
    return;
  }
  if (isSkipped(name, atts)) {
    // A skipped heading still ends a line, or the words either side would join.
    if (isOneOf(name, BLOCK_ELEMENTS)) self->pendingSpace = true;
    self->skipDepth = 1;
    return;
  }
  if (isOneOf(name, BLOCK_ELEMENTS)) self->pendingSpace = true;
  if (self->linkDepth > 0) {
    self->linkDepth++;
  } else if (strcmp(name, "a") == 0) {
    self->linkDepth = 1;
    self->linkProbing = true;
    self->linkProbe.clear();
  }
}

void XMLCALL onEnd(void* userData, const XML_Char* name) {
  auto* self = static_cast<State*>(userData);
  self->visibility.onEndElement(name);
  if (self->skipDepth > 0) {
    if (--self->skipDepth == 0 && isOneOf(name, BLOCK_ELEMENTS)) self->pendingSpace = true;
    return;
  }
  if (self->linkDepth > 0 && --self->linkDepth == 0 && self->linkProbing) {
    std::string_view probe(self->linkProbe);
    while (!probe.empty() && isAsciiSpace(probe.front())) probe.remove_prefix(1);
    while (!probe.empty() && isAsciiSpace(probe.back())) probe.remove_suffix(1);
    // Footnote and cross-reference markers sometimes arrive as plain links.
    if (probe == "*" || probe == "+") {
      self->linkProbing = false;
      self->linkProbe.clear();
      self->pendingSpace = true;
    } else {
      self->flushLinkProbe();
    }
  }
  if (isOneOf(name, BLOCK_ELEMENTS)) self->pendingSpace = true;
}

}  // namespace

VerseTextScanner::VerseTextScanner(const ParserMemory* memory) {
  auto state = makeUniqueNoThrow<VerseTextScannerState>();
  auto anchors = makeUniqueNoThrow<VerseAnchors::Scanner>();
  if (!state || !anchors || !anchors->valid()) {
    LOG_ERR("BSRCH", "OOM: verse text scanner state");
    return;
  }
  state->completed.reserve(VERSES_PER_DOCUMENT);
  state->anchorOffsets.reserve(VERSES_PER_DOCUMENT);
  state->linkProbe.reserve(LINK_PROBE_BYTES * 2);

  XML_Parser parser = nullptr;
  if (memory) {
    const XML_Memory_Handling_Suite suite{memory->allocate, memory->reallocate, memory->release};
    parser = XML_ParserCreate_MM(nullptr, &suite, nullptr);
  } else {
    parser = XML_ParserCreate(nullptr);
  }
  if (!parser) {
    LOG_ERR("BSRCH", "OOM: verse text scanner parser");
    return;
  }
  XML_SetUserData(parser, state.get());
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onCharacterData);
  XML_SetDefaultHandlerExpand(parser, onDefault);

  parser_ = parser;
  state_ = std::move(state);
  anchors_ = std::move(anchors);
}

VerseTextScanner::~VerseTextScanner() {
  if (parser_) XML_ParserFree(static_cast<XML_Parser>(parser_));
}

bool VerseTextScanner::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!parser_ || failed_) return false;
  const XML_Status status =
      XML_Parse(static_cast<XML_Parser>(parser_), chunk, static_cast<int>(length), isFinal ? 1 : 0);
  if (status == XML_STATUS_ERROR) {
    outOfMemory_ = XML_GetErrorCode(static_cast<XML_Parser>(parser_)) == XML_ERROR_NO_MEMORY;
    failed_ = true;
    return false;
  }
  if (!anchors_->feed(chunk, length, isFinal)) {
    // Both parsers read the same bytes with the same expat, so a syntax error
    // would have stopped the first. When only the anchor parser fails, it ran
    // out of memory.
    outOfMemory_ = true;
    failed_ = true;
    return false;
  }
  for (const auto& anchor : anchors_->take()) state_->anchorOffsets.push_back(anchor.offset);
  if (isFinal) {
    state_->closeVerse();
    if (state_->anchorOffsets.size() != state_->markerCount) {
      failed_ = true;
      return false;
    }
  }
  return true;
}

std::vector<VerseText> VerseTextScanner::take() {
  if (!state_ || failed_) return {};
  std::vector<VerseText> out = std::move(state_->completed);
  state_->completed.clear();
  state_->completed.reserve(VERSES_PER_DOCUMENT);
  for (auto& verse : out) {
    const uint32_t ordinal = verse.anchorOffset;
    if (ordinal >= state_->anchorOffsets.size()) {
      failed_ = true;
      return {};
    }
    verse.anchorOffset = state_->anchorOffsets[ordinal];
  }
  return out;
}

std::string VerseTextScanner::continuation() {
  if (!state_ || failed_) return {};
  return std::move(state_->continuation);
}

}  // namespace BibleSearch
