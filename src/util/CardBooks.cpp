#include "util/CardBooks.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <algorithm>

#include "CrossPointSettings.h"

namespace {

// Matches BookPathIndex's cap: a card holding more than this in one directory
// is beyond what any screen here can usefully present anyway.
constexpr int MAX_FILES_PER_DIR = 400;

void collect(const std::string& dir, std::vector<std::string>& out) {
  const std::string prefix = dir == "/" ? "/" : dir + "/";
  for (const String& entry : Storage.listFiles(dir.c_str(), MAX_FILES_PER_DIR)) {
    const std::string name(entry.c_str());
    if (!FsHelpers::hasEpubExtension(name)) continue;

    const std::string full = name.find('/') == std::string::npos ? prefix + name : name;
    if (std::find(out.begin(), out.end(), full) == out.end()) out.push_back(full);
  }
}

}  // namespace

namespace CardBooks {

std::vector<std::string> list() {
  std::vector<std::string> out;

  std::string downloads = SETTINGS.downloadFolder;
  while (downloads.size() > 1 && downloads.back() == '/') downloads.pop_back();
  if (!downloads.empty() && downloads != "/") collect(downloads, out);
  collect("/", out);

  return out;
}

std::string displayStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace CardBooks
