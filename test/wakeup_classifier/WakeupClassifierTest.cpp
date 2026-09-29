#include <gtest/gtest.h>

#include "Input/WakeupClassifier.h"

namespace {

using input::classifyWakeup;
using input::ResetKind;
using input::WakeCause;
using input::WakeupClass;

constexpr bool USB = true;
constexpr bool NO_USB = false;
constexpr bool OTHER_BOARD = true;  // usbCanWakeFromOff
constexpr bool X4_PRO = false;

TEST(ClassifyWakeup, DeepSleepGpioWakeIsPowerButtonWhateverUsb) {
  for (const bool usb : {USB, NO_USB}) {
    for (const bool board : {OTHER_BOARD, X4_PRO}) {
      EXPECT_EQ(classifyWakeup(ResetKind::DeepSleep, WakeCause::GpioOrExt1, usb, board), WakeupClass::PowerButton);
    }
  }
}

TEST(ClassifyWakeup, PowerOnWithoutUsbIsPowerButton) {
  EXPECT_EQ(classifyWakeup(ResetKind::PowerOn, WakeCause::Undefined, NO_USB, OTHER_BOARD), WakeupClass::PowerButton);
  EXPECT_EQ(classifyWakeup(ResetKind::PowerOn, WakeCause::Undefined, NO_USB, X4_PRO), WakeupClass::PowerButton);
}

TEST(ClassifyWakeup, PowerOnWithUsbIsAfterUsbPowerOnOtherBoards) {
  EXPECT_EQ(classifyWakeup(ResetKind::PowerOn, WakeCause::Undefined, USB, OTHER_BOARD), WakeupClass::AfterUSBPower);
}

// Bench, #185: a Power press from fully off with USB attached boots as
// POWERON, cause 0. Treating it as a USB plug would put it back to sleep.
TEST(ClassifyWakeup, PowerOnWithUsbIsPowerButtonOnX4Pro) {
  EXPECT_EQ(classifyWakeup(ResetKind::PowerOn, WakeCause::Undefined, USB, X4_PRO), WakeupClass::PowerButton);
}

TEST(ClassifyWakeup, UnknownResetWithUsbIsAfterFlash) {
  EXPECT_EQ(classifyWakeup(ResetKind::Unknown, WakeCause::Undefined, USB, OTHER_BOARD), WakeupClass::AfterFlash);
  EXPECT_EQ(classifyWakeup(ResetKind::Unknown, WakeCause::Undefined, USB, X4_PRO), WakeupClass::AfterFlash);
}

TEST(ClassifyWakeup, UnknownResetWithoutUsbIsOther) {
  EXPECT_EQ(classifyWakeup(ResetKind::Unknown, WakeCause::Undefined, NO_USB, OTHER_BOARD), WakeupClass::Other);
}

TEST(ClassifyWakeup, DeepSleepWithoutGpioCauseIsOther) {
  EXPECT_EQ(classifyWakeup(ResetKind::DeepSleep, WakeCause::Undefined, NO_USB, OTHER_BOARD), WakeupClass::Other);
  EXPECT_EQ(classifyWakeup(ResetKind::DeepSleep, WakeCause::Other, USB, X4_PRO), WakeupClass::Other);
}

// ESP_RST_USB, ESP_RST_JTAG, ESP_RST_SW and the rest map to ResetKind::Other.
TEST(ClassifyWakeup, OtherResetIsOtherEvenWithUsb) {
  EXPECT_EQ(classifyWakeup(ResetKind::Other, WakeCause::Undefined, USB, OTHER_BOARD), WakeupClass::Other);
  EXPECT_EQ(classifyWakeup(ResetKind::Other, WakeCause::Undefined, USB, X4_PRO), WakeupClass::Other);
}

}  // namespace
