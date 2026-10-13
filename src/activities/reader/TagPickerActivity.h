#pragma once

#include <StudyStore/TagPalette.h>

#include <string>
#include <vector>

#include "TagChipGridActivity.h"
#include "components/themes/BaseTheme.h"

// The tags of one passage, as a paged chip grid: + New tag, then every active tag alphabetically.
// A tag chip is Active when applied, Inactive when it can be added, and Disabled once the passage
// holds PassageDoc::MAX_TAGS_PER_PASSAGE; a tap on a Disabled chip explains the limit. Returns the
// chosen ids as a TagSelectionResult, in palette allocation order so a passage's stored order does
// not follow the display; the caller applies them. Ids that are not active are dropped on entry:
// they have no chip, and must not fill the cap.
//
// Done (in the header) and Home commit; Back cancels. Done is a touch target only: a button user
// commits with Home, which this board always has.
//
// + New tag creates the tag and persists the palette at once, before the passage is saved: the
// palette is global, so a passage edit cancelled afterwards must not take the new tag with it.
class TagPickerActivity final : public TagChipGridActivity {
 public:
  explicit TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::vector<study::TagId> initialSelection = {});

  void onEnter() override;

 private:
  static constexpr freeink::ui::ActionId ACTION_DONE = ACTION_USER;

  const char* headerTitle() const override;
  void drawChrome() override;
  void buildChips(std::vector<TagChipView::ChipEntry>& out) override;
  TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const override;
  void buildHeaderExtras(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTagRetired(study::TagId id) override;
  void onHome() override;

  static void onDoneEvent(const freeink::ui::ActionEvent& event, void* user);
  void startNewTagFlow();
  void reportAddTagFailure(const std::string& name);
  void commitAndFinish();
  bool isSelected(study::TagId id) const;
  void setSelected(study::TagId id, bool on);

  // Written under RenderLock: chipState and the title read it on the render task.
  std::vector<study::TagId> selectedIds_;
  // Render task only. headerTitle formats the title; drawChrome records where the theme put the
  // header and Done, and buildHeaderExtras draws Done there later in the same render.
  mutable char title_[32] = {};
  Rect headerBand_;
  Rect doneRect_;
};
