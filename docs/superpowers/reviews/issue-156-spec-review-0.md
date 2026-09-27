Tier: heavy

# Issue #156 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-156-design.md` (the spec) against
`gh issue view 156 --repo victorstein/berean-os` and
`docs/superpowers/research/2026-09-27-issue-156-research.md`, at `99685def` (base `d956cc8b`).

## Summary

The core design holds up. I checked every load-bearing citation against the code:

- the lookup order;
- the Bible signal (B1), with every call site checked;
- idempotency through `lookup`-then-`record`, and the overwrite semantics at `PubKeyRegistry.cpp:36-39`;
- the study key staying unchanged (`PubKey.cpp:25`);
- the claim that cleanup needs no code (`HttpDownloader.cpp:300-308`, `PublicationDownloader.cpp:218-221`);
- the stack clear on the escape hatch (`ActivityManager.cpp:141-150`);
- the `·` glyph in the tile font (`notosans_8_regular.h:2481`, interval `0xA0-0x17F`);
- the pre-#104 recents lambda, which matches `git show 08d98a9f` verbatim.

All of them hold. There is no BLOCKER. The three MAJORs can each be fixed inline, and none of them changes a decision.

## Findings

### MAJOR 1: `preventAutoSleep() = true` keeps the device awake indefinitely on the confirm and failure dialogs

- **Claim** (spec §D, "The class shape copies `MeetingDownloadActivity`"): "`preventAutoSleep() = true`".
- **Problem:** `MeetingDownloadActivity` starts working as soon as it is entered. This screen opens on a Confirm dialog and can rest on a Failed dialog, and both wait for the user with no time limit. With an unconditional `true`, a user who taps the tile and puts the device down never gets auto-sleep, so the battery drains until someone notices. The codebase's own precedent for "confirm, then run" gates the override on the working state.
- **Evidence:**
  - `src/activities/network/MeetingDownloadActivity.h:89`: `bool preventAutoSleep() override { return true; }`.
  - `src/activities/reader/BibleSearchActivity.h:50`: `return state == State::Building;` This is the confirm-dialog precedent the spec cites for D6.
  - `src/activities/catalog/CatalogSearchActivity.h:29`: `return state == State::FETCHING_INDEX || state == State::DOWNLOADING;`
  - `src/activities/ActivityManager.cpp:271` polls the current activity, and `src/main.cpp:649` reads the result every loop, so a state-gated override takes effect.
- **Fix:** In §D, specify `bool preventAutoSleep() override { return state == State::Downloading; }`. While the Wi-Fi picker is the current activity, its own override applies, not this one.

### MAJOR 2: A cancelled download is silent, which contradicts the issue, and no assumption records the departure

- **Claim** (§D.4 table): "`Cancelled` | `finish()` … No failure screen." No message of any kind is shown.
- **Problem:** The issue's Failure bullet says: "the existing `publication::Result` values (`NoMediaLink`, `DownloadFailed`, `ChecksumMismatch`, `OutOfMemory`, `Cancelled`) each map to a user-facing `tr()` message." The spec drops `Cancelled` without labelling the choice. D3 and D4 do label their departures, so the omission stands out. Showing a failure screen for a cancel would be wrong, but the issue's requirement can be met with no screen at all.
- **Evidence:**
  - The issue text is quoted above.
  - The spec at §D.4 and the Assumptions index (no entry for Cancelled).
  - `src/activities/PostedMessage.h:15` (`PostedMessage::post`) is the bottom toast from #153. It is drawn over the next screen that renders, which here is the launcher: `LauncherActivity.cpp:521` calls `PostedMessage::drawNext` after `displayBuffer`.
- **Fix:**
  - On `Cancelled`, call `PostedMessage::post(tr(STR_DOWNLOAD_CANCELLED))` before `finish()` or `onGoHome()`.
  - Add that key to the Strings table (english "Download cancelled. Nothing was saved.", spanish "Descarga cancelada. No se guardó nada.").
  - Record the choice as D7: a toast, not a failure screen.

### MAJOR 3: `registerBibleIfUnknown` is specified to log from a header that must stay firmware-free, and its `bool` return cannot tell a refusal from a skip

- **Claims:**
  - §"Pure helpers": "The header stays free of firmware includes, as its comment at `:6-7` requires."
  - §B: "`registerBibleIfUnknown` … logs `LOG_INF` on a write and `LOG_ERR` when `record` refuses."
  - The signature returns "whether it wrote".
  - Test 6: "`record` returning false → the helper returns false".
- **Problem:**
  1. Logging needs `Logging.h`. The real `lib/Logging/Logging.h` depends on Arduino's `HardwareSerial`, and the host stub is not on this suite's include path. So the helper either breaks the header's host-compilability, or the test CMake has to change, which contradicts "no CMake change" in §Architecture.
  2. If the logging moves to the call site instead, the `bool` return collapses three different outcomes into `false`: not a Bible, already registered, and `record` refused. The caller then cannot emit the `LOG_ERR` that the error-handling table relies on ("`record` refuses and logs").
- **Evidence:**
  - `src/activities/launcher/LauncherBible.h:6-7`: "Free of firmware includes so the filename rule can be tested on the host."
  - `test/stubs/Logging.h:3-4`: "Host-test stub for lib/Logging/Logging.h, which depends on Arduino's HardwareSerial and cannot compile on the host."
  - `test/launcher_bible/CMakeLists.txt:8-10`: the only include directory is `${REPO_ROOT}/src`.
  - `test/CMakeLists.txt:38-41`: `crosspoint_test_common` adds `${REPO_ROOT}` and `${REPO_ROOT}/lib`, but not `test/stubs`.
- **Fix:**
  - Return `enum class BibleRegistration : uint8_t { NotBible, AlreadyKnown, Recorded, Refused }` from the helper, and keep it free of logging.
  - In `EpubReaderActivity::loadBook`, log `LOG_INF` on `Recorded` and `LOG_ERR` on `Refused`.
  - Update test 6 to assert the enum. The last case becomes "`record` returning false → `Refused`, one call".

### MINOR 4: The firmware build step cannot pass as written, because the new `STR_*` keys are report-only

- **Claims:**
  - §Architecture, Strings row: "reported in the PR, not edited (ui-dev 'Shared files')".
  - §Testing: "`pio run` for `x4pro` …".
- **Problem:** `tr(STR_BIBLE_DOWNLOAD_TITLE)` and the other new keys are generated from `lib/I18n/translations/*.yaml`. If the YAML lines are absent, `I18nKeys.h` has no such enumerators and `pio run` fails. The spec never says how the worker gets a green build.
- **Evidence:**
  - `.claude/agents/ui-dev.md:22-27`.
  - CLAUDE.md, "Generated files": the I18n headers are produced from the YAML.
  - `grep -n 'STR_BIBLE_DOWNLOAD\|STR_CHOOSE_FILE\|STR_NO_WIFI_CONNECTION' lib/I18n/translations/*.yaml` returns nothing.
  - Precedent for editing in-branch: `docs/superpowers/specs/2026-09-27-issue-102-design.md:199-203` (A8).
- **Fix:** Say explicitly how the worker builds. Either apply the reported lines in the worktree for the build and hand them off, or edit the two YAMLs in-branch under a labelled assumption, as issue-102 A8 does. The additions are append-only.

### MINOR 5: Device check 5 misdescribes the no-Wi-Fi path, and the Cancel button is dead during the resolve

- **Claims:**
  - Device check 5: "With Wi-Fi off or out of range, tap and confirm. The screen says there is no connection and offers Retry".
  - §D.3: the progress screen shows "status, filename, bar, Cancel button".
- **Problem:**
  1. With no saved network in range, the auto-connect scan falls back to the picker's network list, and that list waits for the user. The failure screen appears only after the user backs out (D4 says this, but the device check does not).
  2. The model the spec copies arms `DOWNLOADING`, and with it the Cancel button, before `publication::download` runs its blocking resolve. `fetchUrl` pumps no input, so on a connected-but-offline network the visible Cancel does nothing for up to `HTTP_TIMEOUT_MS`. The issue's "not a hang" is borderline there, and the spec does not check that case on the device.
- **Evidence:**
  - `src/activities/network/WifiSelectionActivity.cpp:278-284`: auto-connect falls back to `NETWORK_LIST`.
  - `src/activities/network/MeetingDownloadActivity.cpp:282-291`: `DOWNLOADING` is set before `download()`.
  - `MeetingDownloadActivity.cpp:123-124`: "fetchUrl takes neither a progress nor a cancel hook".
  - `src/network/HttpDownloader.cpp:32`: `HTTP_TIMEOUT_MS = 60000`.
  - `PublicationDownloader.cpp:150-160`: the resolve runs inside `download`.
- **Fix:**
  - Show a status-only "Connecting…" or resolving state with no Cancel button, and switch to the progress layout in the `onResolved` hook.
  - Reword check 5 to "…back out of the network list; the screen says there is no connection and offers Retry".
  - Add a check for a network that is connected but has no internet: expect `STR_BIBLE_DOWNLOAD_FAILED_HINT` within about a minute.

### MINOR 6: "The tile then shows the Bible" depends on `record` succeeding, and that is unchecked

- **Claims:**
  - §C3: "The launcher re-resolves and the tile shows the Bible".
  - §D.4: "Both paths `record` `nwt`".
- **Problem:** The downloader ignores `record`'s return value. The file is named after `pubName`, so it is not CDN-named and the card scan cannot find it. It has never been opened, so recents cannot find it either. If the registry is unreadable or over budget, the tile keeps inviting a download after a successful one. Each retry then returns `AlreadyOnCard` and fails to record again.
- **Evidence:**
  - `src/network/PublicationDownloader.cpp:184,239` (return value discarded).
  - `src/network/MeetingFilename.cpp:55-58` (`publicationFilename` from `pubName`).
  - `src/study/PubKeyRegistry.cpp:25-27,41-44` (the refusals).
- **Fix:** Add a row to the error table: "Download succeeds but the registry refuses → the tile still offers the download. Accepted; the registry failure is already logged." Or, when `PubKeyRegistry::lookup(outPath)` is empty after `Ok`, post a toast. Either is fine as long as the spec states it.

### MINOR 7: With several registered Bibles, the first one registered wins permanently, which the spec does not mention

- **Problem:** Register-on-open makes several `nwt` entries common, for example a Spanish and an English NWT that have both been opened. `findBySymbol` returns the first matching entry in JSON order, which is insertion order. So the tile keeps the Bible that was registered first for as long as its file exists. Pre-#104, the tile showed the most recently opened Bible. B2 covers a Study Bible taking the tile, but not this case.
- **Evidence:** `src/study/PubKeyRegistry.cpp:64-76`.
- **Fix:** Add one sentence to B2 or the Non-goals section saying this is accepted and out of scope.

### MINOR 8: The research note's `&nbsp;` flag is not closed

- **Problem:** The research note asks the spec to check the English `pubName`, which carries a literal `&nbsp;` ("worth a check when the spec names the file", research lines 160-161). `sanitizeFilename` keeps `&` and `;`, so the English download is named `…(2013&nbsp;Revision).epub`. That name appears on the progress screen's filename line, and in the tile subtitle whenever the Bible is not in recents (the stem is cut to 40 characters, which probably hides it). Buscar already behaves this way today.
- **Evidence:** `src/util/StringUtils.cpp:23-32`; `MeetingFilename.cpp:55-58`.
- **Fix:** Record it as pre-existing and out of scope in Non-goals, so the flag is closed deliberately rather than dropped.

VERDICT: CLEAR
