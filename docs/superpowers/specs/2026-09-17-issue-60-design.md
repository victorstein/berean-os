# Issue #60 — two input-layer findings: design

Date: 2026-09-17 · Branch: `fix/60-input-layer-traps`
Research: `docs/superpowers/research/2026-09-17-issue-60-research.md`
Reviews answered: `reviews/issue-60-spec-review-0.md` (BLOCKER 1, MAJOR 3, MINOR 3),
`reviews/issue-60-spec-review-1.md` (BLOCKER 1, MAJOR 1, MINOR 2)

Issue #60 carries two independent findings that share a file neighbourhood and
nothing else. They ship in one PR as two commits.

---

## 0. The ratified decision, and what the reviews changed

### The scope call is settled

**Finding 1 removes the setting. Ratified by the human, via the orchestrator, on
2026-09-17.** Issue #60 offered two remedies — "either reword it, or route the
synthesised path so a held nav key reports its real duration" — and research
disproved both, then disproved the reword's premise as well: chapter skip is
unreachable by any deliberate input on this board (§1a). Removing a shipped
user-facing setting is more than the issue authorises, so it went to the human
and the human chose the gate.

The two alternatives are **rejected and are not fallbacks**:

- *Reword the label.* There is no truthful wording for a control that does
  nothing, and the change was mispriced in an earlier pass as "one line": all 32
  YAMLs carry a translated value for `STR_LONG_PRESS_BEHAVIOR`
  (`spanish.yaml:89` "Al mantener pulsado un botón", `german.yaml:71` "Verhalten
  bei langem Tastendruck"), so an English-only edit leaves 31 of 32 languages
  still naming buttons.
- *Keep the row, document it as dead.* Ships an inert control on a screen the
  user has to scroll.

The evidence the human verified independently before ratifying:
`TOUCH_LONG_PRESS_MS = 500`
(`freeink-sdk/libs/hardware/InputManager/include/InputManager.h:399`) fires
before `SKIP_HOLD_MS = 700` (`ReaderUtils.h:18`);
`EpubReaderActivity.cpp:454` consumes the long press and
`MappedInputManager.cpp:160-161` then calls `suppressTouchContact()`;
`touchReaderControls` defaults to `TOUCH_READER_ON`
(`CrossPointSettings.h:325`), so the consuming gesture is on out of the box.

### Findings applied

| Review | Finding | Change |
|---|---|---|
| 0 | BLOCKER 1 — touch cannot reach chapter skip | §4a became a capability gate, not a reword |
| 0 | MAJOR 2 — `ReaderActivity.cpp:144-146` is dead code | Struck from §5a and from the research note |
| 0 | MAJOR 3 — the red-first step could not redden | §7a rebuilt on an absolute assertion. A8 |
| 0 | MAJOR 4 — an `assert` aborts a shipped device | `LOG_ERR` + `finish()`. A9 |
| 0 | MINOR 5 — the `gen_i18n.py` failure mode was inverted | Corrected in §7b and in the research note |
| 0 | MINOR 6 — label neighbour | Moot; no label is introduced |
| 1 | BLOCKER 1 — the scope call was unratified | Discharged by fact: the human ruled. See above |
| 1 | **MAJOR 2 — gating is not persistence-neutral** | **§5a, §6 and A6 rewritten to state the real consequence. See below** |
| 1 | MINOR 3 — three new off-by-N citations | Corrected: `EpubReaderActivity.cpp:451-459`, `SettingsList.h:347`, `EpubReaderMenuActivity.cpp:73-75`, `NavKeyGestures.cpp:29` |
| 1 | MINOR 4 — research note half-corrected | Finished in this spec's commit; the stale §9 row is dropped |

### The correction that matters: gating removes the key from the settings file

An earlier pass claimed "the runtime path is untouched" and "no migration, no
write". **That was wrong, and it asserted the opposite of the one consequence the
template commit wrote down.** `getSettingsList()` *is* the persistence schema:
`toJson` (`CrossPointSettings.cpp:63`, loop at `:66`) and `fromJson`
(loop at `:113`) both iterate it, and `"longPressButtonBehavior"` has no manual line beside the keys
that do (`:84-100`). So gating the entry:

1. stops `fromJson` reading the key — the member keeps its initializer default
   `OFF` (`CrossPointSettings.h:295`), which makes `usePress` **true**, not
   false;
2. stops `toJson` writing it — the key is **deleted from the settings JSON on
   the next save**, and entering the settings screen is itself such a save
   (`SettingsActivity.cpp:305`);
3. removes the row from the web settings API, which enumerates the same list
   (`CrossPointWebServer.cpp:1159,1263`).

This is accepted, not worked around. `d8e92208` did exactly this on purpose for
`orientation` and said so in its commit body ("**`orientation` leaves the
persistence schema.** `getSettingsList()` is what `toJson`/`fromJson` iterate
… so gating the row drops the key. That is intended"), and `SettingsList.h:452-472`
already un-persists `fadingFix`, `frontButtonFollowOrientation` and
`backShortToFileBrowser` the same way at runtime. It is benign here for the same
reason the setting is being removed: no input can reach the feature, and
press-vs-release is the same event for every button that exists (§5a).

---

## 1. Problem

### 1a. A Controls setting that no input on this board can reach

`src/SettingsList.h:349-358` offers **Long-press button behavior**, with Off /
Chapter skip (the third option, Orientation change, is already compiled out here
by the `#if BEREAN_CAP_ROTATION` that landed with `d8e92208`).

Chapter skip fires when a page turn arrives with a held time above
`SKIP_HOLD_MS = 700` (`src/activities/reader/ReaderUtils.h:18`), tested at
`src/activities/reader/EpubReaderActivity.cpp:605-606`. Nothing can deliver one.

**Buttons — structurally impossible.** `NavKeyGestures::updateKey`
(`lib/Input/Input/NavKeyGestures.cpp:12-32`; the resolve is the single line
`:29`) decides on **release**, into one of two outcomes:

- held < `HOLD_MS` (850, `NavKeyGestures.h:41`) → `NavEvent::Page`, emitted as
  `BTN_UP`/`BTN_DOWN` (`lib/hal/HalGPIO.cpp:198-201`) — but
  `reportingSyntheticHeldTime()` is true for `Page` as well as `Synth`
  (`NavKeyGestures.cpp:60-62`), so `HalGPIO::getHeldTime()` substitutes
  `SYNTHETIC_HELD_MS = 40` (`HalGPIO.cpp:236-239`, `NavKeyGestures.h:68`);
- held ≥ 850 → `NavEvent::Synth`, emitted as `BTN_BACK`/`BTN_CONFIRM`
  (`HalGPIO.cpp:194-197`) — **no page turn at all.**

There is no hold duration that both pages and reports over 700 ms.

**Touch — unreachable in all three modes.** `detectTouchPageTurn`
(`ReaderUtils.h:78-121`) branches on `SETTINGS.touchReaderControls`
(`CrossPointSettings.h:209-214`, default `TOUCH_READER_ON` at `:325`):

| Mode | Behaviour | Can `heldMs > 700` reach a page turn? |
|---|---|---|
| `TOUCH_READER_OFF` | early return, `{false,false,0}` (`ReaderUtils.h:79-81`) | No — no touch page turns exist |
| `TOUCH_READER_SWIPE` | the swipe path returns before `heldMs` is ever assigned, leaving it **0** (`ReaderUtils.h:84-94`) | No — and the code says so: *"A slow swipe never becomes a long-press chapter skip."* |
| `TOUCH_READER_ON` / `_INVERTED_TAP` | tap zones, `heldMs = gpio.lastTouchHeldMs()` (`ReaderUtils.h:120`) | No — see below |

In the tap modes the contact is consumed first. `EpubReaderActivity::loop()`
runs this **ahead of all page-turn handling**, at
`EpubReaderActivity.cpp:451-459`:

```cpp
if (SETTINGS.touchReaderControls && mappedInput.wasScreenLongPress(longPressX, longPressY) &&
    !ReaderUtils::isInMenuZone(renderer, longPressX, longPressY)) {   // :454-455
  openHighlightPassageAt(longPressX, longPressY);
  return;
}
```

`isInMenuZone` is the centre third in **both** axes (`ReaderUtils.h:131-137`),
so both page-turn zones — the outer horizontal thirds, full height
(`ReaderUtils.h:108-112`) — lie entirely outside it. The event fires while the
finger is still down, at `TOUCH_LONG_PRESS_MS = 500`
(`freeink-sdk/libs/hardware/InputManager/include/InputManager.h:399`), and
`wasScreenLongPress` (`src/MappedInputManager.cpp:155-164`) calls
`gpio.suppressTouchContact()` at `:161`, so the lift produces no tap either.
Every surviving tap therefore reports `heldMs < 500 < 700`. The gate is the same
`SETTINGS.touchReaderControls` truthiness that enables the tap zones, so the two
cannot be separated.

**The setting's one remaining effect is inert.** `ReaderUtils.h:53` derives
`usePress = (longPressButtonBehavior == OFF)`, consumed at `ReaderUtils.h:59-69`
and `EndOfBookOptions.cpp:137-140`. Both read only `PageBack`/`PageForward`
(→ `BTN_UP`/`BTN_DOWN`, whose synthetic press and release land on the same tick,
`HalGPIO.cpp:210-225`), `Button::Left`/`Right` (→ `SETTINGS.frontButtonLeft/Right`
= `FRONT_HW_LEFT/RIGHT` = `BTN_LEFT`/`BTN_RIGHT`, dead pins) and
`wasReleased(Power)`, which sits outside the `usePress` ternary
(`ReaderUtils.h:62-66`). `EndOfBookOptions` uses `NavPrevious`/`NavNext`, which
compose to the same four buttons (`MappedInputManager.cpp:98-107`). Press and
release select between two identical results.

**`Long-press button behavior` is not a mislabelled working feature. It is an
inert settings row.**

### 1b. `getPressedFrontButton()` has two arms nothing can fire

`src/MappedInputManager.cpp:378-393` scans `BTN_BACK`, `BTN_CONFIRM`,
`BTN_LEFT`, `BTN_RIGHT`. Back and Confirm are reachable here, synthesised from
an 850 ms nav-key hold. Left and Right fall through to `inputMgr.wasPressed()`
(`HalGPIO.cpp:215`), whose bits come from
`isDigitalPressed(BoardConfig::ACTIVE.input.left / .right)`
(`InputManager.cpp:257-258`), and `isDigitalPressed` is
`pin >= 0 && digitalRead(pin) == LOW` (`InputManager.cpp:246`). The X4 Pro
profile leaves all four at `PIN_UNASSIGNED` (`BoardConfig.h:1396`,
`PIN_UNASSIGNED = -1` at `BoardConfig.h:406`), and `HalGPIO::synthesisedEdge` has
no `BTN_LEFT`/`BTN_RIGHT` case (`HalGPIO.cpp:186-205`). The two arms are dead.

Dormant today: one call site (`ButtonRemapActivity.cpp:71`), one construction
(`SettingsActivity.cpp:308-310`), reached only from the entry appended inside
`if (!BoardConfig::hasTouch())` at `SettingsActivity.cpp:75-78`, and this board
declares a GT911 (`BoardConfig.h:1411`, predicate at `BoardConfig.h:1622`).

## 2. Goal

1. The Controls list stops offering a row that cannot do anything on this board,
   and a board that *can* reach it still gets the row.
2. A future reader of `getPressedFrontButton()` cannot mistake its four arms for
   four working sources; a board that reaches the remap flow without four wired
   front buttons refuses loudly instead of presenting a walk it cannot complete.
3. A host test pins, behaviourally, the constant relationship that makes goal 1's
   button half true — so the gate is revisited if the input layer moves.

## 3. Non-goals

- **Making chapter skip reachable.** Both routes rejected on evidence: A1
  (buttons) and A2 (touch).
- **Rewording the label, or keeping the row with a note.** Rejected by the
  ratified decision; see §0.
- **Removing the reader's chapter-skip branches or the `usePress` derivation.**
  `d8e92208` gated its option while leaving `EpubReaderActivity.cpp:619`'s
  `ORIENTATION_CHANGE` branch in place — still there today. See A4. (The
  *persisted key* does go away; that is an accepted consequence, §0 and A6, not
  a non-goal.)
- **Touching `lib/I18n/translations/`.** See A5.
- **Touching `NavKeyGestures`, `HalGPIO` or the synthesis.** Phase 2 owns the
  input layer (`.claude/agents/ui-dev.md`); this change only documents it.
- **Changing the passage-selection gesture** at `EpubReaderActivity.cpp:451-459`.
  See A2.
- **A settings migration.** See A6.
- **`ButtonNavigator.cpp:70`** — continuous list scrolling is dead on the nav
  keys for a related suppression (`HalGPIO.cpp:181-182`,
  `HOLD_STATE_SUPPRESS_MS = 250`). Pre-existing, out of scope.
- **Device verification.** Flagged for the human in the PR body.

## 4. Architecture

### 4a. Gate the setting behind a capability (commit 1)

Modelled on **`d8e92208` — "fix: gate screen rotation behind a portrait-only
capability (#45)"**, which closed #37: a `BEREAN_CAP_*` macro in
`src/CrossPointSettings.h:17-23` with an `#error` for an unhandled device, an
`#if` around the option in `src/SettingsList.h`, and one `#if` around the
matching reader-menu row (`EpubReaderMenuActivity.cpp:73-75`). Three source
files, no translations, no test.

| Change | File |
|---|---|
| `BEREAN_CAP_LONG_PRESS_PAGE_TURN` — `0` under `FREEINK_DEVICE_X4PRO`, `#error` otherwise | `src/CrossPointSettings.h`, beside `BEREAN_CAP_ROTATION` at `:17-23` |
| Wrap the whole `SettingInfo::Enum` entry (both arms of the existing `#if BEREAN_CAP_ROTATION`) in the new `#if` | `src/SettingsList.h:349-358` |

Wrapping the whole entry is structurally safe: the list is a
`std::vector<SettingInfo>` initializer (`SettingsList.h:254`), not a fixed-size
array, so removing an element shifts nothing.

The existing `#if BEREAN_CAP_ROTATION` split stays **nested inside** the new
gate, not collapsed: on a future board where the long-press page turn is real,
whether it also offers orientation change is still a separate question.

Nothing else moves. The four `STR_LONG_PRESS_BEHAVIOR*` keys stay in all 32
YAMLs and stay "used" as far as `gen_i18n.py:267` is concerned — it is a raw-text
regex, not a preprocessor — exactly as `STR_LONG_PRESS_BEHAVIOR_ORIENTATION` has
since `d8e92208`.

### 4b. Trap-proof the front-button scan (commit 2)

Two mechanisms, for two different hazards:

**A block comment above `getPressedFrontButton()`**
(`src/MappedInputManager.cpp:378`), modelled on the explanatory block above
`HalGPIO::isPressed` (`lib/hal/HalGPIO.cpp:163-177`) — same house style: state
what the function can and cannot answer, name the board fact, cite the file that
proves it. It records that Back and Confirm arrive by synthesis from a nav-key
hold, that Left and Right have no source when `input.left`/`input.right` are
`PIN_UNASSIGNED`, and that `HalGPIO::synthesisedEdge` supplies no substitute.

**`LOG_ERR` + `finish()` in `ButtonRemapActivity::onEnter()`**
(`src/activities/settings/ButtonRemapActivity.cpp:20`), modelled on
`ReaderActivity::onEnter` at `src/activities/reader/ReaderActivity.cpp:44-47` —
the repo's exact shape for "this activity cannot do its job, log and pop":

```cpp
if (!Storage.exists(bookPath.c_str())) {
  LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
  finish();
  return;
}
```

`finish()` from `onEnter()` is supported — it calls `activityManager.popActivity()`
(`Activity.cpp:24`), and `ActivityManager.cpp:161-165` tolerates a pending action
raised there. It refuses when the board does not have four wired front-button
pins, which is what this activity's four-role walk requires (`kRoleCount = 4`,
`ButtonRemapActivity.cpp:13`). `#include <Logging.h>` is added — the file has no
logging include today.

**Not an `assert`, and not inside `getPressedFrontButton()`** — see A9 and A10.

**For the PR description.** Say why `LOG_ERR` + `finish()` rather than `assert`
is more than a style choice: `grep -n NDEBUG platformio.ini` is empty, so
`assert` is live in the release env too and a failing one would `abort()` a
shipped device for a screen the user reached from a settings row. The earlier
draft of this design specified an `assert`; the reviews caught it.

## 5. Data and control flow

### 5a. After the gate

The reader's code path does not change. What changes is that the settings row
disappears and, with it, the key's place in the persistence schema:

```
Settings → Controls row                    REMOVED on this board (#if)
  │
  └─ getSettingsList() is the schema:
       toJson   (CrossPointSettings.cpp:66)   → key no longer written
       fromJson (CrossPointSettings.cpp:113)  → key no longer read
       web API  (CrossPointWebServer.cpp:1159,1263) → row no longer listed

SETTINGS.longPressButtonBehavior   → pinned at its initializer default OFF
                                     (CrossPointSettings.h:295)
  ├─ ReaderUtils.h:53 ──► usePress = (value == OFF) = TRUE, press-triggered
  │     consumers: ReaderUtils.h:59-69, EndOfBookOptions.cpp:137-140
  │     no observable change: both edges of every reachable button land on one
  │     tick (HalGPIO.cpp:210-225); Left/Right are dead pins; Power is outside
  │     the usePress ternary (ReaderUtils.h:62-66)
  │
  └─ EpubReaderActivity.cpp:613 ──► CHAPTER_SKIP branch, now unconditionally
        dead rather than merely unreachable — kept, per A4 and d8e92208
```

**A saved `CHAPTER_SKIP` byte stops being honoured and is then dropped.** A
settings file written by an earlier build keeps the key on disk until the next
save; `fromJson` ignores it from the first boot after the upgrade, and the next
`toJson` — entering the settings screen is one (`SettingsActivity.cpp:305`) —
writes the file without it. This is the `d8e92208` behaviour for `orientation`,
quoted in §0, and it is benign here because the two values are
indistinguishable at runtime on this board.

`ReaderActivity.cpp:144-146` is **not** a second consumer.
`ReaderActivity::loop()` (`ReaderActivity.cpp:131`) is dead:
`EpubReaderActivity` is the only subclass (`EpubReaderActivity.h:19`), its
`loop()` override (`EpubReaderActivity.cpp:368`) never chains to the base, and
`grep -rn "ReaderActivity::loop" src lib` returns only the two definitions.

### 5b. After the refusal

```
SettingsActivity.cpp:75-78   if (!BoardConfig::hasTouch())   ← proxy gate, unchanged
        └─► SettingsActivity.cpp:308-310   new ButtonRemapActivity
                └─► onEnter()  four front pins wired? no → LOG_ERR + finish()   ← NEW
                        └─► loop() → getPressedFrontButton()   ← comment only
```

The gate at `SettingsActivity.cpp:75-78` tests `!hasTouch()`, a *proxy* for "this
board has front buttons". The new check tests the real requirement. On a
hypothetical touchless board with only two nav keys the proxy passes and the real
requirement fails; today that shows a four-role walk that can never complete,
because only Back and Confirm are synthesisable and Left and Right never arrive.

The screen is **degraded, not a trap**: there are two working exits,
`ButtonRemapActivity.cpp:48-57` (`Button::Up` — restore defaults, save, finish)
and `:59-63` (`Button::Down` — cancel), both on `BTN_UP`/`BTN_DOWN`, the keys
that do produce events. That is precisely why the refusal is a log-and-pop and
not a panic (A9).

## 6. Error handling

| Condition | Handling | Precedent |
|---|---|---|
| A board reaches `ButtonRemapActivity` without four wired front buttons | `LOG_ERR("REMAP", …)` + `finish()` in `onEnter()` — `CLAUDE.md` error-handling case 2 | `ReaderActivity.cpp:44-47` |
| A user's saved `longPressButtonBehavior` byte | Ignored on read, dropped on the next save. Accepted, not migrated — §0, A6 | `d8e92208` for `orientation`; `SettingsList.h:452-472` |
| `getPressedFrontButton()` finds nothing | Unchanged: returns `-1`; `ButtonRemapActivity.cpp:72-74` returns early | existing |
| A future board sets no `BEREAN_CAP_LONG_PRESS_PAGE_TURN` | `#error` at compile time, forcing an explicit answer | `CrossPointSettings.h:17-23` |

No new allocation, no new file, no new store, no new task, no string-table
change. The settings JSON gets one key shorter, so `CrossPointSettings`'s
serialised size falls — it is budget-checked at `CrossPointSettings.cpp:374`
(`saveBudget() == 4096`) and moves the safe way.

## 7. Testing strategy

### 7a. Host test (TDD, red first)

One new `TEST` appended to `test/nav_key_gestures/NavKeyGesturesTest.cpp`. It
follows that file's established habit of hardcoding the reader's threshold with a
`<<` message naming it (`:142`), because `ReaderUtils.h` pulls in the whole UI
stack and cannot be included on host.

The assertion is **absolute**: a key pressed and released at `SKIP_HOLD_MS + 1`
(701 ms — the shortest hold a user could aim at to trigger chapter skip) must
still resolve to `NavEvent::Page` **and** report `reportingSyntheticHeldTime()`.
The one duration that would work reports 40, not 701.

Red first: lowering `HOLD_MS` to 700 makes 701 cross into `Synth`, so the new
test fails on its own `EXPECT_EQ`. An earlier pass proposed assertions expressed
*relative* to `HOLD_MS`, which could not redden — lowering `HOLD_MS` would have
failed the pre-existing `ThresholdsSitBetweenTheReadersOwnHolds` (`:141-144`)
instead and the implementer would have banked someone else's failure. The red run
is recorded in the plan.

Acknowledged overlap: `:141-144` also reddens when `HOLD_MS ≤ 700`. The new test
earns its place by driving the state machine at that exact duration rather than
comparing two constants — it fails with "701 ms resolved to Synth", naming the
user-visible consequence. Two assertions an earlier pass proposed are **dropped**
as duplicates: `:44-55` (`AHoldNeverAlsoProducesAPage`) already covers "a hold
produces no page event", and `:132-139` already bounds `SYNTHETIC_HELD_MS` more
strictly (`< 400u`).

No new `add_subdirectory`, so **`test/CMakeLists.txt` is not touched** — the
shared-file rule in `.claude/agents/ui-dev.md` does not bite.

### 7b. What cannot be host-tested

The touch half of §1a and all of §4b pull in `HalGPIO`, `GfxRenderer`,
`FreeInkUICore`, `CrossPointSettings` and the `Activity` lifecycle. Extracting a
predicate into `lib/` to manufacture a test would restructure a file
`.claude/agents/ui-dev.md` explicitly protects. Compensating checks, each
recorded in the plan with its output:

- `pio run -e x4pro` — proves the new macro, the `#if`, the `Logging.h` include
  and the `finish()` call compile, and that the `#error` arm is not taken.
- `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` still reports
  **420 keys, 1 unused** — unchanged, because no key is added or removed and the
  four gated names stay visible to the raw-text scan at `:267`. A `STR_*` used in
  source but absent from `english.yaml` does **not** silently skew that report:
  it prints `CRITICAL: … used in source but missing from english.yaml` and
  `sys.exit(1)` (`gen_i18n.py:873-881`), failing `pio run` at the `pre:` step
  (`platformio.ini:131`). That is why this design keeps the keys.
- `scripts/settings_snapshot.py` is a regex over `SettingsList.h` text
  (`:11-12`), so it will **not** show this diff — gated `StrId::` tokens still
  match. The persistence change has to be verified on device (§7c.2), which is
  why `d8e92208` stated it in prose rather than relying on a tool.
- Host suite green via `cmake -S test -B build && ctest`.
- `./bin/clang-format-fix` over the whole tree, not `-g` (`CLAUDE.md`).

### 7c. Human, on device

1. Settings → Controls no longer lists **Long-press button behavior**; the rows
   above and below it (`STR_FRONT_BTN_FOLLOW_ORIENTATION` at
   `SettingsList.h:347` and the long-press menu at `:359`) render with no gap or
   stale selection.
2. **The persistence change, which no host check can see:** on a unit whose
   settings file already holds `longPressButtonBehavior`, change any setting,
   then inspect `/.crosspoint/`'s settings JSON — the key is gone. Paging still
   works by tap and by nav key, and no chapter skip occurs.
3. A long press in an outer tap zone still opens passage selection — the gesture
   this change refuses to disturb.
4. Free heap above ~50 KB across reader → settings → reader.

## 8. Assumptions

**A1 — RATIFIED. The setting is removed, not reworded.** Decided by the human via
the orchestrator on 2026-09-17, on independently verified evidence (§0). The two
alternatives are rejected, not deferred. *If A2 is wrong, this is wrong.*

**A2 — Touch cannot reach chapter skip in any mode, so "gate on `hasTouch()`" is
not an option.** Verified per mode in §1a. The load-bearing evidence is
`ReaderUtils.h:84-94` (swipe mode never assigns `heldMs`, and says so) and
`EpubReaderActivity.cpp:451-459` + `TOUCH_LONG_PRESS_MS = 500`
(`InputManager.h:399`) + `suppressTouchContact()` (`MappedInputManager.cpp:161`)
for the tap modes.

**A3 — Routing a real hold duration to the nav path is rejected.** Even granting
it, the reachable window is 700–850 ms; widening it means lowering `HOLD_MS`
below 700, which `NavKeyGesturesTest.cpp:141-144` already forbids; and handing a
real duration to the `Page` path collides with
`KeyboardEntryActivity.cpp:546,620,652,659`, the one site of nine branching on an
**Up/Down** hold, inert today only because `HalGPIO::isPressed` suppresses held
nav state past 250 ms (`HalGPIO.cpp:181-182`).

**A4 — The reader's `CHAPTER_SKIP` branch and the `usePress` derivation stay.**
`d8e92208` gated the orientation option while leaving
`EpubReaderActivity.cpp:619`'s `ORIENTATION_CHANGE` branch in the reader; it is
still there. Mirroring that keeps this change to two files and keeps the code
correct for a board that sets the capability to 1.

**A5 — No translation file is touched.** The gated `StrId`s remain in
`SettingsList.h` source text, so `gen_i18n.py:267` still counts them as used and
removing them from `english.yaml` would fail the build (`:873-881`). This is
`d8e92208`'s behaviour for `STR_LONG_PRESS_BEHAVIOR_ORIENTATION`, unchanged since.

**A6 — The key leaves the persistence schema, and that is accepted without a
migration.** Mechanism and precedent in §0. No migration is written because the
two values are indistinguishable on this board (§5a), so there is nothing to
preserve and a migration would cost a store write for no observable change. A
future board that sets the capability to 1 gets the key back and reads whatever
is on disk, defaulting to `OFF` if absent — the same behaviour any new key has.

**A7 — `BEREAN_CAP_LONG_PRESS_PAGE_TURN` is a new capability macro, not a reuse
of `BEREAN_CAP_ROTATION`.** They answer different questions: one is "does this
panel rotate", the other "can any input here deliver a held page turn". A board
could answer them differently, and `d8e92208`'s `#error` idiom exists precisely
to force each board to answer for itself.

**A8 — The host test asserts an absolute duration, not a relative one.** Only an
absolute assertion can go red for its own reason; see §7a. The overlap with
`:141-144` is accepted and stated rather than engineered away.

**A9 — `LOG_ERR` + `finish()`, not `assert`.** The state is reached by a user
selecting a settings row (`SettingsActivity.cpp:75-78`, `:308-310`), not by an
impossible program state, and `grep -n NDEBUG platformio.ini` is empty — the flag
blocks are `:164-176` and `:182-190` — so an `assert` would `abort()` a shipped
device. `CLAUDE.md` reserves `assert(false)` for fatal impossible states and puts
this in case 2.

**A10 — The refusal goes in `ButtonRemapActivity::onEnter()`, not in
`getPressedFrontButton()`.** A guard inside the function would also fire for a
future "press any front button" flow that would in fact work here, since Back and
Confirm are synthesisable — a false refusal. The remap activity's requirement is
unambiguous: four distinct, physically distinguishable buttons. The comment, not
the refusal, carries the reachability knowledge to a future caller.

**A11 — The refusal tests all four pins, not just `left`/`right`.** The activity
needs four distinct sources; a board synthesising Back and Confirm from the same
two nav keys it would also need for Left and Right cannot satisfy it. No board
profile in `BoardConfig.h` today has `left`/`right` wired while `back`/`confirm`
are unassigned, so the four-pin predicate produces no false refusal on any
existing board.

**A12 — Two commits, one PR.** The findings share no code. Commit 1 is the
capability gate plus its host test; commit 2 is the comment plus the refusal. A
reviewer can take either alone.

## 9. Files touched

| File | Change |
|---|---|
| `src/CrossPointSettings.h` | new `BEREAN_CAP_LONG_PRESS_PAGE_TURN` block beside `:17-23` |
| `src/SettingsList.h` | `#if` around `:349-358` |
| `test/nav_key_gestures/NavKeyGesturesTest.cpp` | one new `TEST` |
| `src/MappedInputManager.cpp` | block comment above `:378` |
| `src/activities/settings/ButtonRemapActivity.cpp` | `#include <Logging.h>` + refusal in `onEnter()` |

Not touched: `lib/I18n/translations/`, `test/CMakeLists.txt`, `src/main.cpp`,
`lib/hal/`, `lib/Input/`, `src/activities/reader/`, `freeink-sdk/`. The research
note's corrections landed with this spec's own commits and need no further edit.

**Surfaces.** `src/MappedInputManager.cpp` sits outside the `ui` surface as
`.claude/agents/ui-dev.md` defines it, and that file forbids touching the input
layer "without an explicit instruction"; issue #60 names
`MappedInputManager.cpp:377-393` directly, and the change is one comment that
adds no behaviour. Separately, because gating the entry changes what
`toJson`/`fromJson` persist (§0), `src/CrossPointSettings.h` and
`src/SettingsList.h` are **`data-dev`'s surface as well as `ui`'s** — the PR
should say so, and `d8e92208` is the precedent for a `ui` change making exactly
this edit.
