#pragma once

// Leaves Wi-Fi as it found it. Opened when a screen that may bring Wi-Fi up is
// entered; on destruction it ends the session unless a connection that was up
// on entry is still up (WifiSessionPolicy). Hold it as a std::optional member
// declared last and emplace() it in onEnter(): the activity is built at the
// launch site and may be destroyed without ever being entered. A screen that
// never uses the link itself may instead emplace() it around a single child
// launch and reset() it in that launch's result handler.
class WifiSession {
 public:
  WifiSession();
  ~WifiSession();

  WifiSession(const WifiSession&) = delete;
  WifiSession& operator=(const WifiSession&) = delete;
  WifiSession(WifiSession&&) = delete;
  WifiSession& operator=(WifiSession&&) = delete;

 private:
  bool connectedOnEntry;
};
