#include "StudyPassageScan.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FormatVersion.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <SdPaths.h>
#include <Utf8.h>

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/Unit.h"
#include "study/PsramJsonAllocator.h"
#include "util/TaskWatchdog.h"

namespace study_passage_scan {
namespace {

constexpr const char* MODULE = "PSCAN";

constexpr size_t MAX_FILE_BYTES = study::PassageDoc::SAVE_BYTE_BUDGET + 4096;
// Bounds the work done on the way to sleep; a scan that reaches it keeps the
// pick it has, which the random start makes a uniform draw over a random window.
constexpr size_t MAX_TOTAL_BYTES = 262144;
constexpr uint32_t MAX_ENTRIES = 512;
constexpr size_t MAX_NAME_BYTES = 256;
// Fitting measures every word, so a long file keeps the loop task's watchdog fed.
constexpr uint32_t ROWS_PER_WATCHDOG_RESET = 32;

struct ScanBuffers {
  char name[MAX_NAME_BYTES];
  char path[sizeof(sdpaths::PASSAGES_DIR) + MAX_NAME_BYTES + 1];
};

uint32_t hardwareRandom(void*, const uint32_t bound) { return static_cast<uint32_t>(random(static_cast<long>(bound))); }

bool fitsFloorRung(const FitGate& gate, const std::string_view text) {
  return study_sleep::fitPassage(text, &gate.floor, 1, gate.width, gate.measure, gate.measureCtx).fits;
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
              const study_sleep::RingView* ring, const FitGate& gate, ScanTotals& totals) {
  const char* startUnit = row["u"] | "";
  const std::optional<study::Unit> start = study::unitFromCompact(startUnit);
  if (!start) return;

  const std::string_view wholeText = row["w"] | "";
  if (!study_sleep::rowIsWhole(row["h"] | false, wholeText)) {
    ++totals.rowsNotWhole;
    return;
  }
  const bool underPrefilter = study_sleep::withinPrefilter(wholeText, gate.prefilterBytes);
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

  // PassageDoc::toJson writes the passage's documentSpine as "s"; fromJson reads
  // an absent one as 0.
  const uint32_t rawSpine = row["s"] | 0u;
  const uint16_t spine = rawSpine <= UINT16_MAX ? static_cast<uint16_t>(rawSpine) : 0;

  const uint32_t key = study_sleep::passageKey(pubKey, startUnit, endUnit);
  const std::optional<uint8_t> age = ring != nullptr ? study_sleep::ageOf(*ring, key) : std::nullopt;
  sampler.offer(wholeText, reference, tag, key, age, fits, *start, spine);
}

// False when the byte budget stops the scan.
bool offerFile(const char* path, const size_t bytes, const std::string_view pubKey, study_sleep::Sampler& sampler,
               const study_sleep::RingView* ring, const FitGate& gate, ScanTotals& totals) {
  resetTaskWatchdogIfSubscribed();
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
  // whole texts would otherwise be copied into internal SRAM.
  JsonDocument doc(PsramJsonAllocator::json());
  const DocReadStatus status = PersistableStoreBase::readDocFromFileStreamed(path, doc);
  if (status != DocReadStatus::Ok) {
    LOG_ERR(MODULE, "Skipping %s: passages unreadable (status %u)", path, static_cast<unsigned>(status));
    // A parse that ran out of memory reports as a parse error; only the document knows the difference.
    if (doc.overflowed()) totals.outOfMemory = true;
    return true;
  }
  const int version = doc["v"] | 0;
  if (!persist::isKnownFormatVersion(version, study::PassageDoc::FORMAT_VERSION) || !doc["p"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "Skipping %s: unknown passage format v%d", path, version);
    return true;
  }
  uint32_t rows = 0;
  for (const JsonVariantConst row : doc["p"].as<JsonArrayConst>()) {
    if (++rows % ROWS_PER_WATCHDOG_RESET == 0) resetTaskWatchdogIfSubscribed();
    offerRow(row, pubKey, sampler, ring, gate, totals);
  }
  return true;
}

}  // namespace

bool pickFromAll(study_sleep::Sampler& sampler, const FitGate& gate, const study_sleep::RingView& ring,
                 ScanTotals& totals) {
  auto buffers = makeUniqueNoThrow<ScanBuffers>();
  if (!buffers) {
    LOG_ERR(MODULE, "OOM: passage scan buffers");
    return false;
  }
  auto dir = Storage.open(sdpaths::PASSAGES_DIR);
  if (!dir || !dir.isDirectory()) return false;

  uint32_t count = 0;
  for (auto entry = dir.openNextFile(); entry && count < MAX_ENTRIES; entry = dir.openNextFile()) {
    if (readPassageFileName(entry, buffers->name)) ++count;
  }
  totals.entries = count;
  if (count == 0) return false;
  if (count == MAX_ENTRIES)
    LOG_INF(MODULE, "Passage scan stopped counting at %u files", static_cast<unsigned>(MAX_ENTRIES));

  const uint32_t start = hardwareRandom(nullptr, count);

  for (uint8_t sweep = 0; sweep < 2; ++sweep) {
    dir.rewindDirectory();
    uint32_t index = 0;
    for (auto entry = dir.openNextFile(); entry && index < count; entry = dir.openNextFile()) {
      const auto pubKey = readPassageFileName(entry, buffers->name);
      if (!pubKey) continue;
      const uint32_t current = index++;
      if (!study_sleep::inSweep(sweep, current, start)) continue;

      snprintf(buffers->path, sizeof(buffers->path), "%s/%s", sdpaths::PASSAGES_DIR, buffers->name);
      if (!offerFile(buffers->path, entry.size(), *pubKey, sampler, &ring, gate, totals)) {
        LOG_INF(MODULE, "Passage scan stopped at the %u-byte budget", static_cast<unsigned>(MAX_TOTAL_BYTES));
        return sampler.result() != nullptr;
      }
    }
  }
  return sampler.result() != nullptr;
}

bool pickFromFile(const char* path, study_sleep::Sampler& sampler, const FitGate& gate, ScanTotals& totals) {
  // Checked first because a failed open logs an error, and a Bible with no
  // tagged passages yet is an ordinary case here.
  if (!Storage.exists(path)) return false;
  auto file = Storage.open(path);
  if (!file || file.isDirectory()) return false;
  const size_t bytes = file.size();
  // Close before reopen: readDocFromFileStreamed opens the same path.
  file.close();
  totals.entries = 1;

  const std::string_view name(path);
  const size_t slash = name.find_last_of('/');
  const auto pubKey = study_sleep::pubKeyFromFileName(slash == std::string_view::npos ? name : name.substr(slash + 1));
  if (!pubKey) return false;

  offerFile(path, bytes, *pubKey, sampler, nullptr, gate, totals);
  return sampler.result() != nullptr;
}

}  // namespace study_passage_scan
