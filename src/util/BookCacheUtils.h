#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// The reading cache directory for a book. Delegates to Epub, which owns the
// derivation (a hash of the path as passed in), so a caller cannot drift from
// it -- and the hash means two spellings of one path, "/x" and "//x", key two
// different caches.
std::string bookCachePath(const std::string& path);
