#pragma once

#include <StudyStore/Unit.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "util/CivilDate.h"

// The Home verse cache's pure half (issue #203): what it holds, when it still
// answers, and how fitted lines are packed into it. Free of the renderer and
// storage so test/home_layout checks it; src/activities/launcher/HomeVerse is the
// shell.
namespace home_verse {

// Four Ubuntu 10 lines across the card hold about 180 bytes, so this rejects
// only rows the card could never show, before any of them is measured.
inline constexpr size_t PREFILTER_BYTES = 512;
inline constexpr uint8_t MAX_LINES = 4;
// The lines of a fitted passage, each NUL-terminated: never more than the text.
inline constexpr size_t LINE_BUFFER_BYTES = PREFILTER_BYTES + MAX_LINES;
// study_sleep::REFERENCE_CAPACITY; HomeVerse.cpp static_asserts the match.
inline constexpr size_t REFERENCE_BYTES = 49;
// The seed when the clock has no date, so the undated pick is stable too.
inline constexpr uint32_t FALLBACK_SEED = 0x48564653u;

enum class Empty : uint8_t { None, NoPassages, TooLong };

struct Key {
  bool dated = false;
  CivilDate day{};
  bool fileExists = false;
  uint32_t fileBytes = 0;
};

constexpr bool sameKey(const Key& a, const Key& b) {
  if (a.dated != b.dated || a.fileExists != b.fileExists || a.fileBytes != b.fileBytes) return false;
  return !a.dated || (a.day.year == b.day.year && a.day.month == b.day.month && a.day.day == b.day.day);
}

struct Pick {
  study::Unit start{};
  uint16_t spine = 0;
  char reference[REFERENCE_BYTES] = {};
  uint8_t rung = 0;
  uint8_t lineCount = 0;
  uint16_t lineStart[MAX_LINES] = {};
  char lines[LINE_BUFFER_BYTES] = {};

  const char* line(const uint8_t index) const { return lines + lineStart[index]; }
};

struct Entry {
  bool valid = false;
  Key key{};
  bool hasPick = false;
  Empty empty = Empty::NoPassages;
  Pick pick{};
};

constexpr bool validFor(const Entry& entry, const Key& key) { return entry.valid && sameKey(entry.key, key); }

// Why a completed scan picked nothing: rows that were whole but too long for the
// card say so, rather than asking a user who has tags to go and tag something.
constexpr Empty emptyReason(const uint32_t rowsUnfit, const uint32_t rowsOverPrefilter) {
  return rowsUnfit + rowsOverPrefilter > 0 ? Empty::TooLong : Empty::NoPassages;
}

// False, leaving `pick` unchanged, for no lines, more than MAX_LINES, or more
// text than the buffer holds.
inline bool packLines(const std::vector<std::string>& fitted, Pick& pick) {
  if (fitted.empty() || fitted.size() > MAX_LINES) return false;
  size_t needed = 0;
  for (const std::string& text : fitted) needed += text.size() + 1;
  if (needed > LINE_BUFFER_BYTES) return false;

  size_t at = 0;
  for (size_t i = 0; i < fitted.size(); ++i) {
    pick.lineStart[i] = static_cast<uint16_t>(at);
    memcpy(pick.lines + at, fitted[i].data(), fitted[i].size());
    at += fitted[i].size();
    pick.lines[at++] = '\0';
  }
  pick.lineCount = static_cast<uint8_t>(fitted.size());
  return true;
}

}  // namespace home_verse

static_assert(sizeof(home_verse::Entry) <= 640, "the Home verse cache is static internal SRAM; keep it small");
