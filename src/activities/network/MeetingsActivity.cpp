#include "MeetingsActivity.h"

#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include "activities/network/MeetingDownloadActivity.h"
#include "components/UITheme.h"
#include "network/MeetingLibrary.h"
#include "network/MeetingWeekCache.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "MEETINGS";

const char* nameFor(const MeetingPub pub) {
  return pub == MeetingPub::Watchtower ? tr(STR_MEETING_WATCHTOWER) : tr(STR_MEETING_WORKBOOK);
}

}  // namespace

MeetingsActivity::MeetingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Meetings", renderer, mappedInput) {
  rows_[0].pub = MeetingPub::Watchtower;
  rows_[1].pub = MeetingPub::Workbook;
}

const char* MeetingsActivity::headerTitle() const { return tr(STR_MEETINGS); }

void MeetingsActivity::onEnter() {
  UiListActivity::onEnter();
  refresh();
}

void MeetingsActivity::refresh() {
  HalClock::Date today{};
  IsoWeek week;
  const bool haveWeek = halClock.getDate(today) && isoWeekFromUtcDate(today.year, today.month, today.day, week);

  MeetingWeekTable table;
  MeetingWeekCache::load(table);

  const MeetingWeekEntry* entry = nullptr;
  if (haveWeek) {
    const std::string key = meetingWeekKey(week);
    entry = table.find(key);
    // At most one automatic resolve per visit. refresh() also runs when the
    // download returns, and a resolve that failed -- no wifi, nothing on the
    // page -- leaves the week exactly as absent as it was, so without this the
    // screen would relaunch the download forever.
    resolvePending_ = entry == nullptr && !resolveAttempted_;
    if (entry == nullptr) entry = table.newest();
  } else {
    // Without the clock there is no current week to be stale against, so a
    // resolve could only guess at which week to ask for.
    LOG_ERR(MODULE, "No usable date; showing the newest week held");
    entry = table.newest();
  }

  for (Row& row : rows_) {
    row.issue.clear();
    if (entry != nullptr) {
      row.issue = row.pub == MeetingPub::Watchtower ? entry->watchtower : entry->workbook;
    }
    row.path = MeetingLibrary::findPublication(row.pub, row.issue);

    row.label = nameFor(row.pub);
    if (!row.issue.empty()) row.label += "  " + row.issue;

    if (!row.path.empty()) {
      row.subtitle = tr(STR_OPEN);
    } else if (!row.issue.empty()) {
      row.subtitle = tr(STR_DOWNLOAD);
    } else if (entry != nullptr && row.pub == MeetingPub::Workbook) {
      row.subtitle = tr(STR_WORKBOOK_UNAVAILABLE);
    } else {
      row.subtitle = tr(STR_MEETING_WEEK_UNKNOWN);
    }
  }
}

void MeetingsActivity::loop() {
  if (resolvePending_) {
    resolvePending_ = false;
    // Force the cached rows onto the panel before handing off. The resolve
    // fetches and scrapes a page with no progress or cancel hook, so whatever
    // is on screen when it starts is what the user looks at until it returns.
    requestUpdateAndWait();
    startDownload();
    return;
  }
  UiListActivity::loop();
}

void MeetingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item{};
    item.label = rows_[i].label.c_str();
    item.subtitle = rows_[i].subtitle.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void MeetingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;
  const Row& row = rows_[static_cast<size_t>(index)];

  if (!row.path.empty()) {
    activityManager.goToReader(row.path);
    return;
  }
  // Nothing to open and nothing known to fetch: the week itself is what is
  // missing, so resolving is the only useful thing this row can do.
  startDownload();
}

void MeetingsActivity::startDownload() {
  resolveAttempted_ = true;
  startActivityForResult(std::make_unique<MeetingDownloadActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    refresh();
    requestUpdate();
  });
}
