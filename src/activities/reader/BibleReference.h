#pragma once

#include <string>

// A Bible's covering TOC entry names only the book; the chapter comes from the
// spine item's verse markers, so it is appended here: "Exodo" -> "Exodo 5".
// Free of Arduino and the Epub library so test/ui_layout can exercise it.
inline std::string bibleReference(const std::string& bookName, const int chapter) {
  if (chapter <= 0) return bookName;
  return bookName + ' ' + std::to_string(chapter);
}
