#pragma once

#include <cstddef>

class GfxRenderer;

// One of the user's tagged passages, with its reference and tag and the date, as
// the sleep image. Reads /.berean/ and never writes to it.
namespace study_sleep_screen {

// Draws and pushes the frame, then records the passage as shown. False, with
// nothing pushed, when there is no passage to show; the caller then draws the
// default screen.
bool render(const GfxRenderer& renderer);

// Today's local date as "Wednesday 30 Sep", the line this screen and Home show.
// False, leaving `out` unspecified, when the clock has no date or time.
bool formatDate(char* out, size_t outSize);

}  // namespace study_sleep_screen
