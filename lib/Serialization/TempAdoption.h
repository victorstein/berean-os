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
// branch logic is host-testable even though the I/O around it is not.

// What a load should do next, given the primary file's read status and what is
// known about `<path>.tmp`. tempExists/tempParsed only matter when
// primary == Missing: a `.tmp` is never consulted when the primary file is
// present (whether readable or not), because it must never overwrite or
// second-guess a file that may still hold the user's data.
enum class TempAdoptionAction : uint8_t {
  UseLoaded,              // primary parsed -- use it
  ReportEmpty,            // genuinely nothing on disk
  PromoteTempAndUseIt,    // .tmp is the only surviving copy; rescue it now
  DeleteTempReportEmpty,  // .tmp exists but is unusable
  ReportFailed,           // primary bytes exist but could not be read/parsed
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
      return tempParsed ? TempAdoptionAction::PromoteTempAndUseIt : TempAdoptionAction::DeleteTempReportEmpty;
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
    case TempAdoptionAction::DeleteTempReportEmpty:
      return DocReadStatus::Missing;
    case TempAdoptionAction::ReportFailed:
    default:
      return primary;
  }
}
