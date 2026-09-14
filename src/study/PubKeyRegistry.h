#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "StudyStore/PubKey.h"

// /.berean/pubkeys.json -- book path -> (symbol, issue, language).
//
// The downloader is the only thing on the device that knows a publication's
// symbol: JW's OPF carries a random urn:uuid as dc:identifier and the symbol
// appears nowhere in the file or its name. Without this, a downloaded Watchtower
// gets a path-derived `local-` key that dies the moment the file moves, and
// would change again once Buscar lands -- orphaning every tag on it.
namespace PubKeyRegistry {

// Records what the downloader knew. Overwrites any previous entry for the path.
bool record(const std::string& bookPath, const study::RegisteredPub& pub);

// What was recorded for this path, if anything.
std::optional<study::RegisteredPub> lookup(const std::string& bookPath);

// The path of the first publication carrying one of `symbols`, skipping entries
// whose file is no longer on the card. For asking "is there a Watchtower here?"
// without requiring the book to have been opened.
std::optional<std::string> findBySymbol(std::initializer_list<std::string_view> symbols);

inline constexpr const char* PATH = "/.berean/pubkeys.json";

}  // namespace PubKeyRegistry
