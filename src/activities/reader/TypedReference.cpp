#include "TypedReference.h"

#include <BibleSearch/Fold.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Fits the longest query the search keyboard accepts and the longest name
// BibleBookNameTable keeps; folding never lengthens its input (Fold.h).
constexpr size_t KEY_BYTES = 64;
// The index stores chapter and verse as u8.
constexpr int MAX_NUMBER = 255;
constexpr int MAX_NUMBER_DIGITS = 3;
// Two-letter function words ("de", "la", "el") each prefix exactly one book.
// Exact two-letter abbreviations ("Is.", "Ps.") still match.
constexpr size_t MIN_PREFIX_BYTES = 3;
// Pasted references often carry an en dash; the keyboard types '-'.
constexpr std::string_view EN_DASH = "\xE2\x80\x93";

bool isDigit(const char c) { return c >= '0' && c <= '9'; }
bool isSpace(const char c) { return c == ' ' || c == '\t'; }

std::string_view trim(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  return text;
}

bool startsWithEnDash(const std::string_view text) { return text.substr(0, EN_DASH.size()) == EN_DASH; }

bool isNumberGroup(std::string_view text) {
  while (!text.empty()) {
    const char c = text.front();
    if (isDigit(c) || isSpace(c) || c == ':' || c == '-') {
      text.remove_prefix(1);
    } else if (startsWithEnDash(text)) {
      text.remove_prefix(EN_DASH.size());
    } else {
      return false;
    }
  }
  return true;
}

// The chapter must follow a space, so "1 Juan" keeps its leading digit and
// "3:16" alone has no book.
size_t numberGroupStart(const std::string_view query) {
  for (size_t i = 1; i < query.size(); i++) {
    if (isDigit(query[i]) && isSpace(query[i - 1]) && isNumberGroup(query.substr(i))) return i;
  }
  return std::string_view::npos;
}

class NumberCursor {
 public:
  explicit NumberCursor(const std::string_view text) : rest(text) {}

  bool number(uint8_t& out) {
    skipSpaces();
    int value = 0;
    int digits = 0;
    while (!rest.empty() && isDigit(rest.front())) {
      if (++digits > MAX_NUMBER_DIGITS) return false;
      value = value * 10 + (rest.front() - '0');
      rest.remove_prefix(1);
    }
    if (digits == 0 || value < 1 || value > MAX_NUMBER) return false;
    out = static_cast<uint8_t>(value);
    return true;
  }

  bool colon() {
    skipSpaces();
    if (rest.empty() || rest.front() != ':') return false;
    rest.remove_prefix(1);
    return true;
  }

  bool dash() {
    skipSpaces();
    if (!rest.empty() && rest.front() == '-') {
      rest.remove_prefix(1);
      return true;
    }
    if (!startsWithEnDash(rest)) return false;
    rest.remove_prefix(EN_DASH.size());
    return true;
  }

  bool atEnd() {
    skipSpaces();
    return rest.empty();
  }

 private:
  void skipSpaces() {
    while (!rest.empty() && isSpace(rest.front())) rest.remove_prefix(1);
  }

  std::string_view rest;
};

bool parseNumbers(const std::string_view group, TypedReference& ref) {
  NumberCursor cursor(group);
  if (!cursor.number(ref.chapter)) return false;
  if (cursor.colon()) {
    if (!cursor.number(ref.verse)) return false;
    if (cursor.dash() && (!cursor.number(ref.verseEnd) || ref.verseEnd < ref.verse)) return false;
  }
  return cursor.atEnd();
}

size_t utf8Width(const unsigned char lead) {
  if (lead < 0x80) return 1;
  if (lead < 0xE0) return 2;
  if (lead < 0xF0) return 3;
  return 4;
}

// Latin-1 punctuation and spaces (U+0080-U+00BF, the no-break space among
// them), General Punctuation (U+2000-U+206F, U+202F among them) and U+FEFF:
// the publisher's markup carries these where a name has a space, and Fold.cpp's
// tokenizer treats them as separators too.
bool isSeparatorSequence(const std::string_view utf8) {
  const auto b0 = static_cast<unsigned char>(utf8[0]);
  const auto b1 = utf8.size() > 1 ? static_cast<unsigned char>(utf8[1]) : 0;
  const auto b2 = utf8.size() > 2 ? static_cast<unsigned char>(utf8[2]) : 0;
  if (b0 == 0xC2) return b1 >= 0x80 && b1 <= 0xBF;
  if (b0 == 0xE2) return b1 == 0x80 || (b1 == 0x81 && b2 <= 0xAF);
  return b0 == 0xEF && b1 == 0xBB && b2 == 0xBF;
}

// Search's fold, then only ASCII letters and digits and non-separator non-ASCII
// codepoints survive, so "1 Crón.", "1 cron" and "1Cron" all become "1cron".
size_t normalise(const std::string_view text, std::string& scratch, char (&out)[KEY_BYTES + 1]) {
  scratch.clear();
  BibleSearch::foldAppend(scratch, text);
  const std::string_view folded = scratch;
  size_t length = 0;
  for (size_t i = 0; i < folded.size();) {
    const auto lead = static_cast<unsigned char>(folded[i]);
    const size_t width = std::min(utf8Width(lead), folded.size() - i);
    const std::string_view codepoint = folded.substr(i, width);
    const bool kept = lead < 0x80 ? (lead >= 'a' && lead <= 'z') || isDigit(folded[i])
                                  : !isSeparatorSequence(codepoint);
    if (kept && length + width <= KEY_BYTES) {
      memcpy(out + length, codepoint.data(), width);
      length += width;
    }
    i += width;
  }
  out[length] = '\0';
  return length;
}

enum class Match : uint8_t { None, Prefix, Exact };

Match compareCandidate(const char* candidate, const char* key, const size_t keyLength, std::string& scratch) {
  if (candidate == nullptr || candidate[0] == '\0') return Match::None;
  char normalised[KEY_BYTES + 1];
  const size_t length = normalise(candidate, scratch, normalised);
  if (length < keyLength || memcmp(normalised, key, keyLength) != 0) return Match::None;
  return length == keyLength ? Match::Exact : Match::Prefix;
}

const char* rowAt(const char* base, const size_t stride, const int index) {
  return base == nullptr ? nullptr : base + static_cast<size_t>(index) * stride;
}

// Canonical book number the key names, or 0 when none or several do.
int matchBook(const char* key, const size_t keyLength, const BookNameSource& books, std::string& scratch) {
  int exactBook = 0;
  int exactBooks = 0;
  int prefixBook = 0;
  int prefixBooks = 0;
  for (int i = 0; i < books.count; i++) {
    const Match byName = compareCandidate(rowAt(books.names, books.nameStride, i), key, keyLength, scratch);
    const Match byAbbreviation =
        compareCandidate(rowAt(books.abbreviations, books.abbreviationStride, i), key, keyLength, scratch);
    if (byName == Match::Exact || byAbbreviation == Match::Exact) {
      exactBook = i + 1;
      exactBooks++;
    } else if (byName == Match::Prefix || byAbbreviation == Match::Prefix) {
      prefixBook = i + 1;
      prefixBooks++;
    }
  }
  if (exactBooks > 0) return exactBooks == 1 ? exactBook : 0;
  if (keyLength < MIN_PREFIX_BYTES || prefixBooks != 1) return 0;
  return prefixBook;
}

uint32_t placeKey(const uint8_t book, const uint8_t chapter, const uint8_t verse) {
  return (static_cast<uint32_t>(book) << 16) | (static_cast<uint32_t>(chapter) << 8) | verse;
}

// The verse table is in canonical order, so a place's first entry is its lower bound.
bool lowerBound(const BibleSearch::IndexReader& reader, const uint32_t key, uint32_t& index) {
  uint32_t low = 0;
  uint32_t high = reader.verseCount();
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2;
    BibleSearch::VerseEntry entry;
    if (!reader.verse(mid, entry)) return false;
    if (placeKey(entry.book, entry.chapter, entry.verse) < key) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  index = low;
  return true;
}

// `verse == 0` finds the chapter's first entry. A lower bound at verseCount is
// checked before reading: verse() reports an index past the end as a failure.
bool findPlace(const BibleSearch::IndexReader& reader, const uint8_t book, const uint8_t chapter,
               const uint8_t verse, BibleSearch::VerseEntry& entry, bool& found) {
  found = false;
  uint32_t index = 0;
  if (!lowerBound(reader, placeKey(book, chapter, verse), index)) return false;
  if (index >= reader.verseCount()) return true;
  if (!reader.verse(index, entry)) return false;
  found = entry.book == book && entry.chapter == chapter && (verse == 0 || entry.verse == verse);
  return true;
}

}  // namespace

TypedReference parseTypedReference(const std::string_view query, const BookNameSource& books) {
  const std::string_view trimmed = trim(query);
  const size_t groupStart = numberGroupStart(trimmed);
  if (groupStart == std::string_view::npos) return {};

  TypedReference ref;
  if (!parseNumbers(trimmed.substr(groupStart), ref)) return {};

  std::string scratch;
  scratch.reserve(KEY_BYTES);
  char key[KEY_BYTES + 1];
  const size_t keyLength = normalise(trimmed.substr(0, groupStart), scratch, key);
  if (keyLength < BibleSearch::MIN_TOKEN_BYTES) return {};

  const int book = matchBook(key, keyLength, books, scratch);
  if (book == 0) return {};
  ref.book = static_cast<uint8_t>(book);
  return ref;
}

void formatTypedReference(char* out, const size_t outBytes, const char* bookName, const TypedReference& ref) {
  if (outBytes == 0) return;
  char numbers[16];
  if (ref.verse == 0) {
    snprintf(numbers, sizeof(numbers), "%u", static_cast<unsigned>(ref.chapter));
  } else if (ref.verseEnd == 0) {
    snprintf(numbers, sizeof(numbers), "%u:%u", static_cast<unsigned>(ref.chapter), static_cast<unsigned>(ref.verse));
  } else {
    snprintf(numbers, sizeof(numbers), "%u:%u-%u", static_cast<unsigned>(ref.chapter),
             static_cast<unsigned>(ref.verse), static_cast<unsigned>(ref.verseEnd));
  }
  if (bookName == nullptr || bookName[0] == '\0') {
    snprintf(out, outBytes, "%s", numbers);
  } else {
    snprintf(out, outBytes, "%s %s", bookName, numbers);
  }
}

bool resolveTypedReference(const BibleSearch::IndexReader& reader, const TypedReference& ref,
                           ResolvedReference& out) {
  out = ResolvedReference{};
  if (!ref.valid()) return true;

  TypedReference place = ref;
  BibleSearch::VerseEntry entry;
  bool found = false;
  if (!findPlace(reader, place.book, place.chapter, place.verse, entry, found)) return false;

  if (!found && place.verse == 0 && place.chapter > 1) {
    BibleSearch::VerseEntry secondChapter;
    bool hasSecondChapter = false;
    if (!findPlace(reader, place.book, 2, 0, secondChapter, hasSecondChapter)) return false;
    if (!hasSecondChapter) {
      if (!findPlace(reader, place.book, 1, place.chapter, entry, found)) return false;
      if (found) {
        place.verse = place.chapter;
        place.chapter = 1;
      }
    }
  }
  if (!found) return true;

  out.found = true;
  out.reference = place;
  out.spine = entry.spine;
  if (place.verse != 0) out.offset = entry.offset;
  return true;
}
