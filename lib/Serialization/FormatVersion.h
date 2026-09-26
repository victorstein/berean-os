#pragma once

#include "DocReadStatus.h"

// Format-version rules for the PersistableStore JSON files. Kept free of Arduino
// and ArduinoJson, the same shape as fitsBudget in SaveBudget.h, so the rules are
// host-testable even though most stores applying them are not.
namespace persist {

// Callers read the version as `doc["v"] | FORMAT_VERSION`, so a file written
// before versioning -- no "v" -- arrives here as 1 and loads. No build ever
// wrote 0 or a negative.
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
