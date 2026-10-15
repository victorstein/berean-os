#include "PreachingTopicPickerActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/ActivityResult.h"
#include "activities/PostedMessage.h"
#include "study/StudyStore.h"

namespace {

constexpr const char* MODULE = "PREACH";

// The four-iterator form: the three-iterator one compares only `a`'s length, so dropping the
// last pick would read as unchanged.
bool sameIds(const std::vector<study::TagId>& a, const std::vector<study::TagId>& b) {
  return std::is_permutation(a.begin(), a.end(), b.begin(), b.end());
}

}  // namespace

PreachingTopicPickerActivity::PreachingTopicPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : TagChipGridActivity("PreachingTopicPicker", renderer, mappedInput) {}

void PreachingTopicPickerActivity::onEnter() {
  TagChipGridActivity::onEnter();
  topicsFile.load();
  seedIds = TagChips::selectedInOrder(STUDY.palette().activeIds(), topicsFile.tags());
  {
    RenderLock lock(*this);
    selectedIds = seedIds;
    selectedIds.reserve(STUDY.palette().activeCount());
  }
  rebuildChips();
}

void PreachingTopicPickerActivity::loop() {
  // TagChipGridActivity::onBackButton is final and cancels; this screen's left-edge swipe saves.
  // While the retire confirmation is open, Back stays the popup's.
  if (!retireConfirmationOpen() && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveAndReturn();
    return;
  }
  TagChipGridActivity::loop();
}

const char* PreachingTopicPickerActivity::headerTitle() const { return tr(STR_PREACHING_CHOOSE_TOPICS); }

bool PreachingTopicPickerActivity::isSelected(const study::TagId id) const {
  return std::find(selectedIds.begin(), selectedIds.end(), id) != selectedIds.end();
}

void PreachingTopicPickerActivity::buildChips(std::vector<TagChipView::ChipEntry>& out) {
  const std::vector<study::TagId> ids = STUDY.palette().activeIdsByName();
  out.reserve(ids.size());
  for (const study::TagId id : ids) {
    TagChipView::ChipEntry chip;
    chip.kind = TagChips::Kind::Tag;
    chip.id = id;
    snprintf(chip.label, sizeof(chip.label), "%s", STUDY.palette().name(id).c_str());
    out.push_back(chip);
  }
}

TagChips::ChipState PreachingTopicPickerActivity::chipState(const TagChipView::ChipEntry& chip) const {
  return isSelected(chip.id) ? TagChips::ChipState::Active : TagChips::ChipState::Inactive;
}

void PreachingTopicPickerActivity::activateIndex(const int index) {
  if (retireConfirmationOpen()) return;
  if (index < 0 || index >= listCount()) return;
  const study::TagId id = chips()[static_cast<size_t>(index)].id;
  // A toggle's feedback is its own repaint; a flash must never draw over it.
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    const auto picked = std::find(selectedIds.begin(), selectedIds.end(), id);
    if (picked == selectedIds.end()) {
      selectedIds.push_back(id);
    } else {
      selectedIds.erase(picked);
    }
  }
  requestUpdate();
}

void PreachingTopicPickerActivity::onTagRetired(const study::TagId id) {
  RenderLock lock(*this);
  selectedIds.erase(std::remove(selectedIds.begin(), selectedIds.end(), id), selectedIds.end());
}

void PreachingTopicPickerActivity::onHome() {
  saveAndReturn();
  // Only in this order: goHome() turns finish()'s pending pop into a replace, while a pop requested
  // after goHome() would drop the pending Home screen.
  activityManager.goHome();
}

void PreachingTopicPickerActivity::saveAndReturn() {
  std::vector<study::TagId> picks = TagChips::selectedInOrder(STUDY.palette().activeIdsByName(), selectedIds);
  if (!sameIds(picks, seedIds)) {
    const PreachingTopicsFile::SaveResult saved = topicsFile.save(picks);
    if (saved != PreachingTopicsFile::SaveResult::Ok) {
      LOG_ERR(MODULE, "Topics not saved (result %u)", static_cast<unsigned>(saved));
      PostedMessage::post(tr(STR_PREACHING_SAVE_FAILED));
      picks = TagChips::selectedInOrder(STUDY.palette().activeIdsByName(), topicsFile.tags());
    }
  }
  app.clearTapFlash();
  TagSelectionResult result;
  result.tagIds = std::move(picks);
  setResult(std::move(result));
  finish();
}
