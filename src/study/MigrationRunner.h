#pragma once

#include <cstdint>

// Migrates /.crosspoint/highlights/*.json into the /.berean/ study store.
//
// Resumable: each source file is recorded in a ledger only after its passages
// are safely written, so an interrupted run resumes at the first unrecorded
// file. Idempotent: a file already in the ledger is skipped.
//
// The legacy store is NEVER renamed or deleted. An earlier design renamed each
// source to <name>.json.migrated as its commit point, which voided the rollback
// the spec calls non-negotiable: a pre-Phase-1 build resolves the ORIGINAL
// filename, would find nothing, report "safe to save over", and be one highlight
// away from overwriting the user's data.
#include "study/MigrationProgress.h"

class GfxRenderer;

namespace MigrationRunner {

struct Summary {
  uint16_t sourceFiles = 0;
  uint16_t highlightsRead = 0;
  uint16_t passagesWritten = 0;  // counted from the store AFTER a successful save
  uint16_t addressedVerse = 0;
  uint16_t addressedParagraph = 0;
  uint16_t addressedDocumentOffset = 0;
  uint16_t referenceMismatches = 0;
  uint16_t pendingUpgrade = 0;
  uint16_t dropped = 0;
  uint16_t tagsAdopted = 0;
};

// True when at least one un-migrated source file exists.
bool pending();

// Runs any pending migration. Returns false only on a failure that left work
// undone; "nothing to do" is true. Safe to call on every boot.
//
// Takes the renderer because SpineHtmlStream borrows the framebuffer to draw
// the indexing popup on a large inflate. Migration runs at boot, before any
// activity, so main.cpp's global renderer is the one to pass.
bool runIfPending(Summary& summary, GfxRenderer& renderer, const MigrationProgress& progress = {});

inline constexpr const char* REPORT_PATH = "/.berean/migration-report.json";
inline constexpr const char* LEDGER_PATH = "/.berean/migration-ledger.json";

}  // namespace MigrationRunner
