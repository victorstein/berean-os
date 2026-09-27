#include "network/WifiSession.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "SilentRestart.h"
#include "network/WifiSessionPolicy.h"

WifiSession::WifiSession() : connectedOnEntry(WiFi.status() == WL_CONNECTED) {}

WifiSession::~WifiSession() {
  const bool connectedAtExit = WiFi.status() == WL_CONNECTED;
  const bool radioOnAtExit = WiFi.getMode() != WIFI_MODE_NULL;
  switch (WifiSessionPolicy::onExit(connectedOnEntry, connectedAtExit, radioOnAtExit)) {
    case WifiSessionPolicy::ExitAction::LeaveConnected:
      LOG_DBG("WIFI", "Session closed; leaving the connection it found");
      return;
    case WifiSessionPolicy::ExitAction::Nothing:
      LOG_DBG("WIFI", "Session closed; radio already off");
      return;
    case WifiSessionPolicy::ExitAction::EndSession:
      LOG_DBG("WIFI", "Session ended; turning the radio off");
      WiFi.disconnect(false);
      delay(30);
      // On touch boards this stops SNTP and turns the radio off without a
      // reboot, and it is a no-op once deep sleep has begun (src/main.cpp).
      silentRestart();
      return;
  }
}
