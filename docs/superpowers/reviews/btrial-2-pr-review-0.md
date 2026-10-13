Tier: standard

# PR review 0: btrial-2, tag chip states and the pure chip helpers

Range: `origin/batch/20261012-tag-chip-picker-2l3m...origin/feature/tag-chip-states` (head `68ce7058`).
Against: the brief (`hpipe brief --task t2`), the batch spec
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` §2, §3, §4, the task spec
`docs/superpowers/specs/2026-10-12-btrial-2-design.md`, and the plan
`docs/superpowers/plans/2026-10-12-btrial-2-plan.md`. There are no worker reply commits in the range:
this is the first review of the change.

Checked by running: `TagRowsTest` built from this head into a scratch build dir, 65/65 pass, including
the 10 new tests (`TagChipPickerState*`, `TagChipFindChip*`, `*NameOrdered*`). `./bin/clang-format-fix`
over the whole tree leaves `git status` clean. I did not re-run `pio run`. The brief says the build was
already green with no repo warnings, and the only firmware-only code is `TagChipView.cpp` and the two
call sites, which I read against the FreeInkUI headers instead.

## Intent

Each acceptance criterion against the code:

- **`ChipState` in a header that stays free of FreeInkUI.** `enum class ChipState : uint8_t { Inactive,
  Active, Disabled, Action }` is in `namespace TagChips` (`src/activities/reader/TagChipRow.h:91`). The
  header's includes are unchanged: `<StudyStore/TagPalette.h>` and standard headers only
  (`TagChipRow.h:3-10`). Spec A1 explains the namespace choice, and both call sites spell it
  `TagChips::ChipState`.
- **`Kind::NewTag`, `isSelected`, every switch.** `NewTag` is appended last (`TagChipRow.h:89`).
  Searching `src` and `test` for `TagChips::Kind` finds two switches, both in `TagChipView.cpp`.
  `buildEntries` skips the new kind with `continue` (`:40-42`) and `isSelected` returns false for it
  (`:56-57`). The two `!=` tests (`TagFilterActivity.cpp:163`, `:292`; `HighlightsActivity.cpp:156`)
  need no case. A NewTag chip already fails the retire guard at `:163`, and neither of the other two
  screens ever holds one.
- **`draw` takes a `ChipState` and paints the §2 table.** `TagChipView.cpp:79-113` matches the spec's
  table field for field:
  - Inactive and Action are today's white pill.
  - Active is today's black pill.
  - Disabled is `Paint::dither(Color::LightGray)` with `Paint::none()` and border width 0. Both factory
    functions exist (`FreeInkUICore.h:505`, `:512-517`).
  - Bold is on for Active and Action only.

  `props.state` is `StateSelected` for Active and `StateNormal` for everything else, never
  `StateDisabled`, so `findTouch` (`FreeInkUICore.h:1165-1170`) still routes a Disabled chip's tap and
  long-press. `measure` is unchanged and still bold. The caller supplies the rect, so a chip's width
  cannot depend on its state.
- **The flash style follows the chip's own state.** All four slots (`selected`, `focused`, `active`,
  `disabled`) are set to the state's own look (`TagChipView.cpp:106-109`). `StyleSet::resolve`
  (`FreeInkUICore.h:606-617`) can therefore only return that look, for any state the frame adds,
  including the tap flash's `StateFocused` (`:1039-1043`). For Active this is the same as today, where
  `focused` and `active` were already the inverted pill.
- **Highlights and the filter grid look as before.** Both call sites map the boolean they computed
  before to Active or Inactive, unchanged (`HighlightsActivity.cpp:708`, `TagFilterActivity.cpp:100-101`).
  - Without a flash, every repaint is the same as before.
  - With a flash, the only difference is the case spec A9 states. A dropped zero-count filter chip
    shifts the indices, so the flash lands on a neighbour, which used to paint inverted and now paints
    Inactive. That follows directly from the brief's flash rule, it fixes a wrong look rather than
    breaking a right one, and the spec gives the owner a device check for it.
  - The `…` chip opens the filter grid after `clearTapFlash()` (`HighlightsActivity.cpp:174`), so the
    flash change cannot affect it.
- **`STR_TAG_NEW_CHIP: "+ New tag"`** is in `english.yaml:374`, with an ASCII `+`, directly after
  `STR_TAG_NEW`.
- **The tests the brief lists are all present:**
  - `pickerState` with 7 applied (`TagChipRowTest.cpp:609-612`), with 8 (`:614-617`), after a deselect
    at 8 (`:621-626`), and NewTag at any count (`:632-637`), plus a `static_assert` (`:607`).
  - `findChip`, found and missing (`:657-678`).
  - Name-ordered `candidates` (`:413-429`). This test drives `TagChips::count` with ids out of raw order
    and puts the zero-count filter in the middle, so the Tally's slot mapping is exercised rather than
    hand-built.
  - CAP comes from `PassageDoc::MAX_TAGS_PER_PASSAGE` (= 8, `PassageDoc.h:45`), so a test cannot drift
    away from the real cap.

On scope:

- **No reduction.** Every item in the brief's "The change" list is present.
- **No expansion.** None of `TagChipGridActivity`, `TagPickerActivity`, `STR_TAG_NEW`,
  `TagRowMapping.h`, `TagPalette`, or the Highlights two-line limit and `…` chip was touched.
  `EpubReaderMenuActivity`'s own chip `StyleSet` is left alone, as the spec's non-goals say.

**Plan.** The code follows the plan's Tasks 1–4 almost verbatim: the same test bodies, the same
`pickerState` and `findChip` bodies, and the same `draw` body with `BoxStyle& look = styles.normal`
(plan Step 4.2). That line is a reference to the existing `StyleSet` member, not a second `BoxStyle`
copy, so it satisfies spec review 0's m2 (no new stack-resident style). The only divergence is in the
commits: Task 4's code and its formatting are separate commits (`f30121f3`, `136788c4`), and the string
is its own commit. That divergence does not affect the code.

## Quality

- **Each change mirrors an existing pattern.**
  - `findChip` is a template over the caller's container that reads only named members. That is the
    pattern of `TagChips::count`/`countIn` in the same header, and it keeps `ChipEntry` and FreeInkUI
    out of the header.
  - `pickerState` is a small `constexpr` function beside `chipHeight`.
  - The per-state `StyleSet`, with every slot copied from the state's own look, matches the existing
    dither-fill style construction the spec cites (`UiTabListActivity.cpp:160-181`).
- **Naming** follows its siblings:
  - Test suites use the `TagChipPickerState` and `TagChipFindChip` prefixes, like the existing
    `TagChipCandidates`, `TagChipGeometry` and `TagChipGrid`.
  - The `FakeChip` stand-in follows `FakePassage`.
- **Comments** explain a non-obvious why and do not narrate the code:
  - why `findChip` is a template (`TagChipRow.h:126-128`);
  - why Disabled is a look rather than an untappable chip (`TagChipRow.h:93-94`);
  - why every slot holds the chip's own look (`TagChipView.cpp:104-105`);
  - why `StateDisabled` is never set (`TagChipView.cpp:116-117`);
  - why the `NewTag` case is skipped (`TagChipView.cpp:41`).
- **Leftovers and allocation.** There is no dead or commented-out code. `draw` allocates nothing and
  adds no new `BoxStyle` or `StyleSet`.
- **Error handling.** No new failure path. `findChip` returns `-1` for a missing chip, which matches how
  the existing index helpers in this header report one.
- **The tests check behaviour.**
  - The deselect test states the transition it covers.
  - `TheKindMustMatch` guards the one plausible bug in `findChip`: a Tag chip and the Unlabelled chip
    both carrying the `UNLABELLED` id.
  - `FindsTheSingletonKindsByKindAlone` pins the documented choice to ignore `id` for kinds other than
    Tag (spec A10).
- **No duplication.** Nothing duplicates an existing helper. The tree had no other find-by-kind or cap
  helper before this change (spec "Problem" 4).

## Findings

None. I looked for a BLOCKER, MAJOR or MINOR and found nothing worth raising. The one wording mismatch
is the spec's "adds no local" against the `BoxStyle&` reference in `draw`. It is a reference, not a
copy, and the stack claim it supports still holds, so it would not mislead the implementer or the
tester.

Owner device checks still apply, as the spec lists them:
- the Highlights row looks as it does today;
- the A9 zero-count-filter tap leaves only the tapped chip inverted;
- the filter grid shows only the current filter inverted.

The Disabled and Action looks are first visible in the picker task.

VERDICT: CLEAR
