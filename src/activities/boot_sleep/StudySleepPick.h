#pragma once

#include <Catalog/CatalogLabel.h>

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

// PassageDoc::MAX_SNIPPET_BYTES / MAX_REFERENCE_BYTES plus a terminator.
// StudySleepScreen.cpp static_asserts the match; including PassageDoc.h here
// would pull ArduinoJson into the host test.
inline constexpr size_t SNIPPET_CAPACITY = 121;
inline constexpr size_t REFERENCE_CAPACITY = 49;

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

struct RingView {
  const uint32_t* keys;
  uint8_t capacity;
  uint8_t pos;  // the slot the next push writes
  uint8_t fill;
};

// 0 is the most recent showing. Walks newest-first, so a key held twice reports
// its newer slot.
inline std::optional<uint8_t> ageOf(const RingView& ring, const uint32_t key) {
  const uint8_t held = ring.fill < ring.capacity ? ring.fill : ring.capacity;
  for (uint8_t age = 0; age < held; ++age) {
    const auto slot = static_cast<uint8_t>((ring.pos + ring.capacity - 1 - age) % ring.capacity);
    if (ring.keys[slot] == key) return age;
  }
  return std::nullopt;
}

// Returns a value in [0, bound); bound is never 0.
using RandomFn = uint32_t (*)(void* ctx, uint32_t bound);

struct Candidate {
  char snippet[SNIPPET_CAPACITY] = {};
  char reference[REFERENCE_CAPACITY] = {};
  uint16_t tag = 0;
  uint32_t key = 0;
  uint8_t age = 0;
};

// One pass over every passage. Passages not shown recently are reservoir-sampled:
// the k-th replaces the pick with probability 1/k. Recent ones count only when
// nothing else exists, and then the least recently shown wins, the first seen on
// a tie.
class Sampler {
 public:
  Sampler(const RandomFn random, void* const randomCtx) : random_(random), randomCtx_(randomCtx) {}

  void offer(const std::string_view snippet, const std::string_view reference, const uint16_t tag, const uint32_t key,
             const std::optional<uint8_t> age) {
    if (!age) {
      ++freshSeen_;
      if (random_(randomCtx_, freshSeen_) == 0) fill(fresh_, snippet, reference, tag, key, 0);
      return;
    }
    if (!hasStale_ || *age > stale_.age) {
      fill(stale_, snippet, reference, tag, key, *age);
      hasStale_ = true;
    }
  }

  const Candidate* result() const {
    if (freshSeen_ > 0) return &fresh_;
    return hasStale_ ? &stale_ : nullptr;
  }

 private:
  static void copyInto(char* out, const size_t capacity, const std::string_view text) {
    const size_t length = text.size() < capacity - 1 ? text.size() : capacity - 1;
    memcpy(out, text.data(), length);
    out[length] = '\0';
  }

  static void fill(Candidate& slot, const std::string_view snippet, const std::string_view reference,
                   const uint16_t tag, const uint32_t key, const uint8_t age) {
    copyInto(slot.snippet, sizeof(slot.snippet), snippet);
    copyInto(slot.reference, sizeof(slot.reference), reference);
    slot.tag = tag;
    slot.key = key;
    slot.age = age;
  }

  RandomFn random_;
  void* randomCtx_;
  Candidate fresh_;
  Candidate stale_;
  uint32_t freshSeen_ = 0;
  bool hasStale_ = false;
};

// The scan starts at a random file and wraps: sweep 0 covers [start, count),
// sweep 1 covers [0, start). A scan the byte budget ends early therefore covers
// a random window, not the same leading files on every sleep.
inline bool inSweep(const uint8_t sweep, const uint32_t index, const uint32_t start) {
  return sweep == 0 ? index >= start : index < start;
}

struct CivilDate {
  int32_t year;
  uint8_t month;
  uint8_t day;
};

// Howard Hinnant's days_from_civil, as src/network/WolWeekScan.cpp uses; that
// copy is file-local to the network surface.
inline int64_t daysFromCivil(int32_t year, const unsigned month, const unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const auto yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline CivilDate civilFromDays(int64_t days) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const auto doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  const int64_t year = static_cast<int64_t>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
  return {static_cast<int32_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)};
}

// 0 = Sunday, the order of the weekday word list and Rtc::DateTime::weekday.
inline uint8_t weekdayFromDays(const int64_t days) {
  return static_cast<uint8_t>(days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6);
}

struct ClockReading {
  bool dateValid = false;  // HalClock::getDate succeeded, so the RTC has been set
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  bool timeValid = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
};

// The RTC keeps UTC and HalClock::getDate reports only the UTC date, so the
// viewer's day comes from shifting it by the local time of day.
inline bool formatDateLine(const ClockReading& clock, uint8_t utcOffsetQuarterHoursBiased,
                           const std::string_view weekdays, const std::string_view monthsShort, char* out,
                           const size_t outSize) {
  if (!clock.dateValid || !clock.timeValid || out == nullptr || outSize == 0) return false;
  if (clock.month < 1 || clock.month > 12 || clock.day < 1 || clock.day > 31 || clock.hour > 23 || clock.minute > 59) {
    return false;
  }
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;

  const int localMinutes = clock.hour * 60 + clock.minute + (static_cast<int>(utcOffsetQuarterHoursBiased) - 48) * 15;
  const int dayShift = localMinutes < 0 ? -1 : (localMinutes >= 1440 ? 1 : 0);
  const int64_t days = daysFromCivil(clock.year, clock.month, clock.day) + dayShift;
  const CivilDate local = civilFromDays(days);

  const std::string_view weekday = catalog::wordAt(weekdays, weekdayFromDays(days));
  const std::string_view month = catalog::wordAt(monthsShort, local.month - 1);
  if (weekday.empty() || month.empty()) return false;

  const int written = snprintf(out, outSize, "%.*s %u %.*s", static_cast<int>(weekday.size()), weekday.data(),
                               static_cast<unsigned>(local.day), static_cast<int>(month.size()), month.data());
  return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace study_sleep
