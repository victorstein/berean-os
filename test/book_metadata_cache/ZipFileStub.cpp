// LINK-TIME substitute for lib/ZipFile/ZipFile.cpp. Only BookMetadataCache::buildBookBin calls
// ZipFile, and this suite never builds a cache: every call fails, so a test that reached one would
// fail rather than read a zip.
#include <ZipFile.h>

bool ZipFile::open() { return false; }
bool ZipFile::close() { return true; }
bool ZipFile::getInflatedFileSize(const char*, size_t*) { return false; }
int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>&, std::deque<uint32_t>&) { return 0; }
