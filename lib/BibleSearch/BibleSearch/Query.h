#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "IndexReader.h"

namespace BibleSearch {

// A short prefix can match thousands of terms; the union of their verses stops
// here so a short last word cannot run away.
inline constexpr size_t PREFIX_CAP = 5000;
inline constexpr size_t RESULT_CAP = 1000;

struct QueryResult {
  std::vector<uint16_t> verses;  // global verse numbers, ascending (canonical order)
  bool truncated = false;        // a cap cut the results; more verses may match
};

// Every query word must appear in the verse. Each full word matches a term
// exactly; the LAST word matches every term it prefixes. Lists are
// intersected smallest first. A query with no usable tokens, or one whose
// full word is unknown, matches nothing.
QueryResult runQuery(const IndexReader& reader, std::string_view rawQuery);

}  // namespace BibleSearch
