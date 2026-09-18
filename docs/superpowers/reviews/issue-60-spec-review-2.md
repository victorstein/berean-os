# Issue #60 — spec review 2

Target: `docs/superpowers/specs/2026-09-17-issue-60-design.md` (pass 2, `a3b88886`)
Against: `gh issue view 60`, `docs/superpowers/research/2026-09-17-issue-60-research.md`,
`reviews/issue-60-spec-review-0.md`, `reviews/issue-60-spec-review-1.md`
Date: 2026-09-18 · Branch: `fix/60-input-layer-traps` · Worktree clean (`git status --short` empty).

The ratified scope call (gate the setting off, human ruling recorded in `099946d0`) is
treated as settled and is not re-argued here.

## The three carried findings, checked against source rather than against §0

**Pass 1 MAJOR 2 — fixed, and fixed honestly.** Every place the old claim could have
survived now states the real consequence, and none of them hedges:

- §0 has a dedicated subsection naming the earlier claim as wrong and quoting
  `d8e92208`'s commit body back at it.
- §3's non-goal no longer lists "the persisted byte"; it carries an explicit
  parenthetical that the key does go away.
- §5a: "A saved `CHAPTER_SKIP` byte stops being honoured and is then dropped."
- §6's table row: "Ignored on read, dropped on the next save. Accepted, not migrated."
- A6's title is "The key leaves the persistence schema, and that is accepted without a
  migration."
- §9 adds the `data-dev` surface note pass 1 lacked.

The new mechanism is also correct, not just plausible. `toJson`
(`src/CrossPointSettings.cpp:63`, loop at `:66`) and `fromJson` (`:107`, loop at `:113`)
both iterate `getSettingsList()`; `"longPressButtonBehavior"` has no manual line beside
the keys that do (`:86-104`); `grep -rn longPressButtonBehavior src lib test scripts`
shows no other reader or writer of the key. The member default is `OFF`
(`CrossPointSettings.h:295`), so `usePress` becomes **true** (`ReaderUtils.h:53`) — as
§0 now says, and the opposite of what pass 1 said. Nothing forces an early resave:
`needsResave` is set only at `CrossPointSettings.cpp:125,139,153,178,196,211,213`, none
of which a missing key triggers, so "keeps the key on disk until the next save" holds.

I also re-derived the press-vs-release equivalence the benign-ness argument rests on,
because it is now load-bearing for A6. It holds for *every* index, not just the ones §1a
names: `MappedInputManager::wasPressed` and `wasReleased` have identical special-case
prefixes (`:301-305` vs `:309-313`), `mapButton` is shared (`:57-121`), and
`HalGPIO::wasPressed`/`wasReleased` both return `synthesisedEdge()` for `BTN_BACK`,
`BTN_CONFIRM`, `BTN_UP` and `BTN_DOWN` (`HalGPIO.cpp:210-225`). Even a remapped
`frontButtonLeft` pointing at `BTN_BACK` stays symmetric. `BTN_LEFT`/`BTN_RIGHT` are
dead pins (`BoardConfig.h:1396`, `:406`; `InputManager.cpp:246,257-258`).

**Pass 1 MINOR 3 (citation drift) — the four named corrections landed, but pass 2
introduced one more.** Verified by reading: `EpubReaderActivity.cpp:451-459` is the
block and `:454-455` the test; `SettingsList.h:347` is
`STR_FRONT_BTN_FOLLOW_ORIENTATION`; `EpubReaderMenuActivity.cpp:73-75` is the
`#if BEREAN_CAP_ROTATION` row; `NavKeyGestures.cpp:29` is the resolve. See MINOR 3 below
for what is new.

**Pass 1 MINOR 4 (research note) — cleared.** All four outstanding items are done in the
note itself, not merely claimed: `research:23` is struck and relabelled, `research:158-165`
carries the exit-1 mechanism, the scope note at `research:244-252` lists the current file
set and the persistence consequence, and `research:36` now reads
`NavKeyGestures.cpp:12-32 … the resolve is the single line :29`. §9's stale "edit the
research note" row is gone. (One residual, below a finding's bar: the note's heading
"Is a board-aware assert affordable? — yes" at `research:215` is superseded by A9 and is
the only superseded passage in the note without a `CORRECTED` marker. It is feasibility
research, not a recommendation, so I am not raising it.)

## Independently re-verified and holding

`SettingsList.h:349-358` and the `std::vector<SettingInfo>` initializer at `:254`;
`CrossPointSettings.h:17-23`, `:209-214`, `:295`, `:325`; `ReaderUtils.h:18,53,59-69,
79-81,84-94,108-112,120,131-137`; `EpubReaderActivity.cpp:368,451-459,605-606,613,619`;
`EndOfBookOptions.cpp:137-140`; `HalGPIO.cpp:163-177,181-182,186-205,194-197,198-201,
210-225,215,236-239`; `NavKeyGestures.h:41,48,68`, `NavKeyGestures.cpp:12-32,29,60-62`;
`MappedInputManager.cpp:155-164,161,322-328,378-393`; `BoardConfig.h:406,1396,1411,1622`;
`InputManager.h:399`, `InputManager.cpp:246,257-258`;
`ButtonRemapActivity.cpp:13,20,48-57,59-63,71,72-74` (and it really has no logging
include — though `Logging.h` already arrives transitively via `Activity.h:2`, so the
added include is style, not a fix); `SettingsActivity.cpp:75-78,308-310`;
`ActivityManager.cpp:161-165`; `Activity.cpp:24`; `ReaderActivity.cpp:44-47,131`;
`CrossPointWebServer.cpp:1159,1263`; `KeyboardEntryActivity.cpp:544-546,620,652,659`;
`gen_i18n.py:267,873-881`; `platformio.ini:131` and `grep -n NDEBUG platformio.ini`
empty; `settings_snapshot.py:11-12`; `NavKeyGesturesTest.cpp:44-55,132-139,141-144`;
`spanish.yaml:89`, `german.yaml:71`; `.claude/agents/ui-dev.md:22-27,29-34`.

`python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` on this tree reproduces
§7b exactly: **32 languages, 420 string keys, 1 unused**.

§7a's red-first procedure works as claimed: `updateKey` resolves
`elapsed >= HOLD_MS ? Synth : Page` (`NavKeyGestures.cpp:29`), so with `HOLD_MS`
temporarily 700 a release at 701 ms crosses to `Synth` and the new `EXPECT_EQ(…Page)`
fails on its own assertion. (`reportingSyntheticHeldTime()` stays true either way, which
§7a does not over-claim.)

A11's no-false-refusal claim holds: the only non-X4-Pro profile with a partial front quad
is `MURPHY_M4` (`BoardConfig.h:1027`), which wires `confirm` and leaves `left`/`right`
unassigned — the reverse of the case A11 rules out, and a *true* refusal there.

`main` has not moved in this clone (`git merge-base HEAD main` = `11d9497d` = `main` =
`origin/main`), and no seam with the spec's two files is visible from here.

---

## MAJOR 1 — §7c.1's on-device check names a Controls row that this board never renders, and the spec's own §0 cites the code that removes it

**Claim.** §7c.1: "Settings → Controls no longer lists **Long-press button behavior**;
the rows above and below it (`STR_FRONT_BTN_FOLLOW_ORIENTATION` at `SettingsList.h:347`
and the long-press menu at `:359`) render with no gap or stale selection."

**Problem.** `STR_FRONT_BTN_FOLLOW_ORIENTATION` is erased from the list at runtime on
every touch board, and the X4 Pro is one. The human tester is told to confirm that a row
which does not exist on this device still renders correctly next to the gap. Either they
cannot perform the step, or they perform it, see no such row, and report a regression the
change did not cause. The row actually above the gated entry on this board is
`STR_TAP_FOR_READER_MENU` (`SettingsList.h:345`), which survives because
`BoardConfig::hasHomeKey()` is true here.

This is not a line-number slip: §0 cites the very block that disproves it, while using it
as supporting precedent.

**Evidence.**

- `src/SettingsList.h:467-474`:
  ```cpp
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX ||
                                    s.nameId == StrId::STR_BACK_SHORT_TO_FILE_BROWSER;
                           }),
            v.end());
  ```
- `BoardConfig::hasTouch()` is `ACTIVE.touch.controller != TouchController::None`
  (`BoardConfig.h:1622`); the X4 Pro profile declares a GT911 (`BoardConfig.h:1411`).
  So the erase fires on this board.
- §0 already says so, in the paragraph arguing the persistence change is precedented:
  "`SettingsList.h:452-472` already un-persists `fadingFix`,
  `frontButtonFollowOrientation` and `backShortToFileBrowser` the same way at runtime."
  A row that is un-persisted by being erased from the list is also not displayed.
- The row *below* is fine: `buildLongPressMenuSetting()` (`SettingsList.h:192-197`,
  called at `:359`) is `STR_LONG_PRESS_MENU`, and no erase block removes it
  (`SettingsList.h:453-489`).
- `STR_TAP_FOR_READER_MENU` (`SettingsList.h:345`) is erased only when
  `!BoardConfig::hasHomeKey()` (`SettingsList.h:462-466`); the X4 Pro sets `hasHomeKey`
  (`BoardConfig.h:1430`, predicate at `:1623`), so it stays and is the true neighbour.

**Concrete fix.** In §7c.1 replace `STR_FRONT_BTN_FOLLOW_ORIENTATION` at
`SettingsList.h:347` with `STR_TAP_FOR_READER_MENU` at `SettingsList.h:345`, and add
half a sentence saying why (`SettingsList.h:467-474` already removes the
frontButtonFollowOrientation row on touch boards) so the next reader does not
"correct" it back. While there, tighten §0's `SettingsList.h:452-472` to `:467-474`,
which is the block that actually names the three settings.

---

## MINOR 2 — "entering the settings screen is itself such a save" is false, in the paragraph that fixes pass 1's MAJOR

**Claim.** §0, consequence 2: "the key is **deleted from the settings JSON on the next
save**, and entering the settings screen is itself such a save
(`SettingsActivity.cpp:305`)." §5a repeats it: "the next `toJson` — entering the settings
screen is one (`SettingsActivity.cpp:305`) — writes the file without it."

**Problem.** `SettingsActivity.cpp:305` is not an entry path. It is the result handler
constructed inside the `SettingType::ACTION` branch of a row *activation*, and it runs
when a sub-activity returns. Entering `SettingsActivity` saves nothing. Pass 1's review
described this correctly ("Entering and leaving the remap row is itself such a save");
pass 2 generalised it into something the code does not do. The mechanism and the
conclusion are unaffected — only the example trigger is wrong — but this is the one
paragraph in the spec whose whole job is to be accurate about when the key disappears.

**Evidence.**

- `src/activities/settings/SettingsActivity.cpp:111-112` — `onEnter()` is
  `UiTabListActivity::onEnter();` and nothing else; no save.
- `grep -n "saveToFileAtomic" src/activities/settings/SettingsActivity.cpp` → `:230`,
  `:266`, `:284`, `:305`, `:329`, `:347`, `:361`, `:399` — every one inside a value-change
  or row-activation handler.
- `:304-305`:
  ```cpp
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFileAtomic(); };
  ```

**Concrete fix.** In both places, say "changing any setting, or returning from an ACTION
row such as the front-button remap, is such a save (`SettingsActivity.cpp:361`,
`:305`)." §7c.2 already tells the tester to change a setting first, so it needs no edit.

---

## MINOR 3 — pass 2 corrects four citations and introduces three more

**Claim.** §0's table: "MINOR 3 — three new off-by-N citations | Corrected: …".

**Problem.** The four named corrections did land (verified above). But this pass added
three fresh mis-citations, the first of which is a *regression from the number review 1
handed the spec in the same finding it was answering*. Citation accuracy has now been a
finding in three consecutive passes under a `CLAUDE.md` whose standard is file-and-line.

**Evidence.**

- §1a: "`EndOfBookOptions` uses `NavPrevious`/`NavNext`, which compose to the same four
  buttons (`MappedInputManager.cpp:98-107`)." `:98-103` is the tail of `Button::PageForward`'s
  `sideLayout` switch; `NavNext` is `:104-108` and `NavPrevious` is `:109-112`. The correct
  range is `:104-112` — which is exactly what review 1 printed in its MAJOR 2 evidence.
- §6: "the settings JSON gets one key shorter … it is budget-checked at
  `CrossPointSettings.cpp:374` (`saveBudget() == 4096`)". `:374-375` is a `static_assert`
  that `SAVE_BUDGET` reaches the base template ("SAVE_BUDGET is not reaching saveBudget()
  -- check access and spelling"); it checks plumbing, not a serialised size. The budget is
  enforced at `lib/Serialization/PersistableStore.h:171`
  (`if (!persist::fitsBudget(serialised, saveBudget()))`), with the constant at
  `src/CrossPointSettings.h:406`.
- §0: "`MappedInputManager.cpp:160-161` then calls `suppressTouchContact()`". The call is
  `:161`; `:159-160` is its comment. §1a gets this right (`… at :161`), so the spec
  disagrees with itself.

**Concrete fix.** `:98-107` → `:104-112`; point §6 at `PersistableStore.h:171` (and
`CrossPointSettings.h:406` for the value) rather than at the `static_assert`; §0's
`:160-161` → `:161`.

---

## MINOR 4 — §1a states an absolute that review 0 had already disproved, and drops the exception without saying so

**Claim.** §1a, tap modes: "Every surviving tap therefore reports `heldMs < 500 < 700`."
A2 leans on it. The research note repeats it at `research:79`.

**Problem.** There is one surviving tap that reports more. Long-press classification is
gated on `!touchMovedBeyondTapSlop` (28 px), while tap validity is gated on
`!touchMovedBeyondTapReleaseSlop` (59 px). A contact that drifts 29–59 px is therefore
never classified as a long press — so `wasScreenLongPress` never consumes it and
`suppressTouchContact()` never runs — yet it still releases as a valid tap carrying its
real duration. Held past 700 ms in an outer zone with `CHAPTER_SKIP` saved, it skips.
Review 0 found and documented this precisely ("a contact that drifts more than 28 px but
less than 60 px … No user can aim for that"); pass 2 replaced it with an absolute and
said nothing.

Nothing about the decision changes — it is an accident, not an affordance, §0 correctly
scopes the conclusion to "any *deliberate* input", and gating the setting to `OFF` removes
the accidental skip too. But an unqualified absolute that a prior review disproved is the
kind of claim this spec is supposed to have stopped making.

**Evidence.**

- `InputManager.cpp:1073-1074` — long-press fires only
  `if (touchPressed && !touchMultiContactSequence && !touchMovedBeyondTapSlop && … >= TOUCH_LONG_PRESS_MS)`.
- `InputManager.cpp:566-573` — `wasTouchTap` rejects on `touchSuppressed` and on
  `touchMovedBeyondTapReleaseSlop`, **not** on `touchMovedBeyondTapSlop`; its own comment
  says so ("a released tap remains valid until motion reaches the 60 px swipe threshold").
- `InputManager.cpp:1110-1117` sets the two flags from the same delta against
  `TOUCH_TAP_SLOP_PX = 28` and `TOUCH_TAP_RELEASE_SLOP_PX = TOUCH_SWIPE_MIN_PX - 1 = 59`
  (`InputManager.h:392-394`).
- The duration is real: `MappedInputManager::wasScreenTapped` (`:136-143`) calls
  `rememberTouchHeldTime()` (`:130-134`) → `gpio.lastTouchHeldMs()` →
  `InputManager::lastTouchHeldMs` returns `lastTouchHeldDurationMs`
  (`InputManager.cpp:631-637`); `ReaderUtils.h:120` assigns it to `heldMs`.

**Concrete fix.** Soften §1a's sentence to "Every tap a user can aim at therefore reports
`heldMs < 500 < 700`", and add the exception in one line with its two constants — noting
that the gate removes this accidental skip as well, which strengthens the case rather than
weakening it. Mirror the same qualifier at `research:79`.

---

## MINOR 5 — §7b's host-suite command does not match the repo's own and will not run the tests

**Claim.** §7b, compensating checks: "Host suite green via `cmake -S test -B build && ctest`."

**Problem.** It configures into `build/`, never builds, and runs `ctest` with no
`--test-dir`, so it executes in the repo root where there is no CTest configuration. As
written the step reports nothing. §7b is the section that exists precisely because the
rest of the change cannot be host-tested, so its commands should be copy-pasteable.

**Evidence.**

- `test/README:5-7`:
  ```
  cmake -S test -B build/test
  cmake --build build/test
  ctest --test-dir build/test --output-on-failure -j
  ```
- `.github/workflows/ci.yml:188,192,195` runs the same three, with
  `-G Ninja -DCMAKE_BUILD_TYPE=Release` on the configure.

**Concrete fix.** Quote the three lines from `test/README` verbatim in §7b.

---

## Verdict rationale

The finding this pass existed to fix is fixed, and fixed without hedging: §0, §3, §5a, §6,
A6 and §9 all now say that gating the entry takes `longPressButtonBehavior` out of the
persistence schema, that a saved `CHAPTER_SKIP` stops being read and is dropped on the
next save, and that the member falls back to `OFF` so `usePress` becomes true — and each
of those is what the code does. The research note's corrections are complete rather than
claimed. What is left is one wrong statement about what the device screen shows, which
makes one of four human verification steps unperformable as written, and four smaller
accuracy defects — three of them fresh, in a pass whose change log says citations were
corrected. None reverses a decision, changes scope, or needs the human. All five are
inline edits to the spec. One MAJOR, four MINORs, no BLOCKER.

VERDICT: CLEAR
