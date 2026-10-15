#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Reads one question file of the NWT intro section into blocks the Predicación
// cards lay out. Shaped like BibleNav::Scanner: chunk-fed, expat behind an
// opaque pointer, and free of Arduino and HalStorage so it stays host-testable.
namespace preaching {

// The largest real question is 33 blocks and under 2 KB of text; these bound
// memory against a malformed or unexpected file.
inline constexpr size_t MAX_BLOCKS = 64;
inline constexpr size_t MAX_TEXT_BYTES = 8 * 1024;

enum class BlockKind : uint8_t { Heading, Verse, Text };

struct Block {
  BlockKind kind = BlockKind::Text;
  // Heading and Text: the text. Verse: the quote, empty when the reference had none.
  std::string text;
  // Verse only: the reference as it reads, e.g. "Mateo 26:14, 15; 27:5".
  std::string reference;
  // Verse only: the first link's href, verbatim.
  std::string href;
};

struct Question {
  std::string title;
  std::vector<Block> blocks;
};

struct QuestionParserState;

class QuestionParser {
 public:
  QuestionParser();
  ~QuestionParser();
  QuestionParser(const QuestionParser&) = delete;
  QuestionParser& operator=(const QuestionParser&) = delete;

  bool valid() const { return parser_ != nullptr; }
  // The SpineHtmlStream::ChunkSink contract: false stops the pass, and every
  // later call returns false too. `isFinal` marks the last chunk, which may be
  // empty.
  bool feed(const char* chunk, size_t length, bool isFinal);
  // True when a failed feed ran out of memory rather than met bad markup or a bound.
  bool outOfMemory() const { return outOfMemory_; }
  // Moves the result into `out` once a final chunk was accepted, and only once.
  // Otherwise returns false and leaves `out` untouched: nothing partial.
  bool take(Question& out);

 private:
  void* parser_ = nullptr;  // XML_Parser; opaque here to keep expat out of the header
  std::unique_ptr<QuestionParserState> state_;
  bool failed_ = false;
  bool finished_ = false;
  bool taken_ = false;
  bool outOfMemory_ = false;
};

bool parseQuestion(const char* xhtml, size_t length, Question& out);

}  // namespace preaching
