#pragma once

#include <StudyStore/TagPalette.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "TagChipRow.h"
#include "components/UiAppHost.h"

// Building, measuring and drawing tag chips, shared by the Highlights chip row and the
// TagFilterActivity grid so the two screens cannot drift in size or style.
namespace TagChipView {

// The label is owned here because ButtonProps borrows it during the render.
struct ChipEntry {
  TagChips::Kind kind = TagChips::Kind::All;
  study::TagId id = study::UNLABELLED;
  char label[40] = {};
};

// Counts over every passage (scope nullopt) or only the listed passage indices, then keeps
// TagChips::candidates' chips, at most `cap`. Resolves no units, so it is safe with or without
// RenderLock held; whatever the render task reads must still be published under the lock.
void buildEntries(const std::optional<std::vector<size_t>>& scope, std::optional<study::TagId> filter, size_t cap,
                  std::vector<ChipEntry>& out);

bool isSelected(const ChipEntry& chip, std::optional<study::TagId> filter);

struct Metrics {
  int padX = 0;
  int chipHeight = 0;
  int gap = 0;
};

Metrics metricsFor(UiAppHost::UiScreen& screen);

// Measured bold, the selected weight, so the selection never changes a width or reflows the chips.
int measure(UiAppHost::UiScreen& screen, const char* label, const Metrics& metrics);

void draw(UiAppHost::UiScreen& screen, const freeink::ui::Rect& rect, const char* label, bool selected,
          freeink::ui::ActionId action, int16_t value, uint16_t inputMask, const TagChips::Pad& pad);

}  // namespace TagChipView
