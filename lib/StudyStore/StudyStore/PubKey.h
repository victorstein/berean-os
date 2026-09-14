#pragma once

#include <optional>
#include <string>

// Publication identity for the study store. Study data is keyed on this rather
// than on a file path, so re-downloading or renaming a publication keeps its
// tags -- unlike the EPUB cache, which hashes the path (lib/Epub/Epub.h:48) and
// loses everything when a book moves.
//
// There is no metadata field on disk that yields a symbol: JW's OPF carries a
// random urn:uuid as dc:identifier and the symbol appears nowhere. Only the
// downloader knows it, so it records one in /.berean/pubkeys.json and everything
// else consults that.
namespace study {

struct RegisteredPub {
  std::string symbol;    // "w", "mwb", "lff"
  std::string issue;     // "202607", or empty for a non-periodical
  std::string language;  // "S", "E"
};

struct PubKeyInputs {
  bool isBible = false;  // Epub::getBibleBookNavSpineIndex() >= 0
  // The book-nav page listed exactly 66 books in canonical order. The shared
  // `bible` key means a mark made in one translation resolves in another, which
  // is only safe if the canon and its ordering match -- a Bible with extra or
  // reordered books would shift every book number and land marks in the wrong
  // book. Anything unverified gets a per-publication key instead.
  bool canonVerified = false;
  std::optional<RegisteredPub> registered;
  std::string bookPath;
};

// The Bible's key excludes language deliberately: the verse address is the
// identity, so marks follow the user between renderings.
inline constexpr const char* BIBLE_PUB_KEY = "bible";

std::string resolvePubKey(const PubKeyInputs& in);

// False for a path-derived "local-" key, whose data does not survive the file
// moving. The tag list uses this to mark such passages rather than pretend.
bool pubKeyIsStable(const std::string& key);

}  // namespace study
