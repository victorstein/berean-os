# Issue #60 — spec review 1

Target: `docs/superpowers/specs/2026-09-17-issue-60-design.md` (pass 1, `a5ef2c2d`)
Against: `gh issue view 60`, `docs/superpowers/research/2026-09-17-issue-60-research.md`,
`docs/superpowers/reviews/issue-60-spec-review-0.md`
Date: 2026-09-17 · Branch: `fix/60-input-layer-traps` · Worktree clean (`git status --short` empty).

## What pass 0 asked for, re-verified against source

All seven pass-0 findings were re-checked on the code, not on the spec's word for it.

| Pass-0 finding | Applied? | Check |
|---|---|---|
| BLOCKER 1 (touch cannot reach chapter skip) | Yes, and correctly | `EpubReaderActivity.cpp:451-459` consumes the long press first; `isInMenuZone` is the centre third in both axes (`ReaderUtils.h:131-137`) and the page-turn zones are the outer horizontal thirds, full height (`ReaderUtils.h:108-112`) — disjoint. `TOUCH_LONG_PRESS_MS = 500` (`InputManager.h:399`), `suppressTouchContact()` at `MappedInputManager.cpp:161`. Swipe mode never assigns `heldMs` (`ReaderUtils.h:84-95`). §1a's table is right. |
| MAJOR 2 (`ReaderActivity.cpp:144-146` dead) | Yes | `grep -rn "ReaderActivity::loop" src lib` → only the two definitions (`ReaderActivity.cpp:131`, `EpubReaderActivity.cpp:368`). Research note rows struck at `research:19-20`. |
| MAJOR 3 (red-first cannot redden) | Yes | The replacement assertion is absolute. Driving the machine to release at 701 ms yields `NavEvent::Page` while `HOLD_MS = 850` (`NavKeyGestures.h:41`, resolve at `NavKeyGestures.cpp:29`); with `HOLD_MS` lowered to 700 the same release resolves to `Synth` and the new `EXPECT_EQ` fails on its own assertion. Duplicated bullets dropped. |
| MAJOR 4 (assert aborts a shipped device) | Yes | Replaced with `LOG_ERR` + `finish()`. `grep -n NDEBUG platformio.ini` → empty (exit 1); flag blocks confirmed at `:164-176` and `:182-190`. The precedent shape is real: `ReaderActivity.cpp:41-48`, and `ActivityManager.cpp:162-165` explicitly tolerates a pending action raised from `onEnter`. Both exits (`ButtonRemapActivity.cpp:48-57`, `:59-63`) confirmed, and the "only exit" claim is struck in §5b. |
| MINOR 5 (i18n failure mode inverted) | Yes in the spec | `gen_i18n.py:267` regex and the `CRITICAL … sys.exit(1)` at `:873-881` are exactly as described; `pre:scripts/gen_i18n.py` is `platformio.ini:131`. Run on this tree: **32 languages, 420 keys, 1 unused** — §7b's number reproduces. (Not corrected in the research note — MINOR 4 below.) |
| MINOR 6 (label neighbour) | Moot, correctly | No label is introduced. |
| MINOR 7 (citation drift) | **Partly — and new drift introduced** | MINOR 3 below. |

The `d8e92208` template is real and the spec reads it accurately on structure:
`BEREAN_CAP_ROTATION` at `CrossPointSettings.h:17-23`, the `#if` pair at
`SettingsList.h:349-358`, the reader-menu row at `EpubReaderMenuActivity.cpp:73-75`.
Wrapping the whole `SettingInfo::Enum` entry is structurally safe — the list is a
`std::vector<SettingInfo>` initializer (`SettingsList.h:254`), not a fixed-size array.
Finding 2's dead arms re-confirm (`BoardConfig.h:1396` + `:406`,
`InputManager.cpp:246,257-258`, `HalGPIO.cpp:186-205`), and no current board profile
has `left`/`right` wired with `back`/`confirm` unassigned, so A11's four-pin predicate
produces no false refusal on any board in `BoardConfig.h` today.

---

## BLOCKER 1 — the design proceeds on a scope call pass 0 escalated to the human and nobody answered

**Claim.** §0: "**One unratified scope call.** … the call was misrouted … no human
answered it before this phase re-ran. This spec therefore proceeds on the
recommendation as written." A1: "**UNRATIFIED SCOPE CALL. Removing the setting is
preferred to rewording it.** … **This removes a user-facing setting, which is more
than #60 authorised.**"

**Problem.** Review 0's BLOCKER said in terms: "This is a scope decision and needs the
human, not an inline edit," and listed three options. The spec picked option 1, flagged
it honestly, and moved on. Honest flagging is not ratification. Everything in commit 1
— the new capability macro, the `#if`, §5a, §7c.1, and the host test's stated purpose —
is downstream of a choice the human has not made, and the two branches are not small
variants of each other: one deletes a row from Settings → Controls on every unit in the
field, the other edits a string. A review that returns CLEAR here authorises an
implementer to remove a shipped user-facing setting on a spec that says on its face
that nobody approved removing it.

**Evidence.**

- `gh issue view 60` offers exactly two remedies: "Either reword it, or route the
  synthesised path so a held nav key reports its real duration." Removal is neither.
- No decision artifact exists: `docs/superpowers/` has `notes/ plans/ research/
  reviews/ specs/` and no decisions directory; `gh issue view 60` reports
  `comments: 0`.
- The spec's own §0 and A1 say the `hpipe decide` landed on a different run's completed
  `#38` task.

**Additionally, the fallback is underpriced, and the human needs its real cost to
choose.** A1 says "If the human prefers (b), §4a becomes a one-line English string
change." It does not. `STR_LONG_PRESS_BEHAVIOR` carries a *translated* value in all 32
YAMLs (`grep -l STR_LONG_PRESS_BEHAVIOR: lib/I18n/translations/*.yaml | wc -l` → 32;
`spanish.yaml:89` — "Al mantener pulsado un botón"; `german.yaml:71` — "Verhalten bei
langem Tastendruck"; `french.yaml:86`). A one-line edit to `english.yaml:93` leaves 31
languages naming buttons, which is the exact defect being fixed, for 31 of 32 users.
The honest cost of (b) is either 32 YAML edits — in a directory `.claude/agents/ui-dev.md:22-27`
marks report-do-not-edit — or the `02d9106a` move the research note already records
(`research:191-196`): reuse an existing key that is already translated everywhere.

**Concrete fix.** Put the choice to the human before the plan phase, with the corrected
costs on both branches:
(1) gate and remove the row (§4a as written, plus MAJOR 2's correction);
(2) reword — and price it as a 32-file translation change or an existing-key reuse, not
one line;
(3) reword-and-document.
§4b, §7a and commit 2 are independent of the ruling and can be planned now either way.

---

## MAJOR 2 — "the runtime path is untouched" and "the persisted byte is kept" are both false, and the template commit says so in its own body

**Claim.** §5a: "The runtime path is untouched. `SETTINGS.longPressButtonBehavior`
keeps its persisted byte and both consumers; only the settings row disappears." §5a
again: "**A stale byte is harmless.** A settings file carrying `CHAPTER_SKIP` from an
earlier build keeps it after the row disappears. The effect is `usePress == false` —
release-triggered paging." §6 table: "A user's saved `longPressButtonBehavior` byte |
**Untouched. No migration, no write**". A6: "The persisted key and member are not
renamed, and no migration runs." §3 non-goal: "**Removing** the reader's chapter-skip
branches, **the persisted byte**, or the `usePress` derivation."

**Problem.** `getSettingsList()` *is* the persistence schema. Gating the entry removes
the key from both directions at once:

- `fromJson` never reads `"longPressButtonBehavior"`, so the member keeps its
  struct-initializer default `OFF` (`CrossPointSettings.h:295`) no matter what the file
  says. The effect is `usePress == **true**` — press-triggered — the exact opposite of
  what §5a states.
- `toJson` never writes the key, so the **first settings save after the upgrade deletes
  it from the file**. Entering and leaving the remap row is itself such a save
  (`SettingsActivity.cpp:305`). "No write" is wrong; the byte is not kept, it is
  dropped.
- The row also leaves the web settings API, which enumerates the same list
  (`CrossPointWebServer.cpp:1159,1263`).
- So §3's non-goal "removing … the persisted byte" is violated by the change §4a
  prescribes, and `EpubReaderActivity.cpp:613`'s `CHAPTER_SKIP` branch becomes
  unconditionally dead rather than merely unreachable.

The conclusion — that this is benign on this board — does survive, but only by the
argument §1a makes for press-vs-release equivalence, which I re-derived: `PageBack` /
`PageForward` → `BTN_UP`/`BTN_DOWN`, whose synthetic press and release land on the same
tick (`HalGPIO.cpp:207-225`); `Button::Left`/`Right` → `frontButtonLeft`/`Right` →
`BTN_LEFT`/`BTN_RIGHT`, dead pins; `wasReleased(Power)` is outside the `usePress`
ternary (`ReaderUtils.h:62-66`). `EndOfBookOptions.cpp:137-140` uses `NavPrevious` /
`NavNext`, which compose to the same four buttons (`MappedInputManager.cpp:104-112`).
So the *outcome* is inert either way — but a design section that states the mechanism
backwards is the same defect pass 0 raised as MINOR 5, and here it is load-bearing for
§6 and A6.

**Evidence.**

- `src/CrossPointSettings.cpp:66` (`toJson`) and `:113` (`fromJson`) both
  `for (const auto& info : getSettingsList())`; `"longPressButtonBehavior"` has no
  manual line beside the ones that do (`:84-100` for `frontButton*`, `fontFamily`,
  `longPressMenuFunction`, `language`).
- `grep -rn "longPressButtonBehavior" src lib` → the only reader/writer of the key is
  that loop; the member declaration is `CrossPointSettings.h:295` (`= OFF`).
- **The template commit documents exactly this**, under "Two consequences worth
  knowing": "**`orientation` leaves the persistence schema.** `getSettingsList()` is
  what `toJson`/`fromJson` iterate (`src/CrossPointSettings.cpp:66,113`), so gating the
  row drops the key. That is intended" (`git show d8e92208`). §4a calls `d8e92208` "a
  line-for-line template" and then asserts the opposite of the one consequence its
  author wrote down.
- Nothing in §7b can catch this: `scripts/settings_snapshot.py:11-12` is a regex over
  `SettingsList.h` text, so gated `StrId::` tokens still count and the snapshot shows no
  diff — which is precisely why `d8e92208` had to state the consequence in prose.

**Concrete fix.** Rewrite §5a's "A stale byte is harmless" paragraph, the §6 table row
and A6 to say what happens: the key leaves the persistence schema, a saved
`CHAPTER_SKIP` stops being read (member default `OFF`, so `usePress == true`) and is
deleted from `/.crosspoint/`'s settings JSON on the next save; this is benign here
because press and release are the same event for every reachable button, and it is what
`d8e92208` did on purpose for `orientation`. Amend §3's non-goal, which currently
claims the persisted byte is not removed. Add the file-level check to §7c.2 ("after
changing any setting, `longPressButtonBehavior` is gone from the settings file"). Since
this is a persistence-schema change, note that `src/CrossPointSettings.*` and
`src/SettingsList.h` are `data-dev`'s surface, not `ui`'s — §9's scope note only
accounts for `MappedInputManager.cpp`.

---

## MINOR 3 — a pass that claims "citations corrected throughout" introduces three new off-by-N citations

**Claim.** §0: "MINOR 7 | Citation drift (three) | Yes | Corrected throughout."

**Problem.** Three citations in pass 1 are wrong, one of them a *regression* from pass 0's
correct number, in a spec whose own standard (`CLAUDE.md`) is file-and-line.

**Evidence.**

- §1a, §3, A2 and the §1a code block all cite the passage-selection gesture as
  `EpubReaderActivity.cpp:450-457`. The block is `:451-459`; the quoted `if` is
  `:454-458`; `:450` is the tail of a comment. Review 0 cited `:451-459` correctly, so
  this moved the wrong way.
- §7c.1 cites `STR_FRONT_BTN_FOLLOW_ORIENTATION` at `SettingsList.h:346`. It is `:347`.
- §4a cites the template's reader-menu row at `EpubReaderMenuActivity.cpp:72-74`. It is
  `:73-75` (`:72` closes the `Frontlight.present()` block).
- §1a says "the resolve is line 28" of `NavKeyGestures.cpp`. `:28` is `if (!wasStale) {`;
  the resolve is `:29`. (Inherited from review 0, which had it wrong too.)

**Concrete fix.** Correct the four. Everything else I spot-checked holds:
`ReaderUtils.h:53`, `:84-95`, `:108-112`, `:120`, `:131-137`; `EpubReaderActivity.cpp:605-606,613,619`;
`HalGPIO.cpp:181-182,186-205,194-201,198-201,210-225,236-239`; `NavKeyGestures.h:41,48,68`;
`NavKeyGestures.cpp:60-62`; `BoardConfig.h:406,1396,1411,1622`;
`MappedInputManager.cpp:155-163,378-393`; `ButtonRemapActivity.cpp:13,48-57,59-63,71-74`;
`SettingsActivity.cpp:75-78,308-310`; `CrossPointSettings.h:17-23`;
`platformio.ini:131,164-176,182-190`; `gen_i18n.py:267,873-881`;
`NavKeyGesturesTest.cpp:44-55,132-139,141-144`.

---

## MINOR 4 — the research note's corrections are half-applied, and §9 lists a correction that already landed

**Claim.** §0: "Citations corrected here **and in the research note** (`git show` on the
same commit as this spec)." §9 files-touched: `docs/…/2026-09-17-issue-60-research.md` —
"correct the `ReaderActivity.cpp:144-146` rows (MAJOR 2)".

**Problem.** Two things. The research-note edit already landed in `a5ef2c2d` (the same
commit as this spec), so §9 hands the implementer a row of work that is done — and if
taken literally, invites a second edit to a file the spec otherwise says nothing about.
And the edit was partial: three statements the two reviews disproved are still standing
in the note, where a plan or implementer phase will read them.

**Evidence.**

- `git show --stat a5ef2c2d` → `research/2026-09-17-issue-60-research.md | 25 +-`; the
  MAJOR 2 rows are struck at `research:19-20`.
- Still uncorrected in that note:
  - `research:23` — ownership table row "**The touch path that still works** |
    `src/activities/reader/ReaderUtils.h:120`", contradicted by the note's own
    correction 100 lines later and by BLOCKER 1 of review 0.
  - `research:155-157` — "a retired `STR_*` name left behind in a comment still counts
    as used, **so the unused-key report will not catch it**" — the mechanism review 0's
    MINOR 5 disproved and the spec corrected in §7b. The build fails loudly at
    `gen_i18n.py:873-881`; it is not silent.
  - `research:236-237` — "Finding 1's preferred shape (see spec) touches only
    `src/SettingsList.h` and `lib/I18n/translations/`", which contradicts §3, §4a and §9
    (no translation file is touched; `src/CrossPointSettings.h` is).
  - `research:36` — `NavKeyGestures.cpp:22-31` for `updateKey`, the same drift MINOR 7
    corrected in the spec.

**Concrete fix.** Drop the research-note row from §9 (say instead that the note was
corrected in this spec's own commit), and finish the note: relabel the `:23` row, fix
`:155-157` to the exit-1 mechanism, update `:236-237` to the current file list, and
correct `:36`.

---

## Verdict rationale

Pass 1's substantive work is sound: the BLOCKER's premise was re-derived correctly and
the design reversed itself rather than defending the reword, the assert became a
log-and-pop with the right precedent, and the host test now has an assertion that can
go red for its own reason. MAJOR 2 is a real error but textual — the chosen shape stays,
three paragraphs need rewriting to match what `getSettingsList()`-driven persistence
actually does, which the template commit already spelled out. What gates the pipeline is
BLOCKER 1: the spec removes a user-facing setting on a scope call that pass 0 sent to
the human, that the human never received, and that issue #60 does not authorise — and
the fallback the human would be choosing against is mispriced by 31 translation files.
That ruling is not an inline edit, and it decides the whole of commit 1.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 1
