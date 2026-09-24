#include "Query.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include "Fold.h"

namespace BibleSearch {
namespace {

// The union of every term the last word prefixes, keeping its lowest
// PREFIX_CAP verses. Each term's list is ascending, so its first PREFIX_CAP
// entries are all that can reach the kept part of the union.
bool prefixUnion(const IndexReader& reader, const std::string_view prefix, std::vector<uint16_t>& out,
                 bool& truncated) {
  uint32_t first = 0;
  uint32_t last = 0;
  if (!reader.findPrefixRange(prefix, first, last)) return false;
  out.reserve(PREFIX_CAP);
  std::vector<uint16_t> termVerses;
  std::vector<uint16_t> merged;
  termVerses.reserve(PREFIX_CAP);
  merged.reserve(PREFIX_CAP * 2);
  for (uint32_t index = first; index < last; index++) {
    TermEntry entry;
    termVerses.clear();
    if (!reader.term(index, entry) || !reader.postings(entry, termVerses, PREFIX_CAP)) return false;
    if (entry.postingCount > PREFIX_CAP) truncated = true;
    merged.clear();
    std::set_union(out.begin(), out.end(), termVerses.begin(), termVerses.end(), std::back_inserter(merged));
    if (merged.size() > PREFIX_CAP) {
      merged.resize(PREFIX_CAP);
      truncated = true;
    }
    std::swap(out, merged);
  }
  return true;
}

}  // namespace

QueryResult runQuery(const IndexReader& reader, const std::string_view rawQuery) {
  QueryResult result;
  const std::vector<std::string> tokens = queryTokens(rawQuery);
  if (tokens.empty()) return result;

  std::vector<std::vector<uint16_t>> lists(tokens.size());
  for (size_t i = 0; i + 1 < tokens.size(); i++) {
    TermEntry entry;
    if (!reader.findExact(tokens[i], entry) || !reader.postings(entry, lists[i], entry.postingCount)) return result;
  }
  bool prefixTruncated = false;
  if (!prefixUnion(reader, tokens.back(), lists.back(), prefixTruncated)) return result;

  std::sort(lists.begin(), lists.end(),
            [](const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) { return a.size() < b.size(); });
  std::vector<uint16_t> matches = std::move(lists.front());
  std::vector<uint16_t> narrowed;
  narrowed.reserve(matches.size());
  for (size_t i = 1; i < lists.size() && !matches.empty(); i++) {
    narrowed.clear();
    std::set_intersection(matches.begin(), matches.end(), lists[i].begin(), lists[i].end(),
                          std::back_inserter(narrowed));
    std::swap(matches, narrowed);
  }

  result.truncated = prefixTruncated;
  if (matches.size() > RESULT_CAP) {
    matches.resize(RESULT_CAP);
    result.truncated = true;
  }
  result.verses = std::move(matches);
  return result;
}

}  // namespace BibleSearch
