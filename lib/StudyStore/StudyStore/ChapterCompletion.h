#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "StudyStore/UnitAnchors.h"

// Which Bible chapters the user has paged through, with all format rules and no
// storage access. The storage shell is src/study/ChapterCompletionFile.
//
// Keyed by canonical book + chapter -- the same address a Verse unit carries --
// never by spine index, so the record follows the user to another edition the
// way tagged passages already do.
namespace study {

inline constexpr uint8_t BIBLE_BOOK_COUNT = 66;

// Chapters per book in the 66-book canon, in biblebooknav.xhtml's order -- the
// order Unit::book numbers. English versification (Joel 3, Malachi 4), which is
// the NWT's in every language.
uint8_t canonicalChapterCount(uint8_t book);

inline constexpr uint16_t CANONICAL_CHAPTER_TOTAL = 1189;

class ChapterCompletion {
 public:
  static constexpr int FORMAT_VERSION = 1;
  // A record with every chapter read serialises to under 1 KB; this bounds a
  // hand-edited or corrupt record, well clear of SDCardManager::readFile's
  // 50,000-byte truncation.
  static constexpr size_t SAVE_BYTE_BUDGET = 4096;

  bool isRead(uint8_t book, uint16_t chapter) const;
  // True only when the chapter was not already marked. False for an address
  // outside the canon.
  bool markRead(uint8_t book, uint16_t chapter);

  uint16_t readCount() const;
  uint16_t readCountInBook(uint8_t book) const;

  // Refuses, leaving `doc` unusable, when the serialised record exceeds `budget`.
  bool toJsonWithinBudget(JsonDocument& doc, size_t budget = SAVE_BYTE_BUDGET) const;

  // All-or-nothing: a future version, or anything this firmware would never
  // have written, is rejected and the held record left as it was. Rejection is
  // what tells the caller the file may still hold the user's history.
  bool fromJson(JsonVariantConst doc);

  size_t measureBytes() const;

  bool operator==(const ChapterCompletion&) const = default;

 private:
  void toJson(JsonDocument& doc) const;

  // Dense: one bit per canonical chapter, books back to back.
  std::array<uint8_t, (CANONICAL_CHAPTER_TOTAL + 7) / 8> bits_{};
};

// Whether a forward page turn moves off the end of the current document. Mirrors
// EpubReaderActivity::pageTurn's own branch, which uses it, so "the user left
// the chapter's last page" cannot drift from what the reader actually does.
constexpr bool forwardTurnLeavesDocument(const int currentPage, const int pageCount, const bool stillBuilding) {
  return !(currentPage < pageCount - 1 || stillBuilding);
}

// Marks every chapter whose verse markers `units` carries. True when anything
// was newly marked. A document that is not a resolved Bible chapter marks
// nothing.
bool markDocumentChapters(ChapterCompletion& record, const DocumentUnits& units);

enum class CompletionMarkResult : uint8_t { NothingNew, Saved, SaveRefused, SaveFailed };

using CompletionSaveFn = bool (*)(const ChapterCompletion&);

// Records a paged-through document and persists the record only when a chapter
// was newly marked, so re-reading costs no write. The held record always matches
// the card: a refused or failed save rolls the marks back.
//
// `saveDisabled` is set when the record failed to load -- the card may still
// hold the user's history, and the empty record in memory must never replace it.
CompletionMarkResult recordDocumentRead(ChapterCompletion& record, const DocumentUnits& units, bool saveDisabled,
                                        CompletionSaveFn save);

}  // namespace study
