#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Resolves a visible-codepoint offset inside one spine item to the numbered
// paragraph containing it. JW publications put `data-pid` on the block element
// itself (`<p id="p7" data-pid="7">`); `data-pnum` is a different thing -- the
// printed paragraph number on a <span class="parNum"> -- and is not an address.
//
// Three properties of real markup this relies on, measured across 180 documents
// of w_S_202601 and lff_S:
//   * data-pid elements never nest, so the anchors are a flat sequence and
//     find() can take the greatest anchor at or below an offset.
//   * pid values are unique within a document, so a pid is a valid address.
//   * pid order is NOT document order -- interleaved study-question boxes carry
//     high pids -- so this list is sorted by OFFSET and the pid is payload.
//
// Counting mirrors VerseAnchors: the same VisibleOffsetCounter and the same
// entity-expanding default handler, because a deficit of one codepoint names
// the previous paragraph.
namespace ParagraphAnchors {

struct ParagraphAnchor {
  uint32_t offset;
  uint16_t pid;
};

// Ascending by offset. Empty when the document has no data-pid, and also empty
// when the parse fails part-way: a truncated list would silently resolve later
// passages to a stale anchor with no way for the caller to notice.
std::vector<ParagraphAnchor> scan(const char* xhtml, size_t length);

// Chunk-fed form, so a caller can stream a spine item instead of holding it
// whole. Takes bytes rather than a file handle: HalStorage is firmware-only and
// this must stay host-testable.
class Scanner {
 public:
  Scanner();
  ~Scanner();
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;

  bool valid() const { return parser_ != nullptr; }
  bool feed(const char* chunk, size_t length, bool isFinal);
  std::vector<ParagraphAnchor> take();

 private:
  void* parser_ = nullptr;  // XML_Parser; opaque here to keep expat out of the header
  void* state_ = nullptr;   // State
  bool failed_ = false;
};

// The anchor covering `offset` -- the greatest one at or below it -- or nullptr.
const ParagraphAnchor* find(const std::vector<ParagraphAnchor>& anchors, uint32_t offset);

}  // namespace ParagraphAnchors
