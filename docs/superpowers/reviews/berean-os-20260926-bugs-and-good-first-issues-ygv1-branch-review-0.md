# Batch review 0: seven merged PRs (#118, #117, #121, #122, #123, #127, #126)

Scope: `main` at `c0c5db9f`, reviewed as one body of work. It covers the seams between tasks, not
each PR's internals, which the per-task reviews already covered. Every claim cites current `main`.

## Method

- Read `AGENTS.md`, all seven issue bodies (`gh issue view`), the #111 design and its PR body,
  and the shared-file sections of the other six PR bodies.
- Grepped `main` for raw SD-root literals, version idioms, adoption helpers and allocation sites.
- Checked every `test/*/` directory against `test/CMakeLists.txt`.
- Built and ran the whole host suite from a scratch harness outside the repo
  (`$SCRATCH/harness/CMakeLists.txt`). It does `add_subdirectory(<repo>/test)`, then adds
  `test/sd_paths` separately with the same C++20 settings. Result:
  `100% tests passed out of 950`. That is the 942 registered tests plus 8 `SdPaths.*`. No source
  file was modified.

## Findings

### MAJOR 1: #111's two shared-file lines were never applied, so `SdPathsTest` never runs and `main.cpp` keeps a raw literal

`data-dev` does not edit `test/CMakeLists.txt` or `src/main.cpp`. PR #118's body therefore asked
the orchestrator to apply two lines ("Shared-file lines for the orchestrator"; also the #111 design,
A8 and §"Shared-file lines for the PR description"). Neither was applied:

- **`test/CMakeLists.txt`** has no `add_subdirectory(sd_paths)`. `test/sd_paths/` was added in
  `67480012` and no later commit touches it, so the directory is not registered. The only
  unregistered directories are `build`, `epubs`, `language`, `stubs` (not suites) and `sd_paths`.
  CI's unit-test job has never built or run `SdPathsTest`, so the on-card path pinning protects
  nothing. #118's own intent review flagged this
  (`docs/superpowers/reviews/issue-111-pr-review-intent-0.md:85`), and the follow-through was
  lost. The suite does build and pass when registered: 8/8 in the harness above.
- **`src/main.cpp:228`**: `constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";` is
  still a raw literal next to `sdpaths::SLEEP_FRAME_FILE` (`lib/Serialization/SdPaths.h:17`). It is
  the only raw `"/.crosspoint"` or `"/.berean"` string left in `src/` or `lib/` outside `SdPaths.h`.
  The sweep `grep -rn -e '"/\.crosspoint' -e '"/\.berean' src lib` returns just this line and the
  comment at `src/util/RecentBooksDoc.h:44`. #111's "every use switched to it" is not quite met.

Every other PR's shared-file lines did land: `storage_io` (#121), `ditherers` (#122),
`format_version` and `persistable_store` (#123), and the `#ifdef ENABLE_SERIAL_LOG` around the
`CMD:` handler (#126, `src/main.cpp:623-638`). Only #118's were dropped, probably because it merged
first.

Severity: MAJOR, not BLOCKER. Behaviour is unchanged: the string is byte-identical, and the pinning
test passes when run. The fix is the two lines already written in PR #118's body. It reverses no
decision and needs no user judgment.

### MINOR 1: two format-version idioms now coexist

#101 added `persist::isKnownFormatVersion` (`lib/Serialization/FormatVersion.h:13`), which reads a
missing `"v"` as 1 and refuses anything `<= 0` or above the known version. Only the four inherited
stores use it (`src/CrossPointSettings.cpp:111`, `src/CrossPointState.cpp:65`,
`src/WifiCredentialStore.cpp:33`, `src/util/RecentBooksDoc.cpp:58`).

The fork's own stores still hand-roll their checks, often as `(doc["v"] | 0) > FORMAT_VERSION`:
`src/study/PubKeyRegistry.cpp:57,83`, `src/study/MigrationRunner.cpp:68,95`,
`src/network/MeetingWeekCache.cpp:24`, `lib/StudyStore/StudyStore/TagPalette.cpp:78`,
`ChapterCompletion.cpp:111`, `PassageDoc.cpp:221`, `lib/Epub/Epub/HighlightDoc.cpp:121`. The
older form accepts `v <= 0`. That is harmless today, because no build writes 0, but the rule now has
two spellings. #101 was scoped to the four stores, so this is a follow-up, not a defect.

### MINOR 2: no test covers #101's guard and #98's adoption together

The two changes compose correctly on reading. `PersistableStore<T>::loadFromFile`
(`lib/Serialization/PersistableStore.h:214-234`) still reads through `readDocFromFileAdopting`,
which shares the private `readAdopting` with #98's `loadAdopting`
(`lib/Serialization/PersistableStore.cpp:70-97`, `101-130`). So there is one adoption path, not
two. The cases:

- A future-version `.tmp` with no primary is promoted and returns `Ok`. `fromJson` then refuses it,
  and `persist::loadRefusedAfter` sets `loadRefused` (`FormatVersion.h:23-24`), so saves are
  blocked and the promoted file survives.
- A garbage `.tmp` returns `Missing`. That clears `loadRefused` (`FormatVersion.h:25-26`), and the
  next atomic save overwrites the `.tmp`. This is the A-12 rationale that #98 adopted
  (`TempAdoption.h:26-31`).

`test/persistable_store/PersistableStoreTest.cpp:62-106` only exercises a present primary, and
`test/storage_io/LoadAdoptingTest.cpp` exercises `loadAdopting`, not the CRTP path. The combined
case (primary missing, future-version `.tmp`) is untested. Both suites already link the #99 fake,
so it would be a one-test addition.

## Seams checked and found sound

- **Paths after #111.** #98, #101 and #108 introduced no new SD-root literals. #98's rewritten
  loaders use `sdpaths::` (for example `src/study/TagPaletteFile.cpp:8,36`), and
  `PersistableStore.cpp:13,24` uses `sdpaths::CROSSPOINT_DIR`.
- **#101's guard survives #98's rewrite of `PersistableStore.h/.cpp`.** `loadRefused` is at
  `PersistableStore.h:52`, `saveBlockedByRefusedLoad` at `:152-157`, and it is checked by both
  `saveToFile` (`:183`) and `saveToFileAtomic` (`:201`). The `loadRefusedAfter` update is at `:223`.
- **One adoption helper.** No `TempAdoptionAction` switch remains in `src/`; `grep` finds none. The
  five study loaders call `PersistableStoreBase::loadAdopting`: `TagPaletteFile.cpp:21`,
  `ChapterCompletionFile.cpp:21`, `PassageFile.cpp:60`, `BookmarkFile.cpp:37` and
  `HighlightFile.cpp:26`. PubKeyRegistry, MigrationRunner and MeetingWeekCache use
  `readDocFromFileAdopting`, which runs through the same `readAdopting`.
- **One storage fake.** `test/stubs/HalStorageFake.{h,cpp}` (#99) is the only fake. Both
  `test/storage_io` (#99 and #98 tests) and `test/persistable_store` (#101) link it. There are no
  duplicate fakes or helpers.
- **Test registration.** Every suite the batch added (`storage_io`, `ditherers`, `format_version`,
  `persistable_store`) is registered in `test/CMakeLists.txt`, and #98's three new test files are in
  `test/storage_io/CMakeLists.txt`. The one exception is `sd_paths` (MAJOR 1).
- **The four stores are wired and documented.** `WIFI_STORE.loadFromFile()` runs at boot with the
  other three (`src/main.cpp:412-418`), so the refusal guard is armed before the web server can save
  it. No code writes those four files except through `PersistableStore`: `grep` finds no other use
  of the four path constants. `docs/file-formats.md:377-475` documents the shared rules and each of
  the four files.
- **What each issue asked for is on `main`.**
  - #113: every `errorMessage_` literal and `main.cpp`'s SD error now go through `tr()`
    (`FontDownloadActivity.cpp:92-440`, `src/main.cpp:406`).
  - #112: the release env has `LOG_LEVEL=0` and no `ENABLE_SERIAL_LOG` (`platformio.ini:178-189`),
    and `AGENTS.md:176` names `gnu++2a`.
  - #99: its optional "also worth doing" ASan/UBSan CI job was explicitly deferred (issue-99
    design, A-18).
- **Host suite.** It is green on `main`: 942/942 registered tests.

## Recommended follow-up

Apply the two lines from PR #118's body:

- `add_subdirectory(sd_paths)` after `add_subdirectory(save_budget)` in `test/CMakeLists.txt`.
- `#include <SdPaths.h>`, and `constexpr const char* SLEEP_FRAME_FILE = sdpaths::SLEEP_FRAME_FILE;`
  in place of `src/main.cpp:228`.

VERDICT: CLEAR
