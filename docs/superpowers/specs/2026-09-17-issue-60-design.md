# Issue #60 — two input-layer findings: design

Date: 2026-09-17 · Branch: `fix/60-input-layer-traps`
Research: `docs/superpowers/research/2026-09-17-issue-60-research.md`

Issue #60 carries two independent findings that share a file neighbourhood and
nothing else. They ship in one PR as two commits.

---

## 1. Problem

### 1a. A setting labelled "button" that no button can reach

`src/SettingsList.h:349-358` offers **Long-press button behavior** with options
Off / Chapter skip. (The third option, Orientation change, is already compiled
out on this board by the `#if BEREAN_CAP_ROTATION` that landed with `d8e92208`.)

Chapter skip fires when a page turn arrives with a held time above
`SKIP_HOLD_MS = 700` (`src/activities/reader/ReaderUtils.h:18`), tested at
`src/activities/reader/EpubReaderActivity.cpp:605-606` and
`src/activities/reader/ReaderActivity.cpp:144-146`.

A nav key can never deliver one. `NavKeyGestures::updateKey`
(`lib/Input/Input/NavKeyGestures.cpp:22-31`) resolves each key **on release**
into exactly one of two outcomes:

- held < `HOLD_MS` (850, `NavKeyGestures.h:41`) → `NavEvent::Page`, emitted as
  `BTN_UP`/`BTN_DOWN` (`lib/hal/HalGPIO.cpp:198-201`) — but
  `reportingSyntheticHeldTime()` is true for `Page` as well as `Synth`
  (`NavKeyGestures.cpp:60-62`), so `HalGPIO::getHeldTime()` substitutes
  `SYNTHETIC_HELD_MS = 40` (`HalGPIO.cpp:236-239`, `NavKeyGestures.h:68`);
- held ≥ 850 → `NavEvent::Synth`, emitted as `BTN_BACK`/`BTN_CONFIRM`
  (`HalGPIO.cpp:194-197`) — **no page turn happens at all**.

So the gap is structural, not numeric: there is no hold duration that both pages
and reports over 700 ms. The label names the one input that cannot reach the
feature. Touch can and does — `ReaderUtils.h:120` fills `heldMs` from
`gpio.lastTouchHeldMs()`, a real duration that is never substituted, and both
readers prefer it when the turn came from a tap zone
(`EpubReaderActivity.cpp:605`, `ReaderActivity.cpp:144`).

### 1b. `getPressedFrontButton()` has two arms nothing can fire

`src/MappedInputManager.cpp:378-393` scans `BTN_BACK`, `BTN_CONFIRM`,
`BTN_LEFT`, `BTN_RIGHT`. Back and Confirm are reachable here, synthesised from
an 850 ms nav-key hold. Left and Right fall through to `inputMgr.wasPressed()`
(`HalGPIO.cpp:215`), whose bits come from
`isDigitalPressed(BoardConfig::ACTIVE.input.left / .right)`
(`InputManager.cpp:257-258`), and `isDigitalPressed` is
`pin >= 0 && digitalRead(pin) == LOW` (`InputManager.cpp:246`). The X4 Pro
profile leaves all four at `PIN_UNASSIGNED` (`BoardConfig.h:1396`,
`PIN_UNASSIGNED = -1` at `BoardConfig.h:406`), and
`HalGPIO::synthesisedEdge` has no `BTN_LEFT`/`BTN_RIGHT` case
(`HalGPIO.cpp:186-205`). The two arms are dead.

Dormant today: one call site (`ButtonRemapActivity.cpp:71`), one construction
(`SettingsActivity.cpp:309`), reached only from the entry appended inside
`if (!BoardConfig::hasTouch())` at `SettingsActivity.cpp:75-78`, and this board
declares a GT911 (`BoardConfig.h:1411`, predicate at `BoardConfig.h:1622`).

## 2. Goal

1. The Controls setting names an input that can actually reach it, in a way that
   stays true on boards that do have page buttons.
2. A future reader of `getPressedFrontButton()` cannot mistake its four arms for
   four working sources, and a board that reaches the remap flow without four
   wired front buttons fails loudly instead of stranding the user.
3. A host test pins the constant relationship that makes (1) true, so the label
   is revisited if the input layer changes underneath it.

## 3. Non-goals

- **Making the nav buttons reach chapter skip.** Rejected on evidence; see A1.
- **Removing or gating the setting.** It works, via touch. Gating it on
  `hasTouch()` would hide a working feature.
- **Renaming the persisted key or the settings member.** See A6.
- **Renaming `STR_LONG_PRESS_BEHAVIOR_OFF` / `_SKIP` / `_ORIENTATION`.** Their
  text is correct and hardware-neutral in all 32 languages. See A5.
- **Touching `NavKeyGestures`, `HalGPIO` or the synthesis itself.** Phase 2 owns
  the input layer (`.claude/agents/ui-dev.md`); this change only documents it.
- **`ButtonNavigator.cpp:70`** — continuous list scrolling is dead on the nav
  keys for the same suppression reason (`HalGPIO.cpp:181-182`,
  `HOLD_STATE_SUPPRESS_MS = 250`). Pre-existing, out of scope for #60, recorded
  in the research note so it is not rediscovered as new.
- **Device verification.** Flagged for the human in the PR body.

## 4. Architecture

### 4a. Reword the setting (commit 1)

Modelled on **`02d9106a` — "fix: label the launcher's fourth tile Settings
(#53)"**, which changed a user-facing label by retiring the inaccurate key and
justifying the swap by which languages actually carried it. Same shape here,
with one difference: #53 could reuse an existing key present in all 32 files;
no existing key fits this one (`english.yaml:90-97` is the whole neighbourhood),
so a new key is introduced.

| Change | File |
|---|---|
| Add `STR_LONG_PRESS_PAGE_TURN: "Long-press page turn"` | `lib/I18n/translations/english.yaml` (replacing line 93 in place) |
| Add `STR_LONG_PRESS_PAGE_TURN: "Al mantener para pasar página"` | `lib/I18n/translations/spanish.yaml` (replacing line 89 in place) |
| Delete `STR_LONG_PRESS_BEHAVIOR` | all 32 `lib/I18n/translations/*.yaml` |
| `StrId::STR_LONG_PRESS_BEHAVIOR` → `StrId::STR_LONG_PRESS_PAGE_TURN` | `src/SettingsList.h:350,355` (both arms of the `#if`) |

The option keys, the `#if BEREAN_CAP_ROTATION` split, the persisted key string
`"longPressButtonBehavior"` and the settings member all stay exactly as they are.

Regenerate with
`python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`. The three generated
files are gitignored and rebuilt on every `pio run` (`CLAUDE.md`, "Generated
files"), so only the YAML and `SettingsList.h` are committed.

### 4b. Trap-proof the front-button scan (commit 2)

Two separate mechanisms, because the two hazards are different:

**A block comment above `getPressedFrontButton()`**
(`src/MappedInputManager.cpp:378`), modelled on the explanatory block above
`HalGPIO::isPressed` (`lib/hal/HalGPIO.cpp:163-177`) — same house style: state
what the function can and cannot answer, name the board fact, cite the file that
proves it. It records that Back and Confirm arrive by synthesis from a nav-key
hold, that Left and Right have no source on a board whose `input.left`/
`input.right` are `PIN_UNASSIGNED`, and that `HalGPIO::synthesisedEdge` supplies
no substitute.

**A runtime `assert` in `ButtonRemapActivity::onEnter()`**
(`src/activities/settings/ButtonRemapActivity.cpp:20`), modelled on
`ActivityManager.cpp:325-331` (`assert(cond && "message")` for a programming
error, not a runtime condition). It asserts that all four front-button pins are
wired, because that is what this activity's four-role walk
(`kRoleCount = 4`, `ButtonRemapActivity.cpp:13`) actually requires. `<cassert>`
is added explicitly, matching `UiTabListActivity.cpp:5`.

The assert goes here and **not** inside `getPressedFrontButton()` — see A7.

## 5. Data and control flow

### 5a. After the reword

Nothing about the runtime path changes. The setting still reaches the reader by
the same two routes, both unchanged:

```
SETTINGS.longPressButtonBehavior  (persisted byte, key "longPressButtonBehavior")
  ├─ ReaderUtils.h:53 ──────► usePress = (value == OFF)
  │     consumers: ReaderUtils.h:59-69, EndOfBookOptions.cpp:137
  │     (no-op on this board: HalGPIO emits both synthetic edges on one tick,
  │      HalGPIO.cpp:210-225)
  └─ EpubReaderActivity.cpp:613 / ReaderActivity.cpp:146 ──► CHAPTER_SKIP branch
        heldMs source:
          touch turn  → touch.heldMs   ← gpio.lastTouchHeldMs()  (real)   ✅ > 700 possible
          button turn → getHeldTime()  ← SYNTHETIC_HELD_MS = 40  (forced) ❌ never > 700
```

Only the `StrId` the settings row renders changes, and only in `SettingsList.h`.

**Key ordering.** `gen_i18n.py` builds its ordered key list from English
(`gen_i18n.py:121`), so replacing a key in place shifts nothing after it. Even a
shift would be harmless: nothing persists a `StrId`. Settings persist by their
`key` string (`SettingsList.h:353,357`); the language choice persists by code.
Verified by grepping for `static_cast<StrId>` and for `StrId` in
`CrossPointSettings.{h,cpp}` — no hits.

### 5b. After the assert

```
SettingsActivity.cpp:75-78   if (!BoardConfig::hasTouch())  ← proxy gate, unchanged
        └─► SettingsActivity.cpp:309  new ButtonRemapActivity
                └─► onEnter()  assert(all four front pins wired)   ← NEW
                        └─► loop() → getPressedFrontButton()       ← comment only
```

The gate at `SettingsActivity.cpp:75-78` tests `!hasTouch()`, which is a *proxy*
for "this board has front buttons". The assert tests the real requirement. On a
hypothetical touchless board with only two nav keys the proxy passes and the
real requirement fails — today that strands the user in a four-role walk that
can never complete (only Back and Confirm are synthesisable; Left and Right
never arrive), with the only exit the `wasPressed(Button::Down)` cancel at
`ButtonRemapActivity.cpp:59`. The assert turns that into an immediate, named
failure at integration time.

## 6. Error handling

| Condition | Handling | Precedent |
|---|---|---|
| A board reaches `ButtonRemapActivity` without four wired front buttons | `assert(cond && "...")` — a fatal impossible state, per `CLAUDE.md` error-handling case 3 | `ActivityManager.cpp:325-331` |
| A language has no `STR_LONG_PRESS_PAGE_TURN` | English fallback, the generator's existing behaviour — 30 of 32 languages already fall back on 79+ keys (measured: `Deutsch 341 own / 79 fallback`, `Español 387/33`) | `CLAUDE.md`, "missing keys elsewhere fall back to it" |
| A user's saved `longPressButtonBehavior` byte | Untouched — the persisted key string does not change, so no migration and no store write | A6 |
| `getPressedFrontButton()` finds nothing | Unchanged: returns `-1`, and `ButtonRemapActivity.cpp:72-74` returns early | existing |

No new allocation, no new file, no new store, no new task. Flash moves by the
delta between one 20-character English string and one 26-character one plus 31
deleted translations; RAM is unchanged (string tables are `static const` in
flash per `CLAUDE.md`).

## 7. Testing strategy

### 7a. Host test (TDD, red first)

One new `TEST` appended to the existing suite
`test/nav_key_gestures/NavKeyGesturesTest.cpp`, modelled on
`ASynthesisedEventReportsAShortHeldTime` at lines 131-139 — including its habit
of hardcoding `700u` with a `<<` message naming `SKIP_HOLD_MS`, because
`ReaderUtils.h` pulls in the whole UI stack and cannot be included on host
(existing precedent: line 142).

The test encodes the structural claim, not the constant:

- a key released just under `HOLD_MS` yields `NavEvent::Page` **and**
  `reportingSyntheticHeldTime()` — so its held time is `SYNTHETIC_HELD_MS`;
- a key released at or over `HOLD_MS` yields `NavEvent::Synth`, i.e. **no page
  event on either key** — nothing for chapter skip to act on;
- `SYNTHETIC_HELD_MS < 700u`.

Red first by temporarily lowering `HOLD_MS` below 700 (which makes the middle
assertion fail) and confirming the failure before reverting — the only way to
see this one fail, since it pins an invariant that currently holds. The run is
recorded in the plan.

No new `add_subdirectory` line, so **`test/CMakeLists.txt` is not touched** —
the shared-file rule in `.claude/agents/ui-dev.md` does not bite.

### 7b. What cannot be host-tested

`MappedInputManager` and `ButtonRemapActivity` pull in `HalGPIO`,
`GfxRenderer`, `FreeInkUICore`, `CrossPointSettings` and the `Activity`
lifecycle. Neither the comment nor the assert is reachable from the host suite,
and extracting a pin predicate into `lib/` to manufacture one would restructure
a file `.claude/agents/ui-dev.md` explicitly protects. Compensating checks, all
of which the plan will record with output:

- `pio run -e x4pro` — proves the assert and the `<cassert>` include compile,
  and that `StrId::STR_LONG_PRESS_PAGE_TURN` exists after regeneration.
- `grep -rn "STR_LONG_PRESS_BEHAVIOR\b" src lib` returns **zero** hits,
  including comments — `gen_i18n.py:267` scans raw file text with
  `\bSTR_[A-Za-z0-9_]+\b` and does not skip comments, so a retired name left in
  a comment would silently keep the key off the unused-key report.
- `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` reports
  **420 keys, 1 unused** (unchanged totals: one key added, one removed; the
  single expected unused key stays `STR_HIGHLIGHTS_TOO_LARGE`, kept on purpose
  per `70041d90`).
- Host suite green via `cmake -S test -B build && ctest`.
- `./bin/clang-format-fix` over the whole tree, not `-g` (`CLAUDE.md`).

### 7c. Human, on device

1. Settings → Controls shows the new label, and it fits the row without
   stealing the value slot. (The English string is *shorter* — 20 chars vs 26 —
   so this is a check, not a risk.)
2. With the setting on Chapter skip: a long press on an outer tap zone skips;
   a long press on a nav key still gives Back (left) or Confirm (right), and a
   tap still turns the page.
3. Spanish UI shows the new Spanish label; one other language (e.g. German)
   shows the English fallback rather than a blank or a wrong key.
4. Free heap above ~50 KB across reader → settings → reader.

## 8. Assumptions

Each is a behavioural decision, stated so the review can attack it.

**A1. Rewording is the fix; routing a real hold duration is rejected.**
Three pieces of evidence, all in the research note: the reachable window would be
700–850 ms (150 ms wide, bounded above by the Back/Confirm gesture); widening it
means lowering `HOLD_MS` below 700, which `NavKeyGesturesTest.cpp:141-144`
already forbids; and handing a real duration to the `Page` path collides with
`KeyboardEntryActivity.cpp:546,620,652,659`, the one site of nine that branches
on an **Up/Down** hold, inert today only because `HalGPIO::isPressed` suppresses
held nav state past 250 ms (`HalGPIO.cpp:181-182`). *If this is wrong, the whole
of 4a is wrong.*

**A2. "Long-press page turn" is true on every board, not just this one.**
It names the gesture (a held page-turn input) rather than the hardware, so it
stays correct on a board where that input is a physical button. The previous
text was hardware-specific in all 32 languages, which is why the key is retired
rather than re-valued.

**A3. Deleting `STR_LONG_PRESS_BEHAVIOR` from all 32 files is the right shape,
not an English-only value change.** A value change would leave 31 languages
asserting "button" — text that is wrong on this board in every one of them, so
there is no translator work worth preserving (contrast `70041d90`'s
"no translator work is lost"). Deleting a key from all 32 files is routine here:
`70041d90` did it for 22 keys. The 30 languages that fall back join the 79+ keys
they already fall back on.

**A4. Spanish is supplied, the other 30 are not.** Measured coverage makes this
the repo's norm, not a shortfall: English 420/420, Español 387/420, every other
language ≤ 342. `02d9106a` likewise touched English and Spanish only.

**A5. The three option keys keep their `_BEHAVIOR_` names.** Renaming them costs
96 file edits and 96 discarded translations of text that is correct, for no
user-visible change. The parent/child name asymmetry is documented in the PR
body rather than in a source comment, because a comment naming
`STR_LONG_PRESS_BEHAVIOR` would re-register the retired key as "used"
(`gen_i18n.py:267`).

**A6. The persisted key `"longPressButtonBehavior"` and the member
`CrossPointSettings::longPressButtonBehavior` are not renamed.** The string at
`SettingsList.h:353,357` is the JSON key in the settings store; renaming it would
silently reset every existing user's choice to `OFF`
(`CrossPointSettings.h:295`). The label is a presentation concern; the key is
data.

**A7. The assert goes in `ButtonRemapActivity::onEnter()`, not in
`getPressedFrontButton()`.** An assert at the top of the function would fire on
this board for a *future* "press any front button" flow that would in fact work,
since Back and Confirm are synthesisable — a false alarm, and with no `NDEBUG`
in either env (`platformio.ini:160,178` — `NDEBUG` appears nowhere in the build flags) that means `abort()` on a shipped
device. The remap activity's requirement is unambiguous: four distinct,
physically distinguishable buttons, per its four-role walk. The comment, not the
assert, carries the reachability knowledge to a future caller.

**A8. The assert tests all four pins, not just `left`/`right`.** The activity
needs four distinct sources; a board with Back and Confirm synthesised from the
same two nav keys it would also need for Left and Right cannot satisfy it.

**A9. A runtime `assert` is preferred over `LOG_ERR` + `finish()`.** This is a
board-configuration error caught at integration time, not a runtime condition —
`CLAUDE.md` case 3, and the shape `ActivityManager.cpp:325-331` already uses. A
`static_assert` is not available: `BoardConfig::ACTIVE` is
`inline BoardProfile ACTIVE` (`BoardConfig.h:1532`), deliberately mutable so
`selectDevice()` can swap it, so it is not a constant expression. (`constexpr
DEFAULT_DEVICE` at `BoardConfig.h:1520` *is*, but a `static_assert` over it would
fail this build outright rather than guard a caller.)

**A10. Two commits, one PR.** The findings share no code. Commit 1 is the
reword plus its host test; commit 2 is the comment plus the assert. A reviewer
can take either alone.

## 9. Files touched

| File | Change |
|---|---|
| `lib/I18n/translations/english.yaml` | replace one key |
| `lib/I18n/translations/spanish.yaml` | replace one key |
| `lib/I18n/translations/*.yaml` (30 others) | delete one key |
| `src/SettingsList.h` | `StrId` at lines 350 and 355 |
| `test/nav_key_gestures/NavKeyGesturesTest.cpp` | one new `TEST` |
| `src/MappedInputManager.cpp` | block comment above line 378 |
| `src/activities/settings/ButtonRemapActivity.cpp` | `#include <cassert>` + one assert in `onEnter()` |

Not touched: `test/CMakeLists.txt`, `src/main.cpp`, `lib/hal/`, `lib/Input/`,
`src/CrossPointSettings.{h,cpp}`, `freeink-sdk/`.

`src/MappedInputManager.cpp` sits outside the `ui` surface as
`.claude/agents/ui-dev.md` defines it, and that file forbids touching the input
layer "without an explicit instruction". Issue #60 names
`MappedInputManager.cpp:377-393` directly; the change is one comment and adds no
behaviour. `lib/I18n/translations/*.yaml` is listed there as report-do-not-edit;
the task's batch instruction supersedes it, the #31 purge having landed as
`70041d90`.
