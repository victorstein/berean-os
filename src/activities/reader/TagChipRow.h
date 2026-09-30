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
// Chip hit rects share UiAppHost's 96-interaction table (UiAppHost::MAX_INTERACTIONS) with the
// visible passage rows; past it, hits are dropped and the chip becomes untappable.
constexpr int MAX_CHIPS = 24;

enum class Kind : uint8_t { All, Tag, Unlabelled };

struct Candidate {
  Kind kind = Kind::All;
  // Index into the activeIds (and Counts::perTag) the chips were counted with; Kind::Tag only.
  uint16_t slot = 0;
};

// The chips to offer, in display order: All, each active tag with a non-zero count, then
// Unlabelled when non-zero. A zero-count chip stays while it is the active filter, so an empty list
// still shows why (the palette is global, so most zeros are tags of other publications or chapters).
inline void candidates(const Counts& counts, const std::vector<study::TagId>& activeIds,
                       const std::optional<study::TagId> filter, const size_t cap, std::vector<Candidate>& out) {
  out.clear();
  if (cap == 0) return;
  out.reserve(std::min(cap, activeIds.size() + 2));
  out.push_back(Candidate{Kind::All, 0});
  const size_t slots = std::min(activeIds.size(), counts.perTag.size());
  for (size_t slot = 0; slot < slots; ++slot) {
    if (counts.perTag[slot] == 0 && filter != activeIds[slot]) continue;
    if (out.size() >= cap) return;
    out.push_back(Candidate{Kind::Tag, static_cast<uint16_t>(slot)});
  }
  if (out.size() >= cap) return;
  if (counts.unlabelled > 0 || filter == study::UNLABELLED) out.push_back(Candidate{Kind::Unlabelled, 0});
}

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

// The chip itself meets the touch minimum; its hit rect is never grown past it, because that would
// overlap the next line (hitPadding tiles the gaps instead).
inline int chipHeight(const int textLineHeight, const int padY, const int minTouch) {
  return std::max(textLineHeight + 2 * padY, minTouch);
}

// The ellipsis screen's grid pages at line boundaries. A page stays well under UiAppHost's 96
// interactions, leaving room for the chrome, as NumberGrid::MAX_CELLS = 70 does.
constexpr int GRID_MAX_CHIPS = 64;

inline int linesPerPage(const int bodyHeight, const int chipHeight, const int gap) {
  if (chipHeight <= 0) return 1;
  return std::max(1, (bodyHeight + gap) / (chipHeight + gap));
}

// One page of the grid. ~1 KB: keep it a member, never a render-task local.
struct GridPage {
  Placed placed[GRID_MAX_CHIPS];
  int placedCount = 0;
  // Counted from the top of this page, as is each Placed::line.
  int lines = 0;
  int first = 0;
};

namespace detail {

// Greedy wrap of widths[first..] into at most maxLines lines and GRID_MAX_CHIPS chips; returns the
// index of the first chip left over. The first chip always fits, so paging always advances.
inline int placePage(const int* widths, const int count, const int first, const int lineWidth, const int gap,
                     const int maxLines, GridPage* out) {
  if (out) {
    out->placedCount = 0;
    out->lines = 0;
    out->first = first;
  }
  if (first >= count || lineWidth <= 0) return count;

  const int lineCap = std::max(maxLines, 1);
  int line = 0;
  int x = 0;
  int placed = 0;
  int i = first;
  for (; i < count; ++i) {
    const int width = std::clamp(widths[i], 0, lineWidth);
    if (x > 0 && x + width > lineWidth) {
      ++line;
      x = 0;
    }
    if (line >= lineCap || placed >= GRID_MAX_CHIPS) break;
    if (out) out->placed[placed] = Placed{i, line, x, width};
    ++placed;
    x += width + gap;
  }
  if (out) {
    out->placedCount = placed;
    out->lines = placed > 0 ? out->placed[placed - 1].line + 1 : 0;
  }
  return i;
}

}  // namespace detail

inline int nextPageStart(const int* widths, const int count, const int first, const int lineWidth, const int gap,
                         const int maxLines) {
  return detail::placePage(widths, count, first, lineWidth, gap, maxLines, nullptr);
}

inline void layoutPage(const int* widths, const int count, const int first, const int lineWidth, const int gap,
                       const int maxLines, GridPage& out) {
  detail::placePage(widths, count, first, lineWidth, gap, maxLines, &out);
}

inline int pageCountOf(const int* widths, const int count, const int lineWidth, const int gap, const int maxLines) {
  int pages = 0;
  for (int first = 0; first < count; first = nextPageStart(widths, count, first, lineWidth, gap, maxLines)) ++pages;
  return pages;
}

struct PageSpan {
  int first = 0;
  int next = 0;
  int page = 0;
};

// The page holding `index`, clamped into [0, count). O(count): at most 202 chips, walked on a build
// or a button press, never per chip.
inline PageSpan pageHolding(const int* widths, const int count, const int index, const int lineWidth, const int gap,
                            const int maxLines) {
  PageSpan span;
  if (count <= 0) return span;
  const int target = std::clamp(index, 0, count - 1);
  while (true) {
    span.next = nextPageStart(widths, count, span.first, lineWidth, gap, maxLines);
    if (target < span.next || span.next >= count) return span;
    span.first = span.next;
    ++span.page;
  }
}

struct Pad {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
};

// Splits each gap between the two chips facing across it, so hit rects tile without overlapping.
// `lines` and each Placed::line count from the top of the placed block.
inline Pad hitPadding(const Placed* placed, const int placedCount, const int lines, const int index, const int gap) {
  Pad pad;
  const Placed& p = placed[index];
  const int before = gap / 2;
  const int after = gap - before;
  if (p.x > 0) pad.left = before;
  if (index + 1 < placedCount && placed[index + 1].line == p.line) pad.right = after;
  if (p.line > 0) pad.top = before;
  if (p.line + 1 < lines) pad.bottom = after;
  return pad;
}

inline Pad hitPadding(const Layout& layout, const int index, const int gap) {
  return hitPadding(layout.placed, layout.placedCount, layout.lines, index, gap);
}

}  // namespace TagChips
