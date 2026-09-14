#include "network/MeetingLibrary.h"

#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "network/MeetingFilename.h"
#include "study/PubKeyRegistry.h"

namespace {

constexpr const char* MODULE = "MEETLIB";

bool namedLikeTheWatchtower(const std::string& name) {
  return name.find("talaya") != std::string::npos || name.find("atchtower") != std::string::npos;
}

std::string scanCardForIssue(const MeetingPub pub, const std::string& issue) {
  const std::string suffix = issueSuffix(issue.c_str());
  if (suffix.empty()) return {};

  std::string folder = SETTINGS.downloadFolder[0] != '\0' ? SETTINGS.downloadFolder : "/";
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  // Epub keys its cache directory on a hash of the path, so a doubled separator
  // here would name a different cache than the reader uses for the same file.
  const std::string prefix = folder == "/" ? "/" : folder + "/";

  std::string fallback;
  for (const String& entry : Storage.listFiles(folder.c_str(), 200)) {
    const std::string name = entry.c_str();
    if (meetingIssueSuffixOf(name) != suffix) continue;

    const std::string path = name.find('/') == std::string::npos ? prefix + name : name;
    if (namedLikeTheWatchtower(name) == (pub == MeetingPub::Watchtower)) return path;
    // Right issue, wrong-looking name: keep it only if nothing better turns up,
    // since a publication renamed by hand still beats offering a download of a
    // file that is already there.
    if (fallback.empty()) fallback = path;
  }
  return fallback;
}

}  // namespace

namespace MeetingLibrary {

const char* symbolFor(const MeetingPub pub) { return pub == MeetingPub::Watchtower ? "w" : "mwb"; }

std::string findPublication(const MeetingPub pub, const std::string& issue) {
  if (issue.empty()) return {};

  if (const auto registered = PubKeyRegistry::findBySymbol({symbolFor(pub)}, issue)) {
    LOG_DBG(MODULE, "Registry has %s issue %s", symbolFor(pub), issue.c_str());
    return *registered;
  }

  const std::string found = scanCardForIssue(pub, issue);
  if (!found.empty() && !Storage.exists(found.c_str())) return {};
  return found;
}

}  // namespace MeetingLibrary
