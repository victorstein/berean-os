// Host coverage for the shared .tmp adoption decision.
//
// This suite drives the pure branch logic -- which file wins, and what status
// the caller is told. The Storage call sequence that carries each decision out
// in readDocFromFileAdopting is covered against the in-memory card fake in
// test/storage_io/.

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

TEST(TempAdoptionAction, MissingPrimaryWithAnUnparseableTempIsKeptAndReportedEmpty) {
  EXPECT_EQ(tempAdoptionAction(DocReadStatus::Missing, true, false), TempAdoptionAction::KeepTempReportEmpty);
}

TEST(AdoptedReadStatus, UsableDocumentsReportOk) {
  EXPECT_EQ(adoptedReadStatus(DocReadStatus::Ok, TempAdoptionAction::UseLoaded), DocReadStatus::Ok);
  EXPECT_EQ(adoptedReadStatus(DocReadStatus::Missing, TempAdoptionAction::PromoteTempAndUseIt), DocReadStatus::Ok)
      << "a promoted .tmp is a successful read -- this is the defect #51 is about";
}

TEST(AdoptedReadStatus, NothingOnDiskReportsMissing) {
  EXPECT_EQ(adoptedReadStatus(DocReadStatus::Missing, TempAdoptionAction::ReportEmpty), DocReadStatus::Missing);
}

TEST(AdoptedReadStatus, AnUnusableTempStillReportsMissing) {
  // The .tmp is kept, but the primary is genuinely absent, so overwriting it
  // loses nothing.
  EXPECT_EQ(adoptedReadStatus(DocReadStatus::Missing, TempAdoptionAction::KeepTempReportEmpty), DocReadStatus::Missing);
}

TEST(AdoptedReadStatus, PreservesUnreadable) {
  EXPECT_EQ(adoptedReadStatus(DocReadStatus::Unreadable, TempAdoptionAction::ReportFailed), DocReadStatus::Unreadable);
}

TEST(AdoptedReadStatus, PreservesParseError) {
  EXPECT_EQ(adoptedReadStatus(DocReadStatus::ParseError, TempAdoptionAction::ReportFailed), DocReadStatus::ParseError);
}

TEST(AdoptedReadStatus, ANonMissingPrimaryIsNeverReportedMissing) {
  // The DocReadStatus.h:6-7 contract, exhaustively. Missing is the ONLY status
  // that tells a caller "safe to overwrite"; a read that invents it over a file
  // whose bytes are still on the card is how data gets destroyed.
  constexpr DocReadStatus every[] = {DocReadStatus::Ok, DocReadStatus::Missing, DocReadStatus::Unreadable,
                                     DocReadStatus::ParseError};
  for (const DocReadStatus primary : every) {
    for (const bool tempExists : {false, true}) {
      for (const bool tempParsed : {false, true}) {
        const DocReadStatus reported = adoptedReadStatus(primary, tempAdoptionAction(primary, tempExists, tempParsed));
        if (primary != DocReadStatus::Missing) {
          EXPECT_NE(reported, DocReadStatus::Missing)
              << "primary=" << static_cast<int>(primary) << " exists=" << tempExists << " parsed=" << tempParsed;
        }
        if (primary == DocReadStatus::Ok) {
          // Braced deliberately: gtest's EXPECT_* expands to an if/else, so an
          // unbraced body trips GCC's -Wdangling-else (clang does not warn).
          EXPECT_EQ(reported, DocReadStatus::Ok);
        }
      }
    }
  }
}

TEST(AdoptedLoad, AnAcceptedDocumentIsLoadedOrRecovered) {
  EXPECT_EQ(adoptedLoad(TempAdoptionAction::UseLoaded, true), AdoptedLoad::Loaded);
  EXPECT_EQ(adoptedLoad(TempAdoptionAction::PromoteTempAndUseIt, true), AdoptedLoad::RecoveredFromTemp);
}

TEST(AdoptedLoad, ARejectedDocumentFails) {
  EXPECT_EQ(adoptedLoad(TempAdoptionAction::UseLoaded, false), AdoptedLoad::Failed);
  EXPECT_EQ(adoptedLoad(TempAdoptionAction::PromoteTempAndUseIt, false), AdoptedLoad::Failed)
      << "a promoted .tmp the store rejects must still latch saving off";
}

TEST(AdoptedLoad, NothingUsableOnDiskIsEmpty) {
  for (const bool accepted : {false, true}) {
    EXPECT_EQ(adoptedLoad(TempAdoptionAction::ReportEmpty, accepted), AdoptedLoad::Empty);
    EXPECT_EQ(adoptedLoad(TempAdoptionAction::KeepTempReportEmpty, accepted), AdoptedLoad::Empty);
  }
}

TEST(AdoptedLoad, AnUnreadablePrimaryFails) {
  for (const bool accepted : {false, true}) {
    EXPECT_EQ(adoptedLoad(TempAdoptionAction::ReportFailed, accepted), AdoptedLoad::Failed);
  }
}

TEST(AdoptedLoad, NeverReportsEmptyOverAPresentPrimaryOrSuccessForARejectedDocument) {
  // Empty is the only result that tells a caller "safe to save over"; inventing
  // it over a primary whose bytes are still on the card is how data is lost.
  constexpr DocReadStatus every[] = {DocReadStatus::Ok, DocReadStatus::Missing, DocReadStatus::Unreadable,
                                     DocReadStatus::ParseError};
  for (const DocReadStatus primary : every) {
    for (const bool tempExists : {false, true}) {
      for (const bool tempParsed : {false, true}) {
        for (const bool accepted : {false, true}) {
          const AdoptedLoad result = adoptedLoad(tempAdoptionAction(primary, tempExists, tempParsed), accepted);
          if (primary != DocReadStatus::Missing) {
            EXPECT_NE(result, AdoptedLoad::Empty)
                << "primary=" << static_cast<int>(primary) << " exists=" << tempExists << " parsed=" << tempParsed;
          }
          if (!accepted) {
            EXPECT_NE(result, AdoptedLoad::Loaded);
            EXPECT_NE(result, AdoptedLoad::RecoveredFromTemp);
          }
          if (primary == DocReadStatus::Ok && accepted) {
            EXPECT_EQ(result, AdoptedLoad::Loaded);
          }
        }
      }
    }
  }
}
