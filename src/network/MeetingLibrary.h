#pragma once

#include <string>

#include "network/WolWeekScan.h"

// Where a meeting publication actually lives on the card, given the symbol and
// issue a week resolved to. The meetings screen asks this to decide whether a
// row offers "open" or "download".
namespace MeetingLibrary {

// The CDN symbol for a publication: "w" or "mwb".
const char* symbolFor(MeetingPub pub);

// Empty when this issue is not on the card.
//
// The registry is authoritative, but it only knows downloads made since it
// existed, so the card is the fallback: a meeting download is named
// "<pubName> <YYYY-MM>.epub" and that suffix is derived from the issue code, so
// the issue alone identifies the file. The two publications of one week almost
// always carry different issue codes, which is what keeps the fallback from
// confusing them; where they collide, the symbol's own name breaks the tie.
std::string findPublication(MeetingPub pub, const std::string& issue);

}  // namespace MeetingLibrary
