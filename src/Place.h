#pragma once

#include <StudyStore/Unit.h>

#include <cstdint>
#include <string>

// One entry in /.berean/places.json: a Bible chapter the reader left, with the verse at the top of
// the page. Free of <Arduino.h> so src/util/PlacesDoc.h can be host-tested, as src/RecentBook.h is.
struct Place {
  study::Unit unit;  // Verse kind: book 1-66, major = chapter, minor = verse
  std::string reference;
  // The page began before the chapter's first verse, so the place is the chapter, not a verse.
  bool chapterOnly = false;
  // Hints into the edition it was recorded in; the unit is what survives an edition change.
  uint16_t spineIndex = 0;
  uint32_t visibleTextOffset = 0;

  bool operator==(const Place&) const = default;
};
