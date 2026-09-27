#include "Catalog/CatalogLabel.h"

#include <cstdio>
#include <cstring>

namespace catalog {
namespace {

constexpr size_t DAILY_ISSUE_LENGTH = 8;    // YYYYMMDD: semimonthly and weekly issues
constexpr size_t MONTHLY_ISSUE_LENGTH = 6;  // YYYYMM: the builder strips a 00 day
constexpr size_t MAX_MONTH_NAME_BYTES = 16;
constexpr size_t MAX_LABEL_BYTES = 48;

}  // namespace

std::string_view wordAt(const std::string_view list, const int index) {
  size_t start = 0;
  int seen = 0;
  while (start < list.size()) {
    while (start < list.size() && list[start] == ' ') ++start;
    const size_t space = list.find(' ', start);
    const size_t stop = space == std::string_view::npos ? list.size() : space;
    if (stop > start) {
      if (seen == index) return list.substr(start, stop - start);
      ++seen;
    }
    start = stop + 1;
  }
  return {};
}

bool digitsToInt(const std::string_view text, int& out) {
  if (text.empty()) return false;
  out = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    out = out * 10 + (c - '0');
  }
  return true;
}

bool copyOut(const std::string_view text, char* out, const size_t outSize) {
  if (text.size() + 1 > outSize) return false;
  // string_view is not null-terminated; the precision form copies exactly the
  // view's bytes and terminates.
  snprintf(out, outSize, "%.*s", static_cast<int>(text.size()), text.data());
  return true;
}

std::string_view displayTitle(const std::string_view title, const std::string_view year, const bool hasIssue) {
  if (!hasIssue || year.empty() || title.size() <= year.size() + 1) return title;
  const size_t yearStart = title.size() - year.size();
  if (title.substr(yearStart) != year || title[yearStart - 1] != ' ') return title;
  return title.substr(0, yearStart - 1);
}

bool formatIssueDate(const std::string_view issue, const std::string_view monthsLong, const char* dayFormat,
                     const char* monthFormat, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return false;

  const bool daily = issue.size() == DAILY_ISSUE_LENGTH;
  int year = 0;
  int month = 0;
  int day = 0;
  bool parsed = (daily || issue.size() == MONTHLY_ISSUE_LENGTH) && digitsToInt(issue.substr(0, 4), year) &&
                digitsToInt(issue.substr(4, 2), month) && month >= 1 && month <= 12;
  if (parsed && daily) parsed = digitsToInt(issue.substr(6, 2), day) && day >= 1 && day <= 31;
  if (!parsed) return copyOut(issue, out, outSize);

  // The word is a view into the middle of the twelve-name list; %s needs it
  // terminated, or it prints every month after it.
  char monthName[MAX_MONTH_NAME_BYTES];
  if (!copyOut(wordAt(monthsLong, month - 1), monthName, sizeof(monthName)) || monthName[0] == '\0') {
    return copyOut(issue, out, outSize);
  }

  char label[MAX_LABEL_BYTES];
  const int written = daily ? snprintf(label, sizeof(label), dayFormat, day, monthName, year)
                            : snprintf(label, sizeof(label), monthFormat, monthName, year);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(label)) return copyOut(issue, out, outSize);
  if (!daily && label[0] >= 'a' && label[0] <= 'z') label[0] = static_cast<char>(label[0] - 'a' + 'A');
  return copyOut(std::string_view(label, static_cast<size_t>(written)), out, outSize);
}

}  // namespace catalog
