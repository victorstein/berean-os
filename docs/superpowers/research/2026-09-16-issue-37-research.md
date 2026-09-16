# Issue #37 — research

How the repo behaves today, measured on `ded48d17` in the
`fix/37-gate-screen-rotation` worktree on 2026-09-16. Every line cited below was
read; every command shown was run here.

The issue's own file:line claims were re-checked and hold, with three
corrections noted in §6. The findings that change the design are §3 (the gate
predicate cannot be what criterion 3 asks for), §2.2 (erasing the row silently
un-persists `orientation`), and §5 (nothing automatable can verify this change).

---

## 1. Who owns the behaviour

Three ungated entry points, all confirmed:

| # | Entry point | Line |
| --- | --- | --- |
| 1 | reader menu row | `src/activities/reader/EpubReaderMenuActivity.cpp:73` |
| 2 | Settings → Reader row | `src/SettingsList.h:317-320` |
| 3 | `STR_LONG_PRESS_BEHAVIOR` enum value | `src/SettingsList.h:347-350` |

Their supporting cast:

- Popup + labels for #1: `EpubReaderMenuActivity.cpp:105`,
  `EpubReaderMenuActivity.h:81-82` (`orientationLabels`).
- Handler for #3: `EpubReaderActivity.cpp:625-632`.
- `ORIENTATION_CHANGE = 2` is the **trailing** member of
  `LONG_PRESS_BUTTON_BEHAVIOR` (`CrossPointSettings.h:177-182`). That matters —
  see §2.3.

**Every reader of the persisted value**, which is wider than the issue's
constraint list (it names `SleepActivity.cpp:513,525` and
`ReaderActivity.cpp:37`):

```
src/main.cpp:608                          halTiltSensor.update(..., SETTINGS.orientation, ...)
src/activities/reader/ReaderActivity.cpp:37
src/activities/reader/EpubReaderActivity.cpp:282   (seeds the menu ctor)
src/activities/reader/EpubReaderActivity.cpp:286-287, 627-629, 883-898
src/activities/boot_sleep/SleepActivity.cpp:513, 525
```

`main.cpp:608` and `EpubReaderActivity.cpp:282` are additional readers the brief
does not mention. Neither blocks the change, but "leave the persisted value
alone" has five call sites behind it, not three.

## 2. Control flow, and the two things it does that are not obvious

### 2.1 `getSettingsList()` is not only the UI list

`src/SettingsList.h:229` builds a `static const baseList` **once** (lines
230-443), then returns a per-call copy with the capability filters applied
(445-482). Five callers:

```
src/activities/settings/SettingsActivity.cpp:49   device Settings screen
src/network/CrossPointWebServer.cpp:1159, 1263    web settings API (read, write)
src/CrossPointSettings.cpp:66                     toJson  — SAVE
src/CrossPointSettings.cpp:113                    fromJson — LOAD
```

The list is the persistence schema. One gate therefore covers the device UI and
the web API together — and also changes what is written to disk.

### 2.2 Erasing the `STR_ORIENTATION` row stops `orientation` persisting

`toJson` (`CrossPointSettings.cpp:63-84`) writes `doc[info.key] = s.*(info.valuePtr)`
for each listed entry; `fromJson` (`:107-171`) reads it back the same way. The
`orientation` key exists **only** through that loop:

```
$ grep -n orientation src/CrossPointSettings.cpp
(no output)
```

So an erase drops `"orientation"` from the settings file on the next save, and
`SETTINGS.orientation` falls back to its struct default `PORTRAIT`
(`CrossPointSettings.h:241`) on every boot.

This cuts both ways and the spec must choose deliberately:

- **It is the migration.** A unit that already persisted landscape would
  otherwise be stuck there forever once all three entry points are hidden. The
  erase un-sticks it for free.
- **It contradicts the brief's constraint** as literally worded. If the value
  must keep persisting, the established remedy is in the same file:
  `longPressMenuFunction`, `fontFamily` and `fontSize` are written by hand at
  `CrossPointSettings.cpp:92-100` and read back by hand at `:193-221`, precisely
  because the generic loop skips them. Orientation would need both halves, not
  just the save.

Note the cost of that remedy: it edits `src/CrossPointSettings.cpp`, which task
t5 (#27) has claimed. Leaving persistence alone keeps this diff out of that
file.

### 2.3 A shortened enum self-heals, because `fromJson` already clamps

`CrossPointSettings.cpp:161`:

```cpp
v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
```

Drop `ORIENTATION_CHANGE` from #3's value list and a persisted `2` is clamped at
load to the field default `OFF` (`CrossPointSettings.h:278`). **Acceptance
criterion 2 is satisfied by this existing clamp** — the handler at
`EpubReaderActivity.cpp:625` becomes genuinely unreachable, with nothing new
written to achieve it.

Two consequences of shortening that list from 3 values to 2:

- `SettingsActivity.cpp:260` switches the control from a popup to
  cycle-on-tap (`> 2` gates the popup); the cycle at `:273` is
  `(currentValue + 1) % 2`.
- Within a single session a stale `2` renders as an empty value string, not an
  out-of-bounds read — `settingValueText` guards it at
  `SettingsActivity.cpp:412-413`. After a reboot the clamp has already fixed it.

Because `ORIENTATION_CHANGE` is the **trailing** enum member, a plain shortened
list is safe: displayed position still equals raw value. The gap-handling
machinery in `buildLongPressMenuRawValues` (`SettingsList.h:157-167`) is **not**
needed here, and its comment at `:138-148` says why — that apparatus exists only
because `LP_MENU_READER_MENU` sits mid-enum.

### 2.4 Removing the reader-menu row leaves no dead code

`pendingOrientation` is seeded from `SETTINGS.orientation` in the menu
constructor (`EpubReaderMenuActivity.cpp:25`, member at `.h:79`). With the row
gone it is never reassigned, so `menu.orientation == SETTINGS.orientation` and
the guard at `EpubReaderActivity.cpp:286` simply never fires. No cleanup is
forced on the result path.

## 3. The gate predicate — criterion 3 cannot be met as written

Acceptance criterion 3 asks for "a `BoardConfig` capability predicate, not
`BoardConfig::isX4Pro()`". **`BoardConfig.h` is not in this repository.** It is
`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`, inside the
`freeink-sdk` submodule, whose remote is `https://github.com/Free-Ink/freeink-sdk.git`
(`.gitmodules`), pinned at `310ec615`. We cannot push there, so a new
`BoardConfig::isPortraitOnly()` is out of reach for this task.

The existing roster of runtime predicates is `BoardConfig.h:1614-1695`
(`isX4Pro()`, `hasTouch()`, `hasHomeKey()`, `hasPwmFrontlight()`, `hasAudio()`,
`hasMic()`, `hasBuzzer()`, `hasRtc()`, `hasTempHumidity()`, `hasImu()`,
`hasLeds()`). **None is about orientation**, and each derives from a
`BoardDescriptor` field we also cannot add.

Three reachable options, all in this repo:

| Option | Shape | Precedent |
| --- | --- | --- |
| **a** | extend the `hasTouch()` erase block | `SettingsList.h:459-467` — the literal block the brief points at |
| **b** | a repo-owned predicate in `lib/hal` | `halTiltSensor.isAvailable()` (`lib/hal/HalTiltSensor.h:54`), `Frontlight.present()` (`lib/hal/HalFrontlight.h:13`) |
| **c** | a compile-time capability macro | `#if FREEINK_CAP_TOUCH` / `FREEINK_CAP_FRONTLIGHT` / `FREEINK_CAP_WARMLIGHT`, already used at `SettingsList.h:279, 352, 392` |

Worth seeing: the two predicates the brief cites as precedent are **both owned by
this repo's HAL**, not by `BoardConfig`. Option (b) is therefore the closest
thing to what criterion 3 is reaching for that is actually buildable here.

**Option (a) is semantically wrong.** `hasTouch()` is true for 8 of the SDK's 13
boards (`FREEINK_CAP_TOUCH`, `BoardConfig.h:175-179`: Murphy, LilyGo, M5Paper,
Sticky, X4 Pro, PaperMono, PaperS3, Murphy M4). Touch is not portrait-only, and
the erase block is shared code inherited from upstream CrossPoint — commit
`bbca4886 feat: Add support for x4pro & papermono devices (#2983)` is the nearest
example of this exact kind of change, and it came from upstream.

**Does another board lose a working feature?** Not in this firmware. Both envs
pass `-DFREEINK_DEVICE_X4PRO=1` and nothing else (`platformio.ini:166, 184`);
there is no other `[env:...]`. `DEFAULT_DEVICE` resolves to `XTEINK_X4_PRO`
(`BoardConfig.h:1520`) and `ACTIVE = DEFAULT_DEVICE` at
`BoardConfig.h:1532`, so `hasTouch()`, `isX4Pro()` and `FREEINK_CAP_TOUCH` are
all statically true here — any of the three "works". `selectDevice()` is called
only for X3/X4 (`lib/hal/HalGPIO.cpp:118,126`), so `ACTIVE` is correct before
`main()` and a gate inside the static `baseList` initializer would read it
safely. So the answer to the brief's "stop and say so" test is: **no board in
this build loses anything**, but option (a) would be wrong the moment this code
is shared upstream or a second board appears — which is a reason to prefer (b)
or (c), not a reason to stop.

One trap if (c) is chosen: no `-Wundef` and no `-Werror` anywhere in
`platformio.ini`, so an undefined macro in `#if` silently evaluates to 0 with no
diagnostic. Polarity decides whether that silence is safe. A flag meaning
"portrait only" defaults to 0 = rotation kept elsewhere; a flag meaning
"rotation supported" defaults to 0 = rotation stripped from every board that
never heard of it. Caps are declared `#ifndef` (`BoardConfig.h:174-179`) and can
be forced from a build env (`freeink-sdk/platformio.sample.ini:60`).

## 4. Nearest existing examples, per entry point

The brief points all three at `SettingsList.h:459-467`. Only #2 actually lives
there; #1 and #3 have closer neighbours:

- **#1 reader menu** — `buildMenuItems` already gates rows with a plain `if`
  around the `push_back`: `if (Frontlight.present())` at
  `EpubReaderMenuActivity.cpp:70-72`, and `if (hasHighlights)` at `:61-63, 65-67`
  (a `BOARD_HAS_PSRAM` decision passed down from the caller,
  `EpubReaderActivity.cpp:272-279`). Not the erase idiom at all. `MAX_MENU_ITEMS`
  is only a `reserve` (`EpubReaderMenuActivity.h:57-58`), so dropping a row needs
  no other adjustment.
- **#2 Settings row** — `SettingsList.h:459-467` (erase on `hasTouch()`), or the
  conditional-insert form at `:430-442` (`halTiltSensor.isAvailable()`).
- **#3 enum value** — `buildLongPressMenuRawValues` (`SettingsList.h:157-167`) is
  the only existing example of dropping an *option* rather than a row. Per §2.3
  its gap machinery is unnecessary here.

## 5. Verification reality — both gates are blind to a runtime erase

This is the part most likely to be assumed wrong later.

**No host test covers any of this.** `test/` has 51 directories; the only two
matching `orientation` or `SettingsList` are a code comment
(`test/number_grid/NumberGridLayoutTest.cpp:128`) and JSON fixture prose
(`test/release_json_parser/ReleaseJsonParserTest.cpp:97`). Nothing compiles
`src/SettingsList.h`.

Both repo gates are **textual scans of the source**, so a runtime `erase` — which
leaves every `StrId::` token in place — changes neither:

- `scripts/settings_snapshot.py` regexes `StrId::(STR_[A-Z0-9_]+)` out of
  `SettingsList.h`. Baseline today: **133 keys**, including `STR_ORIENTATION`,
  `STR_ORIENTATION_INVERTED`, `STR_PORTRAIT`, `STR_LANDSCAPE_CW`,
  `STR_LANDSCAPE_CCW`, `STR_LONG_PRESS_BEHAVIOR_ORIENTATION`.
- `./scripts/i18n_orphans.sh` greps `src` and `lib` for `\bSTR_KEY\b`. Baseline
  today: **22 orphans, exit 0** (gate is informational). None is orientation-related.

Consequence: **if the gate is implemented as a runtime erase, no automatable
check in this repo can detect whether it worked.** `pio run` proves only that it
compiles. The device smoke test is the only real verification and it is the
human's.

If instead the rows are removed at compile time, six StrIds lose their last
reference and surface in the orphan gate — `STR_ORIENTATION`, `STR_PORTRAIT`,
`STR_LANDSCAPE_CW`, `STR_LANDSCAPE_CCW`, `STR_ORIENTATION_INVERTED`,
`STR_LONG_PRESS_BEHAVIOR_ORIENTATION` (reference sites enumerated by grep).
`STR_INVERTED` would **not** orphan: it survives via `SettingsList.h:261` (image
filter) and `:436` (tilt page turn). Six new orphans also land in territory task
t6 (#31, `lib/I18n/translations`) owns.

Two stale baselines to discard: `docs/superpowers/notes/phase-0-baseline.md`
records 0 i18n orphans and 132 settings keys as of 2026-09-14. Both have drifted
(22 and 133). Re-capture before diffing, do not trust that note.

## 6. Corrections to the issue text

1. "the seven `BoardConfig::isX4Pro()` call sites" — there are **eight**, and the
   issue's own list names eight. The substantive claim holds: all eight concern
   the power button, recovery boot, the fading fix and input frames, none
   orientation.
2. `SYNTHETIC_HELD_MS` is at `lib/Input/Input/NavKeyGestures.h:68` (value 40),
   returned via `HalGPIO.cpp:237`, not `236-239`. The mechanism is as described
   and verified: `reportingSyntheticHeldTime()` (`NavKeyGestures.cpp:60-62`) is
   true whenever a left/right nav event is pending, and 40 < `SKIP_HOLD_MS = 700`
   (`ReaderUtils.h:18`), so the button path can never register a long press.
3. The issue's "the feature still works via touch" aside is correct and matters
   for criterion 2: `touch.heldMs` comes from `gpio.lastTouchHeldMs()`
   (`ReaderUtils.h:120`) and is a real duration, so before the gate the
   `ORIENTATION_CHANGE` handler **is** reachable by a long touch page-turn. It is
   the `fromJson` clamp of §2.3, not the absence of a button, that closes it.

## 7. Environment, as actually installed

Verified by running each:

| | |
| --- | --- |
| PlatformIO Core | 6.1.19, at `~/.platformio/penv/bin/pio` |
| `pio` on PATH | **not present** — `which pio` → not found; use the penv path |
| platform | pioarduino `platform-espressif32` 55.03.37 (`platformio.ini:15`) |
| framework | `framework-arduinoespressif32` 3.3.7 |
| compiler | `xtensa-esp-elf-g++` 14.2.0 (crosstool-NG esp-14.2.0_20251107) |
| esptool | 5.1.2 |
| clang-format | **absent** — no `.venv` here, none on PATH |

Two fresh-worktree bootstrap facts, both hit in this session:

- **The `freeink-sdk` submodule was uninitialized** (`git submodule status` showed
  a leading `-`), so `BoardConfig.h` did not exist. Fixed here with
  `git submodule update --init --recursive`. Without it nothing builds and no
  `BoardConfig` claim can be checked.
- **There is no `.venv`**, so the brief's
  `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` cannot work as written.
  The wrapper requires clang-format ≥ 21 and exits 1 when absent
  (`bin/clang-format-fix:4-11, 22-30`); CI installs `clang-format-21` and runs
  `PATH="/usr/lib/llvm-21/bin:$PATH" ./bin/clang-format-fix`
  (`.github/workflows/ci.yml:45-56`). A working binary already exists at
  `/Volumes/stein/Documents/development/personal/berean-os/.venv/bin/clang-format`
  (21.1.8) — the implement phase must either create a venv here or point PATH at
  that one.

**Baseline build passes.** `~/.platformio/penv/bin/pio run -e x4pro` → SUCCESS,
48.6 s compile, exit 0:

```
RAM:   [==        ]  19.5% (used 64052 bytes from 327680 bytes)
Flash: [========  ]  81.2% (used 5318858 bytes from 6553600 bytes)
firmware.bin = 5,319,360 bytes
```

Flash is at 81.2% of the 6,553,600 B app partition. This change only removes
list entries, so it cannot make that worse.

## 8. One coordination risk worth knowing

The brief says task t4 (#38) "is serialized behind you". On the evidence it is
not — the pipeline's file-overlap gate is inert for this run.

`filesOverlap` compares entries with `x.startsWith(y) || y.startsWith(x)`
(`herdr-plugin-pipeline/src/lib/gating.ts:19-21`), but every task in
`~/.local/state/herdr/plugins/stein.pipeline/runs/personal/berean-os-20260916-berean-os-issue-batch-ujku.json`
registered its `files` as a **single space-joined string**, so no prefix ever
matches. Evaluating the real ledger data against that function yields **zero
overlapping pairs**, while the literal paths overlap:

- t3 (this task) ∩ t4 (#38): `EpubReaderActivity.cpp`,
  `EpubReaderMenuActivity.cpp`, `EpubReaderMenuActivity.h`
- t3 ∩ t5 (#27): `EpubReaderActivity.cpp`, `EpubReaderMenuActivity.cpp`

`depends_on` is `[]` for t3, t4 and t5, so nothing else serialises them either.
Treat the reader-menu files as concurrently edited: keep the diff to the
orientation rows (as the brief already says), and expect the real backstop to be
the orchestrator's PR-level conflict check rather than the file gate.

## 9. What I could not verify

- **Anything on hardware.** That the gated build actually hides all three entry
  points, and that the sleep screen still renders correctly once
  `SETTINGS.orientation` is no longer persisted (§2.2), are device checks.
- **Whether the un-sticking migration in §2.2 is wanted.** It is a product call,
  and it is the one place where following the brief's constraint literally and
  serving the user diverge.
- **`pio check`** was not run; `pio run -e x4pro` was.
- Five sibling worktrees may build concurrently against the same
  `~/.platformio`; this build logged
  `*** Original Arduino "idf_component.yml" restored ***`, which is the shared
  file that races. A one-off build failure there is not necessarily a code fault.
