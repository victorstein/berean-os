# Issue #101 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-26-issue-101-design.md` (at `469a7c7a`), measured against
issue #101 (`gh issue view 101 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-26-issue-101-research.md`.

Checked against the code and found accurate: the table of `toJson`/`fromJson` line ranges; that
every `fromJson` returns `true` unconditionally; the `loadFromFile` control flow
(`PersistableStore.h:179-199`); the `DeleteTempReportEmpty` → `Missing` mapping
(`TempAdoption.h:53-55`); `{"v":1,"books":[]}` = 18 bytes, so 11421 → 11427, still under
`DEFAULT_SAVE_BUDGET = 45000` (`SaveBudget.h:23`); only four `public PersistableStore<` users; the
refusal-surfacing paths (`SettingsSave.h:15-19`, `CrossPointWebServer.cpp:1292-1295`,
`WifiSelectionActivity.cpp:191`, `STR_WIFI_SAVE_FAILED` at `english.yaml:78`); A4's claim that
OTA still works without a saved credential (`WifiSelectionActivity.cpp:70-76,776-783`: a failed
`addCredential` posts the popup and then `onComplete(true)` anyway); and the #99 dependency
(`origin/fix/99-halstorage-test-fake` is one commit, `beef81cd docs: research ...`).

The save guard (A3/A9/d1) is sound for the three stores that load at boot. Its one real gap is
the store that does not.

## BLOCKER

### B1. `WIFI_STORE` can be saved without ever being loaded, so the guard never arms and a newer `wifi.json` is overwritten

**Claim.** Goal 3 (spec :36-38): a refused file is not overwritten "for as long as the refusal
stands". A2 (:66-71): the mid-session reloads keep whatever was in memory, and "On a rolled-back
build that is the defaults as well, because the boot load was refused first." A3 arms the guard
only inside `loadFromFile()`.

**Problem.** `WIFI_STORE` has no boot load. Only three stores load at boot (`src/main.cpp:411-413`:
`SETTINGS`, `APP_STATE`, `RECENT_BOOKS`). The only `WIFI_STORE.loadFromFile()` in the tree is at
`src/activities/network/WifiSelectionActivity.cpp:106` (`grep -rn 'loadFromFile()' src lib`).
The web server's Wi-Fi API saves the store without loading it first:

- `CrossPointWebServer.cpp:174-176` registers `/api/wifi` GET/POST and `/api/wifi/delete`.
- `:1402` `WIFI_STORE.addCredential(...)` → `saveToFileAtomic()` (`WifiCredentialStore.cpp:125`).
- `:1398` `updateCredential`, `:1440` `removeCredential` also save.
- The settings page calls these endpoints (`src/network/html/SettingsPage.html:648,682,698`).

The web server can run without passing through `WifiSelectionActivity`. In
`CrossPointWebServerActivity::onNetworkModeSelected`, `CREATE_HOTSPOT` goes straight to
`startAccessPoint()` (`CrossPointWebServerActivity.cpp:170-174`), and only `JOIN_NETWORK` launches
`WifiSelectionActivity` (`:153-169`). So from a fresh boot, going to File Transfer → Create Hotspot →
settings page → add a network calls `saveToFileAtomic()` on a store that was never loaded.
`loadRefused` is still at its initial `false`, and the save writes a `wifi.json` containing only
that one network. A `"v":2` file from the newer build, with every saved password in it, is
replaced. This is exactly the loss that A4 says the rule exists to prevent. The load-time
refusal never ran, so there was no "refusal" to stand, and Goal 3's wording lets this through.

The same path already destroys a v1 `wifi.json` today: an unloaded store adds one credential and
saves the list with just that entry. The research's "Who loads" list (research §2 :51-62) and the
spec both missed this seam. Fixing it for #101 either fixes that existing bug too, which widens
scope, or defines "no load yet" as its own blocked state, which changes the hotspot feature. The
human has to choose which.

**Evidence.** As cited above. `grep -rn 'WIFI_STORE\.' src` shows no load in
`CrossPointWebServer.cpp` or `CrossPointWebServerActivity.cpp`.

**Concrete fix.** Decide on one of these, then record it as d2 and update A2's "boot load"
sentence, the Error-handling table, and the device test (item 7 should add a `"v":2`
`wifi.json` combined with hotspot-mode add):

- (a) Load `WIFI_STORE` at boot next to the other three (`main.cpp:413`). This is the smallest
  change and also fixes the existing v1 loss. It keeps up to 8 credentials resident for the
  whole session, which needs the internal-heap justification that `CLAUDE.md` asks for.
- (b) Load `WIFI_STORE` at the top of the three `/api/wifi` handlers (or once when the web
  server starts). This is scoped to the seam. Note which task runs the handler, because
  `readDocFromFileAdopting` renames (`PersistableStore.h:80-87`). The CRTP stores hold
  `storeMutex`, which covers it.
- (c) Make the base guard three-state (`NotLoaded` blocks saves, exactly like `Refused`).
  This is the most general option, but it turns every save before a load into a refusal, so it
  would break hotspot add unless (b) is also done.

Add a host case to A9's table if (c) is chosen.

## MINOR

### m1. A1 says the unconvertible-`"v"` behaviour is "pinned" by a test that does not pin it

**Claim.** A1 (:59-60): a non-integer `"v"` such as `"2"` reads as 1, "pinned by
`test/bookmark_doc/BookmarkDocTest.cpp:81-115`".

**Problem.** Those lines contain three tests: absent reads as 1 (`:81-99`), a present `0` is
refused (`:101-108`), and `FORMAT_VERSION + 1` is refused (`:110-116`). None of them feeds a
string or float `"v"`. The spec deliberately accepts that behaviour, but nothing tests it.

**Fix.** Either drop the "pinned by" citation, or add
`AStringVersionReadsAsTheDefault` to the planned `RecentBooksDocTest` cases (Testing item 2) so
the accepted behaviour is under test.

### m2. Two more comments become false, and the spec lists only two

**Claim.** Documentation (:292-293) rewrites `RecentBooksDoc.h:84-86` and `CrossPointState.h:32`.

**Problem.**
- `CrossPointSettings.h:427` says "One key per SettingsList.h row plus nine written by hand in
  toJson()". Those nine are `frontButtonBack/Confirm/Left/Right`, `fontFamily`, `fontSize`,
  `sdFontFamilyName`, `longPressMenuFunction` and `language` (`CrossPointSettings.cpp:84-104`).
  With `"v"` there are ten.
- `SettingsSave.h:8-10` says `saveToFileAtomic` fails because of "a budget refusal or an SD
  failure". A format refusal becomes a third cause.
- The `RecentBooksDoc.h:46` comment `// {"books":[]}` must become `{"v":1,"books":[]}` along with
  the constant. The architecture row implies this but does not say it.

**Fix.** Add these to the "Comments that become false" list.

### m3. A3 puts the flag in the wrong class

**Claim.** A3 (:76): "`PersistableStore<T>` gains a private `bool loadRefused` next to
`resaveRequested` (`PersistableStore.h:44-46`)".

**Problem.** `resaveRequested` and `storeMutex` belong to `PersistableStoreBase`
(`PersistableStore.h:23-46`), not to the template. "Private in `PersistableStore<T>`" and "next to
`resaveRequested`" name two different places. Either placement works, because the template's
three methods are the only readers and writers. The implementer should not have to pick.

**Fix.** Pick one. `PersistableStoreBase`'s protected block, directly under `resaveRequested`,
matches the mechanism A3 says it is extending.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 0
