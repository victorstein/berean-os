#pragma once

#include <StudyStore/TagPalette.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Counting and two-line wrap for the Highlights tag chip row. Free of FreeInkUI, Arduino and
// GfxRenderer so the host suite can exercise it (test/tag_rows).
namespace TagChips {

inline bool passageMatches(const std::vector<study::TagId>& tags, const std::optional<study::TagId> filter) {
  if (!filter) return true;
  return std::find(tags.begin(), tags.end(), *filter) != tags.end();
}

struct Counts {
  size_t all = 0;
  size_t unlabelled = 0;
  // Aligned with the activeIds passed to count().
  std::vector<uint16_t> perTag;
};

namespace detail {

class Tally {
 public:
  explicit Tally(const std::vector<study::TagId>& activeIds) {
    out.perTag.assign(activeIds.size(), 0);
    // Sorted (raw id, slot) so each carried id is a binary search, not a scan of the palette.
    slots.reserve(activeIds.size());
    for (size_t i = 0; i < activeIds.size(); ++i) {
      slots.emplace_back(study::toRaw(activeIds[i]), static_cast<uint16_t>(i));
    }
    std::sort(slots.begin(), slots.end());
  }

  void add(const std::vector<study::TagId>& tags) {
    ++out.all;
    for (const study::TagId id : tags) {
      if (id == study::UNLABELLED) {
        ++out.unlabelled;
        continue;
      }
      const uint16_t raw = study::toRaw(id);
      const auto it = std::lower_bound(slots.begin(), slots.end(), std::make_pair(raw, uint16_t{0}));
      if (it == slots.end() || it->first != raw) continue;
      if (out.perTag[it->second] < UINT16_MAX) ++out.perTag[it->second];
    }
  }

  Counts out;

 private:
  std::vector<std::pair<uint16_t, uint16_t>> slots;
};

}  // namespace detail

// Agrees with passageMatches only because PassageDoc stores each passage's tags without duplicates
// and UNLABELLED only alone (normaliseTags); EveryCountEqualsTheRowsItsFilterShows pins that.
template <typename Passages>
Counts count(const Passages& passages, const std::vector<study::TagId>& activeIds) {
  detail::Tally tally(activeIds);
  for (const auto& passage : passages) tally.add(passage.tags);
  return std::move(tally.out);
}

// Counts only passages[i] for each i in scope, so a chapter-scoped list's chips count the rows that
// list can show. Out-of-range indices are skipped, as the list skips them.
template <typename Passages>
Counts countIn(const Passages& passages, const std::vector<size_t>& scope, const std::vector<study::TagId>& activeIds) {
  detail::Tally tally(activeIds);
  for (const size_t i : scope) {
    if (i < passages.size()) tally.add(passages[i].tags);
  }
  return std::move(tally.out);
}

constexpr int MAX_LINES = 2;
// Chip hit rects share UiAppHost's 64-interaction table with the visible passage rows; past it,
// hits are dropped silently and the chip becomes untappable.
constexpr int MAX_CHIPS = 24;

enum class Kind : uint8_t { All, Tag, Unlabelled };

struct Placed {
  int chip = 0;  // index into the widths passed to layout(), or -1 for the ellipsis
  int line = 0;
  int x = 0;
  int width = 0;
};

struct Layout {
  Placed placed[MAX_CHIPS + 1];
  int placedCount = 0;
  int lines = 0;
  bool overflow = false;
};

// Fills a caller-owned Layout: returning ~400 B by value would put a temporary on the render
// task's stack.
inline void layout(const int* widths, const int count, const int moreWidth, const int lineWidth, const int gap,
                   Layout& out) {
  out.placedCount = 0;
  out.lines = 0;
  out.overflow = false;
  if (lineWidth <= 0) return;

  int line = 0;
  int x = 0;
  bool fitsAll = true;
  for (int i = 0; i < count; ++i) {
    const int width = std::clamp(widths[i], 0, lineWidth);
    if (x > 0 && x + width > lineWidth) {
      ++line;
      x = 0;
    }
    if (line >= MAX_LINES || out.placedCount >= MAX_CHIPS) {
      fitsAll = false;
      break;
    }
    out.placed[out.placedCount++] = Placed{i, line, x, width};
    x += width + gap;
  }

  if (!fitsAll) {
    const int more = std::clamp(moreWidth, 0, lineWidth);
    // Drop chips from the end until the ellipsis fits after the last one kept.
    while (true) {
      int moreLine = 0;
      int moreX = 0;
      if (out.placedCount > 0) {
        const Placed& last = out.placed[out.placedCount - 1];
        moreLine = last.line;
        moreX = last.x + last.width + gap;
        if (moreX + more > lineWidth) {
          ++moreLine;
          moreX = 0;
        }
      }
      if (moreLine < MAX_LINES) {
        out.placed[out.placedCount++] = Placed{-1, moreLine, moreX, more};
        out.overflow = true;
        break;
      }
      --out.placedCount;
    }
  }

  out.lines = out.placedCount > 0 ? out.placed[out.placedCount - 1].line + 1 : 0;
}

inline int lineTop(const int line, const int chipHeight, const int gap) { return line * (chipHeight + gap); }

inline int bandHeight(const int lines, const int chipHeight, const int gap) {
  return lines > 0 ? lines * chipHeight + (lines - 1) * gap : 0;
}

struct Pad {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
};

// Splits each gap between the two chips facing across it, so hit rects tile without overlapping.
inline Pad hitPadding(const Layout& layout, const int index, const int gap) {
  Pad pad;
  const Placed& p = layout.placed[index];
  const int before = gap / 2;
  const int after = gap - before;
  if (p.x > 0) pad.left = before;
  if (index + 1 < layout.placedCount && layout.placed[index + 1].line == p.line) pad.right = after;
  if (p.line > 0) pad.top = before;
  if (p.line + 1 < layout.lines) pad.bottom = after;
  return pad;
}

}  // namespace TagChips
