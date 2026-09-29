#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Sets a passage's text in the space the study sleep screen leaves for it --
// whole, or not at all. Free of Arduino and the renderer -- widths come through
// MeasureFn -- so test/study_sleep_pick runs it on the host.
namespace study_sleep {

// Pixel width of `text` set at font `sizeIndex`.
using MeasureFn = int (*)(const void* ctx, uint8_t sizeIndex, const char* text);

// One step of the size ladder: a font, its line height, and the height left for
// the passage once that step's chrome is laid out.
struct FitRung {
  uint8_t sizeIndex;
  int lineHeight;
  int maxHeight;
};

struct FitResult {
  bool fits = false;
  uint8_t rung = 0;  // index into the rungs passed; meaningful only when it fits
  std::vector<std::string> lines;
};

namespace fit_detail {

struct LineSpan {
  size_t begin;  // first word
  size_t end;    // one past the last word
};

struct Wrapped {
  std::vector<LineSpan> spans;
  bool overflows = false;  // a word alone is wider than the column
};

// U+0020 only, as GfxRenderer::wrappedText breaks: U+00A0 and U+202F in the
// publisher's markup must keep their words together.
inline std::vector<std::string_view> splitWords(const std::string_view text) {
  std::vector<std::string_view> words;
  words.reserve(text.size() / 4 + 1);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(' ', start);
    if (end == std::string_view::npos) end = text.size();
    if (end > start) words.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return words;
}

inline std::string join(const std::vector<std::string_view>& words, const LineSpan span) {
  std::string line;
  for (size_t i = span.begin; i < span.end; ++i) {
    if (i != span.begin) line.push_back(' ');
    line.append(words[i]);
  }
  return line;
}

// Greedy: each line takes words while it measures within maxWidth. A word too
// wide for any line marks the wrap as overflowing -- it is never cut.
inline Wrapped wrap(const std::vector<std::string_view>& words, const uint8_t sizeIndex, const int maxWidth,
                    const MeasureFn measure, const void* const ctx) {
  Wrapped wrapped;
  wrapped.spans.reserve(16);
  std::string line;
  size_t begin = 0;
  for (size_t i = 0; i < words.size(); ++i) {
    if (i != begin) {
      line.push_back(' ');
      line.append(words[i]);
      if (measure(ctx, sizeIndex, line.c_str()) <= maxWidth) continue;
      wrapped.spans.push_back({begin, i});
      begin = i;
    }
    line.assign(words[i]);
    if (measure(ctx, sizeIndex, line.c_str()) > maxWidth) wrapped.overflows = true;
  }
  wrapped.spans.push_back({begin, words.size()});
  return wrapped;
}

inline void setLines(FitResult& result, const std::vector<std::string_view>& words,
                     const std::vector<LineSpan>& spans) {
  result.lines.reserve(spans.size());
  for (const LineSpan span : spans) result.lines.push_back(join(words, span));
}

}  // namespace fit_detail

// The first rung, in the order given, at which every line fits maxWidth and all
// of them fit that rung's maxHeight. Words are never split and nothing is ever
// cut: a text that fits no rung returns fits == false and no lines.
inline FitResult fitPassage(const std::string_view text, const FitRung* const rungs, const uint8_t rungCount,
                            const int maxWidth, const MeasureFn measure, const void* const ctx) {
  FitResult result;
  if (rungs == nullptr || rungCount == 0) return result;
  const auto words = fit_detail::splitWords(text);
  if (words.empty()) return result;

  for (uint8_t rung = 0; rung < rungCount; ++rung) {
    const auto wrapped = fit_detail::wrap(words, rungs[rung].sizeIndex, maxWidth, measure, ctx);
    const bool fitsHeight = static_cast<long>(wrapped.spans.size()) * rungs[rung].lineHeight <= rungs[rung].maxHeight;
    if (fitsHeight && !wrapped.overflows) {
      result.fits = true;
      result.rung = rung;
      fit_detail::setLines(result, words, wrapped.spans);
      return result;
    }
  }
  return result;
}

}  // namespace study_sleep
