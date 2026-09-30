#pragma once

#include <StudyStore/Unit.h>

#include <cstdint>

#include "Place.h"

// A one-shot instruction to the reader, carried by ActivityManager::goToReader and consumed once
// the book has loaded: open at a place, or straight into the chapter grid, verse search or tags.
// RAM only. Free of Arduino so test/ui_layout can exercise route().
struct ReaderEntryIntent {
  enum class Kind : uint8_t { None, OpenAt, GoTo, Search, Tags };
  enum class Route : uint8_t { None, Locate, ChapterGrid, TocList, Search, Highlights };

  Kind kind = Kind::None;
  // OpenAt only. The unit, not an offset: the reader resolves it in whichever edition is open.
  study::Unit unit{};
  uint16_t spineHint = 0;

  static ReaderEntryIntent of(const Kind kind) {
    ReaderEntryIntent intent;
    intent.kind = kind;
    return intent;
  }

  static ReaderEntryIntent openAt(const Place& place) {
    ReaderEntryIntent intent;
    intent.kind = Kind::OpenAt;
    intent.unit = place.unit;
    intent.spineHint = place.spineIndex;
    return intent;
  }

  // Places and verse search exist only in the Bible; outside it the grid is the TOC list.
  static constexpr Route route(const Kind kind, const bool isBible) {
    switch (kind) {
      case Kind::OpenAt:
        return isBible ? Route::Locate : Route::None;
      case Kind::GoTo:
        return isBible ? Route::ChapterGrid : Route::TocList;
      case Kind::Search:
        return isBible ? Route::Search : Route::None;
      case Kind::Tags:
        return Route::Highlights;
      case Kind::None:
        return Route::None;
    }
    return Route::None;
  }
};
