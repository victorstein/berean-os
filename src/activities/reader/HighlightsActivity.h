#pragma once

#include <StudyStore/TagPalette.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "activities/ActivityResult.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Browse this publication's passages (most recent first) and jump to one,
// filter them by tag, or delete one. The list is always the ONE open
// publication's -- there is no cross-publication list here -- while the tag
// filter cycles StudyStore's palette, which since Phase 1 is global. So a tag
// coined in another publication appears in the filter and simply matches
// nothing here.
//
// Row 0 is a persistent filter control, not a highlight: tapping/confirming
// it opens TagFilterActivity ("All", "Unlabelled", then each tag), narrowing
// which highlights rows 1.. show. It does NOT
// reuse TagPickerActivity: that picker enforces
// HighlightDoc::MAX_TAGS_PER_HIGHLIGHT (8) and offers "New tag...", both
// correct for tagging one highlight but wrong for a filter, which has no
// reason to cap how many tags narrow the list and has nothing to gain from
// minting a tag no highlight has yet.
//
// A long-press (touch) or a held Confirm release (physical buttons) on a
// highlight row opens a Tags.../Delete/Cancel OptionPopup (actionChooser_),
// not the delete confirmation directly -- editing tags is the other action
// this screen offers. Choosing "Tags..." pushes TagPickerActivity seeded with
// the entry's current tagIndices as its initialSelection; choosing "Delete"
// forces a synchronous clean repaint (requestUpdateAndWait) before opening
// confirmPopup_, since actionChooser_'s three rows are taller than
// confirmPopup_'s two and would otherwise frame it with leftover pixels.
// actionChooser_ and confirmPopup_ are two separate OptionPopup members --
// never the same one reused -- because OptionPopup::show() reassigns
// onSelectCallback and is invoked AS that member, so calling show() again
// from inside a running callback would destroy the closure still executing.
//
// Deleting is the only destructive, irreversible-on-disk action in the
// feature: on confirm, HighlightDoc::removeHighlight runs first and
// HighlightFile::save second; if the save fails the just-removed entry is
// re-added via addHighlight so the resident doc never diverges from what's on
// disk (matching PassageSelectActivity::finalizeSelection's own rollback on a
// failed save). Because addHighlight only appends, a rolled-back entry lands
// at the end of the vector rather than back at its original position --
// accepted here since the alternative (copying the whole document, up to
// HighlightDoc::MAX_HIGHLIGHTS entries, to save one) is the exact memory
// pressure this feature is built to avoid on the C3. This path only runs on
// a save failure, which is rare. Retagging (HighlightDoc::setTags) is
// likewise rolled back to the entry's previous tagIndices on a failed save.
//
// Jumping does NOT route through EpubReaderActivity's
// progressChangeResultHandler -- that lambda opens with
// std::get<ProgressChangeResult>(result.data), which is only safe because
// every OTHER caller of it always returns that alternative; it also calls
// loadCachedBookmarks() and reopens the reader menu on cancel, both wrong for
// a browse screen. Instead this activity returns a ProgressChangeResult with
// hasVisibleTextOffset=true and the highlight's start offset directly; the
// reader's existing offset-based jump branch (immune to re-pagination)
// applies unchanged. Wiring the launch site and its handler is Task 7's job,
// not this activity's.
//
// Home: NOT overridden, unlike PassageSelectActivity and TagPickerActivity.
// Both of those override handleHomeGesture() because the screen has no OTHER
// way to leave carrying a result -- selecting a passage or checking tags
// never itself finishes the activity. Here, activating a row (touch tap, or
// physical Confirm release) already IS the "jump" gesture and already leaves
// the screen, on every board, exactly like EpubReaderBookmarksActivity (the
// same browse-plus-delete shape) which also leaves Home unoverridden. Home
// therefore keeps its ordinary meaning -- ActivityManager::loop() takes an
// unconsumed Home gesture straight to the home screen -- rather than being
// repurposed as a second, redundant "jump" affordance that would make Home
// behave inconsistently with every other browse list in the app.
class HighlightsActivity final : public UiListActivity {
 public:
  explicit HighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;

  // Rebuilds visibleIndices_ from STUDY.passages() + filterTagId_.
  // Indices only, most-recent-first (reverse insertion order) -- never the
  // findBySpine-style raw HighlightEntry* pointers, which addHighlight's
  // push_back (rollback on a failed delete-save) or removeHighlight's erase
  // would invalidate out from under a held pointer.
  void rebuildVisibleIndices();
  // Rebuilds rowTagValues_/rowItems_ from visibleIndices_ + filterTagId_.
  // Called only when the underlying data changes (onEnter, filter cycle,
  // delete), not on every repaint -- mirrors
  // EpubReaderBookmarksActivity::rebuildBookmarkRowItems.
  void rebuildRowItems();
  std::string computeFilterSubtitle() const;
  void dropRetiredFilter();
  // Tag names for one passage, joined and capped for the row's value slot.
  std::string tagsValueFor(size_t passageIndex) const;

  // Pushes TagFilterActivity and applies its pick. Stepping one tag per tap
  // stopped scaling once the palette cap rose past a handful of tags.
  void openTagFilter();
  void jumpToHighlight(size_t docIndex);
  void showActionChooser(size_t docIndex);
  void editTags(size_t docIndex);
  void applyTagEdit(size_t docIndex, const ActivityResult& result);
  void showDeleteConfirmation(size_t docIndex);
  void deleteHighlight(size_t docIndex);

  // nullopt = no filter ("All"); otherwise a global tag id.
  //
  // An ID, not an index. The model this replaced had to re-resolve the active
  // filter BY NAME after every push, because deleting a tag renumbered every
  // index in place and an index held across a screen could come back naming a
  // different tag. Ids are allocated once and never reused, so there is nothing
  // to reconcile and that whole class of bug is gone.
  std::optional<study::TagId> filterTagId_;

  // Indices into STUDY.passages(), most-recent-first, filtered by filterTagId_.
  // See rebuildVisibleIndices()'s comment for why these are indices and not
  // pointers.
  std::vector<size_t> visibleIndices_;

  // Row 0 mirrors the filter control; rows 1.. mirror visibleIndices_.
  std::string filterSubtitle_;
  // Backing storage for each row's tag text; ListItem borrows a const char*.
  std::vector<std::string> rowTagValues_;
  std::vector<freeink::ui::ListItem> rowItems_;

  bool confirmingDelete_ = false;
  OptionPopup confirmPopup_;
  // Doc index captured when the delete confirmation opens, so the popup's
  // callback (which runs after further input has been processed) deletes the
  // exact entry that was long-pressed rather than re-deriving it from
  // whatever nav.selected happens to be by the time the popup resolves.
  size_t pendingDeleteIndex_ = 0;

  // Separate from confirmPopup_ on purpose -- see the class comment.
  bool choosingAction_ = false;
  OptionPopup actionChooser_;
  // Doc index captured when the action chooser opens, so its callback (Tags...
  // or Delete) dispatches on the exact entry that was long-pressed, same
  // reasoning as pendingDeleteIndex_ above.
  size_t pendingActionIndex_ = 0;
};
