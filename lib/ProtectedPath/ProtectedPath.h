#pragma once

#include <string_view>

// The SD paths the web server and WebDAV must never read, write, list or
// delete: anything under a dot-named folder (/.berean study data, /.crosspoint
// settings and Wi-Fi credentials, /.fonts) plus a few system folders. Free of
// Arduino and storage dependencies so the rule can be unit tested on the host.
//
// Paths arrive already URL-decoded (WebServer::arg() and the WebDAV request
// path decode before any handler sees them); a literal "%2E" is not a dot to
// SdFat either, so it is not treated as one here.
namespace protectedpath {

// One path component, judged the way SdFat resolves it: leading spaces are
// skipped and trailing dots and spaces are trimmed before the name is matched.
bool isProtectedName(std::string_view name);

// True when ANY component of the path is protected. A ".." component starts
// with a dot, so traversal is refused rather than resolved.
bool isProtectedPath(std::string_view path);

}  // namespace protectedpath
