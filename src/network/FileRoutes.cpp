#include "FileRoutes.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <ProtectedPath.h>

#include <cinttypes>
#include <string_view>

#include "WebRouteUtils.h"
#include "html/FilesPageHtml.generated.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
String normalizeWebPath(const String& inputPath) {
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

bool isProtectedWebName(const String& name) {
  return protectedpath::isProtectedName(std::string_view(name.c_str(), name.length()));
}
}  // namespace

void FileRoutes::registerRoutes(WebServer& server) {
  server.on("/files", HTTP_GET, [this, &server] { handleFileList(server); });
  server.on("/api/files", HTTP_GET, [this, &server] { handleFileListData(server); });
  server.on("/download", HTTP_GET, [this, &server] { handleDownload(server); });

  // Upload endpoint with special handling for multipart form data
  server.on(
      "/upload", HTTP_POST, [this, &server] { handleUploadPost(server, upload); },
      [this, &server] { handleUpload(server, upload); });

  server.on("/mkdir", HTTP_POST, [this, &server] { handleCreateFolder(server); });
  server.on("/rename", HTTP_POST, [this, &server] { handleRename(server); });
  server.on("/move", HTTP_POST, [this, &server] { handleMove(server); });
  server.on("/delete", HTTP_POST, [this, &server] { handleDelete(server); });
}

void FileRoutes::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Protected entries stay hidden even with Show hidden files on: every route
    // refuses them, and listing /.berean invites deleting study data.
    if (!isProtectedWebName(fileName)) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();                          // Yield to allow WiFi and other tasks to process during long scans
    resetTaskWatchdogIfSubscribed();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool FileRoutes::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void FileRoutes::handleFileList(WebServer& server) const {
  webroutes::sendHtmlContent(server, FilesPageHtml, sizeof(FilesPageHtml));
}

void FileRoutes::handleFileListData(WebServer& server) const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server.hasArg("path")) {
    currentPath = server.arg("path");
    // Ensure path starts with /
    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }
    // Remove trailing slash unless it's root
    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
  }

  if (webroutes::isProtectedWebPath(currentPath)) {
    server.send(403, "text/plain", "Cannot access protected items");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &server, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server.sendContent(",");
    } else {
      seenFirst = true;
    }
    server.sendContent(output);
  });
  server.sendContent("]");
  // End of streamed response, empty chunk to signal client
  server.sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void FileRoutes::handleDownload(WebServer& server) const {
  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server.arg("path");
  if (itemPath.isEmpty() || itemPath == "/") {
    server.send(400, "text/plain", "Invalid path");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  if (webroutes::isProtectedWebPath(itemPath)) {
    server.send(403, "text/plain", "Cannot access protected items");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server.send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server.send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server.setContentLength(file.size());
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.send(200, contentType.c_str(), "");

  NetworkClient client = server.client();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];

  bool downloadOk = true;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
    }
  }
  client.clear();
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(FileRoutes::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    resetTaskWatchdogIfSubscribed();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetTaskWatchdogIfSubscribed();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

void FileRoutes::handleUpload(WebServer& server, UploadState& state) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  resetTaskWatchdogIfSubscribed();

  // Safety check: ensure server is still valid
  if (!serverRunning) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    resetTaskWatchdogIfSubscribed();

    state.fileName = upload.filename;
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server.hasArg("path")) {
      state.path = server.arg("path");
      // Ensure path starts with /
      if (!state.path.startsWith("/")) {
        state.path = "/" + state.path;
      }
      // Remove trailing slash unless it's root
      if (state.path.length() > 1 && state.path.endsWith("/")) {
        state.path = state.path.substring(0, state.path.length() - 1);
      }
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %" PRIu32 " bytes", ESP.getFreeHeap());

    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    if (webroutes::isProtectedWebPath(filePath)) {
      state.error = "Cannot write to a protected path";
      LOG_ERR("WEB", "[UPLOAD] Refused protected path: %s", filePath.c_str());
      return;
    }

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    if (Storage.exists(filePath.c_str())) {
      state.error = "File already exists: " + state.fileName;
      LOG_DBG("WEB", "[UPLOAD] Collision: %s", filePath.c_str());
      return;
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache after uploading the file
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        clearBookCache(filePath.c_str());
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      Storage.remove(filePath.c_str());
    }
    state.error = "Upload aborted";
    LOG_DBG("WEB", "Upload aborted");
  }
}

void FileRoutes::handleUploadPost(WebServer& server, UploadState& state) const {
  if (state.success) {
    server.send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server.send(400, "text/plain", error);
  }
}

void FileRoutes::handleCreateFolder(WebServer& server) const {
  // Get folder name from form data
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server.arg("name");

  // Validate folder name
  if (folderName.isEmpty()) {
    server.send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server.hasArg("path")) {
    parentPath = server.arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  if (webroutes::isProtectedWebPath(folderPath)) {
    server.send(403, "text/plain", "Cannot create a protected folder");
    return;
  }

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server.send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server.send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server.send(500, "text/plain", "Failed to create folder");
  }
}

void FileRoutes::handleRename(WebServer& server) const {
  if (!server.hasArg("path") || !server.hasArg("name")) {
    server.send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server.arg("path"));
  String newName = server.arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server.send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server.send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server.send(400, "text/plain", "Invalid file name");
    return;
  }
  if (isProtectedWebName(newName)) {
    server.send(403, "text/plain", "Cannot rename to protected name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (webroutes::isProtectedWebPath(itemPath)) {
    server.send(403, "text/plain", "Cannot rename protected item");
    return;
  }
  if (newName == itemName) {
    server.send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server.send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server.send(400, "text/plain", "Only files can be renamed");
    return;
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server.send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server.send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server.send(500, "text/plain", "Failed to rename file");
  }
}

void FileRoutes::handleMove(WebServer& server) const {
  if (!server.hasArg("path") || !server.hasArg("dest")) {
    server.send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server.arg("path"));
  String destPath = normalizeWebPath(server.arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server.send(400, "text/plain", "Invalid path");
    return;
  }
  if (destPath.isEmpty()) {
    server.send(400, "text/plain", "Invalid destination");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (webroutes::isProtectedWebPath(itemPath)) {
    server.send(403, "text/plain", "Cannot move protected item");
    return;
  }
  if (webroutes::isProtectedWebPath(destPath)) {
    server.send(403, "text/plain", "Cannot move into protected folder");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server.send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server.send(400, "text/plain", "Only files can be moved");
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server.send(404, "text/plain", "Destination not found");
    return;
  }
  HalFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server.send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server.send(200, "text/plain", "Already in destination");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server.send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server.send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server.send(500, "text/plain", "Failed to move file");
  }
}

void FileRoutes::handleDelete(WebServer& server) const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server.hasArg("path");
  bool hasPathsArg = server.hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server.send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server.send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server.arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server.arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server.send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server.send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = p.as<String>();

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Ensure path starts with /
    if (!itemPath.startsWith("/")) {
      itemPath = "/" + itemPath;
    }

    if (webroutes::isProtectedWebPath(itemPath)) {
      failedItems += itemPath + " (protected file); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      HalFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (folder not empty); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) — remove file
      if (f) f.close();
      success = Storage.remove(itemPath.c_str());
      clearBookCache(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server.send(200, "text/plain", "All items deleted successfully");
  } else {
    server.send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}
