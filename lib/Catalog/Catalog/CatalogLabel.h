#pragma once

#include <cstddef>
#include <string_view>

// How a catalog entry is named on a Buscar row: the publication and the date of
// its issue, rather than the symbol and codes the API is addressed by.
namespace catalog {

// The nth space-separated word of `list`, empty when there are fewer. Month
// names travel as one translated string of twelve words rather than twelve keys.
std::string_view wordAt(std::string_view list, int index);

// Parses a run of ASCII digits. False for an empty run or any other character.
bool digitsToInt(std::string_view text, int& out);

// Copies `text` into `out` and terminates it. False, writing nothing, when it
// does not fit.
bool copyOut(std::string_view text, char* out, size_t outSize);

// "¡Despertad! 1980" -> "¡Despertad!" for an entry with an issue, whose row
// shows the date underneath. Any other title, and any book, is returned whole.
std::string_view displayTitle(std::string_view title, std::string_view year, bool hasIssue);

// "19800422" -> "22 de abril de 1980" through `dayFormat` ("%d de %s de %d"),
// and "198004" -> "Abril de 1980" through `monthFormat` ("%s de %d"), the first
// letter raised because the month then starts the label. The formats come from
// tr() and take day, month, year in that order. Writes the code through unchanged
// when it cannot be parsed. Returns false when `out` was too small, having
// written nothing.
bool formatIssueDate(std::string_view issue, std::string_view monthsLong, const char* dayFormat,
                     const char* monthFormat, char* out, size_t outSize);

}  // namespace catalog
