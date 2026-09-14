#pragma once

#include <cstddef>

// Byte budgets for anything persisted through PersistableStore.
//
// SDCardManager::readFile caps reads at SD_READ_TRUNCATION_CAP and returns the
// truncated string with no error and no log. A document that saves larger than
// that reads back mid-token on the next boot, fails to parse, initialises the
// store empty, and the following save overwrites the real file with {} -- so an
// oversized store does not degrade, it deletes itself.
//
// Measure the serialised document and refuse before writing. Refusing is
// recoverable; truncating is not.
namespace persist {

// SDCardManager::readFile's hard cap. Not a budget -- the point at which data
// starts disappearing silently.
inline constexpr size_t SD_READ_TRUNCATION_CAP = 50000;

// Default ceiling, deliberately clear of the cap. Mirrors the figure
// src/util/HighlightFile.h arrived at the same way.
inline constexpr size_t DEFAULT_SAVE_BUDGET = 45000;

// A store with a bounded record count may declare a tighter budget of its own.
constexpr bool fitsBudget(size_t serialisedBytes, size_t budget) { return serialisedBytes <= budget; }

}  // namespace persist
