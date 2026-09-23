#pragma once

#include <I18n.h>

#include "CrossPointSettings.h"
#include "activities/PostedMessage.h"

// For a settings change the user made on the device. saveToFileAtomic reports a
// budget refusal or an SD failure only through LOG_ERR, which reaches the serial
// port and nowhere else, so without the message the change silently fails to
// stick. Posted rather than drawn: every caller goes on to render or finish(),
// which would paint over an immediate popup.
// Background saves (boot, font fallback, clock-sync bookkeeping) call
// SETTINGS.saveToFileAtomic() directly: nobody is looking at the screen for them.
inline bool saveSettingsOrReport() {
  if (SETTINGS.saveToFileAtomic()) return true;
  PostedMessage::post(tr(STR_SETTINGS_SAVE_FAILED));
  return false;
}
