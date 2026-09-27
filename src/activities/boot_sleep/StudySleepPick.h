#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

// The pure half of the study sleep screen: which passage to show and how to word
// the date. Free of Arduino, the HAL and ArduinoJson so test/study_sleep_pick
// runs it on the host.
namespace study_sleep {

inline constexpr uint32_t FNV_OFFSET_BASIS = 0x811c9dc5u;
inline constexpr uint32_t FNV_PRIME = 0x01000193u;

inline uint32_t fnv1a32(uint32_t hash, const std::string_view bytes) {
  for (const char c : bytes) {
    hash ^= static_cast<uint8_t>(c);
    hash *= FNV_PRIME;
  }
  return hash;
}

// The end unit is part of the identity: PassageDoc::add does not dedupe on the
// start, so two passages in one publication can begin on the same verse.
inline uint32_t passageKey(const std::string_view pubKey, const std::string_view startUnit,
                           const std::string_view endUnit) {
  uint32_t hash = fnv1a32(FNV_OFFSET_BASIS, pubKey);
  hash = fnv1a32(hash, "/");
  hash = fnv1a32(hash, startUnit);
  hash = fnv1a32(hash, "/");
  return fnv1a32(hash, endUnit);
}

// PassageDoc::fromJson reads an absent or unparseable end as the start, so the
// key does too.
inline std::string_view endUnitOrStart(const std::string_view startUnit, const std::string_view endUnit,
                                       const bool endIsValid) {
  return endIsValid ? endUnit : startUnit;
}

// The pubkey is a passage file's stem. `.json` only: an interrupted save's
// `.json.tmp` is left for the next ordinary load to recover.
inline std::optional<std::string_view> pubKeyFromFileName(const std::string_view fileName) {
  constexpr std::string_view suffix = ".json";
  if (fileName.empty() || fileName.front() == '.' || fileName.size() <= suffix.size()) return std::nullopt;
  if (fileName.substr(fileName.size() - suffix.size()) != suffix) return std::nullopt;
  return fileName.substr(0, fileName.size() - suffix.size());
}

}  // namespace study_sleep
