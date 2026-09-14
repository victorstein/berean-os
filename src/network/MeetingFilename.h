#pragma once

#include <string>

// On-disk filename for a downloaded meeting publication: "<pubName> <YYYY-MM>.epub",
// e.g. "La Atalaya (ed. estudio) 2026-07.epub". Publication first so a file browser
// groups by publication, the numeric issue code second so issues sort
// chronologically within a group.
//
// `issue` is the six-digit code the week scan recovered ("202607"); the YYYY-MM
// suffix is derived from it rather than from the API's formattedDate, which
// carries HTML entities. Falls back to the CDN's own filename when the response
// published no usable pubName or the issue is malformed.
//
// Pure: no I/O, no globals.
std::string meetingPublicationFilename(const char* pubName, const char* issue, const std::string& url);

// "202607" -> "2026-07", the suffix meetingPublicationFilename writes. Empty for
// anything that is not six digits.
std::string issueSuffix(const char* issue);

// The " YYYY-MM" a meeting download carries, without the extension, or empty.
// This is the only marker of a meeting publication that survives on the card:
// the CDN name is discarded at download time and the symbol appears nowhere in
// the file, so recognising one after the fact means recognising this suffix.
std::string meetingIssueSuffixOf(const std::string& filename);
