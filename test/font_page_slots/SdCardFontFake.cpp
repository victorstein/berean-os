// Link-time host substitute for SdCardFont, the same device test/pagination
// uses for GfxRenderer: SdCardFont has no virtual functions, so there is no
// seam to inject through, and SdCardFont.cpp reaches <Arduino.h> and the SD
// stack. The real SdCardFont.h is included unmodified — it needs no stubbing,
// pulling only <cstdint> <deque> <string> <vector> and the two Epd headers
// (SdCardFont.h:3-9).
//
// These are the five methods FontCacheManager.cpp calls: clearCache (:18),
// releaseResidentCaches (:25), prewarm (:33), logStats (:58), resetStats (:64).
// Every test passes an EMPTY sdCardFonts_ map, so no body here ever runs; they
// exist so the calls link. If FontCacheManager starts calling a sixth
// SdCardFont method, this link fails loudly instead of the suite silently
// diverging from the firmware.

#include <SdCardFont.h>

void SdCardFont::clearCache() {}

void SdCardFont::releaseResidentCaches() {}

int SdCardFont::prewarm(const char*, uint8_t, bool, bool) { return 0; }

void SdCardFont::logStats(const char*) {}

void SdCardFont::resetStats() {}
