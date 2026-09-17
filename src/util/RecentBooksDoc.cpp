#include "RecentBooksDoc.h"

#include <Utf8.h>

#include <algorithm>

namespace RecentBooksDoc {
namespace {

// ArduinoJson stores a std::string length-aware, so an embedded NUL survives to
// the serialiser and expands to a six-byte \u escape, which would break
// ESCAPE_FACTOR. Erasing is cheaper than arguing that it cannot happen.
bool eraseNuls(std::string& field) {
  const auto it = std::remove(field.begin(), field.end(), '\0');
  if (it == field.end()) return false;
  field.erase(it, field.end());
  return true;
}

// utf8SafeSummary collapses whitespace runs, strips newlines, trims, and caps on
// a codepoint boundary. It clamps before calling utf8SafeTruncateBuffer, which
// indexes buf[len - 1] without checking the buffer is that long (Utf8.cpp:148).
bool capField(std::string& field, const size_t maxBytes) {
  std::string capped = utf8SafeSummary(field, maxBytes);
  if (capped == field) return false;
  field = std::move(capped);
  return true;
}

}  // namespace

bool normalise(RecentBook& book) {
  // capField(...) || changed, never changed || capField(...): the second form
  // short-circuits and would skip the author once the title had changed.
  bool changed = eraseNuls(book.title);
  changed = eraseNuls(book.author) || changed;
  changed = capField(book.title, MAX_TITLE_BYTES) || changed;
  changed = capField(book.author, MAX_AUTHOR_BYTES) || changed;
  return changed;
}

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
    if (normalise(book)) needsResave = true;
    books.push_back(std::move(book));
  }
  return true;
}

}  // namespace RecentBooksDoc
