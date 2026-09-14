#include "PublicationsActivity.h"

#include <I18n.h>
#include <Utf8.h>

#include <algorithm>

#include "CatalogSearchActivity.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "study/PubKeyRegistry.h"
#include "util/CardBooks.h"

namespace fui = freeink::ui;

PublicationsActivity::PublicationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Publications", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

int PublicationsActivity::entryIndexForRow(const int row) const {
  if (row < FIRST_BOOK_ROW) return -1;
  const int entry = row - FIRST_BOOK_ROW;
  return entry < static_cast<int>(entries_.size()) ? entry : -1;
}

const char* PublicationsActivity::headerTitle() const { return tr(STR_PUBLICATIONS); }

void PublicationsActivity::onEnter() {
  UiListActivity::onEnter();
  refresh();
}

void PublicationsActivity::refresh() {
  entries_.clear();
  RECENT_BOOKS.loadFromFile();
  const auto& recents = RECENT_BOOKS.getBooks();

  for (const std::string& path : CardBooks::list()) {
    Entry entry;
    entry.path = path;

    // A title only exists for a book that has been opened; the filename is what
    // the downloader named it after, so it is a fair label until then.
    const auto opened =
        std::find_if(recents.begin(), recents.end(), [&](const RecentBook& book) { return book.path == path; });
    entry.label = opened != recents.end() && !opened->title.empty() ? utf8SafeSummary(opened->title, 48)
                                                                    : CardBooks::displayStem(path);

    if (const auto registered = PubKeyRegistry::lookup(path)) {
      entry.subtitle = registered->symbol;
      if (!registered->issue.empty()) entry.subtitle += "  " + registered->issue;
    }
    entries_.push_back(std::move(entry));
  }
}

void PublicationsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  rowItems_.assign(static_cast<size_t>(listCount()), fui::ListItem{});

  fui::ListItem searchItem{};
  searchItem.label = tr(STR_CATALOG_SEARCH_ROW);
  searchItem.subtitle = tr(STR_CATALOG_SEARCH_HINT);
  searchItem.actionValue = SEARCH_ROW;
  rowItems_[SEARCH_ROW] = searchItem;

  fui::ListItem headerItem{};
  headerItem.label = tr(STR_ON_THIS_DEVICE);
  headerItem.isHeader = true;
  headerItem.actionValue = HEADER_ROW;
  rowItems_[HEADER_ROW] = headerItem;

  for (size_t i = 0; i < entries_.size(); ++i) {
    fui::ListItem item{};
    item.label = entries_[i].label.c_str();
    if (!entries_[i].subtitle.empty()) item.subtitle = entries_[i].subtitle.c_str();
    // The ROW, not the entry: UiListActivity assigns this straight to
    // nav.selected, so an entry index here would desync the viewport.
    item.actionValue = static_cast<int16_t>(FIRST_BOOK_ROW + i);
    rowItems_[FIRST_BOOK_ROW + i] = item;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  // Long-press deletes; the physical-button path stays in loop().
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void PublicationsActivity::activateIndex(const int index) {
  if (index == SEARCH_ROW) {
    openSearch();
    return;
  }
  const int entry = entryIndexForRow(index);
  if (entry < 0) return;
  activityManager.goToReader(entries_[static_cast<size_t>(entry)].path);
}

void PublicationsActivity::onRowLongPress(const int index) {
  const int entry = entryIndexForRow(index);
  if (entry >= 0) confirmDelete(static_cast<size_t>(entry));
}

void PublicationsActivity::confirmDelete(const size_t entryIndex) {
  if (confirmPopup_.isActive() || entryIndex >= entries_.size()) return;

  pendingDeleteEntry_ = entryIndex;
  confirmingDelete_ = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup_.show(tr(STR_CONFIRM_DELETE_PUBLICATION), options, 2, 0, [this](const int idx) {
    confirmingDelete_ = false;
    if (idx == 1) deleteEntry(pendingDeleteEntry_);
    requestUpdate();
  });
  requestUpdate();
}

void PublicationsActivity::deleteEntry(const size_t entryIndex) {
  if (entryIndex >= entries_.size()) return;
  CardBooks::remove(entries_[entryIndex].path);
  refresh();
  // The deleted row is gone; keep the selection inside the list it left behind.
  auto& listNav = activeNav();
  listNav.selected = std::min(listNav.selected, std::max(0, listCount() - 1));
}

bool PublicationsActivity::handleCustomInput() {
  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete_) {
    // Dismissed without choosing -- Back, or a tap outside the popup. Cancel the
    // pending delete rather than letting it stand.
    confirmingDelete_ = false;
    requestUpdate();
    return true;
  }
  return false;
}

void PublicationsActivity::render(RenderLock&& lock) {
  if (confirmPopup_.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void PublicationsActivity::openSearch() {
  startActivityForResult(std::make_unique<CatalogSearchActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    // A download while search was open is a new row here.
    refresh();
    requestUpdate();
  });
}
