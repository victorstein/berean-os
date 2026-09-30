# Issue #205 research — tag chip row in Highlights/Tags

Branch `feature/205-tag-chip-row`, base `67eaaf1e` (release 1.21.0, which includes #194's compact
metrics as `3591e3fe`). Every claim below cites a line read on this branch or a command run on
2026-09-29.

## Installed tools and packages

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| CMake (host tests) | 4.4.2 | `cmake --version` |
| freeink-sdk gitlink | `67f7e01` | `git submodule status freeink-sdk` |
| UI small font | Ubuntu 10; has U+2026 `…` | `lib/EpdFont/builtinFonts/ubuntu_10_regular.h:2557` (`{ 0x2026, 0x2026, 0x32F }`), bold `ubuntu_10_bold.h:2755` |

No new library or package is involved.

## Which files own the behaviour

| File | Role |
|---|---|
| `src/activities/reader/HighlightsActivity.{h,cpp}` | The screen. Row 0 is the filter control; rows 1.. are passages. |
| `src/activities/reader/TagFilterActivity.{h,cpp}` | Today's picker: "All", "Unlabelled", then each active tag; long-press retires a tag. |
| `src/activities/reader/TagRowMapping.h` | Host-testable row arithmetic for both pickers (`TagRows`, `FilterRows`). |
| `src/study/StudyStore.{h,cpp}` | `passages()`, `activeTags()`, `passagesWithTag()`, `tagName()`, `tagNamesFor()`, `palette()`. |
| `lib/StudyStore/StudyStore/TagPalette.h`, `PassageDoc.{h,cpp}` | `TagId`, `UNLABELLED`, normalisation, `unlabelledCount()`. |
| `src/activities/UiListActivity.{h,cpp}` | Base list protocol: touch routing, nav, `syncListViewport`, render skeleton. |
| `test/tag_rows/` | Host suite for `TagRowMapping.h` (`TagRowsTest.cpp`, `PassageActionsTest.cpp`). |

The only launch site is the reader menu: `EpubReaderActivity.cpp:347-348`. There is no Home entry
for Tags today; the mockup's "Tags becomes its own entry on Home" is phase 5, not this issue.

## Current control flow

1. **Entry.** `HighlightsActivity::onEnter` → `rebuildVisibleIndices()` → `rebuildRowItems()`
   (`HighlightsActivity.cpp:32-36`).
2. **Filtering.** `filterTagId_` is `std::optional<study::TagId>`; `nullopt` means "All"
   (`HighlightsActivity.h:127-134`). `rebuildVisibleIndices` walks `STUDY.passages()` newest-first
   and keeps a passage only if `std::find(tags, *filterTagId_)` hits (`HighlightsActivity.cpp:42-55`).
   The same test serves UNLABELLED, because a passage with no real tag *carries*
   `study::UNLABELLED` explicitly: `normaliseTags` never returns an empty list
   (`PassageDoc.cpp:15-26`), and `removeTagEverywhere` re-adds UNLABELLED when the last tag goes
   (`PassageDoc.cpp:202-205`).
3. **Row 0.** `rebuildRowItems` pushes a `ListItem` labelled `STR_FILTER_BY_TAG` with the current
   filter name as subtitle (`HighlightsActivity.cpp:84-89`), so `listCount()` is
   `visibleIndices_.size() + 1` (`:38`).
4. **Opening the picker.** Tapping row 0 → `activateIndex(0)` → `openTagFilter()`
   (`:171-174`, `:117-143`). The result handler takes `TagSelectionResult.tagIds` — empty means
   All, otherwise `.front()` — then **always** runs `dropRetiredFilter()`, rebuilds, and
   `moveSelectionTo(0)` (`:124-141`). "Always" matters: the picker can retire a tag even when
   cancelled (`TagFilterActivity.cpp:113-129`).
5. **Picker contents.** `TagFilterActivity` snapshots `STUDY.activeTags()` (allocation order,
   `StudyStore.cpp:129-133`) and lays rows out per `FilterRows`: All = 0, Unlabelled = 1, then tags
   (`TagRowMapping.h:28-42`, `TagFilterActivity.cpp:43-58`). Selecting Unlabelled returns
   `study::UNLABELLED` (`:170-171`).
6. **Retired filters.** `dropRetiredFilter` resets a filter naming a retired tag, and skips the
   check for UNLABELLED, which is in no palette (`HighlightsActivity.cpp:63-69`).
7. **Other data changes.** Retagging (`applyTagEdit`, `:339-370`) and deleting (`deleteHighlight`,
   `:393-415`) both rebuild under `RenderLock`, because `rowItems_` borrows pointers into passage
   strings that the render task reads concurrently (`:357-365`, `:403-410`).
8. **Empty publication.** With zero passages, `buildScreen` shows `STR_NO_HIGHLIGHTS` and draws no
   filter row (`:477-482`).
9. **Layout.** `buildScreen` sets the content margin below the header, adds `verticalSpacing`, then
   a subtitle-bearing list (`:468-510`). Row height goes through
   `UiListActivity::syncListViewport` → `ListRowHeight::resolve` (`UiListActivity.cpp:124-140`,
   `ListRowHeight.h:19-28`). On touch with a subtitle it returns the FreeInkUI token row height,
   not `touchListRowHeight`, which only applies to one-line rows. So **rows already use #194's
   metrics path**; the `hasSubtitle=true` passage rows are unaffected by `touchListRowHeight = 50`
   (`LyraTheme.h:20`).

### What counting would read

- "All n" = `STUDY.passages().size()` — the list under "All" is exactly every passage
  (`HighlightsActivity.cpp:48-54` with no filter).
- Per-tag n = passages whose `tags` contain the id, the same predicate as the filter. The store
  already exposes it as `passagesWithTag(id)`, which returns a `std::vector<size_t>` per call
  (`StudyStore.cpp:135-146`) — calling it once per chip allocates once per tag. A single pass over
  `passages()` counting into a per-tag slot gives the same numbers without that.
- "Unlabelled n" = `PassageDoc::unlabelledCount()` (`PassageDoc.cpp:254-259`), which is the same
  predicate, because normalisation guarantees UNLABELLED appears only alone. `StudyStore` does not
  forward `unlabelledCount()` (only `passages()`, `StudyStore.h:88`), so the chip row counts
  UNLABELLED like any other id.
- A passage carries at most `MAX_TAGS_PER_PASSAGE = 8` tags (`PassageDoc.h:45`); the palette holds
  at most `MAX_ACTIVE_TAGS = 200` (`TagPalette.h:38`); names are at most 24 bytes
  (`TagPalette.h:39`). The passage file's cap is a byte budget (`SAVE_BYTE_BUDGET = 200000`,
  `PassageDoc.h:38`), not a count.
- Chip order is not stated in the issue. The mockup lists `All 37, hope 12, ministry 9, name 4,
  trust 5, study 3, Unlabelled 4, …` — `name 4` before `trust 5`, so it is **not** sorted by
  count; it matches palette allocation order, which is also `TagFilterActivity`'s order. The
  mockup puts Unlabelled *after* the tags; the picker puts it second.
- The palette is global (`TagPalette.h:10-12`), so tags coined in another publication are active
  here with a count of 0 (`HighlightsActivity.h:18-21`). Whether a 0-count chip is drawn is a
  spec decision; the issue's examples only show non-zero counts.

## Rendering primitives available

- **No chip component exists.** `grep -rli chip` over `src lib freeink-sdk/libs/ui` hits only
  unrelated files (board tags, OTA, `TagPalette.h`'s comment). FreeInkUI's components are listed
  under `freeink-sdk/libs/ui/FreeInkUI/include/components/{bars,controls,keyboard,lists,media,overlays,text}`
  and none wraps a row of labelled pills.
- **A button at an explicit rect** is the nearest primitive: `Screen::button(const ButtonProps&,
  Rect)` (`FreeInkApp.h:220-231`, "for layouts the row cadence can't express"). `ButtonProps`
  carries `action`, `value`, `state`, `styles`, `radius`, `hitPadding`, `minTouchSize`
  (`components/controls/button.h:8-33`). It registers a hit rect of at least `minTouchSize` via
  `ensureMinTouchRect` (`button.h:38-44`) — relevant because pills in a wrapped row sit close
  together and enlarged hit rects can overlap neighbours; `hitPadding` exists to split gaps.
- **Default button "selected" is not inverted.** The stock style keeps black text on
  `selectedBackground` (`FreeInkUICore.h:1519-1530`). The existing inverted look is hand-built in
  `UiTabListActivity.cpp:160-182`: `selected.background = solid(Black)`, `selected.foreground =
  solid(White)`, with `focused`/`active` copied from `selected` so a tap flash keeps the pill. Its
  other two branches use `Paint::dither`, which the overhaul rules forbid under text.
- **Text measurement** for width-fitting is `screen.target().measureText(font, text, style)`
  (`FreeInkUICore.h:687`), as `button.h:62` does.
- **Extra actions on a list screen** are registered with `app.on(ACTION_USER + k, handler, this)`
  in `onEnter`; `BibleSearchActivity` is the nearest example
  (`BibleSearchActivity.h:68-70`, `BibleSearchActivity.cpp:102-104`, buttons placed at rects
  `:771-781`). `UiListActivity` routes all hits through the same `app` (`UiListActivity.cpp:58-67`).
- **Interaction budget.** `UiAppHost::MAX_INTERACTIONS = 64`; hits past it are dropped silently and
  become untappable (`UiAppHost.h:24-37`). Chips + visible list rows + any other hit rects share it.
  `NumberGrid::MAX_CELLS = 48` exists for exactly this reason (`NumberGridLayout.h:15-18`). A
  two-line chip cap bounds the chip count by width, but the spec must still state a hard cap.

## Nearest existing examples of this kind of change

1. **Pure layout arithmetic in a header, host-tested:** `src/activities/reader/NumberGridLayout.h`
   ("Deliberately free of FreeInkUI, Arduino and GfxRenderer so the host suite can exercise it",
   `:6-8`), tested by `test/number_grid/NumberGridLayoutTest.cpp`; also
   `src/components/ListRowHeight.h` tested in `test/ui_layout`. The chip counting and wrap/overflow
   logic fits this shape: plain ints and widths in, chip placements and an overflow flag out.
2. **Tag-row mapping beside its consumers:** `TagRowMapping.h` and `test/tag_rows/TagRowsTest.cpp`.
   A new test file can join `test/tag_rows/CMakeLists.txt` (its own `add_executable` list), which is
   **not** the shared root `test/CMakeLists.txt`; a new test directory would need a hand-off line
   for the root file (`test/CMakeLists.txt:76,101-102` show the `add_subdirectory` pattern).
3. **Inverted selected pill:** `UiTabListActivity.cpp:160-182`.
4. **Buttons at rects with user actions on a `UiListActivity`:** `BibleSearchActivity.cpp:102-104,
   755-781`.

## Strings

Existing keys cover the chip labels: `STR_TAG_FILTER_ALL: "All"` and `STR_TAG_UNLABELLED:
"Unlabeled"` (`english.yaml:389-390`); `STR_FILTER_BY_TAG` (`:388`) titles the picker. The count is
a number, so "hope 12" can be `snprintf("%s %u")` of the tag name without a new key. `…` is a glyph,
not a word; whether it needs a key is a spec call. The mockup titles the screen "Tags";
`STR_TAGS: "Tags"` exists (`english.yaml:376`), the screen uses `STR_HIGHLIGHTS` today
(`HighlightsActivity.cpp:40`).

## Open questions for the spec

- **"…" trigger.** The issue says `…` appears "when they overflow onto a second line", and the
  acceptance criteria say "more tags than fit on two lines". The mockup shows two lines with `…` as
  the last chip on line two. The reading consistent with both is: chips wrap onto at most two
  lines, and when the rest do not fit, the last slot on line two becomes `…`, which opens
  `TagFilterActivity`.
- **Selected chip hidden behind `…`.** If the active filter is a tag that did not fit, nothing
  would appear inverted. Options: invert `…`, or promote the selected chip into view.
- **0-count tags** (global palette): draw them or skip them.
- **Unlabelled with 0**: draw it or skip it.
- **Physical buttons.** `handleButtons` still handles Confirm/Back (`HighlightsActivity.cpp:438-459`),
  and the nav ring today includes row 0. Chips are touch targets; whether button navigation reaches
  them, or keeps a row that opens the picker, has to be decided. The X4 Pro is touch-first
  (`CLAUDE.md`, "Input hardware").
- **Long-press on a chip.** The picker's long-press retires a tag (`TagFilterActivity.cpp:83-90`);
  the issue does not ask for that on a chip, and doing it silently would be destructive.
- **Screen title** "Highlights" vs the mockup's "Tags".

## Scope and tier

No store, format, network or HAL change: counting reads `STUDY.passages()` and the palette, both
already in memory. The work stays in `src/activities/reader/` plus a host test under
`test/tag_rows/`. No `test/CMakeLists.txt` edit is needed if the test joins that directory, and no
new translation key is needed unless the spec adds one. The review tier stays **standard**.
