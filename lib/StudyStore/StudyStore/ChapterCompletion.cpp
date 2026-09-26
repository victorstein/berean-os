#include "StudyStore/ChapterCompletion.h"

#include <FormatVersion.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace study {
namespace {

constexpr uint8_t CHAPTER_COUNTS[BIBLE_BOOK_COUNT] = {
    50, 40, 27, 36, 34, 24, 21, 4, 31, 24, 22, 25, 29, 36, 10, 13, 10, 42, 150, 31, 12, 8,
    66, 52, 5,  48, 12, 14, 3,  9, 1,  4,  7,  3,  3,  3,  2,  14, 4,  28, 16,  24, 21, 28,
    16, 16, 13, 6,  6,  4,  4,  5, 3,  6,  4,  3,  1,  13, 5,  5,  3,  5,  1,   1,  1,  22,
};

constexpr std::array<uint16_t, BIBLE_BOOK_COUNT + 1> firstBitOfBook() {
  std::array<uint16_t, BIBLE_BOOK_COUNT + 1> first{};
  for (size_t i = 0; i < BIBLE_BOOK_COUNT; ++i) first[i + 1] = first[i] + CHAPTER_COUNTS[i];
  return first;
}

constexpr auto FIRST_BIT = firstBitOfBook();
static_assert(FIRST_BIT[BIBLE_BOOK_COUNT] == CANONICAL_CHAPTER_TOTAL);

// 150 chapters -> 19 bytes -> 38 hex digits.
constexpr size_t MAX_BOOK_BYTES = (150 + 7) / 8;

constexpr size_t bytesForBook(const uint8_t book) { return (CHAPTER_COUNTS[book - 1] + 7) / 8; }

bool validBook(const uint8_t book) { return book >= 1 && book <= BIBLE_BOOK_COUNT; }

bool validAddress(const uint8_t book, const uint16_t chapter) {
  return validBook(book) && chapter >= 1 && chapter <= CHAPTER_COUNTS[book - 1];
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The key is written by toJson as a plain decimal; anything else is not ours.
uint8_t parseBookKey(const char* key) {
  if (key == nullptr || key[0] < '1' || key[0] > '9') return 0;
  char* end = nullptr;
  const long value = strtol(key, &end, 10);
  if (*end != '\0' || value < 1 || value > BIBLE_BOOK_COUNT) return 0;
  return static_cast<uint8_t>(value);
}

}  // namespace

uint8_t canonicalChapterCount(const uint8_t book) { return validBook(book) ? CHAPTER_COUNTS[book - 1] : 0; }

bool ChapterCompletion::isRead(const uint8_t book, const uint16_t chapter) const {
  if (!validAddress(book, chapter)) return false;
  const uint16_t bit = FIRST_BIT[book - 1] + chapter - 1;
  return (bits_[bit / 8] >> (bit % 8)) & 1u;
}

bool ChapterCompletion::markRead(const uint8_t book, const uint16_t chapter) {
  if (!validAddress(book, chapter) || isRead(book, chapter)) return false;
  const uint16_t bit = FIRST_BIT[book - 1] + chapter - 1;
  bits_[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
  return true;
}

uint16_t ChapterCompletion::readCount() const {
  uint16_t count = 0;
  for (const uint8_t byte : bits_) count += static_cast<uint16_t>(__builtin_popcount(byte));
  return count;
}

uint16_t ChapterCompletion::readCountInBook(const uint8_t book) const {
  uint16_t count = 0;
  for (uint16_t chapter = 1; chapter <= canonicalChapterCount(book); ++chapter) count += isRead(book, chapter);
  return count;
}

void ChapterCompletion::toJson(JsonDocument& doc) const {
  doc["v"] = FORMAT_VERSION;
  const auto books = doc["b"].to<JsonObject>();

  for (uint8_t book = 1; book <= BIBLE_BOOK_COUNT; ++book) {
    uint8_t bookBytes[MAX_BOOK_BYTES] = {};
    size_t usedBytes = 0;
    for (uint16_t chapter = 1; chapter <= CHAPTER_COUNTS[book - 1]; ++chapter) {
      if (!isRead(book, chapter)) continue;
      const size_t byteIndex = (chapter - 1) / 8;
      bookBytes[byteIndex] |= static_cast<uint8_t>(1u << ((chapter - 1) % 8));
      usedBytes = byteIndex + 1;
    }
    if (usedBytes == 0) continue;

    char hex[MAX_BOOK_BYTES * 2 + 1];
    for (size_t i = 0; i < usedBytes; ++i) snprintf(hex + i * 2, 3, "%02x", bookBytes[i]);
    char key[4];
    snprintf(key, sizeof(key), "%u", static_cast<unsigned>(book));
    books[key] = hex;
  }
}

bool ChapterCompletion::toJsonWithinBudget(JsonDocument& doc, const size_t budget) const {
  toJson(doc);
  return measureJson(doc) <= budget;
}

bool ChapterCompletion::fromJson(const JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;
  const int version = doc["v"] | 0;
  if (!persist::isKnownFormatVersion(version, FORMAT_VERSION)) return false;
  if (!doc["b"].is<JsonObjectConst>()) return false;

  ChapterCompletion parsed;
  for (const JsonPairConst entry : doc["b"].as<JsonObjectConst>()) {
    const uint8_t book = parseBookKey(entry.key().c_str());
    if (book == 0 || !entry.value().is<const char*>()) return false;

    const char* hex = entry.value().as<const char*>();
    const size_t hexLength = strlen(hex);
    if (hexLength % 2 != 0 || hexLength / 2 > bytesForBook(book)) return false;

    for (size_t byteIndex = 0; byteIndex < hexLength / 2; ++byteIndex) {
      const int high = hexValue(hex[byteIndex * 2]);
      const int low = hexValue(hex[byteIndex * 2 + 1]);
      if (high < 0 || low < 0) return false;
      const unsigned byte = static_cast<unsigned>(high * 16 + low);
      for (unsigned bit = 0; bit < 8; ++bit) {
        if (!((byte >> bit) & 1u)) continue;
        const uint16_t chapter = static_cast<uint16_t>(byteIndex * 8 + bit + 1);
        if (!parsed.markRead(book, chapter)) return false;
      }
    }
  }

  *this = parsed;
  return true;
}

size_t ChapterCompletion::measureBytes() const {
  JsonDocument doc;
  toJson(doc);
  return measureJson(doc);
}

bool markDocumentChapters(ChapterCompletion& record, const DocumentUnits& units) {
  if (units.kind != UnitKind::Verse || units.book == 0) return false;
  bool markedAny = false;
  for (const UnitAnchor& anchor : units.anchors) markedAny |= record.markRead(units.book, anchor.major);
  return markedAny;
}

CompletionMarkResult recordDocumentRead(ChapterCompletion& record, const DocumentUnits& units, const bool saveDisabled,
                                        const CompletionSaveFn save) {
  const ChapterCompletion before = record;
  if (!markDocumentChapters(record, units)) return CompletionMarkResult::NothingNew;

  if (saveDisabled) {
    record = before;
    return CompletionMarkResult::SaveRefused;
  }
  if (!save(record)) {
    record = before;
    return CompletionMarkResult::SaveFailed;
  }
  return CompletionMarkResult::Saved;
}

}  // namespace study
