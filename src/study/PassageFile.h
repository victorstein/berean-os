#pragma once

#include <cstdint>
#include <string>

#include "StudyStore/PassageDoc.h"

// Moves bytes between PassageDoc and /.berean/passages/<pubkey>.json.
//
// Reads by streaming the file into ArduinoJson rather than through
// Storage.readFile: that caps at 50,000 bytes and returns a silently truncated
// String, which for this file would mean the user's older passages quietly
// ceasing to exist on the next boot.
//
// Single-writer: the main task owns this. If the web server ever writes
// passages, add a mutex -- PersistableStore.h documents the same hazard.
namespace PassageFile {

std::string path(const std::string& pubKey);

enum class LoadResult : uint8_t {
  Loaded,
  Empty,              // genuinely absent -- safe to save over
  RecoveredFromTemp,  // a failed rename left .tmp as the only copy; promoted
  Failed,             // unreadable, unparseable or over budget -- DATA MAY STILL EXIST
};

// Failed means the bytes could not be read, parsed or validated AND THE FILE MAY
// STILL HOLD THE USER'S DATA. The caller MUST latch saving off for the session.
LoadResult load(const std::string& pubKey, study::PassageDoc& doc);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

// Measures the serialised document and refuses BEFORE touching any file when it
// exceeds the budget. Writes through writeDocToFileAtomic -- never
// writeDocToFile, which is the non-atomic variant.
SaveResult save(const std::string& pubKey, const study::PassageDoc& doc);

}  // namespace PassageFile
