#include "StudyStore/Unit.h"

#include <cstdio>

namespace study {
namespace {

char kindLetter(const UnitKind k) {
  switch (k) {
    case UnitKind::Verse: return 'v';
    case UnitKind::Paragraph: return 'p';
    case UnitKind::DocumentOffset: return 'd';
  }
  return 'd';
}

std::optional<UnitKind> kindFromLetter(const char c) {
  switch (c) {
    case 'v': return UnitKind::Verse;
    case 'p': return UnitKind::Paragraph;
    case 'd': return UnitKind::DocumentOffset;
    default: return std::nullopt;
  }
}

}  // namespace

bool orderableByAddress(const Unit& a, const Unit& b) {
  return a.kind == b.kind && a.kind != UnitKind::Paragraph;
}

bool operator<(const Unit& a, const Unit& b) {
  // Kind first, so the ordering is a strict weak ordering consistent with the
  // defaulted operator==. Without it a DocumentOffset and a Verse unit compare
  // mutually non-less yet unequal, which is silently wrong in a std::set or
  // std::map even though std::sort tolerates it.
  if (a.kind != b.kind) return a.kind < b.kind;
  if (a.book != b.book) return a.book < b.book;
  // Paragraph units carry no positional meaning in `minor` -- data-pid runs out
  // of document order in 46% of the documents that have it -- so they order by
  // pid only to be deterministic, never to mean "earlier in the document".
  if (a.major != b.major) return a.major < b.major;
  if (a.minor != b.minor) return a.minor < b.minor;
  return a.offset < b.offset;
}

std::string unitToCompact(const Unit& u) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%c:%u:%u:%u:%u", kindLetter(u.kind), u.book, u.major, u.minor, u.offset);
  return buf;
}

std::optional<Unit> unitFromCompact(const std::string& s) {
  if (s.size() < 2 || s[1] != ':') return std::nullopt;
  const auto kind = kindFromLetter(s[0]);
  if (!kind) return std::nullopt;

  unsigned book = 0, major = 0, minor = 0, offset = 0;
  char tail = '\0';
  if (sscanf(s.c_str() + 2, "%u:%u:%u:%u%c", &book, &major, &minor, &offset, &tail) != 4) return std::nullopt;
  if (book > 66 || major > UINT16_MAX || minor > UINT16_MAX) return std::nullopt;

  return Unit{*kind, static_cast<uint8_t>(book), static_cast<uint16_t>(major), static_cast<uint16_t>(minor), offset};
}

}  // namespace study
