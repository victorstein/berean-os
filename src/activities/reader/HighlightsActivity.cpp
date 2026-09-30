#include "HighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <variant>

#include "MappedInputManager.h"
#include "PassageLinksActivity.h"
#include "ReaderUtils.h"
#include "TagChipRow.h"
#include "TagFilterActivity.h"
#include "TagPickerActivity.h"
#include "activities/PostedMessage.h"
#include "components/ListRowHeight.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

namespace {
// Matches EpubReaderBookmarksActivity's own threshold for "this Confirm
// release was a hold, not a tap" on boards with a physical Confirm button.
constexpr int ENTER_DELETE_MODE_MS = 700;
}  // namespace

HighlightsActivity::HighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Highlights", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

void HighlightsActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_CHIP, &HighlightsActivity::onChipEvent, this);
  app.on(ACTION_MORE, &HighlightsActivity::onMoreEvent, this);
  RenderLock lock(*this);
  rebuildVisibleIndices();
  rebuildRowItems();
}

int HighlightsActivity::listCount() const { return static_cast<int>(visibleIndices_.size()) + 1; }

const char* HighlightsActivity::headerTitle() const { return tr(STR_HIGHLIGHTS); }

void HighlightsActivity::rebuildVisibleIndices() {
  visibleIndices_.clear();
  const auto& passages = STUDY.passages();
  visibleIndices_.reserve(passages.size());
  // Most recent first: add only ever appends, so storage order is
  // oldest-to-newest and "most recent" is the reverse walk.
  for (size_t i = passages.size(); i-- > 0;) {
    if (!TagChips::passageMatches(passages[i].tags, filterTagId_)) continue;
    visibleIndices_.push_back(i);
  }
}

void HighlightsActivity::dropRetiredFilter() {
  // A filter on a retired tag would show an empty list forever. UNLABELLED is in
  // no palette, so isActive() is false for it and must not be consulted.
  if (filterTagId_ && *filterTagId_ != study::UNLABELLED && !STUDY.palette().isActive(*filterTagId_)) {
    filterTagId_.reset();
  }
}

std::string HighlightsActivity::tagsValueFor(const size_t passageIndex) const {
  // list() takes the value slot's measured width out of the label's, so an
  // unbounded tag list drives the reference's width negative and list() then
  // skips drawing it. Matching MAX_TAG_NAME_BYTES keeps one longest tag whole.
  return utf8SafeSummary(STUDY.tagNamesFor(passageIndex), study::TagPalette::MAX_TAG_NAME_BYTES);
}

void HighlightsActivity::rebuildRowItems() {
  rowTagValues_.clear();
  rowItems_.clear();
  rowTagValues_.reserve(visibleIndices_.size());
  rowItems_.reserve(visibleIndices_.size());

  const auto& passages = STUDY.passages();
  for (size_t i = 0; i < visibleIndices_.size(); ++i) {
    const auto& entry = passages[visibleIndices_[i]];
    rowTagValues_.push_back(tagsValueFor(visibleIndices_[i]));

    // Reference and tags share the first line, passage wraps beneath. Nothing
    // relies on an embedded newline: GfxRenderer::wrappedText splits on spaces
    // only, and text() draws a string narrower than the row on a single line
    // regardless, so a '\n' here would measure as a break and never draw as one.
    fui::ListItem item{};
    if (!entry.reference.empty()) {
      item.label = entry.reference.c_str();
      // Left null when there is no passage: list() tests the pointer, not the
      // string, so an empty one would still reserve a blank second line.
      if (!entry.snippet.empty()) item.subtitle = entry.snippet.c_str();
    } else {
      // No verse anchors in this book: the passage takes the label line, which
      // is exactly the layout this replaced.
      item.label = entry.snippet.empty() ? tr(STR_UNNAMED) : entry.snippet.c_str();
    }
    if (!rowTagValues_.back().empty()) item.value = rowTagValues_.back().c_str();
    item.actionValue = static_cast<int16_t>(i + 1);
    rowItems_.push_back(item);
  }
  rebuildChips();
}

void HighlightsActivity::rebuildChips() {
  chips_.clear();
  chips_.reserve(TagChips::MAX_CHIPS + 1);

  const std::vector<study::TagId> activeIds = STUDY.palette().activeIds();
  const TagChips::Counts counts = TagChips::count(STUDY.passages(), activeIds);

  const auto push = [this](const TagChips::Kind kind, const study::TagId id, const char* name, const size_t n) {
    if (chips_.size() >= TagChips::MAX_CHIPS + 1) return;
    ChipEntry chip;
    chip.kind = kind;
    chip.id = id;
    snprintf(chip.label, sizeof(chip.label), "%s %u", name, static_cast<unsigned>(n));
    chips_.push_back(chip);
  };

  push(TagChips::Kind::All, study::UNLABELLED, tr(STR_TAG_FILTER_ALL), counts.all);
  for (size_t slot = 0; slot < activeIds.size(); ++slot) {
    const study::TagId id = activeIds[slot];
    // Zero-count tags come from other publications (the palette is global); the active filter
    // stays visible even at zero so an empty list still shows why.
    if (counts.perTag[slot] == 0 && filterTagId_ != id) continue;
    if (chips_.size() >= TagChips::MAX_CHIPS + 1) break;
    push(TagChips::Kind::Tag, id, STUDY.tagName(id).c_str(), counts.perTag[slot]);
  }
  if (counts.unlabelled > 0 || filterTagId_ == study::UNLABELLED) {
    push(TagChips::Kind::Unlabelled, study::UNLABELLED, tr(STR_TAG_UNLABELLED), counts.unlabelled);
  }
}

bool HighlightsActivity::chipIsSelected(const ChipEntry& chip) const {
  switch (chip.kind) {
    case TagChips::Kind::All:
      return !filterTagId_;
    case TagChips::Kind::Unlabelled:
      return filterTagId_ == study::UNLABELLED;
    case TagChips::Kind::Tag:
      return filterTagId_ == chip.id;
  }
  return false;
}

void HighlightsActivity::onChipEvent(const fui::ActionEvent& event, void* user) {
  static_cast<HighlightsActivity*>(user)->selectChip(event.value);
}

void HighlightsActivity::onMoreEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<HighlightsActivity*>(user);
  if (self->confirmPopup_.isActive() || self->actionChooser_.isActive()) return;
  {
    RenderLock lock(*self);
    self->buttonFocus_ = false;
  }
  self->openTagFilter();
}

void HighlightsActivity::selectChip(const int chipIndex) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;
  if (chipIndex < 0 || chipIndex >= static_cast<int>(chips_.size())) return;

  const ChipEntry& chip = chips_[static_cast<size_t>(chipIndex)];
  std::optional<study::TagId> picked;
  if (chip.kind != TagChips::Kind::All) picked = chip.id;
  if (picked == filterTagId_) {
    if (buttonFocus_) moveRingTo(0);  // clears the outline; nothing else changed
    return;
  }

  {
    // chips_ and rowItems_ are borrowed by the render task; rebuild them under its lock.
    RenderLock lock(*this);
    filterTagId_ = picked;
    rebuildVisibleIndices();
    rebuildRowItems();
  }
  moveRingTo(0);
}

void HighlightsActivity::openTagFilter() {
  app.clearTapFlash();
  startActivityForResult(std::make_unique<TagFilterActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           // Guarded on the alternative, not just isCancelled: any finish() that
                           // forgets to set a result leaves monostate here, and std::get on the
                           // wrong alternative aborts under -fno-exceptions.
                           if (!result.isCancelled && std::holds_alternative<TagSelectionResult>(result.data)) {
                             const auto& selection = std::get<TagSelectionResult>(result.data);
                             if (selection.tagIds.empty()) {
                               filterTagId_.reset();
                             } else {
                               filterTagId_ = selection.tagIds.front();
                             }
                           }

                           // The filter screen can retire a tag. Nothing needs re-resolving --
                           // the id is stable -- but the filter may now name a retired tag.
                           dropRetiredFilter();

                           // Always rebuilt, including on cancel: a retirement changes the rows
                           // and the labels they borrow even when the filter is untouched.
                           {
                             RenderLock lock(*this);
                             rebuildVisibleIndices();
                             rebuildRowItems();
                           }
                           moveRingTo(0, buttonFocus_);
                         });
}

void HighlightsActivity::jumpToHighlight(const size_t docIndex) {
  if (docIndex >= STUDY.passages().size()) return;
  const auto& entry = STUDY.passages()[docIndex];

  // The stored address is a Unit, so BOTH the document and the offset are
  // resolved now rather than trusted. locate() falls back to searching the book
  // when the stored spine no longer holds, which is what makes a mark survive
  // the publication being replaced by a different edition.
  const auto found = STUDY.locate(docIndex);

  // hasVisibleTextOffset plus the resolved offset routes through the reader's
  // existing offset-based jump branch (immune to re-pagination); see the class
  // comment for why this bypasses progressChangeResultHandler.
  ProgressChangeResult result;
  result.spineIndex = static_cast<int>(found ? found->spineIndex : entry.documentSpine);
  result.hasVisibleTextOffset = found.has_value();
  result.visibleTextOffset = found ? found->offset : 0;
  setResult(std::move(result));
  finish();
}

void HighlightsActivity::activateIndex(const int index) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  activeNav().selected = index;

  if (index == 0) {
    openTagFilter();
    return;
  }

  // Leaving this screen for the reader; a lingering tap flash would gray an
  // unrelated row if the user comes back here later.
  app.clearTapFlash();
  jumpToHighlight(visibleIndices_[static_cast<size_t>(index - 1)]);
}

void HighlightsActivity::moveRingTo(const int ringIndex, const bool fromButton) {
  {
    // The render task reads nav and buttonFocus_ mid-build.
    RenderLock lock(*this);
    auto& n = activeNav();
    const int rowCount = static_cast<int>(visibleIndices_.size());
    n.selected = std::clamp(ringIndex, 0, rowCount);
    buttonFocus_ = fromButton;
    ringFollowPending_ = n.selected > 0;
    if (n.selected == 0) {
      n.top = 0;
    } else {
      // ListNav::follow reads selected as a row index, and here it is a ring position.
      const uint16_t rows = n.pageRows() > 0 ? static_cast<uint16_t>(n.pageRows()) : 1;
      n.top = fui::listTopIndexFor(static_cast<int16_t>(n.selected - 1), static_cast<uint16_t>(n.top < 0 ? 0 : n.top),
                                   rows, static_cast<uint16_t>(rowCount));
    }
  }
  requestUpdate();
}

void HighlightsActivity::navigateButtons() {
  const int ringSize = listCount();
  auto& n = activeNav();
  buttonNavigator.onNextRelease(
      [this, ringSize, &n] { moveRingTo(ButtonNavigator::nextIndex(n.selected, ringSize), true); });
  buttonNavigator.onPreviousRelease(
      [this, ringSize, &n] { moveRingTo(ButtonNavigator::previousIndex(n.selected, ringSize), true); });
  buttonNavigator.onNextContinuous(
      [this, ringSize, &n] { moveRingTo(ButtonNavigator::nextPageIndex(n.selected, ringSize, n.pageRows()), true); });
  buttonNavigator.onPreviousContinuous([this, ringSize, &n] {
    moveRingTo(ButtonNavigator::previousPageIndex(n.selected, ringSize, n.pageRows()), true);
  });
}

void HighlightsActivity::onRowAction(const fui::ActionEvent& event) {
  {
    RenderLock lock(*this);
    buttonFocus_ = false;
  }
  UiListActivity::onRowAction(event);
}

void HighlightsActivity::syncRingViewport(UiScreen& screen, fui::ListProps& props) {
  // Same row-height rule as UiListActivity::syncListViewport, so rows keep #194's compact metrics.
  const auto& metrics = UITheme::getInstance().getMetrics();
  ListRowHeight::Inputs in;
  in.touch = mappedInput.hasTouch();
  in.hasSubtitle = true;
  in.tokenRowHeight = screen.theme().rowHeight;
  in.minTouchSize = screen.theme().minTouchSize;
  in.denseRow = metrics.listRowHeight;
  in.denseSubtitleRow = metrics.listWithSubtitleRowHeight;
  in.touchSingleRow = metrics.touchListRowHeight;
  const int16_t rowHeight = ListRowHeight::resolve(in);
  props.rowHeight = rowHeight;

  auto& n = activeNav();
  const int rowCount = static_cast<int>(visibleIndices_.size());
  const uint16_t rows = fui::listVisibleRows(screen.body(), rowHeight, screen.theme().listRowGap);
  n.visibleRows = rows > 0 ? rows : 1;
  if (n.followOnBuild) {
    n.followOnBuild = false;
    n.top = n.selected > 0 ? static_cast<int>(fui::listTopIndexFor(
                                 static_cast<int16_t>(n.selected - 1), static_cast<uint16_t>(n.top < 0 ? 0 : n.top),
                                 static_cast<uint16_t>(n.visibleRows), static_cast<uint16_t>(rowCount)))
                           : 0;
  }
  n.scrollBy(0, rowCount);
  if (n.selected > rowCount) n.selected = rowCount;
  props.topIndex = static_cast<uint16_t>(n.top);
  // -1 while the chip row holds the focus.
  props.selectedIndex = static_cast<int16_t>(n.selected - 1);
  // list() reports its drawn rows back through nav, which keeps the variable-height tail reachable.
  // follow() is never called: it would read selected as a row index, and here it is a ring position.
  props.nav = &n;
}

void HighlightsActivity::onRowLongPress(const int index) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;
  if (index <= 0 || index >= listCount()) return;  // ring 0 is the chip row; nothing to act on
  app.clearTapFlash();
  activeNav().selected = index;
  showActionChooser(visibleIndices_[static_cast<size_t>(index - 1)]);
}

namespace {

const char* actionLabel(const PassageActions::Action action) {
  switch (action) {
    case PassageActions::Action::EditTags:
      return tr(STR_EDIT_TAGS);
    case PassageActions::Action::ShowLinks:
      return tr(STR_PASSAGE_LINKS);
    case PassageActions::Action::LinkToMarked:
      return tr(STR_LINK_TO_MARKED);
    case PassageActions::Action::MarkAsLinkSource:
      return tr(STR_MARK_LINK_SOURCE);
    case PassageActions::Action::Delete:
      return tr(STR_DELETE);
    case PassageActions::Action::Cancel:
      return tr(STR_CANCEL);
  }
  return tr(STR_CANCEL);
}

PassageActions::LinkMark linkMarkFor(const size_t docIndex) {
  const auto source = STUDY.linkSource();
  if (!source) return PassageActions::LinkMark::None;
  return *source == docIndex ? PassageActions::LinkMark::ThisPassage : PassageActions::LinkMark::OtherPassage;
}

}  // namespace

void HighlightsActivity::showActionChooser(const size_t docIndex) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;
  if (docIndex >= STUDY.passages().size()) return;

  pendingActionIndex_ = docIndex;
  choosingAction_ = true;

  // A store that failed to load refuses every save, so the menu offers nothing
  // that writes except Delete, which reports the refusal itself.
  pendingMenu_ =
      PassageActions::menuFor(!STUDY.saveDisabled(), !STUDY.passages()[docIndex].links.empty(), linkMarkFor(docIndex));
  const char* options[PassageActions::Menu::MAX_ACTIONS];
  for (int i = 0; i < pendingMenu_.count; ++i) options[i] = actionLabel(pendingMenu_.actions[i]);

  actionChooser_.show(tr(STR_HIGHLIGHT_ACTIONS), options, pendingMenu_.count, 0, [this](const int idx) {
    choosingAction_ = false;
    if (idx >= 0 && idx < pendingMenu_.count) runAction(pendingMenu_.actions[idx], pendingActionIndex_);
    requestUpdate();
  });
  requestUpdate();
}

void HighlightsActivity::runAction(const PassageActions::Action action, const size_t docIndex) {
  switch (action) {
    case PassageActions::Action::EditTags:
      // Pushes a whole new activity, which repaints the screen from scratch on
      // entry -- no leftover-chooser-pixels hazard here.
      editTags(docIndex);
      return;
    case PassageActions::Action::ShowLinks:
      openLinks(docIndex);
      return;
    case PassageActions::Action::LinkToMarked:
      linkMarkedSourceTo(docIndex);
      return;
    case PassageActions::Action::MarkAsLinkSource:
      STUDY.markLinkSource(docIndex);
      ReaderUtils::showMessage(renderer, tr(STR_LINK_SOURCE_MARKED));
      return;
    case PassageActions::Action::Delete:
      // actionChooser_ is taller than confirmPopup_ and both draw over the
      // current screen without clearing it (OptionPopup's class comment), so
      // confirmPopup_ would otherwise be framed by actionChooser_'s leftover
      // pixels. Force one synchronous clean repaint of the underlying list --
      // with neither popup active -- first. Safe from inside the chooser's
      // callback: it runs on the loop task with no RenderLock held
      // (ActivityManager.cpp's three requestUpdateAndWait asserts all pass
      // here), mirroring PassageSelectActivity::showActionChooser's identical use.
      requestUpdateAndWait();
      showDeleteConfirmation(docIndex);
      return;
    case PassageActions::Action::Cancel:
      return;
  }
}

void HighlightsActivity::openLinks(const size_t docIndex) {
  if (docIndex >= STUDY.passages().size()) return;
  app.clearTapFlash();
  startActivityForResult(std::make_unique<PassageLinksActivity>(renderer, mappedInput, docIndex),
                         [this](const ActivityResult& result) {
                           // A followed link is the same jump a row tap makes, so it goes to the
                           // reader through this screen's own result.
                           if (result.isCancelled || !std::holds_alternative<ProgressChangeResult>(result.data)) {
                             return;
                           }
                           ProgressChangeResult jump = std::get<ProgressChangeResult>(result.data);
                           setResult(std::move(jump));
                           finish();
                         });
}

void HighlightsActivity::linkMarkedSourceTo(const size_t docIndex) {
  const char* message = nullptr;
  switch (STUDY.linkMarkedSourceTo(docIndex)) {
    case StudyStore::LinkOutcome::Linked:
      message = tr(STR_LINK_ADDED);
      break;
    case StudyStore::LinkOutcome::AlreadyLinked:
      message = tr(STR_LINK_ALREADY_EXISTS);
      break;
    case StudyStore::LinkOutcome::AtCap:
      message = tr(STR_LINK_LIMIT_REACHED);
      break;
    case StudyStore::LinkOutcome::SelfLink:
      message = tr(STR_LINK_TO_ITSELF);
      break;
    case StudyStore::LinkOutcome::NoSource:
      message = tr(STR_LINK_NO_SOURCE);
      break;
    case StudyStore::LinkOutcome::NoTarget:
      message = tr(STR_LINK_TARGET_NOT_FOUND);
      break;
    case StudyStore::LinkOutcome::NotSaved:
      message = tr(STR_LINK_SAVE_FAILED);
      break;
  }
  ReaderUtils::showMessage(renderer, message);
}

void HighlightsActivity::editTags(const size_t docIndex) {
  if (docIndex >= STUDY.passages().size()) return;  // stale index; nothing to do
  if (STUDY.saveDisabled()) {
    // Defense in depth: showActionChooser already omits "Tags..." in this
    // state, but mirror showDeleteConfirmation's own bail so this method is
    // safe to call regardless of how it's reached.
    ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_LOAD_FAILED));
    requestUpdate();
    return;
  }

  // clearTapFlash is what the row-tap pushes do (activateIndex, above)
  // because a row flash is what lingers. This push comes from a popup
  // button, so it is optional here -- PassageSelectActivity::startTagFlow
  // pushes the same activity from a popup callback without it.
  app.clearTapFlash();
  const std::vector<study::TagId> initialSelection = STUDY.passages()[docIndex].tags;
  startActivityForResult(std::make_unique<TagPickerActivity>(renderer, mappedInput, initialSelection),
                         [this, docIndex](const ActivityResult& result) { applyTagEdit(docIndex, result); });
}

void HighlightsActivity::applyTagEdit(const size_t docIndex, const ActivityResult& result) {
  // A size_t doc index is stable across the picker push: the picker edits the
  // palette but never adds or removes a passage. Bounds-checked defensively.
  if (!result.isCancelled && docIndex < STUDY.passages().size()) {
    // -fno-exceptions means a mismatched alternative aborts with no
    // recovery, so this must never run on the cancelled path.
    const auto& selection = std::get<TagSelectionResult>(result.data);
    // setPassageTags saves synchronously and restores the previous tags itself
    // if the write fails, so the resident document can never hold tags that are
    // not on disk.
    if (!STUDY.setPassageTags(docIndex, selection.tagIds)) {
      ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_SAVE_FAILED));
    }
  }

  // The picker persists palette changes itself, so even a cancelled edit may
  // have retired the tag being filtered on.
  dropRetiredFilter();
  {
    // rebuildVisibleIndices/rebuildRowItems refill the vector buildScreen
    // hands the render task as rowItems_.data(); the render lock is
    // explicitly released before this handler runs (ActivityManager.cpp),
    // so nothing else serialises this against a concurrent render.
    RenderLock lock(*this);
    rebuildVisibleIndices();
    rebuildRowItems();
  }
  // The rebuild can shrink visibleIndices_ (filter reset above, or the
  // filtered tag itself deleted); moveRingTo clamps the ring position and issues its own
  // requestUpdate().
  moveRingTo(activeNav().selected, buttonFocus_);
}

void HighlightsActivity::showDeleteConfirmation(const size_t docIndex) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;
  if (STUDY.saveDisabled()) {
    // The file may still hold the user's data (a Failed load); never let a
    // delete through in that state, matching PassageSelectActivity's own bail.
    ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_LOAD_FAILED));
    requestUpdate();
    return;
  }

  pendingDeleteIndex_ = docIndex;
  confirmingDelete_ = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup_.show(tr(STR_CONFIRM_DELETE_HIGHLIGHT), options, 2, 0, [this](const int idx) {
    confirmingDelete_ = false;
    if (idx == 1) deleteHighlight(pendingDeleteIndex_);
    requestUpdate();
  });
  requestUpdate();
}

void HighlightsActivity::deleteHighlight(const size_t docIndex) {
  if (docIndex >= STUDY.passages().size()) return;  // stale index; nothing to do

  // removePassage saves synchronously and re-adds the passage itself if the
  // write fails, so the resident document never diverges from disk. A
  // rolled-back passage lands at the end of the vector rather than back at its
  // original position, so visibleIndices_ must be rebuilt wholesale either way
  // -- never patched in place.
  const bool removed = STUDY.removePassage(docIndex);

  // Rebuilt under the render lock: rowItems_[i].label aliases each passage's
  // std::string storage (see rebuildRowItems) and the render task runs
  // concurrently, so it must never see rows aliasing erased or moved storage.
  {
    RenderLock lock(*this);
    rebuildVisibleIndices();
    rebuildRowItems();
  }

  if (!removed) ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_SAVE_FAILED));

  moveRingTo(activeNav().selected, buttonFocus_);
}

bool HighlightsActivity::handleCustomInput() {
  if (actionChooser_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (choosingAction_) {
    // Popup dismissed without a selection (Back button/gesture, or a tap
    // outside it): cancel the pending action, stay on this screen.
    choosingAction_ = false;
    requestUpdate();
    return true;
  }

  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete_) {
    // Popup dismissed without a selection (Back button/gesture, or a tap
    // outside it): cancel the pending delete, stay on this screen.
    confirmingDelete_ = false;
    requestUpdate();
    return true;
  }
  return false;
}

bool HighlightsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (selected < 0 || selected >= listCount()) return true;
    // Matches EpubReaderBookmarksActivity: a held Confirm release on a
    // highlight row (not ring position 0, the chip row, which has nothing to
    // delete) opens the delete confirmation instead of jumping.
    if (selected > 0 && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
      onRowLongPress(selected);
    } else {
      activateIndex(selected);
    }
    return true;
  }

  return false;
}

void HighlightsActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void HighlightsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Nothing to browse or filter: skip the chip row entirely rather than
  // show a lone "All 0" over an empty list.
  if (STUDY.passages().empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

  buildChipRow(screen);

  // "Hold Open to Delete" names a physical button; on touch boards the row
  // long-press covers deletion instead, so the hint would be wrong there --
  // matches EpubReaderBookmarksActivity's own gating.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  // Tap jumps; long-press opens the action menu (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  // Theme FIRST: Screen::list only substitutes the theme font into a style that
  // still passes textStyleUnset (FreeInkUICore.h:550-554), and maxLines != 1
  // fails that test. Setting maxLines alone would skip the substitution and
  // render the subtitle in font 0.
  props.subtitleText = screen.theme().smallText;
  // The passage owns the subtitle band alone, so two lines of it is the whole
  // budget; tags sit in the value slot on the label line.
  props.subtitleText.maxLines = 2;
  syncRingViewport(screen, props);
  screen.list(props);
  confirmRingFollow();
}

void HighlightsActivity::confirmRingFollow() {
  if (!ringFollowPending_) return;
  auto& n = activeNav();
  const int selectedRow = n.selected - 1;
  const int drawn = n.drawnRows > 0 ? n.drawnRows : 1;
  if (selectedRow < n.top || selectedRow < n.top + drawn) {
    ringFollowPending_ = false;
    return;
  }
  int next = selectedRow - drawn + 1;
  if (next <= n.top) next = n.top + 1;
  if (next > selectedRow) next = selectedRow;
  n.top = next;
  n.rebuildNeeded = true;
}

void HighlightsActivity::buildChipRow(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Every chip is measured bold, the selected weight, so which chip is selected never changes a
  // width and never reflows the row.
  fui::TextStyle measureStyle = theme.smallText;
  measureStyle.bold = true;
  const int padX = theme.spaceMd;
  const int chipHeight = screen.target().lineHeight(theme.smallText.font) + 2 * theme.spaceSm;
  const int gap = theme.spaceSm;
  const int count = static_cast<int>(chips_.size());
  for (int i = 0; i < count; ++i) {
    const int textWidth = screen.target().measureText(theme.smallText.font, chips_[i].label, measureStyle).width;
    chipWidths_[i] = std::max(textWidth + 2 * padX, chipHeight);
  }
  const int moreWidth = std::max(
      screen.target().measureText(theme.smallText.font, fui::TEXT_ELLIPSIS, measureStyle).width + 2 * padX, chipHeight);

  const int inset = theme.listInset;
  const int lineWidth = screen.body().width - 2 * inset;
  TagChips::layout(chipWidths_, count, moreWidth, lineWidth, gap, chipLayout_);
  if (chipLayout_.lines == 0) return;

  const fui::Rect band = screen.takeTop(static_cast<int16_t>(TagChips::bandHeight(chipLayout_.lines, chipHeight, gap)),
                                        static_cast<int16_t>(metrics.verticalSpacing));

  fui::StyleSet styles;
  styles.explicitlySet = true;
  const auto radius = static_cast<uint8_t>(std::min(chipHeight / 2, 255));
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.normal.border = fui::Paint::solid(fui::Color::Black);
  styles.normal.borderWidth = 1;
  styles.normal.radius = radius;
  styles.selected = styles.normal;
  styles.selected.background = fui::Paint::solid(fui::Color::Black);
  styles.selected.foreground = fui::Paint::solid(fui::Color::White);
  // Focus/flash states keep the inverted pill instead of falling back to an unset style.
  styles.focused = styles.selected;
  styles.active = styles.selected;
  styles.disabled = styles.normal;

  bool selectedPlaced = false;
  for (int k = 0; k < chipLayout_.placedCount; ++k) {
    const int chip = chipLayout_.placed[k].chip;
    if (chip >= 0 && chipIsSelected(chips_[static_cast<size_t>(chip)])) selectedPlaced = true;
  }

  for (int k = 0; k < chipLayout_.placedCount; ++k) {
    const TagChips::Placed& placed = chipLayout_.placed[k];
    const bool isMore = placed.chip < 0;
    // An active filter pushed past the row is shown by inverting the ellipsis that leads to it.
    const bool selected = isMore ? !selectedPlaced : chipIsSelected(chips_[static_cast<size_t>(placed.chip)]);

    fui::ButtonProps props;
    props.label = isMore ? fui::TEXT_ELLIPSIS : chips_[static_cast<size_t>(placed.chip)].label;
    props.action = isMore ? ACTION_MORE : ACTION_CHIP;
    props.value = static_cast<int16_t>(isMore ? 0 : placed.chip);
    props.inputMask = fui::InputTouch;
    props.state = selected ? fui::StateSelected : fui::StateNormal;
    props.text = theme.smallText;
    props.text.bold = selected;
    props.styles = styles;
    // screen.button() would force the theme's minTouchSize, growing each hit rect over the
    // neighbouring line; the padding below tiles the gaps instead.
    props.minTouchSize = 0;
    const TagChips::Pad pad = TagChips::hitPadding(chipLayout_, k, gap);
    props.hitPadding = fui::Insets{static_cast<int16_t>(pad.top), static_cast<int16_t>(pad.right),
                                   static_cast<int16_t>(pad.bottom), static_cast<int16_t>(pad.left)};
    const fui::Rect rect{static_cast<int16_t>(band.x + inset + placed.x),
                         static_cast<int16_t>(band.y + TagChips::lineTop(placed.line, chipHeight, gap)),
                         static_cast<int16_t>(placed.width), static_cast<int16_t>(chipHeight)};
    fui::button(screen.frame(), rect, props);
  }

  if (activeNav().selected == 0 && buttonFocus_) {
    // Horizontally the outline may only use the list inset (0 on the base theme); vertically it
    // sits in the spacer above and the takeTop gap below, both at least verticalSpacing.
    const int outset = std::min(gap, inset);
    const fui::Rect outline{static_cast<int16_t>(band.x + inset - outset), static_cast<int16_t>(band.y - gap),
                            static_cast<int16_t>(lineWidth + 2 * outset), static_cast<int16_t>(band.height + 2 * gap)};
    screen.target().stroke(outline, fui::Paint::solid(fui::Color::Black), 2, theme.listRowRadius);
  }
}

void HighlightsActivity::render(RenderLock&&) {
  // Duplicates UiListActivity::render()'s body (chrome, app, rebuild-retry
  // loop, footer, display) with the delete-confirmation popup interleaved
  // between the app render and the footer -- exactly where
  // EpubReaderBookmarksActivity's own render() puts the same check, and for
  // the same reason: the base render() has no seam to inject it into.
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }

  // Chooser first: processRender paints the dialog AND calls
  // renderer.displayBuffer(), returning true (OptionPopup.h) -- calling both
  // and falling through would paint the footer over the dialog and trigger a
  // second e-ink refresh.
  if (actionChooser_.processRender(renderer, mappedInput)) return;
  if (confirmPopup_.processRender(renderer, mappedInput)) return;

  drawFooter();
  renderer.displayBuffer();
  PostedMessage::drawNext(renderer);
}
