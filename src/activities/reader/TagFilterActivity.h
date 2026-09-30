#pragma once

#include <StudyStore/TagPalette.h>

#include <cstddef>
#include <optional>
#include <vector>

#include "TagChipRow.h"
#include "TagChipView.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The highlights chip row's ellipsis: every chip the row would offer, uncapped, as a wrapping grid
// with counts, paged only when it outgrows one screen. Returns a TagSelectionResult holding no
// elements for "All" or exactly one id -- study::UNLABELLED for "Unlabelled"; a cancelled result
// means the caller keeps its current filter.
//
// A long-press RETIRES a tag: it leaves the pickers and stops filtering, while every passage that
// carried it keeps its remaining tags and stays on the card. A destructive delete here would be
// device-wide now that the palette is global -- one tidy-up gesture would wipe work across every
// publication.
//
// Chips report a study::TagId, not an index. Ids are allocated once and never reused, so a
// retirement cannot renumber anything and the caller's captured filter stays valid.
class TagFilterActivity final : public UiListActivity {
 public:
  // scope: nullopt counts every passage; otherwise only these passage indices, so the chapter view's
  // counts match the list it shows. Computed by the caller with no RenderLock held.
  TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::optional<std::vector<size_t>> scope,
                    std::optional<study::TagId> filter);

  void onEnter() override;

 private:
  // Both exits MUST set a result. UiListActivity::onBackButton is a bare
  // finish(), which leaves ActivityResult default-constructed: isCancelled
  // false and the variant holding monostate. A consumer that then reads its
  // own alternative calls std::get on the wrong one, and with -fno-exceptions
  // that aborts.
  void onBackButton() override;
  bool handleHomeGesture() override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void navigateButtons() override;
  void render(RenderLock&&) override;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void rebuildChips();
  int selectedChipIndex() const;
  void stepSelection(int direction);
  void turnPage(int direction, bool fromButton);
  void showRetireConfirmation(study::TagId id);
  void retireTag(study::TagId id);

  const std::optional<std::vector<size_t>> scope_;
  // Written under RenderLock: the render task reads it to invert the selected chip.
  std::optional<study::TagId> filter_;

  // chips_ and widths_ are replaced together under RenderLock; widths_ is filled by the build.
  std::vector<TagChipView::ChipEntry> chips_;
  std::vector<int> widths_;
  TagChips::GridPage page_;
  // Page geometry the build writes and the loop task's page turns read, both under RenderLock.
  int gridLineWidth_ = 0;
  int gridGap_ = 0;
  int gridLinesPerPage_ = 1;
  int pageNumber_ = 0;
  int pageCount_ = 0;
  // The focused chip is outlined only after a button step, as HighlightsActivity's chip band is.
  bool buttonFocus_ = false;

  bool confirmingDelete_ = false;
  OptionPopup confirmPopup_;
  // Captured when the confirmation opens, so the callback retires the chip that was long-pressed
  // even if a rebuild moved the chips in between.
  study::TagId pendingRetireId_ = study::UNLABELLED;
};
