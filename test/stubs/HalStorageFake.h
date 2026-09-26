#pragma once

// Test controls for the in-memory Storage fake (HalStorageFake.cpp). Kept out of
// HalStorage.h so that header stays a mirror of lib/hal/HalStorage.h.
//
// The fake is one process-wide card, like Storage on the device: call reset() in
// every fixture's SetUp(). Failure hooks are sticky until clearFailures() or
// reset().

#include <optional>
#include <string>

namespace storage_fake {

// Empty card: only "/" exists, and no failure hooks.
void reset();

// Drops every failure hook; files and directories are kept.
void clearFailures();

// Seeds a file, creating its parent directories. Bypasses the failure hooks.
void putFile(const std::string& path, std::string bytes);

std::optional<std::string> fileBytes(const std::string& path);
bool isDir(const std::string& path);

// exists() stays true; readFile returns "" and openFileForRead fails -- the
// case PersistableStore.cpp calls indistinguishable from an empty file.
void failReadsOf(const std::string& path);

// rename() away from this path fails and changes nothing.
void failRenamesFrom(const std::string& path);

// writeFile removes an existing file at this path, then fails, as
// SDCardManager::writeFile does when the open fails after its remove.
// openFileForWrite fails.
void failWritesTo(const std::string& path);

}  // namespace storage_fake
