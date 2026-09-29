#pragma once

#include <ArduinoJson.h>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

// PSRAM for the passage store's whole texts and the JSON documents that carry
// them. Every block under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (4,096 B) would
// otherwise land in internal SRAM, so uncapped texts would grow the internal
// footprint with the user's data. Task context only, never an ISR.
//
// A failed allocation returns nullptr: PassageText reports it, and ArduinoJson
// reports it as overflowed() or DeserializationError::NoMemory. There is no
// fallback to internal SRAM.
namespace PsramJsonAllocator {

ArduinoJson::Allocator* json();

study::TextAllocator text();

study::PassageDoc::Allocators passageDoc();

}  // namespace PsramJsonAllocator
