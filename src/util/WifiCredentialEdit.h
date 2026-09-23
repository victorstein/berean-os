#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../WifiCredential.h"

// The in-memory half of WifiCredentialStore::addCredential, kept free of
// Arduino so it can be host-tested (WifiCredentialStore.cpp reaches Arduino.h
// through PersistableStore.h).
//
// addCredential edits the list, then saves. When the save fails, the edit must
// be undone: left in memory, the next unrelated save (setLastConnectedSsid,
// clearLastConnectedSsid) would persist a credential the user was told was not
// saved.
struct WifiCredentialEdit {
  enum class Kind : uint8_t { Rejected, Appended, Replaced };
  Kind kind = Kind::Rejected;
  std::string ssid;
  std::string previousPassword;  // meaningful for Replaced only
};

inline WifiCredentialEdit upsertCredential(std::vector<WifiCredential>& credentials, const std::string& ssid,
                                           const std::string& password, const size_t maxNetworks) {
  WifiCredentialEdit edit;
  edit.ssid = ssid;
  const auto existing = std::find_if(credentials.begin(), credentials.end(),
                                     [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (existing != credentials.end()) {
    edit.kind = WifiCredentialEdit::Kind::Replaced;
    edit.previousPassword = std::exchange(existing->password, password);
    return edit;
  }
  if (credentials.size() >= maxNetworks) return edit;
  credentials.push_back({ssid, password});
  edit.kind = WifiCredentialEdit::Kind::Appended;
  return edit;
}

// Finds the entry by SSID, not position: credentialMutex is released for the SD
// write between the edit and its undo, so another writer may have shifted the
// list.
inline void undoCredentialEdit(std::vector<WifiCredential>& credentials, const WifiCredentialEdit& edit) {
  if (edit.kind == WifiCredentialEdit::Kind::Rejected) return;
  const auto entry = std::find_if(credentials.begin(), credentials.end(),
                                  [&edit](const WifiCredential& cred) { return cred.ssid == edit.ssid; });
  if (entry == credentials.end()) return;
  if (edit.kind == WifiCredentialEdit::Kind::Appended) {
    credentials.erase(entry);
  } else {
    entry->password = edit.previousPassword;
  }
}
