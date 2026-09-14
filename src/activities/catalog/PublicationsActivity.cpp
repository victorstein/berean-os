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
    : UiListActivity("Publications", renderer, mappedInput) {}

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
  searchItem.actionValue = 0;
  rowItems_[0] = searchItem;

  for (size_t i = 0; i < entries_.size(); ++i) {
    fui::ListItem item{};
    item.label = entries_[i].label.c_str();
    if (!entries_[i].subtitle.empty()) item.subtitle = entries_[i].subtitle.c_str();
    item.actionValue = static_cast<int16_t>(i + 1);
    rowItems_[i + 1] = item;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void PublicationsActivity::activateIndex(const int index) {
  if (index == 0) {
    openSearch();
    return;
  }
  const size_t entry = static_cast<size_t>(index - 1);
  if (entry >= entries_.size()) return;
  activityManager.goToReader(entries_[entry].path);
}

void PublicationsActivity::openSearch() {
  startActivityForResult(std::make_unique<CatalogSearchActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) {
                           // A download while search was open is a new row here.
                           refresh();
                           requestUpdate();
                         });
}
