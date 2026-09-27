#include "Catalog/CatalogStamp.h"

#include <cstdio>

#include "Catalog/CatalogLabel.h"

namespace catalog {
namespace {

constexpr size_t ISO_DATE_LENGTH = 10;  // YYYY-MM-DD

}  // namespace

Stamp stampOf(const Header& header) {
  Stamp stamp;
  if (!header.valid()) return stamp;
  stamp.language.assign(header.language);
  stamp.manifestId.assign(header.manifestId);
  stamp.builtOn.assign(header.builtOn);
  stamp.version = header.version;
  return stamp;
}

bool sameRelease(const Stamp& held, const Stamp& remote) {
  return held.valid() && remote.valid() && held.language == remote.language && held.manifestId == remote.manifestId &&
         held.builtOn == remote.builtOn && held.version == remote.version;
}

bool indexAcceptable(const Header& header, const std::string_view expectedLanguage) {
  return header.valid() && header.version >= OLDEST_READABLE_VERSION && header.version <= FORMAT_VERSION &&
         header.language == expectedLanguage;
}

bool formatIndexDate(const std::string_view isoDate, const std::string_view monthsShort, char* out,
                     const size_t outSize) {
  if (out == nullptr || outSize == 0) return false;

  int year = 0;
  int month = 0;
  int day = 0;
  const bool parsed = isoDate.size() == ISO_DATE_LENGTH && isoDate[4] == '-' && isoDate[7] == '-' &&
                      digitsToInt(isoDate.substr(0, 4), year) && digitsToInt(isoDate.substr(5, 2), month) &&
                      digitsToInt(isoDate.substr(8, 2), day) && month >= 1 && month <= 12;
  if (!parsed) return copyOut(isoDate, out, outSize);

  const std::string_view name = wordAt(monthsShort, month - 1);
  if (name.empty()) return copyOut(isoDate, out, outSize);

  char buffer[32];
  const int written =
      snprintf(buffer, sizeof(buffer), "%d %.*s %d", day, static_cast<int>(name.size()), name.data(), year);
  if (written <= 0) return copyOut(isoDate, out, outSize);
  return copyOut(std::string_view(buffer, static_cast<size_t>(written)), out, outSize);
}

}  // namespace catalog
