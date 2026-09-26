#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>
#include <SdPaths.h>

#include <optional>
#include <string>
#include <vector>

#include "WifiCredential.h"

struct WifiCredentialSummary {
  std::string ssid;
  bool hasPassword = false;
  bool isLastConnected = false;
};

/**
 * Singleton class for storing WiFi credentials on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON (not cryptographically secure,
 * but prevents casual reading and ties credentials to the specific device).
 */
class WifiCredentialStore : public PersistableStore<WifiCredentialStore> {
 private:
  std::vector<WifiCredential> credentials;
  std::string lastConnectedSsid;
  // Protects the in-memory strings independently of PersistableStore's file
  // serialization mutex. Readers only hold this briefly and never wait on SD
  // I/O; saveToFileAtomic() snapshots under this mutex from toJson().
  mutable std::mutex credentialMutex;

  static constexpr size_t MAX_NETWORKS = 8;
  static constexpr size_t MAX_PASSWORD_LENGTH = 64;

  // Private constructor for singleton
  WifiCredentialStore() = default;

  friend class PersistableStore<WifiCredentialStore>;

 public:
  // 8 networks x (ssid <= 32 B, base64 of a <= 64 B password, two integers).
  // ~1,700 B worst case. The headroom absorbs a password written through the web
  // server, which does not bound it (CrossPointWebServer.cpp:1385); the load path
  // discards anything over MAX_PASSWORD_LENGTH on the next boot (:52-56).
  static constexpr size_t SAVE_BUDGET = 8192;

  static const char* getFilePath() { return sdpaths::WIFI_FILE; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Credential management
  // Why a credential edit did not stick. Only SaveFailed is a storage problem;
  // the others are the caller's input, so a caller can tell them apart.
  enum class EditResult : uint8_t { Ok, NotFound, LimitReached, SaveFailed };

  // Every edit is all or nothing: a failed save rolls the in-memory list back,
  // so a later unrelated save cannot persist what the user was told failed.
  EditResult addCredential(const std::string& ssid, const std::string& password);
  EditResult removeCredential(const std::string& ssid);
  // Edits the saved network `oldSsid`, possibly renaming it. All or nothing: a
  // failed save leaves the old entry in memory and on the card.
  EditResult updateCredential(const std::string& oldSsid, const std::string& ssid, const std::string& password);
  std::optional<WifiCredential> findCredential(const std::string& ssid) const;
  std::optional<WifiCredential> getCredentialAt(size_t index) const;
  std::optional<std::string> getSsidAt(size_t index) const;
  size_t getCredentialCount() const;
  // Password-free snapshot for display/API consumers.
  std::vector<WifiCredentialSummary> getCredentialSummaries() const;

  // Check if a network is saved
  bool hasSavedCredential(const std::string& ssid) const;

  // Last connected network
  void setLastConnectedSsid(const std::string& ssid);
  std::string getLastConnectedSsid() const;
  void clearLastConnectedSsid();

  // Clear all credentials
  void clearAll();
};

// Helper macro to access credentials store
#define WIFI_STORE WifiCredentialStore::getInstance()
