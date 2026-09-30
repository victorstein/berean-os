#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "activities/launcher/LauncherBible.h"

// Finds the Bible on the card, for Home and for a reader outside the Bible.
// The rules and their order live in LauncherBible.h; this is the card I/O.
namespace BibleFinder {

struct Found {
  std::string path;
  BibleLookup by;
};

// Registry, then the CDN's name on the card, then a guess over recents, which
// must already be loaded. `exclude` is skipped by the recents guess: the open
// book always heads recents, and a non-Bible titled "New World" there would
// otherwise hide a real Bible further down.
std::optional<Found> find(std::string_view exclude = {});

}  // namespace BibleFinder
