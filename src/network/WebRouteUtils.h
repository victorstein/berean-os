#pragma once

#include <WebServer.h>

// Helpers shared by more than one route group of the device web server.
namespace webroutes {

bool isProtectedWebPath(const String& path);

void sendHtmlContent(WebServer& server, const char* data, size_t len);

}  // namespace webroutes
