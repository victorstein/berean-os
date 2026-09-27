#pragma once

#include <cstddef>
#include <string_view>

// How the launcher recognises the Bible on the card. Free of firmware includes
// so the filename rule can be tested on the host.

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
