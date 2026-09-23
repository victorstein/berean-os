#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

// For a settings change the user made on the device. saveToFileAtomic reports a
// budget refusal or an SD failure only through LOG_ERR, which x4pro-gh_release
// compiles out, so without the popup the change silently fails to stick.
// Background saves (boot, font fallback, clock-sync bookkeeping) call
// SETTINGS.saveToFileAtomic() directly: nobody is looking at the screen for them.
inline bool saveSettingsOrReport(const GfxRenderer& renderer) {
  if (SETTINGS.saveToFileAtomic()) return true;
  GUI.drawPopup(renderer, tr(STR_SETTINGS_SAVE_FAILED));
  return false;
}
