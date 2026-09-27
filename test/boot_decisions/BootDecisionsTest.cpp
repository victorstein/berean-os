#include <gtest/gtest.h>

#include "boot/BootDecisions.h"

namespace {
// Every input set to the value that leads to ResumeReader; each test flips what it is about.
BootRouteInputs resumeReaderInputs() {
  return {/*recoveryFirmwareMode=*/false,
          /*rebootedFromPanic=*/false,
          /*resume=*/BootResume::Splash,
          /*snapshotTarget=*/SILENT_REBOOT_TARGET_HOME,
          /*openBookEmpty=*/false,
          /*lastSleepFromReader=*/true,
          /*backHeld=*/false,
          /*readerCrashedLastTime=*/false};
}
}  // namespace

TEST(DecodeSilentReboot, WrongMagicIsNotSilentAndTargetsHome) {
  const SilentReboot decoded = decodeSilentReboot(0, SILENT_REBOOT_TARGET_READER);
  EXPECT_FALSE(decoded.silent);
  EXPECT_EQ(decoded.target, SILENT_REBOOT_TARGET_HOME);
}

TEST(DecodeSilentReboot, MagicKeepsHomeTarget) {
  const SilentReboot decoded = decodeSilentReboot(SILENT_REBOOT_MAGIC, SILENT_REBOOT_TARGET_HOME);
  EXPECT_TRUE(decoded.silent);
  EXPECT_EQ(decoded.target, SILENT_REBOOT_TARGET_HOME);
}

TEST(DecodeSilentReboot, MagicKeepsReaderTarget) {
  const SilentReboot decoded = decodeSilentReboot(SILENT_REBOOT_MAGIC, SILENT_REBOOT_TARGET_READER);
  EXPECT_TRUE(decoded.silent);
  EXPECT_EQ(decoded.target, SILENT_REBOOT_TARGET_READER);
}

TEST(DecodeSilentReboot, OutOfRangeTargetFallsBackToHome) {
  EXPECT_EQ(decodeSilentReboot(SILENT_REBOOT_MAGIC, 2).target, SILENT_REBOOT_TARGET_HOME);
  EXPECT_EQ(decodeSilentReboot(SILENT_REBOOT_MAGIC, 0xFFFFFFFF).target, SILENT_REBOOT_TARGET_HOME);
}

TEST(ResolveBootResume, SilentRebootWinsOverSleepWake) {
  EXPECT_EQ(resolveBootResume(true, true, false), BootResume::Silent);
}

TEST(ResolveBootResume, SleepWakeWithSplashSuppressedIsSplashless) {
  EXPECT_EQ(resolveBootResume(false, true, false), BootResume::SplashlessWake);
}

TEST(ResolveBootResume, SleepWakeWithSplashArmedShowsSplash) {
  EXPECT_EQ(resolveBootResume(false, true, true), BootResume::Splash);
}

TEST(ResolveBootResume, ColdBootIgnoresStaleSplashFlag) {
  EXPECT_EQ(resolveBootResume(false, false, false), BootResume::Splash);
}

TEST(ChooseBootRoute, AllClearResumesReader) {
  EXPECT_EQ(chooseBootRoute(resumeReaderInputs()), BootRoute::ResumeReader);
}

TEST(ChooseBootRoute, RecoveryBeatsPanicAndSilentReboot) {
  BootRouteInputs in = resumeReaderInputs();
  in.recoveryFirmwareMode = true;
  in.rebootedFromPanic = true;
  in.resume = BootResume::Silent;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::RecoveryFirmware);
}

TEST(ChooseBootRoute, PanicBeatsSilentReboot) {
  BootRouteInputs in = resumeReaderInputs();
  in.rebootedFromPanic = true;
  in.resume = BootResume::Silent;
  in.snapshotTarget = SILENT_REBOOT_TARGET_READER;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::CrashReport);
}

TEST(ChooseBootRoute, SilentReaderTargetWithOpenBookReopensIt) {
  BootRouteInputs in = resumeReaderInputs();
  in.resume = BootResume::Silent;
  in.snapshotTarget = SILENT_REBOOT_TARGET_READER;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::SilentReader);
}

TEST(ChooseBootRoute, SilentReaderTargetWithoutOpenBookGoesHome) {
  BootRouteInputs in = resumeReaderInputs();
  in.resume = BootResume::Silent;
  in.snapshotTarget = SILENT_REBOOT_TARGET_READER;
  in.openBookEmpty = true;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::SilentHome);
}

TEST(ChooseBootRoute, SilentHomeTargetIgnoresStaleReaderState) {
  BootRouteInputs in = resumeReaderInputs();
  in.resume = BootResume::Silent;
  in.snapshotTarget = SILENT_REBOOT_TARGET_HOME;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::SilentHome);
}

TEST(ChooseBootRoute, NoOpenBookGoesHome) {
  BootRouteInputs in = resumeReaderInputs();
  in.openBookEmpty = true;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::Home);
}

TEST(ChooseBootRoute, LastSleepOutsideReaderGoesHome) {
  BootRouteInputs in = resumeReaderInputs();
  in.lastSleepFromReader = false;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::Home);
}

TEST(ChooseBootRoute, BackHeldGoesHome) {
  BootRouteInputs in = resumeReaderInputs();
  in.backHeld = true;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::Home);
}

TEST(ChooseBootRoute, ReaderCrashLastTimeGoesHome) {
  BootRouteInputs in = resumeReaderInputs();
  in.readerCrashedLastTime = true;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::Home);
}

TEST(ChooseBootRoute, SplashlessWakeCanResumeReader) {
  BootRouteInputs in = resumeReaderInputs();
  in.resume = BootResume::SplashlessWake;
  EXPECT_EQ(chooseBootRoute(in), BootRoute::ResumeReader);
}
