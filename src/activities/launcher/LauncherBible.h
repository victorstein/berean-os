#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// How the launcher recognises the Bible on the card, and how the reader
// registers one it has opened. Free of firmware includes so the rules can be
// tested on the host; the callers supply the registry and card I/O.

// The symbol the jw.org catalog lists the New World Translation under, and so
// the one PublicationDownloader records in PubKeyRegistry for a Buscar download.
inline constexpr std::string_view BIBLE_SYMBOL = "nwt";

// True when `path` ends in the CDN's own name for a publication with no issue:
// "<symbol>_<LANG>.epub", such as "nwt_S.epub". That name is what a copy fetched
// from jw.org by hand carries, and what the downloader left on the card before
// it named files after the publication. The underscore is required right after
// the symbol so that "nwtsty_S.epub" is not taken for "nwt".
constexpr bool isCdnNamedCopyOf(const std::string_view path, const std::string_view symbol) {
  const size_t slash = path.find_last_of('/');
  const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);

  constexpr std::string_view EXTENSION = ".epub";
  if (symbol.empty() || name.size() < symbol.size() + 1 + 1 + EXTENSION.size()) return false;
  if (name.substr(0, symbol.size()) != symbol || name[symbol.size()] != '_') return false;

  const std::string_view extension = name.substr(name.size() - EXTENSION.size());
  for (size_t i = 0; i < EXTENSION.size(); ++i) {
    const char c = extension[i];
    const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    if (lower != EXTENSION[i]) return false;
  }

  // JW language codes are short and upper-case: "S", "E", "CHS", "ASL".
  const std::string_view language = name.substr(symbol.size() + 1, name.size() - symbol.size() - 1 - EXTENSION.size());
  constexpr size_t LONGEST_LANGUAGE_CODE = 8;
  if (language.empty() || language.size() > LONGEST_LANGUAGE_CODE) return false;
  for (const char c : language) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

enum class BibleLookup : uint8_t { Registry, CardScan, Recents };

inline constexpr BibleLookup BIBLE_LOOKUP_ORDER[] = {BibleLookup::Registry, BibleLookup::CardScan,
                                                     BibleLookup::Recents};

constexpr const char* bibleLookupName(const BibleLookup step) {
  switch (step) {
    case BibleLookup::Registry:
      return "registry";
    case BibleLookup::CardScan:
      return "card scan";
    case BibleLookup::Recents:
      return "recents";
  }
  return "?";
}

// Stops at the first hit, so a later lookup never runs once an earlier one has
// found a Bible. `tryLookup(step)` returns std::optional<std::string>.
template <typename TryLookup>
std::optional<std::string> resolveBible(TryLookup&& tryLookup) {
  for (const BibleLookup step : BIBLE_LOOKUP_ORDER) {
    std::optional<std::string> found = tryLookup(step);
    if (found) return found;
  }
  return std::nullopt;
}

// The pre-#104 recents match, kept exactly as it was: the path carries "nwt" or
// the title names the translation in Spanish or English. It can take a
// non-Bible titled "New World", which is why it runs last.
constexpr bool looksLikeBibleInRecents(const std::string_view path, const std::string_view title) {
  return path.find("nwt") != std::string_view::npos || title.find("Nuevo Mundo") != std::string_view::npos ||
         title.find("New World") != std::string_view::npos;
}

enum class BibleRegistration : uint8_t { NotBible, AlreadyKnown, Recorded, Refused };

// Records `path` as the Bible unless the registry already holds ANY entry for
// it: record() overwrites, and a Buscar download of another NWT edition must
// keep its own symbol. Never logs -- this header stays host-buildable, so the
// caller logs from the result.
template <typename IsRegistered, typename RecordAsBible>
BibleRegistration registerBibleIfUnknown(const bool isBible, const std::string& path, IsRegistered&& isRegistered,
                                         RecordAsBible&& recordAsBible) {
  if (!isBible) return BibleRegistration::NotBible;
  if (isRegistered(path)) return BibleRegistration::AlreadyKnown;
  return recordAsBible(path) ? BibleRegistration::Recorded : BibleRegistration::Refused;
}
