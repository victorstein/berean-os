#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "MenuCloseRestore.h"

// The last page renderContents drew, kept so a cancelled reader-menu close can show it again without
// rendering it: the B/W frame and, with anti-aliasing, the two gray masks the display was given. Each
// plane is the framebuffer's size, in PSRAM, allocated on first use and kept for the reader's life.
// Render task only, plus release() from onExit under RenderLock.
class PageFrameCache {
 public:
  void invalidate();
  // Starts a new page from the B/W framebuffer.
  void captureBw(const uint8_t* frame, size_t bytes, int spineIndex, int pageIndex, bool grayExpected);
  // One gray mask, laid out like the framebuffer. Ignored unless captureBw saved this page.
  void captureGrayPlane(bool lsbPlane, const uint8_t* plane, size_t bytes);
  void noteGrayShown();
  void release();

  const MenuCloseRestore::CapturedPage& captured() const { return record; }
  const uint8_t* bw() const { return bwBuffer.get(); }
  const uint8_t* grayLsb() const { return lsbBuffer.get(); }
  const uint8_t* grayMsb() const { return msbBuffer.get(); }
  // Time spent copying for the current page.
  unsigned long copyMillis() const { return copyMs; }

 private:
  // heap_caps_malloc is the only way to demand PSRAM; makeUniqueNoThrow's operator new takes no capability
  // argument. The deleter keeps the RAII guarantee anyway.
  struct PsramFree {
    void operator()(uint8_t* p) const;
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramFree>;
  static PsramBuffer allocatePsram(size_t bytes);

  PsramBuffer bwBuffer;
  PsramBuffer lsbBuffer;
  PsramBuffer msbBuffer;
  size_t planeBytes = 0;
  // One failed allocation is not retried for the reader's session, so there is no per-page retry or log spam.
  bool bwAllocationFailed = false;
  bool grayAllocationFailed = false;
  unsigned long copyMs = 0;
  MenuCloseRestore::CapturedPage record;
};
