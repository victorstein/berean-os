#pragma once

#include <cstdint>

namespace input {

enum class ResetKind : uint8_t { PowerOn, DeepSleep, Unknown, Other };
enum class WakeCause : uint8_t { Undefined, GpioOrExt1, Other };
enum class WakeupClass : uint8_t { PowerButton, AfterFlash, AfterUSBPower, Other };

// usbCanWakeFromOff is false where a Power press from fully off also boots as
// POWERON with USB attached (X4 Pro, #185), so USB presence there says nothing
// about what caused the boot.
inline WakeupClass classifyWakeup(const ResetKind reset, const WakeCause cause, const bool usbConnected,
                                  const bool usbCanWakeFromOff) {
  const bool usbCausedPowerOn = usbConnected && usbCanWakeFromOff;
  if (reset == ResetKind::DeepSleep && cause == WakeCause::GpioOrExt1) {
    return WakeupClass::PowerButton;
  }
  if (cause == WakeCause::Undefined && reset == ResetKind::PowerOn && !usbCausedPowerOn) {
    return WakeupClass::PowerButton;
  }
  if (cause == WakeCause::Undefined && reset == ResetKind::Unknown && usbConnected) {
    return WakeupClass::AfterFlash;
  }
  if (cause == WakeCause::Undefined && reset == ResetKind::PowerOn && usbCausedPowerOn) {
    return WakeupClass::AfterUSBPower;
  }
  return WakeupClass::Other;
}

}  // namespace input
