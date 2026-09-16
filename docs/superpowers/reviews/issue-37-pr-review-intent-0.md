# PR #45 — stage-1 INTENT review (issue #37)

Branch `fix/37-gate-screen-rotation` @ `db1a3652`, base `main` @ `ded48d17`.
Reviewed against issue #37's **Brief** and the PR's own
`docs/superpowers/specs/2026-09-16-issue-37-design.md`, with
`docs/superpowers/plans/2026-09-16-issue-37-plan.md` as the divergence baseline.
Prior gates: spec review 0 CLEAR, plan review 0 CLEAR.

Stage 1 only — does it do what was asked, completely, and nothing more. Code
quality is out of scope and no quality finding is recorded below.

**No findings.** 0 BLOCKER, 0 MAJOR, 0 MINOR.

---

## What the code change is

27 insertions, 0 deletions, three files — matching the plan's step-5 expected
diffstat exactly:

| File | Change |
| --- | --- |
| `src/CrossPointSettings.h:8-24` | `BEREAN_CAP_ROTATION` derived from the device set, `#error` on an unhandled one |
| `src/SettingsList.h:317,322` | `STR_ORIENTATION` row wrapped in `#if BEREAN_CAP_ROTATION` |
| `src/SettingsList.h:349,355-360` | `STR_LONG_PRESS_BEHAVIOR` as two variants, the `#else` dropping `STR_LONG_PRESS_BEHAVIOR_ORIENTATION` |
| `src/activities/reader/EpubReaderMenuActivity.cpp:73-75` | `ROTATE_SCREEN` row wrapped in `#if BEREAN_CAP_ROTATION` |

Byte-for-byte what plan steps 1–4 prescribe. No file appears that the plan told
the implementer not to open: `src/activities/reader/EpubReaderActivity.cpp` and
`src/CrossPointSettings.cpp` are both absent from the diff, as the plan's step-5
overlap guard requires.

## Acceptance criteria — each verified, not taken on trust

Verification method: preprocess the real translation units with the real build
flags from a freshly generated `compile_commands.json` (509 entries, asserted to
contain project sources), then count the gated construct in the output. This
reports what the compiler actually sees, not what the source text says.
`compile_commands.json` was deleted afterwards.

**Criterion 1 — `STR_ORIENTATION` gone from the Settings Reader category and the
reader menu.** Both confirmed at 0 occurrences:

```
src/activities/settings/SettingsActivity.cpp  'STR_ORIENTATION, &CrossPointSettings::orientation'  -> 0
src/activities/settings/SettingsActivity.cpp  '"orientation", StrId::STR_CAT_READER'               -> 0
src/activities/reader/EpubReaderMenuActivity.cpp 'MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION' -> 0
```

The row removal is index-safe. `buildMenuRowItems`
(`EpubReaderMenuActivity.cpp:35-41`) and the value refresh loop (`:200-211`)
both dispatch on `menuItems[i].action`, never on a fixed index, and
`MAX_MENU_ITEMS` (`EpubReaderMenuActivity.h:57`) is an upper bound only. One
fewer row cannot shift anything.

**Criterion 2 — `ORIENTATION_CHANGE` gone from `STR_LONG_PRESS_BEHAVIOR`, and
its handler unreachable as a result.** Confirmed:

```
'StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION}'  -> 0
'STR_LONG_PRESS_BEHAVIOR_SKIP},'               -> 1   (the two-value variant is the one compiled)
```

Both variants keep the same persisted key `"longPressButtonBehavior"`, so no
schema rename. The handler at `EpubReaderActivity.cpp:625-632` is correctly left
untouched, and it is genuinely unreachable — every route to
`longPressButtonBehavior == 2` is closed:

- load: `fromJson`'s `clamp(v, info.enumValues.size(), fieldDefault)`
  (`src/CrossPointSettings.cpp:161`, lambda at `:111`) with `size() == 2` maps a
  persisted `2` to the field default `OFF` (`CrossPointSettings.h:295`).
- device UI: `enumValues.size() > 2` is false, so the popup path is skipped and
  the write is `(currentValue + 1) % 2` (`SettingsActivity.cpp:260,273`).
- web API: `if (val >= 0 && val < maxVal)` with `maxVal == 2`
  (`CrossPointWebServer.cpp:1281-1283`).

`ORIENTATION_CHANGE = 2` is the trailing enumerator
(`CrossPointSettings.h:195-198`), so dropping it needs no index remapping —
spec **A8** holds.

**Criterion 3 — the gate is a capability, not `BoardConfig::isX4Pro()`.** Met in
the form spec **A1** argues for, and **A1**'s premise checks out: `BoardConfig.h`
is in the `freeink-sdk` submodule and its own capabilities use exactly this shape
(`FREEINK_CAP_TOUCH`, `BoardConfig.h:174-179`; `FREEINK_CAP_WARMLIGHT`, `:191`).
No `isX4Pro()` call was added. The deviation is recorded below.

**Criterion 4 — `pio run` succeeds.** CI on `db1a3652`: `Build x4pro` pass,
`clang-format` pass, `cppcheck` pass, `unit-tests` pass, `Test Status` pass — all
seven green. Not rebuilt locally; a green CI build of this exact SHA is the same
evidence.

**Constraint — `SETTINGS.orientation` and `applyOrientation` intact.** The field
(`CrossPointSettings.h:258`), the `ORIENTATION` enum (`:84-88`),
`ReaderUtils::applyOrientation` (`ReaderUtils.h:27-41`) and
`GfxRenderer::setOrientation` are all untouched. Every reader still compiles and
still reads a legal value: `main.cpp:608`, `ReaderActivity.cpp:37`,
`EpubReaderActivity.cpp:282,286`, `SleepActivity.cpp:513,525`. The sleep screen —
the stated reason for the constraint — is unaffected.

**Constraint — no `MappedInputManager::mapButton` / `NavKeyGestures` edit.**
Neither file is in the diff.

**Constraint — list filtering only.** Nothing in `lib/GfxRenderer` or
`freeink-sdk` changed; other boards keep rotation, because `BEREAN_CAP_ROTATION`
is `#ifndef`-guarded and only the X4 Pro derivation sets it to 0.

**Constraint — keep off `QrDisplayActivity` and the QR menu row (task #38).** The
diff touches `EpubReaderMenuActivity.cpp` at one hunk only, around the
`ROTATE_SCREEN` `push_back`. No QR symbol appears in the diff.

## Completeness — is any rotation surface left open

Swept independently of the issue's list. Every remaining user-facing reference to
rotation is either gated or provably dead:

```
src/SettingsList.h:319-320                      inside the #if       (gated)
src/activities/reader/EpubReaderMenuActivity.cpp:74   inside the #if  (gated)
src/activities/reader/EpubReaderMenuActivity.cpp:107  popup           (dead: no row ever carries ROTATE_SCREEN)
src/activities/reader/EpubReaderMenuActivity.h:81-82  orientationLabels (dead, feeds :107 and :203)
```

No hardcoded orientation control exists in `data/html/`, so the web settings page
is driven entirely by `getSettingsList()` and closes with it — the dormant fourth
surface the spec names in §5.1. The only writer of `SETTINGS.orientation` is
`EpubReaderActivity::applyOrientation` (`:896`), reachable only from
`:287` (menu result) and `:629` (long-press). Both are now closed, so the value
can never leave `PORTRAIT` on this board. No fifth entry point exists.

The dead popup and `orientationLabels` are spec **A6**'s deliberate choice to
keep the diff to one line in a file #38 is editing concurrently. That is a
documented conflict-surface trade, not a silent scope reduction.

## Scope — nothing added beyond what was asked

The non-`src` half of the diff (research note, spec, plan, two prior reviews,
1,939 lines) is this repo's `docs/superpowers/` workflow convention, not feature
scope. No new file, no new include, no new i18n key, no new runtime state, no
allocation.

Both repo gates confirmed unmoved on this SHA, which is what proves rows were
gated rather than deleted:

```
python3 scripts/settings_snapshot.py | wc -l   -> 133   (main's baseline)
./scripts/i18n_orphans.sh | wc -l              ->  22   (main's baseline)
```

## Deviations from the Brief — checked, argued in the PR, and sound

All three were raised and adjudicated at the spec-review gate (CLEAR) and are
restated in the PR body. Re-verified here rather than assumed; none is a finding.

1. **The gate is compile-time, not a `BoardConfig` runtime predicate**
   (criterion 3's literal wording; spec **A1**). The reachable alternative — a
   repo-owned HAL probe like `halTiltSensor.isAvailable()` — would assert the
   hardware cannot rotate, which is false. The chosen form matches the SDK's own
   capability idiom. Confirmed empirically to gate the right things.
2. **`orientation` leaves the persistence schema** (spec **A5**), contradicting
   the Brief's "leave the persisted `SETTINGS.orientation` alone" as literally
   worded. Verified in full: `toJson`/`fromJson` iterate `getSettingsList()`
   (`CrossPointSettings.cpp:66,113`) and a case-insensitive grep for
   `orientation` in that file returns nothing, so there is no manual handling to
   keep the key alive. `saveToFile` builds a **fresh** `JsonDocument`
   (`PersistableStore.h:126-131`), so the stale key is dropped at the next save.
   This is the only migration path for a unit already saved in landscape, and the
   Brief's stated reason for the constraint (the sleep screen) is preserved
   because the field still exists and reads `PORTRAIT`. The repo already does
   this on this board: the erase block at `SettingsList.h:459-467` un-persists
   `"fadingFix"`, `"frontButtonFollowOrientation"` and `"backShortToFileBrowser"`
   the same way.
3. **Entry #2 gated with `#if` rather than by extending the erase block**
   (spec **A4**). The resulting list is identical and `#if` is the idiom this
   same static list already uses for compile-time capabilities
   (`SettingsList.h:279`, `:352`).

## Tests — the gap is real, disclosed, and correctly scoped out

No automated test is added, and none is possible without a materially larger
change. Verified: nothing under `test/` references `CrossPointSettings` or
`SettingsList` (grep, zero hits), and `test/` defines no `FREEINK_DEVICE_*`, so
the host suite cannot compile the header the gate lives in. A test asserting
`BEREAN_CAP_ROTATION == 0` would assert the build flag, not behaviour — the PR
says exactly this rather than writing one, which is the right call.

The `#error` of spec **A2** was also verified rather than trusted. Preprocessing
`SettingsActivity.cpp` with `-UFREEINK_DEVICE_X4PRO -DFREEINK_DEVICE_X4=1`
returns rc 1 with exactly one error, ours:

```
src/CrossPointSettings.h:21:2: error: #error "BEREAN_CAP_ROTATION: unhandled device set; ..."
```

And it imposes no new limitation on the firmware build: `FirmwareBoardTag.cpp:16`
already `#error`s any build without `FREEINK_DEVICE_X4PRO`.

Consequently the only real verification is the device walk, and the PR hands it
over item by item (body §"Not verified — needs hardware", items 1–7, matching
plan step 6 and spec §7.3) with the landscape-migration and heap items explicitly
marked as the human's. That disclosure is accurate, not hedged.

## For the human to confirm on hardware

Not a finding — the hand-off list is correct and complete. The two items that
cannot be inferred from source and are worth prioritising:

- item 4, a unit with a **non-portrait value already persisted** comes up
  portrait and stays portrait across a reboot (the **A5** migration path);
- item 2, Long-press behaviour now **cycles on tap** instead of opening a popup,
  because the popup is gated on `enumValues.size() > 2`
  (`SettingsActivity.cpp:260`). Behaviourally different from before, consistent
  with `STR_SHORT_PWR_BTN`'s existing treatment, and disclosed.

---

No source file was modified during this review. `compile_commands.json` was
generated and deleted; `git status --short` shows only this review file.

VERDICT: CLEAR
