#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PreachingOrigin.h"
#include "PreachingSession.h"
#include "activities/UiTabListActivity.h"

struct TopicCards;

// Predicación: the Bible's twenty preaching questions (Preguntas) and the owner's picked tags
// (Mis temas). Opening one pushes PreachingTopicActivity. Opened from Home with no origin, or
// rebuilt from the reader's origin with the topic pushed again at its card.
class PreachingActivity final : public UiTabListActivity {
 public:
  PreachingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const PreachingOrigin& origin = {});

  void onEnter() override;
  void onExit() override;

 private:
  enum class Tab : uint8_t { Questions, Topics, Count };

  int listCount() const override;
  int tabCount() const override { return static_cast<int>(Tab::Count); }
  int activeTab() const override { return static_cast<int>(tab); }
  const char* tabLabel(int index) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

  void openTopicPicker();
  // The picker's chips are STUDY's palette, and its retire writes that palette over tags.json, so
  // STUDY is opened on the Bible first; only the reader opens it otherwise.
  void ensureStudyOpen();
  // Reloads and recounts only when the picks changed.
  void onTopicPickerResult(const ActivityResult& result);
  // Reads the picks again and recounts, for after Elegir temas saves.
  void onTopicsChanged();

  void showTab(Tab next);
  // The caller holds the RenderLock: buildScreen reads the rows on the render task.
  void rebuildRows();
  void seedSelection();
  // False when the topic could not be opened; the failure has been reported. A negative `verse`
  // opens the topic on its first page.
  bool openQuestion(int question, int verse);
  bool openTag(study::TagId tag, int verse);
  bool pushTopic(std::unique_ptr<TopicCards> cards, int verse);
  void reportOpenFailure(const char* message);
  void onTopicResult(const ActivityResult& result);

  PreachingOrigin origin;
  PreachingSession session;
  Tab tab = Tab::Questions;
  std::string chooseTopicsLabel;
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;
};
