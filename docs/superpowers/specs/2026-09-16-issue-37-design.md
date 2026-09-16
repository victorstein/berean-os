# Issue #37 — design: gate screen rotation on a portrait-only device

Pass 0. Builds on
`docs/superpowers/research/2026-09-16-issue-37-research.md`; every claim below
was re-checked against the code in this worktree at `77b5d3ac`.

Two deviations from the brief are argued for and labelled — **A1/A4** (the gate
is compile-time, not a `BoardConfig` runtime predicate) and **A5** (`orientation`
leaves the persistence schema). Both follow from research findings the brief was
written without.

---

## 1. Problem

`CLAUDE.md` states the invariant: **portrait only** — a fixed Left/Right nav
mapping and a device that turns over cannot both be true. The code does not
enforce it. Three entry points offer rotation:

| # | Entry point | Line |
| --- | --- | --- |
| 1 | reader menu row | `src/activities/reader/EpubReaderMenuActivity.cpp:73` |
| 2 | Settings → Reader row | `src/SettingsList.h:317-320` |
| 3 | `STR_LONG_PRESS_BEHAVIOR`'s `ORIENTATION_CHANGE` value | `src/SettingsList.h:347-350` |

A fourth surface exists but is dormant: `getSettingsList()` is also the web
settings schema (`src/network/CrossPointWebServer.cpp:1159, 1263`), so
`/api/settings` (`CrossPointWebServer.cpp:168-170`) can read and write
`orientation` too. It is unreachable from the UI today — `goToFileTransfer()`
(`src/activities/ActivityManager.cpp:200-202`) has exactly one caller,
`HomeActivity::onFileTransferOpen` (`src/activities/home/HomeActivity.cpp:325`),
and `HomeActivity` is never instantiated (no `make_unique<HomeActivity>` in
`src`). Dormant, not deleted.

Rotation is **not broken**. It is a working renderer feature and this change
does not touch it. The case for gating is the product rule.

## 2. Goal

The three entry points are not offered on a board whose nav mapping is fixed in
portrait, using a single named capability, derived once, with no way for a future
board to inherit the decision silently.

## 3. Non-goals

- Removing the renderer's rotation support (`GfxRenderer::setOrientation`,
  `ReaderUtils::applyOrientation` at `src/activities/reader/ReaderUtils.h:27-41`).
  Other `freeink-sdk` boards use it.
- Touching `MappedInputManager::mapButton` or `NavKeyGestures` — the input blast
  radius `CLAUDE.md` flags.
- Deleting the `ORIENTATION` enum (`src/CrossPointSettings.h:67-73`) or the
  `SETTINGS.orientation` field (`:241`). Five readers depend on it: `main.cpp:608`,
  `ReaderActivity.cpp:37`, `EpubReaderActivity.cpp:282`, `SleepActivity.cpp:513`
  and `:525`.
- Any i18n change. See **A9**.
- Anything in `QrDisplayActivity` or the QR menu row — task t4 (#38) owns those.

## 4. Architecture

### 4.1 The capability

One compile-time capability, `BEREAN_CAP_ROTATION`, derived from the device set
and defined once in `src/CrossPointSettings.h` immediately above the `ORIENTATION`
enum it constrains (`:67`):

```cpp
// Whether rotation is OFFERED to the user. The renderer's rotation support is
// untouched; this gates only the three places that exposed it.
//
// The X4 Pro's Left/Right nav buttons are a fixed physical pair and the panel
// cannot tell the firmware which way is up, so a rotated frame and a fixed
// mapping cannot both be right. Any future board must answer this question for
// itself rather than inherit the answer -- hence the #error rather than a
// default.
#ifndef BEREAN_CAP_ROTATION
#if FREEINK_DEVICE_X4PRO
#define BEREAN_CAP_ROTATION 0
#else
#error "BEREAN_CAP_ROTATION: unhandled device set; decide whether this board offers rotation"
#endif
#endif
```

**Modelled on** `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:174-179`
(`#ifndef FREEINK_CAP_TOUCH` + derivation from the `FREEINK_DEVICE_*` set, with
the `#ifndef` leaving a build env free to force it) for the shape, and on
`src/network/FirmwareBoardTag.cpp:13-17` for the `#error`-on-unhandled-device
half — the repo's own precedent for deriving a compile-time value from
`FREEINK_DEVICE_X4PRO` and failing loudly otherwise.

Both call sites already include the home header directly: `src/SettingsList.h:15`
and `src/activities/reader/EpubReaderMenuActivity.cpp:10`. No new file, no new
include.

> **A1.** The gate is a **compile-time capability derived from the device set**,
> not a `BoardConfig` runtime predicate as acceptance criterion 3 asks for.
> Criterion 3 is not reachable: `BoardConfig.h` lives in the `freeink-sdk`
> submodule (remote `Free-Ink/freeink-sdk`, pinned `310ec615` per `.gitmodules`),
> which we cannot push to, and its predicate roster (`BoardConfig.h:1614-1693`)
> has nothing about orientation. This satisfies the criterion's *intent* — a
> named capability rather than board identity — and it is not
> `BoardConfig::isX4Pro()` in disguise, because the derivation is over the
> compiled device set, exactly as `FREEINK_CAP_TOUCH` is. The reachable
> alternative, a repo-owned HAL predicate in the shape of
> `halTiltSensor.isAvailable()` (`lib/hal/HalTiltSensor.h:54`) or
> `Frontlight.present()` (`lib/hal/HalFrontlight.h:13`), was rejected: both
> report a *hardware probe* result, and it would be false to claim the hardware
> cannot rotate when the renderer and panel plainly can. The thing being gated
> is a product rule.

> **A2.** An unhandled device set is an `#error`, not a safe default. This adds
> no new limitation: `src/network/FirmwareBoardTag.cpp:16` already `#error`s any
> build without `FREEINK_DEVICE_X4PRO`, so the repo cannot build for another
> board today. Chosen over "absent means rotation stays" because
> `platformio.ini` carries no `-Wundef` and no `-Werror`, so an undefined macro
> in `#if` evaluates to 0 with no diagnostic — a default would be silent either
> way, and the wrong polarity would strip rotation from a board that wanted it.

> **A3.** The capability lives in `src/CrossPointSettings.h`, beside the enum it
> constrains, rather than in a new `DeviceCapabilities.h`. The repo has no
> central capability header: `BOARD_HAS_PSRAM` is used as a bare `#if` at each
> site (`src/SettingsList.h:161`, `src/activities/reader/EpubReaderActivity.cpp:275`).
> A single derived definition is still preferred over repeating the derivation at
> three sites, for the reason `FirmwareBoardTag.cpp:8-12` documents: an
> independently repeated literal lets a partial edit compile and go wrong
> silently.

### 4.2 The three sites

Each mirrors the nearest existing example **in its own file**, which is not the
same idiom in all three places.

**#1 reader menu** — `src/activities/reader/EpubReaderMenuActivity.cpp:73`:

```cpp
#if BEREAN_CAP_ROTATION
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
#endif
```

Modelled on the `#if BOARD_HAS_PSRAM` capability gate the same feature family
already uses (`src/activities/reader/EpubReaderActivity.cpp:275-279`, whose
result reaches `buildMenuItems` as `hasHighlights` and gates rows at
`EpubReaderMenuActivity.cpp:61-67`). `MAX_MENU_ITEMS` is only a `reserve`
(`EpubReaderMenuActivity.h:57-58`), so dropping a row needs nothing else.

**#2 Settings row** — `src/SettingsList.h:317-320`: wrap the `SettingInfo::Enum`
entry in `#if BEREAN_CAP_ROTATION`. Modelled on `#if FREEINK_CAP_FRONTLIGHT` at
`SettingsList.h:279` and `#if FREEINK_CAP_TOUCH` at `:352`, both of which vary
entries of this same static list at compile time.

**#3 long-press value** — `src/SettingsList.h:347-350`: two variants of the row,
selected by the capability, dropping only the trailing `ORIENTATION_CHANGE`
label:

```cpp
#if BEREAN_CAP_ROTATION
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
#else
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
#endif
```

**Modelled directly on `src/SettingsList.h:352-362`**, where `STR_SHORT_PWR_BTN`
is already written as two variants of one enum row under `#if FREEINK_CAP_TOUCH`,
the non-touch variant dropping the trailing `STR_CONFIRM`. Same file, adjacent
row, same problem.

> **A4.** Entry #2 uses `#if` in the static list rather than extending the erase
> block at `SettingsList.h:459-467`, which is what the brief asks for. Reasons:
> the erase block is a *runtime* filter keyed on a runtime predicate
> (`BoardConfig::hasTouch()`), and our capability is compile-time; `#if` is the
> idiom this same list already uses for compile-time capabilities (`:279`,
> `:352`, `:392`); and `#if` never builds the row at all, where the erase builds
> it into the static `baseList` and then removes it from every copy. The
> resulting list is identical either way.

> **A8.** #3 drops a value from the enum with a plain shortened list and **no
> index remapping**. Safe only because `ORIENTATION_CHANGE = 2` is the
> **trailing** member of `LONG_PRESS_BUTTON_BEHAVIOR`
> (`src/CrossPointSettings.h:177-182`), so displayed position still equals raw
> value. The getter/setter apparatus in `buildLongPressMenuSetting`
> (`SettingsList.h:192-217`) is deliberately **not** copied; its comment at
> `:138-148` explains it exists only because `LP_MENU_READER_MENU` sits
> mid-enum. Copying it here would be cargo-culting.

### 4.3 What is deliberately not touched

> **A7.** The `ORIENTATION_CHANGE` handler at
> `src/activities/reader/EpubReaderActivity.cpp:625-632` is left exactly as is,
> and acceptance criterion 2 is met without editing it. `fromJson` already
> clamps an ENUM to the offered list — `v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault)`
> at `src/CrossPointSettings.cpp:161` — so a persisted `2` becomes the field
> default `OFF` (`CrossPointSettings.h:278`) at load, and the branch is
> unreachable thereafter. This keeps the diff out of a file both t4 (#38) and t5
> (#27) have claimed. Note the clamp does not set `needsResave`, so the stale
> `"longPressButtonBehavior": 2` survives in the file until the next save; the
> in-memory value is `OFF` from load onward and any save writes `OFF`.

> **A6.** The reader menu's rotation popup (`EpubReaderMenuActivity.cpp:104-117`)
> and `orientationLabels` (`EpubReaderMenuActivity.h:81-82`) stay. With the row
> gone, `activateIndex` can never see `ROTATE_SCREEN`, so this is dead but
> compiled code. Kept to hold the diff to one line in a file t4 (#38) is editing
> concurrently. Attack this if the dead code matters more than the conflict
> surface — removing it is a two-line follow-up, and it would also orphan
> nothing (see **A9**).

`EpubReaderMenuActivity.h` needs no change, so the diff touches
`src/CrossPointSettings.h`, `src/SettingsList.h` and
`src/activities/reader/EpubReaderMenuActivity.cpp` only — narrowing the overlap
with t4 (#38) to that last file alone.

## 5. Data and control flow

### 5.1 `getSettingsList()` is the persistence schema

`src/SettingsList.h:229` builds a `static const baseList` once (`:230-443`) and
returns a per-call copy with runtime filters applied (`:445-482`). Five
consumers:

```
src/activities/settings/SettingsActivity.cpp:49   device Settings screen
src/network/CrossPointWebServer.cpp:1159          web settings read
src/network/CrossPointWebServer.cpp:1263          web settings write
src/CrossPointSettings.cpp:66                     toJson  — SAVE
src/CrossPointSettings.cpp:113                    fromJson — LOAD
```

So removing the row at `:317-320` closes four surfaces at once, including the
web write path — which only applies keys it finds in the list
(`CrossPointWebServer.cpp:1267-1268`) and bounds-checks an ENUM against
`enumValues.size()` (`:1280-1283`), so #3's shortened list closes the web route
to `ORIENTATION_CHANGE` too.

> **A10.** Closing the dormant web surface is treated as desirable defence in
> depth, not as the justification. It costs nothing extra — it falls out of
> gating the shared list.

### 5.2 `orientation` leaves the persistence schema

`toJson` (`CrossPointSettings.cpp:63-84`) and `fromJson` (`:107-171`) persist a
field only if its entry is in the list; `orientation` has no manual handling
(`grep -n orientation src/CrossPointSettings.cpp` → no output). So after the
gate:

- `saveToFile()` builds a **fresh** `JsonDocument`
  (`lib/Serialization/PersistableStore.h:127-132`) and writes the whole file, so
  `"orientation"` disappears from `settings.json` at the next save — no stale key
  lingers.
- `fromJson` no longer reads it, so `SETTINGS.orientation` holds its struct
  default `PORTRAIT` (`CrossPointSettings.h:241`) from every boot.
- All five readers therefore see `PORTRAIT`, and
  `ReaderUtils::applyOrientation` maps that to
  `GfxRenderer::Orientation::Portrait` (`ReaderUtils.h:28-30`) — correct for a
  portrait-only device.
- Nothing writes `SETTINGS.orientation` any more: its only writer is
  `EpubReaderActivity::applyOrientation` (`:896`), reachable only from the menu
  result (`:287`) and the long-press branch (`:629`), both now gated.

> **A5.** **This contradicts the brief's constraint "leave the persisted
> `SETTINGS.orientation` alone" as literally worded, and is accepted
> deliberately.** Three reasons. (i) On a portrait-only build `PORTRAIT` is the
> only legal value, so the schema loses no information — the field becomes a
> constant, not a lost setting. (ii) It is the **only** migration path for a unit
> that already saved `LANDSCAPE_CW`: with all three entry points gated there is
> otherwise no way back, and that unit would be stuck sideways forever. (iii) The
> alternative — hand-written save and load beside `longPressMenuFunction`,
> `fontFamily` and `fontSize` at `CrossPointSettings.cpp:92-100` and `:193-221`
> — would preserve exactly the value that strands the user, and would edit a file
> t5 (#27) has claimed. The brief's constraint was written to protect the sleep
> screen and `ReaderActivity`; pinning the value to `PORTRAIT` protects them just
> as well, because they only ever needed *a* valid orientation to render.
>
> Attack this if the intent was that a unit keeps its rotation across the
> update. If so the fix is (iii) plus one of the entry points staying, which
> reopens the issue.

### 5.3 Reader menu result path stays coherent

`pendingOrientation` is seeded from `SETTINGS.orientation` in the menu
constructor (`EpubReaderMenuActivity.cpp:25`, member declared at `.h:79`). With
the row gone it is never reassigned, so `menu.orientation == SETTINGS.orientation`
and the guard at `EpubReaderActivity.cpp:286` never fires. No cleanup is forced
on the result path and `MenuResult` keeps its shape.

## 6. Error handling

There are no new runtime failure modes: the change removes list entries and
adds no allocation, no I/O and no new state. `CLAUDE.md`'s resource protocol has
nothing to bind — no heap, no task, no file.

The failure modes that do exist are build-time and load-time:

| Condition | Handling |
| --- | --- |
| Build for a device that has not answered the question | `#error` at compile time (**A2**), modelled on `FirmwareBoardTag.cpp:16` |
| Persisted `longPressButtonBehavior == ORIENTATION_CHANGE` | Clamped to `OFF` at load by the existing `CrossPointSettings.cpp:161` (**A7**) |
| Persisted `orientation == LANDSCAPE_*` | Ignored at load; value pinned to `PORTRAIT` (**A5**) |
| A stale `2` rendered before any reboot | `settingValueText` already guards the index and returns `""` (`SettingsActivity.cpp:412-413`) rather than reading past `enumValues` |

One behavioural side effect to be aware of rather than handle: shortening #3's
list from three values to two flips that control from a popup to cycle-on-tap,
because the popup is gated on `enumValues.size() > 2`
(`SettingsActivity.cpp:260`); the fallthrough cycle is
`(currentValue + 1) % 2` (`:273`). That is the same treatment `STR_SHORT_PWR_BTN`
already gets on non-touch boards, so it is consistent with the precedent this
mirrors.

## 7. Testing strategy

### 7.1 What can be verified here

1. **Build** — `~/.platformio/penv/bin/pio run`, once, after the last edit
   (the bare `pio` is not on PATH). Acceptance criterion 4. Baseline for
   comparison, measured in research: SUCCESS, flash 81.2% of the 6,553,600 B app
   partition, `firmware.bin` 5,319,360 B. This change only removes list entries,
   so flash must not grow.
2. **Format** — `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` over the
   whole tree. The wrapper requires clang-format ≥ 21 and exits 1 when absent
   (`bin/clang-format-fix:4-11, 22-30`), so a missing PATH prefix reads as a
   failure, not a false pass. `.venv` is now a symlink to the main checkout
   (21.1.8).
3. **Gate re-baseline** — capture both before and after, and expect **no diff**:
   - `python3 scripts/settings_snapshot.py` — 133 keys today. It regexes
     `StrId::(STR_[A-Z0-9_]+)` out of `SettingsList.h`, and `#if` does not remove
     source text, so this must be unchanged. A diff here means rows were deleted
     rather than gated.
   - `./scripts/i18n_orphans.sh` — 22 orphans today, exit 0. Must also be
     unchanged, per **A9**.

> **A9.** No i18n work is needed and no new orphan should appear.
> `scripts/i18n_orphans.sh` is a plain grep for `\bSTR_KEY\b` over `src` and
> `lib`, and every `StrId::` token in this design stays in the source text —
> inside an `#if` branch, an `#else` branch, or untouched code. Verified
> reference sites: `STR_ORIENTATION` (`SettingsList.h:318`,
> `EpubReaderMenuActivity.cpp:73, 105`), `STR_ORIENTATION_INVERTED`
> (`SettingsList.h:319` only), `STR_PORTRAIT` / `STR_LANDSCAPE_CW` /
> `STR_LANDSCAPE_CCW` (`SettingsList.h:319`, `EpubReaderMenuActivity.h:81-82`),
> `STR_LONG_PRESS_BEHAVIOR_ORIENTATION` (`SettingsList.h:349` only). This is the
> main reason to prefer gating over deletion: deletion would orphan six keys and
> land in territory t6 (#31) owns.

### 7.2 What cannot be verified here — and the honest gap

**No host test covers any of this, and neither repo gate can see the change.**
`test/` has 51 directories and none compiles `src/SettingsList.h`; the only
matches for `orientation` are a code comment
(`test/number_grid/NumberGridLayoutTest.cpp:128`) and JSON fixture prose
(`test/release_json_parser/ReleaseJsonParserTest.cpp:97`). Both gates are
textual scans that this design deliberately leaves unchanged (§7.1.3).

So `pio run` proves only that it compiles. **A green build is not evidence the
feature works.** No new host test is proposed: the gated values are compile-time
constants in a header the host suite does not build, and a test asserting
`BEREAN_CAP_ROTATION == 0` would assert the build flag, not the behaviour.

> **A11.** Accepting device-only verification is a deliberate choice, not an
> oversight. Attack it if a host test is wanted; making one possible means
> compiling `SettingsList.h` on the host, which is a materially larger change
> than this issue.

### 7.3 Device smoke test — the human's, and the only real verification

1. Settings → Reader: **no Orientation row**; the rows either side of it
   (Hyphenation, Extra spacing) are intact and the category still scrolls.
2. Settings → Controls → Long-press behaviour: offers **Off / Chapter skip
   only**. It now cycles on tap instead of opening a popup (§6) — confirm it
   cycles between exactly those two.
3. Reader → centre-tap menu: **no Orientation row**; Night mode, Frontlight and
   Auto page turn still present and still activate.
4. On a unit that had been saved in landscape before the update: it comes up
   **portrait** and stays portrait across a reboot (**A5**).
5. Sleep screen still renders right way up after a power-pull and after a normal
   sleep (`SleepActivity.cpp:513, 525`).
6. A long touch page-turn no longer rotates the screen (**A7**).
7. Heap unchanged across a reader → settings → reader cycle; `ESP.getFreeHeap()`
   above ~50 KB.

## 8. Assumption index

| # | Decision | Where |
| --- | --- | --- |
| A1 | Compile-time capability derived from the device set, not a `BoardConfig` runtime predicate — criterion 3 is unreachable | §4.1 |
| A2 | `#error` on an unhandled device set rather than a default | §4.1 |
| A3 | Capability defined in `src/CrossPointSettings.h`, not a new header | §4.1 |
| A4 | Entry #2 gated with `#if`, not by extending the erase block the brief names | §4.2 |
| A5 | `orientation` leaves the persistence schema; contradicts the brief's constraint, accepted | §5.2 |
| A6 | Reader-menu popup and `orientationLabels` left in place as dead code | §4.3 |
| A7 | `EpubReaderActivity.cpp:625-632` untouched; criterion 2 met by the existing load clamp | §4.3 |
| A8 | Trailing enum value dropped with no index remapping | §4.2 |
| A9 | No i18n change and no new orphans, because `#if` preserves tokens | §7.1 |
| A10 | Closing the dormant web surface is a free side effect, not the rationale | §5.1 |
| A11 | Device-only verification accepted; no host test proposed | §7.2 |

## 9. Acceptance criteria, mapped

| Criterion | Met by |
| --- | --- |
| 1 — `STR_ORIENTATION` gone from reader menu and Settings → Reader | §4.2 #1 and #2 |
| 2 — `ORIENTATION_CHANGE` gone and its handler unreachable | §4.2 #3 for the option; **A7** for the handler, via the existing clamp |
| 3 — gate is a capability predicate, not `isX4Pro()` | §4.1, with **A1** recording that the literal `BoardConfig` form is unreachable |
| 4 — `pio run` succeeds | §7.1.1 |
