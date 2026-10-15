#pragma once

#include "PreachingShowLayout.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// One Verse card full screen, as large as it fits, for the householder. Pushed over the topic
// screen with startActivityForResult; it views the topic's cards and returns, as a PreachingResult,
// the Verse ordinal it ended on. Centred as StudySleepScreen sets its passage.
class PreachingShowActivity final : public Activity {
 public:
  // `deck` and `title` view the topic beneath, which outlives this screen.
  PreachingShowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, preaching::show::Deck deck,
                        const char* title, int startVerse);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Geometry {
    int boxTop = 0;
    int footerY = 0;
    int columnWidth = 0;
  };

  void computeGeometry();
  void layOutVerse();
  void stepBy(int direction);
  void finishWithResult();
  void drawPage() const;
  void drawFooter() const;

  preaching::show::Deck deck;
  const char* title;
  int startVerse;
  int verses = 0;
  preaching::show::Cursor cursor;
  preaching::show::Metrics metrics;
  preaching::show::Layout layout;
  Geometry geometry;
  ButtonNavigator buttonNavigator;
  int pagesUntilFullRefresh = 0;
};
