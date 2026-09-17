#include "RecentBooksDoc.h"

#include <algorithm>

namespace RecentBooksDoc {

void toJson(const std::vector<RecentBook>& books, JsonDocument& doc) {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }
}

bool fromJson(const JsonVariantConst doc, std::vector<RecentBook>& books, bool& needsResave) {
  books.clear();
  needsResave = false;

  // A missing or non-array "books" key yields a null JsonArrayConst, which
  // iterates zero times — the tolerated "no data yet" case.
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  books.reserve(std::min(arr.size(), MAX_RECENT_BOOKS));
  for (JsonObjectConst obj : arr) {
    if (books.size() >= MAX_RECENT_BOOKS) break;
    RecentBook book;
    // Read strings as const char*, never as | std::string(""): ArduinoJson's
    // std::string converter drags a copy of the serializer into flash.
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    books.push_back(std::move(book));
  }
  return true;
}

}  // namespace RecentBooksDoc
