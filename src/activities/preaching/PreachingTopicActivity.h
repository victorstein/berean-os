#pragma once

#include <atomic>
#include <memory>

#include "PreachingCardLayout.h"
#include "PreachingCardNav.h"
#include "PreachingContent.h"
#include "PreachingOrigin.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class PreachingSession;

// A question or a tag topic as paged cards. Left/Right select Verse cards, turning the page at its
// edge; a vertical swipe turns the page. Tapping a quote opens show mode, a reference opens the
// Bible, and "Para la revisita" replaces the question in place. Back returns to the list with a
// PreachingResult. Drawn with the renderer, with UiAppHost hit rects over the cards
// (ClockOffsetActivity's shape).
class PreachingTopicActivity final : public Activity, private UiAppHost {
 public:
  // A negative `startVerse` opens on the first page; otherwise on that Verse card, selected.
  PreachingTopicActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const PreachingSession& session,
                         std::unique_ptr<TopicCards> cards, int startVerse);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Geometry {
    int bodyTop = 0;
    int pageHeight = 0;
    int cardX = 0;
    int cardWidth = 0;
    int textX = 0;
    int textWidth = 0;
    int cardPadding = 0;
    int cardGap = 0;
    int cornerRadius = 0;
    int markWidth = 0;
  };

  // Show mode at `verse`; the Verse card it returns is selected on the way back.
  void openShow(int verse);
  void onShowFinished(const ActivityResult& result);
  void openBible(int verse);
  void openNextQuestion();

  void computeGeometry();
  bool layOut(const TopicCards& cards, preaching::Layout& out) const;
  void moveTo(const preaching::CardCursor& next);
  void finishWithResult();
  PreachingOrigin originAt(int verse) const;
  void reportFailure(const char* message);

  void drawPage() const;
  void drawLine(const preaching::CardInput& card, const preaching::Line& line, int y) const;
  void buildHits(UiScreen& screen) const;
  static void topicScreen(UiScreen& screen, void* user);
  static void onQuoteTapped(const freeink::ui::ActionEvent& event, void* user);
  static void onReferenceTapped(const freeink::ui::ActionEvent& event, void* user);
  static void onNextVisitTapped(const freeink::ui::ActionEvent& event, void* user);

  const PreachingSession& session;
  std::unique_ptr<TopicCards> topic;
  std::unique_ptr<preaching::Layout> layout;
  preaching::CardCursor cursor;
  int startVerse;
  Geometry geometry;
  ButtonNavigator buttonNavigator;
  std::atomic<bool> halfRefreshPending{true};
};
