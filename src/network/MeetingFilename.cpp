#include "MeetingFilename.h"

#include <algorithm>

#include "network/WolWeekScan.h"
#include "util/StringUtils.h"

namespace {

constexpr size_t FILENAME_BUDGET_BYTES = 100;
// " YYYY-MM", appended after sanitising so the cap can never eat the code.
constexpr size_t ISSUE_SUFFIX_BYTES = 8;

bool isIssueCode(const char* issue) {
  if (!issue) return false;
  for (size_t i = 0; i < 6; ++i) {
    if (issue[i] < '0' || issue[i] > '9') return false;
  }
  return issue[6] == '\0';
}

// sanitizeFilename drops control characters and trims spaces and dots, so a name
// made only of those survives as its "book" fallback rather than as nothing.
bool hasUsableName(const char* pubName) {
  if (!pubName) return false;
  for (const char* c = pubName; *c != '\0'; ++c) {
    const auto byte = static_cast<unsigned char>(*c);
    if (byte >= 32 && byte != ' ' && byte != '.') return true;
  }
  return false;
}

}  // namespace

std::string meetingPublicationFilename(const char* pubName, const char* issue, const std::string& url) {
  // sanitizeFilename never returns empty — its last resort is the literal "book"
  // — so an unusable name has to be caught before sanitising, or the Watchtower
  // and the workbook would both claim "book <YYYY-MM>.epub" in the same run.
  if (!hasUsableName(pubName) || !isIssueCode(issue)) return filenameFromUrl(url);

  std::string filename = StringUtils::sanitizeFilename(pubName, FILENAME_BUDGET_BYTES - ISSUE_SUFFIX_BYTES);
  filename += ' ';
  filename.append(issue, 4);
  filename += '-';
  filename.append(issue + 4, 2);
  filename += ".epub";
  return filename;
}

// Deliberately separate from the issued form above rather than a special case
// inside it. An issue is what keeps two Watchtowers apart, so a periodical whose
// issue is missing or malformed must keep the CDN's name -- naming it by title
// alone would let one issue overwrite another. A publication with no issue AT
// ALL has nothing to collide with, and only the caller knows which it asked for.
std::string publicationFilename(const char* pubName, const std::string& url) {
  if (!hasUsableName(pubName)) return filenameFromUrl(url);
  return StringUtils::sanitizeFilename(pubName, FILENAME_BUDGET_BYTES - ISSUE_SUFFIX_BYTES) + ".epub";
}

std::string issueSuffix(const char* issue) {
  if (!isIssueCode(issue)) return {};
  std::string out;
  out.append(issue, 4);
  out += '-';
  out.append(issue + 4, 2);
  return out;
}

std::string meetingIssueSuffixOf(const std::string& filename) {
  constexpr size_t SUFFIX = 13;  // " YYYY-MM.epub"
  if (filename.size() < SUFFIX) return {};
  const std::string tail = filename.substr(filename.size() - SUFFIX);
  if (tail[0] != ' ' || tail[5] != '-' || tail.compare(8, 5, ".epub") != 0) return {};
  constexpr size_t DIGITS[] = {1, 2, 3, 4, 6, 7};
  const bool allDigits = std::all_of(std::begin(DIGITS), std::end(DIGITS),
                                     [&tail](const size_t i) { return tail[i] >= '0' && tail[i] <= '9'; });
  if (!allDigits) return {};
  return tail.substr(1, 7);
}
