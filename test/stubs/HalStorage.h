#pragma once

// Host-test stand-in for lib/hal/HalStorage.h, which pulls in <Print.h>,
// <freertos/semphr.h> and <common/FsApiConstants.h>. See HalDisplay.h in this
// directory for why a stub on the include path is sufficient.
//
// The declarations mirror the real header's signatures, minus three things the
// host cannot or need not have: HalFile's Print base (so the two write
// overloads lose `override`), readFileToStream (takes a Print&), and
// StorageLock. HalStorageFake.cpp defines the subset the host suites use; a
// declared method it does not define fails the LINK the first time a suite calls
// it, rather than silently doing nothing. Test controls live in HalStorageFake.h.

#include <Arduino.h>
#include <fcntl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using oflag_t = int;

class HalFile;

class HalStorage {
 public:
  HalStorage();
  bool begin();
  bool ready() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  String readFile(const char* path);
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  bool writeFile(const char* path, const String& content);
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

 private:
  static HalStorage instance;
};

#define Storage HalStorage::getInstance()

class HalFile {
  friend class HalStorage;
  class Impl;
  std::unique_ptr<Impl> impl;
  explicit HalFile(std::unique_ptr<Impl> impl);

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();
  size_t write(const uint8_t* buf, size_t count);
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b);
  bool rename(const char* newPath);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;
};
