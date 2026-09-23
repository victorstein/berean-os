#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The device's global tag vocabulary. Unlike HighlightDoc's per-book palette,
// an id here means the same tag in every publication, so "show me everything
// tagged misericordia" is answerable.
//
// Ids are allocated once and never reused. Deleting retires an id and leaves a
// tombstone, so a passage that still references it renders its real name rather
// than a blank chip or -- far worse -- some later tag's name.
namespace study {

// A scoped enum, not a bare uint16_t, because the model it replaces used
// POSITIONAL INDICES into a per-book vector and the two are not
// interchangeable. TagPickerActivity's selection today is std::vector<uint16_t>
// of indices; with a bare alias every mis-wiring of index to id would compile
// silently and land the wrong tag on a real passage.
enum class TagId : uint16_t {};

constexpr uint16_t toRaw(const TagId id) { return static_cast<uint16_t>(id); }
constexpr TagId toTagId(const uint16_t raw) { return static_cast<TagId>(raw); }

// "Marked, not yet labelled". Never allocated by a palette, so it cannot collide
// with a user tag, and a passage carries it only when it carries nothing else.
inline constexpr TagId UNLABELLED = toTagId(0);

class TagPalette {
 public:
  static constexpr int FORMAT_VERSION = 1;
  // The user's real palette is 48. This bounds growth; the passage document's
  // byte budget is what actually guarantees the store stays readable.
  static constexpr size_t MAX_ACTIVE_TAGS = 200;
  static constexpr size_t MAX_TAG_NAME_BYTES = 24;

  // Returns the existing id when the name already exists, a fresh id when it
  // does not, and nullopt when the name is empty, too long, or the palette is
  // full. Matching is exact -- byte equality, no case folding and no accent
  // folding, because "transformación" and "transformacion" are different words
  // and the user's palette contains one of them.
  std::optional<TagId> add(const std::string& name);

  // Retires `id`. The tag stops appearing in pickers but keeps resolving for
  // display. A no-op for an unknown id.
  void retire(TagId id);

  bool isActive(TagId id) const;
  // The name for any id ever allocated, active or retired; empty if unknown.
  const std::string& name(TagId id) const;
  size_t activeCount() const;

  // Active tags in allocation order, for a picker.
  std::vector<TagId> activeIds() const;

  void toJson(JsonDocument& doc) const;

  // Parses and validates. Rejects a future format version. Recovers nextTagId
  // as max(seen) + 1 when the field is absent or too low, so a hand-edited file
  // cannot hand out an id a passage already carries.
  bool fromJson(JsonVariantConst doc);

 private:
  struct Entry {
    TagId id{};
    std::string name;
    bool active = true;
  };

  std::vector<Entry> entries_;
  uint16_t nextRaw_ = 1;  // 0 is UNLABELLED
};

}  // namespace study
