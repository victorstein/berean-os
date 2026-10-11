#pragma once

#include <cstdint>

// Whether a reader-menu close may show the page the reader saved instead of rendering it again.
// Free of firmware includes so the host suite can exercise it (test/ui_layout).
namespace MenuCloseRestore {

// What the menu could have changed, as its result handler sees it.
struct Close {
  bool cancelled = false;
  bool orientationChanged = false;
  bool autoTurnBefore = false;
  bool autoTurnAfter = false;
  bool nightModeBefore = false;
  bool nightModeAfter = false;
  bool atEndOfBook = false;
};

enum class Verdict : uint8_t { Restore, Action, EndOfBook, Orientation, AutoTurn, NightMode };

// The order only decides which reason is logged; any of them blocks a restore.
constexpr Verdict decide(const Close& close) {
  if (!close.cancelled) return Verdict::Action;
  if (close.atEndOfBook) return Verdict::EndOfBook;
  if (close.orientationChanged) return Verdict::Orientation;
  // Every close re-applies the menu's auto-turn option, which starts at off, so auto turn on at either
  // end changes the status bar.
  if (close.autoTurnBefore || close.autoTurnAfter) return Verdict::AutoTurn;
  if (close.nightModeBefore || close.nightModeAfter) return Verdict::NightMode;
  return Verdict::Restore;
}

constexpr const char* name(const Verdict verdict) {
  switch (verdict) {
    case Verdict::Restore:
      return "restore";
    case Verdict::Action:
      return "action";
    case Verdict::EndOfBook:
      return "end-of-book";
    case Verdict::Orientation:
      return "orientation";
    case Verdict::AutoTurn:
      return "auto-turn";
    case Verdict::NightMode:
      return "night";
  }
  return "";
}

// What the reader saved of the last page it rendered. Every change goes through these methods, so a
// page can never keep planes saved for the one before it.
struct CapturedPage {
  bool bw = false;
  bool grayLsb = false;
  bool grayMsb = false;
  bool grayExpected = false;
  bool grayShown = false;
  int spineIndex = -1;
  int pageIndex = -1;

  void reset() { *this = CapturedPage{}; }
  void beginPage(const int spine, const int page, const bool expectGray) {
    reset();
    bw = true;
    spineIndex = spine;
    pageIndex = page;
    grayExpected = expectGray;
  }
  void markPlane(const bool lsbPlane) { (lsbPlane ? grayLsb : grayMsb) = true; }
  void markGrayShown() { grayShown = true; }
};

// Where the reader is when the close is handled.
struct Position {
  bool hasSection = false;
  int spineIndex = -1;
  int pageIndex = -1;
};

enum class Fit : uint8_t { Fits, Empty, NoSection, Moved, GrayMissing };

constexpr Fit fit(const CapturedPage& page, const Position& at) {
  if (!page.bw) return Fit::Empty;
  if (!at.hasSection) return Fit::NoSection;
  if (page.spineIndex != at.spineIndex || page.pageIndex != at.pageIndex) return Fit::Moved;
  // A gray pass that was skipped, or not saved, is retried by a re-render rather than left flat.
  if (page.grayExpected && !(page.grayShown && page.grayLsb && page.grayMsb)) return Fit::GrayMissing;
  return Fit::Fits;
}

constexpr bool restoresGray(const CapturedPage& page) { return page.grayExpected; }

constexpr const char* name(const Fit result) {
  switch (result) {
    case Fit::Fits:
      return "fits";
    case Fit::Empty:
      return "empty";
    case Fit::NoSection:
      return "no-section";
    case Fit::Moved:
      return "moved";
    case Fit::GrayMissing:
      return "gray-missing";
  }
  return "";
}

}  // namespace MenuCloseRestore
