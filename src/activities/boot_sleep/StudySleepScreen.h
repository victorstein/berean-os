#pragma once

class GfxRenderer;

// One of the user's tagged passages, the date and the Bible progress strip, as
// the sleep image. Reads /.berean/ and never writes to it.
namespace study_sleep_screen {

// Draws and pushes the frame, then records the passage as shown. False, with
// nothing pushed, when there is no passage to show; the caller then draws the
// default screen.
bool render(const GfxRenderer& renderer);

}  // namespace study_sleep_screen
