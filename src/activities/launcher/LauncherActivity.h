#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
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
  void openTagsAndSettings();
  void drawTile(const TileRect& rect, const char* title, const char* subtitle, bool selected, bool emphasised,
                const std::string& coverPath, const uint8_t* icon) const;
  // A publication's own cover when the card has one, else the tile's icon.
  // Covers are what make the launcher legible at a glance -- a shelf of books
  // rather than a list of words -- so the icon is the fallback, not the default.
  void drawTileArt(int x, int y, int w, int h, const std::string& coverPath, const uint8_t* icon) const;
  void drawCoverTile(const TileRect& rect, const std::string& coverPath, const char* title, const char* subtitle,
                     const uint8_t* icon, bool selected, float focusBand) const;
  bool drawCoverFilling(const std::string& coverPath, const TileRect& rect, int visibleHeight, float focusBand) const;
  void drawCenteredIn(int x, int w, int top, const char* title, const char* subtitle) const;
  // Draws the cover at its stored size, or returns 0 without drawing. Never
  // rescales: the thumbnails are dithered 1-bit and resampling destroys them.
  int drawCoverNative(const std::string& coverPath, int x, int y, int boxWidth, int boxHeight) const;
  int tileArtHeight(const TileRect& rect, bool hasSubtitle) const;
  static int coverFillHeight(const TileRect& tile);
  static std::string coverThumbFor(const std::string& bookPath, int height, bool& generatedAny);
  // Finds a meeting publication the registry does not know about, for downloads
  // that predate it. Keyed on the downloader's own filename convention.
  static std::optional<std::string> findMeetingPublicationOnCard();
  static std::string meetingIssueOf(const std::string& filename);
  // Height of a tile's text block, so computeLayout can size a tile around its
  // contents and drawTile can centre the same block inside it.
  int tileTextHeight(int titleFont, bool hasSubtitle) const;

  // Resolved once on entry: the resume strip needs a book, and the Bible tile
  // needs to know whether one is on the card before offering to open it.
  void resolveTargets();

  ButtonNavigator buttonNavigator;
  int selected = 0;
  TileRect rects[static_cast<size_t>(Tile::COUNT)];

  std::string biblePath;
  std::string bibleSubtitle;
  std::string bibleCoverPath;
  std::string meetingsSubtitle;
  std::string meetingsCoverPath;
  std::string resumePath;
  std::string resumeTitle;
  bool hasResume = false;
};
