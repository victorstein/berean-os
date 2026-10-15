#pragma once

#include <StudyStore/TagPalette.h>

#include <vector>

#include "activities/reader/TagChipGridActivity.h"
#include "study/PreachingTopicsFile.h"

// Elegir temas: every active tag as a chip, the preaching topics Active. Multi-select, no tag
// creation; a long-press retires a tag as in every chip grid. The left-edge swipe saves and returns
// a TagSelectionResult holding the picks as they now stand on disk; Home saves the same way and
// goes Home. Leaving with the picks it opened with writes nothing.
//
// The chips are STUDY's palette, and a retire writes that palette over tags.json, so STUDY must be
// open on the Bible before this is pushed (PreachingActivity::ensureStudyOpen).
class PreachingTopicPickerActivity final : public TagChipGridActivity {
 public:
  PreachingTopicPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;

 private:
  const char* headerTitle() const override;
  void buildChips(std::vector<TagChipView::ChipEntry>& out) override;
  TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const override;
  void activateIndex(int index) override;
  void onTagRetired(study::TagId id) override;
  void onHome() override;

  void saveAndReturn();
  bool isSelected(study::TagId id) const;

  // Its own object: PreachingSession's is private. Loaded on enter, so its save latch lasts one
  // opening of the picker.
  PreachingTopicsFile topicsFile;
  std::vector<study::TagId> seedIds;
  // Written under RenderLock: chipState reads it on the render task.
  std::vector<study::TagId> selectedIds;
};
