#include <gtest/gtest.h>

#include "StudyStore/MigrationPlanner.h"

namespace {

study::DocumentUnits psalm119() {
  study::DocumentUnits u;
  u.kind = study::UnitKind::Verse;
  u.book = 19;
  u.anchors = {{0, 119, 144}, {100, 119, 145}, {220, 119, 146}};
  return u;
}

study::LegacyHighlight legacy(const uint16_t spine, const uint32_t start, const uint32_t end, const char* ref,
                              std::vector<std::string> tags) {
  study::LegacyHighlight h;
  h.spineIndex = spine;
  h.start = start;
  h.end = end;
  h.reference = ref;
  h.tagNames = std::move(tags);
  h.snippet = "Te he llamado con todo el corazon";
  return h;
}

std::string fixedText(void*, const study::Unit&) { return std::string("Te he llamado con todo el corazon"); }

study::MigrationInputs inputs() {
  study::MigrationInputs in;
  in.pubKey = "bible";
  in.document = "1001061130-split10.xhtml";
  in.units = psalm119();
  in.unitText = &fixedText;
  return in;
}

TEST(MigrationPlanner, ResolvesALegacyOffsetToItsVerse) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"oracion"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->start.kind, study::UnitKind::Verse);
  EXPECT_EQ(out.passage->start.book, 19);
  EXPECT_EQ(out.passage->start.major, 119);
  EXPECT_EQ(out.passage->start.minor, 145);
  EXPECT_EQ(out.passage->start.offset, 8u) << "offset is relative to the verse, not the document";
  EXPECT_EQ(out.outcome, study::MigrationOutcome::Resolved);
}

TEST(MigrationPlanner, AgreesWithTheStoredReferenceAndSaysSo) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"oracion"}));
  EXPECT_TRUE(out.referenceAgrees);
  EXPECT_EQ(out.resolvedReference, "119:145");
}

TEST(MigrationPlanner, FlagsADisagreementWithTheStoredReferenceWithoutDroppingThePassage) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:200", {"oracion"}));
  ASSERT_TRUE(out.passage.has_value()) << "a disagreement is reported, never silently discarded";
  EXPECT_FALSE(out.referenceAgrees);
  EXPECT_EQ(out.outcome, study::MigrationOutcome::ResolvedReferenceMismatch);
}

TEST(MigrationPlanner, IgnoresTheBookNameWhenComparingReferences) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Sal. 119:145", {"fe"}));
  EXPECT_TRUE(out.referenceAgrees);
}

TEST(MigrationPlanner, HandlesAReferenceWithANumberedBookName) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "1 Juan 119:145", {"fe"}));
  EXPECT_TRUE(out.referenceAgrees) << "the leading book number must not be read as the chapter";
}

TEST(MigrationPlanner, DegradesToDocumentOffsetWhenTheDocumentHasNoUnits) {
  study::MigrationInputs in = inputs();
  in.units = study::DocumentUnits{};
  const auto out = study::planMigration(in, legacy(198, 108, 180, "", {"fe"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->start.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(out.passage->start.offset, 108u);
  EXPECT_EQ(out.outcome, study::MigrationOutcome::ResolvedDocumentOffset);
}

TEST(MigrationPlanner, MarksAPassagePendingWhenTheSourceBookWasUnavailable) {
  study::MigrationInputs in = inputs();
  in.sourceAvailable = false;
  const auto out = study::planMigration(in, legacy(198, 108, 180, "Salmos 119:145", {"fe"}));
  ASSERT_TRUE(out.passage.has_value()) << "an absent EPUB must never cost the user a passage";
  EXPECT_TRUE(out.passage->pendingUpgrade);
  EXPECT_EQ(out.passage->start.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(out.passage->start.offset, 108u) << "the original offset must survive for the later upgrade";
  EXPECT_EQ(out.outcome, study::MigrationOutcome::PendingUpgrade);
}

TEST(MigrationPlanner, CarriesTheReferenceAndSnippetThrough) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"oracion"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->reference, "Salmos 119:145");
  EXPECT_FALSE(out.passage->snippet.empty());
  EXPECT_EQ(out.passage->documentSpine, 198);
  EXPECT_EQ(out.passage->document, "1001061130-split10.xhtml");
}

TEST(MigrationPlanner, DropsAPassageThatCarriedNoTags) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {}));
  EXPECT_FALSE(out.passage.has_value());
  EXPECT_EQ(out.outcome, study::MigrationOutcome::DroppedNoTags);
}

TEST(MigrationPlanner, FingerprintsTheStartUnitsText) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"fe"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->fingerprint, study::fingerprintOf("Te he llamado con todo el corazon"));
}

TEST(MigrationPlanner, FingerprintsTheEndUnitWhenTheSpanCrossesOne) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 240, "Salmos 119:145", {"fe"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_NE(out.passage->end, out.passage->start);
  EXPECT_NE(out.passage->endFingerprint.length, 0u) << "a change in the end unit must be detectable too";
}

TEST(MigrationPlanner, AllocatesTagIdsThroughThePaletteSoNamesDedupeAcrossBooks) {
  study::TagPalette palette;
  study::MigrationInputs in = inputs();
  in.palette = &palette;

  const auto a = study::planMigration(in, legacy(198, 108, 180, "Salmos 119:145", {"fe", "amor"}));
  const auto b = study::planMigration(in, legacy(199, 10, 40, "Salmos 119:144", {"amor"}));
  ASSERT_TRUE(a.passage.has_value());
  ASSERT_TRUE(b.passage.has_value());
  EXPECT_EQ(a.passage->tags[1], b.passage->tags[0]) << "one name, one global id";
  EXPECT_EQ(palette.activeCount(), 2u);
}

TEST(MigrationPlanner, KeepsATagNameThatNoPassageUses) {
  // The user's palette has two: `igualdad` and `transformación`. Vocabulary the
  // user chose is not the migration's to discard.
  study::TagPalette palette;
  study::adoptTagNames(palette, {"igualdad", "transformación"});
  EXPECT_EQ(palette.activeCount(), 2u);
}

TEST(MigrationPlannerReferenceTail, ParsesTheRealShapesTheUsersDataContains) {
  EXPECT_EQ(study::referenceTail("Apocalipsis 1:8"), "1:8");
  EXPECT_EQ(study::referenceTail("1 Juan 4:20"), "4:20");
  EXPECT_EQ(study::referenceTail("Salmos 119:145"), "119:145");
  EXPECT_EQ(study::referenceTail("no colon here"), "");
  EXPECT_EQ(study::referenceTail(""), "");
}

}  // namespace
