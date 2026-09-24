#include "Fold.h"

#include <cstdint>

namespace BibleSearch {
namespace {

constexpr uint32_t INVALID = 0xFFFFFFFF;

struct Decoded {
  uint32_t codepoint;  // INVALID for a malformed or truncated sequence
  size_t length;       // bytes consumed; 1 for a malformed sequence so the caller resyncs
};

// Bounds-checked, unlike utf8NextCodepoint, which reads until it sees a NUL: a
// string_view carries none.
Decoded decode(const std::string_view s, const size_t at) {
  const auto lead = static_cast<unsigned char>(s[at]);
  if (lead < 0x80) return {lead, 1};
  size_t length;
  uint32_t cp;
  uint32_t minimum;
  if ((lead & 0xE0) == 0xC0) {
    length = 2;
    cp = lead & 0x1F;
    minimum = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    length = 3;
    cp = lead & 0x0F;
    minimum = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    length = 4;
    cp = lead & 0x07;
    minimum = 0x10000;
  } else {
    return {INVALID, 1};
  }
  if (at + length > s.size()) return {INVALID, 1};
  for (size_t i = 1; i < length; i++) {
    const auto next = static_cast<unsigned char>(s[at + i]);
    if ((next & 0xC0) != 0x80) return {INVALID, 1};
    cp = (cp << 6) | (next & 0x3F);
  }
  if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {INVALID, 1};
  return {cp, length};
}

constexpr uint32_t LATIN_FIRST = 0x00C0;
constexpr uint32_t LATIN_LAST = 0x017F;

// Up to two ASCII bytes per codepoint from U+00C0 to U+017F; an empty entry
// passes the codepoint through (× and ÷, which the tokenizer treats as
// punctuation).
constexpr char LATIN_FOLD[LATIN_LAST - LATIN_FIRST + 1][3] = {
    "a", "a", "a",  "a",  "a", "a", "ae", "c",   // U+00C0 ÀÁÂÃÄÅÆÇ
    "e", "e", "e",  "e",  "i", "i", "i",  "i",   // U+00C8 ÈÉÊËÌÍÎÏ
    "d", "n", "o",  "o",  "o", "o", "o",  "",    // U+00D0 ÐÑÒÓÔÕÖ×
    "o", "u", "u",  "u",  "u", "y", "th", "ss",  // U+00D8 ØÙÚÛÜÝÞß
    "a", "a", "a",  "a",  "a", "a", "ae", "c",   // U+00E0 àáâãäåæç
    "e", "e", "e",  "e",  "i", "i", "i",  "i",   // U+00E8 èéêëìíîï
    "d", "n", "o",  "o",  "o", "o", "o",  "",    // U+00F0 ðñòóôõö÷
    "o", "u", "u",  "u",  "u", "y", "th", "y",   // U+00F8 øùúûüýþÿ
    "a", "a", "a",  "a",  "a", "a", "c",  "c",   // U+0100 ĀāĂăĄąĆć
    "c", "c", "c",  "c",  "c", "c", "d",  "d",   // U+0108 ĈĉĊċČčĎď
    "d", "d", "e",  "e",  "e", "e", "e",  "e",   // U+0110 ĐđĒēĔĕĖė
    "e", "e", "e",  "e",  "g", "g", "g",  "g",   // U+0118 ĘęĚěĜĝĞğ
    "g", "g", "g",  "g",  "h", "h", "h",  "h",   // U+0120 ĠġĢģĤĥĦħ
    "i", "i", "i",  "i",  "i", "i", "i",  "i",   // U+0128 ĨĩĪīĬĭĮį
    "i", "i", "ij", "ij", "j", "j", "k",  "k",   // U+0130 İıĲĳĴĵĶķ
    "k", "l", "l",  "l",  "l", "l", "l",  "l",   // U+0138 ĸĹĺĻļĽľĿ
    "l", "l", "l",  "n",  "n", "n", "n",  "n",   // U+0140 ŀŁłŃńŅņŇ
    "n", "n", "n",  "n",  "o", "o", "o",  "o",   // U+0148 ňŉŊŋŌōŎŏ
    "o", "o", "oe", "oe", "r", "r", "r",  "r",   // U+0150 ŐőŒœŔŕŖŗ
    "r", "r", "s",  "s",  "s", "s", "s",  "s",   // U+0158 ŘřŚśŜŝŞş
    "s", "s", "t",  "t",  "t", "t", "t",  "t",   // U+0160 ŠšŢţŤťŦŧ
    "u", "u", "u",  "u",  "u", "u", "u",  "u",   // U+0168 ŨũŪūŬŭŮů
    "u", "u", "u",  "u",  "w", "w", "y",  "y",   // U+0170 ŰűŲųŴŵŶŷ
    "y", "z", "z",  "z",  "z", "z", "z",  "s",   // U+0178 ŸŹźŻżŽžſ
};
static_assert(sizeof(LATIN_FOLD) / sizeof(LATIN_FOLD[0]) == LATIN_LAST - LATIN_FIRST + 1);

constexpr bool isCombiningMark(const uint32_t cp) { return cp >= 0x0300 && cp <= 0x036F; }

constexpr bool isAsciiAlnum(const uint32_t cp) {
  return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
}

// Non-ASCII codepoints are letters unless they sit in a punctuation or space
// block: Latin-1's symbols (¡ ¿ « » NBSP), × and ÷, General Punctuation (“ ” ‘ ’
// — … and U+202F), Supplemental Punctuation, CJK punctuation, and the BOM.
constexpr bool isTokenCodepoint(const uint32_t cp) {
  if (cp < 0x80) return isAsciiAlnum(cp);
  if (cp <= 0xBF || cp == 0xD7 || cp == 0xF7) return false;
  if (cp >= 0x2000 && cp <= 0x206F) return false;
  if (cp >= 0x2E00 && cp <= 0x2E7F) return false;
  if (cp >= 0x3000 && cp <= 0x3003) return false;
  return cp != 0xFEFF;
}

void appendRaw(std::string& out, const std::string_view s, const size_t at, const size_t length) {
  out.append(s.data() + at, length);
}

}  // namespace

void foldAppend(std::string& out, const std::string_view utf8) {
  out.reserve(out.size() + utf8.size());
  size_t at = 0;
  while (at < utf8.size()) {
    const Decoded d = decode(utf8, at);
    if (d.codepoint == INVALID) {
      out.push_back(' ');
    } else if (d.codepoint < 0x80) {
      const char c = static_cast<char>(d.codepoint);
      out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    } else if (d.codepoint >= LATIN_FIRST && d.codepoint <= LATIN_LAST && LATIN_FOLD[d.codepoint - LATIN_FIRST][0]) {
      out.append(LATIN_FOLD[d.codepoint - LATIN_FIRST]);
    } else if (!isCombiningMark(d.codepoint)) {
      appendRaw(out, utf8, at, d.length);
    }
    at += d.length;
  }
}

void tokenize(const std::string_view folded, const TokenSink sink, void* ctx) {
  size_t start = 0;
  size_t end = 0;  // end of the part of the current token that fits MAX_TOKEN_BYTES
  bool inToken = false;
  const auto flush = [&]() {
    if (inToken && end - start >= MIN_TOKEN_BYTES) sink(ctx, folded.substr(start, end - start));
    inToken = false;
  };

  size_t at = 0;
  while (at < folded.size()) {
    const Decoded d = decode(folded, at);
    if (d.codepoint != INVALID && isTokenCodepoint(d.codepoint)) {
      if (!inToken) {
        inToken = true;
        start = at;
        end = at;
      }
      if (end == at && at + d.length - start <= MAX_TOKEN_BYTES) end = at + d.length;
    } else {
      flush();
    }
    at += d.length;
  }
  flush();
}

std::vector<std::string> queryTokens(const std::string_view rawQuery) {
  std::string folded;
  foldAppend(folded, rawQuery);
  std::vector<std::string> out;
  out.reserve(8);
  tokenize(
      folded,
      [](void* ctx, const std::string_view token) { static_cast<std::vector<std::string>*>(ctx)->emplace_back(token); },
      &out);
  return out;
}

}  // namespace BibleSearch
