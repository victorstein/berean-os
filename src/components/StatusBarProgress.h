#pragma once

// Which whole-book progress the reader status bar may draw. Plain bools and free of
// CrossPointSettings, Arduino and GfxRenderer so the host suite can exercise it
// (test/ui_layout).
namespace StatusBarProgress {

struct Inputs {
  bool showWholeBookProgress = true;  // false in a Bible: a reference, not a book to finish
  bool showBookPercent = false;       // StatusBarSpec::showBookProgressPercent
  bool barTracksBook = false;         // StatusBarSpec::progressBarMode == BOOK_PROGRESS
};

struct View {
  bool showBookPercent = false;
  bool barTracksBook = false;  // false: the bar, if shown, tracks the chapter
};

constexpr View resolve(const Inputs& in) {
  return View{.showBookPercent = in.showBookPercent && in.showWholeBookProgress,
              .barTracksBook = in.barTracksBook && in.showWholeBookProgress};
}

}  // namespace StatusBarProgress
