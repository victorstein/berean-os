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

// The reading cache directory for a book, matching Epub's own key derivation
// (Epub.h:48): a hash of the full path as passed in. Removing a book without
// this leaves its cached sections behind, and a later book landing on the same
// path would render from them.
std::string cachePathFor(const std::string& bookPath);

// Deletes the book and its reading cache, and drops it from recents. The study
// data under /.berean/ is deliberately left: passages key on publication
// identity, not on the file, so re-downloading restores the tags.
bool remove(const std::string& bookPath);

}  // namespace CardBooks
