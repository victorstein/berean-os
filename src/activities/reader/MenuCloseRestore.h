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

}  // namespace MenuCloseRestore
