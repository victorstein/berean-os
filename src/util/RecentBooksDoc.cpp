#include "RecentBooksDoc.h"

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

}  // namespace RecentBooksDoc
