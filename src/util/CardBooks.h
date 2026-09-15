#pragma once

#include <string>
#include <vector>

// Every EPUB on the card, as full paths.
//
// The configured download folder is listed first and the root second, matching
// the order BookPathIndex and MeetingLibrary already search in; a path seen
// twice is kept once. Nothing is opened -- this is a directory listing, not a
// metadata scan -- so it is cheap enough to run when a screen opens.
namespace CardBooks {

std::vector<std::string> list();

// The filename without its directory or extension. What a book is called when
// nothing better is known about it.
std::string displayStem(const std::string& path);

// Deletes the book and its reading cache, and drops it from recents. The study
// data under /.berean/ is deliberately left: passages key on publication
// identity, not on the file, so re-downloading restores the tags.
bool remove(const std::string& bookPath);

}  // namespace CardBooks
