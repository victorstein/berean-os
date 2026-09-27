#pragma once

#include <cstdint>

// What a WifiSession does when it closes. Pure: no Arduino, so the host suite
// exercises it directly. WifiSession reads the driver and carries the answer out.
namespace WifiSessionPolicy {

enum class ExitAction : uint8_t { EndSession, LeaveConnected, Nothing };

// A connection is left alone only if it was there on entry and still is. The
// Wi-Fi picker drops any existing association before it scans or connects, so
// "connected on entry" alone would keep a half-up radio -- and power saving off
// -- behind a backed-out picker.
constexpr ExitAction onExit(const bool connectedOnEntry, const bool connectedAtExit, const bool radioOnAtExit) {
  if (connectedOnEntry && connectedAtExit) return ExitAction::LeaveConnected;
  return radioOnAtExit ? ExitAction::EndSession : ExitAction::Nothing;
}

}  // namespace WifiSessionPolicy
