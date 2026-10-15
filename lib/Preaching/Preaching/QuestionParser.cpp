#include "Preaching/QuestionParser.h"

#include <Logging.h>
#include <Memory.h>
#include <expat.h>

#include <cstring>
#include <utility>

#include "Epub/htmlEntities.h"

namespace preaching {
namespace {

bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

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

bool hasClass(const XML_Char** atts, const char* token) {
  const char* classes = attribute(atts, "class");
  return classes && hasToken(classes, token);
}

bool isHeading(const XML_Char* name) { return name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0'; }

// The "PREGUNTA N" label, images, and printed page markers, which are empty
// spans carrying their number in an attribute.
bool isSkipped(const XML_Char* name, const XML_Char** atts) {
  if (strcmp(name, "figure") == 0) return true;
  if (strcmp(name, "p") == 0) return hasClass(atts, "contextTtl");
  if (strcmp(name, "span") == 0) return hasClass(atts, "pageNum");
  return false;
}

}  // namespace

struct QuestionParserState {
  enum class Collecting : uint8_t { None, Title, Heading, Paragraph };

  XML_Parser parser = nullptr;
  int skipDepth = 0;
  Collecting collecting = Collecting::None;
  // Depth below the collecting element, so text inside <strong>, <q> or <a> is kept.
  int collectDepth = 0;
  std::string buffer;
  bool pendingSpace = false;
  bool titleSeen = false;

  int linkDepth = 0;
  bool paragraphHasLink = false;
  bool paragraphHasQuote = false;
  bool paragraphHasNonLinkText = false;
  std::string paragraphHref;

  // A quote paragraph waiting for the reference paragraph after it.
  bool hasPendingQuote = false;
  std::string pendingQuote;

  std::string title;
  std::vector<Block> blocks;
  size_t committedBytes = 0;
  bool overLimit = false;

  size_t bytesInUse() const { return committedBytes + (hasPendingQuote ? pendingQuote.size() : 0) + buffer.size(); }

  bool admit(const size_t extraBytes) {
    if (bytesInUse() + extraBytes <= MAX_TEXT_BYTES) return true;
    refuse();
    return false;
  }

  void refuse() {
    overLimit = true;
    XML_StopParser(parser, XML_FALSE);
  }

  void open(const Collecting kind) {
    collecting = kind;
    collectDepth = 0;
    buffer.clear();
    pendingSpace = false;
    linkDepth = 0;
    paragraphHasLink = false;
    paragraphHasQuote = false;
    paragraphHasNonLinkText = false;
    paragraphHref.clear();
  }

  // Collapses ASCII whitespace runs to one space, with none leading or
  // trailing, so the buffer's size while growing is its final size.
  void append(const char* text, const int length) {
    for (int i = 0; i < length; i++) {
      const char c = text[i];
      if (isAsciiSpace(c)) {
        if (!buffer.empty()) pendingSpace = true;
        continue;
      }
      if (collecting == Collecting::Paragraph && linkDepth == 0) paragraphHasNonLinkText = true;
      if (!admit(pendingSpace ? 2 : 1)) return;
      if (pendingSpace) {
        buffer.push_back(' ');
        pendingSpace = false;
      }
      buffer.push_back(c);
    }
  }

  bool commit(const BlockKind kind, std::string text, std::string reference = {}, std::string href = {}) {
    if (blocks.size() >= MAX_BLOCKS) {
      refuse();
      return false;
    }
    committedBytes += text.size() + reference.size() + href.size();
    blocks.push_back(Block{kind, std::move(text), std::move(reference), std::move(href)});
    return true;
  }

  // A quote whose reference never came reads as text where it stood.
  void flushPendingQuote() {
    if (!hasPendingQuote) return;
    hasPendingQuote = false;
    std::string quote = std::move(pendingQuote);
    pendingQuote.clear();
    commit(BlockKind::Text, std::move(quote));
  }

  void closeParagraph() {
    const bool isReference = paragraphHasLink && !paragraphHasNonLinkText;
    if (isReference) {
      if (!admit(paragraphHref.size())) return;
      std::string quote;
      if (hasPendingQuote) {
        hasPendingQuote = false;
        quote = std::move(pendingQuote);
        pendingQuote.clear();
      }
      commit(BlockKind::Verse, std::move(quote), buffer, paragraphHref);
    } else if (paragraphHasQuote) {
      flushPendingQuote();
      pendingQuote = buffer;
      hasPendingQuote = true;
    } else {
      flushPendingQuote();
      commit(BlockKind::Text, buffer);
    }
  }

  void close() {
    const Collecting closing = collecting;
    collecting = Collecting::None;
    // An empty heading or paragraph (the publisher ends most files with one)
    // adds nothing and leaves a quote still waiting for its reference.
    if (buffer.empty()) return;
    if (closing == Collecting::Title) {
      flushPendingQuote();
      committedBytes += buffer.size();
      title = buffer;
    } else if (closing == Collecting::Heading) {
      flushPendingQuote();
      commit(BlockKind::Heading, buffer);
    } else if (closing == Collecting::Paragraph) {
      closeParagraph();
    }
    buffer.clear();
  }

  void clear() {
    title.clear();
    blocks.clear();
    pendingQuote.clear();
    hasPendingQuote = false;
    buffer.clear();
  }
};

namespace {

using Collecting = QuestionParserState::Collecting;

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto& state = *static_cast<QuestionParserState*>(userData);
  if (state.overLimit) return;
  if (state.skipDepth > 0) {
    state.skipDepth++;
    return;
  }
  if (isSkipped(name, atts)) {
    state.skipDepth = 1;
    return;
  }
  if (state.collecting != Collecting::None) {
    state.collectDepth++;
    if (state.collecting != Collecting::Paragraph) return;
    if (strcmp(name, "q") == 0 && hasClass(atts, "scrp")) state.paragraphHasQuote = true;
    if (strcmp(name, "a") == 0) {
      state.linkDepth++;
      const char* href = attribute(atts, "href");
      if (href && *href && !state.paragraphHasLink) {
        state.paragraphHasLink = true;
        state.paragraphHref = href;
      }
    }
    return;
  }
  if (strcmp(name, "h1") == 0 && !state.titleSeen) {
    state.titleSeen = true;
    state.open(Collecting::Title);
  } else if (isHeading(name)) {
    state.open(Collecting::Heading);
  } else if (strcmp(name, "p") == 0) {
    state.open(Collecting::Paragraph);
  }
}

void XMLCALL onEnd(void* userData, const XML_Char* name) {
  auto& state = *static_cast<QuestionParserState*>(userData);
  if (state.overLimit) return;
  if (state.skipDepth > 0) {
    state.skipDepth--;
    return;
  }
  if (state.collecting == Collecting::None) return;
  if (state.collectDepth > 0) {
    state.collectDepth--;
    if (strcmp(name, "a") == 0 && state.linkDepth > 0) state.linkDepth--;
    return;
  }
  state.close();
}

void XMLCALL onText(void* userData, const XML_Char* text, const int length) {
  auto& state = *static_cast<QuestionParserState*>(userData);
  if (state.overLimit || state.skipDepth > 0 || state.collecting == Collecting::None) return;
  state.append(text, length);
}

// Mirrors VerseAnchors' default handler. Under XML_GE=0 expat reports an
// undeclared entity here only once the document declares an external subset;
// without one the entity is a parse error and the question is refused.
void XMLCALL onDefault(void* userData, const XML_Char* s, const int length) {
  if (length >= 3 && s[0] == '&' && s[length - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(length));
    if (value != nullptr) {
      onText(userData, value, static_cast<int>(strlen(value)));
      return;
    }
    onText(userData, s, length);
  }
}

}  // namespace

QuestionParser::QuestionParser() {
  auto state = makeUniqueNoThrow<QuestionParserState>();
  if (!state) {
    LOG_ERR("PREACH", "OOM: question parser state");
    return;
  }
  state->blocks.reserve(MAX_BLOCKS);

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_ERR("PREACH", "OOM: question parser");
    return;
  }
  state->parser = parser;
  XML_SetUserData(parser, state.get());
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  XML_SetDefaultHandlerExpand(parser, onDefault);

  parser_ = parser;
  state_ = std::move(state);
}

QuestionParser::~QuestionParser() {
  if (parser_) XML_ParserFree(static_cast<XML_Parser>(parser_));
}

bool QuestionParser::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!parser_ || failed_ || finished_) return false;
  auto parser = static_cast<XML_Parser>(parser_);
  const XML_Status status = XML_Parse(parser, chunk, static_cast<int>(length), isFinal ? 1 : 0);
  if (status != XML_STATUS_ERROR && isFinal) state_->flushPendingQuote();
  if (state_->overLimit) {
    LOG_ERR("PREACH", "Question refused: over %zu blocks or %zu bytes of text", MAX_BLOCKS, MAX_TEXT_BYTES);
    failed_ = true;
    state_->clear();
    return false;
  }
  if (status == XML_STATUS_ERROR) {
    const XML_Error error = XML_GetErrorCode(parser);
    outOfMemory_ = error == XML_ERROR_NO_MEMORY;
    LOG_ERR("PREACH", "Question parse failed: %s", XML_ErrorString(error));
    failed_ = true;
    state_->clear();
    return false;
  }
  if (isFinal) finished_ = true;
  return true;
}

bool QuestionParser::take(Question& out) {
  if (!state_ || !finished_ || failed_ || taken_) return false;
  out.title = std::move(state_->title);
  out.blocks = std::move(state_->blocks);
  taken_ = true;
  return true;
}

bool parseQuestion(const char* xhtml, const size_t length, Question& out) {
  QuestionParser parser;
  if (!parser.valid()) return false;
  if (!parser.feed(xhtml, length, /*isFinal=*/true)) return false;
  return parser.take(out);
}

}  // namespace preaching
