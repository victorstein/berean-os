#include "Query.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "Fold.h"

namespace BibleSearch {
namespace {

// Candidates narrowed in place: each further list keeps only the candidates
// it contains, so intersection needs no second buffer.
struct Candidates {
  uint16_t* verses;
  size_t size;
  size_t read;
  size_t write;
};

void appendCandidate(void* ctx, const uint16_t verse) {
  auto* c = static_cast<Candidates*>(ctx);
  c->verses[c->size++] = verse;
}

void keepCandidate(void* ctx, const uint16_t verse) {
  auto* c = static_cast<Candidates*>(ctx);
  while (c->read < c->size && c->verses[c->read] < verse) c->read++;
  if (c->read < c->size && c->verses[c->read] == verse) c->verses[c->write++] = c->verses[c->read++];
}

void markVerse(void* ctx, const uint16_t verse) {
  static_cast<uint8_t*>(ctx)[verse >> 3] |= static_cast<uint8_t>(1u << (verse & 7));
}

bool isMarked(const uint8_t* bits, const uint32_t verse) { return (bits[verse >> 3] >> (verse & 7)) & 1u; }

QueryResult failed() {
  QueryResult result;
  result.ok = false;
  return result;
}

void keepAtMost(QueryResult& result, const size_t total, const uint16_t* verses, const size_t count) {
  const size_t kept = std::min(count, RESULT_CAP);
  result.verses.reserve(kept);
  result.verses.assign(verses, verses + kept);
  result.truncated = total > kept;
}

}  // namespace

QueryResult runQuery(const IndexReader& reader, const std::string_view rawQuery) {
  QueryResult result;
  // Bounded by the query's length: the keyboard caps it at 64 bytes, so at
  // most ~21 tokens of two or more bytes.
  const std::vector<std::string> tokens = queryTokens(rawQuery);
  if (tokens.empty()) return result;
  const size_t exactCount = tokens.size() - 1;

  std::vector<TermEntry> exact;
  exact.reserve(exactCount);
  for (size_t i = 0; i < exactCount; i++) {
    TermEntry entry;
    bool found = false;
    if (!reader.findExact(tokens[i], entry, found)) return failed();
    if (!found) return result;
    exact.push_back(entry);
  }
  uint32_t first = 0;
  uint32_t last = 0;
  if (!reader.findPrefixRange(tokens.back(), first, last)) return failed();
  if (first == last) return result;

  const BuildAllocator allocator = reader.allocator();
  AllocatedBuffer marks(allocator, (reader.verseCount() + 7) / 8);
  if (!marks.get()) return failed();
  memset(marks.get(), 0, marks.size());
  if (!reader.postingsRange(first, last, markVerse, marks.get())) return failed();
  const auto* bits = marks.as<uint8_t>();

  if (exact.empty()) {
    AllocatedBuffer kept(allocator, std::min<size_t>(PREFIX_CAP, reader.verseCount()) * sizeof(uint16_t));
    if (!kept.get()) return failed();
    auto* verses = kept.as<uint16_t>();
    size_t total = 0;
    size_t count = 0;
    for (uint32_t verse = 0; verse < reader.verseCount(); verse++) {
      if (!isMarked(bits, verse)) continue;
      if (count < PREFIX_CAP) verses[count++] = static_cast<uint16_t>(verse);
      total++;
    }
    keepAtMost(result, total, verses, count);
    return result;
  }

  // Smallest first: the candidate buffer is sized by the rarest word, and
  // every later list can only shrink it.
  std::sort(exact.begin(), exact.end(),
            [](const TermEntry& a, const TermEntry& b) { return a.postingCount < b.postingCount; });
  AllocatedBuffer buffer(allocator, std::max<size_t>(exact.front().postingCount, 1) * sizeof(uint16_t));
  if (!buffer.get()) return failed();
  Candidates candidates{buffer.as<uint16_t>(), 0, 0, 0};
  if (!reader.postingsOf(exact.front(), appendCandidate, &candidates)) return failed();
  for (size_t i = 1; i < exact.size() && candidates.size > 0; i++) {
    candidates.read = 0;
    candidates.write = 0;
    if (!reader.postingsOf(exact[i], keepCandidate, &candidates)) return failed();
    candidates.size = candidates.write;
  }

  size_t matched = 0;
  for (size_t i = 0; i < candidates.size; i++) {
    if (isMarked(bits, candidates.verses[i])) candidates.verses[matched++] = candidates.verses[i];
  }
  keepAtMost(result, matched, candidates.verses, matched);
  return result;
}

}  // namespace BibleSearch
