#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VerseAnchors {
class Scanner;
}

// Extracts each verse's visible text from one NWT-shaped Bible spine document,
// for indexing and for the result rows.
//
// Shaped like VerseAnchors::Scanner: chunk-fed, expat behind an opaque pointer,
// no HalStorage, host-testable. It runs a VerseAnchors::Scanner over the same
// bytes and pairs its anchors with the verses in order, so anchorOffset is the
// reader's own offset rather than a second count that could drift from it.
namespace BibleSearch {

struct VerseTextScannerState;

// A malformed page cannot grow memory without bound. The longest real verse
// (Esther 8:9) is under 500 bytes.
inline constexpr size_t MAX_VERSE_TEXT_BYTES = 4096;

struct VerseText {
  uint16_t chapter;
  uint16_t verse;
  uint32_t anchorOffset;  // VerseAnchors' visible-codepoint offset of the verse marker
  std::string text;       // visible verse text, raw (unfolded), whitespace-collapsed
};

// Where the scanner's own expat parser allocates, in expat's malloc/realloc/
// free shape. Null means expat's default. A host test passes one that fails,
// which is the only way to reach the out-of-memory path off the device.
struct ParserMemory {
  void* (*allocate)(size_t bytes);
  void* (*reallocate)(void* block, size_t bytes);
  void (*release)(void* block);
};

class VerseTextScanner {
 public:
  explicit VerseTextScanner(const ParserMemory* memory = nullptr);
  ~VerseTextScanner();
  VerseTextScanner(const VerseTextScanner&) = delete;
  VerseTextScanner& operator=(const VerseTextScanner&) = delete;

  bool valid() const { return parser_ != nullptr; }
  // Returns false once the document is malformed; the caller should stop and
  // discard. `isFinal` marks the last chunk.
  bool feed(const char* chunk, size_t length, bool isFinal);
  // True when a failed feed ran out of memory rather than met bad markup. The
  // two call for opposite handling: bad markup fails the same way every time,
  // while out of memory says nothing about the document.
  bool outOfMemory() const { return outOfMemory_; }
  // Verses completed so far, in document order; moved out. The last verse is
  // complete once the footnote section opens or the final chunk is fed. Empty
  // after a failed feed.
  std::vector<VerseText> take();
  // Visible text before the first verse marker, which would belong to the
  // previous document's last verse. Navigation, headings and superscriptions
  // are not verse text and never appear here. Moved out; empty after a failed
  // feed.
  std::string continuation();

 private:
  void* parser_ = nullptr;  // XML_Parser; opaque here to keep expat out of the header
  std::unique_ptr<VerseTextScannerState> state_;
  std::unique_ptr<VerseAnchors::Scanner> anchors_;
  bool failed_ = false;
  bool outOfMemory_ = false;
};

}  // namespace BibleSearch
