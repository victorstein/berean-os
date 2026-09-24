#include "BibleNavScanner.h"

#include <expat.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace BibleNav {
namespace {

// Psalms, the longest chapter-nav page, is 150 chapters plus the back-link.
constexpr size_t MAX_LINKS_PER_PAGE = 151;

struct State {
  std::vector<std::string> links;
  std::vector<std::string> labels;
  std::vector<BookNavSection> sections;
  // Nesting depth inside the current <a> / <strong>; text is collected while
  // either is positive so child elements like <span> keep their text.
  int linkDepth = 0;
  int headingDepth = 0;
  // Whether the element that opened linkDepth was an <a> we recorded a target
  // for; an <a> without href has no row, so its text must not become one.
  bool linkRecorded = false;
  std::string pendingText;
};

void appendCapped(std::string& out, const char* text, const int length) {
  for (int i = 0; i < length; i++) {
    const char c = text[i];
    const bool whitespace = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (whitespace) {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  if (out.size() <= MAX_TEXT_BYTES) return;
  size_t cut = MAX_TEXT_BYTES;
  // Back off UTF-8 continuation bytes so the cap never splits a character.
  while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
  out.resize(cut);
}

std::string trimmed(std::string text) {
  while (!text.empty() && text.back() == ' ') text.pop_back();
  const size_t first = text.find_first_not_of(' ');
  return first == std::string::npos ? std::string() : text.substr(first);
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<State*>(userData);
  if (self->linkDepth > 0) {
    self->linkDepth++;
    return;
  }
  if (self->headingDepth > 0) {
    self->headingDepth++;
    return;
  }
  if (strcmp(name, "strong") == 0) {
    self->headingDepth = 1;
    self->pendingText.clear();
    return;
  }
  if (strcmp(name, "a") != 0) return;

  self->linkDepth = 1;
  self->linkRecorded = false;
  self->pendingText.clear();
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], "href") != 0) continue;
    std::string_view tail = filenameTail(atts[i + 1]);
    // Chapter targets carry no fragment in these publications, but a stray one
    // would otherwise become part of the filename and match no spine entry.
    const size_t hash = tail.find('#');
    if (hash != std::string_view::npos) tail = tail.substr(0, hash);
    if (!tail.empty() && self->links.size() < MAX_LINKS_PER_PAGE) {
      self->links.emplace_back(tail);
      self->linkRecorded = true;
    }
    break;
  }
}

void XMLCALL onEnd(void* userData, const XML_Char*) {
  auto* self = static_cast<State*>(userData);
  if (self->linkDepth > 0) {
    if (--self->linkDepth == 0 && self->linkRecorded) self->labels.push_back(trimmed(std::move(self->pendingText)));
    return;
  }
  if (self->headingDepth > 0 && --self->headingDepth == 0) {
    self->sections.push_back(
        BookNavSection{trimmed(std::move(self->pendingText)), static_cast<int>(self->links.size())});
  }
}

void XMLCALL onText(void* userData, const XML_Char* text, const int length) {
  auto* self = static_cast<State*>(userData);
  if (self->linkDepth > 0 || self->headingDepth > 0) appendCapped(self->pendingText, text, length);
}

}  // namespace

Scanner::Scanner() {
  auto* state = new (std::nothrow) State();
  if (!state) return;
  state->links.reserve(MAX_LINKS_PER_PAGE);
  state->labels.reserve(MAX_LINKS_PER_PAGE);

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    delete state;
    return;
  }
  XML_SetUserData(parser, state);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);

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

std::vector<std::string> Scanner::take() {
  if (!state_ || failed_) return {};
  return std::move(static_cast<State*>(state_)->links);
}

BookNavPage Scanner::takeBookNav() {
  if (!state_ || failed_) return {};
  auto* state = static_cast<State*>(state_);
  BookNavPage page;
  page.targets = std::move(state->links);
  page.labels = std::move(state->labels);
  page.sections = std::move(state->sections);
  return page;
}

std::vector<std::string> scan(const char* xhtml, const size_t length) {
  Scanner scanner;
  if (!scanner.valid()) return {};
  if (!scanner.feed(xhtml, length, /*isFinal=*/true)) return {};
  return scanner.take();
}

std::string_view filenameTail(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool isChapterNav(const std::string_view filename) { return filename.rfind(CHAPTER_NAV_PREFIX, 0) == 0; }

void dropBookNavLinks(std::vector<std::string>& links) {
  links.erase(std::remove(links.begin(), links.end(), BOOK_NAV_FILENAME), links.end());
}

int findTargetByHref(const std::string* targets, const int count, const std::string_view href) {
  const std::string_view tail = filenameTail(href);
  for (int i = 0; i < count; i++) {
    if (std::string_view(targets[i]) == tail) return i;
  }
  return -1;
}

void joinBookNames(const std::string* targets, const int targetCount, const std::string* tocHrefs,
                   const std::string* tocTitles, const int tocCount, std::string* names) {
  for (int i = 0; i < tocCount; i++) {
    const int match = findTargetByHref(targets, targetCount, tocHrefs[i]);
    if (match >= 0 && names[match].empty()) names[match] = tocTitles[i];
  }
}

}  // namespace BibleNav
