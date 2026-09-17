// Host coverage for the shared .tmp adoption decision.
//
// PersistableStore.cpp cannot be built on the host: PersistableStore.h:3
// includes <Arduino.h> unconditionally, and test/stubs/HalStorage.h is a bare
// HalFile with no Storage singleton. So this suite drives the pure branch
// logic -- which file wins, and what status the caller is told -- and the
// Storage call sequence in readDocFromFileAdopting stays device-verified only.

#include <gtest/gtest.h>

#include "TempAdoption.h"

TEST(TempAdoptionAction, PrimaryOkAlwaysUsesTheLoadedDoc) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Ok, false, false), TempAdoptionAction::UseLoaded);
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Ok, false, true), TempAdoptionAction::UseLoaded);
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Ok, true, false), TempAdoptionAction::UseLoaded);
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Ok, true, true), TempAdoptionAction::UseLoaded)
      << "a parseable .tmp must never displace a primary that read fine";
}

TEST(TempAdoptionAction, PrimaryUnreadableAlwaysFailsRegardlessOfTemp) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Unreadable, false, false), TempAdoptionAction::ReportFailed);
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Unreadable, true, true), TempAdoptionAction::ReportFailed);
}

TEST(TempAdoptionAction, PrimaryParseErrorAlwaysFailsRegardlessOfTemp) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::ParseError, false, false), TempAdoptionAction::ReportFailed);
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::ParseError, true, true), TempAdoptionAction::ReportFailed);
}

TEST(TempAdoptionAction, MissingPrimaryWithNoTempIsGenuinelyEmpty) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Missing, false, false), TempAdoptionAction::ReportEmpty);
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Missing, false, true), TempAdoptionAction::ReportEmpty)
      << "tempParsed is meaningless when no .tmp exists";
}

TEST(TempAdoptionAction, MissingPrimaryWithAParsedTempIsPromoted) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Missing, true, true), TempAdoptionAction::PromoteTempAndUseIt);
}

TEST(TempAdoptionAction, MissingPrimaryWithAnUnparseableTempIsDiscarded) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Missing, true, false), TempAdoptionAction::DeleteTempReportEmpty);
}
