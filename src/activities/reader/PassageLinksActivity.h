#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// One passage's outgoing links. Tapping a row resolves the target through
// StudyStore::locateLink and returns the same ProgressChangeResult
// HighlightsActivity's own jump does, which HighlightsActivity passes up to the
// reader unchanged. A target that does not resolve says so and stays here: a
// deleted target passage is fine (the link is to an address, not a passage),
// but an address this publication does not contain has nowhere to go.
//
// A long-press removes a link after a confirmation; it is the only way to free
// a slot under PassageDoc::MAX_LINKS_PER_PASSAGE.
class PassageLinksActivity final : public UiListActivity {
 public:
  PassageLinksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, size_t passageIndex);

 private:
  // Both exits MUST set a result, for the reason TagFilterActivity gives.
  void onBackButton() override;
  bool handleHomeGesture() override;
  void onRowLongPress(int row) override;
  bool handleCustomInput() override;
  void render(RenderLock&&) override;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void refreshLabels();
  void showRemoveConfirmation(size_t linkIndex);
  void removeLink(size_t linkIndex);

  const size_t passageIndex_;
  // Snapshot of the labels; rowItems_ borrows their pointers, and a removal
  // moves the store's strings.
  std::vector<std::string> labels_;
  std::vector<freeink::ui::ListItem> rowItems_;

  bool confirmingRemove_ = false;
  OptionPopup confirmPopup_;
  size_t pendingRemoveIndex_ = 0;
};
