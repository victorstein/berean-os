# Surfacing store save refusals to the user

Design note for issue #39, on `fix/39-surface-store-refusals`.

The problem: a `PersistableStore` save that is refused (over its byte budget) or
fails (SD write) is reported with `LOG_ERR` only
(`lib/Serialization/PersistableStore.h:171-173`). That goes to the serial port and
nowhere else, so on a device without a cable attached the user's change silently
fails to stick.

No on-disk format changes and no new mechanism. On-device messages go through
`PostedMessage` (`src/activities/PostedMessage.h`, from #78), which draws a queued
popup after the next screen finishes rendering. The web server reports through the
HTTP status that the settings page already renders
(`src/network/html/SettingsPage.html:579-592`). See Decision 4 for which screens
draw posted messages.

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

A failed settings save keeps the new value applied in memory, unlike Wi-Fi, which
rolls back. A setting takes effect the moment it is chosen: night mode has already
inverted the screen and a font change has already re-laid out the page. Rolling
the value back would silently undo what the user is looking at, while keeping it
means the next successful save persists it. A Wi-Fi credential has no visible
effect in memory, so keeping it would only let a later unrelated save write
something the user was told had failed.

**Only the screen that changed a setting saves it.** Every settings screen saves
and reports its own changes (`ButtonRemapActivity`, `LanguageSelectActivity`,
`StatusBarSettingsActivity`, `ClockOffsetActivity`, `TextSettingsActivity`).
`FontInstaller` saves through `clearSdFontFamily`, and the Wi-Fi screen saves its
own clock-synced flag. The other screens `SettingsActivity` opens (Network, Clear
cache, Check for updates, SD firmware update) change no setting. So
`SettingsActivity` no longer saves after a child screen returns, and no longer
saves on Back, where it only repeated a save each change had already made. Before
this, a single failure was reported twice, and a failure could be reported after a
screen that changed nothing.

**One helper, not per-site code.** There are 24 device-side settings save sites
that use `saveSettingsOrReport()`. It puts the save and the message in one place,
so a new settings screen gets the report by calling the same function the others
do. It is a header-only inline that posts a `tr()` string. It adds no state and no
allocation.

The one settings site that does not use it is `BmpViewerActivity`'s "set as sleep
screen". There the failure is drawn at once, in place of its existing `STR_DONE`
popup. That screen holds each of its popups with `delay(1000)` and redraws through
`onEnter()`, which does not draw posted messages.

**Forgetting a network rolls back too.** `removeCredential` undoes the removal
and restores `lastConnectedSsid` when the save fails
(`removeCredentialNamed`/`undoCredentialRemoval`). Without that, a later
unrelated save would persist a deletion the user had just been told failed. The
Wi-Fi screen keeps the network's saved mark in that case, and reports only
`SaveFailed`, so forgetting a network that was never saved raises no error.

All three credential edits return `WifiCredentialStore::EditResult`
(`Ok`, `NotFound`, `LimitReached`, `SaveFailed`), so callers can tell a storage
failure from bad input. The web server's `/api/wifi` and `/api/wifi/delete` answer
400 for bad input, an unknown network or the network limit, and 500 when the card
would not take the write.

**Editing a network from the web server was two saves.**
`POST /api/wifi` with an index removed the old entry, saved, then added the new
one and saved again. If the add failed, the old network was already gone, in
memory and on the card. It now calls `WifiCredentialStore::updateCredential`,
which applies the edit as one in-memory change (`renameCredential` in
`src/util/WifiCredentialEdit.h`) and makes one atomic save. If that save fails, it
undoes the change and restores `lastConnectedSsid`, and the file on the card was
never touched. `addCredential` undoes a failed save the same way, so a later
unrelated save (such as `setLastConnectedSsid`) cannot persist a network the user
was told was not saved.

## Decision 3 — study data keeps its generic message for now

`StudyStore::addPassage` and the other mutators return `bool`, which folds
`PassageFile::SaveResult::TooLarge` into the same `false` as an SD failure
(`src/study/StudyStore.cpp:64-71`). `STR_HIGHLIGHTS_TOO_LARGE` fits the passage
case exactly. Wiring it in means changing four `StudyStore` signatures and their
call sites. The passage budget is 200,000 B
(`lib/StudyStore/StudyStore/PassageDoc.h:26`), so this is not the ceiling heavy
Bible use reaches first; bookmarks, at 45,000 B, are. That change is left as a
follow-up, and the key is kept for it.

## Decision 4 — where each posted message is drawn

`UiListActivity::render` and the reader's render already draw posted messages
(`UiListActivity.cpp:168`, `ReaderActivity.cpp:180,185`). For each message this PR
posts, the screen that renders next is:

| Posted from | Next screen | Draws posted messages? |
|---|---|---|
| Settings, Text settings, Status bar, Language, Reader menu, Clock offset (on exit), Button remap (then finish) | a `UiListActivity` | yes, already |
| `EpubReaderActivity::applyOrientation` | the reader | yes, already |
| any message still held when the user goes home, such as one from the frontlight panel | `LauncherActivity` | **added** |
| Wi-Fi "Forget network?" (then `startWifiScan`) | `WifiSelectionActivity` | **added** |
| Wi-Fi "Save password?" (then `onComplete`) | the screen that opened Wi-Fi: Settings, ClockSync, FontDownload, OtaUpdate, CatalogSearch, CrossPointWebServer, MeetingDownload | Settings already; the other six **added** |
| `FrontlightPanelActivity::onExit` | whatever screen the panel was opened over | yes for the list screens, the reader, the launcher and the screens above; see below |

Each added call is `PostedMessage::drawNext(renderer)` right after that render's
own `displayBuffer`, the placement #78 established. I chose this over a single call
in `ActivityManager`'s render loop. The render functions that already call
`drawNext` would then call it twice, and when two messages are queued the second
call would draw the second message over the first straight away. The per-screen
call also follows #78's pattern and does not change the mechanism #78 introduced.

**Frontlight panel.** The panel can be opened over any screen
(`ActivityManager.cpp:85-86`). Over a screen that does not draw posted messages,
the message stays queued and appears on the next screen that does. It arrives
late but is not lost. The queue holds two messages and logs any it has to drop.

**Messages are held on screen and not repeated.** A drawn message stays current
for `PostedMessageQueue::MIN_DISPLAY_MS` (2.5 s, the same hold as the bookmark
toast), and every `drawNext` in that window redraws it. Without the hold, screens
with a progress bar (OTA update, meeting download, font download) repaint within a
second or two and would wipe it. A post identical (by pointer) to a message
already queued, or still on screen, is dropped. The rules live in
`src/activities/PostedMessageQueue.h`, with the clock passed in, and are
host-tested in `test/posted_message/`. `PostedMessage.cpp` adds only the mutex,
`millis()` and the draw. There are no timers or tasks: a message whose hold has
run out is replaced at the next render, and until then it simply stays on screen.

**The bookmark refusal needs no change.** `EpubReaderActivity` does not draw it as
a one-off popup. It sets `showBookmarkMessage`, and the reader's own render draws
the toast every time it paints (`EpubReaderActivity.cpp:1286-1287`) until
`BOOKMARK_MESSAGE_DURATION_MS` (2.5 s) has passed. So no later render paints over
it.

## Testing

The Wi-Fi edit and rollback rules are host-tested in
`test/credential_integrity/WifiCredentialEditTest.cpp`: append, update, the
network limit, removal, undo of each, undo after the list has shifted, and
renaming (in place, onto another saved network, and at the limit). The message
queue's hold, ordering, de-duplication, capacity and `millis()` wrap are tested in
`test/posted_message/PostedMessageQueueTest.cpp`. The message plumbing is a
`tr()` lookup plus a `PostedMessage::post` at each site, and neither can be built
on the host. When bookmarks refuse is already covered by
`test/bookmark_save_action/`. Whether each message actually appears on screen can
only be checked on the device.
