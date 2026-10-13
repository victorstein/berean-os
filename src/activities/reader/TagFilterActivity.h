#pragma once

#include <StudyStore/TagPalette.h>

#include <cstddef>
#include <optional>
#include <vector>

#include "TagChipGridActivity.h"

// The highlights chip row's ellipsis: every chip the row would offer, uncapped, as a wrapping grid
// with counts, paged only when it outgrows one screen. Returns a TagSelectionResult holding no
// elements for "All" or exactly one id -- study::UNLABELLED for "Unlabelled"; a cancelled result
// means the caller keeps its current filter.
//
// Chips report a study::TagId, not an index. Ids are allocated once and never reused, so a
// retirement cannot renumber anything and the caller's captured filter stays valid.
class TagFilterActivity final : public TagChipGridActivity {
 public:
  // scope: nullopt counts every passage; otherwise only these passage indices, so the chapter view's
  // counts match the list it shows. Computed by the caller with no RenderLock held.
  TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::optional<std::vector<size_t>> scope,
                    std::optional<study::TagId> filter);

  void onEnter() override;

 private:
  const char* headerTitle() const override;
  void buildChips(std::vector<TagChipView::ChipEntry>& out) override;
  TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const override;
  void activateIndex(int index) override;
  void onTagRetired(study::TagId id) override;
  void onHome() override;

  const std::optional<std::vector<size_t>> scope_;
  // Written under RenderLock: chipState reads it on the render task.
  std::optional<study::TagId> filter_;
};
