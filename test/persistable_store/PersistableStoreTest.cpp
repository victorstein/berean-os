// The save guard issue #101 adds to PersistableStore: a store that refused its
// file's format must not write over it, including a file that #98's .tmp
// adoption promoted into place. Runs the real template and the real
// PersistableStore.cpp against the in-memory HalStorage fake.

#include <ArduinoJson.h>
#include <FormatVersion.h>
#include <HalStorage.h>
#include <HalStorageFake.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

#include <string>

namespace {

class ProbeStore : public PersistableStore<ProbeStore> {
  ProbeStore() = default;
  friend class PersistableStore<ProbeStore>;

 public:
  static constexpr int FORMAT_VERSION = 1;
  int value = 0;

  static const char* getFilePath() { return "/.crosspoint/probe.json"; }

  void toJson(JsonDocument& doc) const {
    doc["v"] = FORMAT_VERSION;
    doc["value"] = value;
  }

  bool fromJson(JsonVariantConst doc) {
    const int version = doc["v"] | FORMAT_VERSION;
    if (!persist::isKnownFormatVersion(version, FORMAT_VERSION)) return false;
    value = doc["value"] | 0;
    return true;
  }
};

const std::string PATH = ProbeStore::getFilePath();
const std::string TMP_PATH = PATH + ".tmp";
const std::string NEWER = R"({"v":2,"value":7})";

std::string bytesOn(const std::string& path) {
  const auto bytes = storage_fake::fileBytes(path);
  return bytes ? *bytes : std::string("<absent>");
}

// The store is a process-wide singleton, like on the device, so every test
// starts from an empty card and a load that resets its refusal flag.
class PersistableStoreGuard : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_fake::reset();
    ASSERT_FALSE(store().loadFromFile()) << "empty card: Missing clears any earlier refusal";
    store().value = 0;
  }
  static ProbeStore& store() { return ProbeStore::getInstance(); }
};

}  // namespace

TEST_F(PersistableStoreGuard, ARefusedLoadBlocksEverySaveAndLeavesTheFileUntouched) {
  storage_fake::putFile(PATH, NEWER);

  EXPECT_FALSE(store().loadFromFile());
  EXPECT_EQ(store().value, 0) << "a refused load keeps the pre-load value";

  store().value = 42;
  EXPECT_FALSE(store().saveToFileAtomic());
  EXPECT_FALSE(store().saveToFile());
  EXPECT_EQ(bytesOn(PATH), NEWER);
  EXPECT_EQ(bytesOn(TMP_PATH), "<absent>") << "refused before the temp file is written";
}

TEST_F(PersistableStoreGuard, AMissingFileLiftsTheRefusal) {
  storage_fake::putFile(PATH, NEWER);
  ASSERT_FALSE(store().loadFromFile());
  ASSERT_TRUE(Storage.remove(PATH.c_str()));

  EXPECT_FALSE(store().loadFromFile()) << "Missing";
  store().value = 5;
  EXPECT_TRUE(store().saveToFileAtomic());
  EXPECT_EQ(bytesOn(PATH), R"({"v":1,"value":5})");
}

TEST_F(PersistableStoreGuard, ALegacyFileLoadsAndIsStampedOnTheNextSave) {
  storage_fake::putFile(PATH, R"({"value":3})");

  ASSERT_TRUE(store().loadFromFile());
  EXPECT_EQ(store().value, 3);
  EXPECT_EQ(bytesOn(PATH), R"({"value":3})") << "an absent version alone does not force a rewrite";
  EXPECT_TRUE(store().saveToFileAtomic());
  EXPECT_EQ(bytesOn(PATH), R"({"v":1,"value":3})");
}

TEST_F(PersistableStoreGuard, AnUnparseableFileAfterARefusalKeepsSavesBlocked) {
  storage_fake::putFile(PATH, NEWER);
  ASSERT_FALSE(store().loadFromFile());
  storage_fake::putFile(PATH, "not json");

  EXPECT_FALSE(store().loadFromFile());
  EXPECT_FALSE(store().saveToFileAtomic());
  EXPECT_EQ(bytesOn(PATH), "not json");
}

TEST_F(PersistableStoreGuard, AnUnparseableFileIsStillOverwritableAsBefore) {
  storage_fake::putFile(PATH, "not json");

  EXPECT_FALSE(store().loadFromFile());
  store().value = 1;
  EXPECT_TRUE(store().saveToFileAtomic()) << "#101 does not change corrupt-file handling";
  EXPECT_EQ(bytesOn(PATH), R"({"v":1,"value":1})");
}

TEST_F(PersistableStoreGuard, AFutureVersionTempIsPromotedThenRefusedAndNeverOverwritten) {
  storage_fake::putFile(TMP_PATH, NEWER);

  EXPECT_FALSE(store().loadFromFile());
  EXPECT_EQ(store().value, 0) << "a refused load keeps the pre-load value";
  EXPECT_EQ(bytesOn(PATH), NEWER) << "the load promoted the .tmp before refusing it";
  EXPECT_EQ(bytesOn(TMP_PATH), "<absent>");

  store().value = 42;
  EXPECT_FALSE(store().saveToFileAtomic());
  EXPECT_FALSE(store().saveToFile());
  EXPECT_EQ(bytesOn(PATH), NEWER);
  EXPECT_EQ(bytesOn(TMP_PATH), "<absent>") << "refused before the temp file is written";
}
