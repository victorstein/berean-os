#pragma once

#include <cstddef>
#include <cstdint>

#include "StudySleepFit.h"
#include "StudySleepPick.h"

// The bounded scan over /.berean/passages that the study sleep screen and the
// Home verse card share: stream each passage file, drop rows that are not whole
// or do not fit, and offer the rest to a Sampler. Reads /.berean/ and never
// writes to it.
namespace study_passage_scan {

// Decides, before sampling, whether a passage can be shown whole at all: the
// caller's floor rung, measured by its own fonts.
struct FitGate {
  study_sleep::MeasureFn measure = nullptr;
  const void* measureCtx = nullptr;
  study_sleep::FitRung floor{};
  int width = 0;
  // Rows longer than this are refused without measuring.
  size_t prefilterBytes = study_sleep::FIT_PREFILTER_BYTES;
};

struct ScanTotals {
  uint32_t entries = 0;
  size_t bytesParsed = 0;
  bool capHit = false;
  uint32_t rowsNotWhole = 0;
  uint32_t rowsOverPrefilter = 0;
  uint32_t rowsUnfit = 0;
  // A file's document ran out of memory: nothing about the passages is known.
  bool outOfMemory = false;
};

// Every passage file, starting at a random one and wrapping, within the byte
// budget. Rows in `ring` count only when nothing else exists. False when nothing
// was picked, including on OOM.
bool pickFromAll(study_sleep::Sampler& sampler, const FitGate& gate, const study_sleep::RingView& ring,
                 ScanTotals& totals);

// One passage file, in file order, with no recent ring. False when nothing was
// picked; a missing or unreadable file picks nothing.
bool pickFromFile(const char* path, study_sleep::Sampler& sampler, const FitGate& gate, ScanTotals& totals);

}  // namespace study_passage_scan
