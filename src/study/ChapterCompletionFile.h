#pragma once

#include <cstdint>
#include <string>

#include "StudyStore/ChapterCompletion.h"

// Moves bytes between ChapterCompletion and /.berean/completion/<pubkey>.json.
//
// The record is bounded -- every chapter read serialises to under 1 KB -- so
// the ordinary PersistableStore read path is safe here, as it is for the tag
// palette (TagPaletteFile.h).
//
// Single-writer: the main (Arduino loop) task owns this file. Every access goes
// through HalStorage, which holds storageMutex per call; if the web server or a
// background task ever writes it, add a mutex -- PersistableStore.h documents
// the same hazard.
namespace ChapterCompletionFile {

std::string path(const std::string& pubKey);

enum class LoadResult : uint8_t { Loaded, Empty, RecoveredFromTemp, Failed };

// Failed means the file may still hold the user's reading history. The caller
// MUST latch saving off: an empty record saved over it erases that history.
LoadResult load(const std::string& pubKey, study::ChapterCompletion& record);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

SaveResult save(const std::string& pubKey, const study::ChapterCompletion& record);

}  // namespace ChapterCompletionFile
