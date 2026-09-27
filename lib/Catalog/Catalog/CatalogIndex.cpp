#include "Catalog/CatalogIndex.h"

#include <cctype>

namespace catalog {
namespace {

constexpr std::string_view MAGIC = "berean-catalog";

std::string_view lineAt(const std::string_view buffer, size_t& cursor) {
  if (cursor >= buffer.size()) return {};
  const size_t end = buffer.find(RECORD_SEP, cursor);
  const size_t stop = end == std::string_view::npos ? buffer.size() : end;
  const std::string_view line = buffer.substr(cursor, stop - cursor);
  cursor = end == std::string_view::npos ? buffer.size() : end + 1;
  return line;
}

// Splits `line` into at most `count` fields. The last field takes the rest of
// the line, tabs included, so a title containing a tab stays whole rather than
// shifting every later field.
size_t split(const std::string_view line, std::string_view* fields, const size_t count) {
  size_t found = 0;
  size_t start = 0;
  while (found < count) {
    const size_t sep = line.find(FIELD_SEP, start);
    if (sep == std::string_view::npos || found + 1 == count) {
      fields[found++] = line.substr(start);
      break;
    }
    fields[found++] = line.substr(start, sep - start);
    start = sep + 1;
  }
  return found;
}

// ASCII-only, deliberately. Folding Spanish accents would need a table this
// device does not otherwise carry, and JW symbols are ASCII; a user searching
// "atalaya" still matches "La Atalaya" without it.
char lowerAscii(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool containsFold(const std::string_view haystack, const std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    size_t j = 0;
    while (j < needle.size() && lowerAscii(haystack[i + j]) == lowerAscii(needle[j])) ++j;
    if (j == needle.size()) return true;
  }
  return false;
}

EpubAvailability parseEpub(const std::string_view field) {
  if (field == "1") return EpubAvailability::Available;
  if (field == "0") return EpubAvailability::Unavailable;
  return EpubAvailability::Unknown;
}

bool readable(const int version) { return version >= OLDEST_READABLE_VERSION && version <= FORMAT_VERSION; }

}  // namespace

Header parseHeader(const std::string_view index) {
  Header header;
  size_t cursor = 0;
  const std::string_view line = lineAt(index, cursor);
  if (line.substr(0, MAGIC.size()) != MAGIC) return header;

  std::string_view fields[5];
  if (split(line, fields, 5) < 5) return header;

  int version = 0;
  for (const char c : fields[1]) {
    if (c < '0' || c > '9') return header;
    version = version * 10 + (c - '0');
  }
  header.version = version;
  header.language = fields[2];
  header.manifestId = fields[3];
  header.builtOn = fields[4];
  return header;
}

size_t recordsBegin(const std::string_view index) {
  size_t cursor = 0;
  const std::string_view line = lineAt(index, cursor);
  if (line.substr(0, MAGIC.size()) != MAGIC) return index.size();
  return cursor;
}

bool nextEntry(const std::string_view index, const int version, size_t& cursor, Entry& out) {
  if (!readable(version)) return false;
  const size_t fieldCount = version == OLDEST_READABLE_VERSION ? 5 : 6;

  while (cursor < index.size()) {
    const std::string_view line = lineAt(index, cursor);
    if (line.empty()) continue;

    std::string_view fields[6];
    if (split(line, fields, fieldCount) < fieldCount) continue;  // malformed row: skip, do not stop

    out.symbol = fields[0];
    out.issue = fields[1];
    out.year = fields[2];
    out.type = fields[3];
    if (fieldCount == 5) {
      out.epub = EpubAvailability::Unknown;
      out.title = fields[4];
    } else {
      out.epub = parseEpub(fields[4]);
      out.title = fields[5];
    }
    return true;
  }
  return false;
}

bool matches(const Entry& entry, const std::string_view query) {
  size_t start = 0;
  bool sawTerm = false;
  while (start <= query.size()) {
    const size_t space = query.find(' ', start);
    const size_t stop = space == std::string_view::npos ? query.size() : space;
    const std::string_view term = query.substr(start, stop - start);
    if (!term.empty()) {
      sawTerm = true;
      // Every term must hit, so a second word narrows rather than widens.
      if (!containsFold(entry.title, term) && !containsFold(entry.symbol, term) && !containsFold(entry.issue, term) &&
          !containsFold(entry.year, term)) {
        return false;
      }
    }
    if (space == std::string_view::npos) break;
    start = space + 1;
  }
  return sawTerm;
}

bool listable(const Entry& entry) { return entry.epub != EpubAvailability::Unavailable; }

size_t search(const std::string_view index, const std::string_view query, const size_t maxResults, bool& truncated,
              const SearchVisitor visit, void* ctx) {
  truncated = false;
  const Header header = parseHeader(index);
  if (!readable(header.version)) return 0;

  size_t cursor = recordsBegin(index);
  size_t visited = 0;
  Entry entry;
  while (nextEntry(index, header.version, cursor, entry)) {
    // Hidden rows are dropped before the cap, so they never take a result slot.
    if (!listable(entry) || !matches(entry, query)) continue;
    if (visited >= maxResults) {
      truncated = true;
      break;
    }
    visit(ctx, entry);
    ++visited;
  }
  return visited;
}

}  // namespace catalog
