#include "WebRouteUtils.h"

#include <ProtectedPath.h>

#include <string_view>

namespace webroutes {

bool isProtectedWebPath(const String& path) {
  return protectedpath::isProtectedPath(std::string_view(path.c_str(), path.length()));
}

void sendHtmlContent(WebServer& server, const char* data, size_t len) {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", data, len);
}

}  // namespace webroutes
