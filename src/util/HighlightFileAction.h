#pragma once

#include <cstddef>
#include <cstdint>

// Pure decision logic behind HighlightFile::save. The load-side rule this file
// used to carry now lives in Serialization/TempAdoption.h, shared with
// PersistableStoreBase::readDocFromFileAdopting.
//
// HighlightFile.cpp itself cannot be built on the host -- it includes
// <PersistableStore.h>, which includes <Arduino.h> unconditionally, a
// genuinely ESP32-specific header (FreeRTOS, esp32-hal, pins_arduino,
// soc/gpio_reg) with no host stub anywhere in this repo and too costly to fake
// convincingly. So the branch logic lives here, free of Arduino and
// HalStorage, and is host-tested in test/highlight_file/.

// Whether save() may write, given the serialised size it measured. Checked
// BEFORE any file is touched: measure first, refuse over budget, write only
// on Write -- so a refusal never creates or modifies a directory or file.
enum class HighlightSaveAction : uint8_t { Write, RefuseTooLarge };

constexpr HighlightSaveAction highlightSaveAction(const size_t measuredBytes, const size_t budget) {
  return measuredBytes > budget ? HighlightSaveAction::RefuseTooLarge : HighlightSaveAction::Write;
}
