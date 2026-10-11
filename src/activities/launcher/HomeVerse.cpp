#include "HomeVerse.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SdPaths.h>
#include <esp_app_desc.h>

#include <cstdio>
#include <cstring>

#include "StudyStore/PubKey.h"
#include "activities/boot_sleep/StudyPassageScan.h"
#include "activities/boot_sleep/StudySleepFit.h"
#include "activities/boot_sleep/StudySleepPick.h"
#include "activities/launcher/HomeVerseDoc.h"
#include "activities/launcher/HomeVerseFile.h"
#include "study/PsramJsonAllocator.h"
#include "util/LocalDate.h"

namespace {

constexpr const char* MODULE = "HOMEV";

static_assert(home_verse::REFERENCE_BYTES == study_sleep::REFERENCE_CAPACITY);
static_assert(home_verse::PREFILTER_BYTES < study_sleep::FIT_PREFILTER_BYTES);

// "/.berean/passages/bible.json": the Bible's passages only, since OpenAt resolves
// only in the Bible.
void biblePassagesPath(char* out, const size_t outSize) {
  snprintf(out, outSize, "%s/%s.json", sdpaths::PASSAGES_DIR, study::BIBLE_PUB_KEY);
}

constexpr size_t PATH_BYTES = sizeof(sdpaths::PASSAGES_DIR) + 16;

// The first 8 hex digits of the running ELF's SHA-256 and a terminator: all IDF
// keeps (CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9), and the prefix it prints at boot.
constexpr size_t BUILD_ID_BUFFER_BYTES = 9;

void readBuildId(char (&out)[BUILD_ID_BUFFER_BYTES]) { esp_app_get_elf_sha256(out, sizeof(out)); }

int measureCardText(const void* ctx, const uint8_t rung, const char* text) {
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(HomeVerse::FONT_IDS[rung], text,
                                                            HomeVerse::FONT_STYLES[rung]);
}

home_verse::Key currentKey() {
  home_verse::Key key;
  bool shifted = false;
  key.dated = readLocalDate(key.day, shifted);
  char path[PATH_BYTES];
  biblePassagesPath(path, sizeof(path));
  if (Storage.exists(path)) {
    auto file = Storage.open(path);
    if (file && !file.isDirectory()) {
      key.fileExists = true;
      key.fileBytes = static_cast<uint32_t>(file.size());
    }
  }
  return key;
}

}  // namespace

HomeVerse& HomeVerse::getInstance() {
  static HomeVerse instance;
  return instance;
}

bool HomeVerse::isCurrent() {
  const home_verse::Key key = currentKey();
  restoreOnce(key);
  return home_verse::validFor(entry, key);
}

bool HomeVerse::ensure(const GfxRenderer& renderer, const int textWidth, const int textHeight) {
  const home_verse::Key key = currentKey();
  restoreOnce(key);
  if (home_verse::validFor(entry, key)) return true;
  if (!scan(renderer, textWidth, textHeight, key)) return false;
  save();
  return true;
}

void HomeVerse::restoreOnce(const home_verse::Key& key) {
  if (restoreAttempted) return;
  restoreAttempted = true;

  char buildId[BUILD_ID_BUFFER_BYTES];
  readBuildId(buildId);
  [[maybe_unused]] const unsigned long started = millis();
  const HomeVerseFile::Load outcome = HomeVerseFile::load(buildId, RUNG_COUNT, PsramJsonAllocator::json(), entry);
  if (outcome == HomeVerseFile::Load::NoBuildId) {
    LOG_ERR(MODULE, "No usable build identity (\"%s\"); Home's verse is not kept across sleep", buildId);
    return;
  }
  LOG_DBG(MODULE, "Verse restore: %s, current %d, %lu ms", HomeVerseFile::describe(outcome),
          home_verse::validFor(entry, key) ? 1 : 0, millis() - started);
}

void HomeVerse::save() const {
  char buildId[BUILD_ID_BUFFER_BYTES];
  readBuildId(buildId);
  // restoreOnce() has already reported an unusable identity, once.
  if (!HomeVerseDoc::isUsableBuildId(buildId)) return;

  [[maybe_unused]] const unsigned long started = millis();
  const size_t bytes = HomeVerseFile::save(entry, buildId, PsramJsonAllocator::json());
  if (bytes == 0) {
    LOG_ERR(MODULE, "Verse save failed");
    return;
  }
  LOG_DBG(MODULE, "Verse saved: %u bytes, %lu ms", static_cast<unsigned>(bytes), millis() - started);
}

bool HomeVerse::scan(const GfxRenderer& renderer, const int textWidth, const int textHeight,
                     const home_verse::Key& key) {
  uint32_t seed = home_verse::FALLBACK_SEED;
  if (key.dated) {
    seed = study_sleep::dailySeed(key.day);
  } else {
    LOG_DBG(MODULE, "No date on the clock; fallback seed %08lx", static_cast<unsigned long>(seed));
  }
  auto sampler = makeUniqueNoThrow<study_sleep::Sampler>(&study_sleep::splitmixDraw, &seed);
  if (!sampler) {
    LOG_ERR(MODULE, "OOM: verse sampler");
    entry.valid = false;
    entry.empty = home_verse::Empty::NoPassages;
    return false;
  }

  study_sleep::FitRung rungs[RUNG_COUNT];
  for (uint8_t rung = 0; rung < RUNG_COUNT; ++rung) {
    rungs[rung] = study_sleep::FitRung{rung, renderer.getLineHeight(FONT_IDS[rung]), textHeight};
  }
  study_passage_scan::FitGate gate;
  gate.measure = &measureCardText;
  gate.measureCtx = &renderer;
  gate.floor = rungs[RUNG_COUNT - 1];
  gate.width = textWidth;
  gate.prefilterBytes = home_verse::PREFILTER_BYTES;

  char path[PATH_BYTES];
  biblePassagesPath(path, sizeof(path));
  study_passage_scan::ScanTotals totals;
  const unsigned long started = millis();
  const bool picked = key.fileExists && study_passage_scan::pickFromFile(path, *sampler, gate, totals);
  LOG_INF(MODULE, "Verse scan: %u bytes, %u not whole, %u over prefilter, %u unfit, picked %d, %lu ms",
          static_cast<unsigned>(totals.bytesParsed), static_cast<unsigned>(totals.rowsNotWhole),
          static_cast<unsigned>(totals.rowsOverPrefilter), static_cast<unsigned>(totals.rowsUnfit), picked ? 1 : 0,
          millis() - started);
  if (totals.outOfMemory) {
    // Not cached, so the next Home entry retries.
    LOG_ERR(MODULE, "OOM: verse scan document");
    entry.valid = false;
    entry.empty = home_verse::Empty::NoPassages;
    return false;
  }

  // Filled in place: an Entry is over half a kilobyte, too large for the stack.
  entry.valid = true;
  entry.key = key;
  entry.hasPick = false;
  entry.empty = home_verse::emptyReason(totals.rowsUnfit, totals.rowsOverPrefilter);
  if (!picked) return true;

  const study_sleep::Candidate& candidate = *sampler->result();
  const auto fitted =
      study_sleep::fitPassage(candidate.text, rungs, RUNG_COUNT, textWidth, &measureCardText, &renderer);
  if (!fitted.fits || !home_verse::packLines(fitted.lines, entry.pick)) {
    // The gate measured this text at the floor rung, so this means the two
    // measurements disagreed. Never draw it cut.
    LOG_ERR(MODULE, "Picked verse does not fit the card; showing the hint");
    entry.empty = home_verse::Empty::TooLong;
    return true;
  }
  entry.pick.start = candidate.start;
  entry.pick.spine = candidate.spine;
  entry.pick.rung = fitted.rung;
  memcpy(entry.pick.reference, candidate.reference, sizeof(entry.pick.reference));
  entry.hasPick = true;
  entry.empty = home_verse::Empty::None;
  return true;
}
