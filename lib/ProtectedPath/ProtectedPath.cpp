#include "ProtectedPath.h"

namespace protectedpath {

namespace {
constexpr std::string_view SYSTEM_ITEMS[] = {"System Volume Information", "XTCache"};

bool isSeparator(const char c) { return c == '/' || c == '\\'; }

char asciiLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreAsciiCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (asciiLower(a[i]) != asciiLower(b[i])) return false;
  }
  return true;
}
}  // namespace

bool isProtectedName(std::string_view name) {
  while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
  if (!name.empty() && name.front() == '.') return true;
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.remove_suffix(1);
  for (const auto item : SYSTEM_ITEMS) {
    if (equalsIgnoreAsciiCase(name, item)) return true;
  }
  return false;
}

bool isProtectedPath(const std::string_view path) {
  size_t start = 0;
  while (start < path.size()) {
    size_t end = start;
    while (end < path.size() && !isSeparator(path[end])) end++;
    if (end > start && isProtectedName(path.substr(start, end - start))) return true;
    start = end + 1;
  }
  return false;
}

}  // namespace protectedpath
