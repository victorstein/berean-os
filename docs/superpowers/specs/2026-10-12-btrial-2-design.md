# Design: tag chip states and the pure chip helpers (btrial-2)

Surface: `ui`. Bead `btrial-2`, task `t2` of run `berean-os-20261012-tag-chip-picker-2l3m`, branch
`feature/tag-chip-states`. Parent design (approved, and it wins where this one disagrees):
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`, referred to below as **the batch
spec**. Research: `docs/superpowers/research/2026-10-12-btrial-2-research.md`.

## Problem

The batch's later tasks need chips that show more than two looks, and pure helpers they can share
and host-test. Today none of that exists:

1. **A chip is either selected or not.** `TagChipView::draw` takes `bool selected`
   (`src/activities/reader/TagChipView.h:43-44`) and maps it to `fui::StateSelected` or
   `fui::StateNormal` (`TagChipView.cpp:97`). The picker grid needs four looks: active, inactive,
   disabled at the 8-tag cap, and an action look for **+ New tag** and **Done** (batch spec §2, D4).
2. **The tap flash always paints the inverted pill.** `draw` copies the selected style into
   `focused` and `active` (`TagChipView.cpp:88-89`), and FreeInkUI paints a just-tapped element
   with its focused style for one repaint (`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUICore.h:1040-1043`,
   `FreeInkApp.h:705-716`). In the picker every tap toggles a chip, so a tag just removed would
   repaint as applied (batch spec, review 0 finding B1).
3. **No chip kind means "+ New tag".** `TagChips::Kind` is `{ All, Tag, Unlabelled }`
   (`src/activities/reader/TagChipRow.h:89`). The picker would need an index offset to place its
   action chip, and the batch spec removes those offsets (§3 "No index offsets").
4. **No pure rule for a picker chip's state, and no pure way to find a chip again after a
   rebuild.** The cap rule lives inline in `TagPickerActivity.cpp:134`. The filter grid restores its
   selection by clamping an index (`TagFilterActivity.cpp:46-47`), which lands on a neighbouring
   chip once create and retire re-sort the list (batch spec §3 "The selection follows the tag").

## Goal

- `TagChipView::draw` paints four states. A chip's width never depends on its state, and a flash
  never changes an Inactive, Disabled or Action chip's look.
- `TagChips::Kind::NewTag`, `TagChips::ChipState`, `TagChips::pickerState` and
  `TagChips::findChip` exist in `TagChipRow.h`, which still includes no FreeInkUI header, and the
  host suite tests them.
- The Highlights chip row and the filter grid look and behave as they do today (one edge-case
  difference, a correction, is stated under A9).
- `STR_TAG_NEW_CHIP: "+ New tag"` is in `english.yaml`, ready for the picker task.

## Non-goals

- No consumer of `Action`, `Disabled`, `NewTag`, `pickerState` or `findChip` in this change. The
  grid base (`feature/tag-chip-grid-base`) and the picker (`feature/tag-picker-grid`) consume them.
- No change to `buildEntries`' order, `TagFilterActivity`'s structure, `TagPickerActivity`,
  `STR_TAG_NEW`, `TagRowMapping.h` or `TagRowsTest.cpp` (sibling tasks).
- No `TagPalette` change (`feature/tag-order-rank`).
- No change to the Highlights row's two-line limit or its **…** chip.
- No change to `measure`, `metricsFor`, chip geometry or hit rects.
- No change to the reader menu's recent-search chips, which build their own identical `StyleSet`
  (`src/activities/reader/EpubReaderMenuActivity.cpp:436-448`) and do not call `TagChipView`.

## Modelled on

- **#245** (`b5425810`), which created both files: pure helpers in `TagChipRow.h` with host tests in
  `test/tag_rows/TagChipRowTest.cpp`, FreeInkUI drawing in `TagChipView`. This change extends both
  the same way.
- **`TagChips::count` / `countIn`** (`TagChipRow.h:66-82`): templates over the caller's container
  that touch only named members (`passage.tags`). `findChip` follows this to stay free of
  `ChipEntry`, and so of FreeInkUI.
- **`TagChips::chipHeight`** (`TagChipRow.h:191-193`): a small pure inline function with a host
  test. `pickerState` follows it.
- **`TagChipView::draw`** itself (`TagChipView.cpp:74-107`) and `UiTabListActivity.cpp:160-181` for a
  per-state `StyleSet` with a dither background and `focused`/`active` copied from the shown style.

## Design

### 1. Types and helpers in `TagChipRow.h`

All in `namespace TagChips`, beside `Kind`. No new include: `<cstddef>`, `<cstdint>` and
`<StudyStore/TagPalette.h>` are already there (`TagChipRow.h:3-10`).

```cpp
enum class Kind : uint8_t { All, Tag, Unlabelled, NewTag };

enum class ChipState : uint8_t { Inactive, Active, Disabled, Action };

constexpr ChipState pickerState(Kind kind, bool selected, size_t selectedCount, size_t cap);

template <typename Chips>
int findChip(const Chips& chips, Kind kind, study::TagId id);
```

**`pickerState`** encodes the batch spec's §4 table:

| Input | Result |
|---|---|
| `kind == NewTag` | `Action`, whatever the count |
| `selected` | `Active` |
| `selectedCount < cap` | `Inactive` |
| otherwise | `Disabled` |

**`findChip`** returns the index of the first chip in `chips` whose `kind` equals `kind` and, for
`Kind::Tag` only, whose `id` equals `id`; `-1` when there is none. It reads only `.kind` and `.id`, so
`TagChipView::ChipEntry` (`TagChipView.h:18-22`) satisfies it and the test can use a plain struct.
It is a linear scan, run once per rebuild on the loop task (batch spec §3), over at most
`activeCount + 2` chips (`TagChipRow.h:104`) for the filter or `activeCount + 1` for the picker.

### 2. `TagChipView`

**Signature.** `draw(screen, rect, label, TagChips::ChipState state, action, value, inputMask, pad)`:
only the fourth parameter's type changes (`TagChipView.h:43-44`).

**Looks.** `draw` builds one `fui::BoxStyle look` for the state and copies it into every slot of
the `StyleSet` (`normal`, `selected`, `focused`, `active`, `disabled`), so whatever state FreeInkUI
resolves (`FreeInkUICore.h:606-617`), the chip paints its own look.

| State | `look.background` | `look.foreground` | `look.border`, width | `props.text.bold` | `props.state` |
|---|---|---|---|---|---|
| Inactive | `solid(White)` | `solid(Black)` | `solid(Black)`, 1 | false | `StateNormal` |
| Active | `solid(Black)` | `solid(White)` | `solid(Black)`, 1 | true | `StateSelected` |
| Disabled | `dither(LightGray)` | `solid(Black)` | `none()`, 0 | false | `StateNormal` |
| Action | `solid(White)` | `solid(Black)` | `solid(Black)`, 1 | true | `StateNormal` |

Every look keeps today's pill radius, `min(rect.height / 2, 255)` (`TagChipView.cpp:83`). Inactive
and Active are today's `normal` and `selected` styles field for field (`TagChipView.cpp:79-86`).

`props.state` is never `fui::StateDisabled`, so `findTouch` still routes a Disabled chip's tap and
long-press (`FreeInkUICore.h:1165-1170`). `props.minTouchSize = 0` and the `hitPadding` mapping are
unchanged (`TagChipView.cpp:103-105`).

**Width.** `measure` is unchanged and measures bold (`TagChipView.cpp:67-72`), the heaviest weight
any state uses. Its comment (`TagChipView.h:40`) is reworded from "the selected weight" to say the
width is the same in every state.

**`isSelected`** (`TagChipView.cpp:45-55`) gains `case TagChips::Kind::NewTag: return false;`.

**`buildEntries`' switch** (`TagChipView.cpp:27-40`) gains `case TagChips::Kind::NewTag: continue;`
before the `push_back`, so a `NewTag` candidate, which `candidates` never produces
(`TagChipRow.h:105`, `:110`, `:113`), is skipped rather than emitted with an empty label.

### 3. Call sites

Both map the boolean they compute today, unchanged, to a state:

- `HighlightsActivity.cpp:702-708`: `selected ? TagChips::ChipState::Active :
  TagChips::ChipState::Inactive`, the `…` chip included (its `selected` is `!selectedPlaced`,
  `:692-696`, `:703`).
- `TagFilterActivity.cpp:100`: the same mapping of `TagChipView::isSelected(entry, filter_)`.

### 4. The string

`STR_TAG_NEW_CHIP: "+ New tag"` appended to `lib/I18n/translations/english.yaml` directly after
`STR_TAG_NEW` (`:373`). ASCII `+`. No other YAML changes; other languages fall back to English
(`scripts/gen_i18n.py:15-16`). Until the picker task references it, the firmware build strips it as
unused (`scripts/gen_i18n.py:916-922`, `:1001`), so it costs no flash and fails nothing.

## Data and control flow

- `pickerState` and `findChip` are pure and allocation-free. In this change nothing calls them
  outside the host tests.
- `draw` runs on the render task, inside `buildScreen` (`HighlightsActivity.cpp:707`,
  `TagFilterActivity.cpp:100`). It allocates nothing: like today it keeps a `StyleSet` and a
  `ButtonProps` on the stack (`TagChipView.cpp:77`, `:92`); the new `BoxStyle look` is one more
  local of a few dozen bytes and no heap object. `fui::button` registers the hit with
  `props.state`, then resolves the drawn style through `frame.stateFor` (`button.h:43`, `:47-48`).
- When FreeInkUI paints a chip's `focused` or `active` slot (research, "When the focused and
  active styles are actually painted"): never for the focus ring, because chips carry no
  `InputFocus` (`HighlightsActivity.cpp:709`, `TagFilterActivity.cpp:102`,
  `FreeInkUICore.h:294-307`); never for a held touch, because list screens do no pressed-state
  repaint (`UiListActivity.cpp:61-69`); and for one repaint after a tap whose handler did not call
  `clearTapFlash()`, which on today's screens means only a Highlights tag chip
  (`HighlightsActivity.cpp:136-171`; the filter grid clears it, `TagFilterActivity.cpp:162`, `:283`).

## Error handling

No new failure path. `pickerState` is total over its inputs. `findChip` returns `-1` for an empty
list or a missing chip; the caller (grid base) clamps, as the filter does today
(`TagFilterActivity.cpp:46-47`). `draw` has no error return today and gains none.

## Assumptions

Each is a behavioural decision this design makes; the review should attack them.

- **A1. `ChipState` and `pickerState` live in `namespace TagChips`.** The batch spec places
  `ChipState` in `TagChipRow.h` (§2) and calls the helper `TagChips::pickerState` (§4) without
  naming the enum's namespace. Everything in that header is in `TagChips` (`TagChipRow.h:14`, `:313`).
- **A2. `NewTag` is appended last**, so `All`, `Tag` and `Unlabelled` keep their underlying values.
  `Kind` is never persisted or used as an array index: its only uses are `Candidate`, `ChipEntry`,
  two `switch`es and three `!=` tests (research, "`Kind` and every switch over it").
- **A3. Disabled text is regular weight.** The batch spec says "black text" (§2) without a weight.
  A Disabled chip is an Inactive chip the cap blocks, and Inactive is regular. Width is unaffected
  (A6).
- **A4. Disabled has no border.** The batch spec says so (§2); the dither fill alone marks the
  pill's extent, as the `LightPill` selection does (`FreeInkApp.h:262-265`).
- **A5. Every `StyleSet` slot holds the chip's own look**, Active included. For Active this equals
  today: `focused` and `active` were already the inverted pill (`TagChipView.cpp:88-89`). For
  Inactive, Disabled and Action it is the batch spec's flash rule (§2). `disabled` was `normal`
  (`:90`) and is never resolved, because `props.state` never carries `StateDisabled` and `enabled`
  stays true (`button.h:46`).
- **A6. Width is state-independent because the caller passes the rect.** `draw` takes `rect` from
  the caller, which sizes it with `measure` (bold); the border is drawn inside the rect
  (`button.h:50-53`). Removing the border for Disabled moves nothing.
- **A7. Active keeps `props.state = StateSelected`.** With every slot equal it no longer changes the
  look, but it keeps each registered interaction identical to today's. Nothing in `src/` or
  FreeInkUI's routing reads `StateSelected` from an interaction; `StyleSet::resolve` is its only
  reader (`FreeInkUICore.h:613`; `grep -rn StateSelected src` finds only state assignments).
- **A8. `pickerState` treats every kind other than `NewTag` by the `selected` / cap rule.** The
  picker never shows All or Unlabelled (batch spec §4, "+ New tag first, then
  `activeIdsByName()`"), so this only fixes a total answer for inputs it never gets. A
  `selectedCount` above `cap` yields `Disabled` for an unselected chip, as at the cap.
- **A9. The Highlights row's look is unchanged except in one case, which this corrects.** With a
  zero-count tag X as the filter, tapping a later tag Y drops X's chip (`TagChipRow.h:108`), so Y
  moves one index left and the flash, keyed by `(ACTION_CHIP, index)`, lands on the chip after Y.
  Today that chip paints inverted for one repaint (`TagChipView.cpp:88`), so two chips look
  selected; after this change it paints Inactive. Every other Highlights and filter-grid repaint is
  pixel-identical, by A5 and the table in §2.
- **A10. `findChip` ignores `id` for kinds other than `Tag`.** All, Unlabelled and NewTag each occur
  at most once in a list (`TagChipRow.h:105`, `:113`; the picker adds one NewTag, batch spec §4),
  so the kind alone identifies them. Today those chips carry `id == UNLABELLED` by default
  (`TagChipView.h:20`, `TagChipView.cpp:36`), and ignoring the id means a caller that sets it
  differently cannot break the lookup.
- **A11. `findChip` returns `int`, `-1` when missing.** `nav.selected` is an `int`
  (`TagFilterActivity.cpp:31`, `:47`) and the existing index helpers return `int`
  (`TagChipRow.h:275-286`).
- **A12. `buildEntries` skips a `NewTag` candidate.** It cannot occur (`candidates` never emits
  one); `continue` is the answer that cannot show an empty chip if that ever changes.

## Testing strategy

TDD: each test below is written first and fails to compile, because the names do not exist, before
the helper is added.

**Host tests, `test/tag_rows/TagChipRowTest.cpp`** (built into `TagRowsTest`,
`test/tag_rows/CMakeLists.txt:1-5`; no CMake change):

- `TagChipPickerState`:
  - At 7 applied (cap 8): an unselected tag is `Inactive`, a selected one `Active`.
  - At 8 applied: an unselected tag is `Disabled`, a selected one `Active`.
  - After a deselect at 8 (7 applied): the tag that was `Disabled` is `Inactive` again, and the
    one just removed is `Inactive`.
  - `NewTag` is `Action` at 0, 7 and 8 applied.
  - `static_assert` on one case, since `pickerState` is `constexpr`.
- `TagChipFindChip`, over a local `struct { TagChips::Kind kind; TagId id; }` list holding All, two
  tags, Unlabelled and NewTag:
  - finds a tag by id; finds All, Unlabelled and NewTag by kind;
  - returns `-1` for a tag id that is not in the list (a retired tag) and for an empty list;
  - does not match a `Tag` chip when asked for `Kind::Unlabelled` with `UNLABELLED`'s id, nor the
    reverse.
- `TagChipCandidates.NameOrderedIdsKeepAllFirstAndUnlabelledLast`: `candidates` given ids in an
  order that is not raw-id order (`{NAME, HOPE, MINISTRY}`, raw 3, 1, 2; fixtures at
  `TagChipRowTest.cpp:18-20`) emits All, the tags' slots in the given order, then Unlabelled.

**Firmware build:** `pio run -e x4pro` builds with no warning from `src/` or our `lib/`. Baseline at
`59c370cf` (the research's build, PlatformIO 6.1.19): `SUCCESS`, zero `warning:` lines under `src/`
or `lib/` (the log's only warning is in `.pio/libdeps/x4pro/WebSockets`), RAM 66,220 B, flash
5,558,994 of 6,553,600 B. This is the only automated check of `TagChipView.cpp` and the two call
sites; no host target compiles them (research, "Who owns the behaviour"). A flash delta of more
than a few hundred bytes would mean something other than a style table changed.

**Format:** `./bin/clang-format-fix` over the whole tree leaves it clean.

**Full host suite:** `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir
build/test --output-on-failure -j` passes (baseline 1,678 tests).

**Device checks (owner), since only hardware shows the looks:**

- Highlights: the chip row looks as today; tapping a chip shows it inverted and the old one white.
- Filter grid (**…**): looks as today; the current filter is the only inverted chip.
- The Disabled and Action looks are first visible in the picker task; they are checked there.
