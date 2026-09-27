Tier: heavy

# Issue #156 — plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-27-issue-156-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-156-design.md`
Base checked: `95d80662` (branch head; source identical to `d956cc8b`).

## What was checked, and how

- **Plan against spec.** Every numbered decision (A1–A4, B1–B8, C1–C3, D1–D7, E1), the D.4 outcome table, the error-handling table and host tests 1–6 were traced to a plan step. None is missing (see the map below).
- **Plan against the tree.** Every quoted "old" block was compared with the file it replaces. All of them match: `LauncherBible.h:3-7`, `LauncherActivity.h:75`, `LauncherActivity.cpp:108-117`, `:183-189` and `:547-556` (`openBible`), and `EpubReaderActivity.cpp:257-264`. Each API the new code calls was checked at its declaration:
  - `publication::Request`/`Hooks`/`Result`/`failureMessage` (`PublicationDownloader.h:18-66`);
  - `PubKeyRegistry::record`/`lookup` (`PubKeyRegistry.h:22,25`) and `study::RegisteredPub{symbol, issue, language}` (`PubKey.h:17-21`);
  - `Epub::getPath`/`getBibleBookNavSpineIndex` (`Epub.h:56,92`);
  - `CrossPointSettings::PUB_LANG_ENGLISH`/`langWritten` (`CrossPointSettings.h:69,72`);
  - the `WifiSelectionActivity` constructor (`WifiSelectionActivity.h:168-169`);
  - `ActivityManager::goToFileBrowser` and the global (`ActivityManager.h:86,116`) and `PostedMessage::post`/`drawNext` (`PostedMessage.h:16,22`);
  - `Activity::preventAutoSleep`/`onGoHome`/`requestUpdateAndWait` (`Activity.h:42,45,70`) and `UiAppHost::routeTouch` (`UiAppHost.h:67`);
  - every `tr()` key the new screen reuses (`STR_LANG_ENGLISH`, `STR_LANG_SPANISH`, `STR_BIBLE`, `STR_DOWNLOAD`, `STR_RETRY`, `STR_CANCEL`, `STR_BACK`, `STR_DOWNLOADING`, `STR_CONNECTING`, `STR_DOWNLOAD_FAILED`), each present once in `english.yaml`.
- **The dialog and progress code, against its models.** Task 6's `buildDialog` matches `BibleSearchActivity.cpp:744-782` line for line, minus the single-button branch it does not need. `buildProgressScreen`, the three hook trampolines and `render` match `MeetingDownloadActivity.cpp:225-258,332-375,396-407`. The only differences are the ones the spec asks for: no phase line, a `Resolving` state with no Cancel, the D.4 outcome mapping, and a gated `preventAutoSleep`.
- **Order of `publication::download`'s hooks.** `onResolved` fires before `migrateCdnNamedCopy`, the `AlreadyOnCard` check and the transfer (`PublicationDownloader.cpp:175-185`). So the Resolving→Downloading switch in `onDownloadResolved` lands after the blocking resolve fetch, and only then does Cancel appear. That is what D3 and the spec's MINOR 5 require.
- **Tasks 1–3 run literally.** I applied the plan's code blocks for Tasks 1–3 unchanged to a throwaway copy of the tree and built `LauncherBibleTest` with the host CMake. The result was **18 tests, all passing**, the count the plan predicts (`plan:317`, `:1091`), with no warnings from the suite's own sources. Each task's "expected compile error" step also holds, because each test names a symbol that task introduces.
- **Task 4 run literally.** I applied the six appends and two value changes, then ran `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`: exit 0, and both new keys appear in `I18nKeys.h`. Neither proposed key collides with an existing one, and `spanish.yaml` ends with a newline.
- **The build lock and the file lock.** `pio-locked.sh` exists at the path the plan names (`plan:35`). All five `FILES:` lines (`plan:6-10`) start at column 0, sit outside any code fence, and hold repo-relative paths. Together they cover every file a task edits or creates: `LauncherBible.h`, `LauncherBibleTest.cpp`, `LauncherActivity.{h,cpp}`, `EpubReaderActivity.cpp`, `BibleDownloadActivity.{h,cpp}` and `{english,spanish}.yaml`. The generated i18n headers from Task 4.3 are gitignored build output that every `pio run` rewrites anyway. `test/CMakeLists.txt:121` already registers `launcher_bible`, so leaving it untouched is correct.

### Spec → plan map

| Spec | Plan |
|---|---|
| A1 match rule, verbatim and case-sensitive | Task 2 (`plan:179-209`) |
| A2 recents hit must exist | Task 7.4 (`plan:1043-1050`) |
| A3 order, lazy | Task 1 (`plan:131-157`), tests `:63-89` |
| A4 no launcher registration | Task 7 writes nothing to the registry |
| B, B1, B3, B6, B7 register on open, outside the PSRAM gate | Task 3 (`plan:302-314`) and Task 5.2 (`plan:416-439`) |
| B4 empty language, B8 via `record` | Task 5.2 (`plan:430`) |
| MAJOR 3 enum result, caller logs | Task 3 and Task 5.2 (`plan:432-436`) |
| C1 subtitle text | Task 4.1/4.2 |
| C2, C3 tap → download screen → back to the launcher, re-resolve | Task 7.5 (`plan:1067-1084`) |
| D, D1, D2, D6 confirm dialog, size constant, Choose a file | Task 6 (`plan:589`, `:614-616`, `:669-675`) |
| D3, D4, D7 outcome mapping | Task 6 `runDownload` (`plan:793-819`) and `onWifiSelectionComplete` (`:703-709`) |
| `preventAutoSleep` gated (MAJOR 1) | `plan:493` |
| Resolving with no Cancel (MINOR 5) | `plan:711-738`, `:893-896`, `:932-936` |
| OOM at the launcher | Task 7.5 `makeUniqueNoThrow` (`plan:1072-1076`) |
| Host tests 1–6 | Tasks 1–3 |
| Firmware build, whole-tree format | Tasks 5, 6, 7 and 8 |
| Device checklist | PR section (`plan:1132`) |

## Findings

No BLOCKER or MAJOR findings.

### MINOR 1 — The log name the tester is told to look for is not the one the code prints

- **Claim.** The PR's device checklist is copied verbatim from the spec (`plan:1132`). Spec device check 2 tells the tester that serial shows ``Bible: … (Registry)`` (`spec:439-440`).
- **Problem.** `bibleLookupName` returns `"registry"`, `"card scan"` and `"recents"` in lower case (`plan:136-146`), and a test pins those strings (`plan:91-95`). A tester who searches the log for `(Registry)` will not find it.
- **Evidence.** `plan:139` returns `"registry"`, and `spec:440` expects `(Registry)`.
- **Fix.** In the PR's copy of device check 2, write `Bible: … (registry)`. Changing the returned strings instead would also mean changing the test.

### MINOR 2 — The flash-figure instruction names the wrong baseline, and no base figure is ever taken

- **Claim.** Step 7.6 compares the Task 7 flash figure with "the figure from the Task 5 build (which predates the new screen being reachable, not its compilation …)" (`plan:1092-1094`).
- **Problem.** The Task 5 build runs before Task 6 creates the screen, so it predates the screen's compilation too, and the parenthetical is wrong. The spec also asks for "the `pio run` size delta" in the PR (`spec:396-397`). A delta for the whole change needs a figure from base, and the Task 5 build already contains the registration code and the strings.
- **Evidence.** Task order: 5 (`plan:386`) comes before 6 (`plan:453`). The plan never builds base.
- **Fix.** Report the Task 7 figure against the size of the `main` build that CI already produced, or say plainly that the Task 5→7 delta covers the screen and the launcher change only. Drop the parenthetical either way.

### MINOR 3 — Tasks 4–7 do not start with a failing test (accepted, not a defect)

- **Claim.** The brief asks for every step to start with a failing test. Tasks 4 (strings), 5 (reader wiring), 6 (the new activity) and 7 (launcher wiring) instead end with a `pio run`.
- **Problem.** This is not really a problem. The rules those tasks wire in are pinned by Tasks 1–3, and the spec's testing strategy lists only host tests 1–6 (`spec:399-432`), all of which Tasks 1–3 write first. The new screen is Arduino- and `WiFi`-bound, and its model has no host test either (`plan:458-459`). The only untested logic of any weight is A2's existence check in `findBibleInRecents`, which is five lines and a `Storage.exists` call. Covering it would need the activity or the `HalStorageFake`, which is more machinery than the check warrants.
- **Evidence.** `plan:329`, `:388`, `:458-459`.
- **Fix.** None required. If the implementer wants the check pinned, `findBibleInRecents` could be lifted into `LauncherBible.h` as a template over an `exists` callable, tested like `registerBibleIfUnknown`. That change is optional.

## Verdict

The plan is concrete enough to execute literally. Tasks 1–3 and Task 4 were run as written, and they compile, pass and generate as the plan predicts. The Task 6 code copies its two models closely, and every call was checked against its declaration. Each spec decision maps to a step, and the `FILES:` lines cover every touched file. The three MINORs can be fixed inline.

VERDICT: CLEAR
