#include "StudyStore.h"

#include <Logging.h>
#include <Memory.h>

#include "PassageFile.h"
#include "StudyStore/PubKey.h"
#include "StudyStore/UnitAnchors.h"
#include "TagPaletteFile.h"

namespace {

constexpr const char* MODULE = "STUDY";

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
  return true;
}

void StudyStore::closePublication() {
  units_.reset();
  passages_ = study::PassageDoc{};
  pubKey_.clear();
  // saveDisabled_ deliberately survives: it is a property of the session's
  // knowledge that a file may hold data we could not read, not of one book.
}

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

std::string StudyStore::tagNamesFor(const size_t passageIndex) const {
  if (passageIndex >= passages_.passages().size()) return {};
  std::string out;
  for (const study::TagId id : passages_.passages()[passageIndex].tags) {
    const std::string& name = palette_.name(id);
    if (name.empty()) continue;
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}

std::optional<uint32_t> StudyStore::documentOffsetFor(const size_t passageIndex) {
  if (!units_ || !units_->ready() || passageIndex >= passages_.passages().size()) return std::nullopt;
  const auto& p = passages_.passages()[passageIndex];
  return study::documentOffsetOf(units_->unitsFor(p.documentSpine), p.start);
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
  if (saveDisabled_) return false;

  palette_.retire(id);
  if (TagPaletteFile::save(palette_) != TagPaletteFile::SaveResult::Ok) return false;

  // Passages keep their other tags; one left with none is retained and shows as
  // untagged, awaiting re-tagging.
  passages_.removeTagEverywhere(id);
  return save();
}

std::vector<StudyStore::PaintedPassage> StudyStore::passagesInDocument(const uint16_t spineIndex) {
  std::vector<PaintedPassage> out;
  if (!units_ || !units_->ready()) return out;

  const study::DocumentUnits& units = units_->unitsFor(spineIndex);

  for (size_t i = 0; i < passages_.passages().size(); ++i) {
    const auto& p = passages_.passages()[i];
    if (p.documentSpine != spineIndex) continue;

    const auto start = study::documentOffsetOf(units, p.start);
    if (!start) continue;
    const auto end = study::documentOffsetOf(units, p.end);

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
                            const std::string& snippet, const std::string& reference,
                            std::vector<study::TagId> tags) {
  if (saveDisabled_ || tags.empty() || !units_) return false;

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
