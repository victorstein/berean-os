#include "BookmarkDoc.h"

namespace BookmarkDoc {

void toJson(const std::vector<BookmarkEntry>& bookmarks, JsonDocument& doc) {
  doc["v"] = FORMAT_VERSION;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  for (const auto& bookmark : bookmarks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = bookmark.xpath;
    obj["percentage"] = bookmark.percentage;
    obj["summary"] = bookmark.summary;
    obj["si"] = bookmark.computedSpineIndex;
    obj["pc"] = bookmark.computedChapterPageCount;
    obj["pp"] = bookmark.computedChapterProgress;
    if (bookmark.hasVisibleTextOffset) {
      obj["vo"] = bookmark.visibleTextOffset;
    }
  }
}

bool fromJson(const JsonVariantConst doc, std::vector<BookmarkEntry>& bookmarks) {
  bookmarks.clear();
  if (!doc.is<JsonObjectConst>()) return false;

  // ArduinoJson's operator| yields the default only when the key is absent or
  // unconvertible, so an absent "v" reads as 1 (a file written before
  // versioning) while a written 0 keeps its value and is refused.
  const int version = doc["v"] | FORMAT_VERSION;
  if (version <= 0 || version > FORMAT_VERSION) return false;

  JsonArrayConst arr = doc["bookmarks"].as<JsonArrayConst>();
  bookmarks.reserve(arr.size());
  for (JsonObjectConst obj : arr) {
    bookmarks.emplace_back();
    auto& bookmark = bookmarks.back();
    // Read strings as const char*, never as | std::string(""): ArduinoJson's
    // std::string converter drags a copy of the serializer into flash.
    bookmark.xpath = obj["xpath"] | "";
    bookmark.percentage = obj["percentage"] | static_cast<float>(0);
    bookmark.summary = obj["summary"] | "";
    bookmark.computedSpineIndex = obj["si"] | static_cast<uint16_t>(0);
    bookmark.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    bookmark.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
    if (!obj["vo"].isNull()) {
      bookmark.visibleTextOffset = obj["vo"] | static_cast<uint32_t>(0);
      bookmark.hasVisibleTextOffset = true;
    }
  }
  return true;
}

}  // namespace BookmarkDoc
