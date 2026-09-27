#pragma once

#include <cstdint>

// The boot-time decisions setup() makes, free of firmware includes so they can
// be tested on the host. setup() reads the inputs and performs the side effects.

constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

struct SilentReboot {
  bool silent;
  uint32_t target;
};

// RTC_NOINIT memory is uninitialised on cold boot, so the target is bounded too.
inline SilentReboot decodeSilentReboot(const uint32_t magic, const uint32_t target) {
  const bool silent = magic == SILENT_REBOOT_MAGIC;
  return {silent, (silent && target <= SILENT_REBOOT_TARGET_READER) ? target : 0};
}

// Only a verified deep-sleep wake may use the one-shot persisted flag;
// otherwise a stale flag could suppress the splash on a cold boot.
inline BootResume resolveBootResume(const bool isSilentReboot, const bool isSleepWake, const bool showBootScreen) {
  return isSilentReboot                   ? BootResume::Silent
         : isSleepWake && !showBootScreen ? BootResume::SplashlessWake
                                          : BootResume::Splash;
}

enum class BootRoute : uint8_t {
  RecoveryFirmware,
  CrashReport,
  SilentReader,
  SilentHome,
  Home,
  ResumeReader,
};

struct BootRouteInputs {
  bool recoveryFirmwareMode;
  bool rebootedFromPanic;
  BootResume resume;
  uint32_t snapshotTarget;
  bool openBookEmpty;
  bool lastSleepFromReader;
  bool backHeld;
  bool readerCrashedLastTime;  // readerActivityLoadCount > 0
};

inline BootRoute chooseBootRoute(const BootRouteInputs& in) {
  if (in.recoveryFirmwareMode) return BootRoute::RecoveryFirmware;
  if (in.rebootedFromPanic) return BootRoute::CrashReport;
  if (in.resume == BootResume::Silent && in.snapshotTarget == SILENT_REBOOT_TARGET_READER && !in.openBookEmpty) {
    return BootRoute::SilentReader;
  }
  // A silent reboot never falls through to the sleep-wake resume below, which
  // would fire on a stale open book and lastSleepFromReader from a prior session.
  if (in.resume == BootResume::Silent) return BootRoute::SilentHome;
  if (in.openBookEmpty || !in.lastSleepFromReader || in.backHeld || in.readerCrashedLastTime) {
    return BootRoute::Home;
  }
  return BootRoute::ResumeReader;
}
