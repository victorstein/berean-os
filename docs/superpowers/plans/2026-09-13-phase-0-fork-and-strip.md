# Phase 0 — Fork and Strip: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce the inherited CrossPoint fork to a single-board bereanOS skeleton that builds, flashes, releases under its own identity, and behaves exactly as it does today for everything that remains.

**Architecture:** Deletion in dependency order — leaf reader activities first, then their call sites, then the libraries, then the translation keys, then the build environments. Identity and release plumbing last, because they are the only steps that change how the artifact is named. The input layer is **not touched in this phase**; see the guard below.

**Tech Stack:** PlatformIO (ESP-IDF + Arduino), C++20 with no exceptions and no RTTI, a CMake host test suite, release-please, GitHub Actions.

---

## Read before starting

**This is a deletion phase, so the TDD loop inverts.** There is no failing test to write for removing a feature. Each task's verification triad is instead:

1. **Grep proves no dangling references** — the deletion is complete.
2. **`pio run -e x4pro` builds** — nothing that remains depended on it.
3. **The host suite passes** — nothing shared broke.

A task is not done until all three are green. Where a task *does* add code (the agent definitions, the board-tag change), normal discipline applies.

**Hard guard — do not touch the input layer in this phase.** `MappedInputManager` is 418 references across 121 files, sits in the `Activity` base-class constructor (`src/activities/Activity.h:22,28-29`), and *implements* this device's Back gesture via a left-edge swipe (`src/MappedInputManager.cpp:266,301`). Deleting it here would leave the device with no way out of any screen for the whole of Phase 1. It is removed in Phase 2, in the same change that lands its replacement. If a task below seems to require touching it, stop and escalate.

**Acceptance for the phase as a whole:**

| | |
|---|---|
| Binary shrinks | `firmware-x4pro.bin` measurably smaller than the 5,591,088 B baseline; expect ~229 KB |
| Everything builds | `pio run -e x4pro` and `pio run -e x4pro-gh_release` |
| Host suite green | Every test in `test/` except the ones deliberately deleted |
| On-device smoke | Every remaining activity entered and exited once on hardware |

---

## File structure

Deleted wholesale:

| Path | What it is |
|---|---|
| `lib/OpdsParser/`, `src/OpdsServerStore.*`, `src/util/OpdsFilename.*` | OPDS catalog client |
| `src/activities/browser/`, `src/activities/settings/Opds*` | OPDS browse + settings UI |
| `lib/KOReaderSync/`, `src/activities/reader/KOReaderSyncActivity.*`, `src/activities/settings/KOReader*` | KOReader progress sync |
| `src/util/Dictionary.*`, `src/util/DictionaryRegistry.*`, `src/util/DictZip.*`, `src/util/DictHtmlPages.*` | Dictionary lookup |
| `src/activities/reader/Dictionary*Activity.*` | Dictionary UI |
| `lib/Txt/`, `lib/Xtc/`, `src/activities/reader/TxtReaderActivity.*`, `src/activities/reader/Xtc*` | TXT / Markdown / XTC readers |
| `test/opds_filename/` | Test for deleted code |

Modified to drop their references:

| Path | Why it appears |
|---|---|
| `src/main.cpp` | OPDS and KOReader wiring |
| `src/activities/ActivityManager.cpp` | OPDS activity routing |
| `src/activities/home/HomeActivity.{h,cpp}` | OPDS entry point, Xtc format handling |
| `src/activities/settings/SettingsActivity.{h,cpp}` | OPDS, KOReader, Dictionary settings rows |
| `src/SettingsList.h` | KOReader and Dictionary setting definitions |
| `src/CrossPointSettings.{h,cpp}` | OPDS, KOReader, Dictionary persisted fields |
| `src/network/CrossPointWebServer.{h,cpp}`, `src/network/html/SettingsPage.html` | OPDS server management endpoints |
| `src/activities/reader/ReaderActivity.cpp:11,15,16,31-36` | TXT/XTC format dispatch |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | KOReader sync + Dictionary hooks |
| `src/activities/reader/ReaderUtils.h` | Dictionary hook |
| `src/util/BookCacheUtils.cpp`, `src/RecentBooksStore.cpp`, `src/activities/boot_sleep/SleepActivity.cpp` | Txt/Xtc format branches |
| `lib/GfxRenderer/GfxRenderer.h` | Dictionary reference |
| `lib/I18n/translations/*.yaml` (33 files) | Strings for deleted features |
| `platformio.ini` | 13 environments → 3; identity |
| `scripts/git_branch.py:76,79,85` | Reads `[crosspoint] version`, gates on env names |
| `src/network/FirmwareBoardTag.cpp:10,14,16` | Board names |
| `release-please-config.json`, `version.txt`, `CHANGELOG.md` | Release identity |
| `.github/workflows/*` | CI for deleted boards |

Created:

| Path | Responsibility |
|---|---|
| `.claude/agents/epub-dev.md` | Owns `lib/Epub` — parsers, unit addressing, caches |
| `.claude/agents/ui-dev.md` | Owns `src/activities`, FreeInkUI, `GfxRenderer` |
| `.claude/agents/net-dev.md` | Owns `src/network` — downloads, catalog, OTA, web server |
| `.claude/agents/hal-dev.md` | Owns `lib/hal` and the `freeink-sdk` boundary |

---

## Task 1: Record the baseline

**Files:**
- Create: `docs/superpowers/notes/phase-0-baseline.md`

- [ ] **Step 1: Bootstrap the worktree**

A fresh worktree needs its submodule and a clang-format 21 venv before anything works.

```bash
git submodule update --init --recursive
```

- [ ] **Step 2: Build the release target and record its size**

```bash
pio run -e x4pro-gh_release
ls -l .pio/build/x4pro-gh_release/firmware.bin
```

Expected: builds with no errors. Note the byte count.

- [ ] **Step 3: Run the host suite and record the result**

```bash
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass. Note the count.

- [ ] **Step 4: Write the baseline note**

Create `docs/superpowers/notes/phase-0-baseline.md`:

```markdown
# Phase 0 baseline — <date>

Measured before any deletion, on commit <sha>.

- `x4pro-gh_release` firmware.bin: <N> bytes
- Published crosspoint reference (`firmware-x4pro.bin`): 5,591,088 bytes
- Host suite: <N> tests, all passing
- `pio run -e x4pro` (dev, LOG_LEVEL=2): builds

Phase 0 succeeds when the firmware is smaller, the host suite is still green
minus deliberately deleted tests, and every remaining activity has been entered
and exited once on hardware.
```

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/notes/phase-0-baseline.md
git commit -m "docs: record the Phase 0 baseline before stripping"
```

---

## Task 2: Delete the TXT and XTC readers

These are the leaves — nothing else depends on them, so they go first.

**Files:**
- Delete: `lib/Txt/`, `lib/Xtc/`, `src/activities/reader/TxtReaderActivity.{h,cpp}`, `src/activities/reader/XtcReaderActivity.{h,cpp}`, `src/activities/reader/XtcReaderChapterSelectionActivity.{h,cpp}`
- Modify: `src/activities/reader/ReaderActivity.cpp`, `src/util/BookCacheUtils.cpp`, `src/RecentBooksStore.cpp`, `src/activities/home/HomeActivity.cpp`, `src/activities/boot_sleep/SleepActivity.cpp`

- [ ] **Step 1: List every reference before touching anything**

```bash
grep -rn "TxtReader\|XtcReader\|Txt\.h\|Xtc\.h\|hasXtcExtension\|hasTxtExtension\|hasMarkdownExtension" src lib --include=*.cpp --include=*.h
```

Expected: hits in the five files listed above plus the files being deleted. If a file appears that is not on that list, stop and update the plan.

- [ ] **Step 2: Simplify the reader factory**

In `src/activities/reader/ReaderActivity.cpp`, delete the includes at lines 15-16 (`TxtReaderActivity.h`, `XtcReaderActivity.h`) and collapse the three-branch dispatch at lines 31-36 to the EPUB case only:

```cpp
  activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
```

- [ ] **Step 3: Remove the format branches from the remaining four call sites**

In `src/util/BookCacheUtils.cpp`, `src/RecentBooksStore.cpp`, `src/activities/home/HomeActivity.cpp` and `src/activities/boot_sleep/SleepActivity.cpp`, delete the `#include <Txt.h>` / `#include <Xtc.h>` lines and every branch that tests for a `.txt`, `.md` or `.xtc` extension, keeping the EPUB path.

Leave `FsHelpers::hasXtcExtension` and friends in place for now if other code still calls them — Step 5's grep decides.

- [ ] **Step 4: Delete the files**

```bash
git rm -r lib/Txt lib/Xtc \
  src/activities/reader/TxtReaderActivity.h src/activities/reader/TxtReaderActivity.cpp \
  src/activities/reader/XtcReaderActivity.h src/activities/reader/XtcReaderActivity.cpp \
  src/activities/reader/XtcReaderChapterSelectionActivity.h \
  src/activities/reader/XtcReaderChapterSelectionActivity.cpp
```

- [ ] **Step 5: Prove no dangling references remain**

```bash
grep -rn "TxtReader\|XtcReader\|Txt\.h\|Xtc\.h" src lib --include=*.cpp --include=*.h
```

Expected: no output. If `FsHelpers::hasXtcExtension` is now unreferenced, delete it too and re-run.

- [ ] **Step 6: Build and test**

```bash
pio run -e x4pro
cd test && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: both succeed.

- [ ] **Step 7: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop the TXT and XTC readers

bereanOS reads EPUB publications only. The reader factory now has a
single branch."
```

---

## Task 3: Delete the dictionary

**Files:**
- Delete: `src/util/Dictionary.{h,cpp}`, `src/util/DictionaryRegistry.{h,cpp}`, `src/util/DictZip.cpp`, `src/util/DictHtmlPages.{h,cpp}`, `src/activities/reader/DictionaryDefinitionActivity.{h,cpp}`, `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}`, `docs/dictionary.md`, `scripts/generate_dictionary_synonyms_test_epub.py`, `test/epubs/test_dictionary_synonyms.epub`
- Modify: `src/SettingsList.h`, `src/CrossPointSettings.{h,cpp}`, `src/util/StringUtils.h`, `src/activities/reader/EpubReaderActivity.{h,cpp}`, `src/activities/reader/ReaderUtils.h`, `src/activities/settings/SettingsActivity.cpp`, `lib/GfxRenderer/GfxRenderer.h`, `lib/I18n/translations/english.yaml`

- [ ] **Step 1: Confirm `PassageSelectActivity` is not actually coupled**

`PassageSelectActivity` mentions `DictionaryWordSelectActivity` four times, and this is the selection UI bereanOS keeps — so verify the mentions are comments, not code:

```bash
grep -n -i "dict" src/activities/reader/PassageSelectActivity.h src/activities/reader/PassageSelectActivity.cpp
```

Expected: every hit is on a line beginning with `//` (they are comments about snapshot sizing at `PassageSelectActivity.h:63,155` and `PassageSelectActivity.cpp:47,79`). If any hit is an `#include` or a call, **stop and escalate** — the deletion order needs rethinking.

- [ ] **Step 2: List every reference**

```bash
grep -rn "Dictionary\|DictZip\|DictHtml\|dictionary" src lib --include=*.cpp --include=*.h
```

Record the list. It should match the Files block above.

- [ ] **Step 3: Remove the reader hooks**

In `src/activities/reader/EpubReaderActivity.{h,cpp}` and `src/activities/reader/ReaderUtils.h`, delete the dictionary includes, the menu action that opens the definition activity, and any member holding a dictionary handle. In `lib/GfxRenderer/GfxRenderer.h`, delete the dictionary reference.

- [ ] **Step 4: Remove the settings**

In `src/SettingsList.h` delete the dictionary setting rows; in `src/CrossPointSettings.{h,cpp}` delete the corresponding persisted fields and their defaults; in `src/activities/settings/SettingsActivity.cpp` delete the row that navigates to dictionary settings.

Dropping a persisted field is safe here: `PersistableStore` tolerates unknown and missing keys, so an existing settings file simply stops carrying it.

- [ ] **Step 5: Update the comments in `PassageSelectActivity`**

Those four comments now reference a class that no longer exists. Rewrite each to describe the constraint without the dead name — for example, at `PassageSelectActivity.cpp:47`, replace the reference to `DictionaryWordSelectActivity::SNAPSHOT_CAPACITY` with the reason the capacity differs: a passage snapshot spans multiple lines, so it is sized for `MAX_SELECTED_SNAPSHOT_LINES` rather than a single word.

- [ ] **Step 6: Delete the files**

```bash
git rm src/util/Dictionary.h src/util/Dictionary.cpp \
  src/util/DictionaryRegistry.h src/util/DictionaryRegistry.cpp \
  src/util/DictZip.cpp src/util/DictHtmlPages.h src/util/DictHtmlPages.cpp \
  src/activities/reader/DictionaryDefinitionActivity.h \
  src/activities/reader/DictionaryDefinitionActivity.cpp \
  src/activities/reader/DictionaryWordSelectActivity.h \
  src/activities/reader/DictionaryWordSelectActivity.cpp \
  docs/dictionary.md scripts/generate_dictionary_synonyms_test_epub.py \
  test/epubs/test_dictionary_synonyms.epub
```

- [ ] **Step 7: Remove the translation keys**

Delete the dictionary `STR_*` keys from `lib/I18n/translations/english.yaml`. Other languages fall back to English, so a key absent from English but present elsewhere is an orphan — remove it from every file that has it:

```bash
grep -rln "STR_DICT" lib/I18n/translations/
```

Then delete those lines from each file listed, and regenerate:

```bash
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

The three generated files are gitignored; only the YAML is committed.

- [ ] **Step 8: Verify, build, test**

```bash
grep -rn "Dictionary\|DictZip\|DictHtml" src lib --include=*.cpp --include=*.h
pio run -e x4pro
cd test && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: no grep output; both build and tests succeed.

- [ ] **Step 9: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop the dictionary

Out of scope for a study firmware. PassageSelectActivity's comments
referenced DictionaryWordSelectActivity for snapshot sizing; they now state
the constraint directly."
```

---

## Task 4: Delete KOReader sync

**Files:**
- Delete: `lib/KOReaderSync/`, `src/activities/reader/KOReaderSyncActivity.{h,cpp}`, `src/activities/settings/KOReaderAuthActivity.{h,cpp}`, `src/activities/settings/KOReaderSettingsActivity.{h,cpp}`
- Modify: `src/main.cpp`, `src/SettingsList.h`, `src/CrossPointSettings.cpp`, `src/activities/reader/EpubReaderActivity.{h,cpp}`, `src/activities/settings/SettingsActivity.{h,cpp}`, `lib/I18n/translations/*.yaml` (33 files)

- [ ] **Step 1: List every reference**

```bash
grep -rn "KOReader\|KOSync\|kosync" src lib --include=*.cpp --include=*.h
```

- [ ] **Step 2: Remove the reader integration**

In `src/activities/reader/EpubReaderActivity.{h,cpp}`, delete the sync client member, the include, the progress-push calls and the menu action that opens the sync activity. In `src/main.cpp`, delete the sync wiring and credential-store initialisation.

- [ ] **Step 3: Remove the settings**

Delete the KOReader rows from `src/SettingsList.h`, the persisted credential fields from `src/CrossPointSettings.cpp`, and the settings navigation from `src/activities/settings/SettingsActivity.{h,cpp}`.

- [ ] **Step 4: Delete the files**

```bash
git rm -r lib/KOReaderSync \
  src/activities/reader/KOReaderSyncActivity.h src/activities/reader/KOReaderSyncActivity.cpp \
  src/activities/settings/KOReaderAuthActivity.h src/activities/settings/KOReaderAuthActivity.cpp \
  src/activities/settings/KOReaderSettingsActivity.h src/activities/settings/KOReaderSettingsActivity.cpp
```

- [ ] **Step 5: Remove the translation keys from all 33 languages**

The keys are `STR_KOREADER_SYNC`, `STR_KOREADER_USERNAME`, `STR_KOREADER_PASSWORD`, `STR_KOREADER_AUTH`, `STR_KOSYNC` and `STR_KOREADER_SETUP_HINT` (in `english.yaml` at lines 126, 144, 145, 152, 187, 360). Every translation file carries its own copies:

```bash
for f in lib/I18n/translations/*.yaml; do
  sed -i '' '/^STR_KOREADER_/d; /^STR_KOSYNC:/d' "$f"
done
grep -rn "STR_KOREADER\|STR_KOSYNC" lib/I18n/translations/
```

Expected: no output from the grep.

- [ ] **Step 6: Regenerate, verify, build, test**

```bash
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
grep -rn "KOReader\|KOSync" src lib --include=*.cpp --include=*.h
pio run -e x4pro
cd test && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: no grep output; both succeed. A `STR_*` key still referenced in C++ but deleted from the YAML fails the build at the generated enum, which is the safety net working.

- [ ] **Step 7: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop KOReader sync

bereanOS is standalone and is the source of truth for its own study data."
```

---

## Task 5: Delete OPDS

The largest deletion, because it reaches the web server and the home screen.

**Files:**
- Delete: `lib/OpdsParser/`, `src/OpdsServerStore.{h,cpp}`, `src/util/OpdsFilename.{h,cpp}`, `src/activities/browser/`, `src/activities/settings/OpdsSettingsActivity.{h,cpp}`, `src/activities/settings/OpdsServerListActivity.{h,cpp}`, `test/opds_filename/`
- Modify: `src/main.cpp`, `src/CrossPointSettings.h`, `src/activities/ActivityManager.cpp`, `src/activities/home/HomeActivity.{h,cpp}`, `src/activities/settings/SettingsActivity.cpp`, `src/network/CrossPointWebServer.{h,cpp}`, `src/network/html/SettingsPage.html`, `test/CMakeLists.txt`, `lib/I18n/translations/*.yaml`

- [ ] **Step 1: List every reference**

```bash
grep -rn "Opds\|OPDS" src lib test --include=*.cpp --include=*.h --include=*.txt
```

- [ ] **Step 2: Remove the home-screen entry and routing**

In `src/activities/home/HomeActivity.{h,cpp}`, delete the browse entry point and its include. In `src/activities/ActivityManager.cpp`, delete the OPDS activity routing. In `src/main.cpp`, delete the server-store initialisation.

- [ ] **Step 3: Remove the web server endpoints**

In `src/network/CrossPointWebServer.{h,cpp}`, delete the OPDS server-management handlers and their route registrations. In `src/network/html/SettingsPage.html`, delete the OPDS section. The HTML is the source; `src/network/html/*.generated.h` is regenerated at build time and must not be edited or committed.

- [ ] **Step 4: Remove the settings**

Delete the OPDS fields from `src/CrossPointSettings.h` and the settings row from `src/activities/settings/SettingsActivity.cpp`.

- [ ] **Step 5: Delete the files and the dead test**

```bash
git rm -r lib/OpdsParser src/activities/browser test/opds_filename \
  src/OpdsServerStore.h src/OpdsServerStore.cpp \
  src/util/OpdsFilename.h src/util/OpdsFilename.cpp \
  src/activities/settings/OpdsSettingsActivity.h src/activities/settings/OpdsSettingsActivity.cpp \
  src/activities/settings/OpdsServerListActivity.h src/activities/settings/OpdsServerListActivity.cpp
```

Then remove the `opds_filename` entry from `test/CMakeLists.txt`.

- [ ] **Step 6: Remove the translation keys**

There are seven OPDS keys in `english.yaml`:

```bash
for f in lib/I18n/translations/*.yaml; do sed -i '' '/^STR_OPDS/d' "$f"; done
grep -rn "STR_OPDS" lib/I18n/translations/
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

Expected: no grep output.

- [ ] **Step 7: Verify, build, test**

```bash
grep -rn "Opds\|OPDS" src lib test --include=*.cpp --include=*.h --include=*.txt
pio run -e x4pro
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: no grep output; both succeed. `cmake -B build` is re-run here because `CMakeLists.txt` changed.

- [ ] **Step 8: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop the OPDS browser

Publications come from jw.org, not an OPDS catalog. Removes the browse
activity, the server store, the web-server management endpoints and the
settings surface."
```

---

## Task 6: Prune the board environments

**Files:**
- Modify: `platformio.ini`, `scripts/git_branch.py:85`, `src/network/FirmwareBoardTag.cpp:10-16`

- [ ] **Step 1: Delete the non-X4-Pro environments**

`platformio.ini` has thirteen. Delete `[env:default]` (161), `[env:gh_release]` (172), `[env:gh_release_rc]` (182), `[env:slim]` (192), `[env:sticky]` (207), `[env:sticky-gh_release]` (217), `[env:sticky-gh_release_rc]` (228), `[env:papermono]` (294), `[env:papermono-gh_release]` (309) and `[env:papermono-gh_release_rc]` (322), along with their comment banners.

Keep `[env:x4pro]`, `[env:x4pro-gh_release]` and `[env:x4pro-gh_release_rc]`.

Note: this frees **no flash** — PlatformIO compiles one environment at a time. It removes repo surface and CI time, which is the actual gain.

- [ ] **Step 2: Fix the dev-version gate**

`scripts/git_branch.py:85` gates on a tuple of environment names, two of which no longer exist:

```python
    if env['PIOENV'] not in ('x4pro',):
```

- [ ] **Step 3: Reduce the board tag to one board**

`src/network/FirmwareBoardTag.cpp` defines `CROSSPOINT_BOARD_NAME` per board at lines 10, 14 and 16. Collapse the conditional chain to the single X4 Pro case, keeping the name `"x4pro"` for now — the OTA asset naming depends on it and is not renamed until Task 8.

- [ ] **Step 4: Build both remaining targets**

```bash
pio run -e x4pro
pio run -e x4pro-gh_release
```

Expected: both succeed. The release build must still produce a version string; check the build log shows `CROSSPOINT_VERSION`.

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "build: reduce to the X4 Pro target only

Ten environments removed. This frees no flash -- PlatformIO builds one env
at a time -- but removes repo surface and CI time."
```

---

## Task 7: Rename the project identity

**Files:**
- Modify: `platformio.ini` (the `[crosspoint]` section and every `${crosspoint.version}` reference), `scripts/git_branch.py:76,79`, `README.md`, `SCOPE.md`, `GOVERNANCE.md`, `USER_GUIDE.md`, `ROADMAP.md`

- [ ] **Step 1: Rename the version section**

In `platformio.ini`, rename `[crosspoint]` to `[berean]`, preserving the release-please block markers exactly:

```ini
[berean]
; Block markers, not the inline `x-release-please-version` form: an inline
; comment survives scripts/git_branch.py's configparser and would end up inside
; BEREAN_VERSION for the dev builds.
# x-release-please-start-version
version = 0.1.0
# x-release-please-end
```

Then update every `${crosspoint.version}` reference in the three remaining environments to `${berean.version}`.

- [ ] **Step 2: Update the version reader**

`scripts/git_branch.py` reads the old section name at lines 76 and 79:

```python
    if not config.has_option('berean', 'version'):
        warn('No [berean] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('berean', 'version')
```

- [ ] **Step 3: Decide the build-flag name**

`CROSSPOINT_VERSION` is referenced in `platformio.ini` and in `scripts/git_branch.py:94`, and read somewhere in `src/`. Find every use before renaming:

```bash
grep -rn "CROSSPOINT_VERSION" src lib scripts platformio.ini
```

Rename all of them to `BEREAN_VERSION` in one pass so nothing is left reading a macro that is no longer defined. The same applies to `CROSSPOINT_BOARD_NAME` in `src/network/FirmwareBoardTag.cpp`.

Leave the `.crosspoint/` **SD cache directory name alone** — renaming it orphans every cache and every reading position on the card, and Phase 1 has its own migration to run. Change it there or not at all.

- [ ] **Step 4: Rewrite the project documents**

`README.md`, `SCOPE.md`, `ROADMAP.md` and `USER_GUIDE.md` all describe CrossPoint's product. Replace each with bereanOS's, drawing scope from `docs/superpowers/specs/2026-09-13-berean-os-design.md`. `GOVERNANCE.md` describes CrossPoint's contribution model and does not apply to a personal fork — delete it.

- [ ] **Step 5: Build and verify the version string**

```bash
pio run -e x4pro 2>&1 | grep -i "version"
```

Expected: a dev version string built from `0.1.0` plus branch and short SHA.

- [ ] **Step 6: Commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: rename the project to bereanOS

Version section, build flags, board tag and project docs. The .crosspoint/
SD cache directory keeps its name -- renaming it would orphan every cached
book and reading position, and Phase 1 owns that migration."
```

---

## Task 8: Reset the release plumbing

**This task requires a decision from the user and touches infrastructure outside this repo. Stop at Step 1 and escalate.**

**Files:**
- Modify: `release-please-config.json`, `version.txt`, `CHANGELOG.md`

- [ ] **Step 1: Escalate the ownership question**

In crosspoint-x4pro, `release-please-config.json`, `.release-please-manifest.json`, `version.txt` and `.github/workflows/release-please.yml` are **pushed by stein-infra's tofu**, not maintained in-repo (see `AGENTS.md`). bereanOS is not onboarded there.

Present the user with the choice — onboard `berean-os` in stein-infra's `tofu/repos.tf` the way crosspoint-x4pro was, or manage these files in-repo — and **wait for an answer**. Creating a GitHub repository or editing stein-infra is outward-facing and is the orchestrator's gate, not a delegated task.

- [ ] **Step 2: Reset the version to a new product's first version**

`version.txt` currently reads `1.6.0`, inherited from crosspoint. bereanOS has shipped nothing:

```bash
echo "0.1.0" > version.txt
```

This must match the `[berean] version` set in Task 7.

- [ ] **Step 3: Set the bootstrap SHA to the fork point**

`release-please-config.json` carries `"bootstrap-sha": "af9e352fd6ba8742bb11f7325f618f913ca19c2c"` and `"package-name": "crosspoint-x4pro"`. Left alone, the first release changelog replays **1,256 inherited commits** of someone else's history — this exact failure has already happened once on crosspoint.

Set the bootstrap SHA to bereanOS's founding commit and the package name to the new project:

```bash
git log --oneline | tail -n +1 | grep "found bereanOS" # confirm the sha
```

Then edit `release-please-config.json`:

```json
  "bootstrap-sha": "<sha of the 'docs: found bereanOS' commit>",
  "packages": {".":{"changelog-path":"CHANGELOG.md","package-name":"berean-os","release-type":"simple"}},
```

Leave `"extra-files": ["platformio.ini"]` — that is what bumps the version block Task 7 renamed.

- [ ] **Step 4: Empty the inherited changelog**

`CHANGELOG.md` is crosspoint's release history. Replace its contents with a single line:

```markdown
# Changelog
```

- [ ] **Step 5: Commit**

```bash
git add release-please-config.json version.txt CHANGELOG.md
git commit -m "chore: reset the release identity to bereanOS 0.1.0

The inherited bootstrap-sha would have replayed 1,256 commits of crosspoint
history into the first changelog."
```

---

## Task 9: Prune the CI workflows

**Files:**
- Modify: `.github/workflows/ci.yml`, `.github/workflows/release-publish.yml`
- Delete: `.github/workflows/release-fonts.yml`, `.github/workflows/release_candidate.yml`

- [ ] **Step 1: Reduce the build matrix to one board**

`ci.yml` builds every board. Reduce its matrix to `x4pro` only. Confirm the trigger targets `main` — CrossPoint's `ci.yml` once triggered on `master`, which meant pushes to `main` went unchecked.

- [ ] **Step 2: Reduce the release build**

`release-publish.yml` builds all four boards and attaches four binaries. Reduce it to `x4pro-gh_release`, producing one asset. Keep the asset name `firmware-x4pro.bin` — the OTA updater matches on it.

- [ ] **Step 3: Delete the workflows that no longer apply**

```bash
git rm .github/workflows/release-fonts.yml .github/workflows/release_candidate.yml
```

`release-fonts.yml` publishes font bundles bereanOS does not ship separately; `release_candidate.yml` builds RC firmware for a release cadence this project does not have yet. Both can return if wanted.

- [ ] **Step 4: Validate the YAML parses**

```bash
for f in .github/workflows/*.yml; do python3 -c "import yaml,sys; yaml.safe_load(open('$f'))" && echo "ok $f"; done
```

Expected: `ok` for each remaining file.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "ci: build one board

Matrix reduced to x4pro across CI and release-publish. Drops the font and
release-candidate workflows."
```

---

## Task 10: Write the scoped agent definitions

Four surfaces, one definition each, modelled on the `nicaraguan-laws` pattern: frontmatter, a pointer to the authoritative guide, the prime directive, where things live, and what to mirror.

**Files:**
- Create: `.claude/agents/epub-dev.md`, `.claude/agents/ui-dev.md`, `.claude/agents/net-dev.md`, `.claude/agents/hal-dev.md`

- [ ] **Step 1: Confirm the surface boundaries with the user**

The four surfaces below are a proposal, not an inherited fact. Before writing them, confirm the split — agents will route by it, and a wrong boundary means two agents editing the same files, which the workflow forbids.

- [ ] **Step 2: Write `.claude/agents/epub-dev.md`**

```markdown
---
name: epub-dev
description: Use for changes under lib/Epub — EPUB parsing, pagination, unit addressing (verse and data-pid scanners), section and metadata caches, and the on-disk formats they own. Mirrors existing patterns; escalates before inventing new ones.
---

You are **epub-dev**, owner of `lib/Epub` in bereanOS — the parsing and layout
engine every reading surface sits on.

## Read first
`AGENTS.md` is authoritative for hardware constraints, memory rules and storage
discipline. `docs/contributing/development-workflow.md` is authoritative for
process. **If they ever conflict with this file, they win.**

## Prime directive — follow the existing pattern, escalate before inventing
1. Find the nearest existing example in this library and mirror it — structure,
   naming, error handling, and its host test. Name the file you modelled on in
   your first message.
2. If `lib/Epub` has no way to do what the task needs, **stop and ask.** A new
   dependency, a new on-disk format, or a new cross-cutting mechanism is a user
   decision.

## Where things live (`lib/Epub`)
- `Epub.{h,cpp}` — the archive façade: spine, manifest, href resolution.
- `Epub/parsers/` — expat-based streaming parsers. `ContentOpfParser.cpp` reads
  the spine; `ChapterHtmlSlimParser.cpp` is the layout parser, and its
  `isNonNavigableInlineElement` (line 108) classifies `span` as inline, which is
  why marker ids on spans are invisible to the id-harvester.
- `Epub/VerseAnchors.{h,cpp}` — offset→(chapter, verse) scanning for Bible EPUBs.
  **This does not generalise**: it hardcodes the `id` attribute and a
  `chapter%u_verse%u` grammar (lines 30-41). A `data-pid` scanner is a second
  scanner sharing only the `VisibleOffsetCounter` + expat skeleton.
- `Epub/Section.{h,cpp}`, `Epub/BookMetadataCache.{h,cpp}` — the on-disk caches.
  **Increment the format version before changing a binary structure**, and note
  that `BookMetadataCache` validates on version only — never size or mtime — so
  replacing a file in place does not self-invalidate.
- `Epub/BibleNavScanner.{h,cpp}`, `Epub/BibleChapterNumber.{h,cpp}` — Bible
  navigation pages.

## The pattern to mirror for a new scanner
`VerseAnchors::Scanner` — chunk-fed, expat behind an opaque `void*` so the header
stays parser-free, no `HalStorage` and no Arduino, with a host test under
`test/`. A unit that needs hardware to test will not be tested.

## Constraints that bite here
- `std::string_view` is not null-terminated; convert explicitly at any C API.
- `.reserve(N)` before every `push_back` loop.
- Never bare `new` — `makeUniqueNoThrow` from `lib/Memory/Memory.h`.
- Never call SdFat directly; everything goes through `HalStorage`.
```

- [ ] **Step 3: Write `.claude/agents/ui-dev.md`**

```markdown
---
name: ui-dev
description: Use for changes under src/activities, lib/GfxRenderer and the FreeInkUI layer — screens, lists, the launcher, selection, rendering and theming. Mirrors existing patterns; escalates before inventing new ones.
---

You are **ui-dev**, owner of `src/activities`, `lib/GfxRenderer` and the
FreeInkUI surface in bereanOS.

## Read first
`AGENTS.md` is authoritative for hardware constraints and memory rules.
`docs/contributing/development-workflow.md` is authoritative for process.
**If they conflict with this file, they win.**

## Prime directive — follow the existing pattern, escalate before inventing
1. Find the nearest existing screen and mirror it — lifecycle, list plumbing,
   theming, touch routing. Name the file you modelled on in your first message.
2. If no existing screen does what the task needs, **stop and ask** before
   introducing a new interaction pattern.

## Do not touch the input layer without an explicit instruction
`MappedInputManager` is 418 references across 121 files, sits in the `Activity`
base-class constructor (`src/activities/Activity.h:22,28-29`), and implements
this device's Back gesture (`src/MappedInputManager.cpp:266,301`). It is replaced
in Phase 2 as a single planned change. Removing or bypassing it piecemeal leaves
the device with no way out of a screen.

## Where things live
- `src/activities/Activity.{h,cpp}` — the lifecycle: `onEnter`, `loop`,
  `onExit`. Activities are heap-allocated and **deleted on exit**: anything
  allocated in `onEnter` is freed in `onExit`, tasks are `vTaskDelete`d there,
  and member `FsFile` handles are closed there.
- `src/activities/UiListActivity.{h,cpp}` — the list screen base.
- `src/activities/UiTabListActivity.{h,cpp}` — lists with a tab band. The ring
  is position 0 = tab bar, 1..N = rows.
- `src/activities/reader/` — the reading surface. `PassageSelectActivity` is the
  two-anchor selection modal; `NumberGridLayout.h` is the grid used by the
  chapter and verse pickers; `ReturnStack.h` is the cross-reference return ring
  (`CAPACITY = 3`, silently evicts the oldest).
- `lib/GfxRenderer/` — drawing. All UI goes through the `GUI` macro (`UITheme`);
  never hardcode a font, colour or position, and never assume 800 or 480 — use
  `getScreenWidth()` / `getScreenHeight()`.

## Constraints that bite here
- **All user-facing text uses `tr(STR_*)`.** Logging may be hardcoded; UI never.
- The panel is 1-bit on this unit — no grayscale, no anti-aliasing. A full
  refresh is ~1.7 s; there is no windowed black-and-white update primitive.
  Design interactions around a small number of deterministic refreshes.
- `'\n'` breaks lines when measured but not when drawn. Never rely on it in a
  list row.
- An uncapped `value` slot on a list item steals the label's width.
```

- [ ] **Step 4: Write `.claude/agents/net-dev.md`**

```markdown
---
name: net-dev
description: Use for changes under src/network — publication downloads, the jw.org catalog and pub-media clients, OTA updates, and the on-device web server. Mirrors existing patterns; escalates before inventing new ones.
---

You are **net-dev**, owner of `src/network` in bereanOS.

## Read first
`AGENTS.md` is authoritative — in particular its storage-discipline section.
`docs/contributing/development-workflow.md` is authoritative for process.
**If they conflict with this file, they win.**

## Prime directive — follow the existing pattern, escalate before inventing
1. Mirror the nearest existing client. `PubMediaJson.{h,cpp}` and
   `WolWeekScan.{h,cpp}` are the pattern: pure, chunk-fed, no Arduino, no
   `HalStorage`, each with a host test. Name what you modelled on.
2. If nothing here does what the task needs, **stop and ask** before adding a
   dependency or a new protocol.

## Where things live
- `PubMediaJson.{h,cpp}` — streaming parser for `GETPUBMEDIALINKS`. Keeps its own
  container stack because the response carries a decoy `pubImage.url` ahead of
  `files`.
- `WolWeekScan.{h,cpp}` — resolves an ISO week to publication issues from
  wol.jw.org. The year in a link path is the *publication's* year, not the
  week's, which is why the matcher is year-less.
- `HttpDownloader.{h,cpp}` — downloads. `MeetingFilename.{h,cpp}` — readable
  names.
- `OtaUpdater.{h,cpp}`, `OtaVersion.h`, `OtaBootSwitch.{h,cpp}` — self-update.
- `CrossPointWebServer.{h,cpp}` — the on-device server. Note it exposes
  `POST /delete`, so it is a **second writer** to anything on the card.

## Constraints that bite here
- **Storage discipline is not optional.** `SDCardManager::readFile` silently
  truncates at 50,000 bytes; `PersistableStore::saveToFile` is non-atomic. Every
  store writes through `writeDocToFileAtomic`, checks a byte budget before
  writing, and streams anything over ~40 KB.
- Suppress auto-sleep while a transfer is in flight — it triggers on *input*
  inactivity, which a download does not reset, and deep sleep mid-write
  corrupts the file.
- Download to `<name>.part` and rename on completion, so a partial file is never
  mistaken for a complete one.
- TLS here is `setInsecure()`, not CA-verified. Do not claim otherwise.
```

- [ ] **Step 5: Write `.claude/agents/hal-dev.md`**

```markdown
---
name: hal-dev
description: Use for changes under lib/hal and at the freeink-sdk boundary — display, storage, GPIO, power and board configuration. Mirrors existing patterns; escalates before inventing new ones.
---

You are **hal-dev**, owner of `lib/hal` and the `freeink-sdk` boundary in
bereanOS.

## Read first
`AGENTS.md` is authoritative for hardware facts and memory rules.
`docs/contributing/development-workflow.md` is authoritative for process.
**If they conflict with this file, they win.**

## Prime directive — follow the existing pattern, escalate before inventing
1. Mirror the nearest existing HAL wrapper. Name what you modelled on.
2. `freeink-sdk` is a **submodule** — do not edit it to fix something that
   belongs in the HAL. If a change genuinely belongs upstream, **stop and ask.**

## Where things live
- `lib/hal/HalStorage.{h,cpp}` — the `Storage` singleton and `HalFile`.
- `lib/hal/HalDisplay.{h,cpp}` — `RefreshMode` is FULL / HALF (~1720 ms) / FAST.
  There is no windowed black-and-white update. This unit's panel may be SSD1677
  or UC8179 depending on production batch; the UC8179 has no grayscale.
- `lib/hal/HalGPIO.{h,cpp}` — buttons and the GT911 capacitive Home key
  (`hasHomeKey()`, `wasHomeKeyTapped()`, `wasHomeKeyLongPressed()`, lines
  166-170).

## Constraints that bite here
- **SdFat is not thread-safe.** Everything goes through `HalStorage`, which
  serialises on `storageMutex`. Calling SdFat, `SdSpiCard`, `FsBaseFile` or
  `SDCardManager` directly bypasses the mutex and panics FreeRTOS.
- `DESTRUCTOR_CLOSES_FILE=1`: do **not** call `close()` on a local `HalFile`.
  Do close before deleting the same path, before reopening the same variable,
  and for members at their release point.
- ISR handlers are `IRAM_ATTR`; data they read is `DRAM_ATTR`. A flash-resident
  `static const` read from an ISR faults.
- `xSemaphoreTake` cannot be called from an ISR. Use the `FromISR` variants.
- Never lock `storageMutex` on a read path that the renderer sits behind.
```

- [ ] **Step 6: Commit**

```bash
git add .claude/agents/
git commit -m "docs: add the scoped agent definitions

Four surfaces -- epub, ui, net, hal -- each carrying the prime directive,
where things live, and the constraints that actually bite there. ui-dev
carries an explicit prohibition on touching the input layer, which Phase 2
replaces as one planned change."
```

---

## Task 11: Measure and smoke-test

**Files:**
- Modify: `docs/superpowers/notes/phase-0-baseline.md`

- [ ] **Step 1: Build the release target and compare**

```bash
pio run -e x4pro-gh_release
ls -l .pio/build/x4pro-gh_release/firmware.bin
```

Expected: smaller than the Task 1 baseline. The measured estimate is ~229 KB (about 4%); a saving under 150 KB means a deletion was incomplete — re-run the greps from Tasks 2-5.

- [ ] **Step 2: Run the full host suite**

```bash
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all pass, with `opds_filename` gone and everything else intact.

- [ ] **Step 3: Confirm the format check the way CI does**

```bash
./bin/clang-format-fix
git diff --exit-code
```

Expected: no diff. `-g` is not sufficient here — it skips files that are committed and no longer modified, which is exactly how a clean local tree fails CI.

- [ ] **Step 4: Flash and smoke-test on hardware**

This step is the human's. Flash the dev build over USB, then enter and exit every remaining activity once: home, reader, chapter select, verse grid, footnotes, bookmarks, highlights, tag picker, passage select, meeting download, settings and each settings sub-screen, WiFi, and the firmware-update screen.

Watch for: a screen that no longer opens, a settings row that navigates nowhere, an empty list where a deleted feature used to be, and untranslated `STR_*` keys rendering literally.

- [ ] **Step 5: Record the result**

Append to `docs/superpowers/notes/phase-0-baseline.md`:

```markdown
## Phase 0 result — <date>

- `x4pro-gh_release` firmware.bin: <N> bytes (was <baseline>, saved <delta>)
- Host suite: <N> tests, all passing
- Device smoke: every remaining activity entered and exited — <notes>
```

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/notes/phase-0-baseline.md
git commit -m "docs: record the Phase 0 result"
```

---

## Self-review notes

**Spec coverage.** Phase 0's row in the spec's build-order table asks for: delete OPDS, KOSync, dictionary, TXT, XTC and the other board targets (Tasks 2-6); rename to `berean-os` (Task 7); carry CI, release-please and OTA (Tasks 8-9); leave the input layer untouched (guarded at the top and in `ui-dev.md`); acceptance by binary size and an on-device pass of every remaining activity (Tasks 1 and 11). The agent definitions added at the user's request are Task 10.

**Deliberately deferred to Phase 1 or later**, though a reader might expect them here: the `.crosspoint/` SD directory name (renaming orphans every cache and reading position; Phase 1 owns that migration), `ReturnStack`'s capacity, and anything touching the storage format.

**Two tasks stop for a human.** Task 8 Step 1 needs a decision about stein-infra ownership and may require creating a GitHub repository — outward-facing, so it is the orchestrator's gate. Task 10 Step 1 needs the surface boundaries confirmed before agents start routing by them. Task 11 Step 4 is device work only the user can do.
