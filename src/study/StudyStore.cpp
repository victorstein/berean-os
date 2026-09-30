#include "StudyStore.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "PassageFile.h"
#include "PubKeyRegistry.h"
#include "StudyStore/PubKey.h"
#include "StudyStore/UnitAnchors.h"
#include "TagPaletteFile.h"

namespace {

constexpr const char* MODULE = "STUDY";

struct RepairTextContext {
  UnitIndexCache* units;
  uint16_t spine;
};

std::string repairSpanText(void* ctx, const study::PassageSpan& span) {
  auto* self = static_cast<RepairTextContext*>(ctx);
  return self->units->rangeText(self->spine, span);
}

std::string repairUnitText(void* ctx, const study::Unit& unit) {
  auto* self = static_cast<RepairTextContext*>(ctx);
  return self->units->unitText(self->spine, unit);
}

}  // namespace

StudyStore& StudyStore::getInstance() {
  static StudyStore instance;
  return instance;
}

bool StudyStore::openPublication(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer) {
  closePublication();
  if (!epub) return false;

  study::PubKeyInputs keyInputs;
  keyInputs.isBible = epub->getBibleBookNavSpineIndex() >= 0;
  // The canon check the pubkey ladder requires is deferred to the index, which
  // reads the book-nav page anyway. Until it has, an NWT-shaped Bible is trusted
  // -- that is the only Bible this device has ever opened.
  keyInputs.canonVerified = keyInputs.isBible;
  keyInputs.bookPath = epub->getPath();
  keyInputs.registered = PubKeyRegistry::lookup(keyInputs.bookPath);
  pubKey_ = study::resolvePubKey(keyInputs);

  const auto paletteResult = TagPaletteFile::load(palette_);
  if (paletteResult == TagPaletteFile::LoadResult::Failed) {
    LOG_ERR(MODULE, "Tag palette unreadable; saving disabled for this session");
    saveDisabled_ = true;
  }

  const auto passageResult = PassageFile::load(pubKey_, passages_);
  if (passageResult == PassageFile::LoadResult::Failed) {
    LOG_ERR(MODULE, "Passages for %s unreadable; saving disabled for this session", pubKey_.c_str());
    saveDisabled_ = true;
  }

  units_ = makeUniqueNoThrow<UnitIndexCache>(epub, pubKey_, renderer);
  if (!units_ || !units_->begin()) {
    LOG_ERR(MODULE, "Unit index unavailable; addressing degraded to document offsets");
  }
  PsramJsonAllocator::logMemory("Study open");
  return true;
}

void StudyStore::closePublication() {
  units_.reset();
  passages_ = study::PassageDoc{PsramJsonAllocator::passageDoc()};
  pubKey_.clear();
  linkSource_.reset();
  // saveDisabled_ deliberately survives: it is a property of the session's
  // knowledge that a file may hold data we could not read, not of one book.
}

std::vector<uint16_t> StudyStore::spineIndicesForBook(const uint8_t book) {
  if (pubKey_ != study::BIBLE_PUB_KEY || !units_ || !units_->ready()) return {};
  return units_->spineIndicesForBook(book);
}

std::optional<PlacesDoc::PlaceUnit> StudyStore::placeAt(const uint16_t spineIndex, const uint32_t pageOffset) {
  if (pubKey_ != study::BIBLE_PUB_KEY || !units_ || !units_->ready()) return std::nullopt;
  return PlacesDoc::placeUnit(units_->peekUnits(spineIndex), pageOffset);
}

std::optional<StudyStore::Location> StudyStore::locatePlaceAtHint(const study::Unit& unit, const uint16_t spineHint) {
  if (pubKey_ != study::BIBLE_PUB_KEY || !units_ || !units_->ready()) return std::nullopt;
  if (spineHint >= units_->indexedDocumentCount()) return std::nullopt;
  if (const auto offset = study::documentOffsetOf(units_->peekUnits(spineHint), unit)) {
    return Location{spineHint, *offset};
  }
  return std::nullopt;
}

std::optional<StudyStore::Location> StudyStore::locatePlace(const study::Unit& unit, const uint16_t spineHint) {
  if (pubKey_ != study::BIBLE_PUB_KEY) return std::nullopt;
  return locateUnit(unit, spineHint);
}

static_assert(PlacesDoc::MAX_REFERENCE_BYTES == study::PassageDoc::MAX_REFERENCE_BYTES,
              "a place's reference is capped like a passage's");

bool StudyStore::save() {
  if (saveDisabled_) {
    LOG_ERR(MODULE, "Refusing to save: a store failed to load and may still hold data");
    return false;
  }
  if (pubKey_.empty()) return false;
  return PassageFile::save(pubKey_, passages_) == PassageFile::SaveResult::Ok;
}

std::vector<StudyStore::TagView> StudyStore::activeTags() const {
  std::vector<TagView> out;
  for (const study::TagId id : palette_.activeIds()) out.push_back({id, palette_.name(id)});
  return out;
}

std::vector<size_t> StudyStore::passagesWithTag(const study::TagId id) const {
  std::vector<size_t> out;
  const auto& all = passages_.passages();
  for (size_t i = 0; i < all.size(); ++i) {
    for (const study::TagId carried : all[i].tags) {
      if (carried == id) {
        out.push_back(i);
        break;
      }
    }
  }
  return out;
}

std::string StudyStore::tagName(const study::TagId id) const {
  if (id == study::UNLABELLED) return tr(STR_TAG_UNLABELLED);
  return palette_.name(id);
}

std::string StudyStore::tagNamesFor(const size_t passageIndex) const {
  if (passageIndex >= passages_.passages().size()) return {};
  std::string out;
  for (const study::TagId id : passages_.passages()[passageIndex].tags) {
    const std::string name = tagName(id);
    if (name.empty()) continue;
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}

std::optional<StudyStore::Location> StudyStore::locate(const size_t passageIndex) {
  if (passageIndex >= passages_.passages().size()) return std::nullopt;
  const auto& passage = passages_.passages()[passageIndex];
  const auto found = locateUnit(passage.start, passage.documentSpine);
  if (found && found->spineIndex != passage.documentSpine)
    passages_.repairDocumentSpine(passageIndex, found->spineIndex);
  return found;
}

std::optional<StudyStore::Location> StudyStore::locateLink(const size_t passageIndex, const size_t linkIndex) {
  if (!units_ || !units_->ready() || passageIndex >= passages_.passages().size()) return std::nullopt;
  const auto& links = passages_.passages()[passageIndex].links;
  if (linkIndex >= links.size()) return std::nullopt;
  const study::PassageLink& link = links[linkIndex];

  // documentOffsetOf accepts a DocumentOffset unit in ANY document, so a hint
  // left pointing at the wrong document -- an out-of-range spine, a document of
  // another kind after an edition change, or one that could not be indexed at
  // all -- would "resolve" to an arbitrary place. Refuse it rather than open the
  // wrong text.
  if (link.target.kind == study::UnitKind::DocumentOffset) {
    if (link.targetSpine >= units_->indexedDocumentCount()) return std::nullopt;
    if (units_->unitsFor(link.targetSpine).kind != study::UnitKind::DocumentOffset) return std::nullopt;
    if (units_->indexFailed(link.targetSpine)) return std::nullopt;
  }
  return locateUnit(link.target, link.targetSpine);
}

std::optional<StudyStore::Location> StudyStore::locateUnit(const study::Unit& unit, const uint16_t spineHint) {
  if (!units_ || !units_->ready()) return std::nullopt;

  if (const auto offset = study::documentOffsetOf(units_->unitsFor(spineHint), unit)) {
    return Location{spineHint, *offset};
  }

  // Only a Verse address is portable enough to search for. A data-pid or a raw
  // offset means nothing outside the document it was measured in, so a stale
  // hint there is unrecoverable.
  if (unit.kind != study::UnitKind::Verse || unit.book == 0) return std::nullopt;

  for (const uint16_t candidate : units_->spineIndicesForBook(unit.book)) {
    if (candidate == spineHint) continue;
    if (const auto offset = study::documentOffsetOf(units_->unitsFor(candidate), unit)) {
      return Location{candidate, *offset};
    }
    vTaskDelay(1);  // up to 150 documents, each possibly a fresh index build
  }
  return std::nullopt;
}

std::optional<study::TagId> StudyStore::addTagName(const std::string& name) {
  if (saveDisabled_) return std::nullopt;

  const size_t before = palette_.activeCount();
  const auto id = palette_.add(name);
  if (!id) return std::nullopt;
  if (palette_.activeCount() == before) return id;  // existing name, nothing to persist

  if (TagPaletteFile::save(palette_) != TagPaletteFile::SaveResult::Ok) {
    LOG_ERR(MODULE, "Could not persist the tag palette; the new tag is not kept");
    palette_.retire(*id);
    return std::nullopt;
  }
  return id;
}

bool StudyStore::retireTag(const study::TagId id) {
  if (saveDisabled_ || id == study::UNLABELLED) return false;

  palette_.retire(id);
  if (TagPaletteFile::save(palette_) != TagPaletteFile::SaveResult::Ok) return false;

  // Passages keep their other tags; one left with none becomes UNLABELLED.
  passages_.removeTagEverywhere(id);
  return save();
}

std::vector<StudyStore::PaintedPassage> StudyStore::passagesInDocument(const uint16_t spineIndex) {
  std::vector<PaintedPassage> out;
  if (!units_ || !units_->ready()) return out;

  const study::DocumentUnits& units = units_->unitsFor(spineIndex);

  for (size_t i = 0; i < passages_.passages().size(); ++i) {
    const auto& p = passages_.passages()[i];
    const bool spineMatches = p.documentSpine == spineIndex;

    // A Verse address is portable: book + chapter + verse identifies a passage
    // in ANY edition, which is the whole reason the Bible's pubkey carries no
    // language. So the stored spine is only a hint, and a passage whose hint is
    // stale -- because the publication was replaced by an edition laid out
    // differently -- is still found by resolving its address here.
    //
    // Paragraph and DocumentOffset addresses are NOT portable: a data-pid is
    // unique only within its document, and a raw offset means nothing outside
    // the one it was measured in. documentOffsetOf returns a DocumentOffset
    // unconditionally, so without this guard every such passage would paint in
    // every document.
    if (p.start.kind != study::UnitKind::Verse && !spineMatches) continue;

    const auto start = study::documentOffsetOf(units, p.start);
    if (!start) continue;
    const auto end = study::documentOffsetOf(units, p.end);

    // Repaired in memory only. This runs on the page-turn path, and an SD write
    // there costs serialisation, I/O and storageMutex contention; the next tag
    // edit persists it, and until then it simply re-repairs each session.
    if (!spineMatches) passages_.repairDocumentSpine(i, spineIndex);

    // Fingerprint differs -> the text this was attached to is not the text that
    // is there. Paint nothing: a mark drawn over different words is a claim the
    // data does not support. The tag list still lists it.
    if (p.fingerprint.length != 0) {
      const study::Fingerprint current = study::fingerprintOf(units_->unitText(spineIndex, p.start));
      if (current.length != 0 && !(current == p.fingerprint)) continue;
    }

    PaintedPassage painted;
    painted.index = i;
    painted.startOffset = *start;
    painted.wholeUnit = !end.has_value() || *end <= *start;
    painted.endOffset = painted.wholeUnit ? *start : *end;
    out.push_back(painted);
  }
  return out;
}

bool StudyStore::addPassage(const uint16_t spineIndex, const uint32_t startOffset, const uint32_t endOffset,
                            const std::string& reference, std::vector<study::TagId> tags) {
  if (saveDisabled_ || !units_) return false;

  const study::DocumentUnits& units = units_->unitsFor(spineIndex);

  study::TaggedPassage passage;
  passage.start = study::resolve(units, startOffset);
  passage.end = study::resolve(units, endOffset);
  passage.documentSpine = spineIndex;
  passage.reference = reference;
  passage.tags = std::move(tags);

  const auto span = study::snapSpan(units, passage.start, passage.end);
  if (!span) {
    LOG_ERR(MODULE, "Passage span unresolved in spine %u; not saved", spineIndex);
    return false;
  }
  const std::string whole = study::normaliseWholeText(units_->rangeText(spineIndex, *span));
  if (whole.empty()) {
    LOG_ERR(MODULE, "Passage text unreadable in spine %u; not saved", spineIndex);
    return false;
  }
  passage.displayText = passages_.newText();
  if (!passage.displayText.assign(whole)) {
    LOG_ERR(MODULE, "OOM: %u-byte passage text; not saved", static_cast<unsigned>(whole.size()));
    return false;
  }
  passage.whole = true;

  passage.fingerprint = study::fingerprintOf(units_->unitText(spineIndex, passage.start));
  if (!(passage.end == passage.start)) {
    passage.endFingerprint = study::fingerprintOf(units_->unitText(spineIndex, passage.end));
  }

  if (!passages_.add(std::move(passage))) return false;
  if (save()) {
    PsramJsonAllocator::logMemory("Passage added");
    return true;
  }

  passages_.remove(passages_.passages().size() - 1);
  return false;
}

bool StudyStore::removePassage(const size_t index) {
  if (saveDisabled_ || index >= passages_.passages().size()) return false;

  study::TaggedPassage backup;
  backup.displayText = passages_.newText();
  if (!study::copyPassage(passages_.passages()[index], backup)) {
    LOG_ERR(MODULE, "OOM: backup of passage %u; not removed", static_cast<unsigned>(index));
    return false;
  }
  if (!passages_.remove(index)) return false;
  linkSource_.reset();
  if (save()) return true;

  passages_.add(std::move(backup));
  return false;
}

bool StudyStore::setPassageTags(const size_t index, std::vector<study::TagId> tags) {
  if (saveDisabled_ || index >= passages_.passages().size()) return false;

  const std::vector<study::TagId> backup = passages_.passages()[index].tags;
  if (!passages_.setTags(index, std::move(tags))) return false;
  if (save()) return true;

  passages_.setTags(index, backup);
  return false;
}

void StudyStore::markLinkSource(const size_t index) {
  if (index < passages_.passages().size()) linkSource_ = index;
}

StudyStore::LinkOutcome StudyStore::linkMarkedSourceTo(const size_t targetIndex) {
  if (!linkSource_ || *linkSource_ >= passages_.passages().size()) return LinkOutcome::NoSource;
  if (saveDisabled_) return LinkOutcome::NotSaved;

  switch (passages_.linkPassages(*linkSource_, targetIndex)) {
    case study::PassageDoc::LinkResult::Linked:
      break;
    case study::PassageDoc::LinkResult::AlreadyLinked:
      return LinkOutcome::AlreadyLinked;
    case study::PassageDoc::LinkResult::AtCap:
      return LinkOutcome::AtCap;
    case study::PassageDoc::LinkResult::SelfLink:
      return LinkOutcome::SelfLink;
    case study::PassageDoc::LinkResult::NoSuchPassage:
      return LinkOutcome::NoTarget;  // the source was checked above
    case study::PassageDoc::LinkResult::OverBudget:
      return LinkOutcome::NotSaved;
  }
  if (save()) return LinkOutcome::Linked;

  passages_.removeLink(*linkSource_, passages_.passages()[*linkSource_].links.size() - 1);
  return LinkOutcome::NotSaved;
}

bool StudyStore::removeLink(const size_t passageIndex, const size_t linkIndex) {
  if (saveDisabled_ || passageIndex >= passages_.passages().size()) return false;

  const std::vector<study::PassageLink> backup = passages_.passages()[passageIndex].links;
  if (!passages_.removeLink(passageIndex, linkIndex)) return false;
  if (save()) return true;

  passages_.setLinks(passageIndex, backup);
  return false;
}

void StudyStore::repairTexts() {
  if (saveDisabled_ || !units_ || !units_->ready() || pubKey_.empty()) return;
  if (repairPubKey_ != pubKey_) {
    repairSchedule_ = study::RepairSchedule{};
    repairPubKey_ = pubKey_;
  }

  std::vector<bool> needsRepair(passages_.passages().size());
  for (size_t i = 0; i < needsRepair.size(); ++i) needsRepair[i] = !passages_.passages()[i].whole;
  const std::vector<size_t> order = repairSchedule_.order(needsRepair);
  if (order.empty()) return;

  struct Undo {
    size_t index;
    study::TaggedPassage before;
  };
  std::vector<Undo> undo;
  undo.reserve(order.size());
  uint8_t searchesLeft = REPAIR_SEARCHES_PER_PASS;
  unsigned rebuilt = 0;
  unsigned suspect = 0;
  unsigned leftAsIs = 0;
  const unsigned long started = millis();

  // Phase 1: rows whose units resolve at their stored spine hint, one document
  // each. Phase 2: the rest, of which at most REPAIR_SEARCHES_PER_PASS run the
  // book search -- so a stale-hint row stored early cannot spend the budget
  // before the cheap rows after it. Classifying reads each hint's index entry
  // and counts against the same budget.
  struct RowAtHint {
    size_t index;
    bool atHint;
  };
  std::vector<RowAtHint> phased;
  std::vector<RowAtHint> needsSearch;
  phased.reserve(order.size());
  needsSearch.reserve(order.size());
  for (const size_t index : order) {
    const study::TaggedPassage& passage = passages_.passages()[index];
    const bool atHint = passage.documentSpine < units_->indexedDocumentCount() &&
                        study::documentOffsetOf(units_->unitsFor(passage.documentSpine), passage.start).has_value();
    (atHint ? phased : needsSearch).push_back({index, atHint});
  }
  phased.insert(phased.end(), needsSearch.begin(), needsSearch.end());

  for (const auto [index, atHint] : phased) {
    if (millis() - started >= REPAIR_TIME_BUDGET_MS) break;
    const study::TaggedPassage& passage = passages_.passages()[index];
    const std::string startUnit = study::unitToCompact(passage.start);

    const bool searchable = passage.start.kind == study::UnitKind::Verse && passage.start.book != 0;
    if (!atHint && searchable && searchesLeft == 0) continue;  // deferred, not attempted
    repairSchedule_.markAttempted(index);

    std::optional<uint16_t> spine;
    if (atHint) {
      spine = passage.documentSpine;
    } else if (searchable) {
      --searchesLeft;
      if (const auto found = locateUnit(passage.start, passage.documentSpine)) spine = found->spineIndex;
    }

    RepairTextContext ctx{units_.get(), spine.value_or(0)};
    study::TextRepairInputs in;
    if (spine) {
      in.units = &units_->unitsFor(*spine);
      in.ctx = &ctx;
      in.spanText = &repairSpanText;
      in.unitText = &repairUnitText;
    }
    const study::TextRepairPlan plan = study::planTextRepair(passage, in);

    if (plan.outcome != study::TextRepairOutcome::Rebuilt &&
        plan.outcome != study::TextRepairOutcome::RebuiltSuspectStart) {
      LOG_ERR(MODULE, "Repair: %s left as is (%s)", startUnit.c_str(),
              plan.outcome == study::TextRepairOutcome::FingerprintMismatch ? "fingerprint mismatch" : "unresolvable");
      ++leftAsIs;
      vTaskDelay(1);
      continue;
    }

    Undo entry{index, study::TaggedPassage{}};
    entry.before.displayText = passages_.newText();
    if (!study::copyPassage(passage, entry.before)) {
      LOG_ERR(MODULE, "Repair: %s left as is (OOM for its backup)", startUnit.c_str());
      ++leftAsIs;
      vTaskDelay(1);
      continue;
    }

    const auto result = passages_.setWholeText(index, plan.wholeText);
    if (result != study::PassageDoc::TextResult::Set) {
      LOG_ERR(MODULE, "Repair: %s left as is (%s)", startUnit.c_str(),
              result == study::PassageDoc::TextResult::OverBudget ? "over budget" : "out of memory");
      ++leftAsIs;
      vTaskDelay(1);
      continue;
    }
    if (*spine != entry.before.documentSpine) passages_.repairDocumentSpine(index, *spine);
    if (plan.outcome == study::TextRepairOutcome::RebuiltSuspectStart) {
      LOG_INF(MODULE, "Repair: %s (%s) rebuilt from a suspect start", startUnit.c_str(),
              entry.before.reference.c_str());
      ++suspect;
    }
    ++rebuilt;
    undo.push_back(std::move(entry));
    // One yield per row: each can stream a document, and the task watchdog
    // panics at 5 s -- MigrationRunner yields the same way.
    vTaskDelay(1);
  }

  if (!undo.empty() && !save()) {
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) passages_.replace(it->index, std::move(it->before));
    LOG_ERR(MODULE, "Repair: save failed; %u rebuilt passages restored", static_cast<unsigned>(undo.size()));
    return;
  }

  unsigned notWhole = 0;
  for (const auto& p : passages_.passages()) {
    if (!p.whole) ++notWhole;
  }
  LOG_INF(MODULE, "Repair: %u rebuilt (%u suspect), %u left as is, %u still not whole, %lu ms", rebuilt, suspect,
          leftAsIs, notWhole, millis() - started);
}
