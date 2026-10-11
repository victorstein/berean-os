#include <gtest/gtest.h>

#include "study/StudyReuse.h"

// A warm open keeps StudyStore's state only when a fresh openPublication would build the same
// state: same book, a clean last load, and a pubkey the registry still resolves the same way.

namespace {

int resolveCalls = 0;
bool keyResolves = true;

bool keyStillResolves(void*) {
  ++resolveCalls;
  return keyResolves;
}

study_reuse::Inputs reusable() {
  study_reuse::Inputs in;
  in.openAndReady = true;
  in.sameBook = true;
  in.loadedCleanly = true;
  in.keyStable = true;
  return in;
}

class StudyReuse : public ::testing::Test {
 protected:
  void SetUp() override {
    resolveCalls = 0;
    keyResolves = true;
  }
};

}  // namespace

TEST_F(StudyReuse, AStableKeyOpenForTheSameBookIsReusedWithoutAskingTheRegistry) {
  EXPECT_TRUE(study_reuse::mayReuse(reusable(), &keyStillResolves, nullptr));
  EXPECT_EQ(resolveCalls, 0);
}

TEST_F(StudyReuse, AStoreNotOpenOrNotReadyIsNotReused) {
  study_reuse::Inputs in = reusable();
  in.openAndReady = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
}

TEST_F(StudyReuse, AStoreOpenForAnotherBookIsNotReused) {
  study_reuse::Inputs in = reusable();
  in.sameBook = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
}

TEST_F(StudyReuse, AFailedPaletteOrPassagesReadIsRetriedNotReused) {
  study_reuse::Inputs in = reusable();
  in.loadedCleanly = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
}

TEST_F(StudyReuse, AnUnstableKeyIsReusedOnlyWhileItStillResolvesTheSame) {
  study_reuse::Inputs in = reusable();
  in.keyStable = false;
  EXPECT_TRUE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
  keyResolves = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
  EXPECT_EQ(resolveCalls, 2);
}

TEST_F(StudyReuse, TheRegistryIsNotAskedWhenAnotherConditionAlreadyFails) {
  study_reuse::Inputs in = reusable();
  in.keyStable = false;
  in.sameBook = false;
  EXPECT_FALSE(study_reuse::mayReuse(in, &keyStillResolves, nullptr));
  EXPECT_EQ(resolveCalls, 0);
}
