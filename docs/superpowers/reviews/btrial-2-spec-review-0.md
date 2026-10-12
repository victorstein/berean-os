Tier: standard

# btrial-2 spec review 0

Reviewed: `docs/superpowers/specs/2026-10-12-btrial-2-design.md` (at `3ba969d3`), against the brief
(`hpipe brief --task t2`), the research note `docs/superpowers/research/2026-10-12-btrial-2-research.md`,
and the batch spec `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (§2, §3, §4).

## What was verified and holds

- **Flash mechanism and the A5 slot-copy design.** `button()` registers the hit with `props.state`
  (`button.h:42`), resolves the drawn style through `frame.stateFor` (`button.h:47-49`), and
  `StyleSet::resolve` picks Disabled > Active > Focused > Selected > Normal (`FreeInkUICore.h:606-617`).
  `stateFor` ORs `StateFocused` for the tap-flash target, keyed by `(action, value)` (`FreeInkUICore.h:1039-1043`).
  Copying one `look` into all five slots makes every resolution paint the chip's own look. That
  matches the batch spec's flash rule (§2 Rules). For Active it gives the same pixels as today
  (`TagChipView.cpp:84-89`).
- **Text weight is outside the style.** The label is drawn with `props.text` recoloured to the
  resolved foreground (`button.h:77-80`), so a flash cannot change the weight. A3 and the bold column
  are therefore safe.
- **Disabled stays routable.** `findTouch`, `findFirst` and `focusable` skip only `StateDisabled`
  (`FreeInkUICore.h:1159-1170`, `:1187`). The spec never sets it, and `enabled` stays true, so
  `disabled` is never resolved (`button.h:47`).
- **The dither fill works on this target.** `Paint::dither` exists (`FreeInkUICore.h:512-517`). With a
  non-zero radius, `FreeInkUIGfxRenderer::fill` routes `Dither` to `fillRoundedRect`
  (`FreeInkUIGfxRenderer.h:73-80`). A zero `borderWidth` with `Paint::none()` skips the stroke
  (`button.h:51`).
- **Every switch over `Kind`.** `grep -rnE "Kind::(All|Tag|Unlabelled)\b|TagChips::Kind" src lib test`
  finds only two `switch`es, both in `TagChipView.cpp` (`:27-40`, `:46-54`). `-Wall` is added to repo
  sources (`scripts/enable_repo_warnings.py:27-30`) and enables `-Wswitch`, so the spec's two new
  cases are required. They are also enough. The three `!= Kind::All` / `!= Kind::Tag` tests
  (`HighlightsActivity.cpp:156`, `TagFilterActivity.cpp:161`, `:290`) never see a `NewTag`, because
  `buildEntries` drops it (A12).
- **Call sites.** `TagChipView::draw` has exactly two callers (`HighlightsActivity.cpp:707`,
  `TagFilterActivity.cpp:100`). `EpubReaderMenuActivity.cpp:436-448` builds its own `StyleSet`.
- **A7.** No handler in `src/` reads `event.state`: `grep -rnE "(event|ev|e)\.state\b" src` is empty.
  `ActionEvent` carries the state (`FreeInkUICore.h:1219-1222`), but no reader uses it, so keeping
  `StateSelected` for Active is inert.
- **A9 holds.** The flash is armed after a Highlights tag-chip tap. Nothing on that path calls
  `clearTapFlash` (`HighlightsActivity.cpp:136-171`), and the flash is keyed by
  `(ACTION_CHIP, placed.chip)` (`:707-708`). `candidates` drops a zero-count tag once it stops being the
  filter (`TagChipRow.h:108`). The spec's single shifted-index case is real and is the only one: the
  Highlights chip scope never depends on the filter (`HighlightsActivity.cpp:132`).
- **`pickerState` shape.** `MAX_TAGS_PER_PASSAGE` is `size_t` (`PassageDoc.h:45`), and so is
  `selectedIds_.size()` (`TagPickerActivity.h:95`). The `(kind, selected, size_t, size_t)` signature
  and the table match batch §4 and today's check at `TagPickerActivity.cpp:134`.
- **No identifier collisions.** `grep -rn "ChipState\|findChip\|pickerState" src lib test freeink-sdk/libs`
  is empty. `NewTag` appears only in a method name and a test name, never as an enumerator.
- **The string.** `gen_i18n.py` runs with `strip_unused=True` from PlatformIO (`scripts/gen_i18n.py:1001`).
  It drops unused keys and prints a notice, but does not fail (`:916-922`). `scripts/i18n_orphans.sh`
  is not wired into CI. No sibling worktree (`feature-header-action-seam`, `feature-tag-order-rank`)
  edits `english.yaml`. `feature/tag-picker-grid`, which deletes the adjacent `STR_TAG_NEW`, consumes
  this task's output, so it lands after it and the two adjacent edits do not race.
- **Baseline artefacts exist.** `.pio/build/x4pro/firmware.bin` is 5,559,504 B (17:01 today), which
  agrees with the spec's 5,558,994 B app figure.

No BLOCKER or MAJOR found.

## MINOR

### m1. The name-order `candidates` test cannot fail, and the TDD claim does not cover it

- **Claim (spec :221-222, :240-242).** Each test "is written first and fails to compile". The
  candidates test gives `{NAME, HOPE, MINISTRY}` and checks for All, the slots in order, then
  Unlabelled.
- **Problem.**
  - `candidates` never reads an id's value except in `filter != activeIds[slot]`
    (`TagChipRow.h:107-110`). It emits slots `0..n-1` whatever ids it is given.
  - With a hand-built `Counts` (the file's `countsOf` helper) and no filter, the test is the existing
    `TagChipCandidates.AllThenTagsInActiveOrderThenUnlabelled` (`TagChipRowTest.cpp:342-351`) with the
    ids relabelled.
  - It compiles and passes before any code changes, so the TDD statement is wrong for it. Worse, it
    would still pass if name order broke anywhere that matters.
- **Evidence.** `TagChipRow.h:100-114`, and `TagChipRowTest.cpp:342-351`, which already covers the same
  shape.
- **Fix.**
  - Build the counts with `TagChips::count(passages, nameOrdered)` so the Tally's raw-id sort and slot
    mapping run on a non-raw order (`TagChipRow.h:32-53`).
  - Make the filter a zero-count tag in the middle of the name order, and assert it keeps its
    name-order slot between All and Unlabelled.
  - Say in the testing strategy that this is a characterisation test that passes on first run. Only the
    `pickerState`, `findChip` and `NewTag` tests fail first.

### m2. The extra `BoxStyle look` is about 64 B on target, not "a few dozen", and it is avoidable

- **Claim (spec :153-155).** The new `BoxStyle look` is "one more local of a few dozen bytes".
- **Problem.**
  - On the 32-bit target, `Paint` is about 20 B, because it embeds a `BitmapFill`
    (`FreeInkUICore.h:350-360`, `:378-381`, `:498-501`). That makes a `BoxStyle` about 64 B. On the host
    it measured `BoxStyle 104`, `StyleSet 528`, `ButtonProps 624` (a `sizeof` probe compiled against the
    FreeInkUI headers).
  - `draw` already holds a `StyleSet` and a `ButtonProps` (about 324 B and 400 B on target), far over
    the CLAUDE.md 256-byte local guideline, on the 8,192 B render stack (`ActivityManager.cpp:31-32`).
    `button()` adds another `StyleSet` copy (`button.h:45`).
  - The extra local is harmless, but it is not needed.
- **Fix.** Write the per-state look straight into `styles.normal`. Then assign
  `styles.selected = styles.focused = styles.active = styles.disabled = styles.normal`. That gives the
  same result with no new local. Correct the size wording to match.

### m3. A9 understates today's defect, and no device step exercises it

- **Claim (spec :202-207).** The misplaced flash paints an innocent chip inverted "for one repaint".
- **Problem.**
  - The flash is drawn into the repaint that shows the tap's result. It is cleared only on the next
    `render()` (`FreeInkApp.h:681-687`), and "the panel keeps showing it until the next real refresh"
    (same comment). On e-ink, today's second inverted chip therefore stays on screen until something
    else repaints.
  - The device checks (:258-259) never reach this case, so the one acknowledged departure from the
    brief's "look exactly as before" goes unobserved by the tester.
- **Fix.**
  - Reword A9 to "until the next repaint".
  - Optionally add a device step that reaches the case:
    1. Filter Highlights by tag X.
    2. Untag X's last passage through the picker, so X stays as a zero-count filter chip.
    3. Tap a tag after X.
    4. Expect only that tag inverted.

VERDICT: CLEAR
