#include "StudyStore.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "ChapterCompletionFile.h"
#include "PassageFile.h"
#include "PubKeyRegistry.h"
#include "StudyStore/PubKey.h"
#include "StudyStore/UnitAnchors.h"
#include "TagPaletteFile.h"

namespace {

constexpr const char* MODULE = "STUDY";

bool saveBibleCompletion(const study::ChapterCompletion& record) {
  return ChapterCompletionFile::save(study::BIBLE_PUB_KEY, record) == ChapterCompletionFile::SaveResult::Ok;
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

  // Only the shared Bible key has canonical book numbers to record against.
  if (pubKey_ == study::BIBLE_PUB_KEY &&
      ChapterCompletionFile::load(pubKey_, completion_) == ChapterCompletionFile::LoadResult::Failed) {
    LOG_ERR(MODULE, "Chapter completion unreadable; not recording chapters this session");
    completionSaveDisabled_ = true;
  }

  units_ = makeUniqueNoThrow<UnitIndexCache>(epub, pubKey_, renderer);
  if (!units_ || !units_->begin()) {
    LOG_ERR(MODULE, "Unit index unavailable; addressing degraded to document offsets");
  }
  return true;
}

void StudyStore::closePublication() {
  units_.reset();
  passages_ = study::PassageDoc{};
  completion_ = study::ChapterCompletion{};
  pubKey_.clear();
  linkSource_.reset();
  // saveDisabled_ deliberately survives: it is a property of the session's
  // knowledge that a file may hold data we could not read, not of one book.
}

std::vector<uint16_t> StudyStore::spineIndicesForBook(const uint8_t book) {
  if (pubKey_ != study::BIBLE_PUB_KEY || !units_ || !units_->ready()) return {};
  return units_->spineIndicesForBook(book);
}

bool StudyStore::save() {
  if (saveDisabled_) {
    LOG_ERR(MODULE, "Refusing to save: a store failed to load and may still hold data");
    return false;
  }
  if (pubKey_.empty()) return false;
  return PassageFile::save(pubKey_, passages_) == PassageFile::SaveResult::Ok;
}

study::CompletionMarkResult StudyStore::markDocumentRead(const uint16_t spineIndex) {
  if (pubKey_ != study::BIBLE_PUB_KEY || !units_ || !units_->ready()) return study::CompletionMarkResult::NothingNew;

  // The page just rendered asked for these same units, so this is normally the
  // cached document and costs no I/O.
  const auto result = study::recordDocumentRead(completion_, units_->unitsFor(spineIndex), completionSaveDisabled_,
                                                saveBibleCompletion);
  if (result == study::CompletionMarkResult::SaveFailed) {
    LOG_ERR(MODULE, "Could not persist chapter completion for spine %u", spineIndex);
  }
  return result;
}

bool StudyStore::takeCompletionLoadFailureNotice() {
  if (pubKey_ != study::BIBLE_PUB_KEY || !completionSaveDisabled_ || completionLoadFailureAnnounced_) return false;
  completionLoadFailureAnnounced_ = true;
  return true;
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
                            const std::string& snippet, const std::string& reference, std::vector<study::TagId> tags) {
  if (saveDisabled_ || !units_) return false;

  const study::DocumentUnits& units = units_->unitsFor(spineIndex);

  study::TaggedPassage passage;
  passage.start = study::resolve(units, startOffset);
  passage.end = study::resolve(units, endOffset);
  passage.documentSpine = spineIndex;
  passage.snippet = snippet;
  passage.reference = reference;
  passage.tags = std::move(tags);
  passage.fingerprint = study::fingerprintOf(units_->unitText(spineIndex, passage.start));
  if (!(passage.end == passage.start)) {
    passage.endFingerprint = study::fingerprintOf(units_->unitText(spineIndex, passage.end));
  }

  if (!passages_.add(std::move(passage))) return false;
  if (save()) return true;

  passages_.remove(passages_.passages().size() - 1);
  return false;
}

bool StudyStore::removePassage(const size_t index) {
  if (saveDisabled_ || index >= passages_.passages().size()) return false;

  const study::TaggedPassage backup = passages_.passages()[index];
  if (!passages_.remove(index)) return false;
  linkSource_.reset();
  if (save()) return true;

  passages_.add(backup);
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
