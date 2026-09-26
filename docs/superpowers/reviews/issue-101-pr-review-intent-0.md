# PR #123 intent review, pass 0 (issue #101)

Reviewed `gh pr diff 123` (branch `fix/101-inherited-store-format-version`, 8 implementation
commits `b145b4ca..791180ce` on top of `main` at `6c57bcd0`). I checked it against issue #101,
the spec `docs/superpowers/specs/2026-09-26-issue-101-design.md`, and the plan
`docs/superpowers/plans/2026-09-26-issue-101-plan.md`.

I built and ran the three suites this PR adds or changes, with the unstaged
`test/CMakeLists.txt` lines applied locally:

- `FormatVersionTest`: 9/9 passed.
- `PersistableStoreTest`: 5/5 passed.
- `RecentBooksDocTest`: 22/22 passed.

I did not re-run the firmware build.

## Issue acceptance criteria

| Criterion (#101) | Met | Evidence |
|---|---|---|
| Write `"v": 1` in each `toJson` | Yes | `src/CrossPointSettings.cpp:66`, `src/CrossPointState.cpp:47`, `src/WifiCredentialStore.cpp:14`, `src/util/RecentBooksDoc.cpp:44`; each `FORMAT_VERSION = 1` (`CrossPointSettings.h:432`, `CrossPointState.h:36`, `WifiCredentialStore.h:47`, `RecentBooksDoc.h:22`) |
| Missing version reads as 1 | Yes | `doc["v"] \| FORMAT_VERSION` at `CrossPointSettings.cpp:110`, `CrossPointState.cpp:64`, `WifiCredentialStore.cpp:32`, `RecentBooksDoc.cpp:56`. Tested by `AnAbsentVersionIsReadAsOneWithEveryEntry` and `ALegacyFileLoadsAndIsStampedOnTheNextSave` |
| Refuse anything greater than known, fall back to defaults | Yes | The check is the first statement of each `fromJson`, before any member is assigned. In `RecentBooksDoc.cpp:56-58` it comes before `books.clear()`. Tested by `ARefusedDocumentLeavesTheListUntouched` and the `value == 0` assertion in `PersistableStoreTest.cpp:66` |
| …without overwriting the file | Yes | `loadRefused` is set at `PersistableStore.h:225` via `persist::loadRefusedAfter` (`FormatVersion.h:21-32`). Both save paths check it (`PersistableStore.h:160-163`, `:181-184`). `ARefusedLoadBlocksEverySaveAndLeavesTheFileUntouched` asserts the file bytes are unchanged and that no `.tmp` was written |
| Document the four files in `docs/file-formats.md` | Yes | `docs/file-formats.md` gains a shared-rules section and one section each for `settings.json`, `state.json`, `wifi.json` and `recent.json`. I spot-checked the key names against `SettingsList.h:391` and `CrossPointState.cpp:54-59`, and they match |

## Spec coverage

- **A1:** `<= 0` and `> known` are refused, and a string `"v"` reads as 1. Covered by
  `FormatVersionTest.cpp` and `AStringVersionReadsAsTheDefault`.
- **A2:** the check comes first in each store. In the Wi-Fi store it sits after
  `credentialMutex` (`WifiCredentialStore.cpp:31-37`), as the spec says.
- **A3 / d1:**
  - The flag is in `PersistableStoreBase`, under `resaveRequested` (`PersistableStore.h:49-52`).
  - It is set only on an `Ok` read that `fromJson` rejects.
  - It is cleared on an accepted load or on `Missing`, and left alone on `Unreadable` or `ParseError`.
  - Every blocked save calls `LOG_ERR`.
  - `saveToFile()` is guarded too.
- **A5:** loading a legacy file does not trigger a resave. The load/save test proves this against the real template.
- **A6 / A9:** both pure helpers are `constexpr`, host-tested, and have `static_assert`s.
- **A7:** constant placement is as specified.
- **A8:** `"v"` is written first.
- **A10 / d2:** `WIFI_STORE.loadFromFile()` is at `src/main.cpp:418` with the include at `:32`. The PR body gives the hotspot data-loss fix its own bullet, as d2 requires.
- **Budget arithmetic:** 11421 → 11427 and 541 → 547 (`RecentBooksDocTest.cpp:42,50`), and `DOC_WRAPPER_BYTES` is 18 (`RecentBooksDoc.h:50`).
- **Comment rewrites:** all five listed in the spec are done: `RecentBooksDoc.h:49` and `:83-88`, `CrossPointState.h:33-34`, `CrossPointSettings.h:428-430`, and `SettingsSave.h:8-12`.
- **Testing item 4 (#99 dependency):** the suite runs on the landed #99 fake in `test/stubs`. It adds no second fake. `test/storage_io` is untouched.
- **`test/CMakeLists.txt`:** the two lines are left unstaged and handed over in the PR body, as the shared-append-point convention requires.

I checked for other paths that could bypass the guard. Nothing else in `src/` writes these four
files: `grep` for `writeDocToFile` finds only the study and bookmark stores. The only
`loadFromFile()` callers are the four boot loads plus the reloads the spec lists
(`LauncherActivity.cpp:98`, `PublicationsActivity.cpp:46`, `WifiSelectionActivity.cpp:106`).
The user-facing failure paths the spec's error table relies on exist:

- `CrossPointWebServer.cpp:1292-1295` returns HTTP 500.
- `sendCredentialEditFailure` is at `:1337`.

## Divergence from the plan

The implementation follows the plan task for task:

- Task 2.3, the `RecentBooksStore` shell: matches the plan's code verbatim.
- Task 3.1, `loadFromFile`: matches the plan's code verbatim.
- Task 8's test file: matches the plan's code verbatim.

Two things differ from the plan, and the PR's "Where the tree moved under the plan" section
explains both:

- `getFilePath()` now returns `sdpaths::*` (from #118).
- `test/CMakeLists.txt` gained #121's line.

One small unlisted edit: the worst-case estimate in `CrossPointState.h:34` changes from
~1,180 B to ~1,190 B. It is correct arithmetic, it follows from the one extra key, and it
is not a scope change.

No scope expansion. The only behaviour change beyond the version rule is the Wi-Fi boot
load, which d2 approved.

## Findings

### MINOR-1: the per-file `v` bullets state the lift condition more narrowly than the code

`docs/file-formats.md`: the `v` bullet under each of the four `### Version 1` headings (diff
lines for settings, state, wifi and recent) says the store "is not written until a build that
knows the format loads it". A `Missing` load also lifts the refusal
(`FormatVersion.h:25-26`, tested in `AMissingFileLiftsTheRefusal`). The shared-rules section
directly above states the full rule ("until a later load succeeds or finds no file"). This is
the wording the spec's Documentation section prescribed, so it is faithful to the spec. It is
still slightly incomplete. An optional fix: point the per-file bullets at the shared section
instead of restating the rule.

No BLOCKER or MAJOR findings. Every acceptance criterion in #101 and every spec requirement is
implemented and, where the host allows, tested by behaviour. The load/save suite exercises the
real `PersistableStore` template and `PersistableStore.cpp` on the fake, and asserts the file
bytes that result. It does not restate the implementation.

VERDICT: CLEAR
