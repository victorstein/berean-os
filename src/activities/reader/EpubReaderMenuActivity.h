#pragma once
#include <Epub.h>
#include <I18n.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ReaderMenuModel.h"
#include "ReaderMenuSheetLayout.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The reader menu as a sheet over the lower part of the page. Over the page
// when the framebuffer still holds it, otherwise the same sheet on a cleared
// screen; never a scrolling list.
class EpubReaderMenuActivity final : public UiListActivity {
 public:
  using MenuAction = ReaderMenuAction;

  // pageOnScreen: the framebuffer holds the reading page, not a sub-screen's
  // last frame, so the sheet may be drawn over it.
  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  uint8_t currentOrientation, bool hasFootnotes, bool hasBookmarks, bool hasHighlights,
                                  bool isBible, int tagsHereCount, bool pageOnScreen,
                                  const ReaderMenuSheetLayout::RecentChipLabels& recent);

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  // Decided on the first render, when the framebuffer is known to hold the
  // last completed frame. Only ever degrades, OverPage -> Cleared, via
  // Undecided after a rotation.
  enum class SheetMode : uint8_t { Undecided, OverPage, Cleared };

  static constexpr freeink::ui::ActionId ACTION_CLOSE = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_CHROME = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_RECENT = ACTION_USER + 2;
  static constexpr int COLUMN_CAPACITY = ReaderMenuModel::MAX_ROWS / 2;

  int listCount() const override { return model.count(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;

  static void closeTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void recentTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void closeCancelled();
  void buildRowItems();
  void refreshRowStates();
  void decideMode();
  void logHeap(const char* phase) const;
  void drawTile(UiScreen& screen, int index, const ReaderMenuSheetLayout::Box& box);
  void drawRecentBand(UiScreen& screen);

  const ReaderMenuModel::Model model;
  const std::string title;
  const bool pageOnScreen;
  const ReaderMenuSheetLayout::RecentChipLabels recent;

  // Rows split row-major: row i sits in column i % 2, slot i / 2. ListProps
  // has no stride, so each column needs its own contiguous array.
  freeink::ui::ListItem columnItems[2][COLUMN_CAPACITY]{};
  int columnCount[2]{};
  char tagsHereValue[8]{};

  ReaderMenuSheetLayout::Layout layout{};
  // Written on the render task, read on the loop task.
  std::atomic<SheetMode> mode{SheetMode::Undecided};
  bool rotated = false;
  bool firstPaintDone = false;
  // The page region above the sheet (PSRAM), restored on every render so a
  // popup drawn over it never lingers.
  std::unique_ptr<uint8_t[]> pageSnapshot;

  OptionPopup optionPopup;
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
};
