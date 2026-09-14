#include "BookPathIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PathFlatten.h>

#include <vector>

#include "CrossPointSettings.h"

namespace {

constexpr const char* MODULE = "BOOKPATH";
constexpr int MAX_FILES_PER_DIR = 400;

bool endsWithEpub(const std::string& name) {
  if (name.size() < 5) return false;
  const std::string tail = name.substr(name.size() - 5);
  return tail == ".epub" || tail == ".EPUB";
}

// Appends every EPUB in `dir` whose flattened stem matches, as a full path.
void collectMatches(const std::string& dir, const std::string& wanted, std::vector<std::string>& out) {
  const std::string prefix = dir == "/" ? "/" : dir + "/";
  for (const String& entry : Storage.listFiles(dir.c_str(), MAX_FILES_PER_DIR)) {
    const std::string name(entry.c_str());
    if (!endsWithEpub(name)) continue;

    const std::string full = prefix + name;
    if (pathflatten::toCacheName(full) == wanted) out.push_back(full);
  }
}

}  // namespace

namespace BookPathIndex {

std::optional<std::string> resolve(const std::string& flattenedStem) {
  if (flattenedStem.empty()) return std::nullopt;

  std::vector<std::string> matches;

  const std::string downloads = SETTINGS.downloadFolder;
  if (!downloads.empty() && downloads != "/") collectMatches(downloads, flattenedStem, matches);
  collectMatches("/", flattenedStem, matches);

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
