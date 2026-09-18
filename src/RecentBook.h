#pragma once
#include <string>

// One entry in the recents list — a book the reader has opened.
//
// Deliberately in its own header, free of <Arduino.h>: src/util/RecentBooksDoc.h
// needs it and is host-tested, while RecentBooksStore.h reaches Arduino.h through
// PersistableStore.h. Mirrors src/BookmarkEntry.h.
struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};
