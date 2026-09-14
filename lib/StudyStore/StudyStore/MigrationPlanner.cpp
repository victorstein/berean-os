#include "StudyStore/MigrationPlanner.h"

#include <cstdio>

namespace study {

std::string referenceTail(const std::string& reference) {
  const size_t colon = reference.rfind(':');
  if (colon == std::string::npos) return "";
  size_t start = reference.find_last_of(" \t", colon);
  start = (start == std::string::npos) ? 0 : start + 1;

  unsigned chapter = 0;
  unsigned verse = 0;
  char tail = '\0';
  if (sscanf(reference.c_str() + start, "%u:%u%c", &chapter, &verse, &tail) != 2) return "";

  char buf[24];
  snprintf(buf, sizeof(buf), "%u:%u", chapter, verse);
  return buf;
}

void adoptTagNames(TagPalette& palette, const std::vector<std::string>& names) {
  for (const auto& name : names) palette.add(name);
}

MigrationResult planMigration(const MigrationInputs& in, const LegacyHighlight& legacy) {
  MigrationResult out;
  if (legacy.tagNames.empty()) {
    out.outcome = MigrationOutcome::DroppedNoTags;
    return out;
  }

  TaggedPassage p;
  p.document = in.document;
  p.documentSpine = legacy.spineIndex;
  p.snippet = legacy.snippet;
  p.reference = legacy.reference;

  // With no palette the caller is resolving addresses only (the dry-run tool
  // does this); the passage comes back with its address and no ids.
  if (in.palette) {
    for (const auto& name : legacy.tagNames) {
      if (const auto id = in.palette->add(name)) p.tags.push_back(*id);
    }
  }

  if (!in.sourceAvailable) {
    p.start = Unit{UnitKind::DocumentOffset, 0, 0, 0, legacy.start};
    p.end = Unit{UnitKind::DocumentOffset, 0, 0, 0, legacy.end};
    p.pendingUpgrade = true;
    out.passage = std::move(p);
    out.outcome = MigrationOutcome::PendingUpgrade;
    return out;
  }

  p.start = resolve(in.units, legacy.start);
  p.end = resolve(in.units, legacy.end);

  if (in.unitText) {
    p.fingerprint = fingerprintOf(in.unitText(in.unitTextCtx, p.start));
    if (!(p.end == p.start)) p.endFingerprint = fingerprintOf(in.unitText(in.unitTextCtx, p.end));
  }

  if (p.start.kind == UnitKind::DocumentOffset) {
    out.outcome = MigrationOutcome::ResolvedDocumentOffset;
    out.passage = std::move(p);
    return out;
  }

  if (p.start.kind == UnitKind::Verse) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u:%u", p.start.major, p.start.minor);
    out.resolvedReference = buf;
    const std::string stored = referenceTail(legacy.reference);
    out.referenceAgrees = stored.empty() || stored == out.resolvedReference;
  }

  out.outcome = out.referenceAgrees ? MigrationOutcome::Resolved : MigrationOutcome::ResolvedReferenceMismatch;
  out.passage = std::move(p);
  return out;
}

}  // namespace study
