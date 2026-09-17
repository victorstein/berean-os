# Issue #60 — spec review 0

Target: `docs/superpowers/specs/2026-09-17-issue-60-design.md`
Against: `gh issue view 60`, `docs/superpowers/research/2026-09-17-issue-60-research.md`
Date: 2026-09-17 · Branch: `fix/60-input-layer-traps` · Worktree clean at review time.

Everything in §1a about the nav-key path was re-derived from source and holds:
`NavKeyGestures::updateKey` resolves on release into exactly one of `Page` /
`Synth` (`lib/Input/Input/NavKeyGestures.cpp:12-31`), `reportingSyntheticHeldTime()`
is true for both (`NavKeyGestures.cpp:60-62`), `HalGPIO::getHeldTime()` substitutes
40 (`lib/hal/HalGPIO.cpp:236-239`), and the `Synth` arm emits no page event
(`HalGPIO.cpp:186-205`). Finding 2's dead arms are equally confirmed
(`BoardConfig.h:1396` + `:406`, `InputManager.cpp:246,257-258`,
`HalGPIO.cpp:186-205`). Every i18n number in §6, §7b and A4 reproduces exactly
(`python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` → 32 languages,
420 keys, 1 unused, `Deutsch 341/79`, `Español 387/33`, all others ≤ 342).

What does not hold is the premise the whole of §4a rests on.

---

## BLOCKER 1 — "Touch can and does" reach chapter skip is false in the only reader that ships

**Claim.** §1a: "The label names the one input that cannot reach the feature.
Touch can and does". §3 non-goal: "Removing or gating the setting. It works, via
touch." §5a: `touch turn → touch.heldMs (real) ✅ > 700 possible`. Goal 1: after
the reword "the Controls setting names an input that can actually reach it".

**Problem.** In `EpubReaderActivity` — the only reader that exists — a stationary
contact in an outer tap zone is consumed by the passage-selection gesture at
**500 ms**, before it can ever become a page turn with `heldMs > 700`. The
reworded label would name touch, and touch cannot reach chapter skip either by
any gesture a user would deliberately perform. The design's central choice
("rewording is the fix") is made against a reachability claim that was never
verified past `ReaderUtils.h:120`.

**Evidence.**

- `src/activities/reader/EpubReaderActivity.cpp:451-459` — inside `loop()`, ahead
  of all page-turn handling:
  ```cpp
  if (SETTINGS.touchReaderControls && mappedInput.wasScreenLongPress(longPressX, longPressY) &&
      !ReaderUtils::isInMenuZone(renderer, longPressX, longPressY)) {
    openHighlightPassageAt(longPressX, longPressY);
    return;
  }
  ```
  `isInMenuZone` is the centre third in **both** axes (`ReaderUtils.h:131-137`),
  so the whole of both page-turn zones (outer horizontal thirds, full height,
  `ReaderUtils.h:106-112`) is outside it. Every long press in a page-turn zone
  takes this branch and returns.
- `src/MappedInputManager.cpp:155-164` — `wasScreenLongPress` calls
  `gpio.suppressTouchContact()` on consumption.
- `InputManager.cpp:1073-1076` — the long-press event fires **while the finger is
  still down**, once `now - touchDownPoint.timestamp >= TOUCH_LONG_PRESS_MS`.
  `TOUCH_LONG_PRESS_MS = 500` (`InputManager.h:399`).
- `InputManager.cpp:566-568` — `wasTouchTap` returns false when `touchSuppressed`,
  and the latch clears only on a fully idle frame, *after* the release edge is
  consumed (`InputManager.cpp:1049-1056`). So the lift produces no tap, so
  `detectTouchPageTurn` (`ReaderUtils.h:98`) yields `{false,false,0}`.
- Therefore the two survivable cases are: released before 500 ms →
  `touch.heldMs < 500 < SKIP_HOLD_MS (700)`; held past 500 ms → passage
  selection, no page turn. `EpubReaderActivity.cpp:605-617` cannot fire from a
  deliberate long press.
- The one residual path is an accident, not an affordance: a contact that drifts
  **more than 28 px but less than 60 px** sets `touchMovedBeyondTapSlop` (which
  cancels long-press classification, `InputManager.cpp:1112-1113`, `:1073`) but
  not `touchMovedBeyondTapReleaseSlop` (which alone cancels the tap,
  `InputManager.cpp:573`) — `TOUCH_TAP_SLOP_PX = 28`,
  `TOUCH_TAP_RELEASE_SLOP_PX = 59` (`InputManager.h:392-394`). Held past 700 ms
  and released, that produces the chapter skip. No user can aim for that.
- Consequence for §7c.2: the prescribed device check ("a long press on an outer
  tap zone skips") will not skip — it will open `PassageSelectActivity`. The
  design would ship a label that is still false and a verification step that
  fails.

**Concrete fix.** This is a scope decision and needs the human, not an inline
edit. Re-open the choice with the selection gesture in the picture:

1. Gate or retire the option on this board (the mirror of `d8e92208`'s
   `BEREAN_CAP_ROTATION` treatment for the sibling option), since with
   `touchReaderControls != OFF` the 500 ms gesture owns every long contact in the
   page-turn zones; or
2. Make the selection gesture yield inside the page-turn zones while
   `longPressButtonBehavior == CHAPTER_SKIP` (a real behaviour change in
   `EpubReaderActivity.cpp:451-459`, outside the current non-goals); or
3. Keep the reword but state plainly that the feature is unreachable on this
   board and say why the label is worth changing anyway.

Whichever is chosen, A1 has to be re-argued: it compares only button-vs-button
and never asks whether the touch route it falls back on is live. §3's "It works,
via touch", §5a's `✅`, and §7c.2 must be corrected in the same pass.

---

## MAJOR 2 — `ReaderActivity.cpp:144-146` is dead code, so "both readers" is one reader

**Claim.** §1a: "both readers prefer it when the turn came from a tap zone
(`EpubReaderActivity.cpp:605`, `ReaderActivity.cpp:144`)". §5a diagrams
`EpubReaderActivity.cpp:613 / ReaderActivity.cpp:146 ──► CHAPTER_SKIP branch` as
two live consumers. The research note repeats it (`research …:19-20`).

**Problem.** `ReaderActivity::loop()` never runs. Citing it as a second live
branch site inflates the apparent surface of the setting and hid BLOCKER 1: the
cited `ReaderActivity` body has no passage-selection gesture, so reasoning from
it makes the touch path look reachable when the shipping reader's does not.

**Evidence.**

- `src/activities/reader/ReaderActivity.cpp:131` defines `ReaderActivity::loop()`;
  `src/activities/reader/EpubReaderActivity.cpp:368` defines
  `EpubReaderActivity::loop()`, declared `void loop() override;`
  (`EpubReaderActivity.h:169`, `ReaderActivity.h:52`).
- `EpubReaderActivity::loop()` never chains to the base —
  `grep -rn "ReaderActivity::loop()" src` returns only the two definitions, no
  call site.
- `EpubReaderActivity` is the only subclass (`EpubReaderActivity.h:19`,
  `class EpubReaderActivity final : public ReaderActivity`), and
  `ReaderActivity::create` constructs nothing else (`ReaderActivity.cpp:25-29`).

**Concrete fix.** Delete the `ReaderActivity.cpp:144-146` citations from §1a and
§5a, or mark them explicitly as an unreachable base-class implementation. Correct
the same two rows in the research note's ownership table.

---

## MAJOR 3 — §7a's red-first procedure cannot turn the new test red, and the test duplicates landed coverage

**Claim.** §7a: the new `TEST` asserts (i) release just under `HOLD_MS` → `Page`
and `reportingSyntheticHeldTime()`; (ii) release at or over `HOLD_MS` → `Synth`,
no page event; (iii) `SYNTHETIC_HELD_MS < 700u`. "Red first by temporarily
lowering `HOLD_MS` below 700 (which makes the middle assertion fail) and
confirming the failure before reverting."

**Problem.** None of the three assertions is sensitive to `HOLD_MS`'s absolute
value: (i) and (ii) are expressed *relative* to `HOLD_MS`, so they hold for any
value of it, and (iii) compares two constants that `HOLD_MS` does not touch.
Lowering `HOLD_MS` to, say, 600 leaves the new test green and turns the
**pre-existing** `ThresholdsSitBetweenTheReadersOwnHolds` red instead — so the
implementer sees a red run, records it as the TDD red step, and lands a test that
has never failed for its own reason. On top of that the new test adds almost no
coverage.

**Evidence.**

- `test/nav_key_gestures/NavKeyGesturesTest.cpp:141-144` already pins
  `EXPECT_GT(NavKeyGestures::HOLD_MS, 700u) << "above SKIP_HOLD_MS"` — this is
  the assertion that goes red when `HOLD_MS` drops below 700, and it is already
  landed.
- `:132-139` `ASynthesisedEventReportsAShortHeldTime` already asserts
  `EXPECT_TRUE(g.reportingSyntheticHeldTime())` and
  `EXPECT_LT(NavKeyGestures::SYNTHETIC_HELD_MS, 400u)` — strictly stronger than
  bullet (iii)'s `< 700u`.
- `:44-55` `AHoldNeverAlsoProducesAPage` already asserts `pages == 0` and
  `synths == 1` across a hold — bullet (ii).
- `:18-24` `ATapResolvesToAPageTurnOnRelease` — bullet (i)'s first half.
- Goal 3 ("a host test pins the constant relationship") is therefore already met
  by `:138` and `:142-143` before this change.

**Concrete fix.** Rewrite §7a around an **absolute** assertion, which is the only
kind that can fail for the reason the test exists: a key released at
`SKIP_HOLD_MS + 1` (701 ms, hardcoded with the `<<` message naming `SKIP_HOLD_MS`,
per the existing precedent at `:142`) must still resolve to `NavEvent::Page`
*and* report `reportingSyntheticHeldTime()` — i.e. the one duration a user could
aim at reports 40, not 701. That goes red the moment `HOLD_MS` is lowered to 700
or below, which is exactly the change the test is meant to catch. Drop bullets
(ii) and (iii) as already-landed duplicates, or state which existing `TEST` each
one is deliberately re-asserting and why.

---

## MAJOR 4 — the assert's justification is factually wrong, and it converts an escapable screen into `abort()` on a shipped build

**Claim.** §5b: today a touchless board without four wired front buttons "strands
the user in a four-role walk that can never complete … with the only exit the
`wasPressed(Button::Down)` cancel at `ButtonRemapActivity.cpp:59`". A9: a runtime
`assert` is preferred over `LOG_ERR` + `finish()` because this is "a
board-configuration error caught at integration time, not a runtime condition".

**Problem.** Two things. First, "the only exit" is wrong — there are two, and the
user is not stranded. Second, the state the assert guards is *user-reachable at
runtime* (the gate that leads here is a settings row, not a boot path), so the
assert is not guarding an impossible state; it fires when a user opens a menu
entry, and with no `NDEBUG` anywhere it calls `abort()` on a shipped device.
`CLAUDE.md` reserves `assert(false)` for "fatal impossible states (framebuffer
missing)" and puts everything else in case 1/2 (`LOG_ERR` + return / fallback).
The change therefore replaces a degraded-but-escapable screen with a panic, and
justifies it with a defect that does not exist.

**Evidence.**

- `src/activities/settings/ButtonRemapActivity.cpp:48-57` — a second exit:
  `wasPressed(Button::Up)` restores the default mapping, saves, and `finish()`es.
  `:59-63` is the cancel. Both are `MappedInputManager::Button::Up/Down`, which
  map to `BTN_UP`/`BTN_DOWN`, which on a two-nav-key board are exactly the keys
  that *do* produce events (`HalGPIO.cpp:198-201`). The hypothetical stranded
  user has a reset and a cancel, both working.
- Reachability is a user action, not an integration event:
  `SettingsActivity.cpp:75-78` appends the row under `!BoardConfig::hasTouch()`
  and `:308-310` constructs the activity from a list selection.
- `NDEBUG` is absent from the build: not in `platformio.ini` (`[env:x4pro]`
  flags at `:164-176`, `[env:x4pro-gh_release]` at `:182-190` — `grep -n NDEBUG
  platformio.ini` is empty) and not in the framework's own defines
  (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/flags/defines`
  contains no `NDEBUG`; no PlatformIO platform builder adds one). A9's premise is
  right; its conclusion is what's at issue.

**Concrete fix.** Either (a) drop the assert for `LOG_ERR("REMAP", …)` +
`finish()` in `onEnter()` — `CLAUDE.md` error-handling case 2, same loudness on
serial, no panic, and the remap row simply refuses on a board that cannot satisfy
it; or (b) keep the assert and rewrite §5b/A9 honestly: say that it aborts a
shipped device the first time a user opens that row on a misconfigured board, and
that this is preferred to a screen that completes no role. Remove the "only exit"
claim either way — `ButtonRemapActivity.cpp:48-57` disproves it.

---

## MINOR 5 — §7b and A5 invert what happens when the retired key survives in a comment

**Claim.** §7b: a retired name "left in a comment would silently keep the key off
the unused-key report". A5: a comment naming `STR_LONG_PRESS_BEHAVIOR` "would
re-register the retired key as 'used'".

**Problem.** Once the key is deleted from `english.yaml` it is no longer in
`string_keys`, so nothing can "keep it off the unused report" — instead the
generator classifies it as *used in code but missing from English* and **exits 1**,
failing `pio run`. The outcome the spec describes as silent is in fact the
loudest failure in this pipeline. The conclusion (don't leave the name in a
comment) survives; the stated mechanism, and the implied need for the grep as the
only net, do not.

**Evidence.**

- `scripts/gen_i18n.py:873-881`:
  `missing_keys = sorted(used_keys - set(string_keys))` → prints
  `CRITICAL: … used in source but missing from english.yaml` → `sys.exit(1)`.
- `platformio.ini:131` — `pre:scripts/gen_i18n.py`, so that exit fails the build.
- The regex at `:267` does scan comments, as stated; that is what turns a stray
  comment into a build failure rather than a silent report skew.

**Concrete fix.** Reword §7b's third bullet and A5's parenthetical: a retired
`STR_*` left anywhere in `src`/`lib` — comment included — fails `pio run` at the
`gen_i18n.py` pre-step (`:873-881`), which is why the name must not appear in a
source comment. Keep the grep as an early, cheap check, not as the safety net.

---

## MINOR 6 — the new label's nearest neighbour is never checked

**Claim.** A2 argues "Long-press page turn" is true on every board. §9 lists no
other label.

**Problem.** The row immediately below it in the same category is already called
"Long-press Menu". Two adjacent Controls rows both starting "Long-press" is a
legibility question the design should at least name, especially as the other one
*is* about the capacitive Home key — i.e. a real long-pressable control on this
board, which makes the pairing more confusing, not less.

**Evidence.** `lib/I18n/translations/english.yaml:97`
`STR_LONG_PRESS_MENU: "Long-press Menu"`; built at `src/SettingsList.h:192-197`
with `category = StrId::STR_CAT_CONTROLS` and appended directly after the setting
being reworded (`SettingsList.h:359`).

**Concrete fix.** Add a line to A2 comparing the two rows as the user sees them
stacked, or pick a label that does not share a prefix with its neighbour.

---

## MINOR 7 — citation drift

**Claim/Evidence/Fix**, three small ones, all worth correcting since the spec's
own standard is file-and-line:

- §1a cites `NavKeyGestures::updateKey` as `NavKeyGestures.cpp:22-31`; the
  function starts at `:12` and the resolve is the single line `:28`.
- §7a says it models `ASynthesisedEventReportsAShortHeldTime` at "lines 131-139";
  the `TEST` is `:132-139` (`:129-131` is its comment).
- A7 cites "platformio.ini:160,178" for the build flags; those lines are the
  `[env:x4pro]` / `[env:x4pro-gh_release]` headers — the flag blocks are
  `:164-176` and `:182-190`. (The absence of `NDEBUG` is correct.)

---

## Verdict rationale

BLOCKER 1 removes the reason for the change as written: with the passage-selection
gesture in the path, "reword the label to name touch" produces a label that is
still false on this device, and the device-verification step the spec prescribes
will fail. Choosing between gating the option, changing the selection gesture, and
documenting the dead feature is a scope call for the human, not an inline edit.
MAJOR 2 is the evidence error that let it through. MAJORs 3 and 4 are fixable
inline but 4 reverses A9 if taken.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 3
