#include "HighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <utility>
#include <variant>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "TagFilterActivity.h"
#include "TagPickerActivity.h"
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
    if (filterTagId_) {
      const auto& tags = passages[i].tags;
      if (std::find(tags.begin(), tags.end(), *filterTagId_) == tags.end()) continue;
    }
    visibleIndices_.push_back(i);
  }
}

std::string HighlightsActivity::computeFilterSubtitle() const {
  if (!filterTagId_) return tr(STR_TAG_FILTER_ALL);
  const std::string& name = STUDY.palette().name(*filterTagId_);
  return name.empty() ? tr(STR_TAG_FILTER_ALL) : name;
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
  rowItems_.reserve(visibleIndices_.size() + 1);

  filterSubtitle_ = computeFilterSubtitle();
  fui::ListItem filterRow{};
  filterRow.label = tr(STR_FILTER_BY_TAG);
  filterRow.subtitle = filterSubtitle_.c_str();
  filterRow.actionValue = 0;
  rowItems_.push_back(filterRow);

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
}

void HighlightsActivity::openTagFilter() {
  if (STUDY.palette().activeCount() == 0) return;  // nothing to filter by; stays on "All"

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
                           // the id is stable -- but a filter on a now-retired tag would show an
                           // empty list forever, so it is dropped.
                           if (filterTagId_ && !STUDY.palette().isActive(*filterTagId_)) filterTagId_.reset();

                           // Always rebuilt, including on cancel: a retirement changes the rows
                           // and the labels they borrow even when the filter is untouched.
                           rebuildVisibleIndices();
                           rebuildRowItems();
                           moveSelectionTo(0);
                         });
}

void HighlightsActivity::jumpToHighlight(const size_t docIndex) {
  if (docIndex >= STUDY.passages().size()) return;
  const auto& entry = STUDY.passages()[docIndex];

  // The stored address is a Unit, so the document offset is resolved now rather
  // than stored. A passage whose document cannot be indexed has no offset to
  // jump to; open the document anyway rather than doing nothing.
  const auto offset = STUDY.documentOffsetFor(docIndex);

  // hasVisibleTextOffset plus the resolved offset routes through the reader's
  // existing offset-based jump branch (immune to re-pagination); see the class
  // comment for why this bypasses progressChangeResultHandler.
  ProgressChangeResult result;
  result.spineIndex = static_cast<int>(entry.documentSpine);
  result.hasVisibleTextOffset = offset.has_value();
  result.visibleTextOffset = offset.value_or(0);
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

void HighlightsActivity::onRowLongPress(const int index) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;
  if (index <= 0 || index >= listCount()) return;  // row 0 is the filter control; nothing to act on
  app.clearTapFlash();
  activeNav().selected = index;
  showActionChooser(visibleIndices_[static_cast<size_t>(index - 1)]);
}

void HighlightsActivity::showActionChooser(const size_t docIndex) {
  if (confirmPopup_.isActive() || actionChooser_.isActive()) return;

  pendingActionIndex_ = docIndex;
  choosingAction_ = true;

  if (STUDY.saveDisabled()) {
    // Retagging writes the document, so a book whose file failed to load
    // (the resident doc was built from scratch this session) must never see
    // "Tags..." at all -- omit it rather than offer it and bail inside
    // editTags. Delete still shows: showDeleteConfirmation carries its own
    // its own saveDisabled bail below, same as before this task.
    const char* options[] = {tr(STR_DELETE), tr(STR_CANCEL)};
    actionChooser_.show(tr(STR_HIGHLIGHT_ACTIONS), options, 2, 0, [this](const int idx) {
      choosingAction_ = false;
      if (idx == 0) {
        // See the "clean repaint" comment below for why this precedes
        // showDeleteConfirmation.
        requestUpdateAndWait();
        showDeleteConfirmation(pendingActionIndex_);
      }
      requestUpdate();
    });
  } else {
    const char* options[] = {tr(STR_EDIT_TAGS), tr(STR_DELETE), tr(STR_CANCEL)};
    actionChooser_.show(tr(STR_HIGHLIGHT_ACTIONS), options, 3, 0, [this](const int idx) {
      choosingAction_ = false;
      if (idx == 0) {
        // Pushes a whole new activity, which repaints the screen from
        // scratch on entry -- no leftover-chooser-pixels hazard here.
        editTags(pendingActionIndex_);
      } else if (idx == 1) {
        // actionChooser_ (3 rows) is taller than confirmPopup_ (2 rows) and
        // both draw over the current screen without clearing it
        // (OptionPopup's class comment), so confirmPopup_ would otherwise be
        // framed by actionChooser_'s leftover pixels. Force one synchronous
        // clean repaint of the underlying list -- with neither popup active
        // -- before showDeleteConfirmation shows confirmPopup_ on top of it.
        // Safe from inside this callback: it runs on the loop task with no
        // RenderLock held (ActivityManager.cpp's three requestUpdateAndWait
        // asserts all pass here), mirroring
        // PassageSelectActivity::showActionChooser's identical use.
        requestUpdateAndWait();
        showDeleteConfirmation(pendingActionIndex_);
      }
      requestUpdate();
    });
  }
  requestUpdate();
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
    if (!STUDY.setPassageTags(docIndex, selection.tagIds) && !selection.tagIds.empty()) {
      ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_SAVE_FAILED));
    }
  }

  // The picker persists palette changes itself, so even a cancelled edit may
  // have retired the tag being filtered on. Ids are stable, so there is nothing
  // to re-resolve -- only to drop if it is no longer active.
  if (filterTagId_ && !STUDY.palette().isActive(*filterTagId_)) filterTagId_.reset();
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
  // filtered tag itself deleted); moveSelectionTo issues its own
  // requestUpdate().
  moveSelectionTo(std::clamp(activeNav().selected, 0, listCount() - 1));
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

  moveSelectionTo(std::clamp(activeNav().selected, 0, listCount() - 1));
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
    // highlight row (not row 0, the filter control, which has nothing to
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

  // Nothing to browse or filter: skip the filter row entirely rather than
  // show a control that can only ever read "All" over an empty list.
  if (STUDY.passages().empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

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
  // Tap opens/cycles; long-press deletes (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  // Theme FIRST: Screen::list only substitutes the theme font into a style that
  // still passes textStyleUnset (FreeInkUICore.h:550-554), and maxLines != 1
  // fails that test. Setting maxLines alone would skip the substitution and
  // render the subtitle in font 0.
  props.subtitleText = screen.theme().smallText;
  // The passage owns the subtitle band alone, so two lines of it is the whole
  // budget; tags sit in the value slot on the label line.
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
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
}
