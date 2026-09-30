#pragma once

// Whether a forward page turn moves off the end of the current document. It is
// EpubReaderActivity::pageTurn's own branch condition, kept free of Arduino and
// the Epub library so test/auto_page_turn can exercise it.
constexpr bool forwardTurnLeavesDocument(const int currentPage, const int pageCount, const bool stillBuilding) {
  return !(currentPage < pageCount - 1 || stillBuilding);
}
