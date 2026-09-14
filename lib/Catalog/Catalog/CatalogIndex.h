#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The publication catalog, as this device holds it.
//
// GETPUBMEDIALINKS resolves a publication you already name; it does not list
// what exists. That list lives in JW Library's catalog: a 57.6 MB gzipped
// SQLite database covering 319,871 publications in every language. Spanish
// alone is 3,768 of them, which reduce to a 216,708-byte slim index -- 17,914
// bytes gzipped.
//
// The device never sees the database. A CI job derives the slim index and
// publishes it as a static file; the device fetches that, inflates it once into
// PSRAM on entering Buscar, scans RAM, and frees it on exit.
//
// The record is deliberately flat and fixed-field rather than JSON: 3,768 rows
// through a DOM parser would cost more RAM than the data, and the scan is a
// linear walk per keystroke.
namespace catalog {

// "sym\tissue\tyear\ttype\ttitle\n", one per line. Tab-separated because a
// publication title may contain almost anything else -- commas, quotes,
// parentheses and colons all occur in real titles.
inline constexpr char FIELD_SEP = '\t';
inline constexpr char RECORD_SEP = '\n';

struct Entry {
  std::string_view symbol;
  std::string_view issue;  // empty for a non-periodical
  std::string_view year;
  std::string_view type;
  std::string_view title;
};

// Header line: "berean-catalog\t<version>\t<language>\t<manifestId>\t<builtOn>"
struct Header {
  int version = 0;
  std::string_view language;
  std::string_view manifestId;
  std::string_view builtOn;  // ISO date, shown as "Catalogo: 12 sep 2026"

  bool valid() const { return version > 0 && !language.empty(); }
};

inline constexpr int FORMAT_VERSION = 1;

// Parses the header off the front of an inflated index. The view must outlive
// every Entry and the Header -- nothing here copies.
Header parseHeader(std::string_view index);

// Byte offset of the first record, i.e. just past the header line. Equal to
// index.size() when there is no header.
size_t recordsBegin(std::string_view index);

// Parses one record. Returns false at the end of the buffer or on a malformed
// line; `cursor` advances past the line either way, so a single bad row does
// not strand the scan.
bool nextEntry(std::string_view index, size_t& cursor, Entry& out);

// True when every space-separated term in `query` appears in the entry's symbol
// or title, case-insensitively for ASCII. Multi-term so "atalaya 2026" narrows
// the way a user expects, rather than requiring a contiguous match.
bool matches(const Entry& entry, std::string_view query);

}  // namespace catalog
