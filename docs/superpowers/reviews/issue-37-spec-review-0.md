# Issue #37 — adversarial spec review, pass 0

Target: `docs/superpowers/specs/2026-09-16-issue-37-design.md`
Against: `gh issue view 37`, `docs/superpowers/research/2026-09-16-issue-37-research.md`
Reviewed in the worktree at `849a1b49`. Every file:line below was read in this
worktree; every command shown was run here.

Counts: 0 BLOCKER, 1 MAJOR, 3 MINOR.

---

## MAJOR 1 — the device smoke test's first step names two rows that cannot be on that screen

**Claim.** §7.3 step 1: "Settings → Reader: **no Orientation row**; the rows
either side of it (Hyphenation, Extra spacing) are intact and the category still
scrolls."

**Problem.** Hyphenation and Extra spacing are not on the Settings → Reader
screen and never were. Both carry `.withTextSettings()`, and
`SettingsActivity::rebuildSettingsLists` drops every such entry from the Reader
category before it renders. §7.2 and **A11** correctly concede that this device
walk is the *only* real verification of the change; its first step therefore
asks the human to confirm a state the firmware cannot produce, so a perfectly
correct implementation reads as a failure — and the one thing step 1 is supposed
to prove (that gating entry #2 removed exactly one row and nothing else) goes
unchecked.

**Evidence.**

```
src/activities/settings/SettingsActivity.cpp:58-62
     58:     } else if (setting.category == StrId::STR_CAT_READER) {
     59:       // Settings merged into "Text Settings"
     60:       // (they stay in the shared list for the web settings API)
     61:       if (setting.inTextSettings) continue;
     62:       readerSettings.push_back(setting);
```

`STR_HYPHENATION` is `src/SettingsList.h:314-316`, `.withTextSettings()` on
`:316`. `STR_EXTRA_SPACING` is `:321-323`, `.withTextSettings()` on `:323`.
`STR_ORIENTATION` (`:317-320`) is the only one of the three with no
`.withTextSettings()`, which is exactly why it shows on that screen.

The Reader-category rows that survive `:61` are `STR_ORIENTATION` (`:317-320`),
`STR_IMAGES` (`:330-332`) and `STR_NIGHT_MODE` (`:337-338`); the screen is then
framed by three ACTION entries inserted at
`src/activities/settings/SettingsActivity.cpp:86-90` — `STR_TEXT_SETTINGS`,
`STR_MANAGE_FONTS` and `STR_CUSTOMISE_STATUS_BAR`. So the live screen is six
rows, becoming five, on an 800×480 panel — "and the category still scrolls" is
also not a check that can pass.

**Fix.** Rewrite §7.3 step 1 against the real screen:

> Settings → Reader: the list reads **Text Settings, Manage fonts, Images, Night
> mode, Customise status bar** — five rows, with **no Orientation row** between
> Manage fonts and Images. Open Text Settings and confirm Hyphenation and Extra
> spacing are still there (they live in that sub-screen, not in Reader).

---

## MINOR 1 — **A2**'s "adds no new limitation" is true for the firmware build and false for the host build

**Claim.** **A2**: "This adds no new limitation: `src/network/FirmwareBoardTag.cpp:16`
already `#error`s any build without `FREEINK_DEVICE_X4PRO`, so the repo cannot
build for another board today."

**Problem.** The two `#error`s do not have the same blast radius.
`FirmwareBoardTag.cpp` is a single leaf translation unit that nothing includes;
`src/CrossPointSettings.h` — where **A3** puts the new derivation — is the
settings-singleton header included by most of `src`, and it is also precisely
the header a future host test would have to include. The host suite is built by
CMake with no `FREEINK_DEVICE_*` define anywhere, so the day someone acts on
**A11** ("making one possible means compiling `SettingsList.h` on the host") the
first thing they hit is this `#error`, not a missing Arduino shim.

Nothing breaks today — verified, no host test reaches the header:

```
$ grep -rl "CrossPointSettings" test/ | wc -l
0
$ grep -rn "FREEINK_DEVICE" test/
(no output)
```

`test/CMakeLists.txt:41-45` sets only `-Wall -Wextra -pedantic` on
`crosspoint_test_common`; no `-Wundef`, so an undefined `FREEINK_DEVICE_X4PRO`
in `#if` would silently be 0 and fall into the `#error` branch.

**Fix.** Amend **A2** to say what it actually does: the `#error` moves from one
leaf `.cpp` to a widely included header, which is accepted because the firmware
build defines the flag in both envs (`platformio.ini:166,184`) and no host TU
includes the header today. Add one sentence naming the remedy for whoever lands
the **A11** host test — add `FREEINK_DEVICE_X4PRO=1` to that test's
`target_compile_definitions`, as `test/verse_anchors/CMakeLists.txt:18` does for
`XML_GE`/`XML_CONTEXT_BYTES`.

---

## MINOR 2 — **A6**'s dead-code inventory is short, and "a two-line follow-up" is wrong

**Claim.** **A6**: "The reader menu's rotation popup (`EpubReaderMenuActivity.cpp:104-117`)
and `orientationLabels` (`EpubReaderMenuActivity.h:81-82`) stay. … removing it is
a two-line follow-up."

**Problem.** A third site goes dead and is not listed: the live-value refresh in
`buildScreen`.

```
src/activities/reader/EpubReaderMenuActivity.cpp:198-201
    198:   for (size_t i = 0; i < rowCount; i++) {
    199:     const auto action = menuItems[i].action;
    200:     if (action == MenuAction::ROTATE_SCREEN) {
    201:       menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
```

It is a search over `menuItems`, so with the row gated out the branch simply
never fires — no bug, but it is dead, and it is the reason `orientationLabels`
and `pendingOrientation` cannot be removed independently. Removing the feature
properly also reaches `pendingOrientation` (`.h:79`, `.cpp:25`), the
`currentOrientation` constructor parameter (`.h:35`, `.cpp:19,25`, caller
`src/activities/reader/EpubReaderActivity.cpp:282`), `MenuResult`'s orientation
field (`.cpp:86,145`) and the result-path guard
(`EpubReaderActivity.cpp:286-288`). That is a handful of files, not two lines,
which weakens the stated reason for deferring it — the deferral still holds on
the concurrency argument alone.

**Fix.** In **A6**, add `EpubReaderMenuActivity.cpp:198-201` to the
"dead but compiled" list, and replace "a two-line follow-up" with the real
surface (`activateIndex` branch, `buildScreen` branch, `orientationLabels`,
`pendingOrientation`, the ctor parameter, `MenuResult`'s field and
`EpubReaderActivity.cpp:286-288`). Keep the decision.

---

## MINOR 3 — **A2**'s stated reason argues against an option it did not reject

**Claim.** **A2**: "Chosen over 'absent means rotation stays' because
`platformio.ini` carries no `-Wundef` and no `-Werror`, so an undefined macro in
`#if` evaluates to 0 with no diagnostic — a default would be silent either way,
and the wrong polarity would strip rotation from a board that wanted it."

**Problem.** The harm the sentence names is the harm of defaulting to **0**, but
the alternative it says it rejected is defaulting to **1** ("rotation stays"),
which cannot strip rotation from anyone. And the `-Wundef` point does not apply
to `BEREAN_CAP_ROTATION` at all: the `#ifndef` wrapper in §4.1 means that macro
is never undefined at the `#if`. The macro that can be silently 0 is
`FREEINK_DEVICE_X4PRO`, and under the chosen design that silence lands in the
`#error` — i.e. loud — which is the actual argument for `#error` and is stronger
than the one written. Verified: `grep -n "\-W" platformio.ini` returns only
`-Wno-bidi-chars` (`:70`) and the linker `--wrap` line (`:71`).

**Fix.** Restate **A2** as: an unknown device set is a question, not a value; the
`#ifndef` guard already lets a build env answer it explicitly, so the fallthrough
should fail loudly rather than guess, and a missing `FREEINK_DEVICE_X4PRO`
(silently 0, since there is no `-Wundef`) then surfaces as the `#error` instead
of as a silent capability flip. Drop the "wrong polarity" clause or attach it to
the default-0 option it actually describes.

---

## Attacked and survived — recorded so the human knows these were tested

- **A5 (`orientation` leaves the persistence schema) is sounder than the spec
  claims, and needs no human adjudication.** It is not a novel behaviour for
  this file: the existing erase block at `src/SettingsList.h:459-467` already
  drops `fadingFix`, `frontButtonFollowOrientation` and `backShortToFileBrowser`
  from the schema on every `hasTouch()` board — which the X4 Pro is — and those
  three keys appear nowhere else in `src`, so they are *already* unpersisted and
  pinned to their struct defaults on this device while still being read
  (`src/main.cpp:610` reads `SETTINGS.fadingFix`). The brief's own prescribed
  remedy ("extend the existing erase block at `SettingsList.h:459-467`") produces
  exactly the same un-persisting, so the constraint "leave the persisted
  `SETTINGS.orientation` alone" is in conflict with the brief's own instruction,
  not with this design. Adding the precedent to **A5** would strengthen it; no
  decision needs revisiting.
- **A7 (criterion 2 met by the existing load clamp) verified end to end.**
  `src/CrossPointSettings.cpp:158` reads the struct default before overwriting,
  `:161` clamps an ENUM to `info.enumValues.size()`, `:170` stores it;
  `longPressButtonBehavior = OFF` is `src/CrossPointSettings.h:278`. A shortened
  two-value list therefore folds a persisted `2` to `OFF`. The other two routes
  to `2` are also closed: `SettingsActivity.cpp:273` cycles `% size`, and the web
  write bounds-checks at `src/network/CrossPointWebServer.cpp:1281-1283`. The
  handler at `EpubReaderActivity.cpp:625-632` is genuinely unreachable.
- **A8 (trailing value, no index remapping) verified.** `ORIENTATION_CHANGE = 2`
  is the last member before the count sentinel
  (`src/CrossPointSettings.h:177-182`), so displayed position still equals raw
  value and `buildLongPressMenuSetting`'s getter/setter apparatus
  (`src/SettingsList.h:192-217`, rationale at `:138-148`) is correctly not
  copied.
- **A9 (no i18n change, no new orphans) verified, including the trap.**
  `scripts/settings_snapshot.py:12` is a regex over source text and
  `scripts/i18n_orphans.sh:9-10` a `grep -rq "\b${key}\b"`, so `#if` preserves
  every token. Baselines re-measured here and they match §7.1.3 exactly:
  `python3 scripts/settings_snapshot.py | wc -l` → `133`;
  `./scripts/i18n_orphans.sh` → 22 lines, exit 0, none orientation-related.
  `orientationLabels` uses `STR_INVERTED`, not `STR_ORIENTATION_INVERTED`
  (`EpubReaderMenuActivity.h:81`), so the spec's split of which key is
  referenced where is right.
- **§5.3 (result path stays coherent) verified.** `pendingOrientation` is
  assigned only in the popup callback (`EpubReaderMenuActivity.cpp:107`), which
  `activateIndex` can no longer reach, so `menu.orientation ==
  SETTINGS.orientation` and `EpubReaderActivity.cpp:286` never fires.
- **§4.2 #1's mechanics verified.** `MAX_MENU_ITEMS` is a capacity, and both the
  refresh loop (`:193,198`) and the widget count (`:213`) clamp to it, so
  dropping a row needs nothing else. `onReaderMenuConfirm`'s switch already omits
  `ROTATE_SCREEN` today, and `platformio.ini` sets no `-Werror`, so no
  `-Wswitch` fallout.
- **No fourth UI entry point exists.** `grep -rni "rotate\|rotation" src` and
  `grep -rn "setOrientation(" src lib` turn up only the three gated surfaces plus
  renderer/theme internals that force `Portrait` and restore
  (`BaseTheme.cpp:177,202`, `LyraTheme.cpp:306,339`,
  `RoundedRaffTheme.cpp:338,383`, `SleepActivity.cpp:517,527`,
  `ReaderActivity.cpp:67`). There is no `data/` directory in this repo, and
  `src/network/html/SettingsPage.html` renders from `/api/settings`, so no
  hand-written web control orphans when the key leaves the list.
- **Include reachability verified.** `src/SettingsList.h:15` and
  `src/activities/reader/EpubReaderMenuActivity.cpp:10` both include
  `CrossPointSettings.h` directly, so §4.1's "no new file, no new include" holds.
- **Idiom precedents verified.** `#ifndef FREEINK_CAP_TOUCH` + device-set
  derivation is `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:175-179`;
  the `#error`-on-unhandled-device half is `src/network/FirmwareBoardTag.cpp:13-17`;
  the two-variants-of-one-enum-row form is `src/SettingsList.h:352-362`; the
  popup-vs-cycle threshold is `SettingsActivity.cpp:260,273` and the
  out-of-range render guard `:412-413`. All as described.
- **A1's premise verified.** `.gitmodules` points `freeink-sdk` at
  `https://github.com/Free-Ink/freeink-sdk.git`; `git submodule status` shows
  `310ec61506fc915836db7799a2e7f4fc135a570d` (the commit is pinned by the
  gitlink, not by `.gitmodules` — a citation nit, not a finding). The predicate
  roster at `BoardConfig.h:1614-1695` contains nothing about orientation
  (it also has `hasImu()` and `hasLeds()` at `:1694-1695`, which the spec's range
  omits). Criterion 3's literal form is genuinely out of reach.

No build was run: no finding above depends on one, and the brief records a
passing baseline on this branch.

VERDICT: CLEAR
