#pragma once

#include <StudyStore/TagPalette.h>

#include <optional>
#include <vector>

#include "TagChipRow.h"
#include "TagChipView.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// A full-screen, paged grid of tag chips, shared by TagFilterActivity and the tag picker so the two
// cannot drift in paging, focus or retire behaviour. Chips wrap into lines and page at line
// boundaries; the x/y strip shows only when there is more than one page. A swipe turns a page, a
// released button steps one chip (crossing pages as it goes), and a tap or Confirm activates one.
// navigateButtons keeps a held-button page turn for boards whose keys report a hold; on the X4 Pro
// a long hold resolves to Back or Confirm instead.
//
// A long-press RETIRES a tag: it leaves the pickers and stops filtering, while every passage that
// carried it keeps its remaining tags and stays on the card. A destructive delete here would be
// device-wide now that the palette is global -- one tidy-up gesture would wipe work across every
// publication.
//
// Threads. The loop task builds the chip list; the render task draws it. Whatever the render task
// reads, the loop task writes only under RenderLock. RenderLock is a non-recursive mutex the render
// task holds for the whole render(), so a render-task hook must never take it, and every loop-task
// hook is called with it free.
//
// Chips dispatch only through ACTION_ROW, so a subclass's own actions start at ACTION_USER.
class TagChipGridActivity : public UiListActivity {
 public:
  bool handleHomeGesture() final;
  void render(RenderLock&&) final;

 protected:
  TagChipGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // --- subclass contract -----------------------------------------------------
  // Loop task, no lock held. Fills `out` (empty on entry; reserve first) from loop-owned state and
  // STUDY only.
  virtual void buildChips(std::vector<TagChipView::ChipEntry>& out) = 0;
  // Render task, inside the render: must not take RenderLock, and reads only state the loop task
  // writes under it.
  virtual TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const = 0;
  // Render task, after the grid on every build, chips or none. Same rules as chipState.
  virtual void buildHeaderExtras(UiScreen&) {}
  // activateIndex(int), from UiListActivity: loop task, no lock held; take RenderLock to write
  // anything the render task reads.

  // Loop task, no lock held, after a retire attempt left `id` inactive, failed save included; take
  // RenderLock to write anything the render task reads.
  virtual void onTagRetired(study::TagId id) = 0;
  // Loop task, no lock held: Home while no retire confirmation is open.
  virtual void onHome() = 0;

  // --- base behaviour ------------------------------------------------------------
  int listCount() const final;
  void buildScreen(UiScreen& screen) final;
  void drawFooter() final;
  // Both exits MUST set a result. UiListActivity::onBackButton is a bare finish(), which leaves
  // ActivityResult default-constructed: isCancelled false and the variant holding monostate. A
  // consumer that then reads its own alternative calls std::get on the wrong one, and with
  // -fno-exceptions that aborts.
  void onBackButton() final;
  void onRowAction(const freeink::ui::ActionEvent& event) final;
  void onRowLongPress(int index) final;
  bool handleCustomInput() final;
  // UiListActivity's own: Back -> onBackButton, Confirm -> activateIndex(nav.selected).
  bool handleButtons() final;
  void navigateButtons() final;

  // --- helpers ---------------------------------------------------------------------
  // Loop task, no lock held. Rebuilds from buildChips, keeps the selected chip by (kind, id) or
  // clamps its index when it is gone, closes touch routing until the next render, and requests it.
  void rebuildChips();
  // As rebuildChips(), selecting `select` instead (clamping as above when it is not listed).
  void rebuildChips(TagChips::ChipKey select);
  // Loop task. The loop task is the only writer of the list, so reading it needs no lock.
  const std::vector<TagChipView::ChipEntry>& chips() const { return chips_; }
  bool retireConfirmationOpen() const { return confirmPopup_.isActive(); }

 private:
  void rebuildChipsSelecting(std::optional<TagChips::ChipKey> select);
  void buildGrid(UiScreen& screen);
  void stepSelection(int direction);
  void turnPage(int direction, bool fromButton);
  void showRetireConfirmation(study::TagId id);
  void retireTag(study::TagId id);

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
