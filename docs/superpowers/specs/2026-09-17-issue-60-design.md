# Issue #60 — two input-layer findings: design (pass 1)

Date: 2026-09-17 · Branch: `fix/60-input-layer-traps`
Research: `docs/superpowers/research/2026-09-17-issue-60-research.md`
Review answered: `docs/superpowers/reviews/issue-60-spec-review-0.md` (BLOCKER: 1, MAJOR: 3, MINOR: 3)

Issue #60 carries two independent findings that share a file neighbourhood and
nothing else. They ship in one PR as two commits.

---

## 0. What changed from pass 0, and why

Pass 0 proposed **rewording** the Controls setting to name touch instead of
buttons. Review 0's BLOCKER disproved the premise. Re-verified against source,
and the picture is worse than the review states:

| # | Finding | Accepted | Change |
|---|---|---|---|
| BLOCKER 1 | "Touch can and does" reach chapter skip is false | Yes | §4a is now a **capability gate that removes the setting**, not a reword. See A1. |
| MAJOR 2 | `ReaderActivity.cpp:144-146` is dead code | Yes | Citations corrected here and in the research note (`git show` on the same commit as this spec). |
| MAJOR 3 | §7a's red-first step cannot fail the new test | Yes | §7a rewritten around an **absolute** assertion at `SKIP_HOLD_MS + 1`. See A8. |
| MAJOR 4 | The assert's "only exit" claim is wrong, and it aborts a shipped device | Yes | Assert replaced with `LOG_ERR` + `finish()`; the wrong claim is struck. A9 reversed. |
| MINOR 5 | §7b/A5 invert the `gen_i18n.py` failure mode | Yes | Corrected in §7b. The whole i18n section shrank to nothing — see below. |
| MINOR 6 | "Long-press Menu" is the label's neighbour | Moot | No label is introduced any more. |
| MINOR 7 | Citation drift (three) | Yes | Corrected throughout. |

**The reword is gone, and with it eight of pass 0's assumptions.** A2–A5
concerned the new translation string, the deletion of `STR_LONG_PRESS_BEHAVIOR`
from 32 YAMLs, and which languages get a translation. None survives: the gate
mirrors `d8e92208` (#45), which left its gated option's `StrId` *inside* the
`#if` and therefore touched **no translation file at all**. `gen_i18n.py:267`
scans raw file text, not preprocessed output, so a name inside a disabled `#if`
still counts as used — deleting those keys would now trigger the
`CRITICAL: … used in source but missing from english.yaml` exit at
`gen_i18n.py:873-881`. **`lib/I18n/translations/` is not touched by this change.**

**One unratified scope call.** The BLOCKER's fix is a scope decision; it was
raised via `hpipe decide` and the call was misrouted onto a different run's task
(`berean-os-20260916-…-ujku` t4, a completed `#38` task) rather than this one, so
no human answered it before this phase re-ran. This spec therefore proceeds on
the recommendation as written, and **A1 is flagged as unratified**: it removes a
user-facing setting, which is more than issue #60 asked for. If the human rejects
it, §4a reverts to "reword and document the feature as dead" (A1, option 3) and
§4b is unaffected.

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
(`lib/Input/Input/NavKeyGestures.cpp:12-31`, the resolve is line 28) decides on
**release**, into one of two outcomes:

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
(`CrossPointSettings.h:209-214`):

| Mode | Behaviour | Can `heldMs > 700` reach a page turn? |
|---|---|---|
| `TOUCH_READER_OFF` | early return, `{false,false,0}` (`ReaderUtils.h:79-81`) | No — no touch page turns exist |
| `TOUCH_READER_SWIPE` | swipe path returns before `heldMs` is ever assigned, leaving it **0** (`ReaderUtils.h:84-93`) | No — and the code says so: *"A slow swipe never becomes a long-press chapter skip."* |
| `TOUCH_READER_ON` / `_INVERTED_TAP` | tap zones, `heldMs = gpio.lastTouchHeldMs()` (`ReaderUtils.h:120`) | No — see below |

In the tap modes the contact is consumed first. `EpubReaderActivity::loop()`
runs this **ahead of all page-turn handling**, at `EpubReaderActivity.cpp:450-457`:

```cpp
if (SETTINGS.touchReaderControls && mappedInput.wasScreenLongPress(longPressX, longPressY) &&
    !ReaderUtils::isInMenuZone(renderer, longPressX, longPressY)) {
  openHighlightPassageAt(longPressX, longPressY);
  return;
}
```

`isInMenuZone` is the centre third in **both** axes (`ReaderUtils.h:131-137`),
so both page-turn zones — the outer horizontal thirds, full height
(`ReaderUtils.h:108-112`) — lie entirely outside it. The event fires while the
finger is still down, at `TOUCH_LONG_PRESS_MS = 500`
(`freeink-sdk/.../InputManager.h:399`), and `wasScreenLongPress` calls
`gpio.suppressTouchContact()` on consumption (`src/MappedInputManager.cpp:155-163`),
so the lift produces no tap either. Every surviving tap therefore reports
`heldMs < 500 < 700`. The gate is the same `SETTINGS.touchReaderControls`
truthiness that enables the tap zones, so the two cannot be separated.

**The setting's one remaining effect is inert.** `ReaderUtils.h:53` derives
`usePress = (longPressButtonBehavior == OFF)`, consumed at `ReaderUtils.h:59-69`
and `EndOfBookOptions.cpp:137-140`. Both paths read only `PageBack`/`PageForward`
(→ `BTN_UP`/`BTN_DOWN`, whose synthetic press and release land on the same tick,
`HalGPIO.cpp:210-225`), `Button::Left`/`Right` (→ `SETTINGS.frontButtonLeft/Right`
= `FRONT_HW_LEFT/RIGHT` = `BTN_LEFT`/`BTN_RIGHT`, dead pins) and
`wasReleased(Power)`, which `usePress` does not gate (`ReaderUtils.h:64-65`). So
press-vs-release selects between two identical results.

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

- **Making chapter skip reachable.** Both routes are rejected on evidence: A1
  (buttons) and A2 (touch).
- **Removing the reader's chapter-skip branches**, the persisted byte, or the
  `usePress` derivation. `d8e92208` set the precedent by gating its option while
  leaving `EpubReaderActivity.cpp:619`'s `ORIENTATION_CHANGE` branch in place —
  still there today. See A4.
- **Touching `lib/I18n/translations/`.** See §0 and A5.
- **Touching `NavKeyGestures`, `HalGPIO` or the synthesis.** Phase 2 owns the
  input layer (`.claude/agents/ui-dev.md`); this change only documents it.
- **Changing the passage-selection gesture** at `EpubReaderActivity.cpp:450-457`.
  See A2.
- **`ButtonNavigator.cpp:70`** — continuous list scrolling is dead on the nav
  keys for a related suppression (`HalGPIO.cpp:181-182`,
  `HOLD_STATE_SUPPRESS_MS = 250`). Pre-existing, out of scope.
- **Device verification.** Flagged for the human in the PR body.

## 4. Architecture

### 4a. Gate the setting behind a capability (commit 1)

Modelled on **`d8e92208` — "fix: gate screen rotation behind a portrait-only
capability (#45)"**, which closed #37. That commit is a line-for-line template:
a `BEREAN_CAP_*` macro in `src/CrossPointSettings.h:17-23` with an `#error` for
an unhandled device, an `#if` around the option in `src/SettingsList.h`, and one
`#if` around the matching reader-menu row (`EpubReaderMenuActivity.cpp:72-74`).
It touched three source files, no translations, and no test.

| Change | File |
|---|---|
| `BEREAN_CAP_LONG_PRESS_PAGE_TURN` — `0` under `FREEINK_DEVICE_X4PRO`, `#error` otherwise | `src/CrossPointSettings.h`, beside `BEREAN_CAP_ROTATION` at `:17-23` |
| Wrap the whole `SettingInfo::Enum` entry (both arms of the existing `#if BEREAN_CAP_ROTATION`) in the new `#if` | `src/SettingsList.h:349-358` |

The existing `#if BEREAN_CAP_ROTATION` split stays **nested inside** the new
gate, not collapsed: on a future board where the long-press page turn is real,
whether it also offers orientation change is still a separate question.

Nothing else moves. The four `STR_LONG_PRESS_BEHAVIOR*` keys stay in all 32
YAMLs and stay "used" as far as `gen_i18n.py:267` is concerned, exactly as
`STR_LONG_PRESS_BEHAVIOR_ORIENTATION` has since `d8e92208`.

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

It refuses when the board does not have four wired front-button pins, which is
what this activity's four-role walk requires (`kRoleCount = 4`,
`ButtonRemapActivity.cpp:13`). `#include <Logging.h>` is added — the file has no
logging include today.

**Not an `assert`, and not inside `getPressedFrontButton()`** — see A9 and A10.

## 5. Data and control flow

### 5a. After the gate

The runtime path is untouched. `SETTINGS.longPressButtonBehavior` keeps its
persisted byte and both consumers; only the settings row disappears:

```
SETTINGS.longPressButtonBehavior   (persisted byte, key "longPressButtonBehavior")
  │   ◄── Settings → Controls row          REMOVED on this board (#if)
  │
  ├─ ReaderUtils.h:53 ──► usePress = (value == OFF)
  │     consumers: ReaderUtils.h:59-69, EndOfBookOptions.cpp:137-140
  │     inert here: both edges of every reachable button land on one tick
  │     (HalGPIO.cpp:210-225); Left/Right are dead pins
  │
  └─ EpubReaderActivity.cpp:613 ──► CHAPTER_SKIP branch
        heldMs source:
          touch turn  → touch.heldMs  ← < 500 (tap modes) or 0 (swipe)   ❌
          button turn → getHeldTime() ← SYNTHETIC_HELD_MS = 40           ❌
```

`ReaderActivity.cpp:144-146` is **not** a second consumer. `ReaderActivity::loop()`
(`ReaderActivity.cpp:131`) is dead: `EpubReaderActivity` is the only subclass
(`EpubReaderActivity.h:19`), its `loop()` override
(`EpubReaderActivity.cpp:368`) never chains to the base, and
`grep -rn "ReaderActivity::loop" src` returns only the two definitions. Pass 0
and the research note both cited it as live; both are corrected.

**A stale byte is harmless.** A settings file carrying `CHAPTER_SKIP` from an
earlier build keeps it after the row disappears. The effect is `usePress == false`
— release-triggered paging — which on this board is identical to press-triggered
(above). No migration, no store write, no `saveToFileAtomic()`. See A6.

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

Pass 0 claimed the user would be "stranded, with the only exit the cancel at
`ButtonRemapActivity.cpp:59`". **That was wrong.** There are two working exits:
`ButtonRemapActivity.cpp:48-57` (`Button::Up` — restore defaults, save, finish)
and `:59-63` (`Button::Down` — cancel), both on `BTN_UP`/`BTN_DOWN`, the keys
that do produce events. The screen is degraded, not a trap — which is precisely
why the refusal is a log-and-pop and not a panic (A9).

## 6. Error handling

| Condition | Handling | Precedent |
|---|---|---|
| A board reaches `ButtonRemapActivity` without four wired front buttons | `LOG_ERR("REMAP", …)` + `finish()` in `onEnter()` — `CLAUDE.md` error-handling case 2 | `ReaderActivity.cpp:44-47` |
| A user's saved `longPressButtonBehavior` byte | Untouched. No migration, no write | A6 |
| `getPressedFrontButton()` finds nothing | Unchanged: returns `-1`; `ButtonRemapActivity.cpp:72-74` returns early | existing |
| A future board sets no `BEREAN_CAP_LONG_PRESS_PAGE_TURN` | `#error` at compile time, forcing an explicit answer | `CrossPointSettings.h:17-23` |

No new allocation, no new file, no new store, no new task, no string-table change.
Flash drops by one `SettingInfo::Enum` construction; RAM is unchanged.

## 7. Testing strategy

### 7a. Host test (TDD, red first)

One new `TEST` appended to the existing suite
`test/nav_key_gestures/NavKeyGesturesTest.cpp`. It follows that file's
established habit of hardcoding the reader's threshold with a `<<` message
naming it (`:142`), because `ReaderUtils.h` pulls in the whole UI stack and
cannot be included on host.

The assertion is **absolute**, per review MAJOR 3 — a key pressed and released at
`SKIP_HOLD_MS + 1` (701 ms, the shortest duration a user could aim at to trigger
chapter skip) must still resolve to `NavEvent::Page` **and** report
`reportingSyntheticHeldTime()`, i.e. the one duration that would work reports 40,
not 701.

Red first: lowering `HOLD_MS` to 700 makes the new test fail on its own
assertion, because 701 then crosses into `Synth`. Pass 0's procedure could not do
this — its assertions were all expressed *relative* to `HOLD_MS` or between two
unrelated constants, so lowering `HOLD_MS` would have reddened the pre-existing
`ThresholdsSitBetweenTheReadersOwnHolds` (`:141-144`) instead and the implementer
would have banked someone else's failure. The red run is recorded in the plan.

Acknowledged overlap: `:141-144` also goes red when `HOLD_MS ≤ 700`. The new test
earns its place by driving the state machine at that exact duration rather than
comparing two constants — it fails with "701 ms resolved to Synth", which names
the user-visible consequence. The two assertions pass 0 proposed for `Synth`
producing no page event and for `SYNTHETIC_HELD_MS`'s magnitude are **dropped**:
`:44-55` (`AHoldNeverAlsoProducesAPage`) and `:132-139` already cover them, the
latter more strictly (`< 400u` vs `< 700u`).

No new `add_subdirectory`, so **`test/CMakeLists.txt` is not touched** — the
shared-file rule in `.claude/agents/ui-dev.md` does not bite.

### 7b. What cannot be host-tested

The touch half of §1a (`EpubReaderActivity`, `MappedInputManager`, the SDK's
touch classifier) and all of §4b pull in `HalGPIO`, `GfxRenderer`,
`FreeInkUICore`, `CrossPointSettings` and the `Activity` lifecycle. Extracting a
predicate into `lib/` to manufacture a test would restructure a file
`.claude/agents/ui-dev.md` explicitly protects. Compensating checks, each
recorded in the plan with its output:

- `pio run -e x4pro` — proves the new macro, the `#if`, the `Logging.h` include
  and the `finish()` call compile, and that the `#error` arm is not taken.
- `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` still reports
  **420 keys, 1 unused** — unchanged, because no key is added or removed and the
  four gated names stay visible to the raw-text scan at `:267`. (Corrects MINOR
  5: a `STR_*` used in source but absent from `english.yaml` does not skew the
  unused report — it prints `CRITICAL: … used in source but missing from
  english.yaml` and `sys.exit(1)` at `gen_i18n.py:873-881`, failing `pio run` at
  the `pre:` step, `platformio.ini:131`. That is why this design keeps the keys.)
- Host suite green via `cmake -S test -B build && ctest`.
- `./bin/clang-format-fix` over the whole tree, not `-g` (`CLAUDE.md`).

### 7c. Human, on device

1. Settings → Controls no longer lists **Long-press button behavior**; the rows
   above and below it (`STR_FRONT_BTN_FOLLOW_ORIENTATION` at `SettingsList.h:346`
   and the long-press menu at `:359`) render with no gap or stale selection.
2. A unit whose settings file already holds `CHAPTER_SKIP` boots, pages normally
   by tap and by nav key, and shows no chapter skip.
3. A long press in an outer tap zone still opens passage selection — the gesture
   this change refuses to disturb.
4. Free heap above ~50 KB across reader → settings → reader.

## 8. Assumptions

**A1 — UNRATIFIED SCOPE CALL. Removing the setting is preferred to rewording
it.** Issue #60 asked for a reword or a real fix. Review 0 established the
feature is unreachable by any deliberate input (§1a), so a reword would ship a
row that does nothing under a more honest name. The alternatives, rejected:
(a) changing the selection gesture at `EpubReaderActivity.cpp:450-457` to yield
inside the page-turn zones when `CHAPTER_SKIP` is set trades passage
highlighting — core to a study device — for a feature nothing else can reach, and
is a behaviour change well outside the issue; (b) rewording and documenting the
row as dead keeps an inert control on a screen the user has to scroll. **This
removes a user-facing setting, which is more than #60 authorised, and the
`hpipe decide` raised for it was misrouted and never answered — see §0.** If the
human prefers (b), §4a becomes a one-line English string change and §4b is
unaffected.

**A2 — Touch cannot reach chapter skip in any mode, so "gate on `hasTouch()`" is
not an option.** Verified per mode in §1a's table. The load-bearing evidence is
`ReaderUtils.h:84-93` (swipe mode never assigns `heldMs`, and says so) and
`EpubReaderActivity.cpp:450-457` + `TOUCH_LONG_PRESS_MS = 500`
(`InputManager.h:399`) + `suppressTouchContact()` (`MappedInputManager.cpp:161`)
for the tap modes. *If this is wrong, A1 is wrong.*

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

**A6 — The persisted key and member are not renamed, and no migration runs.**
`"longPressButtonBehavior"` at `SettingsList.h:353,357` is the JSON key in the
settings store; a stale `CHAPTER_SKIP` byte is inert (§5a). Writing a migration
would cost a store write for no observable change.

**A7 — `BEREAN_CAP_LONG_PRESS_PAGE_TURN` is a new capability macro, not a reuse
of `BEREAN_CAP_ROTATION`.** They answer different questions: one is "does this
panel rotate", the other "can any input here deliver a held page turn". A board
could answer them differently, and `d8e92208`'s `#error` idiom exists precisely
to force each board to answer for itself.

**A8 — The host test asserts an absolute duration, not a relative one.** Only an
absolute assertion can go red for its own reason; see §7a. The overlap with
`:141-144` is accepted and stated rather than engineered away.

**A9 — `LOG_ERR` + `finish()`, not `assert`. This reverses pass 0's A9.** The
state is reached by a user selecting a settings row (`SettingsActivity.cpp:75-78`,
`:308-310`), not by an impossible program state, and `NDEBUG` is absent from both
envs (`[env:x4pro]` flags at `platformio.ini:164-176`, `[env:x4pro-gh_release]`
at `:182-190`) so an `assert` would `abort()` a shipped device. `CLAUDE.md`
reserves `assert(false)` for fatal impossible states and puts this in case 2.

**A10 — The refusal goes in `ButtonRemapActivity::onEnter()`, not in
`getPressedFrontButton()`.** A guard inside the function would also fire for a
future "press any front button" flow that would in fact work here, since Back and
Confirm are synthesisable — a false refusal. The remap activity's requirement is
unambiguous: four distinct, physically distinguishable buttons. The comment, not
the refusal, carries the reachability knowledge to a future caller.

**A11 — The refusal tests all four pins, not just `left`/`right`.** The activity
needs four distinct sources; a board synthesising Back and Confirm from the same
two nav keys it would also need for Left and Right cannot satisfy it.

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
| `docs/superpowers/research/2026-09-17-issue-60-research.md` | correct the `ReaderActivity.cpp:144-146` rows (MAJOR 2) |

Not touched: `lib/I18n/translations/`, `test/CMakeLists.txt`, `src/main.cpp`,
`lib/hal/`, `lib/Input/`, `src/activities/reader/`, `freeink-sdk/`.

`src/MappedInputManager.cpp` sits outside the `ui` surface as
`.claude/agents/ui-dev.md` defines it, and that file forbids touching the input
layer "without an explicit instruction". Issue #60 names
`MappedInputManager.cpp:377-393` directly; the change is one comment and adds no
behaviour.
