#include "StudyStore/PassageDoc.h"

#include <Utf8.h>

#include <algorithm>

namespace study {
namespace {

// Never returns an empty list: a passage with no real tag carries UNLABELLED,
// and one with any real tag does not.
std::vector<TagId> normaliseTags(const std::vector<TagId>& tags) {
  std::vector<TagId> out;
  out.reserve(std::max<size_t>(1, std::min(tags.size(), PassageDoc::MAX_TAGS_PER_PASSAGE)));
  for (const TagId id : tags) {
    if (out.size() >= PassageDoc::MAX_TAGS_PER_PASSAGE) break;
    if (id == UNLABELLED) continue;
    if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  }
  if (out.empty()) out.push_back(UNLABELLED);
  return out;
}

// Drops links to the passage's own start and repeats, caps the count, and bounds
// each label.
std::vector<PassageLink> normaliseLinks(std::vector<PassageLink> links, const Unit& ownStart) {
  std::vector<PassageLink> out;
  out.reserve(std::min(links.size(), PassageDoc::MAX_LINKS_PER_PASSAGE));
  for (auto& link : links) {
    if (out.size() >= PassageDoc::MAX_LINKS_PER_PASSAGE) break;
    if (link.target == ownStart) continue;
    const auto sameTarget = [&link](const PassageLink& kept) { return kept.target == link.target; };
    if (std::any_of(out.begin(), out.end(), sameTarget)) continue;
    link.label = utf8SafeSummary(std::move(link.label), PassageDoc::MAX_REFERENCE_BYTES);
    out.push_back(std::move(link));
  }
  return out;
}

bool anyPassageHasLinks(const std::vector<TaggedPassage>& passages) {
  return std::any_of(passages.begin(), passages.end(), [](const TaggedPassage& p) { return !p.links.empty(); });
}

}  // namespace

bool PassageDoc::add(TaggedPassage passage) {
  // utf8SafeSummary, never resize(): every one of these strings is Spanish and a
  // raw byte cut can land after a lead byte, producing an invalid sequence that
  // ArduinoJson will then serialise. HighlightDoc::addHighlight uses the same
  // helper for the same reason.
  passage.snippet = utf8SafeSummary(std::move(passage.snippet), MAX_SNIPPET_BYTES);
  passage.reference = utf8SafeSummary(std::move(passage.reference), MAX_REFERENCE_BYTES);
  passage.tags = normaliseTags(passage.tags);
  passage.links = normaliseLinks(std::move(passage.links), passage.start);

  passages_.push_back(std::move(passage));
  if (measureBytes() > SAVE_BYTE_BUDGET) {
    passages_.pop_back();
    return false;
  }
  return true;
}

bool PassageDoc::remove(const size_t index) {
  if (index >= passages_.size()) return false;
  passages_.erase(passages_.begin() + static_cast<long>(index));
  return true;
}

bool PassageDoc::setTags(const size_t index, std::vector<TagId> tags) {
  if (index >= passages_.size()) return false;
  passages_[index].tags = normaliseTags(tags);
  return true;
}

void PassageDoc::removeTagEverywhere(const TagId id) {
  if (id == UNLABELLED) return;
  for (auto& p : passages_) {
    p.tags.erase(std::remove(p.tags.begin(), p.tags.end(), id), p.tags.end());
    if (p.tags.empty()) p.tags.push_back(UNLABELLED);
  }
}

PassageDoc::LinkResult PassageDoc::linkPassages(const size_t sourceIndex, const size_t targetIndex) {
  if (sourceIndex >= passages_.size() || targetIndex >= passages_.size()) return LinkResult::NoSuchPassage;
  auto& source = passages_[sourceIndex];
  const auto& target = passages_[targetIndex];
  if (target.start == source.start) return LinkResult::SelfLink;

  const auto sameTarget = [&target](const PassageLink& link) { return link.target == target.start; };
  if (std::any_of(source.links.begin(), source.links.end(), sameTarget)) return LinkResult::AlreadyLinked;
  if (source.links.size() >= MAX_LINKS_PER_PASSAGE) return LinkResult::AtCap;

  PassageLink link;
  link.target = target.start;
  link.targetSpine = target.documentSpine;
  link.label = utf8SafeSummary(target.reference.empty() ? target.snippet : target.reference, MAX_REFERENCE_BYTES);
  source.links.push_back(std::move(link));

  if (measureBytes() > SAVE_BYTE_BUDGET) {
    source.links.pop_back();
    return LinkResult::OverBudget;
  }
  return LinkResult::Linked;
}

bool PassageDoc::removeLink(const size_t passageIndex, const size_t linkIndex) {
  if (passageIndex >= passages_.size()) return false;
  auto& links = passages_[passageIndex].links;
  if (linkIndex >= links.size()) return false;
  links.erase(links.begin() + static_cast<long>(linkIndex));
  return true;
}

bool PassageDoc::setLinks(const size_t passageIndex, std::vector<PassageLink> links) {
  if (passageIndex >= passages_.size()) return false;
  passages_[passageIndex].links = normaliseLinks(std::move(links), passages_[passageIndex].start);
  return true;
}

void PassageDoc::repairDocumentSpine(const size_t index, const uint16_t spineIndex) {
  if (index < passages_.size()) passages_[index].documentSpine = spineIndex;
}

size_t PassageDoc::unlabelledCount() const {
  size_t n = 0;
  for (const auto& p : passages_) {
    if (p.tags.size() == 1 && p.tags.front() == UNLABELLED) ++n;
  }
  return n;
}

std::vector<size_t> PassageDoc::findByDocument(const std::string& document) const {
  std::vector<size_t> out;
  for (size_t i = 0; i < passages_.size(); ++i) {
    if (passages_[i].document == document) out.push_back(i);
  }
  return out;
}

void PassageDoc::toJson(JsonDocument& doc) const {
  doc["v"] = anyPassageHasLinks(passages_) ? FORMAT_VERSION : LINKLESS_FORMAT_VERSION;
  const auto rows = doc["p"].to<JsonArray>();
  for (const auto& p : passages_) {
    const auto row = rows.add<JsonObject>();
    row["u"] = unitToCompact(p.start);
    row["e"] = unitToCompact(p.end);
    row["f"] = fingerprintToCompact(p.fingerprint);
    if (p.endFingerprint.length != 0) row["fe"] = fingerprintToCompact(p.endFingerprint);
    row["d"] = p.document;
    row["s"] = p.documentSpine;
    row["x"] = p.snippet;
    row["r"] = p.reference;
    if (p.pendingUpgrade) row["g"] = true;
    // UNLABELLED is written as the empty array, which is how every v1 build
    // already stores (and keeps) a passage whose last tag was retired.
    const auto tags = row["t"].to<JsonArray>();
    for (const TagId id : p.tags) {
      if (id != UNLABELLED) tags.add(toRaw(id));
    }
    if (p.links.empty()) continue;
    const auto links = row["k"].to<JsonArray>();
    for (const auto& link : p.links) {
      const auto entry = links.add<JsonObject>();
      entry["u"] = unitToCompact(link.target);
      entry["s"] = link.targetSpine;
      entry["r"] = link.label;
    }
  }
}

bool PassageDoc::fromJson(const JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;
  const int version = doc["v"] | 0;
  if (version <= 0 || version > FORMAT_VERSION) return false;

  passages_.clear();
  for (const JsonVariantConst v : doc["p"].as<JsonArrayConst>()) {
    const auto start = unitFromCompact(v["u"] | "");
    if (!start) continue;  // unaddressable: cannot be painted or listed

    TaggedPassage p;
    p.start = *start;
    p.end = unitFromCompact(v["e"] | "").value_or(*start);
    p.fingerprint = fingerprintFromCompact(v["f"] | "").value_or(Fingerprint{});
    p.endFingerprint = fingerprintFromCompact(v["fe"] | "").value_or(Fingerprint{});
    p.document = v["d"] | "";
    p.documentSpine = static_cast<uint16_t>(v["s"] | 0);
    p.snippet = utf8SafeSummary(v["x"] | "", MAX_SNIPPET_BYTES);
    p.reference = utf8SafeSummary(v["r"] | "", MAX_REFERENCE_BYTES);
    p.pendingUpgrade = v["g"] | false;
    for (const JsonVariantConst t : v["t"].as<JsonArrayConst>()) {
      const uint32_t id = t | 0u;
      if (id <= UINT16_MAX) p.tags.push_back(toTagId(static_cast<uint16_t>(id)));
    }
    p.tags = normaliseTags(p.tags);
    for (const JsonVariantConst k : v["k"].as<JsonArrayConst>()) {
      const auto target = unitFromCompact(k["u"] | "");
      if (!target) continue;
      p.links.push_back(PassageLink{*target, static_cast<uint16_t>(k["s"] | 0), k["r"] | ""});
    }
    p.links = normaliseLinks(std::move(p.links), p.start);

    // Load path: normalise, but NEVER drop for budget. Funnelling this through
    // add() would silently lose a file's tail, still report success, and let the
    // next save make the loss permanent -- the silent-truncation failure the
    // whole budget discipline exists to prevent, one layer up.
    passages_.push_back(std::move(p));
  }
  return measureBytes() <= SAVE_BYTE_BUDGET;
}

size_t PassageDoc::measureBytes() const {
  JsonDocument doc;
  toJson(doc);
  return measureJson(doc);
}

}  // namespace study
