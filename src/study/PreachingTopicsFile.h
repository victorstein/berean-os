#pragma once

#include <SdPaths.h>
#include <TempAdoption.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "StudyStore/TagPalette.h"

// The tags the owner picked as preaching topics, in /.berean/preaching.json:
// {"v":1,"tags":[12,31,7]}. One object is one session. load() reads this file
// and tags.json, and a failed read of either refuses every save from this
// object, so the picks are never overwritten with what could not be read. A new
// object refuses to save until load() has run.
//
// Loop task only: load() can rename a leftover .tmp into place, which is safe
// only while no other task touches these files (PersistableStore.h,
// loadAdopting).
class PreachingTopicsFile {
 public:
  static constexpr int FORMAT_VERSION = 1;
  static constexpr size_t SAVE_BYTE_BUDGET = 2048;
  static constexpr const char* PATH = sdpaths::PREACHING_FILE;

  using LoadResult = AdoptedLoad;
  enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed, Refused };

  // Tests pass a smaller budget: no list this store accepts reaches 2048 bytes.
  explicit PreachingTopicsFile(size_t saveBudget = SAVE_BYTE_BUDGET) : saveBudget_(saveBudget) {}

  // preaching.json's outcome. A failed tags.json shows only in saveRefused().
  LoadResult load();

  const std::vector<study::TagId>& tags() const { return tags_; }
  const study::TagPalette& palette() const { return palette_; }
  bool saveRefused() const { return saveRefused_; }

  // Keeps the picks active in palette(), once each. Any result but Ok leaves
  // tags() as it was.
  SaveResult save(const std::vector<study::TagId>& picked);

 private:
  study::TagPalette palette_;
  std::vector<study::TagId> tags_;
  size_t saveBudget_;
  bool saveRefused_ = true;
};
