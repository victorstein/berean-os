#include "StudyStore/TagPalette.h"

#include <BibleSearch/Fold.h>
#include <FormatVersion.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace study {
namespace {

const std::string kEmpty;

// A fold is never longer than its input, and a name is at most MAX_TAG_NAME_BYTES.
constexpr size_t KEY_BYTES = TagPalette::MAX_TAG_NAME_BYTES;

struct SortKey {
  char key[KEY_BYTES];  // folded name, zero-padded
  uint16_t index;       // into entries_
};

// The fold passes ¿ (C2 BF) and ¡ (C2 A1) through unchanged, so a name opening with one would sort
// after z.
std::string_view withoutLeadingInvertedMark(std::string_view name) {
  if (name.size() >= 2 && static_cast<unsigned char>(name[0]) == 0xC2) {
    const auto second = static_cast<unsigned char>(name[1]);
    if (second == 0xBF || second == 0xA1) name.remove_prefix(2);
  }
  return name;
}

void foldKey(const std::string& name, std::string& scratch, char (&key)[KEY_BYTES]) {
  scratch.clear();
  BibleSearch::foldAppend(scratch, withoutLeadingInvertedMark(name));
  memcpy(key, scratch.data(), std::min(scratch.size(), KEY_BYTES));
}

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
  rebuildDisplayOrder();
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

std::vector<TagId> TagPalette::activeIdsByName() const {
  std::vector<TagId> out;
  out.reserve(activeCount());
  for (const uint16_t index : byName_) {
    if (entries_[index].active) out.push_back(entries_[index].id);
  }
  return out;
}

uint16_t TagPalette::nameRank(const TagId id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return e.rank;
  }
  return UINT16_MAX;
}

void TagPalette::rebuildDisplayOrder() {
  const size_t count = entries_.size();
  byName_.resize(count);
  if (count == 0) return;

  const auto keys = makeUniqueNoThrow<SortKey[]>(count);
  if (!keys) {
    LOG_ERR("TAGS", "OOM: %zu B for the tag display order; showing allocation order", count * sizeof(SortKey));
    for (size_t i = 0; i < count; ++i) {
      entries_[i].rank = static_cast<uint16_t>(i);
      byName_[i] = static_cast<uint16_t>(i);
    }
    return;
  }

  std::string scratch;
  scratch.reserve(KEY_BYTES);
  for (size_t i = 0; i < count; ++i) {
    keys[i].index = static_cast<uint16_t>(i);
    foldKey(entries_[i].name, scratch, keys[i].key);
  }

  std::sort(keys.get(), keys.get() + count, [this](const SortKey& a, const SortKey& b) {
    if (const int byKey = memcmp(a.key, b.key, KEY_BYTES)) return byKey < 0;
    const Entry& left = entries_[a.index];
    const Entry& right = entries_[b.index];
    if (left.name != right.name) return left.name < right.name;
    return toRaw(left.id) < toRaw(right.id);
  });

  for (size_t position = 0; position < count; ++position) {
    byName_[position] = keys[position].index;
    entries_[keys[position].index].rank = static_cast<uint16_t>(position);
  }
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
  if (!persist::isKnownFormatVersion(version, FORMAT_VERSION)) return false;

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
  rebuildDisplayOrder();
  return true;
}

}  // namespace study
