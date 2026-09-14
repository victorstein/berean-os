#include <gtest/gtest.h>

#include "StudyStore/Unit.h"

namespace {

TEST(UnitOrdering, VerseSortsByBookThenChapterThenVerseThenOffset) {
  const study::Unit a{study::UnitKind::Verse, 40, 1, 8, 0};
  const study::Unit b{study::UnitKind::Verse, 40, 1, 9, 0};
  const study::Unit c{study::UnitKind::Verse, 40, 2, 1, 0};
  const study::Unit d{study::UnitKind::Verse, 41, 1, 1, 0};
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b < c);
  EXPECT_TRUE(c < d);
  EXPECT_FALSE(d < a);
}

TEST(UnitOrdering, SameUnitOrdersByOffset) {
  const study::Unit a{study::UnitKind::Verse, 40, 1, 8, 4};
  const study::Unit b{study::UnitKind::Verse, 40, 1, 8, 12};
  EXPECT_TRUE(a < b);
}

// The whole reason `book` exists: the Bible's pubkey carries no language, and
// the document filename that would otherwise name the book differs between the
// Spanish and English NWT. Two renderings of one verse must compare equal.
TEST(UnitIdentity, TheSameVerseInTwoLanguagesIsOneAddress) {
  const study::Unit salmos{study::UnitKind::Verse, 19, 119, 145, 0};
  const study::Unit psalm{study::UnitKind::Verse, 19, 119, 145, 0};
  EXPECT_EQ(salmos, psalm);
}

TEST(UnitIdentity, TheSameChapterAndVerseInDifferentBooksAreNotEqual) {
  const study::Unit genesis{study::UnitKind::Verse, 1, 1, 1, 0};
  const study::Unit matthew{study::UnitKind::Verse, 40, 1, 1, 0};
  EXPECT_NE(genesis, matthew) << "without `book` these collide and a mark lands in the wrong book";
}

// data-pid values run out of document order in 46% of the documents that carry
// them, so the pid is an identifier and must never order two paragraphs.
TEST(UnitOrdering, ParagraphDoesNotClaimAnOrderingByPid) {
  const study::Unit later{study::UnitKind::Paragraph, 0, 0, 7, 0};
  const study::Unit earlier{study::UnitKind::Paragraph, 0, 0, 40, 0};
  EXPECT_FALSE(study::orderableByAddress(later, earlier));
}

TEST(UnitOrdering, IsConsistentWithEqualityAcrossKinds) {
  const study::Unit doc{study::UnitKind::DocumentOffset, 0, 0, 0, 5};
  const study::Unit verse{study::UnitKind::Verse, 1, 1, 1, 5};
  EXPECT_NE(doc, verse);
  EXPECT_TRUE((doc < verse) || (verse < doc)) << "unequal units must be ordered, or std::set silently merges them";
}

TEST(UnitRoundTrip, EncodesAndDecodesEachKind) {
  for (const study::Unit u :
       {study::Unit{study::UnitKind::Verse, 19, 119, 145, 3}, study::Unit{study::UnitKind::Paragraph, 0, 0, 40, 0},
        study::Unit{study::UnitKind::DocumentOffset, 0, 0, 0, 1255}}) {
    EXPECT_EQ(study::unitFromCompact(study::unitToCompact(u)), u);
  }
}

TEST(UnitRoundTrip, RejectsAnUnknownKind) { EXPECT_FALSE(study::unitFromCompact("z:1:2:3:4").has_value()); }

TEST(UnitRoundTrip, RejectsABookOutsideTheCanon) { EXPECT_FALSE(study::unitFromCompact("v:67:1:1:0").has_value()); }

}  // namespace
