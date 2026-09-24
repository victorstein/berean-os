#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Folding and tokenising shared by the index build and the query, so a word
// is reduced identically on both sides: `Señor` in a verse and `senor` typed
// on the keyboard meet as the same term.
namespace BibleSearch {

inline constexpr size_t MIN_TOKEN_BYTES = 2;
// Longer tokens are cut, not dropped, so a very long word still matches a
// query for the same word: both sides are cut at the same byte.
inline constexpr size_t MAX_TOKEN_BYTES = 32;

// Lower-cases ASCII and strips Latin diacritics (Latin-1 Supplement and Latin
// Extended-A: á→a … ñ→n, ç→c, ü→u, ß→ss, æ→ae, œ→oe). Combining marks
// (U+0300..U+036F) are dropped, so decomposed text folds like precomposed
// text. Other scripts pass through unchanged. Malformed UTF-8 becomes a space,
// so the output is always valid UTF-8 and never longer than the input.
void foldAppend(std::string& out, std::string_view utf8);

using TokenSink = void (*)(void* ctx, std::string_view token);

// Splits already-folded text into maximal runs of ASCII letters and digits or
// non-ASCII codepoints that are not punctuation. Tokens under MIN_TOKEN_BYTES
// are dropped; tokens over MAX_TOKEN_BYTES are cut on a UTF-8 boundary.
void tokenize(std::string_view folded, TokenSink sink, void* ctx);

// Fold + tokenize, for the query side.
std::vector<std::string> queryTokens(std::string_view rawQuery);

}  // namespace BibleSearch
