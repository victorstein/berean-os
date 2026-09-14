#include "StudyStore/PassageDoc.h"

#include <Utf8.h>

#include <algorithm>

namespace study {
namespace {

std::vector<TagId> normaliseTags(const std::vector<TagId>& tags) {
  std::vector<TagId> out;
  out.reserve(std::min(tags.size(), PassageDoc::MAX_TAGS_PER_PASSAGE));
  for (const TagId id : tags) {
    if (out.size() >= PassageDoc::MAX_TAGS_PER_PASSAGE) break;
    if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  }
  return out;
}

}  // namespace

bool PassageDoc::add(TaggedPassage passage) {
  if (passage.tags.empty()) return false;
  // utf8SafeSummary, never resize(): every one of these strings is Spanish and a
  // raw byte cut can land after a lead byte, producing an invalid sequence that
  // ArduinoJson will then serialise. HighlightDoc::addHighlight uses the same
  // helper for the same reason.
  passage.snippet = utf8SafeSummary(std::move(passage.snippet), MAX_SNIPPET_BYTES);
  passage.reference = utf8SafeSummary(std::move(passage.reference), MAX_REFERENCE_BYTES);
  passage.tags = normaliseTags(passage.tags);

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
  std::vector<TagId> normalised = normaliseTags(tags);
  if (normalised.empty()) return false;
  passages_[index].tags = std::move(normalised);
  return true;
}

void PassageDoc::removeTagEverywhere(const TagId id) {
  for (auto& p : passages_) {
    p.tags.erase(std::remove(p.tags.begin(), p.tags.end(), id), p.tags.end());
  }
}

void PassageDoc::repairDocumentSpine(const size_t index, const uint16_t spineIndex) {
  if (index < passages_.size()) passages_[index].documentSpine = spineIndex;
}

size_t PassageDoc::untaggedCount() const {
  size_t n = 0;
  for (const auto& p : passages_) {
    if (p.tags.empty()) ++n;
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
  doc["v"] = FORMAT_VERSION;
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
    const auto tags = row["t"].to<JsonArray>();
    for (const TagId id : p.tags) tags.add(toRaw(id));
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
      if (id > 0 && id <= UINT16_MAX) p.tags.push_back(toTagId(static_cast<uint16_t>(id)));
    }
    p.tags = normaliseTags(p.tags);

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
