#include "TagFilterActivity.h"

#include <I18n.h>

#include <cstdint>
#include <utility>

#include "activities/ActivityResult.h"

TagFilterActivity::TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::optional<std::vector<size_t>> scope, const std::optional<study::TagId> filter)
    : TagChipGridActivity("TagFilter", renderer, mappedInput), scope_(std::move(scope)), filter_(filter) {}

void TagFilterActivity::onEnter() {
  TagChipGridActivity::onEnter();
  rebuildChips(TagChips::filterKey(filter_));
}

const char* TagFilterActivity::headerTitle() const { return tr(STR_FILTER_BY_TAG); }

void TagFilterActivity::buildChips(std::vector<TagChipView::ChipEntry>& out) {
  TagChipView::buildEntries(scope_, filter_, SIZE_MAX, out);
}

TagChips::ChipState TagFilterActivity::chipState(const TagChipView::ChipEntry& chip) const {
  return TagChipView::isSelected(chip, filter_) ? TagChips::ChipState::Active : TagChips::ChipState::Inactive;
}

void TagFilterActivity::activateIndex(const int index) {
  if (retireConfirmationOpen()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  TagSelectionResult result;
  // "All" returns an empty selection; every other chip reports a tag ID, never
  // an index -- the caller holds that filter across screens where the palette
  // may be edited. Unlabelled's id is study::UNLABELLED.
  const TagChipView::ChipEntry& chip = chips()[static_cast<size_t>(index)];
  if (chip.kind != TagChips::Kind::All) result.tagIds.push_back(chip.id);
  setResult(std::move(result));
  finish();
}

void TagFilterActivity::onTagRetired(const study::TagId id) {
  if (filter_ != id) return;
  // A filter on a retired tag has no chip; fall back to All so one chip stays inverted, as
  // HighlightsActivity::dropRetiredFilter does on return.
  RenderLock lock(*this);
  filter_.reset();
}

void TagFilterActivity::onHome() {
  // Cancelling matches Back.
  onBackButton();
}
