#include "PageFrameCache.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <cstring>

void PageFrameCache::PsramFree::operator()(uint8_t* p) const { heap_caps_free(p); }

PageFrameCache::PsramBuffer PageFrameCache::allocatePsram(const size_t bytes) {
  return PsramBuffer(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void PageFrameCache::invalidate() {
  record.reset();
  copyMs = 0;
}

void PageFrameCache::captureBw(const uint8_t* frame, const size_t bytes, const int spineIndex, const int pageIndex,
                               const bool grayExpected) {
  invalidate();
  if (frame == nullptr || bytes == 0) return;
  if (!bwBuffer) {
    if (bwAllocationFailed) return;
    bwBuffer = allocatePsram(bytes);
    if (!bwBuffer) {
      bwAllocationFailed = true;
      LOG_ERR("ERS", "OOM: page cache plane (%u bytes, PSRAM)", static_cast<unsigned>(bytes));
      return;
    }
    planeBytes = bytes;
  }
  if (bytes != planeBytes) {
    LOG_ERR("ERS", "Page cache: frame is %u bytes, planes are %u", static_cast<unsigned>(bytes),
            static_cast<unsigned>(planeBytes));
    return;
  }
  const unsigned long start = millis();
  memcpy(bwBuffer.get(), frame, bytes);
  record.beginPage(spineIndex, pageIndex, grayExpected);
  copyMs += millis() - start;
}

void PageFrameCache::captureGrayPlane(const bool lsbPlane, const uint8_t* plane, const size_t bytes) {
  if (!record.bw || plane == nullptr || bytes != planeBytes) return;
  if (!lsbBuffer) {
    if (grayAllocationFailed) return;
    lsbBuffer = allocatePsram(planeBytes);
    msbBuffer = allocatePsram(planeBytes);
    if (!lsbBuffer || !msbBuffer) {
      lsbBuffer.reset();
      msbBuffer.reset();
      grayAllocationFailed = true;
      LOG_ERR("ERS", "OOM: page cache plane (%u bytes, PSRAM)", static_cast<unsigned>(2 * planeBytes));
      return;
    }
  }
  const unsigned long start = millis();
  memcpy(lsbPlane ? lsbBuffer.get() : msbBuffer.get(), plane, planeBytes);
  record.markPlane(lsbPlane);
  copyMs += millis() - start;
}

void PageFrameCache::noteGrayShown() {
  if (record.bw) record.markGrayShown();
}

void PageFrameCache::release() {
  invalidate();
  bwBuffer.reset();
  lsbBuffer.reset();
  msbBuffer.reset();
  planeBytes = 0;
}
