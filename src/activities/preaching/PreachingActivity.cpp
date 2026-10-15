#include "PreachingActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Preaching/IntroSection.h>

#include <string>
#include <utility>
#include <variant>

#include "MappedInputManager.h"
#include "PreachingContent.h"
#include "PreachingTopicActivity.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "PREACH";

using preaching::IntroSection::QUESTION_COUNT;

// A topic opened from the list starts on its first page, with nothing selected.
constexpr int NO_VERSE = -1;

}  // namespace

PreachingActivity::PreachingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const PreachingOrigin& origin)
    : UiTabListActivity("Preaching", renderer, mappedInput), origin(origin) {}

void PreachingActivity::onEnter() {
  session.open(renderer);
  chooseTopicsLabel = std::string("+ ") + tr(STR_PREACHING_CHOOSE_TOPICS);
  if (origin.kind == PreachingOrigin::Kind::Tag) {
    // Only the picks: the counts wait until Mis temas is shown, so a return from the Bible reads
    // bible.json once, for the topic.
    session.ensureTopics();
    tab = Tab::Topics;
  }
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  UiTabListActivity::onEnter();
  seedSelection();

  if (origin.kind == PreachingOrigin::Kind::Question) {
    openQuestion(origin.question, origin.verse);
  } else if (origin.kind == PreachingOrigin::Kind::Tag && !openTag(study::toTagId(origin.tag), origin.verse)) {
    // No topic result will come to run the deferred counts.
    session.ensureCounts();
    RenderLock lock(*this);
    rebuildRows();
  }
}

void PreachingActivity::onExit() {
  session.close();
  UiTabListActivity::onExit();
}

const char* PreachingActivity::headerTitle() const { return tr(STR_PREACHING); }

const char* PreachingActivity::tabLabel(const int index) const {
  return static_cast<Tab>(index) == Tab::Questions ? tr(STR_PREACHING_QUESTIONS) : tr(STR_PREACHING_MY_TOPICS);
}

int PreachingActivity::listCount() const { return static_cast<int>(rowItems.size()); }

void PreachingActivity::rebuildRows() {
  rowItems.clear();
  rowValues.clear();
  if (tab == Tab::Questions) {
    if (!session.sectionFound()) return;
    rowItems.reserve(QUESTION_COUNT);
    for (int question = 1; question <= QUESTION_COUNT; ++question) {
      fui::ListItem item;
      item.label = session.questionLabel(question).c_str();
      item.actionValue = static_cast<int16_t>(question - 1);
      rowItems.push_back(item);
    }
    return;
  }

  const auto& topics = session.topics();
  rowValues.reserve(topics.size());
  rowItems.reserve(topics.size() + 1);
  for (size_t row = 0; row < topics.size(); ++row) {
    rowValues.push_back(session.hasCounts() ? std::to_string(session.count(row)) : std::string());
  }
  for (size_t row = 0; row < topics.size(); ++row) {
    fui::ListItem item;
    item.label = session.topicName(topics[row]).c_str();
    item.value = rowValues[row].empty() ? nullptr : rowValues[row].c_str();
    item.actionValue = static_cast<int16_t>(row);
    rowItems.push_back(item);
  }
  fui::ListItem choose;
  choose.label = chooseTopicsLabel.c_str();
  choose.actionValue = static_cast<int16_t>(topics.size());
  rowItems.push_back(choose);
}

void PreachingActivity::seedSelection() {
  int row = 0;
  if (origin.kind == PreachingOrigin::Kind::Question && origin.question >= 1 && origin.question <= QUESTION_COUNT) {
    row = origin.question - 1;
  } else if (origin.kind == PreachingOrigin::Kind::Tag) {
    const int tagRow = session.topicRow(study::toTagId(origin.tag));
    row = tagRow >= 0 ? tagRow : 0;
  }
  auto& nav = activeNav();
  nav.selected = listCount() > row ? row + 1 : 0;
  nav.followOnBuild = true;
}

void PreachingActivity::showTab(const Tab next) {
  if (next == Tab::Topics) session.ensureCounts();
  RenderLock lock(*this);
  tab = next;
  rebuildRows();
}

void PreachingActivity::onTabAction(const int index) {
  showTab(static_cast<Tab>(index));
  auto& nav = activeNav();
  nav.selected = 0;
  nav.followOnBuild = true;
  app.clearTapFlash();
  requestUpdate();
}

void PreachingActivity::stepTab(int) {
  const bool onTabBar = ringPos() == 0;
  showTab(tab == Tab::Questions ? Tab::Topics : Tab::Questions);
  auto& nav = activeNav();
  nav.selected = onTabBar || listCount() == 0 ? 0 : 1;
  nav.followOnBuild = true;
  requestUpdate();
}

bool PreachingActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }
  return false;
}

void PreachingActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  buildTabBar(screen);

  if (tab == Tab::Questions && !session.sectionFound()) {
    screen.centeredText(tr(STR_PREACHING_NO_SECTION), screen.theme().bodyText);
    return;
  }
  if (tab == Tab::Topics && session.topics().empty()) {
    fui::TextStyle style = screen.theme().bodyText;
    style.align = fui::TextAlign::Center;
    style.maxLines = 2;
    const int lineHeight = screen.target().lineHeight(style.font);
    const fui::Rect notice =
        screen.takeTop(static_cast<int16_t>(2 * lineHeight), static_cast<int16_t>(metrics.verticalSpacing));
    screen.target().text(notice, tr(STR_PREACHING_NO_TOPICS), style);
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void PreachingActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (tab == Tab::Questions) {
    openQuestion(index + 1, NO_VERSE);
    return;
  }
  const auto& topics = session.topics();
  if (index == static_cast<int>(topics.size())) {
    openTopicPicker();
  } else if (index >= 0 && index < static_cast<int>(topics.size())) {
    openTag(topics[static_cast<size_t>(index)], NO_VERSE);
  }
}

void PreachingActivity::openTopicPicker() { LOG_INF(MODULE, "Elegir temas is not built yet"); }

void PreachingActivity::onTopicsChanged() {
  {
    // The rows point into the palette's strings, and buildScreen reads the topics, both of which the
    // reload replaces, so the render task is held off for the whole reload.
    RenderLock lock(*this);
    rowItems.clear();
    rowValues.clear();
    session.reloadTopics();
    if (tab == Tab::Topics) session.ensureCounts();
    rebuildRows();
  }
  requestUpdate();
}

void PreachingActivity::reportOpenFailure(const char* message) {
  PostedMessage::post(message);
  requestUpdate();
}

bool PreachingActivity::openQuestion(const int question, const int verse) {
  auto cards = makeUniqueNoThrow<TopicCards>();
  if (!cards) {
    LOG_ERR(MODULE, "OOM: question %d", question);
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return false;
  }
  const auto result = PreachingContent::loadQuestion(session, renderer, question, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportOpenFailure(PreachingContent::failureMessage(result));
    return false;
  }
  return pushTopic(std::move(cards), verse);
}

bool PreachingActivity::openTag(const study::TagId tag, const int verse) {
  auto cards = makeUniqueNoThrow<TopicCards>();
  if (!cards) {
    LOG_ERR(MODULE, "OOM: tag %u", static_cast<unsigned>(study::toRaw(tag)));
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return false;
  }
  const auto result = PreachingContent::loadTag(session, tag, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportOpenFailure(PreachingContent::failureMessage(result));
    return false;
  }
  return pushTopic(std::move(cards), verse);
}

bool PreachingActivity::pushTopic(std::unique_ptr<TopicCards> cards, const int verse) {
  auto topic = makeUniqueNoThrow<PreachingTopicActivity>(renderer, mappedInput, session, std::move(cards), verse);
  if (!topic) {
    LOG_ERR(MODULE, "OOM: topic activity");
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return false;
  }
  startActivityForResult(std::move(topic), [this](const ActivityResult& result) { onTopicResult(result); });
  return true;
}

void PreachingActivity::onTopicResult(const ActivityResult& result) {
  const auto* returned = std::get_if<PreachingResult>(&result.data);
  if (!returned) return;
  const PreachingOrigin& at = returned->at;
  if (at.kind == PreachingOrigin::Kind::Question && session.sectionFound() && at.question >= 1 &&
      at.question <= QUESTION_COUNT) {
    showTab(Tab::Questions);
    moveRingTo(at.question);
  } else if (at.kind == PreachingOrigin::Kind::Tag) {
    showTab(Tab::Topics);
    const int row = session.topicRow(study::toTagId(at.tag));
    moveRingTo(row >= 0 ? row + 1 : (listCount() > 0 ? 1 : 0));
  }
}
