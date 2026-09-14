#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// bereanOS's home screen: four sections and a resume strip.
//
// A launcher is a screen you pass THROUGH, and on e-ink every pass costs a full
// panel refresh. The resume strip is what pays for that -- the common case,
// waking the device to carry on reading, is one tap and never touches a tile.
//
// Tiles carry no counts. An earlier design put "12 etiquetas - 248 pasajes"
// here, which needs denormalised counters that nothing reconciles; for a device
// whose value is being a trustworthy record, a visibly wrong number is worse
// than no number. Live state that cannot drift (which meeting week is loaded)
// stays.
class LauncherActivity final : public Activity {
 public:
  explicit LauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Launcher", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  // Order is the navigation order, and the layout order: Bible spans the width,
  // Meetings and Search share a row, Tags and settings spans the width again.
  enum class Tile : uint8_t { Bible, Meetings, Search, TagsAndSettings, Resume, COUNT };

  struct TileRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  };

  void computeLayout();
  void activate(Tile tile);
  void openBible();
  void openMeetings();
  void openSearch();
  void openTagsAndSettings();
  void drawTile(const TileRect& rect, const char* title, const char* subtitle, bool selected, bool emphasised) const;

  // Resolved once on entry: the resume strip needs a book, and the Bible tile
  // needs to know whether one is on the card before offering to open it.
  void resolveTargets();

  ButtonNavigator buttonNavigator;
  int selected = 0;
  TileRect rects[static_cast<size_t>(Tile::COUNT)];

  std::string biblePath;
  std::string bibleSubtitle;
  std::string resumePath;
  std::string resumeTitle;
  bool hasResume = false;
};
