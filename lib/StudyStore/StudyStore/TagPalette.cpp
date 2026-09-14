#include "StudyStore/TagPalette.h"

#include <algorithm>

namespace study {
namespace {
const std::string kEmpty;
}  // namespace

std::optional<TagId> TagPalette::add(const std::string& name) {
  if (name.empty() || name.size() > MAX_TAG_NAME_BYTES) return std::nullopt;

  for (auto& e : entries_) {
    if (e.name != name) continue;
    e.active = true;  // re-adding a retired name revives it under its own id
    return e.id;
  }

  if (activeCount() >= MAX_ACTIVE_TAGS) return std::nullopt;
  if (nextRaw_ == UINT16_MAX) return std::nullopt;

  const TagId id = toTagId(nextRaw_++);
  entries_.push_back({id, name, true});
  return id;
}

void TagPalette::retire(const TagId id) {
  for (auto& e : entries_) {
    if (e.id == id) e.active = false;
  }
}

bool TagPalette::isActive(const TagId id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return e.active;
  }
  return false;
}

const std::string& TagPalette::name(const TagId id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return e.name;
  }
  return kEmpty;
}

size_t TagPalette::activeCount() const {
  size_t n = 0;
  for (const auto& e : entries_) {
    if (e.active) ++n;
  }
  return n;
}

std::vector<TagId> TagPalette::activeIds() const {
  std::vector<TagId> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_) {
    if (e.active) out.push_back(e.id);
  }
  return out;
}

void TagPalette::toJson(JsonDocument& doc) const {
  doc["v"] = FORMAT_VERSION;
  doc["n"] = nextRaw_;
  const auto tags = doc["t"].to<JsonArray>();
  for (const auto& e : entries_) {
    const auto row = tags.add<JsonObject>();
    row["i"] = toRaw(e.id);
    row["n"] = e.name;
    if (!e.active) row["r"] = true;
  }
}

bool TagPalette::fromJson(const JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;
  const int version = doc["v"] | 0;
  if (version <= 0 || version > FORMAT_VERSION) return false;

  entries_.clear();
  uint16_t highest = 0;

  for (const JsonVariantConst v : doc["t"].as<JsonArrayConst>()) {
    const uint32_t id = v["i"] | 0u;
    const char* name = v["n"] | "";
    if (id == 0 || id > UINT16_MAX || name[0] == '\0') continue;

    std::string text(name);
    if (text.size() > MAX_TAG_NAME_BYTES) text.resize(MAX_TAG_NAME_BYTES);

    const TagId tagId = toTagId(static_cast<uint16_t>(id));
    bool duplicate = false;
    for (const auto& e : entries_) duplicate = duplicate || e.id == tagId;
    if (duplicate) continue;

    entries_.push_back({tagId, std::move(text), !(v["r"] | false)});
    highest = std::max(highest, static_cast<uint16_t>(id));
  }

  const uint32_t stored = doc["n"] | 0u;
  nextRaw_ = static_cast<uint16_t>(std::max<uint32_t>(stored, highest + 1u));
  return true;
}

}  // namespace study
