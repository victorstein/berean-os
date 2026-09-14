#include "network/MeetingLibrary.h"

#include <HalStorage.h>
#include <Logging.h>

#include "network/MeetingFilename.h"
#include "study/PubKeyRegistry.h"
#include "util/CardBooks.h"

namespace {

constexpr const char* MODULE = "MEETLIB";

bool namedLikeTheWatchtower(const std::string& name) {
  return name.find("talaya") != std::string::npos || name.find("atchtower") != std::string::npos;
}

std::string scanCardForIssue(const MeetingPub pub, const std::string& issue) {
  const std::string suffix = issueSuffix(issue.c_str());
  if (suffix.empty()) return {};

  std::string fallback;
  for (const std::string& path : CardBooks::list()) {
    if (meetingIssueSuffixOf(path) != suffix) continue;

    if (namedLikeTheWatchtower(path) == (pub == MeetingPub::Watchtower)) return path;
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
