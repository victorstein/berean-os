#pragma once

#include <Epub.h>
#include <Preaching/IntroSection.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/reader/ParkedBook.h"
#include "study/PreachingPassages.h"
#include "study/PreachingTopicsFile.h"

class GfxRenderer;

// What Predicación's screens share while it is open: the Bible, taken from ParkedBook and parked
// again on close; where the twenty questions sit in its spine and how their rows read; the picked
// topics; and the Mis temas counts once made. Owned by PreachingActivity. PreachingTopicActivity
// holds a reference: it is only ever pushed over its own PreachingActivity, and the activity
// manager destroys it first. Loop task only.
class PreachingSession {
 public:
  PreachingSession() = default;
  PreachingSession(const PreachingSession&) = delete;
  PreachingSession& operator=(const PreachingSession&) = delete;

  // Finds the Bible, takes or opens it, and resolves the section. Never fails hard: whatever is
  // missing shows as no Bible or no section.
  void open(GfxRenderer& renderer);
  // Parks the Bible for the next screen that opens it.
  void close();

  bool hasBible() const { return bible.book != nullptr; }
  const std::string& biblePath() const { return path; }
  const std::shared_ptr<Epub>& book() const { return bible.book; }
  bool sectionFound() const { return section; }
  int spine(int question) const;
  // "8. ¿Tiene Dios la culpa del sufrimiento?", or "Question 8" without a TOC entry of its own.
  const std::string& questionLabel(int question) const;

  void ensureTopics();
  // For after Elegir temas saves: reads the picks again and drops the held counts.
  void reloadTopics();
  // The picked tags still active, in TagPalette::activeIdsByName order.
  const std::vector<study::TagId>& topics() const { return activeTopics; }
  const std::string& topicName(study::TagId id) const { return topicsFile.palette().name(id); }
  // The row of `id` in topics(), -1 when it is not an active pick.
  int topicRow(study::TagId id) const;

  void ensureCounts();
  bool hasCounts() const { return countsLoaded && PreachingPassages::isAvailable(countsStatus); }
  // Passages carrying topics()[row]; only meaningful while hasCounts().
  uint16_t count(size_t row) const { return row < counts.size() ? counts[row].passages : 0; }

 private:
  std::string path;
  ParkedBook::Held bible;
  int spines[preaching::IntroSection::SLOT_COUNT] = {};
  bool section = false;
  std::string labels[preaching::IntroSection::SLOT_COUNT];

  PreachingTopicsFile topicsFile;
  bool topicsLoaded = false;
  std::vector<study::TagId> activeTopics;

  bool countsLoaded = false;
  PreachingPassages::Status countsStatus = PreachingPassages::Status::Missing;
  std::vector<PreachingPassages::TagCount> counts;

  bool takeOrOpenBible(GfxRenderer& renderer);
  void resolveSection();
  std::string labelFor(int question) const;
};
