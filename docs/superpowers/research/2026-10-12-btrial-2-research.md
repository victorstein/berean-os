# btrial-2 — research: tag chip states and the pure chip helpers

Branch `feature/tag-chip-states`, cut from `batch/20261012-tag-chip-picker-2l3m` at `59c370cf`
("docs: design alphabetical tags and a chip-grid picker"). Governing spec:
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (§2, §3 "No index offsets" and "The
selection follows the tag", §4 `pickerState` table). Every claim below cites a line read in this
worktree or a command run in it.

## Who owns the behaviour

| Concern | Owner |
|---|---|
| Pure chip logic: `Kind`, counting, `candidates`, layout, paging, `hitPadding` | `src/activities/reader/TagChipRow.h` (namespace `TagChips`). Free of FreeInkUI by design (`:12-13`); its only project include is `<StudyStore/TagPalette.h>` (`:3`) |
| `ChipEntry`, building, measuring and drawing chips | `src/activities/reader/TagChipView.{h,cpp}` (namespace `TagChipView`). Includes `components/UiAppHost.h` (`TagChipView.h:11`), so FreeInkUI |
| Highlights chip row (the only `draw` caller besides the grid) | `src/activities/reader/HighlightsActivity.cpp:692-710` |
| Filter grid (the other `draw` caller) | `src/activities/reader/TagFilterActivity.cpp:94-104` |
| Host tests | `test/tag_rows/TagChipRowTest.cpp` (580 lines), built into `TagRowsTest` with `TagRowsTest.cpp` and `PassageActionsTest.cpp` (`test/tag_rows/CMakeLists.txt:1-5`); include dirs `src` and `lib/StudyStore` only (`:7-10`) |
| The string | `lib/I18n/translations/english.yaml` (`STR_TAG*` block at `:371-386`) |

No host test compiles `TagChipView.cpp`: `grep -rln TagChipView test` returns nothing. Its changes
are verified only by `pio run` and on the device.

## Current control flow

### `Kind` and every switch over it

`enum class Kind : uint8_t { All, Tag, Unlabelled }` (`TagChipRow.h:89`). `grep -rnE
"Kind::(All|Tag|Unlabelled)|TagChips::Kind|switch \(.*kind" src lib test` finds exactly two
`switch`es over `TagChips::Kind`, both in `TagChipView.cpp`:

- `buildEntries`, `switch (candidate.kind)` (`:27-40`): All / Unlabelled / Tag, no `default`.
  `candidates` never emits a `NewTag` (`TagChipRow.h:105`, `:110`, `:113`), but `-Wall` reaches
  this file (`scripts/enable_repo_warnings.py:26-30`, applied to repo sources), and `-Wall`
  includes `-Wswitch`, so adding `NewTag` without a case here would be a new warning.
- `isSelected`, `switch (chip.kind)` (`:46-54`): All → `!filter`, Unlabelled → `filter ==
  UNLABELLED`, Tag → `filter == chip.id`; falls through to `return false` (`:54`).

The other hits are `if` comparisons, not switches: `TagFilterActivity.cpp:161` (long-press acts
only on `Kind::Tag`), `:290` (All → empty result), `HighlightsActivity.cpp:156`. The other
`switch (... kind)` hits are over unrelated enums (`ReaderEntryIntent.h:37`,
`BibleNavigationActivity.cpp:302`, `MigrationRunner.cpp:344`).

### How a chip is drawn today

`TagChipView::draw(screen, rect, label, bool selected, action, value, inputMask, pad)`
(`TagChipView.h:43-44`, `TagChipView.cpp:74-107`):

- `styles.normal`: white fill, black foreground, 1 px black border, pill radius
  `min(height/2, 255)` (`:79-83`).
- `styles.selected`: `normal` with black fill and white foreground (`:84-86`).
- `styles.focused = styles.active = styles.selected` (`:88-89`); `styles.disabled = normal` (`:90`).
- `props.state = selected ? StateSelected : StateNormal` (`:97`); `props.text.bold = selected`
  (`:99`).

`measure` always measures bold (`TagChipView.cpp:67-72`), so width is already independent of the
selected flag (`TagChipView.h:40-41`).

### How FreeInkUI turns that into pixels

- `fui::button` (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/button.h:35-82`)
  registers the hit with `props.state` (`:43`), then resolves the drawn style from
  `frame.stateFor(action, value, state)` (`:47-48`), fills the background with the resolved
  style's radius (`:49`), strokes the border only when `border.kind != None && borderWidth > 0`
  (`:50-53`), and draws the label with `props.text` recoloured to the style's foreground (`:73-77`).
  **Text weight comes from `props.text.bold`, not from the resolved style**, so the caller fixes
  the weight per state and a flash cannot change it.
- `StyleSet::resolve` (`FreeInkUICore.h:606-617`) priority: Disabled, then Active, then Focused,
  then Selected/Checked, then Normal.
- `stateFor` (`FreeInkUICore.h:1025-1046`) ORs `StateFocused` when the interaction is the
  focused one, `StateActive` when it is under a held touch, and `StateFocused` again when it is
  the tap-flash target (`:1040-1043`).
- `findTouch` skips an interaction whose registered state has `StateDisabled`, for every input
  kind (`FreeInkUICore.h:1165-1170`); `findFirst` and `focusable` do the same (`:1159-1162`,
  `:1187`). This is why the spec's Disabled must be a look and never `fui::StateDisabled`.
- `Paint::dither(Color)` exists (`FreeInkUICore.h:512-517`). With a non-zero radius the GfxRenderer
  target fills via `fillRoundedRect` (`FreeInkUIGfxRenderer.h:73-80`), which dithers LightGray
  per pixel (`lib/GfxRenderer/GfxRenderer.cpp:1221-1222`). The `LightPill` list selection style
  builds the same paint (`FreeInkApp.h:262-265`).

### When the `focused` and `active` styles are actually painted on a chip

- **Focus ring (`StateFocused` via `focused_`).** Never, on either screen: chips register
  `InputTouch` (Highlights, `HighlightsActivity.cpp:709`) or `InputTouch | InputLongPress` (grid,
  `TagFilterActivity.cpp:102`), neither of which carries `InputFocus` (`FreeInkUICore.h:294-307`),
  so `focusable` is false. Both screens draw their own outline instead
  (`HighlightsActivity.cpp:712-720`, `TagFilterActivity.cpp:106-117`).
- **Held touch (`StateActive`).** Not repainted: `UiListActivity::routeListTouch` deliberately
  does no pressed-state repaint (`src/activities/UiListActivity.cpp:61-69`).
- **Tap flash (`StateFocused` via the flash).** Armed after every dispatched tap unless the handler
  called `clearTapFlash()` (`FreeInkApp.h:705-716` for a dispatch inside `render()`, `:735-741` for one routed from outside it; `clearTapFlash` at `:755-759`), and cleared after the repaint
  that shows the tap's result (`:683-687`).
  - Filter grid: never visible. `activateIndex` calls `clearTapFlash()` and exits
    (`TagFilterActivity.cpp:280-293`); the long-press path clears it too (`:162`).
  - Highlights `…` chip: never; `openTagFilter` clears it (`HighlightsActivity.cpp:173-174`).
  - **Highlights tag chips: armed.** `onChipEvent` → `selectChip` (`:136-171`) never clears it.
    Tapping the current filter changes nothing (`:157-160`), so the flashed chip is Active and
    keeps its inverted look. Tapping another chip sets the filter and rebuilds `chips_`
    (`:164-169` → `rebuildRowItems` → `rebuildChips`, `:127-134`); the flash is keyed by
    `(ACTION_CHIP, chip index)`.

**Consequence for "the Highlights row looks exactly as before".** In the common case the flashed
index holds the newly Active chip, so it is inverted before and after this change. The one
difference: `candidates` keeps a zero-count tag only while it is the filter
(`TagChipRow.h:108`). Filtering by such a tag X and then tapping a tag Y after it drops X's chip,
so Y moves one index left and the flash lands on the chip after Y. Today that innocent chip
paints inverted (`TagChipView.cpp:88`) for one repaint, so two chips look selected. After the
change it paints its own Inactive look. That is a visible change, but it removes a wrong second
"selected" chip, and the spec's flash rule (§2) is meant to produce exactly this.

### Call sites to map

- Highlights: `selected` is computed at `HighlightsActivity.cpp:702-703`; the `…` chip is
  "selected" when the active filter's chip was pushed off the row (`:692-696`, `:701`). Passed to
  `draw` at `:707`.
- Filter grid: `TagChipView::isSelected(entry, filter_)` inline at `TagFilterActivity.cpp:100`.
- `TagFilterActivity::selectedChipIndex` (`:50-55`) also calls `isSelected`; it does not draw.

The spec's citation "`HighlightsActivity.cpp:703-707`" matches: `:703` is the `isSelected` call,
`:707` the `draw` call.

### Selection restore today (what the find-chip helper replaces later)

`TagFilterActivity::rebuildChips` (`:40-48`) swaps in the new list and clamps `nav.selected` into
range (`:46-47`); it does not look the chip up again. `onEnter` places the selection with
`selectedChipIndex()` (`:28-32`). `nav.selected` is an `int`. The base-class wiring is
`feature/tag-chip-grid-base`'s work; this task only supplies the pure helper.

`ChipEntry` (`TagChipView.h:18-22`) defaults `id` to `study::UNLABELLED`, and `buildEntries`
assigns `id` only for `Kind::Tag` (`TagChipView.cpp:36`). So All, Unlabelled and (later) NewTag
chips all carry `id == UNLABELLED`, and a `(kind, id)` match is unambiguous for them too.

### The cap

`study::PassageDoc::MAX_TAGS_PER_PASSAGE = 8` (`lib/StudyStore/StudyStore/PassageDoc.h:45`). The
picker's current cap check is `!isSelected(id) && selectedIds_.size() >= MAX_TAGS_PER_PASSAGE`
(`TagPickerActivity.cpp:134`), the same rule the spec's `pickerState` table encodes (Disabled only
when the tag is not on the passage and the count has reached the cap).

### The string

`english.yaml` has no `STR_TAG_NEW_CHIP` today; `STR_TAG_NEW: "New tag..."` is at `:373` and is
removed by `feature/tag-picker-grid`, not here. `english.yaml` is the only translation file
carrying `STR_TAG_NEW` (`grep -ln STR_TAG_NEW lib/I18n/translations/*.yaml`), so nothing else
needs a new key. Other languages fall back to English (`scripts/gen_i18n.py:15-16`).

The firmware build runs `gen_i18n.py` as a `pre:` script (`platformio.ini:134`) with
`strip_unused=True` (`scripts/gen_i18n.py:998-1001`): a key no source references is dropped from
the generated tables (`:916-922`) and reported, not failed. So adding `STR_TAG_NEW_CHIP` before
the picker task uses it costs nothing and breaks nothing. The usage scan is a plain regex over
source text, comments included (`:267`).

`.claude/agents/ui-dev.md` ("Shared files — report, do not edit") lists the translation YAMLs as
append points to report rather than edit. The brief assigns this line to this task explicitly, so
it is in scope here; it is a single appended key.

## Installed versions

| Tool | Version | Command |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on `PATH` in this shell) |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| Arduino-ESP32 framework | 3.3.7 | `~/.platformio/packages/framework-arduinoespressif32/package.json` |
| Xtensa toolchain | 14.2.0+20251107 | `~/.platformio/packages/toolchain-xtensa-esp-elf/package.json` |
| Language standard | `-std=gnu++2a` | `platformio.ini:40` (per `CLAUDE.md`) |
| Host compiler | Apple clang 21.0.0 | `c++ --version` |
| CMake | 4.4.2 | `cmake --version` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:15-17` (FetchContent) |
| clang-format | 21.1.8, from `.venv/bin/clang-format` | `bin/clang-format-fix:3-12` picks it; `.venv/bin/clang-format --version` |
| Python | 3.14.7 | `python3 --version` |

## Baseline

- Host suite: `cmake -S test -B build/test && cmake --build build/test -j 8 && ctest --test-dir
  build/test -j 8` → `100% tests passed out of 1678`. 43 of those are `TagChip*` tests
  (`ctest -N | grep -c TagChip`).
- Firmware: `pio run -e x4pro` started at `59c370cf`; it is the first build in this worktree, so it compiles the whole framework and had not finished when this note was committed. Its warning set for `src/activities/reader/` is the baseline for "no new warnings" and is recorded in the spec.

## Nearest existing examples

- **The precedent for this whole shape is #245** (`b5425810`, "replace the paged tag filter with a
  chip grid"): it moved chip logic into pure `TagChips::` helpers with host tests in
  `TagChipRowTest.cpp` (+260 lines), and drawing into `TagChipView` (+109/+46). This task extends
  both files the same way.
- **A pure helper over a caller's struct without naming its type:** `TagChips::count` and
  `countIn` are templates over `Passages`, touching only `passage.tags` (`TagChipRow.h:66-82`).
  The find-chip helper can do the same over a container of chips with `.kind` and `.id`, which
  keeps `ChipEntry` (and so FreeInkUI) out of `TagChipRow.h`, and lets the test use a plain struct.
- **A pure helper that returns a small enum for a renderer:** none in `TagChipRow.h` yet. In the
  same file the closest are the `constexpr`/`inline` free functions with host tests
  (`chipHeight`, `:191-193`; `linesPerPage`, `:199-202`).
- **Per-state `StyleSet`s:** `TagChipView::draw` itself (`:77-90`); `UiTabListActivity.cpp:160-181`
  (a dither `selected` background, `focused`/`active` copied from `selected`); the
  `LightPill` expansion (`FreeInkApp.h:262-265`). `EpubReaderMenuActivity.cpp:436-448` builds an
  identical chip `StyleSet` for the reader menu's recent-search chips; it is a separate copy that
  does not call `TagChipView` and is out of scope.

## Points the spec phase must settle

1. **Disabled text weight.** §2 says "black text" with no weight. Inactive is regular and Action is
   bold. Regular is the natural reading (a grayed Inactive chip); width is unaffected either way,
   because `measure` is always bold.
2. **`pickerState` for kinds the picker never shows** (All, Unlabelled). The §4 table covers only
   `NewTag` and `Tag`. The function must still return something for them; the least surprising is
   to treat every non-`NewTag` kind by the `selected` / cap rule.
3. **Return type of the find-chip helper.** `nav.selected` is an `int`, and the base will clamp on
   a miss (§3); an `int` with `-1` for "not found" fits the existing `pageHolding`-style `int`
   indices. `std::optional<size_t>` is the alternative.
4. **Flash look of an Active chip.** The spec changes `focused`/`active` only for Inactive,
   Disabled and Action, so an Active chip keeps the inverted pill under a flash, exactly as today.

## Tier

`standard` holds. The change stays on the `ui` surface plus one appended key in
`english.yaml` that the brief assigns. It writes no on-disk format, moves no cache or store
version, and changes no contract outside `src/activities/reader/`. `ChipState`, `Kind::NewTag`,
`pickerState` and the find-chip helper are consumed only by sibling tasks in this batch, with
names and shapes fixed by the approved spec.
