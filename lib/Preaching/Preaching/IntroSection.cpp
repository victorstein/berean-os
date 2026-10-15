#include "Preaching/IntroSection.h"

namespace preaching::IntroSection {

std::string_view stripTocPrefix(const std::string_view label) {
  const size_t space = label.find(' ');
  if (space == 0 || space == std::string_view::npos) return label;
  const size_t digitsStart = space + 1;
  size_t cursor = digitsStart;
  while (cursor < label.size() && label[cursor] >= '0' && label[cursor] <= '9') cursor++;
  if (cursor == digitsStart) return label;
  const std::string_view rest = label.substr(cursor);
  constexpr std::string_view SEPARATOR = ": ";
  if (rest.substr(0, SEPARATOR.size()) != SEPARATOR || rest.size() == SEPARATOR.size()) return label;
  return rest.substr(SEPARATOR.size());
}

}  // namespace preaching::IntroSection
