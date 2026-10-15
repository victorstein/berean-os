#pragma once

#include <StudyStore/Unit.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "Place.h"
#include "activities/preaching/PreachingOrigin.h"

// An instruction to the reader, carried by ActivityManager::goToReader: open at a place or an href,
// or straight into the chapter grid, verse search or tags. `kind` and its fields are consumed once
// the book has loaded. `origin` is not: the reader keeps it for its whole life, so it must be copied
// out before onBookLoaded clears the intent. RAM only. Free of Arduino so test/ui_layout can
// exercise route().
struct ReaderEntryIntent {
  enum class Kind : uint8_t { None, OpenAt, GoTo, Search, Tags, OpenHref };
  enum class Route : uint8_t { None, Locate, ChapterGrid, TocList, Search, Highlights, Href };

  // The longest href in the NWT's preaching section is 45 bytes.
  static constexpr size_t HREF_BYTES = 64;

  Kind kind = Kind::None;
  // OpenAt only. The unit, not an offset: the reader resolves it in whichever edition is open.
  study::Unit unit{};
  uint16_t spineHint = 0;
  // OpenHref only: a link as the publication's markup has it, NUL-terminated.
  char href[HREF_BYTES] = {};
  // Where in Predicación the reader was opened from, for its way back.
  PreachingOrigin origin{};

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

  // False, leaving `out` untouched, for an empty href or one too long for HREF_BYTES with its NUL.
  static bool openHref(const std::string_view link, const PreachingOrigin& from, ReaderEntryIntent& out) {
    if (link.empty() || link.size() >= HREF_BYTES) return false;
    ReaderEntryIntent intent;
    intent.kind = Kind::OpenHref;
    std::memcpy(intent.href, link.data(), link.size());
    intent.href[link.size()] = '\0';
    intent.origin = from;
    out = intent;
    return true;
  }

  // Places, hrefs, verse search and tags exist only in the Bible; outside it the grid is the TOC list.
  static constexpr Route route(const Kind kind, const bool isBible) {
    switch (kind) {
      case Kind::OpenAt:
        return isBible ? Route::Locate : Route::None;
      case Kind::GoTo:
        return isBible ? Route::ChapterGrid : Route::TocList;
      case Kind::Search:
        return isBible ? Route::Search : Route::None;
      case Kind::Tags:
        return isBible ? Route::Highlights : Route::None;
      case Kind::OpenHref:
        return isBible ? Route::Href : Route::None;
      case Kind::None:
        return Route::None;
    }
    return Route::None;
  }
};
