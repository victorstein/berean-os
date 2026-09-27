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
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "BereanMark.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "StudySleepPick.h"
#include "StudyStore/ChapterCompletion.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PubKey.h"
#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"
#include "fontIds.h"

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
constexpr int MAX_SNIPPET_LINES = 6;
constexpr int PILL_PAD_X = 12;
constexpr int PILL_PAD_Y = 4;
constexpr int STRIP_MAX_BAR = 48;
constexpr int STRIP_MIN_BAR = 3;
constexpr int STRIP_BAR_GAP = 2;
constexpr int STRIP_CAPTION_GAP = 6;
constexpr int MARK_SIZE = 40;
constexpr int MARK_TEXT_GAP = 4;
constexpr int FOOTER_BOTTOM_GAP = 16;
constexpr const char* OPENING_QUOTE = "\xE2\x80\x9C";

static_assert(study_sleep::SNIPPET_CAPACITY == study::PassageDoc::MAX_SNIPPET_BYTES + 1);
static_assert(study_sleep::REFERENCE_CAPACITY == study::PassageDoc::MAX_REFERENCE_BYTES + 1);

struct ScanBuffers {
  char name[MAX_NAME_BYTES];
  char path[sizeof(sdpaths::PASSAGES_DIR) + MAX_NAME_BYTES + 1];
};

struct ScanTotals {
  uint32_t entries = 0;
  size_t bytesParsed = 0;
  bool capHit = false;
};

struct ScreenContent {
  const study_sleep::Candidate* passage = nullptr;
  const char* dateLine = nullptr;
  std::string tagName;
  const study::ChapterCompletion* progress = nullptr;
};

uint32_t hardwareRandom(void*, const uint32_t bound) { return static_cast<uint32_t>(random(static_cast<long>(bound))); }

std::optional<std::string_view> readPassageFileName(HalFile& entry, char* name) {
  if (entry.isDirectory()) return std::nullopt;
  entry.getName(name, MAX_NAME_BYTES);
  return study_sleep::pubKeyFromFileName(name);
}

// The same gates PassageDoc::fromJson applies, so a row the rest of the device
// drops cannot surface here.
void offerRow(const JsonVariantConst row, const std::string_view pubKey, study_sleep::Sampler& sampler,
              const study_sleep::RingView& ring) {
  const char* startUnit = row["u"] | "";
  if (!study::unitFromCompact(startUnit)) return;
  const char* storedEnd = row["e"] | "";
  const std::string_view endUnit =
      study_sleep::endUnitOrStart(startUnit, storedEnd, study::unitFromCompact(storedEnd).has_value());

  const std::string snippet = utf8SafeSummary(row["x"] | "", study::PassageDoc::MAX_SNIPPET_BYTES);
  if (snippet.empty()) return;
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
  sampler.offer(snippet, reference, tag, key, study_sleep::ageOf(ring, key));
}

// False when the byte budget stops the scan.
bool offerFile(const char* path, const size_t bytes, const std::string_view pubKey, study_sleep::Sampler& sampler,
               const study_sleep::RingView& ring, ScanTotals& totals) {
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
  // path must never write to the study store.
  JsonDocument doc;
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
  for (const JsonVariantConst row : doc["p"].as<JsonArrayConst>()) offerRow(row, pubKey, sampler, ring);
  return true;
}

bool pickPassage(study_sleep::Sampler& sampler, ScanBuffers& buffers, ScanTotals& totals) {
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
      if (!offerFile(buffers.path, entry.size(), *pubKey, sampler, ring, totals)) {
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
  return study_sleep::formatDateLine(clock, SETTINGS.clockUtcOffsetQ, tr(STR_WEEKDAYS), tr(STR_MONTHS_SHORT), out,
                                     outSize);
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

// A missing record is an empty strip: the user has not read yet. Only a record
// that exists but cannot be trusted hides the strip.
bool loadCompletion(study::ChapterCompletion& record) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%s.json", sdpaths::COMPLETION_DIR, study::BIBLE_PUB_KEY);
  JsonDocument doc;
  switch (PersistableStoreBase::readDocFromFileChecked(path, doc)) {
    case DocReadStatus::Missing:
      return true;
    case DocReadStatus::Ok:
      if (record.fromJson(doc.as<JsonVariantConst>())) return true;
      LOG_ERR(MODULE, "Chapter record rejected; no progress strip");
      return false;
    default:
      LOG_ERR(MODULE, "Chapter record unreadable; no progress strip");
      return false;
  }
}

int progressHeight(const GfxRenderer& renderer) {
  return STRIP_MAX_BAR + STRIP_CAPTION_GAP + renderer.getLineHeight(SMALL_FONT_ID);
}

void drawProgress(const GfxRenderer& renderer, const study::ChapterCompletion& record, const int left, const int width,
                  const int top) {
  const int slot = width / study::BIBLE_BOOK_COUNT;
  const int barWidth = std::max(1, slot - STRIP_BAR_GAP);
  const int stripLeft = left + (width - slot * study::BIBLE_BOOK_COUNT) / 2;

  uint8_t longestBook = 1;
  for (uint8_t book = 1; book <= study::BIBLE_BOOK_COUNT; ++book) {
    longestBook = std::max(longestBook, study::canonicalChapterCount(book));
  }

  unsigned finishedBooks = 0;
  for (uint8_t book = 1; book <= study::BIBLE_BOOK_COUNT; ++book) {
    const int chapters = study::canonicalChapterCount(book);
    const int read = record.readCountInBook(book);
    const int barHeight = std::max(STRIP_MIN_BAR, STRIP_MAX_BAR * chapters / longestBook);
    const int x = stripLeft + (book - 1) * slot;
    const int barTop = top + STRIP_MAX_BAR - barHeight;
    if (read >= chapters) {
      renderer.fillRect(x, barTop, barWidth, barHeight, true);
      ++finishedBooks;
      continue;
    }
    renderer.drawRect(x, barTop, barWidth, barHeight, true);
    const int filled = barHeight * read / chapters;
    if (filled > 0) renderer.fillRect(x, barTop + barHeight - filled, barWidth, filled, true);
  }

  char finished[48];
  snprintf(finished, sizeof(finished), tr(STR_BOOKS_FINISHED), finishedBooks);
  char caption[96];
  snprintf(caption, sizeof(caption), "%u / %u   %s", static_cast<unsigned>(record.readCount()),
           static_cast<unsigned>(study::CANONICAL_CHAPTER_TOTAL), finished);
  renderer.drawCenteredText(SMALL_FONT_ID, top + STRIP_MAX_BAR + STRIP_CAPTION_GAP, caption);
}

void drawScreen(const GfxRenderer& renderer, const ScreenContent& content) {
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int left = viewLeft + SIDE_MARGIN;
  const int width = pageWidth - viewLeft - viewRight - 2 * SIDE_MARGIN;

  const auto snippetLines = renderer.wrappedText(NOTOSERIF_18_FONT_ID, content.passage->snippet, width,
                                                 MAX_SNIPPET_LINES, EpdFontFamily::ITALIC);
  const int serifLine = renderer.getLineHeight(NOTOSERIF_18_FONT_ID);
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const bool hasReference = content.passage->reference[0] != '\0';
  const bool hasTag = !content.tagName.empty();
  const int pillHeight = smallLine + 2 * PILL_PAD_Y;

  int blockHeight = serifLine + static_cast<int>(snippetLines.size()) * serifLine;
  if (content.dateLine) blockHeight += uiLine + SECTION_GAP;
  if (hasReference) blockHeight += SECTION_GAP + uiLine;
  if (hasTag) blockHeight += SECTION_GAP + pillHeight;
  if (content.progress) blockHeight += SECTION_GAP * 2 + progressHeight(renderer);

  const int footerTextY = pageHeight - viewBottom - FOOTER_BOTTOM_GAP - smallLine;
  const int markY = footerTextY - MARK_SIZE - MARK_TEXT_GAP;
  const int areaBottom = markY - SECTION_GAP;
  int y = viewTop + std::max(0, (areaBottom - viewTop - blockHeight) / 2);

  // The "Entering sleep" popup is still in the framebuffer.
  renderer.clearScreen();

  if (content.dateLine) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, content.dateLine);
    y += uiLine + SECTION_GAP;
  }
  renderer.drawCenteredText(NOTOSERIF_18_FONT_ID, y, OPENING_QUOTE, true, EpdFontFamily::BOLD);
  y += serifLine;
  for (const auto& snippetLine : snippetLines) {
    renderer.drawCenteredText(NOTOSERIF_18_FONT_ID, y, snippetLine.c_str(), true, EpdFontFamily::ITALIC);
    y += serifLine;
  }
  if (hasReference) {
    y += SECTION_GAP;
    renderer.drawCenteredText(UI_12_FONT_ID, y, content.passage->reference, true, EpdFontFamily::BOLD);
    y += uiLine;
  }
  if (hasTag) {
    y += SECTION_GAP;
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, content.tagName.c_str());
    const int pillWidth = std::min(width, textWidth + 2 * PILL_PAD_X);
    renderer.drawRoundedRect((pageWidth - pillWidth) / 2, y, pillWidth, pillHeight, 1, pillHeight / 2, true);
    renderer.drawCenteredText(SMALL_FONT_ID, y + PILL_PAD_Y, content.tagName.c_str());
    y += pillHeight;
  }
  if (content.progress) {
    y += SECTION_GAP * 2;
    drawProgress(renderer, *content.progress, left, width, y);
  }

  berean_mark::draw(renderer, (pageWidth - MARK_SIZE) / 2, markY, MARK_SIZE);
  renderer.drawCenteredText(SMALL_FONT_ID, footerTextY, tr(STR_BEREAN));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

}  // namespace

bool render(const GfxRenderer& renderer) {
  LOG_DBG(MODULE, "Study pick start: free heap %" PRIu32 ", free PSRAM %" PRIu32, ESP.getFreeHeap(),
          ESP.getFreePsram());
  auto sampler = makeUniqueNoThrow<study_sleep::Sampler>(&hardwareRandom, nullptr);
  auto buffers = makeUniqueNoThrow<ScanBuffers>();
  if (!sampler || !buffers) {
    LOG_ERR(MODULE, "OOM: study sleep pick");
    return false;
  }

  ScanTotals totals;
  const bool picked = pickPassage(*sampler, *buffers, totals);
  LOG_DBG(MODULE, "Study pick: %u files, %u bytes parsed, cap %s, free heap %" PRIu32 ", free PSRAM %" PRIu32,
          static_cast<unsigned>(totals.entries), static_cast<unsigned>(totals.bytesParsed),
          totals.capHit ? "hit" : "not hit", ESP.getFreeHeap(), ESP.getFreePsram());
  if (!picked) return false;
  const study_sleep::Candidate& passage = *sampler->result();

  ScreenContent content;
  content.passage = &passage;
  char dateLine[48];
  if (formatDate(dateLine, sizeof(dateLine))) content.dateLine = dateLine;
  content.tagName = tagName(passage.tag);
  auto completion = makeUniqueNoThrow<study::ChapterCompletion>();
  if (!completion) {
    LOG_ERR(MODULE, "OOM: chapter record; no progress strip");
  } else if (loadCompletion(*completion)) {
    content.progress = completion.get();
  }

  drawScreen(renderer, content);

  APP_STATE.pushRecentStudySleep(passage.key);
  APP_STATE.saveToFileAtomic();
  return true;
}

}  // namespace study_sleep_screen
