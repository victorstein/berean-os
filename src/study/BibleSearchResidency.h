#pragma once

#include <BibleSearch/IndexFormat.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// What the search store keeps between visits to the search screen, decided apart
// from Epub and the card so the host suite can test it.
namespace BibleSearchResidency {

// Keeping the ~444 KB term index is a bet that search opens again soon. Below
// these floors the room is worth more. They cover the catalog update check, the
// largest PSRAM user reachable while search is closed: it holds the current
// index while it loads and inflates a staged one, 2.5 MiB by CatalogIndexStore's
// budgets. Only the screen's exit checks them; nothing asks the store to let go
// when a later allocation fails.
inline constexpr size_t MIN_FREE_PSRAM = 3u * 1024u * 1024u;
inline constexpr size_t MIN_LARGEST_PSRAM_BLOCK = 1024u * 1024u;

constexpr bool keepAfterExit(const size_t freePsram, const size_t largestPsramBlock) {
  return freePsram >= MIN_FREE_PSRAM && largestPsramBlock >= MIN_LARGEST_PSRAM_BLOCK;
}

// A kept reader may serve again only while bible.idx still starts with the
// header it was opened on and still has its size. writeHeader leaves no byte
// unwritten, so comparing bytes compares every field.
inline bool heldIndexMatches(const BibleSearch::IndexHeader& held, const uint8_t* raw, const size_t rawLength,
                             const uint32_t fileSize) {
  if (rawLength < BibleSearch::INDEX_HEADER_BYTES || fileSize != held.fileSize) return false;
  uint8_t expected[BibleSearch::INDEX_HEADER_BYTES];
  BibleSearch::writeHeader(expected, held);
  return memcmp(expected, raw, sizeof(expected)) == 0;
}

}  // namespace BibleSearchResidency
