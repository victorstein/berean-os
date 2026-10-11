#pragma once

#include <cstdint>

#include "BibleBookNameTable.h"
#include "BibleNavCache.h"
#include "BookGridLayout.h"

// Everything BibleNavigationActivity::loadBooks() resolves, as the reader keeps it between Go tos.
struct BibleBookIndex {
  BibleBookNameTable names;
  int16_t targetSpine[BibleNavLimits::MAX_BOOKS] = {};
  bool isDirect[BibleNavLimits::MAX_BOOKS] = {};
  char abbrev[BibleNavLimits::MAX_BOOKS][BibleNavLimits::BOOK_ABBREV_BYTES] = {};
  char sectionTitle[BookGrid::MAX_SECTIONS][BibleNavLimits::BOOK_NAME_BYTES] = {};
  int sectionStart[BookGrid::MAX_SECTIONS] = {};
  int sectionCount = 0;
  int bookCount = 0;
};

struct BibleNavCache final : BibleNavCacheOf<BibleBookIndex> {};

static_assert(BibleNavLimits::MAX_BOOKS == BibleBookNameTable::MAX_BOOKS, "one book limit");
static_assert(BibleNavLimits::BOOK_NAME_BYTES == BibleBookNameTable::NAME_BYTES, "one name width");
static_assert(BibleNavLimits::BOOK_ABBREV_BYTES == BibleBookNameTable::ABBREV_BYTES, "one abbreviation width");
