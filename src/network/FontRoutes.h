#pragma once

#include <HalStorage.h>
#include <WebServer.h>

#include <string>
#include <vector>

// The SD-card font manager page and its API: list, upload and delete font families.
class FontRoutes {
 public:
  void registerRoutes(WebServer& server);

 private:
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    FontUploadState() { buffer.resize(BUFFER_SIZE); }
  } fontUpload;

  void handleFontsPage(WebServer& server) const;
  void handleFontList(WebServer& server) const;
  void handleFontUpload(WebServer& server);
  void handleFontUploadData(WebServer& server);
  void handleFontDelete(WebServer& server);
};
