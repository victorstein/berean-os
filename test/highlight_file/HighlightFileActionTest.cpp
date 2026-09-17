// Host coverage for HighlightFile's save budget check.
//
// HighlightFile.cpp itself cannot be built on the host: it includes
// <PersistableStore.h>, which includes <Arduino.h> unconditionally, and
// Arduino.h reaches FreeRTOS, esp32-hal and other ESP32-only headers that have
// no stub in test/stubs and are not worth faking convincingly.
//
// The load-side decision this suite used to cover moved to
// test/temp_adoption/ when the rule became shared with PersistableStore.

#include <gtest/gtest.h>

#include "util/HighlightFileAction.h"

TEST(HighlightSaveAction, WritesWhenAtOrUnderBudget) {
  EXPECT_EQ(highlightSaveAction(0, 45000), HighlightSaveAction::Write);
  EXPECT_EQ(highlightSaveAction(44999, 45000), HighlightSaveAction::Write);
  EXPECT_EQ(highlightSaveAction(45000, 45000), HighlightSaveAction::Write) << "the budget itself must still fit";
}

TEST(HighlightSaveAction, RefusesOneByteOverBudget) {
  EXPECT_EQ(highlightSaveAction(45001, 45000), HighlightSaveAction::RefuseTooLarge);
}

TEST(HighlightSaveAction, RefusesTheDocumentedWorstCase) {
  // HighlightDocTest.WorstCaseDocumentStaysUnderTheSaveBudget records the
  // real worst case as > 50000 bytes; this is the guard that must catch it.
  EXPECT_EQ(highlightSaveAction(90733, 45000), HighlightSaveAction::RefuseTooLarge);
}
