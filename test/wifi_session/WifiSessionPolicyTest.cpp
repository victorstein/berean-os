// Host coverage for the exit decision behind WifiSession. The wrapper adds only
// the driver reads and the teardown calls.

#include <gtest/gtest.h>

#include "network/WifiSessionPolicy.h"

using WifiSessionPolicy::ExitAction;
using WifiSessionPolicy::onExit;

TEST(WifiSessionPolicy, NeverConnectedAndRadioOffDoesNothing) {
  EXPECT_EQ(onExit(false, false, false), ExitAction::Nothing);
}

TEST(WifiSessionPolicy, RadioThisSessionTurnedOnIsEnded) {
  EXPECT_EQ(onExit(false, false, true), ExitAction::EndSession);
}

TEST(WifiSessionPolicy, ConnectedAtExitWithRadioOffDoesNothing) {
  EXPECT_EQ(onExit(false, true, false), ExitAction::Nothing);
}

TEST(WifiSessionPolicy, ConnectionThisSessionMadeIsEnded) {
  EXPECT_EQ(onExit(false, true, true), ExitAction::EndSession);
}

TEST(WifiSessionPolicy, ConnectionLostAndRadioOffDoesNothing) {
  EXPECT_EQ(onExit(true, false, false), ExitAction::Nothing);
}

// The Wi-Fi picker drops an existing association before it scans; backing out
// of it leaves the radio on and unconnected, which must not be kept.
TEST(WifiSessionPolicy, ConnectionDroppedDuringSessionEndsTheHalfUpRadio) {
  EXPECT_EQ(onExit(true, false, true), ExitAction::EndSession);
}

TEST(WifiSessionPolicy, ConnectedThroughoutWithRadioOffLeavesIt) {
  EXPECT_EQ(onExit(true, true, false), ExitAction::LeaveConnected);
}

TEST(WifiSessionPolicy, ConnectionFoundOnEntryAndStillUpIsLeftAlone) {
  EXPECT_EQ(onExit(true, true, true), ExitAction::LeaveConnected);
}
