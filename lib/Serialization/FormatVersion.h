#pragma once

#include "DocReadStatus.h"

// Format-version rules for the PersistableStore JSON files. Kept free of Arduino
// and ArduinoJson, the same shape as fitsBudget in SaveBudget.h, so the rules are
// host-testable even though most stores applying them are not.
namespace persist {

// A caller decides what an absent "v" means by the default it reads with:
// `doc["v"] | FORMAT_VERSION` loads a file written before versioning, and
// `doc["v"] | 0` refuses it, because 0 is never known. A written 0 or negative
// is refused either way; no build ever wrote one.
constexpr bool isKnownFormatVersion(const int version, const int newestKnown) {
  return version > 0 && version <= newestKnown;
}

// Whether saves must be refused after a load that ended in `status`. A file this
// build refused has to survive until a build that knows its format reads it; a
// missing file has nothing left to protect. Unreadable and unparseable files keep
// the previous answer, so their handling is exactly what it was before versioning.
constexpr bool loadRefusedAfter(const DocReadStatus status, const bool accepted, const bool wasRefused) {
  switch (status) {
    case DocReadStatus::Ok:
      return !accepted;
    case DocReadStatus::Missing:
      return false;
    case DocReadStatus::Unreadable:
    case DocReadStatus::ParseError:
    default:
      return wasRefused;
  }
}

}  // namespace persist
