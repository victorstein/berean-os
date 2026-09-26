#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>

#include <mutex>
#include <string>

#include "DocReadStatus.h"
#include "FormatVersion.h"
#include "SaveBudget.h"
#include "TempAdoption.h"

/**
 * @brief Non-template core of PersistableStore.
 *
 * All ArduinoJson parse/serialize machinery is instantiated once here (in
 * PersistableStore.cpp) instead of in every store's translation unit. GCC
 * emits the JSON serializer/parser templates as local .isra clones per TU
 * (~0.5KB each), so keeping serializeJson/deserializeJson out of the stores
 * is what makes the abstraction flash-neutral.
 */
class PersistableStoreBase {
 protected:
  PersistableStoreBase() = default;
  ~PersistableStoreBase() = default;

  // Serializes saveToFile/loadFromFile against each other across FreeRTOS
  // tasks, so the JSON snapshot cannot tear mid-serialize and two concurrent
  // saves cannot write their documents out of order. Concurrent saves are
  // reachable: the web server task saves settings while the main task can too.
  //
  // It is deliberately held across the SD write. That is safe only because the
  // read path does NOT take it — derived stores build their snapshots (e.g.
  // CrossPointSettings::statusBarSpec) unlocked. If you ever lock this mutex on
  // a read path, you put it on the render path and stall rendering behind SD
  // I/O, and you create a storeMutex/storageMutex ordering hazard. Don't.
  mutable std::mutex storeMutex;

  // fromJson() implementations call this (instead of saving directly) when the
  // on-disk JSON used a legacy shape that was upgraded in memory.
  // loadFromFile() performs the save after releasing storeMutex; calling
  // saveToFileAtomic() from inside fromJson() would deadlock on storeMutex.
  void requestResave() { resaveRequested = true; }

  bool resaveRequested = false;

  // Set when a load read the file but fromJson refused it -- a format this build
  // does not know. Every save is then refused so the file survives for a build
  // that can read it. Written only by loadFromFile(), per persist::loadRefusedAfter.
  bool loadRefused = false;

 public:
  // Public so non-store JSON files (e.g. per-book bookmarks) can reuse them
  // instead of instantiating serializeJson/deserializeJson in their own TU —
  // that per-TU duplication is exactly what this class exists to prevent.

  // Serializes doc and writes it to path (ensures /.crosspoint exists). Logs on failure.
  static bool writeDocToFile(const char* path, const JsonDocument& doc);

  // Crash-safe variant of writeDocToFile: serializes to `<path>.tmp`, closes it,
  // then renames it over `path`. An interrupted write damages only the temp file
  // instead of tearing the real one. Same discipline as ProgressFile::writeAtomic.
  // Prefer this for any file whose loss matters (annotations, user data).
  static bool writeDocToFileAtomic(const char* path, const JsonDocument& doc);

  // Reads path and parses it into doc. Returns false silently when the file
  // does not exist (expected on first boot); logs on read/parse failure.
  static bool readDocFromFile(const char* path, JsonDocument& doc);

  // As readDocFromFile, but reports why the read failed.
  static DocReadStatus readDocFromFileChecked(const char* path, JsonDocument& doc);

  // Crash-safe counterpart to readDocFromFileChecked, and the read-side partner
  // of writeDocToFileAtomic. When `path` is absent but `<path>.tmp` is present
  // and parses, that .tmp is by construction the most recent complete write --
  // an interrupted rename between PersistableStore.cpp:38 and :39 -- so it is
  // renamed into place and used. An unparseable .tmp is reported as "nothing
  // there" and left on the card; see the call site for why it is not deleted.
  //
  // Prefer this wherever losing the file matters. readDocFromFileChecked stays
  // for callers that must read literally the path they name -- the three study
  // files pass it their own `<path>.tmp`.
  //
  // This is the first read path in this firmware that RENAMES. It is safe
  // without a lock of its own only because every reader and writer of the
  // adopting files runs on the Arduino loop task; the CRTP stores additionally
  // hold storeMutex, but /.berean/pubkeys.json, migration-ledger.json and
  // meeting-weeks.json rely on that single-task property alone. If a background
  // task ever touches /.berean/, give those files a mutex or move them back to
  // readDocFromFileChecked -- otherwise an adopting read on one task can rename
  // the .tmp another task is still writing.
  static DocReadStatus readDocFromFileAdopting(const char* path, JsonDocument& doc);

 protected:
  /**
   * Helper function for extracting an obfuscated password from a JSON value.
   * Accepts JsonVariantConst so callers can pass either a whole JsonDocument
   * or a JsonObject element (e.g. inside an array iteration).
   * If the decoded password requires a resave (e.g. from plaintext fallback), `needsResave` is set to true.
   */
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave);
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave, size_t maxLength, bool& valid);
};

/**
 * @brief Base class for persistable singletons using CRTP.
 *
 * Derived classes must provide:
 * - A private default constructor
 * - friend class PersistableStore<Derived>;
 * - static const char* getFilePath();
 * - void toJson(JsonDocument& doc) const;
 * - bool fromJson(JsonVariantConst doc);
 *
 * Note for implementers: read string values as `const char*` (e.g.
 * `obj["name"] | ""`), never as `| std::string("")` — ArduinoJson's
 * std::string converter drags a per-TU copy of the whole JSON serializer
 * into flash via its serializeJson fallback.
 *
 * Concurrency: saveToFile/loadFromFile lock storeMutex, so toJson/fromJson
 * always run under it. fromJson must signal legacy-shape upgrades with
 * requestResave(), never by calling saveToFileAtomic() directly (deadlock).
 *
 * Format versions: fromJson returns false for a format it does not know, before
 * touching any member. The store then keeps its pre-load values and refuses
 * every save until a later load is accepted or finds no file.
 */
template <typename T>
class PersistableStore : public PersistableStoreBase {
 protected:
  PersistableStore() = default;
  ~PersistableStore() = default;

 private:
  // Caller holds storeMutex.
  bool saveBlockedByRefusedLoad() const {
    if (loadRefused) {
      LOG_ERR("PERSIST", "Refusing to save %s: its format is unknown to this build", T::getFilePath());
    }
    return loadRefused;
  }

 public:
  // Delete copy constructor and assignment
  PersistableStore(const PersistableStore&) = delete;
  PersistableStore& operator=(const PersistableStore&) = delete;

  // Per-store ceiling. Override by declaring `static constexpr size_t
  // SAVE_BUDGET` on the store; otherwise the shared default applies.
  static constexpr size_t saveBudget() {
    if constexpr (requires { T::SAVE_BUDGET; }) {
      return T::SAVE_BUDGET;
    } else {
      return persist::DEFAULT_SAVE_BUDGET;
    }
  }

  static T& getInstance() {
    static T instance;
    return instance;
  }

  // Non-atomic and unbudgeted, with no store callers. Kept so that reaching for
  // it has to be deliberate; stores use saveToFileAtomic().
  bool saveToFile() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    if (saveBlockedByRefusedLoad()) return false;
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);
    return writeDocToFile(T::getFilePath(), doc);
  }

  // Measures before writing, then writes through a temp file and renames.
  //
  // Both halves matter. Without the budget check, a store that outgrows
  // persist::DEFAULT_SAVE_BUDGET still saves, then reads back truncated at
  // SDCardManager::readFile's cap, fails to parse, initialises empty, and the
  // next save overwrites the real file with {}. Without atomicity, a write
  // interrupted by a dead battery leaves a half-file that fails the same way.
  //
  // A store with a bounded record count may declare `static constexpr size_t
  // SAVE_BUDGET` to tighten the ceiling.
  bool saveToFileAtomic() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    if (saveBlockedByRefusedLoad()) return false;
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);

    const size_t serialised = measureJson(doc);
    if (!persist::fitsBudget(serialised, saveBudget())) {
      LOG_ERR("PERSIST", "Refusing to save %s: %u bytes exceeds budget %u", T::getFilePath(), (unsigned)serialised,
              (unsigned)saveBudget());
      return false;
    }
    return writeDocToFileAtomic(T::getFilePath(), doc);
  }

  bool loadFromFile() {
    bool ok = false;
    bool doResave;
    {
      std::lock_guard<std::mutex> lock(storeMutex);
      resaveRequested = false;
      JsonDocument doc;
      const DocReadStatus status = readDocFromFileAdopting(T::getFilePath(), doc);
      if (status == DocReadStatus::Ok) ok = static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
      loadRefused = persist::loadRefusedAfter(status, ok, loadRefused);
      if (status != DocReadStatus::Ok) return false;
      // Read the flag under the lock that guards the fromJson() that set it.
      doResave = resaveRequested;
      resaveRequested = false;
    }
    // Deliberately outside the lock: saveToFileAtomic() takes storeMutex itself.
    if (ok && doResave && !saveToFileAtomic()) {
      LOG_ERR("PERSIST", "Failed to resave %s after format update", T::getFilePath());
    }
    return ok;
  }
};
