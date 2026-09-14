#pragma once

#include <cstdint>

// Progress sink for the one-off work at boot: the study migration and the unit
// index build behind it.
//
// A plain context pointer and function pointer rather than std::function --
// CLAUDE.md prohibits the latter in this codebase (~2-4 KB per signature plus a
// heap-allocated closure), and this is called from the SD-bound inner loops.
//
// The count is deliberately open-ended: neither the migration nor the index
// build knows its total work up front (the number of chapter-nav pages depends
// on the publication), so this reports STEPS DONE and the caller renders motion
// rather than a percentage it would have to lie about.
struct MigrationProgress {
  void* ctx = nullptr;
  void (*onStep)(void* ctx, const char* label) = nullptr;

  const char* label = "";

  void tick() const {
    if (onStep) onStep(ctx, label);
  }

  void tick(const char* stepLabel) const {
    if (onStep) onStep(ctx, stepLabel);
  }
};
