#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Sets a passage's text in the space the study sleep screen leaves for it. Free
// of Arduino and the renderer -- widths come through MeasureFn -- so
// test/study_sleep_pick runs it on the host.
namespace study_sleep {

inline constexpr char FIT_ELLIPSIS[] = "\xE2\x80\xA6";

// Pixel width of `text` set at candidate size `sizeIndex`.
using MeasureFn = int (*)(const void* ctx, uint8_t sizeIndex, const char* text);

struct FitSize {
  int lineHeight;
};

struct FitResult {
  uint8_t sizeIndex = 0;
  std::vector<std::string> lines;
  bool ellipsized = false;
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
// wide for any line gets one to itself, whole -- it is never cut.
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

// The largest size, `sizes` being largest first, at which every line fits
// maxWidth and all of them fit maxHeight. When none does, the smallest size
// keeps as many lines as fit -- never fewer than one -- and ends the last kept
// word with an ellipsis. Words are never split and the text always starts at its
// first word.
inline FitResult fitPassage(const std::string_view text, const FitSize* const sizes, const uint8_t sizeCount,
                            const int maxWidth, const int maxHeight, const MeasureFn measure, const void* const ctx) {
  FitResult result;
  if (sizes == nullptr || sizeCount == 0) return result;
  const auto words = fit_detail::splitWords(text);
  if (words.empty()) return result;

  fit_detail::Wrapped wrapped;
  for (uint8_t size = 0; size < sizeCount; ++size) {
    wrapped = fit_detail::wrap(words, size, maxWidth, measure, ctx);
    const bool fitsHeight = static_cast<long>(wrapped.spans.size()) * sizes[size].lineHeight <= maxHeight;
    if (fitsHeight && !wrapped.overflows) {
      result.sizeIndex = size;
      fit_detail::setLines(result, words, wrapped.spans);
      return result;
    }
  }

  result.sizeIndex = static_cast<uint8_t>(sizeCount - 1);
  const int lineHeight = sizes[result.sizeIndex].lineHeight;
  const size_t maxLines = (lineHeight > 0 && maxHeight >= lineHeight) ? static_cast<size_t>(maxHeight / lineHeight) : 1;
  if (wrapped.spans.size() <= maxLines) {
    fit_detail::setLines(result, words, wrapped.spans);
    return result;
  }

  wrapped.spans.resize(maxLines);
  auto& last = wrapped.spans.back();
  while (last.end - last.begin > 1 &&
         measure(ctx, result.sizeIndex, (fit_detail::join(words, last) + FIT_ELLIPSIS).c_str()) > maxWidth) {
    --last.end;
  }
  fit_detail::setLines(result, words, wrapped.spans);
  result.lines.back() += FIT_ELLIPSIS;
  result.ellipsized = true;
  return result;
}

}  // namespace study_sleep
