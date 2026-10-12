# Design: alphabetical tags and a chip-grid tag picker

Surface: `ui`, plus a display-order cache in `lib/StudyStore` and one theme seam. No issue yet.
Decisions were made with the owner in a brainstorm on 2026-10-11 and after review 0; mockups are
in `.superpowers/brainstorm/32741-1791777805/content/` (not committed). Revision 1: see "Review 0
outcome" at the end.

## Problem

1. **Tags appear in allocation order.** Every tag surface reads `TagPalette::activeIds()`, which
   returns "active tags in allocation order" (`lib/StudyStore/StudyStore/TagPalette.h:57-58`,
   `TagPalette.cpp:57-64`). With the owner's ~48-tag palette, finding a tag means scanning the
   whole set. The readers are `StudyStore::activeTags()` (`src/study/StudyStore.cpp:159-163`), which
   feeds the picker, and `TagChipView::buildEntries` (`src/activities/reader/TagChipView.cpp:16`),
   which feeds the Highlights chip row and the `TagFilterActivity` grid. A passage row's tag
   summary joins names in stored order (`StudyStore.cpp:184-194`), which the picker writes in
   palette order (`TagPickerActivity.cpp:216-225`).
2. **The tag picker is a scrolling switch list.** `TagPickerActivity` renders "Done", one toggle
   row per active tag, then "New tag..." (`TagPickerActivity.cpp:80-109`). It gets harder to use
   as the palette grows. The filter grid (`TagFilterActivity`, PR #245) already shows the same
   tags as a paged chip grid that is quick to scan and tap.

## Goal

1. Tags are shown in alphabetical order on every tag surface: the Highlights chip row, the filter
   grid, the picker, and the passage-row tag summary. **All** stays first and **Unlabelled** last.
2. The picker becomes a chip grid built on the same code as the filter grid, with chips that are
   active, inactive or disabled.
3. The filter grid's and the Highlights row's behaviour is unchanged apart from order, and two
   small fixes listed in §5.

## Non-goals

- No change to the palette file, `TagPalette::FORMAT_VERSION`, the passage file, or **the order a
  passage's tags are stored in**. The picker keeps committing in palette order (§4), so the sleep
  screen, which shows a passage's first stored tag (`src/activities/boot_sleep/StudyPassageScan.cpp:81-88`,
  `StudySleepScreen.cpp:266`), is unaffected. No store or cache version moves.
- No change to `TagSelectionResult` (`src/activities/ActivityResult.h:81-88`) or to how the two
  picker callers apply it.
- No counts on picker chips. No tag search. No "selected first" grouping.
- No change to the Highlights row's two-line limit or its **…** chip.
- No change to the 28 existing `drawHeader` call sites; the header seam is a new overload.

## Decisions

| # | Decision | Chosen |
|---|---|---|
| D1 | Where alphabetical order applies | Every tag surface, including passage-row summaries |
| D2 | Collation | Case- and accent-insensitive with ñ folded into n (`BibleSearch::foldAppend`); a leading `¿` or `¡` is ignored; raw-name then id tiebreak |
| D3 | Picker layout | **Done** in the header, placed by the theme; **+ New tag** as the first chip |
| D4 | Chip states | Three: active, inactive, and disabled once the passage holds 8 tags |
| D5 | Counts on picker chips | None |
| D6 | Code shape | A shared base activity, `TagChipGridActivity`, for both grids |
| D7 | A tap on a disabled chip | Shows the existing limit message; the chip does not change |
| D8 | Selected tags with no chip (retired, unknown) | Dropped when the picker opens |

D2 consequences the owner accepted: digits sort as text ("10 …" before "2 …"), "año" sorts before
"anzuelo", and Greek or Cyrillic names are not case-folded.

## Modelled on

- **`TagFilterActivity`**: the grid build (`src/activities/reader/TagFilterActivity.cpp:57-118`),
  the page strip (`:79-84`, `:120-132`), the focus outline (`:106-117`), paging and stepping
  (`:201-259`), and the retire-confirmation `render()` override (`:261-278`). These move into the
  base class.
- **`UiTabListActivity`** (`src/activities/UiTabListActivity.h:25-26`): a `UiListActivity`
  subclass that is itself a base for screens of one shape, and registers its own actions from
  `ACTION_USER` (`src/activities/UiListActivity.h:26-28`).
- **`HighlightsActivity`**'s `ACTION_CHIP` (`HighlightsActivity.h:103`,
  `HighlightsActivity.cpp:42`): how a subclass registers a second action beside `ACTION_ROW`.

## 1. Ordering

**One order, computed in one place.** `TagPalette` gains a name rank: one `uint16_t` per entry,
active and retired, giving that entry's position when every entry is sorted by the D2 key.

- `std::vector<TagId> activeIdsByName() const` returns the active ids sorted by rank.
- `uint16_t nameRank(TagId id) const` returns the id's rank, or `UINT16_MAX` for `UNLABELLED` and
  for an id the palette does not know, so those sort last. It is a linear lookup, like `name()`
  (`TagPalette.cpp:42-47`).
- `activeIds()` keeps allocation order and its comment. The picker's commit (§4) still uses it.

**Collation key.** Skip a leading `¿` (U+00BF) or `¡` (U+00A1), then
`BibleSearch::foldAppend(key, name)`: lower-case ASCII, strip Latin diacritics, ñ→n
(`lib/BibleSearch/BibleSearch/Fold.h:18-23`). Without the skip, both marks pass through unfolded
and sort after z (review 0, m10). Keys compare bytewise; ties break on the raw name bytes, then
the raw id. The id tiebreak is load-bearing: `fromJson` dedupes by id only and truncates long
names (`TagPalette.cpp:92`, `:95-97`), so two entries can share a name.

**When the rank is rebuilt.** Only a name change can move a rank, and only two operations change
the set of names: `add` when it appends a new entry (`TagPalette.cpp:24-26`), and `fromJson`
(`:78-106`). `retire` and a revive inside `add` (`:15-19`) leave names as they are, so they do not
rebuild. `StudyStore::addTagName`'s rollback calls `retire` (`StudyStore.cpp:254-257`) and needs
nothing extra.

**Cost and bound.** A rebuild folds every entry into a flat key buffer of `entries × 24` bytes.
That is enough because a fold is never longer than its input (`Fold.h:21-22`) and a name is at
most 24 bytes (`TagPalette.h:39`). Entries are bounded only by the palette file's save budget,
`DEFAULT_SAVE_BUDGET` = 45,000 B (`lib/Serialization/SaveBudget.h:23`, enforced at
`src/study/TagPaletteFile.cpp:29`). The smallest entry serialises to about 16 B, so the bound is
about 2,800 entries:

| Palette | Key buffer | Lives in |
|---|---|---|
| ~60 entries (owner's) | ~1.4 KB, transient | internal SRAM |
| ~2,800 entries (budget bound) | ~67 KB, transient | PSRAM (over 4,096 B, `sdkconfig.x4pro:2146-2147`) |

The buffer comes from `makeUniqueNoThrow`. On OOM the rebuild logs `LOG_ERR` and falls back to
allocation order (rank = entry index), so a failure shows today's order rather than crashing.
`fromJson` also runs on every publication open and every study sleep screen; at ~60 entries that
is ~60 short folds each time.

**Why a cache.** The readers are on hot loop-task paths: `tagNamesFor` runs for every visible
Highlights row through `tagsValueFor` (`HighlightsActivity.cpp:94`) from `rebuildRowItems`, and
`buildEntries` runs on every chip-row rebuild. A rank lookup is an integer compare; a fold per
compare would allocate.

**Consumers:**

- `StudyStore::activeTags()` iterates `activeIdsByName()`. Its comment changes from "allocation
  order" to "display order" (`src/study/StudyStore.h:42-49`).
- `TagChipView::buildEntries` uses `activeIdsByName()` (`TagChipView.cpp:16`).
  `TagChips::candidates` already emits All, then the tags in the order given, then Unlabelled
  (`src/activities/reader/TagChipRow.h:97-101`), so All and Unlabelled stay pinned.
- `StudyStore::tagNamesFor()` looks up each of a passage's ≤ 8 ids' rank once into a local
  `{rank, id}` array (8 × 4 B on the stack), sorts that, and joins. Retired tags still resolve
  (`StudyStore.h:59-61`), which is why the rank covers retired entries.

**Library dependency.** `lib/StudyStore` gains an include of `<BibleSearch/Fold.h>` and a link
dependency on `lib/BibleSearch/BibleSearch/Fold.cpp`. Four host-test targets compile
`TagPalette.cpp`, and all four add `Fold.cpp` and the `lib/BibleSearch` include directory:
`test/tag_palette`, `test/migration_planner` (`CMakeLists.txt:7`), `test/passage_doc`
(`CMakeLists.txt:7`) and `test/storage_io` (`CMakeLists.txt:21`).

## 2. Chip states (`TagChipView`)

`draw(..., bool selected, ...)` (`TagChipView.h:43-44`) becomes `draw(..., ChipState state, ...)`.
`enum class ChipState : uint8_t { Inactive, Active, Disabled, Action }` lives in `TagChipRow.h`,
which is free of FreeInkUI by design (`TagChipRow.h:12-13`), so pure helpers can return it and the
host suite can test them.

| State | Look | Used for |
|---|---|---|
| Inactive | white fill, 1 px black border, regular text (today's unselected chip) | a tag that is not applied or not filtering |
| Active | black fill, white bold text (today's selected chip) | an applied tag; the current filter |
| Disabled | light-gray dither fill (`Paint::dither(Color::LightGray)`, `FreeInkUICore.h:512-517`, as the `LightPill` selection style uses, `FreeInkApp.h:262-265`), no border, black text | inactive picker chips while the passage holds 8 tags |
| Action | the Inactive outline with bold text | **+ New tag** and the header **Done** pill |

The Highlights row and the filter grid map `isSelected` to Active or Inactive and look as they do
today. `TagChipView::isSelected` (`TagChipView.cpp:45-55`) gains a `Kind::NewTag` case, which
returns false.

**Rules:**

- **Width never depends on state.** Chips stay measured bold whatever their state
  (`TagChipView.h:40-41`). In the picker every tap changes a state, and a reflow would move chips
  under the finger.
- **Disabled is a look, not `fui::StateDisabled`.** `findTouch` skips a disabled interaction for
  every input kind, long-press included (`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUICore.h:1165-1170`),
  so a tag could not be retired at 8/8. The chip keeps a live interaction; `activateIndex` handles
  its tap (§4).
- **The flash style follows the chip's own state.** A tap arms a flash that lasts exactly the
  repaint showing the tap's result (`FreeInkApp.h:683-687`, `:739-741`). It paints with the
  focused style (`FreeInkUICore.h:1039-1043`), which `draw` sets to the inverted pill today
  (`TagChipView.cpp:87-89`). For Inactive, Disabled and Action chips, `focused` and `active` become
  that chip's own look. A toggle's result is then not overpainted by a flash (and §4 suppresses the
  flash on toggles as well).
- **The button focus ring is unchanged.** It is drawn in the gap around the focused chip
  (`TagFilterActivity.cpp:106-117`); the base class owns it now.
- **Cost of the cap state.** None beyond a normal toggle. Every toggle already clears and redraws
  the whole frame (`TagPickerActivity.cpp:322-341`); reaching 8/8 changes only which paint each
  chip gets.

**New string:** `STR_TAG_NEW_CHIP: "+ New tag"` in `lib/I18n/translations/english.yaml`, with ASCII
`+` so the glyph is certainly in the UI font. `STR_TAG_NEW` ("New tag...", `english.yaml:373`) is
removed only if a search of the whole tree, comments included, finds no other use:
`scripts/gen_i18n.py` scans comments for `STR_` names.

## 3. `TagChipGridActivity`, the shared base

New `src/activities/reader/TagChipGridActivity.{h,cpp}`: a `UiListActivity` constructed with
`wantsTouchLongPress = true`, as both screens are today (`TagFilterActivity.cpp:22`,
`TagPickerActivity.cpp:26`). Most of its body moves out of `TagFilterActivity`.

**The base owns:**

- `chips_` (`std::vector<TagChipView::ChipEntry>`), `widths_`, `page_` (`TagChips::GridPage`, a
  member and never a render-task local, `TagChipRow.h:204-205`), the page geometry, page number and
  count, and `buttonFocus_`, under the `RenderLock` rules in `TagFilterActivity.h:62-75`.
- `rebuildChips()`: calls the subclass's `buildChips(next)` with no lock held, then under
  `RenderLock` swaps `next` into `chips_`, restores the selection by `(kind, id)` (below), and calls
  `closeRouting()` (`src/components/UiAppHost.h:73-75`). Closing the gate matters because create
  and retire re-sort the list. Without it, a tap routed before the next render publishes its
  interaction table would carry an old chip index and land on the neighbouring chip (review 0, m5).
  `renderUi()` reopens the gate.
- `buildScreen`: content margin, measure, paging with the `x/y` strip only when there is more than
  one page, laying out one page, drawing each chip as `chipState(chip)`, the focus ring, then
  `buildHeaderExtras(screen)`.
- Swipe page turns (`handleCustomInput`), button step and held-button page turns
  (`navigateButtons`), and `drawFooter`.
- Long-press retire: the confirmation popup, the `saveDisabled` guard, `STUDY.retireTag`, and the
  `render()` override that interleaves the popup (`TagFilterActivity.cpp:156-199`, `:261-278`). A
  long-press acts only on a chip of kind `Tag`.
  - After the attempt, the base calls `onTagRetired(id)` whenever `!STUDY.palette().isActive(id)`,
    as the filter does today (`TagFilterActivity.cpp:193`). `retireTag` retires in memory before it
    saves and does not roll back (`StudyStore.cpp:262-271`), so a failed save must still deselect.
  - A failed save shows `STR_TAG_SAVE_FAILED` ("Could not save tag", `english.yaml:379`) on both
    screens. The filter uses `STR_HIGHLIGHTS_SAVE_FAILED` today (`TagFilterActivity.cpp:198`).
- Back returns a cancelled result, as both screens do today (`TagFilterActivity.cpp:134-139`,
  `TagPickerActivity.cpp:200-205`).
- Home: the base's `handleHomeGesture` consumes Home and does nothing while the retire popup is
  open, otherwise calls the subclass's `onHome()`. The picker guards Home this way today
  (`TagPickerActivity.cpp:207-214`); the filter currently cancels from under the popup
  (`TagFilterActivity.cpp:141-146`), and will not.

**Subclass hooks and their lock contract.** The render task reads anything a render-task hook
reads, so the loop task writes that state only under `RenderLock`.

| Hook | Runs on | Lock | Filter grid | Picker |
|---|---|---|---|---|
| `buildChips(out)` | loop | none held; reads only loop-owned state and `STUDY` | `buildEntries(scope, filter, SIZE_MAX)` | + New tag, then `activeIdsByName()` |
| `chipState(chip)` | render | inside the render | Active if it is the filter | `pickerState` (§4) |
| `buildHeaderExtras(screen)` | render | inside the render | none | the Done pill |
| `activateIndex(i)` | loop | takes `RenderLock` to write | return the filter | toggle, or open the keyboard |
| `onTagRetired(id)` | loop | takes `RenderLock` to write | drop the filter to All if it was `id` | deselect `id` |
| `onHome()` | loop | none | cancel | commit |

**No index offsets.** Each chip carries its meaning in `ChipEntry{kind, id, label}`
(`TagChipView.h:17-22`), and `TagChips::Kind` (`TagChipRow.h:89`) gains `NewTag`. `nav.selected`
is always a chip index, and a tap or long-press reads `chips_[i].kind` and `.id`. The picker's
row↔tag offset arithmetic goes away. So does `TagRowMapping.h`, which exists only because that
offset was applied in some consumers and not others (`TagRowMapping.h:3-7`).

**The selection follows the tag, not the index.** After a rebuild the base looks up the previous
selection's `(kind, id)` in the new list with a pure helper in `TagChipRow.h`. If it is gone
(retired), the selection clamps as `TagFilterActivity::rebuildChips` does today
(`TagFilterActivity.cpp:46-47`). A subclass can ask for a specific id instead; the picker does
after a create.

**Done is not in the button ring.** It is a touch target, and Home commits from anywhere. Today a
button user can also reach the Done row and press Confirm (`TagPickerActivity.cpp:119-122`); after
this change Home is their way to commit. Keeping Done out of the ring means there is no offset to
get wrong.

**Interaction budget:** at most 64 chips per page (`TagChipRow.h:197`) plus Done is 65, under
`UiAppHost::MAX_INTERACTIONS` = 96 (`src/components/UiAppHost.h:35`).

## 4. `TagPickerActivity` on the grid

**Opening.** The constructor keeps only ids that `STUDY.palette().isActive(id)` accepts (D8).
Today it drops only `UNLABELLED` (`TagPickerActivity.cpp:29-30`). A tag retired while another
publication was open stays on this publication's passages, because `retireTag` strips only the
open publication's (`StudyStore.cpp:262-271`). Such an id has no chip but would count toward 8.
Today's Done already drops it (`TagPickerActivity.cpp:216-225`); dropping it at open gives the same
result on Done and makes the counter and the cap count only visible chips. Back still leaves the
passage untouched.

**Header and Done.** The title is `Tags 3/8`, built in `headerTitle()` on the render task with
`snprintf` into a member buffer from `tr(STR_TAGS)`, the selection count and
`PassageDoc::MAX_TAGS_PER_PASSAGE` (`lib/StudyStore/StudyStore/PassageDoc.h:45`).

The header has no ready slot for a button. The theme draws it through its own
`GfxRendererFrame<1>` (`src/components/themes/BaseTheme.cpp:292`), so no hit it registers reaches
the screen. FreeInkUI's built-in `trailingAction` anchors to the band's right edge
(`header.h:122-125`), where both themes put the battery. And the right label is not a
header-height slot: under Classic it is drawn in the corner below the battery strip, one text line
tall (`BaseTheme.cpp:335-339`, `:390-396`).

So the theme gains one overload:
`Rect BaseTheme::drawHeaderWithAction(const GfxRenderer&, Rect rect, const char* title, const char* actionLabel) const`.
`drawHeader` (`BaseTheme.h:224-225`) and its 28 call sites are unchanged.

- It measures `actionLabel` as `TagChipView::measure` does: bold small text plus `2 × spaceLg`
  (`TagChipView.cpp:57-72`). The header refreshes the same shared tokens (`BaseTheme.cpp:292-300`).
- **Battery on the title line** (Lyra: a 44 px battery bar in a 44 px header,
  `src/components/themes/lyra/LyraTheme.h:12-13`): `rightReserve` grows by the action width plus
  `spaceMd` (`BaseTheme.cpp:356-361`). The action sits on the title line immediately left of the
  battery reserve.
- **Battery in a shorter top strip** (Classic: 20 px in a 45 px header,
  `src/components/themes/BaseTheme.h:135-136`): the action sits in the band's lower-right corner
  below the strip, where `manualRightLabel` draws (`BaseTheme.cpp:390-396`). The title gives up
  the action's width. Classic centres its title (`BaseTheme.h:153`), so it gives up that width on
  both sides and stays centred.
- **Battery on the left, or detached:** neither theme uses these (`BaseTheme.h:154-155`,
  `LyraTheme.h:32-33`). The action sits at the title line's right edge.
- It returns the action's visual rect, which never overlaps the battery. The battery-percent text
  changes width, so the rect can move between renders; it is recomputed every frame.

The picker overrides `drawChrome()` (`UiListActivity.cpp:142-146`) to call `drawHeaderWithAction`
and keep the rect in a member. Both `drawChrome` and `buildScreen` run on the render task, in that
order, inside one `render()`. `buildHeaderExtras` then draws an Action chip at the rect. It has
`ACTION_DONE = ACTION_USER`, registered in `onEnter` with `app.on` as Highlights registers
`ACTION_CHIP` (`HighlightsActivity.cpp:42`), and input mask `InputTouch` only, so a long-press
cannot commit. Its `hitPadding` grows the hit rect to the full header band (44 or 45 px), which
meets `minTouchSize`; nothing else in the header is touchable. Done does nothing while the retire
popup is open.

**Chips.** **+ New tag** first, then `activeIdsByName()`, names only. `ChipEntry`'s 40-byte label
holds a 24-byte name (`TagPalette.h:39`). `buildChips` reserves `activeCount() + 1` before
filling.

`TagChips::pickerState(kind, selected, selectedCount, cap)` is a pure function in `TagChipRow.h`:

| Chip | State | Tap or Confirm |
|---|---|---|
| + New tag | Action | keyboard, then `addTagName`, as today (`TagPickerActivity.cpp:144-181`) |
| tag on this passage | Active | remove it |
| tag not on it, fewer than 8 applied | Inactive | add it |
| tag not on it, 8 applied | Disabled | `STR_TAG_LIMIT_PER_HIGHLIGHT`, no change (D7), as a tap at the cap does today (`TagPickerActivity.cpp:134-137`) |

- **One check for tap and Confirm.** `activateIndex` recomputes `pickerState`, because Confirm also
  reaches it through `UiListActivity::handleButtons` (`UiListActivity.cpp:48-59`). This board does
  have a Confirm: a held Right key (`lib/hal/HalGPIO.cpp:213-214`) or a short Power press when
  Settings maps it so (`src/SettingsList.h:373-376`).
- **No flash on a toggle.** A toggle calls `app.clearTapFlash()` during dispatch
  (`FreeInkApp.h:755`), so the repaint shows only the new state (review 0, B1).
- **Locking.** `selectedIds_` changes only under `RenderLock`, because `chipState` and the title
  read it on the render task (review 0, M2).
- **Retire.** A long-press on any tag chip, Disabled included, retires it through the base, and
  `onTagRetired` deselects it.

**New tag at the cap.** Today a tag created at 8/8 is left unapplied with no message
(`TagPickerActivity.cpp:173`). It stays unapplied. If the returned id is not already selected, the
picker shows `STR_TAG_LIMIT_PER_HIGHLIGHT`. A name that dedupes to a tag already on the passage
shows nothing, because the user's intent is already met. After any create, the base selects the
new id, so the grid shows its page.

**Exits.** Done and Home commit; Back cancels. The result is a `TagSelectionResult` of the selected
ids **in palette allocation order** (`activeIds()`), exactly as today (`TagPickerActivity.cpp:216-225`).
Stored order and the sleep screen's tag pill are therefore unchanged (Non-goals). Both callers keep
their constructor call and handler (`src/activities/reader/PassageSelectActivity.cpp:330`,
`HighlightsActivity.cpp:476`).

**Removed:**

- `tags_`, the `TagView` snapshot recopied on every `buildScreen` (`TagPickerActivity.cpp:79`)
- `rowItems_[MAX_ROWS]` (`TagPickerActivity.h:110`)
- `handleButtons`' held-Confirm retire (`TagPickerActivity.cpp:298-320`). The filter grid has no
  such path and retires by touch long-press alone. On this board a synthesised Confirm reports a
  40 ms hold (`lib/Input/Input/NavKeyGestures.h:64-68`), under the 700 ms threshold
  (`TagPickerActivity.cpp:21`), so the path is unreachable here anyway.
- `TagRowMapping.h`, and `test/tag_rows/TagRowsTest.cpp`, which tests it

## 5. The filter grid and the Highlights row

**`TagFilterActivity`** becomes a thin subclass. Its counts, chapter scope, hidden zero-count
chips and result contract are unchanged; the order changes. It also picks up two small changes
from the base (§3): Home no longer cancels from under the retire popup, and a failed retire save
says "Could not save tag" instead of "Could not save highlight". Its class comment
(`TagFilterActivity.h:14-25`) moves to the base where it describes shared behaviour.

**Highlights chip row:** its chips come out in name order, and it calls `draw(..., ChipState)`
(`HighlightsActivity.cpp:707`). The two-line limit and the **…** chip are unchanged. The first two
lines now show the alphabetically first tags rather than the oldest ones.

## 6. Errors, memory, testing

**Errors.** No new failure paths beyond the rank rebuild's OOM fallback (§1). `addTagName`
failure (`TagPickerActivity.cpp:183-198`), a failed retire save, and `saveDisabled` keep their
guards; the retire message is unified (§3).

**Memory**, with the 4,096 B PSRAM routing threshold (`sdkconfig.x4pro:2146-2147`):

| Item | Size | Lives in |
|---|---|---|
| Palette rank | 2 B × entries, resident (~120 B at 60 entries) | internal SRAM |
| Rank rebuild key buffer | entries × 24 B, transient | internal at ~60 entries; PSRAM past ~170 |
| Picker `chips_` | (active + 1) × ~44 B: 2,156 B at 48 tags | internal SRAM; PSRAM only past ~93 chips |
| `GridPage` | ~1 KB (`TagChipRow.h:204-205`) | inside the activity object (heap) |

For the owner's palette the picker therefore uses about 2.2 KB more internal SRAM while it is open
than today. Today's `rowItems_` sits inside an activity object that is already over 4 KB, so it
lands in PSRAM. That matches the filter grid, whose `chips_` is in internal SRAM for the same
reason, and it is released at exit.

**Host tests:**

- `test/tag_palette`:
  - `activeIdsByName` orders by case, accents and the ñ→n fold, skips a leading `¿`/`¡`, breaks
    ties by raw name then id (including two entries loaded with the same name), and excludes
    retired tags.
  - The rank is rebuilt after an appending `add` and after `fromJson`; `retire` and a revive leave
    it unchanged.
  - `nameRank` orders retired ids, and returns `UINT16_MAX` for `UNLABELLED` and unknown ids.
  - The OOM fallback gives allocation order, using the replaced global nothrow `operator new[]`
    pattern with `-fno-builtin` that the repo's OOM tests already use.
- `test/tag_rows`:
  - `TagChips::pickerState` at 7 applied, at 8, and after a deselect at 8.
  - The find-chip-by-`(kind, id)` helper.
  - `candidates` still pins All first and Unlabelled last when given name-ordered ids.
  - `TagRowsTest.cpp` is deleted with `TagRowMapping.h`.
- All four CMake targets that compile `TagPalette.cpp` build with `Fold.cpp` (§1).

**Device checks (owner):**

- A 48-tag picker pages, and toggling a chip never moves another chip.
- Removing a tag repaints that chip white, not inverted.
- At 8/8, inactive chips turn gray; a tap shows the limit message; a long-press still retires.
- **+ New tag** creates a tag and lands on its page; at 8/8 a new name shows the limit message, and
  an existing applied name shows nothing.
- Done sits clear of the title and battery under Lyra and Classic, with the battery percentage on
  and off, and commits on a tap anywhere in the header band's height.
- Done commits, Home commits, Back cancels, from both callers.
- A passage tagged earlier keeps the same sleep-screen tag pill after opening the picker and
  pressing Done with no change.
- The filter grid, the Highlights row and the passage rows are alphabetical, with accented names
  sorted among unaccented ones.
- `ESP.getFreeHeap()` over serial is flat across a picker enter/exit cycle.

## Assumptions for the next review

- **A1.** No render task reads the palette. Both grids render from their chip snapshot. Highlights
  calls `tagNamesFor` only from `rebuildRowItems` on the loop task (review 0, W6). The picker
  mutates the palette only under `RenderLock` (`TagPickerActivity.cpp:160-167`, `:264-277`).
- **A2.** Only an appending `add` and `fromJson` change the set of names in `TagPalette`, so the
  rank cannot go stale.
- **A3.** No path toggles a Disabled chip: tap and Confirm both go through `activateIndex`, which
  recomputes the state.
- **A4.** Keeping Done out of the button ring is safe because Home commits on this board.
- **A5.** `drawHeaderWithAction` places the action clear of the title and the battery in every
  combination the two themes produce, and the title still truncates rather than overlapping it.
- **A6.** `closeRouting()` on every rebuild drops no tap that should have landed, because the next
  render reopens the gate before the user can see the new layout.

## Review 0 outcome

Reviews: `docs/superpowers/reviews/tag-chip-picker-spec-review-0-claims.md` (10 wrong, 2
unverifiable, 70 confirmed) and `docs/superpowers/reviews/tag-chip-picker-spec-review-0-design.md`
(1 blocker, 4 major, 11 minor). Every finding was checked against source. What changed:

| Finding | Change |
|---|---|
| B1 tap flash repaints a removed tag as applied | Flash style follows chip state; toggles clear the flash (§2, §4) |
| W1/M3 no header-height slot beside the battery | Theme-owned `drawHeaderWithAction` (§4, D3) |
| W3/M4 sleep screen reads stored order | Commit stays in palette order (Non-goals, §4) |
| W2/m3 three more test targets compile `TagPalette.cpp` | `Fold.cpp` added to all four (§1) |
| W4/m8 picker `chips_` is internal SRAM at 48 tags | Memory table corrected (§6) |
| M1 hidden ids fill the cap | Dropped at open (D8, §4) |
| M2 unlocked selection | Lock contract per hook (§3, §4) |
| m1/W7 Confirm reachable; silent tap | Check in `activateIndex`; limit message (D7, §4) |
| m2 dedupe at the cap | Message only for an unapplied id (§4) |
| m4 types in a FreeInkUI header | `ChipState` and helpers in `TagChipRow.h`; `NewTag` case (§2, §3) |
| m5 stale index after a re-sort | `closeRouting()` on rebuild (§3) |
| m6 retire callback and string | Fires on `!isActive`; `STR_TAG_SAVE_FAILED` (§3) |
| m7 Home under the popup | Base guards it for both screens (§3) |
| m9 rank rationale, rebuild triggers, bound | Rewritten (§1) |
| m10 collation edges | Leading `¿`/`¡` skipped; the rest accepted (D2) |
| m11 title room, long-press on Done | Title reserves the action; `InputTouch` only (§4) |
| W5, W6, W8, W9, W10 | Claims and citations corrected |
