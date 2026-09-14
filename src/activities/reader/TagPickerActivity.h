#pragma once

#include <StudyStore/PassageDoc.h>
#include <StudyStore/TagPalette.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "study/StudyStore.h"
#include "components/OptionPopup.h"

// Multi-select picker over a book's tag palette (HighlightDoc::tags()), plus
// a "New tag..." row that pushes KeyboardEntryActivity and calls
// HighlightDoc::addTag. Returns the checked indices as a TagSelectionResult;
// the caller applies them to whichever highlight it is tagging.
//
// The X4 Pro has no physical Back/Confirm (see PassageSelectActivity's class
// comment for the general mechanism). Checking a row here never leaves the
// screen -- there is no button-handling path that finishes the activity on
// its own -- so, exactly like PassageSelectActivity, this activity
// repurposes the capacitive Home key (handleHomeGesture()) as the picker's
// Done gesture, committing the current selection. Back cancels
// (isCancelled=true, selection discarded), matching both PassageSelectActivity's
// precedent and the codebase-wide Back=cancel convention. A tag created via
// "New tag..." is added to the book's palette AND persisted to disk
// immediately (HighlightFile::save, unless saveDisabled) -- the palette is
// book-wide state, independent of which highlight ends up tagged, so a
// highlight cancelled after this point must not take the new tag down with
// it. If that save fails, the just-added tag is rolled back via removeTag;
// a dedupe hit (addTag returning an existing index rather than adding one)
// is never rolled back, since nothing new was added and removeTag would
// strip a pre-existing tag off every highlight in the book.
//
// A tag row can also be long-pressed (touch) or held-Confirm-released
// (physical buttons) to delete it from the palette entirely -- this removes
// it from EVERY highlight in the book that carries it, not just the one
// being tagged here, so a confirmation dialog names that scope explicitly.
// Unlike the add path above, a failed delete-save cannot be rolled back:
// A long-press RETIRES a tag rather than deleting it. The palette is global, so
// a destructive delete here would reach every publication; retiring removes it
// from the pickers and from this publication's passages while leaving every
// passage itself on the card.
class TagPickerActivity final : public UiListActivity {
 public:
  explicit TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::vector<study::TagId> initialSelection = {});

  void onEnter() override;
  bool handleHomeGesture() override;
  void render(RenderLock&&) override;

 private:
  // "Done" + palette + "New tag..."
  static constexpr int MAX_ROWS = static_cast<int>(study::TagPalette::MAX_ACTIVE_TAGS) + 2;
  static constexpr int DONE_ROW = 0;

  // Row index -> index into tags_, or -1 for a non-tag row.
  // EVERY row-to-tag conversion goes through this. The rows and the palette are
  // no longer the same numbering, and applying the offset in one place but not
  // the next is exactly how a held Confirm on "Done" would reach the delete path.
  int tagIndexForRow(int row) const;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void toggleTag(size_t tagRow);
  void startNewTagFlow();
  void reportAddTagFailure(const std::string& name);
  void commitAndFinish();
  void showRetireConfirmation(size_t tagRow);
  void retireTag(size_t tagRow);

  bool isSelected(study::TagId id) const;
  void setSelected(study::TagId id, bool on);

  // Snapshot of the active palette. rowItems_ borrows label pointers from these
  // strings, so they must outlive the row list -- and must not be a view into
  // the palette itself, which "New tag..." can reallocate mid-visit.
  std::vector<StudyStore::TagView> tags_;

  // The checked set, as IDS. The model this replaced kept a bool array
  // index-aligned with the palette and had to shift every entry down when a tag
  // was deleted, because otherwise each slot above it silently came to mean a
  // different tag. Ids need no alignment and no shifting.
  std::vector<study::TagId> selectedIds_;

  bool confirmingDelete_ = false;
  OptionPopup confirmPopup_;
  // Tag index captured when the delete confirmation opens, so the popup's
  // callback (which runs after further input has been processed) deletes the
  // exact row that was long-pressed rather than re-deriving it from whatever
  // nav.selected happens to be by the time the popup resolves.
  size_t pendingRetireRow_ = 0;

  // Rebuilt from tags_ on every buildScreen() call, never
  // cached across visits: a short tag name can live in std::string's small
  // buffer, whose address moves if the tags vector reallocates after
  // "New tag..." adds an entry -- caching label pointers across that round
  // trip would leave rowItems_ pointing at freed memory.
  freeink::ui::ListItem rowItems_[MAX_ROWS]{};
};
