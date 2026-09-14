#pragma once

#include <StudyStore/TagPalette.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "study/StudyStore.h"

// Single-select tag chooser for the highlights browser's filter row. Returns a
// TagSelectionResult holding no elements for "all tags" or exactly one for a
// specific tag; a cancelled result means the caller keeps its current filter.
//
// A long-press RETIRES a tag: it leaves the pickers and stops filtering, while
// every passage that carried it keeps its remaining tags and stays on the card.
// A destructive delete here would be device-wide now that the palette is
// global -- one tidy-up gesture would wipe work across every publication.
//
// Rows report a study::TagId, not an index. Ids are allocated once and never
// reused, so a retirement cannot renumber anything and the caller's captured
// filter stays valid -- which is what the index model could not promise.
class TagFilterActivity final : public UiListActivity {
 public:
  TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  // Both exits MUST set a result. UiListActivity::onBackButton is a bare
  // finish(), which leaves ActivityResult default-constructed: isCancelled
  // false and the variant holding monostate. A consumer that then reads its
  // own alternative calls std::get on the wrong one, and with -fno-exceptions
  // that aborts.
  void onBackButton() override;
  bool handleHomeGesture() override;
  // Long-press a tag row to delete it from the palette. Book-wide, like the
  // picker's own delete, so it is confirmed first.
  void onRowLongPress(int row) override;
  bool handleCustomInput() override;
  void render(RenderLock&&) override;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void showRetireConfirmation(size_t tagRow);
  void retireTag(size_t tagRow);

  // Snapshot taken when the screen is built. rowItems_ borrows label pointers
  // from these strings, so they must outlive the row list and must not be a view
  // into the palette, which an edit can move.
  std::vector<StudyStore::TagView> tags_;

  bool confirmingDelete_ = false;
  OptionPopup confirmPopup_;
  // Captured when the confirmation opens so the callback deletes the row that
  // was long-pressed, not whatever nav.selected became by the time it resolves.
  size_t pendingRetireRow_ = 0;
  // Sized to the live palette rather than MAX_TAGS: the cap is 100 and a fixed
  // array would cost ~5KB for a palette that is usually a fraction of that.
  std::vector<freeink::ui::ListItem> rowItems_;
};
