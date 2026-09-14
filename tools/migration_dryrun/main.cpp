// Runs the firmware's own MigrationPlanner over real legacy highlights, off
// device. A regression gate: it proves the host build reproduces the device
// build on identical bytes, and that no change to the scanners, the counter or
// the planner has silently degraded addressing.
//
// It does NOT prove the migration correct end to end -- the stored `ref` was
// written by the device using VerseAnchors against the same offsets this
// re-resolves with VerseAnchors, so agreement is close to tautological. The
// write paths, the store and the tag ids are covered by the host suite and the
// device checklist.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "StudyStore/MigrationPlanner.h"
#include "StudyStore/UnitText.h"

namespace {

struct DocumentContext {
  std::string xhtml;
  study::DocumentUnits units;
};

std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string unitTextFor(void* ctx, const study::Unit& unit) {
  auto* doc = static_cast<DocumentContext*>(ctx);
  for (const auto& anchor : doc->units.anchors) {
    if (anchor.major == unit.major && anchor.minor == unit.minor) {
      return study::extractUnitText(doc->xhtml.c_str(), doc->xhtml.size(), doc->units, anchor);
    }
  }
  return {};
}

std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> cells;
  std::istringstream ls(line);
  std::string cell;
  while (std::getline(ls, cell, '\t')) cells.push_back(cell);
  return cells;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: migration_dryrun <tsv>\n");
    return 2;
  }

  std::ifstream tsv(argv[1]);
  std::string line;
  int total = 0, verse = 0, paragraph = 0, documentOffset = 0, mismatch = 0, dropped = 0, missing = 0;

  while (std::getline(tsv, line)) {
    if (line.empty()) continue;
    const auto c = split(line);
    if (c.size() < 6) continue;
    total++;

    DocumentContext doc;
    doc.xhtml = slurp(c[4]);
    if (doc.xhtml.empty()) {
      printf("MISSING    %s\n", c[4].c_str());
      missing++;
      continue;
    }
    doc.units = study::scanUnits(doc.xhtml.c_str(), doc.xhtml.size());
    doc.units.book = static_cast<uint8_t>(std::stoul(c[5]));

    study::MigrationInputs in;
    in.pubKey = "bible";
    in.document = c[4].substr(c[4].find_last_of('/') + 1);
    in.units = doc.units;
    in.unitTextCtx = &doc;
    in.unitText = &unitTextFor;

    study::LegacyHighlight legacy;
    legacy.spineIndex = static_cast<uint16_t>(std::stoul(c[0]));
    legacy.start = static_cast<uint32_t>(std::stoul(c[1]));
    legacy.end = static_cast<uint32_t>(std::stoul(c[2]));
    legacy.reference = c[3];
    legacy.tagNames = {"placeholder"};

    const auto out = study::planMigration(in, legacy);
    if (!out.passage) {
      printf("DROPPED    %s\n", c[3].c_str());
      dropped++;
      continue;
    }

    switch (out.passage->start.kind) {
      case study::UnitKind::Verse: verse++; break;
      case study::UnitKind::Paragraph: paragraph++; break;
      case study::UnitKind::DocumentOffset: documentOffset++; break;
    }

    if (!out.referenceAgrees) {
      printf("MISMATCH   stored=%-22s resolved=%s\n", c[3].c_str(), out.resolvedReference.c_str());
      mismatch++;
    } else if (out.passage->fingerprint.length == 0) {
      printf("NO-TEXT    %s  (fingerprint would be empty)\n", c[3].c_str());
    }
  }

  printf("\n total=%d  verse=%d  paragraph=%d  document-offset=%d  ref-mismatch=%d  dropped=%d  missing=%d\n", total,
         verse, paragraph, documentOffset, mismatch, dropped, missing);

  const bool ok = mismatch == 0 && dropped == 0 && missing == 0 && verse == total;
  printf(" %s\n", ok ? "GATE PASS" : "GATE FAIL");
  return ok ? 0 : 1;
}
