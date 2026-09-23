# Surfacing store save refusals to the user

Design note for issue #39, on `fix/39-surface-store-refusals`.

The problem: a `PersistableStore` save that is refused (over its byte budget) or
fails (SD write) is reported with `LOG_ERR` only
(`lib/Serialization/PersistableStore.h:171-173`). That goes to the serial port and
nowhere else, so on a device without a cable attached the user's change silently
fails to stick.

No on-disk format changes. No new mechanism: every message goes through the
surface each activity already uses — `GUI.drawPopup`, which
`ReaderUtils::showMessage` wraps (`src/activities/reader/ReaderUtils.h:233`) — or,
for the web server, the HTTP status the settings page already renders
(`src/network/html/SettingsPage.html:579-592`).

## Decision 1 — bookmarks: a new key, not `STR_HIGHLIGHTS_TOO_LARGE`

`STR_HIGHLIGHTS_TOO_LARGE` reads *"This book already has too many highlights"*
(`english.yaml:370`). Reusing it for bookmarks names the wrong noun, and the
issue's point is that the message must be actionable. A new key sits beside the
other bookmark strings: `STR_BOOKMARKS_TOO_LARGE`, *"Too many bookmarks. Delete
some first."* (`english.yaml:275`). `STR_HIGHLIGHTS_TOO_LARGE` stays in place and
unreferenced, for the passage-file case below.

**Where it is used.** The refusal the issue worries about — 219 bookmarks in the
one Bible EPUB — happens when a bookmark is *added*, which is
`EpubReaderActivity::addBookmark` (`EpubReaderActivity.cpp:1826-1846`). That path
already kept `TooLarge` apart from `WriteFailed` in `BookmarkToast`, and only the
string was generic. `bookmarkToastString` now maps `TooLarge` to the new key
(`EpubReaderActivity.cpp:1726-1727`); `SaveFailed` stays on
`STR_ERROR_GENERAL_FAILURE`, which is true for an SD failure.

**Where it is not used.** The site the issue cites,
`EpubReaderBookmarksActivity.cpp:196-204`, is the *delete* path, and a delete
cannot hit the budget: `bookmarkSaveAction` writes any document at or under the
budget and any over-budget document that is strictly shrinking
(`src/util/BookmarkSaveAction.h:27-30`). The only `TooLarge` left on a delete is a
document over the 50,000-byte read cap, which could not have loaded in the first
place — a truncated read fails to parse, `load` returns `Failed`, and the list
never opens (`EpubReaderBookmarksActivity.cpp:34-49`). What a delete can hit is an
SD failure, for which the generic message is the honest one. Telling a user who is
deleting to "delete some first" would be wrong, so that site is left as it is.

## Decision 2 — which of the other stores reach the screen

The rule: surface a refusal when it loses something **the user just did on
purpose**, at the site where they did it. Leave log-only the saves that run as
bookkeeping, where nobody is looking at the screen for a result and the data
rebuilds itself.

| Store | Site | Decision |
|---|---|---|
| `CrossPointSettings` | every settings change made on the device: `SettingsActivity`, `TextSettingsActivity`, `StatusBarSettingsActivity`, `ButtonRemapActivity`, `LanguageSelectActivity`, `ClockOffsetActivity`, `EpubReaderMenuActivity` (night mode, frontlight), `EpubReaderActivity::applyOrientation`, `FrontlightPanelActivity::onExit`, `BmpViewerActivity` (set as sleep screen) | **Popup** through `saveSettingsOrReport` (`src/activities/SettingsSave.h`) |
| `CrossPointSettings` | web server `POST /api/settings` (`CrossPointWebServer.cpp:1320`) | **HTTP 500**; it used to answer 200 "Applied" regardless |
| `CrossPointSettings` | boot (`main.cpp:222`), SD font fallback (`SdCardFontSystem.cpp:21`, `CrossPointSettings.cpp:343`), the clock-synced flag (`ClockSyncActivity.cpp:76`, `WifiSelectionActivity.cpp:561`) | Log only. Nothing the user chose is lost: the fallback re-derives on the next boot, and a lost synced flag only means one extra automatic NTP sync |
| `WifiCredentialStore` | "Save password?" and "Forget network?" in `WifiSelectionActivity` | **Popup** (`STR_WIFI_SAVE_FAILED`) |
| `WifiCredentialStore` | `setLastConnectedSsid` | Log only; it is bookkeeping for auto-connect |
| `CrossPointState` | every site (`main.cpp`, `ReaderActivity`, `LauncherActivity`, `SleepActivity`, `PublicationDownloader`, `EpubReaderActivity:152`) | Log only. Runtime state such as the last-opened book and the sleep-image cursor. Every write is automatic, and its budget has more than 800 B of headroom over a fixed key set (`CrossPointState.h:32-34`) |
| `RecentBooksStore` | `addBook`/`updateBook`/`pruneMissing` | Log only. Recents are written automatically as books open, and the list rebuilds from use. Its budget is the computed worst case of a capped list (`RecentBooksDoc.h:61-70`), so growth cannot reach it |
| Study data (`StudyStore`: passages, tag palette) | `PassageSelectActivity:395`, `HighlightsActivity:265,318`, `TagPickerActivity`, `TagFilterActivity` | **Already surfaced** (`STR_HIGHLIGHTS_SAVE_FAILED`, `STR_TAG_SAVE_FAILED`). Unchanged; see below |

The settings refusal is realistically an SD failure rather than a budget refusal,
since the file's worst case is about 1,600 B against a 4,096 B budget
(`CrossPointSettings.h:425-427`). It is surfaced anyway because the user sees the
same thing either way: a change that does not stick.

**One helper, not per-site code.** There are 29 device-side settings save sites.
`saveSettingsOrReport(renderer)` puts the save and the popup in one place, so a
new settings screen gets the report by calling the same function the others do.
It is a header-only inline that calls the existing `GUI.drawPopup`. It adds no
state and no allocation.

**WiFi `removeCredential` returns false for an unknown SSID too**
(`WifiCredentialStore.cpp:137-150`). The forget sites therefore record
`hasSavedCredential` first and report only when the SSID was actually saved, so
forgetting a network that was never stored does not raise a false error.

## Decision 3 — study data keeps its generic message for now

`StudyStore::addPassage` and the other mutators return `bool`, which folds
`PassageFile::SaveResult::TooLarge` into the same `false` as an SD failure
(`src/study/StudyStore.cpp:64-71`). `STR_HIGHLIGHTS_TOO_LARGE` fits the passage
case exactly. Wiring it in means changing four `StudyStore` signatures and their
call sites. The passage budget is 200,000 B
(`lib/StudyStore/StudyStore/PassageDoc.h:26`), so this is not the ceiling heavy
Bible use reaches first; bookmarks, at 45,000 B, are. That change is left as a
follow-up, and the key is kept for it.

## Testing

No host-testable decision logic is added. The new code is a `tr()` lookup plus a
`GUI.drawPopup` call at each site, and neither can be built on the host. When
bookmarks refuse is already pinned by `test/bookmark_save_action/`. The popups,
and how long they stay readable before the next repaint, can only be checked on
the device.
