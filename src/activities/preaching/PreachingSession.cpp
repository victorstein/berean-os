#include "PreachingSession.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SdPaths.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/launcher/BibleFinder.h"
#include "components/UITheme.h"
#include "study/PsramJsonAllocator.h"

namespace {

constexpr const char* MODULE = "PREACH";

using preaching::IntroSection::QUESTION_COUNT;
using preaching::IntroSection::SLOT_COUNT;

bool isQuestion(const int question) { return question >= 1 && question <= QUESTION_COUNT; }

}  // namespace

void PreachingSession::open(GfxRenderer& renderer) {
  const unsigned long started = millis();
  RECENT_BOOKS.loadFromFile();
  const auto found = BibleFinder::find();
  if (!found) {
    LOG_INF(MODULE, "No Bible on the card");
    return;
  }
  path = found->path;
  if (!takeOrOpenBible(renderer)) return;
  resolveSection();
  LOG_INF(MODULE, "Opened %s in %lu ms, section %s", path.c_str(), millis() - started, section ? "found" : "missing");
}

bool PreachingSession::takeOrOpenBible(GfxRenderer& renderer) {
  const bool skipLoadingCss = SETTINGS.embeddedStyle == 0;
  bible = ParkedBook::take(path, skipLoadingCss);
  if (!bible.book) {
    auto fresh = makeUniqueNoThrow<Epub>(path, sdpaths::CROSSPOINT_DIR);
    if (!fresh) {
      LOG_ERR(MODULE, "OOM: Bible EPUB");
      return false;
    }
    const bool uncached = !Storage.exists((fresh->getCachePath() + "/book.bin").c_str());
    if (uncached) GUI.drawPopup(renderer, tr(STR_INDEXING));
    bool loaded;
    {
      std::optional<GfxRenderer::FrameBufferLoan> loan;
      if (uncached) loan.emplace(renderer);
      loaded = fresh->load(true, skipLoadingCss);
    }
    if (!loaded) {
      LOG_ERR(MODULE, "Failed to load the Bible: %s", path.c_str());
      return false;
    }
    bible = {std::shared_ptr<Epub>(std::move(fresh)), nullptr};
  }
  bible.book->setupCacheDir();
  return true;
}

void PreachingSession::resolveSection() {
  auto names = makeUniqueNoThrow<std::string[]>(SLOT_COUNT);
  if (!names) {
    LOG_ERR(MODULE, "OOM: %d section filenames", SLOT_COUNT);
    return;
  }
  for (int slot = 0; slot < SLOT_COUNT; ++slot) names[slot] = preaching::IntroSection::BASENAMES[slot];
  bible.book->resolveFilenamesToSpineIndices(names.get(), spines, SLOT_COUNT);
  section = std::all_of(spines, spines + SLOT_COUNT, [](const int spineIndex) { return spineIndex >= 0; });
  if (!section) return;
  for (int question = 1; question <= QUESTION_COUNT; ++question) labels[question] = labelFor(question);
}

std::string PreachingSession::labelFor(const int question) const {
  const int spineIndex = spines[question];
  const int toc = bible.book->getTocIndexForSpineIndex(spineIndex);
  if (toc >= 0) {
    // A spine with no TOC entry of its own inherits the previous spine's, so ownership is checked.
    const auto entry = bible.book->getTocItem(toc);
    const std::string_view title = preaching::IntroSection::stripTocPrefix(entry.title);
    if (entry.spineIndex == spineIndex && !title.empty()) {
      std::string label = std::to_string(question) + ". ";
      label.append(title);
      return label;
    }
  }
  char fallback[48];
  snprintf(fallback, sizeof(fallback), tr(STR_PREACHING_QUESTION_N), question);
  return fallback;
}

void PreachingSession::close() {
  if (bible.book) ParkedBook::park(path, std::move(bible));
  bible = {};
}

int PreachingSession::spine(const int question) const { return isQuestion(question) ? spines[question] : -1; }

const std::string& PreachingSession::questionLabel(const int question) const {
  return labels[isQuestion(question) ? question : 0];
}

void PreachingSession::ensureTopics() {
  if (topicsLoaded) return;
  topicsLoaded = true;
  topicsFile.load();

  const std::vector<study::TagId> byName = topicsFile.palette().activeIdsByName();
  const std::vector<study::TagId>& picked = topicsFile.tags();
  activeTopics.clear();
  activeTopics.reserve(picked.size());
  for (const study::TagId id : byName) {
    if (std::find(picked.begin(), picked.end(), id) != picked.end()) activeTopics.push_back(id);
  }
}

void PreachingSession::reloadTopics() {
  topicsLoaded = false;
  countsLoaded = false;
  counts.clear();
  ensureTopics();
}

int PreachingSession::topicRow(const study::TagId id) const {
  const auto found = std::find(activeTopics.begin(), activeTopics.end(), id);
  return found == activeTopics.end() ? -1 : static_cast<int>(found - activeTopics.begin());
}

void PreachingSession::ensureCounts() {
  if (countsLoaded) return;
  ensureTopics();
  countsLoaded = true;
  if (activeTopics.empty()) {
    countsStatus = PreachingPassages::Status::Ok;
    counts.clear();
    return;
  }
  countsStatus = PreachingPassages::countByTag(activeTopics, PsramJsonAllocator::json(), counts);
}
