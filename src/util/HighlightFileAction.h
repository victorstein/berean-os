#pragma once

#include <cstddef>
#include <cstdint>

// Pure decision logic behind HighlightFile::save. The load-side rule this file
// used to carry now lives in Serialization/TempAdoption.h, shared with
// PersistableStoreBase::readDocFromFileAdopting.
//
// The branch logic lives here, free of Arduino and HalStorage, and is
// host-tested in test/highlight_file/. HighlightFile.cpp itself has no host
// suite yet; test/stubs now carries an Arduino.h and an in-memory Storage fake
// it can be built against (see test/storage_io/).

// Whether save() may write, given the serialised size it measured. Checked
// BEFORE any file is touched: measure first, refuse over budget, write only
// on Write -- so a refusal never creates or modifies a directory or file.
enum class HighlightSaveAction : uint8_t { Write, RefuseTooLarge };

constexpr HighlightSaveAction highlightSaveAction(const size_t measuredBytes, const size_t budget) {
  return measuredBytes > budget ? HighlightSaveAction::RefuseTooLarge : HighlightSaveAction::Write;
}
