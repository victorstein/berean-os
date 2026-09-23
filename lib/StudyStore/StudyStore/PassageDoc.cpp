#include "StudyStore/PassageDoc.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>

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

// A Verse unit names one place in the whole publication. A Paragraph or
// DocumentOffset unit is unique only within its document -- two Watchtower
// articles both have a pid 5 -- so for those the spine is part of the identity.
bool samePlace(const Unit& a, const uint16_t spineA, const Unit& b, const uint16_t spineB) {
  if (!(a == b)) return false;
  return a.kind == UnitKind::Verse || spineA == spineB;
}

bool linksTo(const PassageLink& link, const Unit& unit, const uint16_t spine) {
  return samePlace(link.target, link.targetSpine, unit, spine);
}

// Drops links to the passage's own start and repeats, caps the count, and bounds
// each label. The in-memory path only: a loaded file is validated, never
// normalised, by linksFromJson.
std::vector<PassageLink> normaliseLinks(std::vector<PassageLink> links, const Unit& ownStart, const uint16_t ownSpine) {
  std::vector<PassageLink> out;
  out.reserve(std::min(links.size(), PassageDoc::MAX_LINKS_PER_PASSAGE));
  for (auto& link : links) {
    if (out.size() >= PassageDoc::MAX_LINKS_PER_PASSAGE) break;
    if (linksTo(link, ownStart, ownSpine)) continue;
    const auto sameTarget = [&link](const PassageLink& kept) { return linksTo(kept, link.target, link.targetSpine); };
    if (std::any_of(out.begin(), out.end(), sameTarget)) continue;
    link.label = utf8SafeSummary(std::move(link.label), PassageDoc::MAX_REFERENCE_BYTES);
    out.push_back(std::move(link));
  }
  return out;
}

// Refuses, rather than repairs, anything this build would not have written:
// repairing would drop or cut a link, and the next save would make that loss
// permanent. A refused file becomes a load failure, which latches saving off.
bool linksFromJson(const JsonVariantConst row, TaggedPassage& passage) {
  const JsonVariantConst stored = row["k"];
  if (stored.isNull()) return true;
  if (!stored.is<JsonArrayConst>()) return false;

  const auto entries = stored.as<JsonArrayConst>();
  if (entries.size() > PassageDoc::MAX_LINKS_PER_PASSAGE) return false;
  passage.links.reserve(entries.size());
  for (const JsonVariantConst entry : entries) {
    const auto target = unitFromCompact(entry["u"] | "");
    if (!target) return false;
    const uint32_t spine = entry["s"] | UINT32_MAX;
    if (spine > UINT16_MAX) return false;
    const JsonVariantConst label = entry["r"];
    if (!label.isNull() && !label.is<const char*>()) return false;
    const char* labelText = label | "";
    if (strlen(labelText) > PassageDoc::MAX_REFERENCE_BYTES) return false;

    PassageLink link{*target, static_cast<uint16_t>(spine), labelText};
    if (linksTo(link, passage.start, passage.documentSpine)) return false;
    const auto sameTarget = [&link](const PassageLink& kept) { return linksTo(kept, link.target, link.targetSpine); };
    if (std::any_of(passage.links.begin(), passage.links.end(), sameTarget)) return false;
    passage.links.push_back(std::move(link));
  }
  return true;
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
  passage.links = normaliseLinks(std::move(passage.links), passage.start, passage.documentSpine);

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
  if (samePlace(target.start, target.documentSpine, source.start, source.documentSpine)) return LinkResult::SelfLink;

  const auto sameTarget = [&target](const PassageLink& link) {
    return linksTo(link, target.start, target.documentSpine);
  };
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
  const auto& passage = passages_[passageIndex];
  passages_[passageIndex].links = normaliseLinks(std::move(links), passage.start, passage.documentSpine);
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
    if (!linksFromJson(v, p)) {
      passages_.clear();
      return false;
    }

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
