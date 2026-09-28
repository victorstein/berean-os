#pragma once

#include <cstddef>
#include <string>

// The selected words of a passage, joined for storage. Free of Arduino and the
// renderer so test/passage_label runs it on the host.
namespace passage_label {

inline constexpr char ELLIPSIS[] = "\xE2\x80\xA6";
inline constexpr size_t ELLIPSIS_BYTES = sizeof(ELLIPSIS) - 1;

// Joins words with one space. A word that would leave no room for the ellipsis
// under `capacity` stops the text there: it and every later word are dropped,
// and text() ends the last kept word with the ellipsis, so a stored passage
// never ends mid-word and a cut one says so.
class Builder {
 public:
  explicit Builder(const size_t capacity) : capacity_(capacity) {}

  void reset() {
    text_.clear();
    truncated_ = false;
  }

  bool addWord(const char* word, const size_t length) {
    if (truncated_) return false;
    if (length == 0) return true;
    const size_t separator = text_.empty() ? 0 : 1;
    if (text_.size() + separator + length + ELLIPSIS_BYTES > capacity_) {
      truncated_ = true;
      return false;
    }
    if (separator != 0) text_.push_back(' ');
    text_.append(word, length);
    return true;
  }

  bool truncated() const { return truncated_; }

  std::string text() const { return truncated_ ? text_ + ELLIPSIS : text_; }

 private:
  size_t capacity_;
  std::string text_;
  bool truncated_ = false;
};

}  // namespace passage_label
