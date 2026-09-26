#include "HighlightFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PathFlatten.h>
#include <PersistableStore.h>
#include <SdPaths.h>

#include "HighlightFileAction.h"

namespace {

std::string highlightsDir() { return std::string(sdpaths::HIGHLIGHTS_DIR) + "/"; }

std::string highlightPath(const std::string& bookPath) {
  return highlightsDir() + pathflatten::toCacheName(bookPath) + ".json";
}

}  // namespace

namespace HighlightFile {

LoadResult load(const std::string& bookPath, HighlightDoc& doc) {
  const std::string path = highlightPath(bookPath);
  return PersistableStoreBase::loadAdopting(
      path.c_str(), &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) { return static_cast<HighlightDoc*>(target)->fromJson(json); },
      &doc);
}

SaveResult save(const std::string& bookPath, const HighlightDoc& doc) {
  JsonDocument json;
  doc.toJson(json);

  if (highlightSaveAction(measureJson(json), SAVE_BYTE_BUDGET) == HighlightSaveAction::RefuseTooLarge) {
    LOG_ERR("HLFILE", "Highlights for %s exceed the save budget; not written", bookPath.c_str());
    return SaveResult::TooLarge;
  }

  // writeDocToFileAtomic only ensures /.crosspoint; the highlights
  // subdirectory is ours, same as BookmarkFile.cpp does for bookmarks.
  Storage.mkdir(highlightsDir().c_str());
  const std::string path = highlightPath(bookPath);
  return PersistableStoreBase::writeDocToFileAtomic(path.c_str(), json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace HighlightFile
