#pragma once

#include <optional>
#include <string>

// Recovers the book path a /.crosspoint/highlights/<name>.json file was named
// after.
//
// pathflatten::toCacheName cannot be inverted: it erases the first character,
// maps both '/' and '\\' to '_' -- indistinguishable from a literal underscore
// -- and drops everything from the last dot. So the path is recovered by walking
// the card and re-flattening each candidate until one matches, which is exact by
// construction.
//
// This exists because assuming the filename WAS the path would have produced a
// migration that silently addressed nothing: every passage would fall back to a
// raw document offset, which paints identically to the old model, so every
// acceptance check would still pass.
namespace BookPathIndex {

// The EPUB whose flattened name equals `flattenedStem`, or nullopt when no file
// on the card produces it. Searches SETTINGS.downloadFolder first, then the
// card root.
//
// Ambiguity -- two files flattening to the same stem -- resolves to NO MATCH,
// never a guess: the wrong EPUB would address every passage into the wrong book.
std::optional<std::string> resolve(const std::string& flattenedStem);

}  // namespace BookPathIndex
