# btrial-4 — research: `TagChipGridActivity`, with `TagFilterActivity` as its first subclass

Branch `feature/tag-chip-grid-base`, cut from `batch/20261012-tag-chip-picker-2l3m` at `783a1ff4`
(`git fetch origin; git merge-base --is-ancestor origin/batch/20261012-tag-chip-picker-2l3m HEAD`
succeeds and `git log HEAD..origin/batch/...` is empty, so this branch is the batch head). Governing
spec: `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (Goal, D6, Modelled on, §1
`buildEntries`, §3, §4, §5, A1, A6). Every claim below cites a line read in this worktree or a
command run in it.

The two prerequisites are on the base: `TagPalette::activeIdsByName()` (`TagPalette.h:60-62`,
`TagPalette.cpp:98-105`, btrial-1) and `ChipState`, `Kind::NewTag`, `pickerState`, `findChip` and
`TagChipView::draw(..., ChipState, ...)` (`TagChipRow.h:89-99`, `:126-136`; `TagChipView.h:44-45`,
btrial-2). The header seam (`src/components/HeaderLayout.h`, btrial-3) has also landed, but this
task does not use it.

## Who owns the behaviour

| Concern | Owner today |
|---|---|
| The filter grid: chips, paging, focus ring, swipe and button paging, retire popup, Back/Home, result | `src/activities/reader/TagFilterActivity.{h,cpp}` (84 + 295 lines). `final`, derives `UiListActivity` (`TagFilterActivity.h:26`) |
| List protocol: `loop()`, `ACTION_ROW` dispatch, `handleButtons`, `drawChrome`, `drawFooter`, `render` | `src/activities/UiListActivity.{h,cpp}` |
| Routing gate (`uiReady`, `closeRouting`, `renderUi`) | `src/components/UiAppHost.{h,cpp}` (`UiAppHost.h:73-76`, `UiAppHost.cpp:17-27`) |
| Chip entries, measure, draw | `src/activities/reader/TagChipView.{h,cpp}` |
| Pure chip logic: `Kind`, `ChipState`, `candidates`, `findChip`, `GridPage`, paging, `hitPadding` | `src/activities/reader/TagChipRow.h` (FreeInkUI-free, `:12-13`) |
| Retire | `StudyStore::retireTag` (`src/study/StudyStore.cpp:266-275`) |
| Home dispatch | `ActivityManager::loop` (`src/activities/ActivityManager.cpp:73-80`) |
| Only caller of `TagFilterActivity` | `HighlightsActivity::openTagFilter` (`HighlightsActivity.cpp:173-206`) |
| Other `buildEntries` caller | `HighlightsActivity::rebuildChips` (`HighlightsActivity.cpp:130-134`) |

`grep -rn "TagFilterActivity\|TagChipView::\|activeIdsByName\|activeIds()" src` finds no other
consumer of the filter grid.

## The spec's line numbers have drifted

The spec was written before btrial-2 landed, so most `TagFilterActivity.cpp` citations in the spec
and the brief are 2 lines early. Later phases should cite these:

| Spec / brief cites | What | Now |
|---|---|---|
| `TagFilterActivity.cpp:57-118` | `buildScreen` | `:57-120` |
| `:106-117` | focus ring | `:108-119` |
| `:120-132` | `drawFooter` and the `x/y` strip | `:122-134` |
| `:134-139` | `onBackButton` (cancelled result) | `:136-141` |
| `:141-146` | `handleHomeGesture` | `:143-148` |
| `:156-199` | long-press, popup, retire | `:158-201` |
| `:193` | `!isActive(filter)` → All | `:195` |
| `:198` | `STR_HIGHLIGHTS_SAVE_FAILED` | `:200` |
| `:201-259` | swipe, buttons, step, page turn | `:203-261` |
| `:261-278` | `render()` override | `:263-280` |
| `:46-47`, `:79-84` | clamp; strip re-plan | unchanged |
| `TagChipRow.h:97-101` | `candidates` | `:107-124` |
| `TagChipRow.h:197` | `GRID_MAX_CHIPS = 64` | `:219` |
| `TagChipRow.h:204-205` | `GridPage`, "keep it a member" | `:226-233` |
| `english.yaml:379` | `STR_TAG_SAVE_FAILED` | `:380` |
| `StudyStore.cpp:262-271` | `retireTag` | `:266-275` |

Unchanged and verified: `UiListActivity.h:26-28` (`ACTION_ROW = 1`, `ACTION_USER = 2`),
`UiTabListActivity.h:25-26`, `HighlightsActivity.h:103`, `HighlightsActivity.cpp:42`,
`UiAppHost.h:35` and `:73-75`, `TagChipView.cpp:16`, `TagFilterActivity.cpp:22`,
`TagFilterActivity.h:14-25`.

## Current control flow

### Entry

`TagFilterActivity::onEnter` (`:26-34`): `UiListActivity::onEnter()` resets `nav`, calls
`resetUi()` (which closes the gate, `UiAppHost.cpp:12-15`), registers `ACTION_ROW` and the screen
trampoline, and requests an update (`UiListActivity.cpp:20-27`). Then `rebuildChips()`, then under
`RenderLock` `nav.selected = selectedChipIndex()`, the index of the chip `TagChipView::isSelected`
accepts for `filter_` (`:50-55`), or 0.

### `rebuildChips` today

`TagFilterActivity.cpp:40-48`: `buildEntries(scope_, filter_, SIZE_MAX, next)` with no lock, then
under `RenderLock`: move into `chips_`, `widths_.assign(size, 0)`, clamp `nav.selected` into
`[0, last]`. It does **not** restore by `(kind, id)` and does **not** call `closeRouting()`. Its
only callers are `onEnter` (`:28`) and `retireTag` (`:197`).

### `buildScreen` (render task)

`:57-120`: content margin below the header, above the hints; `spacer`; measure every chip into
`widths_` (the render task writes `widths_`' contents, `:70`); `linesPerPage`, `pageCountOf`; when
`pageCount_ > 1` take `metrics.tabBarHeight` for the strip and re-plan (`:79-84`); `pageHolding` for
`nav.selected`, write `pageNumber_` and `nav.top` (`:86-89`); `layoutPage` into the member `page_`
(`:90`); draw each placed chip with `ACTION_ROW`, `value = chip index`,
`InputTouch | InputLongPress`, and `hitPadding` (`:94-106`). The chip state is computed inline from
`filter_` (`:100-101`). If `buttonFocus_`, stroke the focus ring clamped to the body (`:108-119`).

`drawFooter` (`:122-134`) draws the hints, then the `x/y` strip with `GUI.drawSubHeader` only when
`pageCount_ > 1`. Neither registers an interaction.

### Input, in the order the loop sees it

1. **Home**: `ActivityManager::loop` checks `wasHomeGesture()` *before* calling the activity's
   `loop()` and gives it to `handleHomeGesture()` (`ActivityManager.cpp:75-80`). So the popup's own
   input handling never sees Home. The filter's `handleHomeGesture` calls `onBackButton()`
   unconditionally (`:143-148`), so today Home cancels the screen from under an open retire popup.
   The picker guards it (`TagPickerActivity.cpp:207-214`).
2. `UiListActivity::loop` (`UiListActivity.cpp:84-107`): `handleCustomInput` → `handleButtons` →
   `routeListTouch` → base swipe scroll → `navigateButtons`.
3. `handleCustomInput` (`:203-218`): while the popup is active, `confirmPopup_.handleInput` returns
   true for every pass (`OptionPopup.h:65-133`, the trailing `return true`), so nothing below it
   runs. A dismissed popup clears `confirmingDelete_`. Swipe Up/Down turns a page and is consumed,
   so the base's row scroll (`UiListActivity.cpp:91-104`) never runs for this screen.
4. `handleButtons` is the base's (`UiListActivity.cpp:48-59`): Back → `onBackButton`; Confirm →
   `activateIndex(nav.selected)` directly, not through `onRowAction`.
5. `routeListTouch` → `routeTouch(mappedInput, wantsTouchLongPress)` (gated on `uiReady`,
   `UiAppHost.cpp:29-40`) → `rowActionTrampoline` bounds-checks against `listCount()`
   (`UiListActivity.cpp:33-37`) → the filter's `onRowAction` clears `buttonFocus_` under the lock,
   then the base writes `activeNav().selected = event.value` **without** the lock and calls
   `onRowLongPress` or `activateIndex` (`UiListActivity.cpp:39-46`).
6. `navigateButtons` (`:220-227`): release steps one chip (`stepSelection`, `:229-238`), a held
   button turns a page (`turnPage`, `:240-261`). Each handler takes `RenderLock` itself.

### Retire

`onRowLongPress` (`:158-166`) ignores the press while the popup is up, out of range, or on a chip
whose kind is not `Tag`; clears the flash; `showRetireConfirmation(chip.id)`. That bails with
`STR_HIGHLIGHTS_LOAD_FAILED` when `STUDY.saveDisabled()` (`:170-176`), else captures
`pendingRetireId_` and shows the popup. Its callback runs on the loop task from inside
`handleInput` with no lock held (`OptionPopup.h:82`, `:123`) and calls `retireTag`.

`retireTag` (`:189-201`): `STUDY.retireTag(id)` with no lock; then under `RenderLock`, reset
`filter_` when it names a tag that is no longer active; then `rebuildChips()` (which takes the lock
again, separately); `requestUpdate()`; on failure `STR_HIGHLIGHTS_SAVE_FAILED`.

`StudyStore::retireTag` (`StudyStore.cpp:266-275`) returns false *without* retiring when
`saveDisabled_` or the id is `UNLABELLED`; otherwise it retires in memory first, then saves the
palette, and returns false on a failed save with the tag already retired and no rollback. That is
why the spec keys `onTagRetired` on `!isActive(id)`, not on the return value.

### Render

`TagFilterActivity::render` (`:263-280`) is `UiListActivity::render` with the popup interleaved:
chrome, `renderUi`, the bounded rebuild loop, then `if (confirmPopup_.processRender(...)) return;`
(`OptionPopup.h:134-140` draws hints and the popup and displays), else footer, `displayBuffer()`,
`PostedMessage::drawNext`. It does not honour `halfRefreshPending`; the filter never sets it.

### Exits and the result contract

- Back: cancelled result, `finish()` (`:136-141`).
- Home: same as Back today (`:143-148`).
- Chip tap or Confirm: `activateIndex` (`:282-295`) ignores it while the popup is up, clears the
  flash, and returns a `TagSelectionResult`: empty for All, else `{chip.id}` (`UNLABELLED` for
  Unlabelled).
- The caller guards on `std::holds_alternative<TagSelectionResult>` and drops a retired filter
  whatever the result (`HighlightsActivity.cpp:183-194`).

## Concurrency facts the base has to respect

**`RenderLock` is not recursive.** It takes `activityManager.renderingMutex` with `portMAX_DELAY`
(`ActivityManager.cpp:328-336`), created by `xSemaphoreCreateMutex()` (`ActivityManager.h:70`), a
plain FreeRTOS mutex. A hook documented "takes `RenderLock` to write" therefore must be called with
no lock held, or the loop task deadlocks on itself. Today every `RenderLock` scope in the filter is
disjoint (`:30`, `:43`, `:152`, `:194`, `:231`, `:244`), and `retireTag` releases its lock before
`rebuildChips` takes its own (`:191-197`).

Who touches what today:

| State | Written by | Read by |
|---|---|---|
| `chips_` | loop, under the lock (`:44`) | render (`:70`, `:96`); loop, unlocked (`:161`, `:291`) — safe, the loop is its only writer |
| `widths_` | size: loop under lock (`:45`); contents: render (`:70`) | loop `turnPage`, under lock (`:247`) |
| `page_`, `gridLineWidth_`, `gridGap_`, `gridLinesPerPage_`, `pageNumber_`, `pageCount_` | render (`:74-90`) | render; `turnPage` under lock (`:244-253`) |
| `nav.selected` | loop, locked (`:31`, `:47`, `:234`, `:255`), **and unlocked in `UiListActivity::onRowAction` (`UiListActivity.cpp:40`)** | render (`:87`, `:105`) |
| `nav.top` | render (`:89`) | — |
| `buttonFocus_` | loop, locked (`:153`, `:235`, `:256`) | render (`:108`) |
| `filter_` | loop, locked (`:195`) | render (`:101`); loop |
| `confirmPopup_`, `confirmingDelete_`, `pendingRetireId_` | loop | render: `processRender` reads the popup (`:275`) |

The unlocked `nav.selected` write in the base `onRowAction` is pre-existing and shared with every
`UiListActivity`; it is a single aligned `int` store. The base can take the lock around it when it
overrides `onRowAction` (it has to override it anyway, to clear `buttonFocus_`).

**The gate.** `closeRouting()` stores `uiReady = false` (`UiAppHost.h:75`), an `std::atomic<bool>`
(`:84`). `renderUi()` sets it true after `app.render()` publishes (`UiAppHost.cpp:17-27`), and the
render task holds `RenderLock` for the whole `render()` (taken at `ActivityManager.cpp:52`, moved into `render` at `:59`). Closing it
under `RenderLock` therefore orders the close after any in-flight render built against the old
chips, so that render cannot reopen the gate for a stale table; the next render, built from the
new `chips_`, reopens it. That is the basis for A6.

**A1 holds on this branch.** The filter's render task reads `chips_` labels and `filter_`, never the
palette or the passages. `buildEntries` reads `STUDY` and runs on the loop task; Highlights calls it
under `RenderLock` from `rebuildRowItems` (`HighlightsActivity.cpp:45-47`, `:97-128`, `:130-134`).

## Ordering change (`buildEntries`)

`TagChipView::buildEntries` takes `STUDY.palette().activeIds()` (`TagChipView.cpp:16`) and passes
the same vector to `count`/`countIn` and to `candidates` (`:17-20`). `Counts::perTag` is aligned
with whatever ids it was given (`TagChipRow.h:24`, `Tally` at `:30-40`), so swapping in
`activeIdsByName()` keeps counts aligned without any other change. `candidates` emits All, the tags
in the given order, then Unlabelled (`TagChipRow.h:107-124`); the existing host test
`TagChipCandidates.NameOrderedIdsKeepAllFirstAndUnlabelledLast` (`TagChipRowTest.cpp:415`) pins
that for name-ordered ids. `activeIdsByName` reserves `activeCount()` and walks `byName_`
(`TagPalette.cpp:98-105`); like `activeIds` (`:89-96`) it is one vector allocation, so the call
under Highlights' lock costs what it does today.

Already done in the batch: `StudyStore::activeTags()` iterates `activeIdsByName()`
(`StudyStore.cpp:160-165`). One side effect is known and owned elsewhere: the current picker
commits in `tags_` order, now name order, until `t5` restores palette order (btrial-1 spec A18,
`docs/superpowers/specs/2026-10-12-btrial-1-design.md:507-511`).

## What the picker (§4) will need from the base

Read against today's `TagPickerActivity.{h,cpp}`:

- **`activateIndex` while the popup is up.** The filter's own guard reads `confirmPopup_`
  (`:283`). Once the popup belongs to the base, a subclass's `activateIndex` needs to see it: a
  protected accessor, or a base-owned guard before the hook. Same for the picker's Done handler,
  which "does nothing while the retire popup is open" (§4).
- **`drawChrome` is already a virtual seam** (`UiListActivity.h:66`, `.cpp:142-147`) and the
  filter's render calls it before `renderUi` (`:267`, `:271`), so the picker's `drawChrome`
  override runs before `buildScreen` → `buildHeaderExtras` in one `render()`, as §4 requires. The
  base's `render` must keep calling `drawChrome()`, not `GUI.drawHeader` directly.
- **Action ids.** The base needs no action of its own: chips use `ACTION_ROW`. `ACTION_USER` stays
  free for `ACTION_DONE`. The precedent for publishing a first free id is
  `UiTabListActivity::ACTION_TAB_USER = ACTION_USER + 1` (`UiTabListActivity.h:24-26`), which no
  subclass uses yet (`grep -rn ACTION_TAB_USER src` finds only its declaration).
- **Requesting a selection.** The picker selects a created tag by id (§4). The filter's entry
  selection is the chip for `filter_`, which for All and Unlabelled is a kind, not a palette id:
  `isSelected` maps `nullopt` → All, `UNLABELLED` → Unlabelled, otherwise → that `Tag`
  (`TagChipView.cpp:48-60`). `findChip(chips, kind, id)` already matches All, Unlabelled and NewTag
  by kind alone (`TagChipRow.h:126-136`). So a request expressed as `(kind, id)` serves both
  subclasses; one expressed as a bare `TagId` does not serve the filter's All.
- **Messages.** The filter's render calls `PostedMessage::drawNext` (`:279`); the picker's does not
  (`TagPickerActivity.cpp:322-341`). `ReaderUtils::showMessage` only posts
  (`ReaderUtils.h:233`), so the picker's limit and save-failure messages today appear on whatever
  screen renders next. A base `render` modelled on the filter's fixes that for the picker when it
  moves onto the base.
- **Back and Home.** Both screens' Back already returns a cancelled result
  (`TagFilterActivity.cpp:136-141`, `TagPickerActivity.cpp:200-205`).
- **Selected state on the render task.** The picker's `chipState` will read `selectedIds_`, which
  today it writes without the lock (`TagPickerActivity.cpp:42-49`, `:140`, `:173`); §4 moves those
  writes under `RenderLock`. That is the picker task's, but the hook table must say `chipState` runs
  inside the render.

## Interaction budget

A page places at most `GRID_MAX_CHIPS = 64` chips (`TagChipRow.h:219`, enforced by
`placed >= GRID_MAX_CHIPS` at `:259`). Nothing else on the filter screen registers an interaction:
`drawChrome` draws through `GUI.drawHeader` and `drawFooter` through `drawButtonHints` and
`drawSubHeader`, neither through `app`. 64 + the picker's one Done = 65 <
`MAX_INTERACTIONS = 96` (`UiAppHost.h:35`). `renderUi` logs an overflow (`UiAppHost.cpp:23-25`).

## Testability

No host test compiles an activity `.cpp`, `TagChipView.cpp` or `UiAppHost`:
`grep -rn "Activity\.cpp\|UiAppHost\|FreeInkUI" test --include=CMakeLists.txt` prints nothing. The
tests that include `activities/` headers include pure ones (`NumberGridLayout.h` and the like).
So `TagChipGridActivity` and the `buildEntries` order change are verified by `pio run` and on the
device; anything the base needs that is worth a host test must be a pure function in
`TagChipRow.h`, tested in `test/tag_rows/TagChipRowTest.cpp` (already in the `TagRowsTest` target,
`test/tag_rows/CMakeLists.txt:1-5`, with `src` and `lib/StudyStore` on the include path, `:7-10`).
`findChip` already has four tests there (`TagChipRowTest.cpp:655-678`, using a `FakeChip` struct,
`:642-645`). The "restore by `(kind, id)`, else clamp" rule is the one piece of new base logic that
fits this mould.

## Tools and versions

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on `PATH` in this shell) |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| `freeink-sdk` | `7dde134` | `git submodule status` |
| CMake (host) | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 (clang-2100.0.123.102) | `c++ --version` |
| googletest / ArduinoJson (host) | v1.17.0 / v7.4.2 | `test/CMakeLists.txt:14-18`, `:28-33` |
| C++ | GNU++20 on device (`platformio.ini:40`); C++20, `-Wall -Wextra -pedantic` on host (`test/CMakeLists.txt:4-6`, `:42-46`) | |

No `build_src_filter` in `platformio.ini` (`grep -n src_filter platformio.ini` is empty), so a new
`src/activities/reader/TagChipGridActivity.cpp` is compiled with no build-file change.

Baseline, before any change: `cmake -S test -B build/test && cmake --build build/test --target
TagRowsTest && ./build/test/tag_rows/TagRowsTest` → `[  PASSED  ] 65 tests.` (`build/` is
ignored; `git status --short` stays empty.) The full host suite, `cmake --build build/test && ctest --test-dir
build/test -j8` → `100% tests passed out of 1736`. The host build already prints warnings from
sources this task does not touch (`-Wsign-compare` in `lib/Epub/Epub/ParsedText.cpp:968`,
`:970` and `TextBlock.cpp:396`; unused `MODULE` in `src/study/TagPaletteFile.cpp:12`,
`PassageFile.cpp:10`, `CoverBandCache.cpp:14`, `HomeVerseFile.cpp:16`; unused `label` in
`lib/EpdFont/FontDecompressor.cpp:524`), so "no new warnings" is judged against that list.

## Nearest existing example

- **`UiTabListActivity`** (`src/activities/UiTabListActivity.{h,cpp}`, added in `9e3cea2e`, "feat:
  convert all lists and tabs to FUI"): a non-final `UiListActivity` subclass that is itself the base
  for two screens (`SettingsActivity`, `TextSettingsActivity`). It declares its hooks pure virtual
  with a one-line contract each (`UiTabListActivity.h:30-42`), overrides `onRowAction` and
  `navigateButtons` for the whole family (`:48-51`), registers its own action in `onEnter`
  (`UiTabListActivity.cpp:16-22`), and publishes the first free action id (`:24-26`). That is the
  shape §3 asks for.
- **`HighlightsActivity`'s `ACTION_CHIP`**: `ACTION_CHIP = ACTION_USER` (`HighlightsActivity.h:103`)
  registered with `app.on(...)` after `UiListActivity::onEnter()` (`HighlightsActivity.cpp:40-43`),
  the pattern the picker's Done will follow.
- **The extraction itself** is mostly a move of `TagFilterActivity.cpp:40-280`. What changes in the
  moved code: the chip state comes from a hook (`:100-101`), `rebuildChips` gains restore-by-
  `(kind, id)` and `closeRouting()` (`:40-48`), `retireTag` calls `onTagRetired` and says
  `STR_TAG_SAVE_FAILED` (`:189-201`), and Home is guarded by the popup (`:143-148`).

## Scope and tier

The change stays in surface `ui`: `src/activities/reader/TagChipGridActivity.{h,cpp}` (new),
`TagFilterActivity.{h,cpp}`, `TagChipView.cpp`, and possibly `TagChipRow.h` with tests in the
existing `TagChipRowTest.cpp`. None of the shared append points is needed:
`STR_TAG_SAVE_FAILED` already exists (`english.yaml:380`), no new host target means no
`test/CMakeLists.txt` line, and `src/main.cpp` does not construct the filter. No on-disk format,
store or cache version moves. Tier stays `heavy`; nothing found warrants raising it.

## Questions for the spec

1. Where the popup guard lives for `activateIndex` and the picker's Done (base-owned guard before
   the hook, or a protected `retirePopupOpen()`).
2. How a subclass requests a selection: `(kind, id)` serves both the filter's entry selection and
   the picker's create; a bare id does not cover All.
3. Whether the restore-or-clamp rule becomes a pure `TagChips` helper with host tests, beside
   `findChip`.
4. Whether the base's `onRowAction` override takes the lock around the `nav.selected` write the
   base class leaves unlocked.
5. Whether to publish a named first free action id (`UiTabListActivity` style) or document that
   `ACTION_USER` is free.
