#include "MigrationRunner.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <SaveBudget.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BookPathIndex.h"
#include "PassageFile.h"
#include "PubKeyRegistry.h"
#include "StudyStore/MigrationPlanner.h"
#include "StudyStore/PubKey.h"
#include "TagPaletteFile.h"
#include "study/UnitIndexCache.h"
#include "util/HighlightFile.h"

namespace {

constexpr const char* MODULE = "MIGRATE";
constexpr const char* BEREAN_DIR = "/.berean";
constexpr const char* LEGACY_DIR = "/.crosspoint/highlights";
constexpr int LEDGER_FORMAT_VERSION = 1;

struct FileReport {
  std::string source;
  std::string pubKey;
  std::string bookPath;
  uint16_t read = 0;
  uint16_t written = 0;
  uint16_t verse = 0;
  uint16_t mismatches = 0;
  uint16_t pending = 0;
  std::vector<std::string> drops;
};

std::vector<std::string> legacySources() {
  std::vector<std::string> out;
  for (const String& entry : Storage.listFiles(LEGACY_DIR, 200)) {
    const std::string name(entry.c_str());
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) out.push_back(name);
  }
  return out;
}

// nullopt means the ledger's bytes are on the card but unusable -- unreadable,
// unparseable, or a format this build refuses. An empty vector means genuinely
// absent, or present and recording nothing; both are safe to append to.
//
// The two must not be collapsed. A ledger read as "nothing migrated" re-runs
// the migration, and PassageDoc::add appends without deduplicating, so every
// passage in every already-migrated file would be added a second time.
std::optional<std::vector<std::string>> readLedger() {
  JsonDocument doc;
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);
  if (status == DocReadStatus::Missing) return std::vector<std::string>{};
  if (status != DocReadStatus::Ok) return std::nullopt;
  // The one nullopt cause nothing else reports: a well-formed future-format
  // file reads Ok, so readDocFromFileAdopting stays silent.
  if ((doc["v"] | 0) > LEDGER_FORMAT_VERSION) {
    LOG_ERR(MODULE, "Refusing to read a newer ledger format");
    return std::nullopt;
  }

  std::vector<std::string> done;
  for (const JsonVariantConst v : doc["done"].as<JsonArrayConst>()) {
    const char* name = v["f"] | "";
    if (name[0] != '\0') done.emplace_back(name);
  }
  return done;
}

bool ledgerContains(const std::vector<std::string>& ledger, const std::string& name) {
  for (const auto& entry : ledger) {
    if (entry == name) return true;
  }
  return false;
}

bool appendLedger(const std::string& name, const uint16_t passages) {
  JsonDocument doc;
  // Refuse BEFORE reading doc: a ParseError leaves the partially parsed
  // document behind, so appending to it would write back half a ledger.
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(MigrationRunner::LEDGER_PATH, doc);
  if (!mayOverwriteAfterRead(status)) {
    LOG_ERR(MODULE, "Migration ledger unreadable; refusing to overwrite it");
    return false;
  }
  if ((doc["v"] | 0) > LEDGER_FORMAT_VERSION) {
    LOG_ERR(MODULE, "Refusing to rewrite a newer ledger format");
    return false;
  }
  doc["v"] = LEDGER_FORMAT_VERSION;
  if (!doc["done"].is<JsonArray>()) doc["done"].to<JsonArray>();

  const auto row = doc["done"].as<JsonArray>().add<JsonObject>();
  row["f"] = name;
  row["p"] = passages;

  // Bounded well clear of the ceiling: at most 200 sources (legacySources caps
  // the listing) at at most 146 bytes a row -- the SD layer reads a filename
  // into a char[128] -- is about 29 KB. The gate is the rule, not a reachable
  // limit.
  if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Migration ledger exceeds the save budget; not written");
    return false;
  }

  Storage.mkdir(BEREAN_DIR);
  return PersistableStoreBase::writeDocToFileAtomic(MigrationRunner::LEDGER_PATH, doc);
}

// The flattened stem a legacy filename was built from: "<stem>.json".
std::string stemOf(const std::string& filename) { return filename.substr(0, filename.size() - 5); }

struct UnitTextContext {
  UnitIndexCache* index = nullptr;
  uint16_t spineIndex = 0;
};

std::string unitTextFor(void* ctx, const study::Unit& unit) {
  auto* self = static_cast<UnitTextContext*>(ctx);
  if (!self->index) return {};
  return self->index->unitText(self->spineIndex, unit);
}

void writeReport(const MigrationRunner::Summary& summary, const std::vector<FileReport>& files) {
  JsonDocument doc;
  doc["v"] = 1;

  const auto s = doc["summary"].to<JsonObject>();
  s["sourceFiles"] = summary.sourceFiles;
  s["highlightsRead"] = summary.highlightsRead;
  s["passagesWritten"] = summary.passagesWritten;
  s["addressedVerse"] = summary.addressedVerse;
  s["addressedParagraph"] = summary.addressedParagraph;
  s["addressedDocumentOffset"] = summary.addressedDocumentOffset;
  s["referenceMismatches"] = summary.referenceMismatches;
  s["pendingUpgrade"] = summary.pendingUpgrade;
  s["dropped"] = summary.dropped;
  s["tagsAdopted"] = summary.tagsAdopted;

  const auto rows = doc["files"].to<JsonArray>();
  for (const auto& f : files) {
    // A report that refuses to save would be an absurd way to fail a successful
    // migration, so rows stop before the budget and the truncation is declared.
    if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET - 2048) {
      doc["truncated"] = true;
      break;
    }
    const auto row = rows.add<JsonObject>();
    row["source"] = f.source;
    row["pubKey"] = f.pubKey;
    row["bookPath"] = f.bookPath;
    row["read"] = f.read;
    row["written"] = f.written;
    row["verse"] = f.verse;
    row["mismatches"] = f.mismatches;
    row["pending"] = f.pending;
    const auto drops = row["drops"].to<JsonArray>();
    for (const auto& d : f.drops) drops.add(d);
  }

  Storage.mkdir(BEREAN_DIR);
  if (!PersistableStoreBase::writeDocToFileAtomic(MigrationRunner::REPORT_PATH, doc)) {
    LOG_ERR(MODULE, "Could not write the migration report");
  }
}

}  // namespace

namespace MigrationRunner {

bool pending() {
  const auto sources = legacySources();
  if (sources.empty()) return false;
  const auto ledger = readLedger();
  // Pending on purpose when the ledger is unusable: runIfPending is the only
  // place that can log the refusal, so returning false here would make a
  // corrupt ledger a silent no-op.
  if (!ledger) return true;
  for (const auto& name : sources) {
    if (!ledgerContains(*ledger, name)) return true;
  }
  return false;
}

bool runIfPending(Summary& summary, GfxRenderer& renderer, const MigrationProgress& progress) {
  const auto sources = legacySources();
  if (sources.empty()) return true;

  auto ledgerRead = readLedger();
  if (!ledgerRead) {
    LOG_ERR(MODULE, "Migration ledger unreadable; refusing to migrate over it");
    return false;
  }
  auto ledger = std::move(*ledgerRead);
  std::vector<FileReport> reports;
  bool allOk = true;

  study::TagPalette palette;
  const auto paletteLoad = TagPaletteFile::load(palette);
  if (paletteLoad == TagPaletteFile::LoadResult::Failed) {
    LOG_ERR(MODULE, "Tag palette unreadable; refusing to migrate over it");
    return false;
  }

  for (const auto& name : sources) {
    if (ledgerContains(ledger, name)) continue;

    FileReport report;
    report.source = name;
    summary.sourceFiles++;

    const std::string stem = stemOf(name);
    const auto bookPath = BookPathIndex::resolve(stem);

    // HighlightFile::load keys on the BOOK PATH, so a source whose book cannot
    // be found is unreadable through it. Nothing is lost -- the legacy file is
    // untouched and stays out of the ledger, so a later boot with the card
    // populated picks it up.
    if (!bookPath) {
      LOG_ERR(MODULE, "Cannot locate the book for %s; leaving it for a later run", name.c_str());
      report.drops.push_back("book not found on card");
      reports.push_back(report);
      allOk = false;
      continue;
    }
    report.bookPath = *bookPath;

    HighlightDoc legacy;
    const auto loadResult = HighlightFile::load(*bookPath, legacy);
    if (loadResult == HighlightFile::LoadResult::Failed) {
      LOG_ERR(MODULE, "%s is unreadable; leaving it untouched", name.c_str());
      report.drops.push_back("legacy file unreadable");
      reports.push_back(report);
      allOk = false;
      continue;
    }
    if (loadResult == HighlightFile::LoadResult::Empty) {
      if (!appendLedger(name, 0)) {
        LOG_ERR(MODULE, "Could not record %s as migrated; stopping", name.c_str());
        report.drops.push_back("ledger not updated");
        reports.push_back(report);
        allOk = false;
        break;
      }
      ledger.push_back(name);
      reports.push_back(report);
      continue;
    }

    // Tag names FIRST, so a tag the user defined but never applied survives.
    const size_t tagsBefore = palette.activeCount();
    study::adoptTagNames(palette, legacy.tags());
    summary.tagsAdopted = static_cast<uint16_t>(palette.activeCount());
    (void)tagsBefore;

    auto epub = makeUniqueNoThrow<Epub>(*bookPath, "/.crosspoint");
    const bool sourceAvailable = epub && epub->load(/*buildIfMissing=*/true);

    study::PubKeyInputs keyInputs;
    keyInputs.isBible = sourceAvailable && epub->getBibleBookNavSpineIndex() >= 0;
    keyInputs.canonVerified = keyInputs.isBible;
    keyInputs.bookPath = *bookPath;
    keyInputs.registered = PubKeyRegistry::lookup(*bookPath);
    const std::string pubKey = study::resolvePubKey(keyInputs);
    report.pubKey = pubKey;

    study::PassageDoc passages;
    if (PassageFile::load(pubKey, passages) == PassageFile::LoadResult::Failed) {
      LOG_ERR(MODULE, "Existing passages for %s unreadable; refusing to overwrite", pubKey.c_str());
      report.drops.push_back("destination unreadable");
      reports.push_back(report);
      allOk = false;
      continue;
    }

    std::shared_ptr<Epub> sharedEpub(epub.release());
    std::unique_ptr<UnitIndexCache> index;
    if (sourceAvailable) {
      index = makeUniqueNoThrow<UnitIndexCache>(sharedEpub, pubKey, renderer);
      if (index) {
        index->setProgress(progress);
        if (!index->begin()) index.reset();
      }
    }

    for (const auto& entry : legacy.highlights()) {
      summary.highlightsRead++;
      report.read++;

      study::LegacyHighlight flat;
      flat.spineIndex = entry.spineIndex;
      flat.start = entry.range.start;
      flat.end = entry.range.end;
      flat.snippet = entry.label;
      flat.reference = entry.reference;
      for (const uint16_t tagIndex : entry.tagIndices) {
        if (tagIndex < legacy.tags().size()) flat.tagNames.push_back(legacy.tags()[tagIndex]);
      }

      UnitTextContext textCtx;
      textCtx.index = index.get();
      textCtx.spineIndex = entry.spineIndex;

      study::MigrationInputs in;
      in.pubKey = pubKey;
      in.sourceAvailable = sourceAvailable && index != nullptr;
      in.palette = &palette;
      if (in.sourceAvailable) {
        in.units = index->unitsFor(entry.spineIndex);
        in.document = sharedEpub->getSpineItem(entry.spineIndex).href;
        in.unitTextCtx = &textCtx;
        in.unitText = &unitTextFor;
      }

      const auto planned = study::planMigration(in, flat);
      switch (planned.outcome) {
        case study::MigrationOutcome::DroppedNoTags:
          summary.dropped++;
          report.drops.push_back(entry.reference.empty() ? "untagged" : entry.reference);
          continue;
        case study::MigrationOutcome::ResolvedReferenceMismatch:
          summary.referenceMismatches++;
          report.mismatches++;
          break;
        case study::MigrationOutcome::PendingUpgrade:
          summary.pendingUpgrade++;
          break;
        default:
          break;
      }

      if (!planned.passage) continue;
      switch (planned.passage->start.kind) {
        case study::UnitKind::Verse:
          summary.addressedVerse++;
          report.verse++;
          break;
        case study::UnitKind::Paragraph:
          summary.addressedParagraph++;
          break;
        case study::UnitKind::DocumentOffset:
          summary.addressedDocumentOffset++;
          break;
      }

      if (!passages.add(*planned.passage)) {
        report.drops.push_back("store full");
        summary.dropped++;
      }

      // One yield per document, not per source file: 50 documents at ~30 ms is
      // comfortable, 200 would not be, and the watchdog panics at 5 s.
      progress.tick();
      vTaskDelay(1);
    }

    const bool passagesSaved = PassageFile::save(pubKey, passages) == PassageFile::SaveResult::Ok;
    const bool paletteSaved = TagPaletteFile::save(palette) == TagPaletteFile::SaveResult::Ok;

    if (!passagesSaved || !paletteSaved) {
      LOG_ERR(MODULE, "Save failed for %s; not recording it as migrated", name.c_str());
      allOk = false;
      reports.push_back(report);
      continue;
    }

    // Counted from the store AFTER the save, never from the planner's outcomes:
    // the planner can report Resolved for a passage that add() then refuses.
    study::PassageDoc verify;
    if (PassageFile::load(pubKey, verify) == PassageFile::LoadResult::Loaded) {
      report.written = static_cast<uint16_t>(verify.passages().size());
    }
    summary.passagesWritten = static_cast<uint16_t>(summary.passagesWritten + report.written);

    // A ledger write failure is global, not per-file -- all of its causes are
    // properties of the ledger or the card -- so continuing would keep writing
    // passages that nothing records, each one a duplicate on the next boot.
    if (!appendLedger(name, report.written)) {
      LOG_ERR(MODULE, "Could not record %s as migrated; stopping", name.c_str());
      report.drops.push_back("ledger not updated");
      reports.push_back(report);
      allOk = false;
      break;
    }
    ledger.push_back(name);
    reports.push_back(report);
  }

  writeReport(summary, reports);
  return allOk;
}

}  // namespace MigrationRunner
