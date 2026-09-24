#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "IndexReader.h"

namespace BibleSearch {

// A lone prefix word ("co") can match a third of the Bible. Its union is
// counted in full through a bitmap and then cut here; when another word
// narrows the query, the prefix is only a filter and no cap applies before
// the intersection.
inline constexpr size_t PREFIX_CAP = 5000;
inline constexpr size_t RESULT_CAP = 1000;

struct QueryResult {
  std::vector<uint16_t> verses;  // global verse numbers, ascending; never more than RESULT_CAP
  bool truncated = false;        // a cap cut the results; more verses match
  bool ok = true;                // false on an I/O failure or a malformed index, not "no results"
};

// Every query word must appear in the verse. Each full word matches a term
// exactly; the LAST word matches every term it prefixes. The full words are
// intersected smallest first, and the prefix range's postings then keep the
// candidates they contain. A query with no usable tokens, or one whose full
// word is unknown, matches nothing.
//
// Working memory comes from the reader's allocator: the smallest full word's
// list (at most verseCount u16) and a verseCount-bit set. Nothing grows while
// the query runs.
QueryResult runQuery(const IndexReader& reader, std::string_view rawQuery);

}  // namespace BibleSearch
