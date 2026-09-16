# Issue #37 — PR #45 stage-2 review: code quality

Reviewed `fix/37-gate-screen-rotation` (`gh pr diff 45`) against the code it
sits in. Stage 1 (`issue-37-pr-review-intent-0.md`) already ruled on intent and
acceptance criteria; nothing below re-opens them. The question here is whether
the 27 added lines are written the way this repo already writes this kind of
change.

Source diff under review, in full: `src/CrossPointSettings.h` (+17),
`src/SettingsList.h` (+8), `src/activities/reader/EpubReaderMenuActivity.cpp`
(+2). 0 deletions.

**Two MINORs, no MAJOR, no BLOCKER.** The change mirrors patterns that already
exist in this repo rather than inventing a second way, and both MINORs are
cosmetic or bookkeeping.

---

## What was verified, with evidence

Recorded here because the strongest stage-2 finding is often "this pattern does
not exist" — and in this case it does, in two places, so the claim needed
checking rather than asserting.

**The `#if FREEINK_DEVICE_X4PRO / #else / #error` macro shape is an existing
bereanOS idiom, not a deviation invented here.**
`src/network/FirmwareBoardTag.cpp:13-17` is the same construct, in the same
project, for the same reason (derive a per-board constant from the compiled
device set; refuse to guess for an unknown board):

```cpp
#if FREEINK_DEVICE_X4PRO
#define BEREAN_BOARD_NAME "x4pro"
#else
#error "FirmwareBoardTag: no FREEINK_DEVICE_X4PRO flag set; cannot derive board name"
#endif
```

Like the new block, it relies on `FREEINK_DEVICE_X4PRO` arriving as a `-D`
(`platformio.ini:166, 184`) and does not include `BoardConfig.h` to get it. The
new `#ifndef BEREAN_CAP_ROTATION` wrapper additionally matches the SDK's
override idiom (`BoardConfig.h:174-176` — `#ifndef FREEINK_CAP_TOUCH` /
`#define` / `#endif`, documented at `freeink-sdk/platformio.sample.ini:60` as
"force with `-DFREEINK_CAP_<NAME>=0/1`"). So the macro reads as a blend of the
two existing precedents and introduces no third convention.

**The two-variant row in `SettingsList.h:349-358` is copied in shape from its
sibling five lines below.** `SettingsList.h:360-370` already does exactly this
under `#if FREEINK_CAP_TOUCH` for `STR_SHORT_PWR_BTN` — same `#if` / duplicated
`SettingInfo::Enum` / `#else` / shorter label list / `#endif`, same
no-helper-builder decision. Duplicating the row rather than extracting a builder
is therefore the established local choice, not laziness introduced here.

**Not copying `buildLongPressMenuSetting`'s getter/setter apparatus is correct,
and I checked the enum rather than taking A8's word for it.**
`src/CrossPointSettings.h:194-198` — `OFF = 0, CHAPTER_SKIP = 1,
ORIENTATION_CHANGE = 2, LONG_PRESS_BUTTON_BEHAVIOR_COUNT`. `ORIENTATION_CHANGE`
is the last member, so dropping the trailing label leaves displayed position ==
persisted raw value and no remapping is needed. The getter/setter pair at
`SettingsList.h:192-217` exists only because `LP_MENU_READER_MENU = 4` sits
mid-enum (`CrossPointSettings.h:179`, and the comment at `SettingsList.h:137-149`
says so). Copying it here would have been cargo-culting.
`LONG_PRESS_BUTTON_BEHAVIOR_COUNT` has no users anywhere in `src/` or `lib/`, so
no cycle bound or clamp still believes in three values.

**`SettingsList.h` has a second, runtime hiding mechanism, and the PR was right
not to reach for it.** `SettingsList.h:453-474` erases rows by `nameId` under
`BoardConfig::hasTouch()` / `hasHomeKey()`. That mechanism pre-exists alongside
the compile-time `#if FREEINK_CAP_*` one (`:279`, `:360`, `:400`), so choosing
the compile-time form (spec A1/A4) is picking one of two existing paths, not
adding a third. Worth noting for anyone re-reading the PR body: that same erase
block at `:467-474` already removes `STR_FRONT_BTN_FOLLOW_ORIENTATION` on a
touch board, so the one remaining orientation-shaped Controls row is not left
dangling by this change — it was never offered on the X4 Pro.

**Macro scope is real, not accidental.** `src/SettingsList.h:15` and
`src/activities/reader/EpubReaderMenuActivity.cpp:10` both include
`CrossPointSettings.h`, so `#if BEREAN_CAP_ROTATION` evaluates the real
definition in both TUs rather than silently defaulting to 0. `platformio.ini`
sets no `-Wundef`, so a missing include would have been invisible; it isn't
missing.

**No host-test collateral from the `#error`.** No suite under `test/` references
`CrossPointSettings` (`grep -rn "CrossPointSettings" test/` → no hits) and
`test/CMakeLists.txt` pulls in nothing from `src/`, so the `#error` cannot fire
in the host build.

**No fourth dead site beyond A6's inventory.** `src/activities/settings/` and
`src/network/CrossPointWebServer.cpp` contain no `orientation` special case, and
the only `"orientation"` JSON-key literal in `src/` or `lib/` is the gated row
itself (`SettingsList.h:321`). A6 + A7 account for every site that goes dead.

**Build, format and the two textual gates, run here:**

- `~/.platformio/penv/bin/pio run -e x4pro` → SUCCESS, Flash `5318598` B, RAM
  `64052` B — byte-for-byte the figures the plan predicted, no warning from
  `src/`.
- `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` → exit 0, no
  modifications (`git status --short` clean afterwards).
- `scripts/settings_snapshot.py` → 133 `StrId` tokens, unchanged. Confirmed the
  script is a regex over `StrId::` tokens (`scripts/settings_snapshot.py:11-12`),
  which is precisely why `#if`-gating rather than deleting keeps it flat — A9's
  mechanism holds.

---

## MINOR 1 — the dead reader-menu code is deliberate, but nothing tracks its removal

`src/activities/reader/EpubReaderMenuActivity.cpp:73-75` stops the only
`ROTATE_SCREEN` row from ever entering `menuItems`, which makes three things
unreachable in every buildable configuration of this repo:

- `EpubReaderMenuActivity.cpp:106-118` — the `optionPopup.show(...)` branch in
  `activateIndex`
- `EpubReaderMenuActivity.cpp:202-203` — `menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);`
- `EpubReaderMenuActivity.h:81-82` — `const std::vector<StrId> orientationLabels = {...}`, read only from those two branches

The first two are flash-only. The third is not: it is a non-static
`std::vector` member, so every reader-menu open pays a heap allocation for four
`StrId` values that no live path reads. `CLAUDE.md`'s resource protocol
("justify any new heap allocation", "`constexpr` first") would not accept that
allocation if it were being added today; the PR does not add it, but it does
remove its last reader.

This is a recorded decision, not an oversight: spec A6
(`docs/superpowers/specs/2026-09-16-issue-37-design.md:213-229`) inventories all
three sites and keeps them to hold the diff to one line in a file that #38 and
#27 are editing concurrently, and the plan repeats the instruction verbatim
(`plans/2026-09-16-issue-37-plan.md:433-438`). I agree with the trade — a
mechanical rebase for two sibling branches is worth more than ~10 lines of flash
and a 4-element vector.

What is missing is the expiry. The trade is only valid while #38 and #27 are
open, and the only record that the cleanup is owed is a sentence in a spec
("Attack this if the dead code matters more than the conflict surface"), which
nobody will re-read after those branches merge.

**Fix, no code change in this PR:** state the debt where it will be seen — one
line in the PR body's "Merge-conflict overlap" section naming the three sites as
dead-until-#38-and-#27-land, or a follow-up issue. Do not remove the code on
this branch; that would reverse A6 and re-open the conflict surface the decision
was made to avoid.

## MINOR 2 — one sentence of the new comment is before/after narration with a drifting count

`src/CrossPointSettings.h:8-9`:

```
// Whether screen rotation is OFFERED to the user. The renderer's rotation
// support is untouched; this gates only the three places that exposed it.
```

The first sentence is the comment the merged state needs. The second is written
from the diff's point of view: "untouched" and "exposed" only mean anything to a
reader who knows what the file looked like before, which is exactly what
`CLAUDE.md` rules out — "write them for the merged state, as if the code had
always worked this way. Remove before/after narration". "The three places" also
hard-codes a count in a file that cannot see the three call sites; gating a
fourth surface later (the `/api/settings` path the spec flags at §1, say)
silently makes the comment wrong.

The rest of the block earns its place — the Left/Right-mapping constraint and the
"BoardConfig catches no-device, this catches a *different* device" parenthetical
are both non-obvious *why*, which is the one thing `CLAUDE.md` wants comments
for. Only the one sentence needs to go.

**Fix, inline:** replace that sentence with a statement that holds independently
of the change, e.g.

```cpp
// Whether screen rotation is OFFERED to the user. Independent of the
// renderer's rotation support, which every board keeps.
```

---

## Not findings

Recorded so the next reader does not spend the time twice:

- **`BEREAN_CAP_ROTATION` living in `src/CrossPointSettings.h`** rather than a
  capability header. There is no bereanOS capability header to put it in —
  `BoardConfig.h` is vendored `freeink-sdk` this repo cannot push to, and the
  one existing local precedent (`FirmwareBoardTag.cpp:13-17`) also defines its
  board macro in the file that consumes it. Spec A3 considered the placement.
  With a third such macro it would be worth a home; with two it is not.
- **`STR_ORIENTATION_INVERTED`, `STR_LANDSCAPE_CCW` and
  `STR_LONG_PRESS_BEHAVIOR_ORIENTATION` are now referenced only from
  preprocessor-dead text**, so `scripts/i18n_orphans.sh` will never flag them.
  That is A9 working as designed (the gate is a text scan) and costs nothing:
  `lib/I18n` generates its tables from YAML regardless of use, so no key is
  dropped and no flash is wasted that was not already spent.
- **Commit granularity and messages** — one conventional commit per gated
  surface, `feat:` for the capability and `fix:` for each gate, and a
  conventional PR title (`fix: gate screen rotation behind a portrait-only
  capability`) against `main`, which is what release-please reads on squash.

---

No source file was modified by this review. `git status --short` is clean, and
no `compile_commands.json` was left behind.

VERDICT: CLEAR
