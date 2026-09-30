#pragma once

#include <Epub/BibleNavScanner.h>

#include <string>
#include <string_view>

// Resolves Bible nav link targets to spine indices without walking the spine from item 0. Free of
// Arduino and Epub so the host suite can exercise it (test/number_grid).
//
// A slot counts as unset while it is negative. Both functions fill every unset slot whose target
// matches, so duplicate targets all resolve, as Epub::resolveFilenamesToSpineIndices does.
namespace SpineSearch {

// The href of spine item `spineIndex`. `scratch` may own the returned bytes.
using HrefAt = std::string_view (*)(const void* ctx, int spineIndex, std::string& scratch);

// Walks [first, spineCount) then [0, first), stopping once no slot is unset. A `first` outside
// the spine starts at 0.
inline void resolveFrom(const std::string* targets, int* out, const int count, int first, const int spineCount,
                        const HrefAt hrefAt, const void* ctx) {
  int unset = 0;
  for (int i = 0; i < count; i++) {
    if (out[i] < 0) unset++;
  }
  if (unset == 0 || spineCount <= 0) return;
  if (first < 0 || first >= spineCount) first = 0;

  std::string scratch;
  for (int step = 0; step < spineCount && unset > 0; step++) {
    const int spineIndex = (first + step) % spineCount;
    const std::string_view tail = BibleNav::filenameTail(hrefAt(ctx, spineIndex, scratch));
    for (int j = 0; j < count; j++) {
      if (out[j] >= 0 || std::string_view(targets[j]) != tail) continue;
      out[j] = spineIndex;
      unset--;
    }
  }
}

// Takes one TOC entry's build-time spine index for every unset target naming the same file.
// book.bin resolves a TOC entry's spine when it is built, so this costs no spine reads.
inline void takeTocSpine(const std::string* targets, int* out, const int count, const std::string_view tocHref,
                         const int tocSpine, const int spineCount) {
  if (tocSpine < 0 || tocSpine >= spineCount) return;
  const std::string_view tail = BibleNav::filenameTail(tocHref);
  for (int j = 0; j < count; j++) {
    if (out[j] < 0 && std::string_view(targets[j]) == tail) out[j] = tocSpine;
  }
}

}  // namespace SpineSearch
