#include "EpubReaderMenuActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/SettingsSave.h"
#include "components/ListRowHeight.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"

namespace fui = freeink::ui;
using ReaderMenuSheetLayout::Box;

namespace {

constexpr int TILE_ICON_SIZE = 32;
// Same corner and border weights as the launcher's tiles (LauncherActivity.cpp:53,399).
constexpr int TILE_RADIUS = 8;
constexpr int TILE_BORDER = 2;
constexpr int TILE_BORDER_SELECTED = 3;
// Pages per minute is a number in every language; an en dash reads as "none"
// without a translated "Off" crowding a half-width row.
constexpr const char* AUTO_TURN_OFF_VALUE = "\xe2\x80\x93";

StrId labelFor(const ReaderMenuAction action) {
  switch (action) {
    case ReaderMenuAction::SELECT_CHAPTER:
      return StrId::STR_GO_TO;
    case ReaderMenuAction::SEARCH_BIBLE:
      return StrId::STR_SEARCH;
    case ReaderMenuAction::TOGGLE_BOOKMARK:
      return StrId::STR_MARK;
    case ReaderMenuAction::HIGHLIGHT_PASSAGE:
      return StrId::STR_TAG;
    case ReaderMenuAction::FOOTNOTES:
      return StrId::STR_FOOTNOTES;
    case ReaderMenuAction::TEXT_SETTINGS:
      return StrId::STR_TEXT_SETTINGS;
    case ReaderMenuAction::NIGHT_MODE:
      return StrId::STR_NIGHT_MODE;
    case ReaderMenuAction::FRONTLIGHT:
      return StrId::STR_FRONTLIGHT;
    case ReaderMenuAction::GO_TO_PERCENT:
      return StrId::STR_GO_TO_PERCENT;
    case ReaderMenuAction::AUTO_PAGE_TURN:
      return StrId::STR_AUTO_TURN;
    case ReaderMenuAction::ROTATE_SCREEN:
      return StrId::STR_ORIENTATION;
    case ReaderMenuAction::BOOKMARKS:
      return StrId::STR_BOOKMARKS;
    case ReaderMenuAction::SCREENSHOT:
      return StrId::STR_SCREENSHOT_BUTTON;
    case ReaderMenuAction::GO_HOME:
      return StrId::STR_GO_HOME_BUTTON;
    case ReaderMenuAction::DELETE_CACHE:
      return StrId::STR_DELETE_CACHE;
    case ReaderMenuAction::HIGHLIGHTS:
      return StrId::STR_HIGHLIGHTS;
    case ReaderMenuAction::TAGS_HERE:
      return StrId::STR_TAGS_HERE;
    case ReaderMenuAction::OPEN_RECENT_PLACE:
      return StrId::STR_RECENT;
  }
  return StrId::STR_GO_TO;
}

fui::BitmapRef tileIconFor(const ReaderMenuAction action) {
  switch (action) {
    case ReaderMenuAction::SEARCH_BIBLE:
      return fui::bitmapFromIcon(icon_search_32);
    case ReaderMenuAction::TOGGLE_BOOKMARK:
      return fui::bitmapFromIcon(icon_bookmark_32);
    case ReaderMenuAction::HIGHLIGHT_PASSAGE:
      return fui::bitmapFromIcon(icon_tag_32);
    default:
      return fui::bitmapFromIcon(icon_grid_32);
  }
}

fui::Rect toFui(const Box& box) {
  return fui::Rect{static_cast<int16_t>(box.x), static_cast<int16_t>(box.y), static_cast<int16_t>(box.w),
                   static_cast<int16_t>(box.h)};
}

// target().text draws from the rect's top, so a label is centred by giving it a
// one-line rect in the middle of its band.
fui::Rect centredLine(const Box& box, const int lineHeight) {
  return fui::Rect{static_cast<int16_t>(box.x), static_cast<int16_t>(box.y + (box.h - lineHeight) / 2),
                   static_cast<int16_t>(box.w), static_cast<int16_t>(lineHeight)};
}

}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const bool hasHighlights, const bool isBible, const int tagsHereCount,
                                               const bool pageOnScreen,
                                               const ReaderMenuSheetLayout::RecentChipLabels& recent)
    : UiListActivity("EpubReaderMenu", renderer, mappedInput),
      model(ReaderMenuModel::build(ReaderMenuModel::Inputs{isBible, hasFootnotes, hasBookmarks, hasHighlights,
                                                           Frontlight.present(), BEREAN_CAP_ROTATION != 0,
                                                           tagsHereCount})),
      title(title),
      pageOnScreen(pageOnScreen),
      recent(recent),
      pendingOrientation(currentOrientation) {
  if (model.overflowed) {
    LOG_ERR("MENU", "Reader menu over capacity (%d quick, %d rows); extra items dropped", model.quickCount,
            model.rowCount);
  }
  snprintf(tagsHereValue, sizeof(tagsHereValue), "%d", tagsHereCount);
  buildRowItems();
}

void EpubReaderMenuActivity::buildRowItems() {
  for (int i = 0; i < model.rowCount; i++) {
    const int column = i % 2;
    const int slot = i / 2;
    if (slot >= COLUMN_CAPACITY) break;
    const auto action = model.row(i);
    fui::ListItem item;
    item.label = I18N.get(labelFor(action));
    item.actionValue = static_cast<int16_t>(model.quickCount + i);
    item.toggle = action == ReaderMenuAction::NIGHT_MODE || action == ReaderMenuAction::FRONTLIGHT;
    if (action == ReaderMenuAction::TAGS_HERE) item.value = tagsHereValue;
    columnItems[column][slot] = item;
    columnCount[column] = slot + 1;
  }
}

// Only the rows showing live state change between renders.
void EpubReaderMenuActivity::refreshRowStates() {
  for (int i = 0; i < model.rowCount; i++) {
    const int slot = i / 2;
    if (slot >= COLUMN_CAPACITY) break;
    auto& item = columnItems[i % 2][slot];
    switch (model.row(i)) {
      case ReaderMenuAction::NIGHT_MODE:
        item.toggleChecked = SETTINGS.screenInverted != 0;
        break;
      case ReaderMenuAction::FRONTLIGHT:
        item.toggleChecked = Frontlight.isOn();
        break;
      case ReaderMenuAction::AUTO_PAGE_TURN:
        item.value = selectedPageTurnOption == 0 ? AUTO_TURN_OFF_VALUE : pageTurnLabels[selectedPageTurnOption];
        break;
      case ReaderMenuAction::ROTATE_SCREEN:
        item.value = I18N.get(orientationLabels[pendingOrientation]);
        break;
      default:
        break;
    }
  }
}

void EpubReaderMenuActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_CLOSE, &EpubReaderMenuActivity::closeTrampoline, this);
  app.on(ACTION_RECENT, &EpubReaderMenuActivity::recentTrampoline, this);
}

void EpubReaderMenuActivity::onExit() {
  pageSnapshot.reset();
  logHeap("closed");
  UiListActivity::onExit();
}

void EpubReaderMenuActivity::closeTrampoline(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<EpubReaderMenuActivity*>(user);
  self->app.clearTapFlash();
  self->closeCancelled();
}

void EpubReaderMenuActivity::recentTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderMenuActivity*>(user);
  self->app.clearTapFlash();
  MenuResult result{static_cast<int>(ReaderMenuAction::OPEN_RECENT_PLACE), self->pendingOrientation,
                    self->selectedPageTurnOption, static_cast<int8_t>(event.value)};
  self->setResult(std::move(result));
  self->finish();
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::logHeap(const char* phase) const {
  LOG_INF("MENU", "heap %s: internal=%u psram=%u", phase, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

// Render task, first render (or the first after a rotation): the framebuffer
// holds the last completed frame, so this is the one moment the page can be
// saved.
void EpubReaderMenuActivity::decideMode() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  ListRowHeight::Inputs rowInputs;
  rowInputs.touch = mappedInput.hasTouch();
  rowInputs.tokenRowHeight = app.theme().rowHeight;
  rowInputs.minTouchSize = app.theme().minTouchSize;
  rowInputs.denseRow = metrics.listRowHeight;
  rowInputs.denseSubtitleRow = metrics.listWithSubtitleRowHeight;
  rowInputs.touchSingleRow = metrics.touchListRowHeight;

  ReaderMenuSheetLayout::Inputs in;
  in.safeX = safe.x;
  in.safeY = safe.y;
  in.safeW = safe.width;
  in.safeH = safe.height;
  in.titleHeight = metrics.headerHeight;
  in.gap = metrics.verticalSpacing;
  in.iconSize = TILE_ICON_SIZE;
  in.labelLineHeight = uiTarget.lineHeight(fui::GfxRendererTarget::FONT_BODY);
  in.minTouchSize = app.theme().minTouchSize;
  in.rowHeight = ListRowHeight::resolve(rowInputs);
  in.ruleWidth = metrics.popupFrameThickness;
  in.quickCount = model.quickCount;
  in.rowCount = model.rowCount;
  in.recentCount = recent.count;
  in.recentHeight = app.theme().minTouchSize;
  layout = ReaderMenuSheetLayout::compute(in);
  if (!layout.fitsAlone) {
    LOG_ERR("MENU", "Sheet taller than the screen (%d quick, %d rows); rows clipped", model.quickCount, model.rowCount);
  }

  logHeap("before");
  const char* reason = nullptr;
  if (rotated) {
    reason = "rotated";
  } else if (!pageOnScreen) {
    reason = "not-on-page";
  } else if (SETTINGS.screenInverted != 0) {
    // Night mode inverts at output; the framebuffer holds the page in normal
    // polarity while the panel shows it inverted.
    reason = "night";
  } else if (!renderer.hasFrameBuffer()) {
    reason = "no-fb";
  } else if (!layout.fitsOverPage) {
    reason = "no-fit";
  }

  if (!reason) {
    const size_t bytes = ReaderMenuSheetLayout::snapshotBytes(layout.page.w, layout.page.h);
#ifdef READER_MENU_FORCE_SNAPSHOT_OOM
    pageSnapshot.reset();
#else
    pageSnapshot = makeUniqueNoThrow<uint8_t[]>(bytes);
#endif
    if (!pageSnapshot || renderer.readFramebufferRegion(layout.page.x, layout.page.y, layout.page.w, layout.page.h,
                                                        pageSnapshot.get(), bytes) == 0) {
      LOG_ERR("MENU", "page snapshot unavailable (%u bytes); cleared sheet", static_cast<unsigned>(bytes));
      pageSnapshot.reset();
      reason = "oom";
    }
  }

  mode = reason ? SheetMode::Cleared : SheetMode::OverPage;
  LOG_DBG("MENU", "sheet mode: %s (%s)", reason ? "cleared" : "over-page", reason ? reason : "page saved");
  logHeap("open");
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  if (index < 0 || index >= model.count()) return;
  // The activated item leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  const auto selectedAction = model.items[index];
  if (selectedAction == ReaderMenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       {
                         RenderLock lock(*this);
                         pendingOrientation = idx;
                         // Rotate the menu immediately. Only the renderer turns;
                         // SETTINGS.orientation stays unchanged so the reader's
                         // result handler still detects the change and reflows.
                         ReaderUtils::applyOrientation(renderer, pendingOrientation);
                         app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                         // The saved region and the geometry belong to the old frame.
                         pageSnapshot.reset();
                         rotated = true;
                         firstPaintDone = false;
                         mode = SheetMode::Undecided;
                       }
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == ReaderMenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == ReaderMenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    saveSettingsOrReport();
    requestUpdate();
    return;
  }

  if (selectedAction == ReaderMenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    saveSettingsOrReport();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // app.route has already dispatched the action (rows and tiles to
  // activateIndex, the page and the close control to closeTrampoline), so the
  // pass ends here or the base loop would route the same release again. A
  // routed release that hit nothing -- the Back swipe ends at (-1, -1) -- falls
  // through so handleButtons still sees it.
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (!route) return false;
  if (route.event.action == ACTION_CHROME) {
    // A tap in a gap of the sheet: nothing to show, so no refresh.
    app.clearTapFlash();
    return true;
  }
  if (app.invalidated()) requestUpdate();
  return true;
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

// Release steps only: nothing scrolls, so there is no page to jump by.
void EpubReaderMenuActivity::navigateButtons() {
  const int count = listCount();
  buttonNavigator.onNextRelease([this, count] { moveSelectionTo(ButtonNavigator::nextIndex(nav.selected, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count] { moveSelectionTo(ButtonNavigator::previousIndex(nav.selected, count)); });
}

void EpubReaderMenuActivity::drawTile(UiScreen& screen, const int index, const Box& box) {
  const auto& theme = screen.theme();
  const auto action = model.items[index];
  screen.frame().hit(toFui(box), ACTION_ROW, static_cast<int16_t>(index), fui::InputTouch);
  renderer.drawRoundedRect(box.x, box.y, box.w, box.h, nav.selected == index ? TILE_BORDER_SELECTED : TILE_BORDER,
                           TILE_RADIUS, true);

  const fui::Rect iconRect{static_cast<int16_t>(box.x), static_cast<int16_t>(box.y + layout.gap),
                           static_cast<int16_t>(box.w), static_cast<int16_t>(layout.iconSize)};
  screen.target().bitmap(iconRect, tileIconFor(action), fui::BitmapMode::Center);

  const int lineHeight = screen.target().lineHeight(fui::GfxRendererTarget::FONT_BODY);
  const fui::Rect labelRect{static_cast<int16_t>(box.x + theme.spaceXs),
                            static_cast<int16_t>(iconRect.bottom() + layout.gap / 2),
                            static_cast<int16_t>(box.w - 2 * theme.spaceXs), static_cast<int16_t>(lineHeight)};
  fui::TextStyle labelStyle = theme.bodyText;
  labelStyle.align = fui::TextAlign::Center;
  screen.target().text(labelRect, I18N.get(labelFor(action)), labelStyle);
}

// The mockup's Recent row: a bold caption, then pill chips sized to their labels, as the
// Highlights tag chips are drawn (HighlightsActivity::buildChipRow).
void EpubReaderMenuActivity::drawRecentBand(UiScreen& screen) {
  const auto& theme = screen.theme();
  const Box& band = layout.recent;

  fui::TextStyle captionStyle = theme.smallText;
  captionStyle.bold = true;
  const char* caption = I18N.get(StrId::STR_RECENT);
  const int captionWidth = screen.target().measureText(theme.smallText.font, caption, captionStyle).width;
  const int lineHeight = screen.target().lineHeight(theme.smallText.font);
  screen.target().text(centredLine(Box{band.x, band.y, captionWidth, band.h}, lineHeight), caption, captionStyle);

  const int padX = theme.spaceMd;
  const int gap = theme.spaceSm;
  const int chipHeight = lineHeight + 2 * theme.spaceSm;
  int widths[ReaderMenuSheetLayout::MAX_RECENT_CHIPS] = {};
  for (int i = 0; i < recent.count; ++i) {
    const int textWidth = screen.target().measureText(theme.smallText.font, recent.text[i], theme.smallText).width;
    widths[i] = std::max(textWidth + 2 * padX, chipHeight);
  }
  const int shown = ReaderMenuSheetLayout::fitChips(captionWidth, widths, recent.count, band.w, gap);

  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.normal.border = fui::Paint::solid(fui::Color::Black);
  styles.normal.borderWidth = 1;
  styles.normal.radius = static_cast<uint8_t>(std::min(chipHeight / 2, 255));
  styles.selected = styles.normal;
  styles.selected.background = fui::Paint::solid(fui::Color::Black);
  styles.selected.foreground = fui::Paint::solid(fui::Color::White);
  styles.focused = styles.selected;
  styles.active = styles.selected;
  styles.disabled = styles.normal;

  // The pill is drawn at text height; its hit rect fills the band, a touch target tall.
  const int padY = (band.h - chipHeight) / 2;
  int x = band.x + captionWidth + gap;
  for (int i = 0; i < shown; ++i) {
    fui::ButtonProps props;
    props.label = recent.text[i];
    props.action = ACTION_RECENT;
    props.value = static_cast<int16_t>(i);
    props.inputMask = fui::InputTouch;
    props.state = fui::StateNormal;
    props.text = theme.smallText;
    props.styles = styles;
    props.minTouchSize = 0;
    props.hitPadding = fui::Insets{static_cast<int16_t>(padY), static_cast<int16_t>(gap / 2),
                                   static_cast<int16_t>(padY), static_cast<int16_t>(gap / 2)};
    const fui::Rect rect{static_cast<int16_t>(x), static_cast<int16_t>(band.y + padY), static_cast<int16_t>(widths[i]),
                         static_cast<int16_t>(chipHeight)};
    fui::button(screen.frame(), rect, props);
    x += widths[i] + gap;
  }
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  refreshRowStates();
  const auto& theme = screen.theme();
  const int lineHeight = screen.target().lineHeight(fui::GfxRendererTarget::FONT_BODY);

  // Registration order matters: routing scans newest-first, so the controls
  // win inside the plate, the plate guard swallows its gaps, and only the page
  // itself closes.
  screen.frame().hit(toFui(layout.page), ACTION_CLOSE, 0, fui::InputTouch);
  screen.frame().hit(toFui(layout.plate), ACTION_CHROME, 0, fui::InputTouch);

  fui::TextStyle titleStyle = theme.bodyText;
  titleStyle.bold = true;
  screen.target().text(centredLine(layout.title, lineHeight), title.c_str(), titleStyle);

  screen.frame().hit(toFui(layout.close), ACTION_CLOSE, 0, fui::InputTouch);
  screen.target().bitmap(toFui(layout.close), fui::bitmapFromIcon(icon_close_24), fui::BitmapMode::Center);

  for (int i = 0; i < layout.tileCount && i < model.quickCount; i++) drawTile(screen, i, layout.tiles[i]);
  if (recent.count > 0) drawRecentBand(screen);

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int selectedRow = nav.selected - model.quickCount;
  for (int column = 0; column < 2; column++) {
    if (columnCount[column] == 0) continue;
    const Box& band = layout.columns[column];
    screen.setContentMargin(fui::Insets{static_cast<int16_t>(band.y), static_cast<int16_t>(screenWidth - band.right()),
                                        static_cast<int16_t>(screenHeight - band.bottom()),
                                        static_cast<int16_t>(band.x)});
    fui::ListProps props;
    props.items = columnItems[column];
    props.count = static_cast<uint16_t>(columnCount[column]);
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
    // Every size is explicit: the theme's defaults (a 66 px row token, a 20 px
    // band inset per side, a scroll track) would hide rows or squeeze labels
    // in a half-width column.
    props.rowHeight = static_cast<int16_t>(layout.rowHeight);
    props.rowGap = 0;
    props.rowInset = theme.spaceSm;
    props.sidePadding = theme.spaceSm;
    props.valueInset = theme.spaceSm;
    props.scrollIndicator = false;
    props.selectedIndex = static_cast<int16_t>(selectedRow >= 0 && selectedRow % 2 == column ? selectedRow / 2 : -1);
    screen.list(props, static_cast<int16_t>(band.h));
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  if (mode.load() == SheetMode::Undecided) decideMode();

  const bool overPage = mode.load() == SheetMode::OverPage && pageSnapshot;
  if (overPage) {
    renderer.writeFramebufferRegion(layout.page.x, layout.page.y, layout.page.w, layout.page.h, pageSnapshot.get());
  } else {
    renderer.clearScreen();
  }
  // Opaque plate: text never sits over page pixels.
  renderer.fillRect(layout.plate.x, layout.plate.y, layout.plate.w, layout.plate.h, false);
  renderer.fillRect(layout.rule.x, layout.rule.y, layout.rule.w, layout.rule.h, true);

  renderUi();

  // Over the page only the sheet differs from the panel, so a fast refresh
  // drives just that region. A cleared screen is a clean entry and gets one
  // half refresh.
  const bool cleanEntry = !overPage && !firstPaintDone;
  renderer.displayBuffer(cleanEntry ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstPaintDone = true;
}
