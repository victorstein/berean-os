// Host coverage for WifiCredentialStore::addCredential's in-memory edit and its
// rollback. The store itself reaches Arduino.h through PersistableStore.h; see
// util/HighlightFileAction.h for why that cannot be built on the host.

#include <gtest/gtest.h>

#include <vector>

#include "util/WifiCredentialEdit.h"

namespace {
constexpr size_t LIMIT = 8;

std::vector<WifiCredential> twoNetworks() { return {{"home", "old-pass"}, {"work", "work-pass"}}; }
}  // namespace

TEST(WifiCredentialEdit, NewSsidIsAppended) {
  auto credentials = twoNetworks();
  const auto edit = upsertCredential(credentials, "cafe", "latte", LIMIT);
  EXPECT_EQ(edit.kind, WifiCredentialEdit::Kind::Appended);
  ASSERT_EQ(credentials.size(), 3u);
  EXPECT_EQ(credentials.back().ssid, "cafe");
  EXPECT_EQ(credentials.back().password, "latte");
}

TEST(WifiCredentialEdit, KnownSsidIsUpdatedInPlace) {
  auto credentials = twoNetworks();
  const auto edit = upsertCredential(credentials, "home", "new-pass", LIMIT);
  EXPECT_EQ(edit.kind, WifiCredentialEdit::Kind::Replaced);
  ASSERT_EQ(credentials.size(), 2u);
  EXPECT_EQ(credentials[0].password, "new-pass");
}

TEST(WifiCredentialEdit, NewSsidPastTheLimitIsRejectedUntouched) {
  std::vector<WifiCredential> credentials(LIMIT, WifiCredential{"x", "y"});
  const auto edit = upsertCredential(credentials, "one-too-many", "p", LIMIT);
  EXPECT_EQ(edit.kind, WifiCredentialEdit::Kind::Rejected);
  EXPECT_EQ(credentials.size(), LIMIT);
}

TEST(WifiCredentialEdit, KnownSsidAtTheLimitIsStillUpdated) {
  std::vector<WifiCredential> credentials(LIMIT - 1, WifiCredential{"x", "y"});
  credentials.push_back({"home", "old-pass"});
  const auto edit = upsertCredential(credentials, "home", "new-pass", LIMIT);
  EXPECT_EQ(edit.kind, WifiCredentialEdit::Kind::Replaced);
  EXPECT_EQ(credentials.back().password, "new-pass");
}

TEST(WifiCredentialEdit, UndoOfAnAppendRemovesIt) {
  auto credentials = twoNetworks();
  const auto edit = upsertCredential(credentials, "cafe", "latte", LIMIT);
  undoCredentialEdit(credentials, edit);
  ASSERT_EQ(credentials.size(), 2u);
  EXPECT_EQ(credentials[0].ssid, "home");
  EXPECT_EQ(credentials[1].ssid, "work");
}

TEST(WifiCredentialEdit, UndoOfAnUpdateRestoresThePreviousPassword) {
  auto credentials = twoNetworks();
  const auto edit = upsertCredential(credentials, "home", "new-pass", LIMIT);
  undoCredentialEdit(credentials, edit);
  ASSERT_EQ(credentials.size(), 2u);
  EXPECT_EQ(credentials[0].ssid, "home");
  EXPECT_EQ(credentials[0].password, "old-pass") << "an update must be restored, not erased";
}

TEST(WifiCredentialEdit, UndoFindsTheEntryBySsidAfterTheListShifted) {
  auto credentials = twoNetworks();
  const auto edit = upsertCredential(credentials, "work", "new-work", LIMIT);
  credentials.erase(credentials.begin());  // another writer removed "home" in between
  undoCredentialEdit(credentials, edit);
  ASSERT_EQ(credentials.size(), 1u);
  EXPECT_EQ(credentials[0].password, "work-pass");
}

TEST(WifiCredentialRename, RenameReplacesTheOldEntry) {
  auto credentials = twoNetworks();
  const auto rename = renameCredential(credentials, "home", "home-5g", "new-pass", LIMIT);
  ASSERT_TRUE(rename.applied);
  ASSERT_EQ(credentials.size(), 2u);
  EXPECT_EQ(credentials[0].ssid, "work");
  EXPECT_EQ(credentials[1].ssid, "home-5g");
  EXPECT_EQ(credentials[1].password, "new-pass");
}

TEST(WifiCredentialRename, UndoRestoresTheOldEntryInItsPlace) {
  auto credentials = twoNetworks();
  const auto rename = renameCredential(credentials, "home", "home-5g", "new-pass", LIMIT);
  undoCredentialRename(credentials, rename);
  ASSERT_EQ(credentials.size(), 2u);
  EXPECT_EQ(credentials[0].ssid, "home");
  EXPECT_EQ(credentials[0].password, "old-pass");
  EXPECT_EQ(credentials[1].ssid, "work");
  EXPECT_EQ(credentials[1].password, "work-pass");
}

TEST(WifiCredentialRename, RenameOntoAnotherSavedSsidMergesAndUndoSplitsThemAgain) {
  auto credentials = twoNetworks();
  const auto rename = renameCredential(credentials, "home", "work", "merged", LIMIT);
  ASSERT_TRUE(rename.applied);
  ASSERT_EQ(credentials.size(), 1u);
  EXPECT_EQ(credentials[0].password, "merged");

  undoCredentialRename(credentials, rename);
  ASSERT_EQ(credentials.size(), 2u);
  EXPECT_EQ(credentials[0].ssid, "home");
  EXPECT_EQ(credentials[0].password, "old-pass");
  EXPECT_EQ(credentials[1].password, "work-pass");
}

TEST(WifiCredentialRename, SameSsidIsAnInPlaceUpdate) {
  auto credentials = twoNetworks();
  const auto rename = renameCredential(credentials, "home", "home", "new-pass", LIMIT);
  ASSERT_TRUE(rename.applied);
  EXPECT_EQ(credentials[0].ssid, "home");
  EXPECT_EQ(credentials[0].password, "new-pass");
  undoCredentialRename(credentials, rename);
  EXPECT_EQ(credentials[0].password, "old-pass");
}

TEST(WifiCredentialRename, RenameAtTheLimitStillFits) {
  std::vector<WifiCredential> credentials;
  for (size_t i = 0; i < LIMIT; ++i) credentials.push_back({"net" + std::to_string(i), "p"});
  const auto rename = renameCredential(credentials, "net0", "renamed", "p", LIMIT);
  ASSERT_TRUE(rename.applied) << "the old entry frees the slot the new one takes";
  EXPECT_EQ(credentials.size(), LIMIT);
}

TEST(WifiCredentialRename, UnknownOldSsidAppliesNothing) {
  auto credentials = twoNetworks();
  const auto rename = renameCredential(credentials, "absent", "cafe", "latte", LIMIT);
  EXPECT_FALSE(rename.applied);
  undoCredentialRename(credentials, rename);
  EXPECT_EQ(credentials.size(), 2u);
}

TEST(WifiCredentialEdit, UndoOfARejectionChangesNothing) {
  std::vector<WifiCredential> credentials(LIMIT, WifiCredential{"x", "y"});
  const auto edit = upsertCredential(credentials, "one-too-many", "p", LIMIT);
  undoCredentialEdit(credentials, edit);
  EXPECT_EQ(credentials.size(), LIMIT);
}
