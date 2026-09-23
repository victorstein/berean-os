# File Transfer entry point — design note (issue #29)

The user chose to restore an entry point to the web server rather than delete
the stack. This note records the smaller choices the issue left open.

## Where the entry lives

Settings > System, directly after **WiFi Networks**
(`src/activities/settings/SettingsActivity.cpp`, `rebuildSettingsLists()`).
The label reuses `STR_FILE_TRANSFER`, which the web server activity and the
mode list already use as their header (`NetworkModeSelectionActivity.cpp`,
`headerTitle()`), so no new string is needed.

## Pushed, not `goToFileTransfer()`

`ActivityManager::goToFileTransfer()` calls `replaceActivity()`
(`ActivityManager.cpp:200-202`), which destroys Settings and clears the stack.
Every other Settings action is pushed with `startActivityForResult()`
(`SettingsActivity.cpp`, the `SettingType::ACTION` switch), and the File
Transfer action follows them, so backing out before Wi-Fi starts lands back in
Settings.

For the same reason the three places `CrossPointWebServerActivity` handled a
cancelled mode list with `onGoHome()` now call `finish()`, through one
`launchModeSelection()` helper. If the activity is ever started with nothing
beneath it, `ActivityManager::loop()` already sends an empty-stack pop home.

Exits after Wi-Fi has started are unchanged: `onExit()` calls
`silentRestart()` whenever `WiFi.getMode() != WIFI_MODE_NULL`, which reboots to
the launcher. That matches the other Wi-Fi activities launched from Settings
(`OtaUpdateActivity.cpp:98`, `FontDownloadActivity.cpp:49`).

## Back on a device with no Back button

`MappedInputManager::wasReleased(Button::Back)` returns true for the left-edge
swipe (`MappedInputManager.cpp:266,309`), and the server
loop polls that alongside `wasHomeGesture()` inside and outside its
request-handling burst. The mode list is a `UiListActivity`, which routes the
same Back to `onBackButton()` (`UiListActivity.cpp:47-48`), and that cancels. No input-layer change is needed.

## Calibre Wireless removed from the mode list

Calibre Wireless receives books from a desktop library — a general-reader
workflow CLAUDE.md places out of scope. Leaving it in the menu would restore a
capability the fork has deliberately dropped; leaving the activity in the tree
without a menu entry would recreate exactly the unreachable-code problem #29
is about. So `CalibreConnectActivity.{h,cpp}` are deleted and
`NetworkMode::CONNECT_CALIBRE` is gone.

The Calibre strings stay in the translation files (the standing rule is not to
remove keys), and the web server's WebSocket upload and UDP discovery listener
stay: they are generic upload paths the File Manager page uses.

## Meeting Publications stays

It is in scope, and it is independent of the web server: it brings up its own
station connection and returns to the mode list.

## HomeActivity and RecentBooksActivity stay

Both remain unreachable. Deleting them is not required by #29, and they are
tangled with theme code (`BaseTheme.h`, the Lyra and RoundedRaff themes) that
still carries HomeActivity-specific layout helpers. Removing them cleanly is a
separate change.
