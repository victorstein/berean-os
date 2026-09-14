# Phase 0 — Fork and Strip: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce the inherited CrossPoint fork to a single-board bereanOS skeleton that builds, flashes, and self-updates from its own releases — with everything that remains behaving exactly as it does today.

**Architecture:** Identity and release safety first, because a device that can reach CrossPoint's releases will flash them over itself. Then the agent guide, because everything defers to it. Then deletions in order of increasing coupling. Docs and agent definitions last.

**Tech Stack:** PlatformIO (ESP-IDF + Arduino), C++20 with no exceptions and no RTTI, a CMake host test suite, release-please seeded by stein-infra's tofu, GitHub Actions.

> **Revision note.** The first version of this plan was reviewed adversarially and failed. It would have shipped a device that flashes CrossPoint firmware over itself on the first update check, because `OTA_RELEASE_REPO` was never renamed while two of its own steps preserved the asset name and board tag that make the collision match. It also reset the wrong version file, missed fourteen source files across four deletions, and told the engineer to renumber a persisted enum. Ordering, verification gates and file lists below are all rebuilt from that review.

---

## Read before starting

**This is a deletion phase, so the TDD loop inverts.** There is no failing test to write for removing a feature. But the inherited verification triad — grep, build, host suite — is close to a **null test** over exactly the surfaces these deletions touch: the host suite (`test/CMakeLists.txt`) compiles nothing from `src/activities`, `src/SettingsList.h`, `src/CrossPointSettings.*`, `src/network/html/` or `lib/I18n`. A deleted settings row, a broken menu index, an orphaned string and an unstyled web page all pass it cleanly.

So each deletion task carries **five** gates, not three:

1. **Grep proves no dangling references** — using patterns that match the *symbols*, not just the filenames.
2. **`pio run -e x4pro` builds**, and `pio check` passes (CI runs it with no `-e`).
3. **The host suite passes.**
4. **The settings-schema snapshot diff shows only intended removals** (Task 1 creates it).
5. **The i18n orphan check is clean** (Task 1 creates it).

Plus a device smoke test **after the deletions** (Task 12), not only at the end — Tasks 2-5 and 13-14 cannot break a screen, so flashing once mid-plan halves the bisect range.

**Hard guard — do not touch the input layer.** `MappedInputManager` is ~432 references across 122 files, sits in the `Activity` base-class constructor (`src/activities/Activity.h:22,28-29`), and *implements* this device's Back gesture via a left-edge swipe (`src/MappedInputManager.cpp:266,301`). It is removed in Phase 2, in the same change that lands its replacement. If a task seems to require touching it, stop and escalate.

**Second hard guard — persisted enums keep their numeric values.** Deleting an enumerator that a settings file stores by number silently reassigns every user who had it. Leave holes; never renumber. This applies to `LP_MENU_*` in `src/CrossPointSettings.h:149-163` and is called out in the tasks that touch it.

**`sed -i ''` is macOS-only.** Every `sed` below uses the BSD form because the executor is on Darwin. In a Linux container it fails with `sed: can't read : No such file or directory` — use `perl -i -ne` there.

**Acceptance for the phase:**

| | |
|---|---|
| No self-overwrite | A release build queried against its own repo finds no CrossPoint release |
| Binary shrinks | Dev build (`-e x4pro`) 200-230 KB smaller than the Task 1 baseline |
| Everything builds | `pio run`, `pio run -e x4pro-gh_release`, `pio check` |
| Host suite green | All tests except `opds_filename`, deliberately deleted |
| Schema + i18n clean | Settings snapshot diff intended-only; zero orphaned `STR_*` |
| On-device smoke | Every remaining activity entered and exited once |

---

## Prerequisite — resolved before execution

**Release plumbing is stein-infra's, and the repo must arrive clean.**

`stein-infra/tofu/release-please.tf:84-120` seeds `.release-please-manifest.json` (line 90) and `version.txt` (line 112) from a `seed_version`, then `lifecycle { ignore_changes = [content] }` hands them to release-please permanently. `tofu/repos.tf` shows the clean onboarding shape:

```hcl
"berean-os" = {
  visibility  = "public"
  description = "A JW study firmware for the Xteink X4 Pro"
  topics      = ["esp32", "eink", "epub", "firmware", "platformio", "ereader"]

  release_please = {
    release_type = "simple"
    package_name = "berean-os"
    seed_version = "0.0.0"
  }
}
```

**This only works if the repo has no inherited release state.** The clone carries `.release-please-manifest.json` at `{".":"1.6.0"}`, `version.txt` at `1.6.0`, and the tag `v1.6.0`. Task 3 removes all three *before* the first push; the tag is never pushed. Otherwise release-please reads `1.6.0` from the manifest and cuts `v1.7.0` for a product that has shipped nothing — and `extra-files` rewrites `platformio.ini` back over whatever version Task 2 set.

Creating the repository and opening the stein-infra PR are **outward-facing and the orchestrator's gate**, not delegated work.

---

## File structure

Deleted wholesale:

| Path | What it is |
|---|---|
| `lib/OpdsParser/`, `src/OpdsServerStore.*`, `src/util/OpdsFilename.*` | OPDS catalog client |
| `src/activities/browser/`, `src/activities/settings/Opds*` | OPDS browse + settings UI |
| `lib/KOReaderSync/`, `src/activities/reader/KOReaderSyncActivity.*`, `src/activities/settings/KOReader*` | KOReader progress sync |
| `src/util/Dictionary.*`, `src/util/DictionaryRegistry.*`, `src/util/DictZip.{h,cpp}`, `src/util/DictHtmlPages.*` | Dictionary lookup |
| `src/activities/reader/Dictionary*Activity.*` | Dictionary UI |
| `lib/Txt/`, `lib/Xtc/`, `src/activities/reader/TxtReaderActivity.*`, `src/activities/reader/Xtc*` | TXT / Markdown / XTC readers |
| `test/opds_filename/` | Test for deleted code |
| `.release-please-manifest.json`, `version.txt`, `release-please-config.json`, `.github/workflows/release-please.yml` | Inherited release state; tofu recreates them |
| `GOVERNANCE.md`, `docs/dictionary.md` | CrossPoint's contribution model; deleted feature's docs |

Created:

| Path | Responsibility |
|---|---|
| `scripts/settings_snapshot.py` | Dumps the settings key set for before/after diffing |
| `scripts/i18n_orphans.sh` | Lists `STR_*` keys defined but no longer referenced |
| `lib/Serialization/PersistableStore.{h,cpp}` *(modified)* | Gains `saveToFileAtomic()` and a byte-budget hook |
| `test/persistable_store/` | Host test for the above |
| `.claude/agents/{epub,ui,net,hal,data}-dev.md` | Five scoped agent definitions |

Modified — the full list, corrected against the tree:

| Path | Why |
|---|---|
| `platformio.ini` | `default_envs`, `OTA_RELEASE_REPO`, `[crosspoint]`→`[berean]`, 13 envs → 3 |
| `scripts/git_branch.py:76,79,85,94,109` | Section name, env tuple, macro name, self-test env |
| `src/network/FirmwareBoardTag.cpp` | Nine board defines → one, keeping the `#error` fallback |
| `src/network/OtaUpdater.cpp:25` | Fallback `OTA_RELEASE_REPO` |
| `.github/workflows/ci.yml`, `release-publish.yml` | Matrix, and the repo guard at `release-publish.yml:112` |
| `AGENTS.md` | Prune the 1,070 inherited lines |
| `src/activities/reader/ReaderActivity.cpp:15,16,31-37` | TXT/XTC dispatch |
| `lib/FsHelpers/FsHelpers.{h,cpp}`, `src/activities/home/FileBrowserActivity.cpp:70-71`, `src/components/UITheme.cpp:136,139`, `src/util/NextBookFinder.cpp:18-19`, `src/network/WebDAVHandler.cpp:804` | Txt/Xtc extension helpers and their callers |
| `src/util/BookCacheUtils.cpp`, `src/RecentBooksStore.cpp`, `src/activities/boot_sleep/SleepActivity.cpp` | Txt/Xtc format branches |
| `src/activities/reader/EpubReaderMenuActivity.{h,cpp}` | Dictionary and Sync menu rows |
| `src/activities/reader/EpubReaderActivity.{h,cpp}`, `ReaderUtils.h` | Dictionary and KOReader hooks |
| `src/CrossPointSettings.{h,cpp}` | Persisted fields; `LP_MENU_*` holes |
| `src/SettingsList.h` | Dictionary, KOReader and OPDS rows |
| `src/activities/settings/SettingsActivity.{h,cpp}` | Settings rows and the `SettingAction` enum |
| `src/activities/ActivityManager.{h,cpp}` | `HomeMenuItem::OPDS_BROWSER`, `goToBrowser()` |
| `src/activities/home/HomeActivity.{h,cpp}` | Menu index arithmetic, Xtc handling |
| `src/activities/network/MeetingDownloadActivity.cpp:56` | Reads `SETTINGS.opdsDownloadFolder` |
| `src/network/CrossPointWebServer.{h,cpp}`, `src/network/html/SettingsPage.html` | OPDS endpoints; shared `.opds-*` CSS |
| `src/activities/util/KeyboardEntryActivity.cpp:89` | `/opds` URL snippet |
| `src/main.cpp` | OPDS and KOReader wiring |
| `lib/GfxRenderer/GfxRenderer.h` | Dictionary reference |
| `lib/I18n/translations/*.yaml` (32 files) | Strings for deleted features |
| `test/CMakeLists.txt:56` | `add_subdirectory(opds_filename)` |
| `README.md`, `SCOPE.md`, `ROADMAP.md`, `USER_GUIDE.md`, `docs/webserver*.md`, `docs/contributing/*.md`, `.github/ISSUE_TEMPLATE/*`, `.github/PULL_REQUEST_TEMPLATE.md`, `.skills/scope-discipline/SKILL.md` | CrossPoint branding and deleted features |
| `.gitignore:26` | `.claude/*` blocks the agent definitions |

---

## Task 1: Baseline and the two missing gates

**Files:**
- Create: `docs/superpowers/notes/phase-0-baseline.md`, `scripts/settings_snapshot.py`, `scripts/i18n_orphans.sh`

- [ ] **Step 1: Bootstrap the worktree**

```bash
git submodule update --init --recursive
python3 -m venv .venv && ./.venv/bin/pip install 'clang-format==21.*'
```

The clang-format venv is required before `./bin/clang-format-fix` works.

- [ ] **Step 2: Build the dev target and record its size**

```bash
pio run -e x4pro
ls -l .pio/build/x4pro/firmware.bin
```

Measure the **dev** build, not the release build. The 200-230 KB expectation was derived by summing object contributions out of `.pio/build/x4pro/firmware.map`, which is `LOG_LEVEL=2`. Comparing a release build against it mixes two different `.rodata` log-string sets and one published binary from another commit — three measurements presented as one, which is what the first version of this plan did.

- [ ] **Step 3: Write the i18n orphan checker**

Create `scripts/i18n_orphans.sh`:

```bash
#!/usr/bin/env bash
# Lists STR_* keys defined in english.yaml but no longer referenced in code.
# gen_i18n.py builds the key list from English only and strips unused keys at
# build time, so an orphan costs no flash -- but it hides a half-finished
# deletion, which is what this catches.
set -euo pipefail
cd "$(dirname "$0")/.."
grep -o '^STR_[A-Z0-9_]*' lib/I18n/translations/english.yaml | sort -u | while read -r key; do
  if ! grep -rq "\b${key}\b" src lib --include=*.cpp --include=*.h; then
    echo "orphan: $key"
  fi
done
```

```bash
chmod +x scripts/i18n_orphans.sh && ./scripts/i18n_orphans.sh | tee /tmp/i18n-orphans-baseline.txt
```

Record the baseline output — the tree may already have orphans, and only *new* ones matter.

- [ ] **Step 4: Write the settings-schema snapshot**

Create `scripts/settings_snapshot.py`:

```python
#!/usr/bin/env python3
"""Dump the settings key set from SettingsList.h for before/after diffing.

The host suite compiles nothing from SettingsList.h or CrossPointSettings.*, so a
row deleted by accident -- or one left behind pointing at a deleted store -- is
invisible to every other gate in this phase.
"""
import re
import sys
from pathlib import Path

src = Path(__file__).resolve().parent.parent / "src" / "SettingsList.h"
text = src.read_text(encoding="utf-8")
keys = sorted(set(re.findall(r"StrId::(STR_[A-Z0-9_]+)", text)))
print("\n".join(keys))
```

```bash
chmod +x scripts/settings_snapshot.py
python3 scripts/settings_snapshot.py > /tmp/settings-baseline.txt
wc -l /tmp/settings-baseline.txt
```

- [ ] **Step 5: Run the host suite**

```bash
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Write the baseline note**

Create `docs/superpowers/notes/phase-0-baseline.md` recording: the commit SHA, the dev `firmware.bin` byte count, the host test count, the settings key count, and the baseline orphan list. State that Phase 0 succeeds when the dev build is 200-230 KB smaller, the host suite is green minus `opds_filename`, the settings diff shows only intended removals, no new orphans appear, and the device smoke passes.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/notes/phase-0-baseline.md scripts/settings_snapshot.py scripts/i18n_orphans.sh
git commit -m "test: add the settings-schema and i18n-orphan gates

The host suite compiles nothing from SettingsList.h, CrossPointSettings or
lib/I18n, so the deletions in this phase are invisible to it."
```

---

## Task 2: Identity and OTA safety

**This task comes first because everything after it is safe only once it lands.** A build carrying CrossPoint's OTA repo will, on its first update check, find `v1.6.0`, judge it newer than `0.1.0`, match the asset name and the board tag, and flash CrossPoint over bereanOS.

**Files:**
- Modify: `platformio.ini:2,50`, the `[crosspoint]` section and the three `x4pro` envs; `scripts/git_branch.py:76,79,85,94,109`; `src/network/OtaUpdater.cpp:25`; `src/network/FirmwareBoardTag.cpp`

- [ ] **Step 1: Point OTA at bereanOS**

`platformio.ini:50` currently reads:

```ini
  -DOTA_RELEASE_REPO=\"victorstein/crosspoint-x4pro\"
```

Change it to:

```ini
  -DOTA_RELEASE_REPO=\"victorstein/berean-os\"
```

And the compile-time fallback at `src/network/OtaUpdater.cpp:25`:

```cpp
#define OTA_RELEASE_REPO "victorstein/berean-os"
```

Until berean-os publishes its first release, the update check correctly reports "no update available" — which is the safe answer, unlike the current one.

- [ ] **Step 2: Fix the default environment**

`platformio.ini:2` reads `default_envs = default`, and Task 5 deletes that env. CI's cppcheck job runs `pio check` with **no `-e`**, so it resolves this:

```ini
default_envs = x4pro
```

- [ ] **Step 3: Rename the version section**

Rename `[crosspoint]` to `[berean]`, preserving the release-please block markers exactly — `extra-files` keys on the markers, not the section name, so the rename is safe:

```ini
[berean]
; Block markers, not the inline `x-release-please-version` form: an inline
; comment survives scripts/git_branch.py's configparser and would end up inside
; BEREAN_VERSION for the dev builds.
# x-release-please-start-version
version = 0.0.0
# x-release-please-end
```

`0.0.0` matches the `seed_version` stein-infra will seed. Update every `${crosspoint.version}` reference in the three remaining envs to `${berean.version}`.

- [ ] **Step 4: Rename the version macro everywhere at once**

```bash
grep -rn "CROSSPOINT_VERSION" src lib scripts platformio.ini .github
```

Rename every hit to `BEREAN_VERSION` in one pass — a partial rename leaves code reading a macro that is no longer defined. This includes `scripts/git_branch.py:94`.

- [ ] **Step 5: Update the version reader and its self-test**

`scripts/git_branch.py` reads the old section at lines 76 and 79 and gates on env names at line 85:

```python
    if not config.has_option('berean', 'version'):
        warn('No [berean] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('berean', 'version')
```

```python
    if env['PIOENV'] not in ('x4pro',):
```

Only `x4pro` belongs in that tuple: the two `gh_release` envs set the version from `${berean.version}` via `build_flags` and must not be overridden.

Line 109's direct-run self-test hardcodes `'PIOENV': 'default'`, which after this change returns immediately and prints nothing. Change it to `'x4pro'`. Also update the cosmetic `CrossPoint build version:` strings at lines 3, 5, 6, 73 and 95 — Task 5's verification greps the build log for this text.

- [ ] **Step 6: Reduce the board tag to one board, carefully**

`src/network/FirmwareBoardTag.cpp` has **nine** `#define CROSSPOINT_BOARD_NAME` lines (10, 12, 14, 16, 18, 20, 22, 24, 26) and an `#else`/`#error` fallback at line 28. Keep the `FREEINK_DEVICE_X4PRO` case and the fallback; delete the other eight. The file stops compiling without the fallback when no device macro is set, which is the intended behaviour.

Rename the macro itself to `BEREAN_BOARD_NAME`.

**Do not rename the magic string.** Lines 36 and 39 embed `"CROSSPOINT-BOARD-V1:"` as two independent literals:

```cpp
constexpr size_t MAGIC_LEN = sizeof("CROSSPOINT-BOARD-V1:") - 1;
const char TAG[] = "CROSSPOINT-BOARD-V1:" BEREAN_BOARD_NAME ";";
```

A rename that catches one and not the other compiles cleanly and makes `boardName()` return a garbage slice, which feeds the OTA asset name (`OtaUpdater.cpp:47`) and the flash-time guard (`FirmwareFlasher.cpp:212`). Leaving the magic verbatim keeps cross-fork image rejection working in both directions; the board *name* still distinguishes `x4pro` from `papermono`. Add a comment saying the magic is deliberately unchanged.

- [ ] **Step 7: Build and verify the version string**

```bash
pio run -e x4pro 2>&1 | grep -i "build version"
pio run -e x4pro-gh_release
```

Expected: a dev version string built from `0.0.0` plus branch and short SHA; the release build succeeds.

- [ ] **Step 8: Commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: rename to bereanOS and point OTA at its own repo

OTA_RELEASE_REPO still named victorstein/crosspoint-x4pro. A bereanOS build
at 0.0.0 would have found CrossPoint's v1.6.0, judged it newer, matched the
firmware-x4pro.bin asset and the x4pro board tag, and flashed CrossPoint
over itself on the first update check.

The board-tag magic string stays CROSSPOINT-BOARD-V1 deliberately: it appears
twice as independent literals, and renaming one but not the other returns a
garbage board slice into that same OTA asset-name path."
```

---

## Task 3: Hand the release state to stein-infra

**Files:**
- Delete: `.release-please-manifest.json`, `version.txt`, `release-please-config.json`, `.github/workflows/release-please.yml`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Remove the inherited release state**

```bash
git rm .release-please-manifest.json version.txt release-please-config.json \
       .github/workflows/release-please.yml
```

All four are tofu-managed (`AGENTS.md` says so explicitly), and `stein-infra/tofu/release-please.tf` recreates them with `overwrite_on_create = true` and then `ignore_changes = [content]`. Leaving the inherited copies means release-please reads `{".":"1.6.0"}` and cuts `v1.7.0` for a product that has shipped nothing.

`release-please.yml` also triggers on push to `main` and **auto-merges its own release PR** — leaving it in place while Phase 0 lands `refactor:` and `build:` commits would fire a release mid-strip.

- [ ] **Step 2: Empty the inherited changelog**

```bash
printf '# Changelog\n' > CHANGELOG.md
```

- [ ] **Step 3: Confirm the inherited tag is not pushed**

```bash
git tag
```

Expected: `v1.6.0`, inherited from the clone. **Do not push it.** When the repo is created, push only `main` and the working branch:

```bash
git push -u origin HEAD   # never `git push --tags`
```

If it has already been pushed, delete it remotely before the first tofu apply.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore: hand release state to stein-infra

Deletes the four tofu-managed files so the onboarding seeds them fresh at
0.0.0 rather than importing crosspoint's 1.6.0. The inherited v1.6.0 tag is
deliberately not pushed."
```

---

## Task 4: Prune the agent guide

`AGENTS.md` is 1,188 lines: a bereanOS prelude over CrossPoint's verbatim guide. The five agent definitions in Task 14 all declare it authoritative, so it must be true before they exist. It currently teaches `380KB RAM is the hard ceiling` (line 124), `NO PSRAM on C3` (line 174), a RISC-V alignment section on an Xtensa target, a four-orientation testing checklist the spec retired, and a four-button input model this device does not have.

**Files:**
- Modify: `AGENTS.md` (`CLAUDE.md` is a symlink to it)

- [ ] **Step 1: Delete the sections that are false for this device**

Remove: the ESP32-C3 hardware specs and the "380KB is the hard ceiling" framing; the multi-MCU list; the RISC-V alignment section; the "Logical Button Mapping" section; the four-orientation testing rows; the build-environment table listing `default`/`gh_release`/`slim`; and the OPDS, KOSync, dictionary and TXT/XTC references.

- [ ] **Step 2: Keep and fold in what is still true**

Retain, merged into the prelude's structure: the HAL rules and the SdFat-threading section, `DESTRUCTOR_CLOSES_FILE`, the memory-safety rules (`makeUniqueNoThrow`, nothrow `new`, `.reserve`, `constexpr`, string policy), the `tr()` requirement, the cache-versioning rules, the generated-file rules, and the git/CI conventions.

- [ ] **Step 3: Remove the separator and its promise**

The prelude says "Everything after the separator is inherited from CrossPoint and is being stripped in Phase 0." Once stripped, delete that framing so the document reads as one guide.

- [ ] **Step 4: Verify the symlink survived**

```bash
readlink CLAUDE.md
```

Expected: `AGENTS.md`.

- [ ] **Step 5: Commit**

```bash
git add AGENTS.md
git commit -m "docs: prune the agent guide to this device

Removes the ESP32-C3 ceiling, the RISC-V alignment section, the four-button
input model and the four-orientation checklist -- all false for the X4 Pro.
Keeps the HAL, SdFat-threading, memory-safety and cache-versioning rules."
```

---

## Task 5: Prune board environments and CI together

These must move in one commit: between them, `ci.yml` references envs that no longer exist and fails three of four matrix legs for reasons unrelated to the change under test.

**Files:**
- Modify: `platformio.ini`, `.github/workflows/ci.yml`, `.github/workflows/release-publish.yml`
- Delete: `.github/workflows/release-fonts.yml`, `.github/workflows/release_candidate.yml`

- [ ] **Step 1: Delete the ten non-X4-Pro environments**

Delete `[env:default]` (161), `[env:gh_release]` (172), `[env:gh_release_rc]` (182), `[env:slim]` (192), `[env:sticky]` (207), `[env:sticky-gh_release]` (217), `[env:sticky-gh_release_rc]` (228), `[env:papermono]` (294), `[env:papermono-gh_release]` (309) and `[env:papermono-gh_release_rc]` (322), with their comment banners.

Also delete `[env:x4pro-gh_release_rc]` — Step 3 deletes `release_candidate.yml`, the only thing that built it, and it depends on `${sysenv.CROSSPOINT_RC_HASH}`, which nothing else sets.

This frees **no flash** — PlatformIO compiles one environment at a time. The gain is repo surface and CI time.

- [ ] **Step 2: Fix the repo guard and the matrix**

`.github/workflows/release-publish.yml:112` reads:

```yaml
    if: github.repository == 'victorstein/crosspoint-x4pro'
```

Change it to `victorstein/berean-os`. Left alone, the publish job is skipped, the release is cut with no `firmware-x4pro.bin`, the workflow goes green, and OTA finds a release whose asset does not exist.

Reduce its build matrix to `x4pro-gh_release` only, producing one asset. **Keep the asset name `firmware-x4pro.bin`** — `OtaUpdater.cpp:44-48` derives it from the board tag.

Reduce `ci.yml`'s matrix to `x4pro`. Its trigger is already `branches: [main]` — verify, don't change.

- [ ] **Step 3: Delete the workflows that no longer apply**

```bash
git rm .github/workflows/release-fonts.yml .github/workflows/release_candidate.yml
```

- [ ] **Step 4: Verify**

```bash
for f in .github/workflows/*.yml; do python3 -c "import yaml,sys; yaml.safe_load(open('$f'))" && echo "ok $f"; done
grep -rn "sticky\|papermono\|gh_release_rc\|env:default" .github/workflows/ || echo "no stale env references"
pio run
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

`pio run` bare exercises `default_envs`; `pio check` is what CI runs with no `-e`. Both would have failed before Task 2 Step 2.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "ci: build one board, and fix the release repo guard

release-publish.yml was gated to victorstein/crosspoint-x4pro, so bereanOS
releases would have published no firmware asset while the workflow reported
success."
```

---

## Task 6: Delete the TXT and XTC readers

**Files:**
- Delete: `lib/Txt/`, `lib/Xtc/`, `src/activities/reader/TxtReaderActivity.{h,cpp}`, `src/activities/reader/XtcReaderActivity.{h,cpp}`, `src/activities/reader/XtcReaderChapterSelectionActivity.{h,cpp}`
- Modify: `src/activities/reader/ReaderActivity.cpp`, `lib/FsHelpers/FsHelpers.{h,cpp}`, `src/activities/home/FileBrowserActivity.cpp`, `src/components/UITheme.cpp`, `src/util/NextBookFinder.cpp`, `src/network/WebDAVHandler.cpp`, `src/util/BookCacheUtils.cpp`, `src/RecentBooksStore.cpp`, `src/activities/home/HomeActivity.cpp`, `src/activities/boot_sleep/SleepActivity.cpp`

- [ ] **Step 1: Simplify the reader factory**

In `src/activities/reader/ReaderActivity.cpp`, delete the includes at lines 15-16 and collapse the dispatch at lines 31-37 to:

```cpp
  activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
```

Keep the `EpubReaderActivity.h` include at line 11.

- [ ] **Step 2: Remove the extension helpers and their non-obvious callers**

`hasXtcExtension`, `hasTxtExtension` and `hasMarkdownExtension` are declared at `lib/FsHelpers/FsHelpers.h:55-65` and defined at `FsHelpers.cpp:168-174`. Four callers are outside the deletion set and easy to miss:

- `src/activities/home/FileBrowserActivity.cpp:70-71` — lists `.txt`, `.md` and `.xtc` as **openable**. Left in place, tapping one now reaches a reader factory with only an EPUB branch, and the device tries to parse a text file as an EPUB.
- `src/components/UITheme.cpp:136,139` — file-type icons.
- `src/util/NextBookFinder.cpp:18-19` — "next book" candidates.
- `src/network/WebDAVHandler.cpp:804` — `hasTxtExtension` → `text/plain` MIME.

Delete `hasXtcExtension` and `hasMarkdownExtension` and their uses in the first three. **Keep `hasTxtExtension`** for `WebDAVHandler` — serving a `.txt` upload with the right MIME type is unrelated to reading it — and delete its other call sites.

- [ ] **Step 3: Remove the remaining format branches**

In `src/util/BookCacheUtils.cpp`, `src/RecentBooksStore.cpp`, `src/activities/home/HomeActivity.cpp` and `src/activities/boot_sleep/SleepActivity.cpp`, delete the `#include <Txt.h>` / `#include <Xtc.h>` lines and the non-EPUB branches.

- [ ] **Step 4: Delete the files**

```bash
git rm -r lib/Txt lib/Xtc \
  src/activities/reader/TxtReaderActivity.h src/activities/reader/TxtReaderActivity.cpp \
  src/activities/reader/XtcReaderActivity.h src/activities/reader/XtcReaderActivity.cpp \
  src/activities/reader/XtcReaderChapterSelectionActivity.h \
  src/activities/reader/XtcReaderChapterSelectionActivity.cpp
```

- [ ] **Step 5: Run all five gates**

```bash
grep -rn "TxtReader\|XtcReader\|Txt\.h\|Xtc\.h\|hasXtcExtension\|hasMarkdownExtension" src lib --include=*.cpp --include=*.h
pio run -e x4pro && pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
cd test && cmake --build build && ctest --test-dir build --output-on-failure && cd ..
python3 scripts/settings_snapshot.py | diff /tmp/settings-baseline.txt - || true
./scripts/i18n_orphans.sh | diff /tmp/i18n-orphans-baseline.txt - || true
```

Expected: the first grep silent — note it now matches the *symbols*, not just filenames, which the first version of this plan did not. Settings diff empty. Orphan diff empty or explained.

- [ ] **Step 6: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop the TXT and XTC readers

bereanOS reads EPUB publications only. FileBrowserActivity was still
offering .txt/.md/.xtc files, which would have reached a reader factory with
a single EPUB branch. hasTxtExtension survives for WebDAV MIME typing."
```

---

## Task 7: Delete the dictionary

**Files:**
- Delete: `src/util/Dictionary.{h,cpp}`, `src/util/DictionaryRegistry.{h,cpp}`, `src/util/DictZip.{h,cpp}`, `src/util/DictHtmlPages.{h,cpp}`, `src/activities/reader/DictionaryDefinitionActivity.{h,cpp}`, `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}`, `docs/dictionary.md`, `scripts/generate_dictionary_synonyms_test_epub.py`, `test/epubs/test_dictionary_synonyms.epub`
- Modify: `src/activities/reader/EpubReaderMenuActivity.{h,cpp}`, `src/activities/reader/EpubReaderActivity.{h,cpp}`, `src/activities/reader/ReaderUtils.h`, `src/activities/reader/PassageSelectActivity.{h,cpp}`, `src/SettingsList.h`, `src/CrossPointSettings.{h,cpp}`, `src/activities/settings/SettingsActivity.cpp`, `src/util/StringUtils.h`, `lib/GfxRenderer/GfxRenderer.h`, `lib/I18n/translations/*.yaml`

- [ ] **Step 1: Confirm `PassageSelectActivity` is comment-coupled only**

This is the selection UI bereanOS keeps, so verify before proceeding:

```bash
grep -n -i "dict" src/activities/reader/PassageSelectActivity.h src/activities/reader/PassageSelectActivity.cpp
```

Expected: four hits, all on comment lines — `PassageSelectActivity.h:63,155` and `.cpp:47,79`. If any is an `#include` or a call, **stop and escalate**.

- [ ] **Step 2: Remove the menu row, not just its handler**

The menu *items* live in a different file from the handler:

- `src/activities/reader/EpubReaderMenuActivity.h:30` — `DICTIONARY,` in the `MenuAction` enum
- `src/activities/reader/EpubReaderMenuActivity.cpp:73` — `items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});`

Delete both, then the `case MenuAction::DICTIONARY:` handler in `EpubReaderActivity.cpp`. Removing only the handler leaves a "Look up" row that closes the menu and does nothing — and it compiles, because there is no `-Werror` in this project.

- [ ] **Step 3: Remove the remaining reader hooks**

Delete the dictionary includes and members from `src/activities/reader/EpubReaderActivity.{h,cpp}` and `ReaderUtils.h`, and the reference in `lib/GfxRenderer/GfxRenderer.h`.

- [ ] **Step 4: Remove the settings, leaving the enum hole**

`src/CrossPointSettings.h:149-163` defines:

```cpp
LP_MENU_KOSYNC = 0,  LP_MENU_DISABLED = 1,  LP_MENU_BOOKMARK = 2,
LP_MENU_DICTIONARY = 3,  LP_MENU_READER_MENU = 4,  LP_MENU_HIGHLIGHT = 5,
LONG_PRESS_MENU_FUNCTION_COUNT
```

**Delete `LP_MENU_DICTIONARY = 3` and leave the hole.** Do not renumber, and do not change `LONG_PRESS_MENU_FUNCTION_COUNT`. `src/CrossPointSettings.cpp:227-228` clamps the loaded value against that count, and `src/SettingsList.h:182-193` already documents handling a gap. Renumbering would silently reset every user whose `longPressMenuFunction` is 4 — `LP_MENU_READER_MENU`, the long-press route to the reader menu — to `LP_MENU_DISABLED`, on a device with no Back and no Confirm button.

Then delete the dictionary rows from `src/SettingsList.h:221-222`, the dictionary-folder save/load at `src/CrossPointSettings.cpp:98,219`, and the settings navigation in `src/activities/settings/SettingsActivity.cpp`.

Dropping a persisted field is safe to read — `fromJson` reads named keys with defaults and never sees unknown ones — but the next save drops the value permanently, so an OTA rollback to a pre-Phase-0 build loses it.

- [ ] **Step 5: Rewrite the four comments in `PassageSelectActivity`**

They now name a class that does not exist. State the constraint directly instead — at `.cpp:47`, that a passage snapshot spans multiple lines and is sized for `MAX_SELECTED_SNAPSHOT_LINES` rather than a single word.

- [ ] **Step 6: Delete the files**

```bash
git rm src/util/Dictionary.h src/util/Dictionary.cpp \
  src/util/DictionaryRegistry.h src/util/DictionaryRegistry.cpp \
  src/util/DictZip.h src/util/DictZip.cpp \
  src/util/DictHtmlPages.h src/util/DictHtmlPages.cpp \
  src/activities/reader/DictionaryDefinitionActivity.h \
  src/activities/reader/DictionaryDefinitionActivity.cpp \
  src/activities/reader/DictionaryWordSelectActivity.h \
  src/activities/reader/DictionaryWordSelectActivity.cpp \
  docs/dictionary.md scripts/generate_dictionary_synonyms_test_epub.py \
  test/epubs/test_dictionary_synonyms.epub
```

- [ ] **Step 7: Remove the translation keys**

`STR_DICT*` plus `STR_LOOKUP`, whose only two call sites are both being deleted:

```bash
for f in lib/I18n/translations/*.yaml; do
  sed -i '' '/^STR_DICT/d; /^STR_LOOKUP:/d' "$f"
done
git diff --stat lib/I18n/translations/
grep -rn "STR_DICT\|STR_LOOKUP" lib/I18n/translations/
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

Inspect the `--stat` before committing — this edits 32 files at once. Expected: the grep is silent.

- [ ] **Step 8: Run all five gates**

```bash
grep -rn "Dictionary\|DICTIONARY\|DictZip\|DictHtml\|STR_LOOKUP" src lib --include=*.cpp --include=*.h
pio run -e x4pro && pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
cd test && cmake --build build && ctest --test-dir build --output-on-failure && cd ..
python3 scripts/settings_snapshot.py | diff /tmp/settings-baseline.txt - || true
./scripts/i18n_orphans.sh | diff /tmp/i18n-orphans-baseline.txt - || true
```

The grep now matches `DICTIONARY` and `STR_LOOKUP` in addition to the type names — the first version of this plan matched neither, so the dead menu row survived all its gates.

- [ ] **Step 9: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop the dictionary

Removes the reader menu row as well as its handler -- deleting only the
handler leaves a Look up row that does nothing and still compiles.
LP_MENU_DICTIONARY leaves a numbering hole: it is a persisted value, and
renumbering would reset anyone whose long-press opens the reader menu."
```

---

## Task 8: Delete KOReader sync

**Files:**
- Delete: `lib/KOReaderSync/`, `src/activities/reader/KOReaderSyncActivity.{h,cpp}`, `src/activities/settings/KOReaderAuthActivity.{h,cpp}`, `src/activities/settings/KOReaderSettingsActivity.{h,cpp}`
- Modify: `src/main.cpp`, `src/activities/reader/EpubReaderMenuActivity.{h,cpp}`, `src/activities/reader/EpubReaderActivity.{h,cpp}`, `src/SettingsList.h`, `src/CrossPointSettings.{h,cpp}`, `src/activities/settings/SettingsActivity.{h,cpp}`, `lib/I18n/translations/*.yaml`

- [ ] **Step 1: Remove the menu row and its handler**

`src/activities/reader/EpubReaderMenuActivity.cpp:80` pushes the Sync row and the `MenuAction` enum in the `.h` carries `SYNC`. Delete both, then the `case MenuAction::SYNC:` handler in `EpubReaderActivity.cpp`.

- [ ] **Step 2: Remove the reader integration and boot wiring**

Delete the sync-client member, include and progress-push calls from `src/activities/reader/EpubReaderActivity.{h,cpp}`, and the credential-store initialisation from `src/main.cpp`.

- [ ] **Step 3: Remove the settings, leaving the enum hole**

**Delete `LP_MENU_KOSYNC = 0` from `src/CrossPointSettings.h:149` and leave the hole.** Same rule as Task 7 — do not renumber, do not change the count. Note this file is `CrossPointSettings.h`, not `.cpp`.

Then delete the KOReader rows from `src/SettingsList.h`, the persisted credential fields from `src/CrossPointSettings.cpp`, and the navigation from `src/activities/settings/SettingsActivity.{h,cpp}` including its `SettingAction` enumerator.

- [ ] **Step 4: Delete the files**

```bash
git rm -r lib/KOReaderSync \
  src/activities/reader/KOReaderSyncActivity.h src/activities/reader/KOReaderSyncActivity.cpp \
  src/activities/settings/KOReaderAuthActivity.h src/activities/settings/KOReaderAuthActivity.cpp \
  src/activities/settings/KOReaderSettingsActivity.h src/activities/settings/KOReaderSettingsActivity.cpp
```

- [ ] **Step 5: Remove the translation keys from all 32 languages**

The prefixed keys are at `english.yaml:126,144,145,152,187,360`. Several unprefixed ones belong to the same feature:

```bash
for f in lib/I18n/translations/*.yaml; do
  sed -i '' '/^STR_KOREADER_/d; /^STR_KOSYNC:/d; /^STR_SYNC_BEHAVIOR:/d; /^STR_SMART_SYNC:/d; /^STR_SYNC_SERVER_URL:/d; /^STR_DOCUMENT_MATCHING:/d; /^STR_CALIBRE_URL_HINT:/d' "$f"
done
git diff --stat lib/I18n/translations/
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
./scripts/i18n_orphans.sh
```

The orphan checker is the authority on what else belongs to this feature — remove whatever it lists that is KOReader-only, and re-run until clean.

- [ ] **Step 6: Run all five gates**

```bash
grep -rn "KOReader\|KOSync\|KOSYNC" src lib --include=*.cpp --include=*.h
pio run -e x4pro && pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
cd test && cmake --build build && ctest --test-dir build --output-on-failure && cd ..
python3 scripts/settings_snapshot.py | diff /tmp/settings-baseline.txt - || true
./scripts/i18n_orphans.sh | diff /tmp/i18n-orphans-baseline.txt - || true
```

A `STR_*` still referenced in C++ but deleted from the YAML fails the build at the generated enum — that safety net is real and works in this direction only.

- [ ] **Step 7: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop KOReader sync

bereanOS is standalone and is the source of truth for its own study data.
LP_MENU_KOSYNC leaves a numbering hole for the same reason as the dictionary.

The credential store's file under /.crosspoint/ is left on the card; nothing
reads it, and Phase 1 owns SD-state migration."
```

---

## Task 9: Delete OPDS

The largest deletion: it reaches the web server, the home screen's index arithmetic, and a setting the meeting downloader depends on.

**Files:**
- Delete: `lib/OpdsParser/`, `src/OpdsServerStore.{h,cpp}`, `src/util/OpdsFilename.{h,cpp}`, `src/activities/browser/`, `src/activities/settings/OpdsSettingsActivity.{h,cpp}`, `src/activities/settings/OpdsServerListActivity.{h,cpp}`, `test/opds_filename/`
- Modify: `src/main.cpp`, `src/CrossPointSettings.h`, `src/SettingsList.h`, `src/activities/ActivityManager.{h,cpp}`, `src/activities/home/HomeActivity.{h,cpp}`, `src/activities/settings/SettingsActivity.{h,cpp}`, `src/activities/network/MeetingDownloadActivity.cpp`, `src/activities/util/KeyboardEntryActivity.cpp`, `src/network/CrossPointWebServer.{h,cpp}`, `src/network/html/SettingsPage.html`, `test/CMakeLists.txt`, `lib/I18n/translations/*.yaml`

- [ ] **Step 1: Give the meeting downloader its own setting first, in its own commit**

`src/activities/network/MeetingDownloadActivity.cpp:56` reads:

```cpp
  const char* folder = SETTINGS.opdsDownloadFolder;
```

That field is `src/CrossPointSettings.h:266`, `char opdsDownloadFolder[64] = "";` — and the meeting downloader is the feature bereanOS exists for. Rename the field to `downloadFolder`, update the reader, and keep the persisted JSON key reading the old name as a fallback so existing settings survive:

```cpp
  char downloadFolder[64] = "";
```

In the settings `fromJson`, read `downloadFolder` and fall back to `opdsDownloadFolder` when absent; write only the new key. Also update its row in `src/SettingsList.h:422-430`.

```bash
pio run -e x4pro
git add -A && git commit -m "refactor: rename opdsDownloadFolder to downloadFolder

The meeting downloader reads it, and OPDS is about to be deleted. Reads the
old JSON key as a fallback so existing settings survive."
```

- [ ] **Step 2: Rename the shared web CSS, in its own commit**

`src/network/html/SettingsPage.html:612,622` builds the **WiFi** network rows with `class="opds-server"` and `class="opds-actions"`, and the CSS at `:210,216,219` is shared. Deleting the OPDS block and its CSS together silently unstyles the WiFi manager, and nothing validates the HTML.

Rename `.opds-server` → `.net-server` and `.opds-actions` → `.net-actions` throughout the file, touching nothing else.

```bash
pio run -e x4pro
git add -A && git commit -m "refactor: rename the shared .opds- web CSS to .net-

The WiFi network manager reuses these classes; deleting them with the OPDS
section would have unstyled it silently."
```

- [ ] **Step 3: Remove the home-screen entry and its index arithmetic**

`src/activities/home/HomeActivity.h:35-58` has two hand-written mirrored functions, `menuItemToIndex(item, hasOpdsUrl)` and `indexToMenuItem(idx, hasOpdsUrl)`, plus `hasOpdsServers` at `HomeActivity.cpp:116,122` and a `switch` at `:188`. Edit **both** functions together — changing one leaves the home menu selecting the wrong row, which compiles and greps clean.

Delete `HomeMenuItem::OPDS_BROWSER` (`src/activities/ActivityManager.h:20`) and `goToBrowser()` (`:88`) with their implementations.

- [ ] **Step 4: Remove the web server endpoints and the settings**

Delete the OPDS handlers and route registrations from `src/network/CrossPointWebServer.{h,cpp}`, and the OPDS section from `SettingsPage.html` (now safe, after Step 2). The HTML is the source; `src/network/html/*.generated.h` is rebuilt and must not be committed.

Delete the remaining OPDS fields from `src/CrossPointSettings.h`, the `opdsFilenameFormat` row from `src/SettingsList.h`, `SettingAction::OPDSBrowser` from `src/activities/settings/SettingsActivity.h:19` with its case in the `.cpp`, the `/opds` snippet at `src/activities/util/KeyboardEntryActivity.cpp:89`, and the store initialisation in `src/main.cpp`.

- [ ] **Step 5: Delete the files and the dead test**

```bash
git rm -r lib/OpdsParser src/activities/browser test/opds_filename \
  src/OpdsServerStore.h src/OpdsServerStore.cpp \
  src/util/OpdsFilename.h src/util/OpdsFilename.cpp \
  src/activities/settings/OpdsSettingsActivity.h src/activities/settings/OpdsSettingsActivity.cpp \
  src/activities/settings/OpdsServerListActivity.h src/activities/settings/OpdsServerListActivity.cpp
```

Remove `add_subdirectory(opds_filename)` from `test/CMakeLists.txt:56`.

- [ ] **Step 6: Remove the translation keys**

Six OPDS keys sit in `english.yaml` at lines 318, 380, 396, 397, 398 and 402, plus feed and server strings that belong to the same feature:

```bash
for f in lib/I18n/translations/*.yaml; do
  sed -i '' '/^STR_OPDS/d; /^STR_FETCH_FEED_FAILED:/d; /^STR_PARSE_FEED_FAILED:/d; /^STR_NO_SERVERS:/d; /^STR_ADD_SERVER:/d; /^STR_DELETE_SERVER:/d' "$f"
done
git diff --stat lib/I18n/translations/
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
./scripts/i18n_orphans.sh
```

Remove whatever else the orphan checker lists as OPDS-only, and re-run until clean.

- [ ] **Step 7: Run all five gates**

```bash
grep -rn "Opds\|OPDS" src lib test --include=*.cpp --include=*.h --include=*.txt
pio run -e x4pro && pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
cd test && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure && cd ..
python3 scripts/settings_snapshot.py | diff /tmp/settings-baseline.txt -
./scripts/i18n_orphans.sh | diff /tmp/i18n-orphans-baseline.txt - || true
```

`cmake -B build` is re-run because `CMakeLists.txt` changed. Remaining `OPDS` hits in comments (`UIThemeTokens.h:32`, `HttpDownloader.cpp:23,28,149`, `scripts/debugging_monitor.py:160`) are cosmetic — clean them, then re-run.

The settings diff must now show exactly the rows removed across Tasks 7-9 and nothing else.

- [ ] **Step 8: Format and commit**

```bash
./bin/clang-format-fix
git add -A
git commit -m "refactor: drop the OPDS browser

Publications come from jw.org. Removes the browse activity, the server store,
the web-server endpoints and the settings surface, and edits both halves of
HomeActivity's mirrored menu-index arithmetic together."
```

---

## Task 10: Land the storage-discipline helper

The spec mandates atomic writes and a checked byte budget for **every** store, and Phase 1 introduces four. `lib/Serialization/PersistableStore.h:112-117` shows `saveToFile()` calling the **non-atomic** `writeDocToFile`, with `writeDocToFileAtomic` opt-in per call site — so the unsafe path is the default and the safe one is the one you must remember. This lands now, while nothing depends on it, rather than being reinvented by four parallel agents.

**Files:**
- Modify: `lib/Serialization/PersistableStore.{h,cpp}`
- Create: `test/persistable_store/PersistableStoreTest.cpp`, `test/persistable_store/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `test/persistable_store/PersistableStoreTest.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <string>

#include "SaveBudget.h"

int main() {
  // Under budget: accepted.
  assert(persist::fitsBudget(std::string(1000, 'x').size(), 45000));

  // At the budget boundary: accepted.
  assert(persist::fitsBudget(45000, 45000));

  // Over budget: refused rather than truncated. SDCardManager::readFile caps
  // reads at 50,000 bytes and returns the truncated result with no error, so a
  // document that saves above the budget reads back unparseable and the next
  // save overwrites the real file with an empty document.
  assert(!persist::fitsBudget(45001, 45000));

  printf("persistable_store: ok\n");
  return 0;
}
```

Create `test/persistable_store/CMakeLists.txt`:

```cmake
add_executable(persistable_store_test PersistableStoreTest.cpp)
target_include_directories(persistable_store_test PRIVATE ${CMAKE_SOURCE_DIR}/../lib/Serialization)
add_test(NAME persistable_store COMMAND persistable_store_test)
```

Add `add_subdirectory(persistable_store)` to `test/CMakeLists.txt`.

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -B build && cmake --build build 2>&1 | tail -5
```

Expected: FAIL — `SaveBudget.h` does not exist.

- [ ] **Step 3: Write the minimal implementation**

Create `lib/Serialization/SaveBudget.h`:

```cpp
#pragma once

#include <cstddef>

// Byte budgets for anything persisted through PersistableStore.
//
// SDCardManager::readFile caps reads at 50,000 bytes and returns the truncated
// string with no error, so a document that saves larger than this reads back
// mid-token, fails to parse, and initialises empty -- and the next save then
// overwrites the real file. Measure before writing and refuse instead.
namespace persist {

inline constexpr size_t DEFAULT_SAVE_BUDGET = 45000;

constexpr bool fitsBudget(size_t serialisedBytes, size_t budget) { return serialisedBytes <= budget; }

}  // namespace persist
```

- [ ] **Step 4: Run it and watch it pass**

```bash
cd test && cmake --build build && ctest --test-dir build -R persistable_store --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Make the safe path the default**

In `lib/Serialization/PersistableStore.h`, add a `saveToFileAtomic()` that measures the serialised document against `persist::DEFAULT_SAVE_BUDGET` (overridable per store via a `static constexpr size_t SAVE_BUDGET` member), `LOG_ERR`s and returns false when it does not fit, and otherwise calls `writeDocToFileAtomic`.

Leave the existing `saveToFile()` in place — changing every current call site is out of scope for Phase 0 — but add a comment on it pointing at the atomic variant and saying new stores must use it.

- [ ] **Step 6: Build and commit**

```bash
pio run -e x4pro
cd test && cmake --build build && ctest --test-dir build --output-on-failure && cd ..
./bin/clang-format-fix
git add -A
git commit -m "feat: add an atomic, budgeted save path for persisted stores

Phase 1 introduces four stores and the spec requires atomic writes and a
checked byte budget for each. PersistableStore's default saveToFile is the
non-atomic variant, so the safe path was the one you had to remember."
```

---

## Task 11: Rewrite the project documents

**Files:**
- Modify: `README.md`, `SCOPE.md`, `ROADMAP.md`, `USER_GUIDE.md`, `docs/webserver.md`, `docs/webserver-endpoints.md`, `docs/contributing/architecture.md`, `docs/contributing/touch-and-ui.md`, `.github/ISSUE_TEMPLATE/config.yml`, `.github/ISSUE_TEMPLATE/feature_request.yml`, `.github/ISSUE_TEMPLATE/bug_report.yml`, `.github/PULL_REQUEST_TEMPLATE.md`, `.skills/scope-discipline/SKILL.md`, `.gitignore`
- Delete: `GOVERNANCE.md`

- [ ] **Step 1: Rewrite the four top-level documents**

Replace `README.md`, `SCOPE.md`, `ROADMAP.md` and `USER_GUIDE.md` with bereanOS's, drawing scope from `docs/superpowers/specs/2026-09-13-berean-os-design.md`. Delete `GOVERNANCE.md` — it describes CrossPoint's contribution model. `LICENSE` is separate and stays untouched.

- [ ] **Step 2: Fix every dangling link**

```bash
grep -rn "GOVERNANCE.md\|docs/dictionary.md" README.md USER_GUIDE.md docs/ .github/ || echo "no dangling links"
```

Known at time of writing: `docs/contributing/architecture.md:218` links `GOVERNANCE.md`; `README.md:15` and `USER_GUIDE.md:266,287,621` link `docs/dictionary.md`, deleted in Task 7.

- [ ] **Step 3: Remove deleted features from the remaining docs**

`docs/webserver.md` and `docs/webserver-endpoints.md` document OPDS endpoints; `docs/contributing/architecture.md` and `docs/contributing/touch-and-ui.md` reference deleted features.

- [ ] **Step 4: Update the GitHub templates and the repo-local skill**

`.github/ISSUE_TEMPLATE/config.yml` points at `crosspoint-reader/crosspoint-reader`; `feature_request.yml:29` names OPDS; `PULL_REQUEST_TEMPLATE.md` opens "CrossPoint is intentionally narrow…". `.skills/scope-discipline/SKILL.md` is built entirely on `SCOPE.md`, rewritten in Step 1 — update it to bereanOS's scope.

- [ ] **Step 5: Unblock the agent definitions**

`.gitignore:26` is `.claude/*`, which will silently reject Task 12's commit. Add a negation immediately after it:

```gitignore
.claude/*
!.claude/agents/
```

- [ ] **Step 6: Verify and commit**

```bash
grep -rni "crosspoint" README.md SCOPE.md ROADMAP.md USER_GUIDE.md .github/ | grep -v "CROSSPOINT-BOARD-V1\|\.crosspoint/"
git check-ignore -v .claude/agents/ || echo "agents directory is committable"
```

The two allowed survivors are the board-tag magic (deliberately unchanged, Task 2) and the `.crosspoint/` SD cache path (renaming it would orphan every cached book and reading position; Phase 1 owns that migration).

```bash
git add -A
git commit -m "docs: rewrite the project documents for bereanOS

Also unblocks .claude/agents/ in .gitignore, which would have silently
rejected the agent definitions."
```

---

## Task 12: Device smoke test

Run this **before** the agent definitions, not after: Tasks 6-9 are where behaviour can regress, and flashing here halves the bisect range if something is wrong.

- [ ] **Step 1: Build and flash a dev build**

```bash
pio run -e x4pro -t upload
```

- [ ] **Step 2: Enter and exit every remaining activity**

Home, reader, chapter select, verse grid, footnotes, bookmarks, highlights, tag picker, passage select, meeting download, settings and each settings sub-screen, WiFi, file browser, file transfer, and the firmware-update screen.

Watch for: a screen that no longer opens; a settings row navigating nowhere; a reader-menu row that does nothing; an empty list where a deleted feature used to be; untranslated `STR_*` keys rendering literally; and the WiFi manager in the web UI being unstyled (Task 9 Step 2's CSS rename).

- [ ] **Step 3: Confirm the update check is safe**

Settings → check for updates. Expected: **no update available** — not a CrossPoint version. This is the single most important check in the phase.

- [ ] **Step 4: Record the result**

Append the outcome to `docs/superpowers/notes/phase-0-baseline.md`, then commit.

---

## Task 13: Write the scoped agent definitions

**Five** surfaces, not four. A realistic Phase 1 task — a `data-pid` scanner, the store that consumes it, and a screen that shows tags — touches three of the original four surfaces plus five files owned by none of them, which is how two agents end up editing one file.

**Files:**
- Create: `.claude/agents/epub-dev.md`, `ui-dev.md`, `net-dev.md`, `hal-dev.md`, `data-dev.md`

- [ ] **Step 1: Write the shared preamble into each definition**

Every definition opens with the same three blocks, varying only in ownership:

```markdown
## Read first
`AGENTS.md` is authoritative for hardware constraints, memory rules and storage
discipline. `docs/contributing/development-workflow.md` is authoritative for
process. **If they conflict with this file, they win.**

## Prime directive — follow the existing pattern, escalate before inventing
1. Find the nearest existing example in this surface and mirror it — structure,
   naming, error handling, and its host test. Name the file you modelled on in
   your first message.
2. If this surface has no way to do what the task needs, **stop and ask.** A new
   dependency, a new on-disk format, or a new cross-cutting mechanism is a user
   decision, not an autonomous one.

## Shared files — report, do not edit
`test/CMakeLists.txt`, `lib/I18n/translations/*.yaml` and `src/main.cpp` are
append points for every surface. Two agents editing them in parallel is the
collision the workflow forbids. When your change needs a line in one of them,
put the exact line in your PR description and let the orchestrator apply it.
```

- [ ] **Step 2: Write `.claude/agents/epub-dev.md`**

Owns `lib/Epub`. Body after the shared preamble:

```markdown
## Where things live (`lib/Epub`)
- `Epub.{h,cpp}` — archive façade: spine, manifest, href resolution. Note the
  cache key is `std::hash<std::string>{}(filepath)` (`Epub.h:48`), so moving a
  file orphans its cache.
- `Epub/parsers/` — expat streaming parsers. `ContentOpfParser.cpp` reads the
  spine and never reads `linear`, so non-linear items are included.
  `ChapterHtmlSlimParser.cpp:108` classifies `span` as non-navigable inline.
- `Epub/VerseAnchors.{h,cpp}` — offset→(chapter, verse) for Bible EPUBs. **This
  does not generalise**: it hardcodes the `id` attribute and a
  `chapter%u_verse%u` grammar (lines 30-41), `break`s after the first `id`, and
  `reserve(176)` is sized for Psalm 119. The `data-pid` scanner Phase 1 needs is
  a second scanner sharing only the `VisibleOffsetCounter` + expat skeleton.
- `Epub/Section.{h,cpp}`, `Epub/BookMetadataCache.{h,cpp}` — on-disk caches.
  Increment the format version **before** changing a binary structure.
  `BookMetadataCache.cpp:467` validates on version only — never size or mtime —
  so replacing a file in place serves a stale cache.

## The pattern to mirror for a new scanner
`VerseAnchors::Scanner` — chunk-fed, expat behind an opaque `void*`, no
`HalStorage`, no Arduino, host-tested.

## Constraints that bite here
- `std::string_view` is not null-terminated; convert explicitly at any C API.
- `.reserve(N)` before every `push_back` loop.
- Never bare `new` — `makeUniqueNoThrow` from `lib/Memory/Memory.h`.
- Never call SdFat directly; everything goes through `HalStorage`.
```

- [ ] **Step 3: Write `.claude/agents/data-dev.md`**

The surface the original four lacked, and the one Phase 1 is mostly about:

```markdown
## Where things live
- `src/*Store.{h,cpp}` — the repo's convention for persisted stores
  (`RecentBooksStore`, `WifiCredentialStore`). Phase 1's tag and passage stores
  land here.
- `lib/Serialization/PersistableStore.{h,cpp}` — the base class, and
  `SaveBudget.h` — the atomic, budgeted save path.
- `src/CrossPointSettings.{h,cpp}`, `src/SettingsList.h` — settings and their UI
  rows.
- On-disk formats under `/.berean/`.

## Constraints that bite here — these are the reason this surface exists
- **Atomic writes only.** `saveToFile()` calls the non-atomic `writeDocToFile`
  (`PersistableStore.h:112-117`). Use `saveToFileAtomic()`.
- **Check the byte budget before writing.** `SDCardManager::readFile` caps at
  50,000 bytes and silently truncates; a store that saves over budget reads back
  unparseable and the next save overwrites it with an empty document.
- **Persisted enums keep their numeric values.** Deleting an enumerator that a
  settings file stores by number reassigns every user who had it. Leave holes.
- **Stream anything over ~40 KB** rather than using `Storage.readFile`.
- **Name the owning task and hold `storageMutex` on write.** The web server
  (`CrossPointWebServer.cpp:164`, `POST /delete`) is a second writer to the card.
- **Never lock `storageMutex` on a read path the renderer sits behind.**
```

- [ ] **Step 4: Write `ui-dev.md`, `net-dev.md` and `hal-dev.md`**

`ui-dev` owns `src/activities` and `lib/GfxRenderer`, and carries an explicit prohibition: **do not touch `MappedInputManager`** — it is in the `Activity` base constructor and implements this device's Back gesture; Phase 2 replaces it as one planned change. It also documents that activities are heap-allocated and deleted on exit, that all user-facing text uses `tr(STR_*)`, that the panel is 1-bit with no windowed update, and that `'\n'` measures as a line break but does not draw as one.

`net-dev` owns `src/network`, mirrors `PubMediaJson` and `WolWeekScan` as the pure-parser pattern, and documents: suppress auto-sleep during transfers (it triggers on *input* inactivity, which a download does not reset); download to `<name>.part`; TLS here is `setInsecure()`, not CA-verified.

`hal-dev` owns `lib/hal` and the `freeink-sdk` boundary, and documents that the submodule is not to be edited to fix something belonging in the HAL, that all SD access goes through `HalStorage`'s mutex, `DESTRUCTOR_CLOSES_FILE` semantics, and the `IRAM_ATTR`/`DRAM_ATTR` and ISR-primitive rules.

- [ ] **Step 5: Verify they are committable and commit**

```bash
git check-ignore -v .claude/agents/epub-dev.md || echo "committable"
git add .claude/agents/
git commit -m "docs: add five scoped agent definitions

Five surfaces, not four: a Phase 1 task touches epub, data and ui, and the
original split left the store layer, PersistableStore and SettingsList owned
by nobody. Each definition declares test/CMakeLists.txt, the translation
YAMLs and main.cpp as report-only, because they are append points for every
surface and the workflow forbids two agents on one file."
```

---

## Task 14: Measure and close

- [ ] **Step 1: Measure the dev build against the baseline**

```bash
pio run -e x4pro
ls -l .pio/build/x4pro/firmware.bin
```

Expected: 200-230 KB smaller than Task 1's figure. The spec's 229 KB estimate includes `MappedInput` + `ButtonRemap` (5,827 B) that this phase's hard guard forbids deleting, so ~223 KB is the honest ceiling. A materially smaller saving means a deletion was incomplete — re-run the Task 6-9 greps.

- [ ] **Step 2: Confirm the release target and the format check**

```bash
pio run -e x4pro-gh_release
./bin/clang-format-fix
git diff --exit-code
```

Expected: no diff. `-g` is not sufficient — it skips committed files, which is exactly how a clean local tree fails CI.

- [ ] **Step 3: Record and commit**

Append the final measurements to `docs/superpowers/notes/phase-0-baseline.md` and commit.

---

## Self-review notes

**Ordering changed from the first version**, on both reviewers' advice. Identity and OTA safety moved from seventh to second: they touch `platformio.ini`, `*.json` and `.github/`, which the deletions never touch, so the stated reason for deferring them (merge friction) does not survive contact with the file lists — and a device carrying CrossPoint's OTA repo is unsafe to flash at any point. `AGENTS.md` pruning moved ahead of everything that defers to it. CI moved into the same commit as the env pruning. The device smoke test moved ahead of the agent definitions.

**Two tasks were split into their own commits** because they are refactors, not deletions: the `opdsDownloadFolder` rename (the meeting downloader depends on it) and the `.opds-` CSS rename (the WiFi manager shares it).

**Deferred to Phase 1**, though a reader might expect them here: the `.crosspoint/` SD directory name, `ReturnStack`'s capacity, the KOReader credential file left on the card, and converting existing `saveToFile()` call sites to the atomic path.

**One task still stops for a human.** Task 12 is device work. The release-ownership decision that previously stopped Task 8 is now resolved in the prerequisite section, and the surface boundaries are settled at five.
