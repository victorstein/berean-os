#pragma once

#include <Epub.h>

#include <string>

#include "BibleBookIndex.h"
#include "ParkedBookOf.h"

// The firmware's one parked book (ParkedBookOf). The reader parks on exit and takes on load, both
// on the loop task, so the slot needs no lock of its own.
namespace ParkedBook {

using Slot = ParkedBookOf<Epub, BibleNavCache>;
using Held = Slot::Held;

// Parks `held` for `path`. With no book, or an EPUB that cannot be sized, it parks nothing and
// empties the slot.
void park(const std::string& path, Held held);
void clear();
// The parked book for `path` if it is still valid (ParkedBookOf::take), else nothing. Always
// leaves the slot empty.
Held take(const std::string& path, bool skipLoadingCss);

}  // namespace ParkedBook
