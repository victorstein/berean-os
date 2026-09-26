#pragma once

#include <cstdint>

#include "DocReadStatus.h"

// The shared rule for recovering a write that was interrupted between
// PersistableStore.cpp:38 (remove the destination) and :39 (rename the temp
// file over it). In that window neither file exists, but a complete
// `<path>.tmp` is on the card.
//
// Kept free of Arduino, HalStorage and PersistableStore -- the same shape as
// classifyDocRead in DocReadStatus.h and fitsBudget in SaveBudget.h -- so the
// branch logic is host-testable on its own. The I/O around it is host-tested
// against the Storage fake in test/storage_io/.

// What a load should do next, given the primary file's read status and what is
// known about `<path>.tmp`. tempExists/tempParsed only matter when
// primary == Missing: a `.tmp` is never consulted when the primary file is
// present (whether readable or not), because it must never overwrite or
// second-guess a file that may still hold the user's data.
enum class TempAdoptionAction : uint8_t {
  UseLoaded,            // primary parsed -- use it
  ReportEmpty,          // genuinely nothing on disk
  PromoteTempAndUseIt,  // .tmp is the only surviving copy; rescue it now
  // .tmp exists but is unusable, and is left on the card. Removing it buys
  // nothing -- the next save truncates it, since SDCardManager::writeFile
  // removes the destination before re-creating it -- and a transient SD read
  // failure is indistinguishable from an empty file, so deleting here could
  // destroy the only surviving copy.
  KeepTempReportEmpty,
  ReportFailed,  // primary bytes exist but could not be read/parsed
};

constexpr TempAdoptionAction tempAdoptionAction(const DocReadStatus primary, const bool tempExists,
                                                const bool tempParsed) {
  switch (primary) {
    case DocReadStatus::Ok:
      return TempAdoptionAction::UseLoaded;
    case DocReadStatus::Unreadable:
    case DocReadStatus::ParseError:
      return TempAdoptionAction::ReportFailed;
    case DocReadStatus::Missing:
    default:
      if (!tempExists) return TempAdoptionAction::ReportEmpty;
      return tempParsed ? TempAdoptionAction::PromoteTempAndUseIt : TempAdoptionAction::KeepTempReportEmpty;
  }
}

// What an adopting read reports to its caller. ReportFailed keeps the primary's
// own status: DocReadStatus.h:6-7 requires callers to tell Missing (safe to
// overwrite) from Unreadable/ParseError (never overwrite), so this must not
// flatten them.
constexpr DocReadStatus adoptedReadStatus(const DocReadStatus primary, const TempAdoptionAction action) {
  switch (action) {
    case TempAdoptionAction::UseLoaded:
    case TempAdoptionAction::PromoteTempAndUseIt:
      return DocReadStatus::Ok;
    case TempAdoptionAction::ReportEmpty:
    case TempAdoptionAction::KeepTempReportEmpty:
      return DocReadStatus::Missing;
    case TempAdoptionAction::ReportFailed:
    default:
      return primary;
  }
}

// What a load built on PersistableStoreBase::loadAdopting reports. Each
// per-store LoadResult is an alias of this.
enum class AdoptedLoad : uint8_t {
  Loaded,             // primary read, parsed and accepted
  Empty,              // nothing usable on disk -- safe to save over
  RecoveredFromTemp,  // an interrupted write left .tmp as the only copy; promoted and accepted
  Failed,             // unreadable, unparseable or rejected -- DATA MAY STILL EXIST
};

// `accepted` is the store's fromJson verdict and only matters for the two
// actions that hand it a document. An action added later lands in `default`
// and fails, which is the safe direction.
constexpr AdoptedLoad adoptedLoad(const TempAdoptionAction action, const bool accepted) {
  switch (action) {
    case TempAdoptionAction::UseLoaded:
      return accepted ? AdoptedLoad::Loaded : AdoptedLoad::Failed;
    case TempAdoptionAction::PromoteTempAndUseIt:
      return accepted ? AdoptedLoad::RecoveredFromTemp : AdoptedLoad::Failed;
    case TempAdoptionAction::ReportEmpty:
    case TempAdoptionAction::KeepTempReportEmpty:
      return AdoptedLoad::Empty;
    case TempAdoptionAction::ReportFailed:
    default:
      return AdoptedLoad::Failed;
  }
}
