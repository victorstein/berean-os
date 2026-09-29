#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "StudyStore/TextRepair.h"
#include "StudyStore/UnitFingerprint.h"
#include "StudyStore/UnitText.h"

namespace {

const char* kChapter =
    "<html><body>"
    "<p data-pid=\"26\"><span id=\"chapter5_verse1\"></span>"
    "<span class=\"w_ch\"><strong>5</strong> </span>Uno dos tres.</p>\n"
    "<p data-pid=\"27\"><span id=\"chapter5_verse2\"></span><strong><sup>2</sup></strong>\xE2\x80\xAF"
    "Cuatro cinco.<a epub:type=\"noteref\" href=\"#f\">*</a> "
    "<span id=\"chapter5_verse3\"></span><strong><sup>3</sup></strong>\xE2\x80\xAF"
    "Seis siete.</p>\n"
    "<div class=\"groupFootnote\"><aside epub:type=\"footnote\"><p data-pid=\"70\">Nota.</p></aside></div>"
    "</body></html>";

struct Source {
  study::DocumentUnits units = study::scanUnits(kChapter, strlen(kChapter));
  bool emptyText = false;
};

std::string spanText(void* ctx, const study::PassageSpan& span) {
  auto* source = static_cast<Source*>(ctx);
  if (source->emptyText) return {};
  return study::extractPassageText(kChapter, strlen(kChapter), span.from, span.to,
                                   study::CaptureFilter{span.verseDocument, span.extendToWordEnd});
}

std::string unitText(void* ctx, const study::Unit& unit) {
  auto* source = static_cast<Source*>(ctx);
  const size_t index = study::anchorIndexOf(source->units, unit);
  if (index == SIZE_MAX) return {};
  return study::extractUnitText(kChapter, strlen(kChapter), source->units, source->units.anchors[index]);
}

study::TextRepairInputs inputsFor(Source& source) {
  study::TextRepairInputs in;
  in.units = &source.units;
  in.ctx = &source;
  in.spanText = &spanText;
  in.unitText = &unitText;
  return in;
}

// A pre-#183 row: verse-addressed, 120-byte snippet only, no whole text.
study::TaggedPassage snippetOnlyRow(const Source& source, const std::string& snippet) {
  study::TaggedPassage p;
  p.start = study::resolve(source.units, source.units.anchors[1].offset + 9);
  p.end = study::resolve(source.units, source.units.anchors[2].offset + 3);
  p.snippet = snippet;
  return p;
}

TEST(TextRepair, ASnippetOnlyV1RowIsRebuiltToItsWholeVerses) {
  Source source;
  const auto plan = study::planTextRepair(snippetOnlyRow(source, "cinco. Seis"), inputsFor(source));
  EXPECT_EQ(plan.outcome, study::TextRepairOutcome::Rebuilt);
  EXPECT_EQ(plan.wholeText, "Cuatro cinco. Seis siete.");
}

TEST(TextRepair, AV3RowWithACappedTextIsRebuilt) {
  Source source;
  study::TaggedPassage row = snippetOnlyRow(source, "Cuatro cinco.");
  row.displayText.assign("Cuatro cinco. Seis\xE2\x80\xA6");
  const auto plan = study::planTextRepair(row, inputsFor(source));
  EXPECT_EQ(plan.outcome, study::TextRepairOutcome::Rebuilt);
  EXPECT_EQ(plan.wholeText, "Cuatro cinco. Seis siete.");
}

TEST(TextRepair, AWholeRowIsLeftAlone) {
  Source source;
  study::TaggedPassage row = snippetOnlyRow(source, "Cuatro");
  row.whole = true;
  row.displayText.assign("Cuatro cinco.");
  EXPECT_EQ(study::planTextRepair(row, inputsFor(source)).outcome, study::TextRepairOutcome::AlreadyWhole)
      << "a second run changes nothing";
}

TEST(TextRepair, AnUnlocatedRowIsUnresolvable) {
  Source source;
  study::TextRepairInputs in = inputsFor(source);
  in.units = nullptr;
  const auto plan = study::planTextRepair(snippetOnlyRow(source, "Cuatro"), in);
  EXPECT_EQ(plan.outcome, study::TextRepairOutcome::Unresolvable);
  EXPECT_TRUE(plan.wholeText.empty());
}

TEST(TextRepair, AnEmptyExtractionIsUnresolvable) {
  Source source;
  source.emptyText = true;
  EXPECT_EQ(study::planTextRepair(snippetOnlyRow(source, "Cuatro"), inputsFor(source)).outcome,
            study::TextRepairOutcome::Unresolvable);
}

TEST(TextRepair, AStartNotInTheDocumentIsUnresolvable) {
  Source source;
  study::TaggedPassage row = snippetOnlyRow(source, "Cuatro");
  row.start = study::Unit{study::UnitKind::Verse, 0, 9, 9, 0};
  EXPECT_EQ(study::planTextRepair(row, inputsFor(source)).outcome, study::TextRepairOutcome::Unresolvable);
}

TEST(TextRepair, AFingerprintMismatchIsNeverRebuilt) {
  Source source;
  study::TaggedPassage row = snippetOnlyRow(source, "Cuatro");
  row.fingerprint = study::Fingerprint{5, 0x1234u};
  const auto plan = study::planTextRepair(row, inputsFor(source));
  EXPECT_EQ(plan.outcome, study::TextRepairOutcome::FingerprintMismatch)
      << "another edition's text at this address must never overwrite the stored one";
  EXPECT_TRUE(plan.wholeText.empty());
}

TEST(TextRepair, AMatchingFingerprintIsRebuilt) {
  Source source;
  study::TaggedPassage row = snippetOnlyRow(source, "Cuatro");
  row.fingerprint = study::fingerprintOf(unitText(&source, row.start));
  EXPECT_EQ(study::planTextRepair(row, inputsFor(source)).outcome, study::TextRepairOutcome::Rebuilt);
}

TEST(TextRepair, AWrongStartSnippetIsRebuiltAndFlaggedSuspect) {
  Source source;
  const auto plan = study::planTextRepair(snippetOnlyRow(source, "palabras que no aparecen"), inputsFor(source));
  EXPECT_EQ(plan.outcome, study::TextRepairOutcome::RebuiltSuspectStart);
  EXPECT_EQ(plan.wholeText, "Cuatro cinco. Seis siete.") << "the known limit: rebuilt from the stored range";
}

TEST(TextRepair, AVerseNumberLedSnippetIsNotFlagged) {
  Source source;
  const std::string snippet = "2\xE2\x80\xAF" "Cuatro cinco.* 3 Seis";
  EXPECT_EQ(study::planTextRepair(snippetOnlyRow(source, snippet), inputsFor(source)).outcome,
            study::TextRepairOutcome::Rebuilt);
}

TEST(TextRepair, NoBreakSpacesInAnOldSnippetAreNotFlagged) {
  EXPECT_TRUE(study::snippetOccursIn("Cuatro\xC2\xA0" "cinco.", "Cuatro cinco. Seis siete."));
}

TEST(TextRepair, NormalisingCollapsesWhitespaceAndCutsNothing) {
  EXPECT_EQ(study::normaliseWholeText("  a\n\nb\t c  "), "a b c");
  const std::string long_(5000, 'x');
  EXPECT_EQ(study::normaliseWholeText(long_), long_);
}

TEST(RepairSchedule, VisitsRowsNeedingRepairInOrder) {
  const study::RepairSchedule schedule;
  EXPECT_EQ(schedule.order({true, false, true}), (std::vector<size_t>{0, 2}));
}

TEST(RepairSchedule, AnAttemptedRowGoesBehindTheRest) {
  study::RepairSchedule schedule;
  schedule.markAttempted(0);
  EXPECT_EQ(schedule.order({true, true, true}), (std::vector<size_t>{1, 2, 0}));
}

TEST(RepairSchedule, ASlowFirstRowCannotStarveTheRowsAfterIt) {
  study::RepairSchedule schedule;
  const std::vector<bool> needs{true, true, true};
  // Each pass has budget for one row, and row 0 never gets repaired.
  std::vector<size_t> firstVisited;
  for (int pass = 0; pass < 3; ++pass) {
    const auto order = schedule.order(needs);
    ASSERT_FALSE(order.empty());
    firstVisited.push_back(order.front());
    schedule.markAttempted(order.front());
  }
  EXPECT_EQ(firstVisited, (std::vector<size_t>{0, 1, 2})) << "every row is reached within three passes";
}

TEST(RepairSchedule, NothingToRepairIsAnEmptyOrder) {
  const study::RepairSchedule schedule;
  EXPECT_TRUE(schedule.order({}).empty());
  EXPECT_TRUE(schedule.order({false, false}).empty());
}

}  // namespace
