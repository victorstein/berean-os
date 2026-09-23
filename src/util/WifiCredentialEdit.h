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

// Editing a saved network may change its SSID. Applied as one in-memory change
// so a failed save can put back exactly what was there: before this, the web
// server removed the old entry and then added the new one as two saves, and a
// failed add left the old network deleted.
struct WifiCredentialRename {
  bool applied = false;
  bool removedOld = false;  // false when the SSID did not change
  size_t removedIndex = 0;
  WifiCredential removed;
  WifiCredentialEdit upsert;
};

inline WifiCredentialRename renameCredential(std::vector<WifiCredential>& credentials, const std::string& oldSsid,
                                             const std::string& newSsid, const std::string& password,
                                             const size_t maxNetworks) {
  WifiCredentialRename rename;
  const auto old = std::find_if(credentials.begin(), credentials.end(),
                                [&oldSsid](const WifiCredential& cred) { return cred.ssid == oldSsid; });
  if (old == credentials.end()) return rename;

  if (oldSsid != newSsid) {
    rename.removedOld = true;
    rename.removedIndex = static_cast<size_t>(old - credentials.begin());
    rename.removed = std::move(*old);
    credentials.erase(old);
  }
  // Cannot be rejected: either the SSID is already present, or the old entry
  // just freed the slot this one takes.
  rename.upsert = upsertCredential(credentials, newSsid, password, maxNetworks);
  rename.applied = true;
  return rename;
}

inline void undoCredentialRename(std::vector<WifiCredential>& credentials, const WifiCredentialRename& rename) {
  if (!rename.applied) return;
  undoCredentialEdit(credentials, rename.upsert);
  if (!rename.removedOld) return;
  const size_t index = std::min(rename.removedIndex, credentials.size());
  credentials.insert(credentials.begin() + static_cast<std::ptrdiff_t>(index), rename.removed);
}
