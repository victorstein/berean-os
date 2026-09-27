#pragma once

#include <HalStorage.h>
#include <WebServer.h>

#include <functional>
#include <vector>

struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

// The file manager page and its API: list, download, upload, mkdir, rename,
// move and delete. Every route refuses protected paths.
class FileRoutes {
 public:
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    UploadState() { buffer.resize(UPLOAD_BUFFER_SIZE); }
  };

  // `serverRunning` is the owning server's running flag; handleUpload refuses to start while it is false.
  explicit FileRoutes(const bool& serverRunning) : serverRunning(serverRunning) {}

  void registerRoutes(WebServer& server);

 private:
  UploadState upload;
  const bool& serverRunning;

  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  bool isEpubFile(const String& filename) const;

  void handleFileList(WebServer& server) const;
  void handleFileListData(WebServer& server) const;
  void handleDownload(WebServer& server) const;
  void handleUpload(WebServer& server, UploadState& state) const;
  void handleUploadPost(WebServer& server, UploadState& state) const;
  void handleCreateFolder(WebServer& server) const;
  void handleRename(WebServer& server) const;
  void handleMove(WebServer& server) const;
  void handleDelete(WebServer& server) const;
};
