#include "BookPathIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PathFlatten.h>

#include <algorithm>
#include <iterator>
#include <vector>

#include "util/CardBooks.h"

namespace {

constexpr const char* MODULE = "BOOKPATH";

}  // namespace

namespace BookPathIndex {

std::optional<std::string> resolve(const std::string& flattenedStem) {
  if (flattenedStem.empty()) return std::nullopt;

  const std::vector<std::string> onCard = CardBooks::list();
  std::vector<std::string> matches;
  std::copy_if(onCard.begin(), onCard.end(), std::back_inserter(matches),
               [&flattenedStem](const std::string& path) { return pathflatten::toCacheName(path) == flattenedStem; });

  if (matches.empty()) {
    LOG_ERR(MODULE, "No book on the card flattens to '%s'", flattenedStem.c_str());
    return std::nullopt;
  }
  if (matches.size() > 1) {
    // Two files flattening alike is rare but real: toCacheName maps '/' and '_'
    // to the same byte. Guessing would address every passage into whichever book
    // the directory happened to list first.
    LOG_ERR(MODULE, "%u books flatten to '%s'; refusing to guess", static_cast<unsigned>(matches.size()),
            flattenedStem.c_str());
    return std::nullopt;
  }
  return matches.front();
}

}  // namespace BookPathIndex
