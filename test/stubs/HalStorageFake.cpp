// In-memory Storage for host tests: a map from path to bytes that keeps the SD
// card's observable rules, because the code under test depends on them.
//
//   - readFile returns at most 50,000 bytes and truncates silently
//     (SDCardManager.cpp:202). "" means absent, empty, or unreadable alike.
//   - writeFile removes an existing destination before writing
//     (SDCardManager.cpp:282-284), so a failed write leaves nothing behind.
//   - rename and mkdir refuse an existing target: SdFat opens the new entry with
//     O_CREAT | O_EXCL (FatFile.cpp:379,974; ExFatFileWrite.cpp:199,312).
//   - Creating a file needs its parent directory to exist.
//
// Single-threaded and lock-free: storageMutex is not modelled.

#include "HalStorageFake.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace {

constexpr size_t READ_FILE_CAP = 50000;

struct FakeCard {
  std::map<std::string, std::string> files;
  std::set<std::string> dirs{"/"};
  std::set<std::string> failRead;
  std::set<std::string> failRename;
  std::set<std::string> failWrite;
};

FakeCard& card() {
  static FakeCard instance;
  return instance;
}

std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

bool isFile(const std::string& path) { return card().files.count(path) != 0; }
bool isDirectoryPath(const std::string& path) { return card().dirs.count(path) != 0; }
bool pathExists(const std::string& path) { return isFile(path) || isDirectoryPath(path); }
bool parentExists(const std::string& path) { return isDirectoryPath(parentOf(path)); }

void createDirectoryChain(const std::string& path) {
  if (path.empty() || isDirectoryPath(path)) return;
  createDirectoryChain(parentOf(path));
  card().dirs.insert(path);
}

}  // namespace

namespace storage_fake {

void reset() { card() = FakeCard{}; }

void clearFailures() {
  card().failRead.clear();
  card().failRename.clear();
  card().failWrite.clear();
}

void putFile(const std::string& path, std::string bytes) {
  createDirectoryChain(parentOf(path));
  card().files[path] = std::move(bytes);
}

std::optional<std::string> fileBytes(const std::string& path) {
  const auto it = card().files.find(path);
  if (it == card().files.end()) return std::nullopt;
  return it->second;
}

bool isDir(const std::string& path) { return isDirectoryPath(path); }

void failReadsOf(const std::string& path) { card().failRead.insert(path); }
void failRenamesFrom(const std::string& path) { card().failRename.insert(path); }
void failWritesTo(const std::string& path) { card().failWrite.insert(path); }

}  // namespace storage_fake

HalStorage HalStorage::instance;

HalStorage::HalStorage() = default;

bool HalStorage::exists(const char* path) { return pathExists(path); }

bool HalStorage::remove(const char* path) { return card().files.erase(path) != 0; }

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  if (card().failRename.count(oldPath) != 0) return false;
  if (!isFile(oldPath) || pathExists(newPath) || !parentExists(newPath)) return false;
  auto node = card().files.extract(oldPath);
  node.key() = newPath;
  card().files.insert(std::move(node));
  return true;
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  const std::string dir = path;
  if (pathExists(dir)) return false;
  if (!parentExists(dir)) {
    if (!pFlag) return false;
    createDirectoryChain(parentOf(dir));
  }
  card().dirs.insert(dir);
  return true;
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  if (isDirectoryPath(path)) return true;
  return mkdir(path, true);
}

String HalStorage::readFile(const char* path) {
  if (card().failRead.count(path) != 0) return String("");
  const auto it = card().files.find(path);
  if (it == card().files.end()) return String("");
  return String(it->second.substr(0, READ_FILE_CAP));
}

bool HalStorage::writeFile(const char* path, const String& content) {
  card().files.erase(path);
  if (card().failWrite.count(path) != 0 || !parentExists(path)) return false;
  card().files[path] = std::string(content.c_str(), content.length());
  return true;
}

class HalFile::Impl {
 public:
  Impl(std::string path, bool writable) : path(std::move(path)), writable(writable) {}

  std::string* bytes() {
    const auto it = card().files.find(path);
    return it == card().files.end() ? nullptr : &it->second;
  }

  std::string path;
  size_t pos = 0;
  bool writable;
};

bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  if (card().failRead.count(path) != 0 || !isFile(path)) return false;
  file = HalFile(std::make_unique<HalFile::Impl>(path, false));
  return true;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  if (card().failWrite.count(path) != 0 || !parentExists(path) || isDirectoryPath(path)) return false;
  card().files[path].clear();
  file = HalFile(std::make_unique<HalFile::Impl>(path, true));
  return true;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

void HalFile::flush() {}

size_t HalFile::size() {
  if (!impl) return 0;
  const std::string* bytes = impl->bytes();
  return bytes ? bytes->size() : 0;
}

size_t HalFile::fileSize() { return size(); }

bool HalFile::seek(size_t pos) {
  if (!impl || pos > size()) return false;
  impl->pos = pos;
  return true;
}

int HalFile::available() const {
  if (!impl) return 0;
  const std::string* bytes = impl->bytes();
  if (!bytes || impl->pos >= bytes->size()) return 0;
  return static_cast<int>(bytes->size() - impl->pos);
}

size_t HalFile::position() const { return impl ? impl->pos : 0; }

int HalFile::read(void* buf, size_t count) {
  if (!impl) return -1;
  const std::string* bytes = impl->bytes();
  if (!bytes) return -1;
  const size_t got = impl->pos >= bytes->size() ? 0 : std::min(count, bytes->size() - impl->pos);
  std::memcpy(buf, bytes->data() + impl->pos, got);
  impl->pos += got;
  return static_cast<int>(got);
}

int HalFile::read() {
  uint8_t byte = 0;
  return read(&byte, 1) == 1 ? byte : -1;
}

size_t HalFile::write(const uint8_t* buf, size_t count) {
  if (!impl || !impl->writable) return 0;
  std::string* bytes = impl->bytes();
  if (!bytes) return 0;
  if (bytes->size() < impl->pos + count) bytes->resize(impl->pos + count);
  std::memcpy(bytes->data() + impl->pos, buf, count);
  impl->pos += count;
  return count;
}

size_t HalFile::write(const void* buf, size_t count) { return write(static_cast<const uint8_t*>(buf), count); }

size_t HalFile::write(uint8_t b) { return write(&b, 1); }

bool HalFile::close() {
  impl.reset();
  return true;
}

bool HalFile::isOpen() const { return impl != nullptr; }

HalFile::operator bool() const { return isOpen(); }
