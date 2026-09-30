#include "StudySleepScreen.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FormatVersion.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <SdPaths.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "BereanMark.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "StudySleepFit.h"
#include "StudySleepPick.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PubKey.h"
#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"
#include "fontIds.h"
#include "study/PsramJsonAllocator.h"
#include "util/WeekdayNames.h"

namespace study_sleep_screen {
namespace {

constexpr const char* MODULE = "SLP";

constexpr size_t MAX_FILE_BYTES = study::PassageDoc::SAVE_BYTE_BUDGET + 4096;
// Bounds the work done on the way to sleep; a scan that reaches it keeps the
// pick it has, which the random start makes a uniform draw over a random window.
constexpr size_t MAX_TOTAL_BYTES = 262144;
constexpr uint32_t MAX_ENTRIES = 512;
constexpr size_t MAX_NAME_BYTES = 256;

constexpr int SIDE_MARGIN = 24;
constexpr int SECTION_GAP = 18;
constexpr int PILL_PAD_X = 12;
constexpr int PILL_PAD_Y = 4;
constexpr int MARK_SIZE = 40;
constexpr int MARK_TEXT_GAP = 4;
constexpr int FOOTER_BOTTOM_GAP = 16;
constexpr const char* OPENING_QUOTE = "\xE2\x80\x9C";

// The reader's serif sizes, largest first, then Ubuntu 10: the smallest face on
// the device the UI already sets body text in. There is no serif below 12pt, and
// Ubuntu has no italic.
constexpr int PASSAGE_FONT_IDS[] = {NOTOSERIF_18_FONT_ID, NOTOSERIF_16_FONT_ID, NOTOSERIF_14_FONT_ID,
                                    NOTOSERIF_12_FONT_ID, UI_10_FONT_ID};
constexpr EpdFontFamily::Style PASSAGE_FONT_STYLES[] = {
    EpdFontFamily::ITALIC, EpdFontFamily::ITALIC, EpdFontFamily::ITALIC, EpdFontFamily::ITALIC, EpdFontFamily::REGULAR};
constexpr uint8_t SERIF_12 = 3;
constexpr uint8_t FLOOR_SIZE = 4;

// What a rung keeps around the passage. The quote mark, the reference and the
// Berean mark always stay: without its reference a quote has no address.
struct Chrome {
  bool date;
  bool tag;
};

struct Rung {
  uint8_t sizeIndex;
  Chrome chrome;
};

// Largest first; the first rung that holds the whole passage wins (issue #188).
constexpr Rung RUNGS[] = {
    {0, {true, true}},
    {1, {true, true}},
    {2, {true, true}},
    {SERIF_12, {true, true}},
    {SERIF_12, {false, false}},
    {FLOOR_SIZE, {false, false}},
};
constexpr uint8_t RUNG_COUNT = sizeof(RUNGS) / sizeof(RUNGS[0]);
constexpr uint8_t FLOOR_RUNG = RUNG_COUNT - 1;

static_assert(study_sleep::REFERENCE_CAPACITY == study::PassageDoc::MAX_REFERENCE_BYTES + 1);

struct ScanBuffers {
  char name[MAX_NAME_BYTES];
  char path[sizeof(sdpaths::PASSAGES_DIR) + MAX_NAME_BYTES + 1];
};

struct ScanTotals {
  uint32_t entries = 0;
  size_t bytesParsed = 0;
  bool capHit = false;
  uint32_t rowsNotWhole = 0;
  uint32_t rowsOverPrefilter = 0;
  uint32_t rowsUnfit = 0;
};

struct ScreenContent {
  const study_sleep::Candidate* passage = nullptr;
  const char* dateLine = nullptr;
  std::string tagName;
};

struct Layout {
  int width = 0;
  int pageWidth = 0;
  int viewTop = 0;
  int areaBottom = 0;
  int footerTextY = 0;
  int markY = 0;
  int serifLine = 0;
  int uiLine = 0;
  int pillHeight = 0;
};

// Decides, before sampling, whether a passage can be shown whole at all: at the
// floor rung, every optional piece of chrome dropped and a reference line
// assumed. A pick that then failed to fit would leave nothing to show.
struct FitGate {
  const GfxRenderer* renderer = nullptr;
  study_sleep::FitRung floor{};
  int width = 0;
};

uint32_t hardwareRandom(void*, const uint32_t bound) { return static_cast<uint32_t>(random(static_cast<long>(bound))); }

int measurePassage(const void* ctx, const uint8_t sizeIndex, const char* text) {
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(PASSAGE_FONT_IDS[sizeIndex], text,
                                                            PASSAGE_FONT_STYLES[sizeIndex]);
}

Layout measureLayout(const GfxRenderer& renderer) {
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  Layout layout;
  layout.pageWidth = renderer.getScreenWidth();
  layout.width = layout.pageWidth - viewLeft - viewRight - 2 * SIDE_MARGIN;
  layout.viewTop = viewTop;
  layout.serifLine = renderer.getLineHeight(NOTOSERIF_18_FONT_ID);
  layout.uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  layout.pillHeight = smallLine + 2 * PILL_PAD_Y;
  layout.footerTextY = renderer.getScreenHeight() - viewBottom - FOOTER_BOTTOM_GAP - smallLine;
  layout.markY = layout.footerTextY - MARK_SIZE - MARK_TEXT_GAP;
  layout.areaBottom = layout.markY - SECTION_GAP;
  return layout;
}

// Everything but the passage lines, the quote mark included.
int chromeHeight(const Layout& layout, const Chrome& chrome, const bool hasDate, const bool hasReference,
                 const bool hasTag) {
  int height = layout.serifLine;
  if (chrome.date && hasDate) height += layout.uiLine + SECTION_GAP;
  if (hasReference) height += SECTION_GAP + layout.uiLine;
  if (chrome.tag && hasTag) height += SECTION_GAP + layout.pillHeight;
  return height;
}

study_sleep::FitRung fitRung(const GfxRenderer& renderer, const Layout& layout, const uint8_t rung, const bool hasDate,
                             const bool hasReference, const bool hasTag) {
  const uint8_t sizeIndex = RUNGS[rung].sizeIndex;
  return {sizeIndex, renderer.getLineHeight(PASSAGE_FONT_IDS[sizeIndex]),
          layout.areaBottom - layout.viewTop - chromeHeight(layout, RUNGS[rung].chrome, hasDate, hasReference, hasTag)};
}

bool fitsFloorRung(const FitGate& gate, const std::string_view text) {
  return study_sleep::fitPassage(text, &gate.floor, 1, gate.width, &measurePassage, gate.renderer).fits;
}

std::optional<std::string_view> readPassageFileName(HalFile& entry, char* name) {
  if (entry.isDirectory()) return std::nullopt;
  entry.getName(name, MAX_NAME_BYTES);
  return study_sleep::pubKeyFromFileName(name);
}

// Drops a row PassageDoc::fromJson would drop -- an unaddressable start -- and any
// row whose text is not its whole verse(s): a legacy row waits for the repair
// rather than showing cut (issue #188). "k" is never read.
void offerRow(const JsonVariantConst row, const std::string_view pubKey, study_sleep::Sampler& sampler,
              const study_sleep::RingView& ring, const FitGate& gate, ScanTotals& totals) {
  const char* startUnit = row["u"] | "";
  if (!study::unitFromCompact(startUnit)) return;

  const std::string_view wholeText = row["w"] | "";
  if (!study_sleep::rowIsWhole(row["h"] | false, wholeText)) {
    ++totals.rowsNotWhole;
    return;
  }
  const bool underPrefilter = study_sleep::withinPrefilter(wholeText);
  const bool fits = underPrefilter && fitsFloorRung(gate, wholeText);
  if (!underPrefilter) {
    ++totals.rowsOverPrefilter;
  } else if (!fits) {
    ++totals.rowsUnfit;
  }

  const char* storedEnd = row["e"] | "";
  const std::string_view endUnit =
      study_sleep::endUnitOrStart(startUnit, storedEnd, study::unitFromCompact(storedEnd).has_value());
  const std::string reference = utf8SafeSummary(row["r"] | "", study::PassageDoc::MAX_REFERENCE_BYTES);

  uint16_t tag = 0;
  for (const JsonVariantConst id : row["t"].as<JsonArrayConst>()) {
    const uint32_t raw = id | 0u;
    if (raw >= 1 && raw <= UINT16_MAX) {
      tag = static_cast<uint16_t>(raw);
      break;
    }
  }

  const uint32_t key = study_sleep::passageKey(pubKey, startUnit, endUnit);
  sampler.offer(wholeText, reference, tag, key, study_sleep::ageOf(ring, key), fits);
}

// False when the byte budget stops the scan.
bool offerFile(const char* path, const size_t bytes, const std::string_view pubKey, study_sleep::Sampler& sampler,
               const study_sleep::RingView& ring, const FitGate& gate, ScanTotals& totals) {
  if (bytes > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "Skipping %s: %u bytes exceeds the %u-byte cap", path, static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_FILE_BYTES));
    return true;
  }
  if (totals.bytesParsed + bytes > MAX_TOTAL_BYTES) {
    totals.capHit = true;
    return false;
  }
  totals.bytesParsed += bytes;

  // Not PassageFile::load: that promotes a leftover .tmp, a rename, and this
  // path must never write to the study store. PSRAM, as the store keeps it: the
  // whole texts would otherwise be copied into internal SRAM on the way to sleep.
  JsonDocument doc(PsramJsonAllocator::json());
  const DocReadStatus status = PersistableStoreBase::readDocFromFileStreamed(path, doc);
  if (status != DocReadStatus::Ok) {
    LOG_ERR(MODULE, "Skipping %s: passages unreadable (status %u)", path, static_cast<unsigned>(status));
    return true;
  }
  const int version = doc["v"] | 0;
  if (!persist::isKnownFormatVersion(version, study::PassageDoc::FORMAT_VERSION) || !doc["p"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "Skipping %s: unknown passage format v%d", path, version);
    return true;
  }
  for (const JsonVariantConst row : doc["p"].as<JsonArrayConst>()) offerRow(row, pubKey, sampler, ring, gate, totals);
  return true;
}

bool pickPassage(study_sleep::Sampler& sampler, ScanBuffers& buffers, const FitGate& gate, ScanTotals& totals) {
  auto dir = Storage.open(sdpaths::PASSAGES_DIR);
  if (!dir || !dir.isDirectory()) return false;

  uint32_t count = 0;
  for (auto entry = dir.openNextFile(); entry && count < MAX_ENTRIES; entry = dir.openNextFile()) {
    if (readPassageFileName(entry, buffers.name)) ++count;
  }
  totals.entries = count;
  if (count == 0) return false;
  if (count == MAX_ENTRIES)
    LOG_INF(MODULE, "Passage scan stopped counting at %u files", static_cast<unsigned>(MAX_ENTRIES));

  const study_sleep::RingView ring{APP_STATE.recentStudySleep, CrossPointState::SLEEP_RECENT_COUNT,
                                   APP_STATE.recentStudySleepPos, APP_STATE.recentStudySleepFill};
  const uint32_t start = hardwareRandom(nullptr, count);

  for (uint8_t sweep = 0; sweep < 2; ++sweep) {
    dir.rewindDirectory();
    uint32_t index = 0;
    for (auto entry = dir.openNextFile(); entry && index < count; entry = dir.openNextFile()) {
      const auto pubKey = readPassageFileName(entry, buffers.name);
      if (!pubKey) continue;
      const uint32_t current = index++;
      if (!study_sleep::inSweep(sweep, current, start)) continue;

      snprintf(buffers.path, sizeof(buffers.path), "%s/%s", sdpaths::PASSAGES_DIR, buffers.name);
      if (!offerFile(buffers.path, entry.size(), *pubKey, sampler, ring, gate, totals)) {
        LOG_INF(MODULE, "Passage scan stopped at the %u-byte budget", static_cast<unsigned>(MAX_TOTAL_BYTES));
        return sampler.result() != nullptr;
      }
    }
  }
  return sampler.result() != nullptr;
}

bool formatDate(char* out, const size_t outSize) {
  study_sleep::ClockReading clock;
  HalClock::Date date{};
  clock.dateValid = halClock.getDate(date);
  clock.year = date.year;
  clock.month = date.month;
  clock.day = date.day;
  clock.timeValid = clock.dateValid && halClock.getTime(clock.hour, clock.minute);
  const char* const weekdayNames[7] = {I18N.get(WEEKDAY_NAME_IDS[0]), I18N.get(WEEKDAY_NAME_IDS[1]),
                                       I18N.get(WEEKDAY_NAME_IDS[2]), I18N.get(WEEKDAY_NAME_IDS[3]),
                                       I18N.get(WEEKDAY_NAME_IDS[4]), I18N.get(WEEKDAY_NAME_IDS[5]),
                                       I18N.get(WEEKDAY_NAME_IDS[6])};
  return study_sleep::formatDateLine(clock, SETTINGS.clockUtcOffsetQ, weekdayNames, tr(STR_MONTHS_SHORT), out, outSize);
}

std::string tagName(const uint16_t rawTag) {
  if (rawTag == 0) return {};
  JsonDocument doc;
  const DocReadStatus status = PersistableStoreBase::readDocFromFileChecked(sdpaths::TAGS_FILE, doc);
  if (status != DocReadStatus::Ok) {
    if (status != DocReadStatus::Missing) LOG_ERR(MODULE, "Tag palette unreadable; no tag pill");
    return {};
  }
  study::TagPalette palette;
  if (!palette.fromJson(doc.as<JsonVariantConst>())) {
    LOG_ERR(MODULE, "Tag palette rejected; no tag pill");
    return {};
  }
  return palette.name(study::toTagId(rawTag));
}

void drawScreen(const GfxRenderer& renderer, const Layout& layout, const ScreenContent& content,
                const study_sleep::FitResult& passage) {
  const Chrome& chrome = RUNGS[passage.rung].chrome;
  const bool hasReference = content.passage->reference[0] != '\0';
  const bool showDate = chrome.date && content.dateLine != nullptr;
  const bool showTag = chrome.tag && !content.tagName.empty();
  const uint8_t sizeIndex = RUNGS[passage.rung].sizeIndex;
  const int passageFont = PASSAGE_FONT_IDS[sizeIndex];
  const int passageLine = renderer.getLineHeight(passageFont);

  const int blockHeight =
      chromeHeight(layout, chrome, content.dateLine != nullptr, hasReference, !content.tagName.empty()) +
      static_cast<int>(passage.lines.size()) * passageLine;
  int y = layout.viewTop + std::max(0, (layout.areaBottom - layout.viewTop - blockHeight) / 2);

  // The "Entering sleep" popup is still in the framebuffer.
  renderer.clearScreen();

  if (showDate) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, content.dateLine);
    y += layout.uiLine + SECTION_GAP;
  }
  renderer.drawCenteredText(NOTOSERIF_18_FONT_ID, y, OPENING_QUOTE, true, EpdFontFamily::BOLD);
  y += layout.serifLine;
  for (const auto& line : passage.lines) {
    renderer.drawCenteredText(passageFont, y, line.c_str(), true, PASSAGE_FONT_STYLES[sizeIndex]);
    y += passageLine;
  }
  if (hasReference) {
    y += SECTION_GAP;
    renderer.drawCenteredText(UI_12_FONT_ID, y, content.passage->reference, true, EpdFontFamily::BOLD);
    y += layout.uiLine;
  }
  if (showTag) {
    y += SECTION_GAP;
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, content.tagName.c_str());
    const int pillWidth = std::min(layout.width, textWidth + 2 * PILL_PAD_X);
    renderer.drawRoundedRect((layout.pageWidth - pillWidth) / 2, y, pillWidth, layout.pillHeight, 1,
                             layout.pillHeight / 2, true);
    renderer.drawCenteredText(SMALL_FONT_ID, y + PILL_PAD_Y, content.tagName.c_str());
    y += layout.pillHeight;
  }

  berean_mark::draw(renderer, (layout.pageWidth - MARK_SIZE) / 2, layout.markY, MARK_SIZE);
  renderer.drawCenteredText(SMALL_FONT_ID, layout.footerTextY, tr(STR_BEREAN));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

}  // namespace

bool render(const GfxRenderer& renderer) {
  PsramJsonAllocator::logMemory("Study pick start");
  auto sampler = makeUniqueNoThrow<study_sleep::Sampler>(&hardwareRandom, nullptr);
  auto buffers = makeUniqueNoThrow<ScanBuffers>();
  if (!sampler || !buffers) {
    LOG_ERR(MODULE, "OOM: study sleep pick");
    return false;
  }

  const Layout layout = measureLayout(renderer);
  FitGate gate;
  gate.renderer = &renderer;
  gate.width = layout.width;
  gate.floor = fitRung(renderer, layout, FLOOR_RUNG, false, true, false);
  // Logged so a tester can compare what the floor rung really holds against FIT_PREFILTER_BYTES.
  constexpr const char* SAMPLE_TEN = "abcdefghij";
  const int sampleWidth = measurePassage(&renderer, gate.floor.sizeIndex, SAMPLE_TEN);
  LOG_DBG(MODULE, "Floor rung: %d lines, ~%d chars a line, prefilter %u bytes",
          gate.floor.lineHeight > 0 ? gate.floor.maxHeight / gate.floor.lineHeight : 0,
          sampleWidth > 0 ? gate.width * 10 / sampleWidth : 0, static_cast<unsigned>(study_sleep::FIT_PREFILTER_BYTES));

  ScanTotals totals;
  const unsigned long scanStarted = millis();
  const bool picked = pickPassage(*sampler, *buffers, gate, totals);
  LOG_DBG(
      MODULE, "Study pick: %u files, %u bytes parsed, cap %s, %u rows not whole, %u over prefilter, %u unfit, %lu ms",
      static_cast<unsigned>(totals.entries), static_cast<unsigned>(totals.bytesParsed),
      totals.capHit ? "hit" : "not hit", static_cast<unsigned>(totals.rowsNotWhole),
      static_cast<unsigned>(totals.rowsOverPrefilter), static_cast<unsigned>(totals.rowsUnfit), millis() - scanStarted);
  PsramJsonAllocator::logMemory("Study pick");
  if (!picked) return false;
  const study_sleep::Candidate& passage = *sampler->result();

  ScreenContent content;
  content.passage = &passage;
  char dateLine[48];
  if (formatDate(dateLine, sizeof(dateLine))) content.dateLine = dateLine;
  content.tagName = tagName(passage.tag);

  study_sleep::FitRung rungs[RUNG_COUNT];
  for (uint8_t rung = 0; rung < RUNG_COUNT; ++rung) {
    rungs[rung] = fitRung(renderer, layout, rung, content.dateLine != nullptr, passage.reference[0] != '\0',
                          !content.tagName.empty());
  }
  const auto fitted =
      study_sleep::fitPassage(passage.text, rungs, RUNG_COUNT, layout.width, &measurePassage, &renderer);
  if (!fitted.fits) {
    // The gate measured this text at the floor rung with a reference assumed, so
    // this means the two measurements disagreed. Never draw it cut.
    LOG_ERR(MODULE, "Picked passage does not fit; study screen skipped");
    return false;
  }

  drawScreen(renderer, layout, content, fitted);

  APP_STATE.pushRecentStudySleep(passage.key);
  APP_STATE.saveToFileAtomic();
  return true;
}

}  // namespace study_sleep_screen
