#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Catalog/CatalogIndex.h"

// What the device remembers about the index it holds, and how that is shown.
//
// Header is a set of views into the inflated buffer, which is freed when Buscar
// closes. Staleness has to outlive that: the screen says "Catalogo: 12 sep 2026"
// before anything is loaded, and comparing a fetched release against the held
// one needs both provenances in hand at once.
namespace catalog {

struct Stamp {
  std::string language;
  std::string manifestId;
  std::string builtOn;  // ISO date, as the CI job wrote it

  bool valid() const { return !language.empty() && !builtOn.empty(); }
};

// Copies the header's views out of the buffer they point into.
Stamp stampOf(const Header& header);

// Whether two stamps name the same published index. Both fields count: the
// manifest id is not reliably content-addressed -- the same id has served a
// newer file -- so the build date is what moves when a rebuild republishes.
bool sameRelease(const Stamp& held, const Stamp& remote);

// Whether this build may read an index. The version must match exactly, so a
// future build's index is refused rather than reinterpreted, and the language
// must be the one asked for so a mis-named asset cannot masquerade.
bool indexAcceptable(const Header& header, std::string_view expectedLanguage);

// "2026-09-12" -> "12 sep 2026", taking the twelve abbreviations from one
// space-separated translated string so the date reads in the user's language
// without twelve more keys. Writes the ISO date through unchanged when it
// cannot be parsed -- a wrong-looking date is more useful than a blank one.
// Returns false when `out` was too small, having written nothing.
bool formatIndexDate(std::string_view isoDate, std::string_view monthsShort, char* out, size_t outSize);

}  // namespace catalog
