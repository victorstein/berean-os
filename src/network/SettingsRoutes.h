#pragma once

#include <WebServer.h>

// The web settings page and its JSON API: GET and POST /api/settings.
class SettingsRoutes {
 public:
  void registerRoutes(WebServer& server);

 private:
  void handleSettingsPage(WebServer& server) const;
  void handleGetSettings(WebServer& server) const;
  void handlePostSettings(WebServer& server);
};
