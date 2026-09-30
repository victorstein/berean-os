#include "activities/launcher/BibleFinder.h"

#include <HalStorage.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "RecentBooksStore.h"
#include "study/PubKeyRegistry.h"
#include "util/CardBooks.h"

namespace BibleFinder {

namespace {

// A Bible the registry does not know is one that did not come through Buscar,
// and the only name it can be recognised by is the CDN's. When several
// languages are on the card the download folder's copy wins, then the root's.
std::optional<std::string> findOnCard() {
  const std::vector<std::string> books = CardBooks::list();
  const auto bible = std::find_if(books.begin(), books.end(),
                                  [](const std::string& path) { return isCdnNamedCopyOf(path, BIBLE_SYMBOL); });
  if (bible == books.end()) return std::nullopt;
  return *bible;
}

// recent.json can still list a deleted file; the existence check keeps that
// from offering a Bible that opens nothing.
std::optional<std::string> findInRecents(const std::string_view exclude) {
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path == exclude) continue;
    if (!looksLikeBibleInRecents(book.path, book.title)) continue;
    if (!Storage.exists(book.path.c_str())) continue;
    return book.path;
  }
  return std::nullopt;
}

}  // namespace

std::optional<Found> find(const std::string_view exclude) {
  BibleLookup foundBy = BibleLookup::Registry;
  auto path = resolveBible([&](const BibleLookup step) -> std::optional<std::string> {
    foundBy = step;
    switch (step) {
      case BibleLookup::Registry:
        return PubKeyRegistry::findBySymbol({BIBLE_SYMBOL});
      case BibleLookup::CardScan:
        return findOnCard();
      case BibleLookup::Recents:
        return findInRecents(exclude);
    }
    return std::nullopt;
  });
  if (!path) return std::nullopt;
  return Found{std::move(*path), foundBy};
}

}  // namespace BibleFinder
