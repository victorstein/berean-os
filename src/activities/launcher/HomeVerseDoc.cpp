#include "activities/launcher/HomeVerseDoc.h"

#include <cstring>

namespace HomeVerseDoc {

bool isUsableBuildId(const char* const buildId) {
  if (buildId == nullptr) return false;
  const size_t length = strnlen(buildId, MAX_BUILD_ID_BYTES + 1);
  if (length == 0 || length > MAX_BUILD_ID_BYTES) return false;
  return strspn(buildId, "0") != length;
}

}  // namespace HomeVerseDoc
