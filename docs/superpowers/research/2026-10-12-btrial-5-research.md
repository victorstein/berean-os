# btrial-5 — research: `TagPickerActivity` on the chip grid

Branch `feature/tag-picker-grid`, level with `origin/batch/20261012-tag-chip-picker-2l3m` at
`46b096bc`. After `git fetch origin`, both `git log HEAD..origin/batch/...` and
`git log origin/batch/.....HEAD` are empty. `hpipe status` lists t1–t4 as done and t5 as the last
task, so no sibling is waiting to rewrite these files. Governing spec:
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (Problem, Goal, Non-goals, D3–D5, D7,
D8, §3 hooks, §4, §6 picker rows, A1, A3, A4). Every claim below cites a line read in this worktree
or a command run in it.

The four prerequisites are on the base:

| Prerequisite | Where |
|---|---|
| `TagPalette::activeIdsByName()`, `nameRank()` (btrial-1) | `lib/StudyStore/StudyStore/TagPalette.h:60-66` |
| `ChipState`, `Kind::NewTag`, `pickerState`, `findChip` (btrial-2) | `src/activities/reader/TagChipRow.h:89-99`, `:126-136` |
| `TagChipView::draw(..., ChipState, ...)` (btrial-2) | `TagChipView.h:44-45` |
| `STR_TAG_NEW_CHIP: "+ New tag"` (btrial-2) | `lib/I18n/translations/english.yaml:374` |
| `BaseTheme::drawHeaderWithAction` (btrial-3) | `src/components/themes/BaseTheme.h:226-232`, `BaseTheme.cpp:291-294`; geometry in `src/components/HeaderLayout.h:55-109` |
| `TagChipGridActivity` and its hooks; `ChipKey`, `reselect` (btrial-4) | `src/activities/reader/TagChipGridActivity.{h,cpp}`; `TagChipRow.h:138-161` |

## Who owns the behaviour

| Concern | Owner today |
|---|---|
| The picker: rows, toggle, cap, create, retire, exits, result | `src/activities/reader/TagPickerActivity.{h,cpp}` (111 + 341 lines), `final`, derives `UiListActivity` (`TagPickerActivity.h:46`) |
| Row ↔ tag offset arithmetic | `src/activities/reader/TagRowMapping.h` (23 lines). Only callers: `TagPickerActivity.cpp:12,54` and `test/tag_rows/TagRowsTest.cpp:3` |
| The shared grid: paging, focus ring, swipe and button stepping, retire popup, Back, Home guard | `TagChipGridActivity.{h,cpp}` (109 + 283 lines) |
| Chip look per state | `TagChipView::draw` (`TagChipView.cpp:79-128`) |
| Picker chip state | `TagChips::pickerState` (`TagChipRow.h:93-99`) |
| Header with an action slot | `BaseTheme::drawHeaderWithAction` → `drawHeaderBand` (`BaseTheme.cpp:291-416`) |
| Palette edits | `StudyStore::addTagName`, `retireTag` (`src/study/StudyStore.cpp:250-273`) |
| The two callers | `PassageSelectActivity.cpp:329-341`, `HighlightsActivity.cpp:474-477` with `applyTagEdit` at `:479-491` |
| Result type | `TagSelectionResult` (`src/activities/ActivityResult.h:81-88`) |

## The commit order has already drifted on this base

The brief says the result keeps "palette allocation order (`activeIds()`), exactly as today
(`TagPickerActivity.cpp:216-225`)". On **this branch** that code no longer does that.

- `commitAndFinish` iterates `tags_` (`TagPickerActivity.cpp:220-222`).
- `tags_` is `STUDY.activeTags()` (`:27`, `:35`, `:79`, `:166`, `:272`).
- `activeTags()` now walks `palette_.activeIdsByName()` (`src/study/StudyStore.cpp:160-164`). It was
  changed by btrial-1's `b984a3ef feat: list study tags in display order`
  (`git log -S activeIdsByName -- src/study/StudyStore.cpp`).
- On `main` it walked `palette_.activeIds()`: `git show main:src/study/StudyStore.cpp`, lines
  159-163.

So the batch head commits a passage's tags in **name** order. The sleep screen shows the first stored
tag, so this would change its pill (batch Non-goals). The new commit must iterate
`STUDY.palette().activeIds()` itself. It cannot go through `activeTags()`.

After this change, `StudyStore::activeTags()` has no caller. `grep -rn -e 'activeTags()' src lib test`
finds only the picker's five uses, its definition and its declaration. Removing it is `src/study`
work and is not in the brief. File it as a discovery once the picker stops calling it.

## Current control flow (the code being replaced)

**Entry.**
- The constructor copies `STUDY.activeTags()` into `tags_` and moves `initialSelection` into
  `selectedIds_`.
- It erases only `UNLABELLED` (`TagPickerActivity.cpp:24-31`).
- `onEnter` re-reads `tags_` (`:33-36`).
- `TagPalette::isActive` returns false for an id with no entry (`TagPalette.cpp:67-72`), and
  `UNLABELLED` is never allocated (`TagPalette.h:29-31`, `nextRaw_ = 1` at `:89`). So D8's
  `isActive` filter also drops `UNLABELLED`, as today's erase does.

**Rows.**
- `buildScreen` re-copies `tags_` on every render (`:79`).
- It fills `rowItems_[MAX_ROWS]` in the order Done, one toggle per tag, New tag... (`:84-102`).
- `MAX_ROWS = MAX_ACTIVE_TAGS + 2 = 202` (`TagPickerActivity.h:57`, `TagPalette.h:38`).
- The list's input mask is `InputTouch | InputLongPress` (`:109`).

**Activate** (`:114-128`). Row 0 commits. The last row opens the keyboard. Any other row maps through
`TagRows::tagIndexForRow` and goes to `toggleTag`.

**Toggle** (`:130-142`).
- At the cap, an unselected tag posts `STR_TAG_LIMIT_PER_HIGHLIGHT` and changes nothing.
- Otherwise it flips `selectedIds_` **without** `RenderLock`, although `buildScreen` reads it on the
  render task (`:94`). This is the M2 race the spec fixes.
- It never calls `app.clearTapFlash()`.

**Create** (`:144-181`).
- `startNewTagFlow` clears the flash, then pushes `KeyboardEntryActivity`.
- The handler runs `STUDY.addTagName` and the `tags_` refresh under `RenderLock`.
- It then selects the id only below the cap (`:173`), without a lock and without a message.
- A failure reports empty name, too long, or palette full (`:183-198`).

**Long-press and retire** (`:227-284`).
- A long-press maps the row to a tag and opens `confirmPopup_`, unless `saveDisabled`
  (`STR_HIGHLIGHTS_LOAD_FAILED`).
- The retire itself runs under `RenderLock`, deselects, and posts `STR_TAG_SAVE_FAILED` on failure.

**Buttons** (`:298-320`).
- `handleButtons` overrides the base so that a Confirm held over 700 ms on a tag row retires it.
- On this board a synthesised Confirm reports a 40 ms hold (`lib/Input/Input/NavKeyGestures.h:64-68`),
  so that branch is unreachable here.

**Exits.**
- Back is a cancelled result (`:200-205`).
- Home commits unless the popup is open (`:207-214`).
- `render` is a copy of the base render with the popup interleaved (`:322-341`). It lacks
  `PostedMessage::drawNext`, which the base render has (`TagChipGridActivity.cpp:282`,
  `UiListActivity.cpp:170`).

**Stale class comment.** `TagPickerActivity.h:15-45` still describes `HighlightDoc`, a per-book
palette, `removeTag` rollback and a held-Confirm delete. One sentence is cut off mid-way (`:41`).
The new class comment replaces it.

## What the base gives the picker, and what it does not

The hooks and where they run (`TagChipGridActivity.h:39-55`):

| Hook | Thread | Lock | Notes |
|---|---|---|---|
| `buildChips(out)` | loop | none held | `out` is empty on entry; reserve first |
| `chipState(chip)` | render | inside the render | must not take `RenderLock` |
| `buildHeaderExtras(screen)` | render | inside the render | called after `buildGrid` on every build, chips or none (`.cpp:54-57`) |
| `activateIndex(int)` | loop | none held | take `RenderLock` to write |
| `onTagRetired(id)` | loop | none held | take `RenderLock` to write |
| `onHome()` | loop | none held | |

Facts the picker relies on:

- **Final in the base:** `handleHomeGesture`, `render`, `listCount`, `buildScreen`, `drawFooter`,
  `onBackButton`, `onRowAction`, `onRowLongPress`, `handleCustomInput`, `handleButtons` and
  `navigateButtons` (`TagChipGridActivity.h:33-34`, `:58-71`). The picker's `handleButtons`,
  `render`, `handleCustomInput` and `onRowLongPress` overrides therefore cannot survive. The
  held-Confirm retire goes with them.
- **Still open:** `onEnter`, `headerTitle` and `drawChrome` (`UiListActivity.h:21`, `:65-66`).
- **Helpers:** `chips()`, `retireConfirmationOpen()`, `rebuildChips()` and
  `rebuildChips(ChipKey)` (`TagChipGridActivity.h:76-81`).
- **Back** sets a cancelled result and finishes (`.cpp:136-141`).
- **Home** calls `onHome()` only while the popup is closed (`.cpp:143-148`).
- **Long-press** acts on `Kind::Tag` only. It clears the flash and opens the popup
  (`.cpp:163-172`).
- **Retire:**
  - `retireTag` calls `STUDY.retireTag` with no lock.
  - It calls `onTagRetired(id)` whenever `!isActive(id)`, failed save included.
  - It then calls `rebuildChips()` and posts `STR_TAG_SAVE_FAILED` (`.cpp:195-202`).
- **Disabled stays tappable.** Every chip is drawn with `InputTouch | InputLongPress`
  (`.cpp:102-104`). `TagChipView::draw` never sets `fui::StateDisabled` (`TagChipView.cpp:116-118`).
  `findTouch` would skip such a hit for every input (`FreeInkUICore.h:1169-1170`), so long-press
  retire works on a Disabled chip.
- **The flash style follows the state.** `TagChipView::draw` copies the chip's own look into
  `selected`, `focused`, `active` and `disabled` (`TagChipView.cpp:104-109`). A flash therefore
  repaints a chip in its new state, and `clearTapFlash` on a toggle is the second guard (B1).
- **Rebuilds close routing.** `rebuildChips` swaps the list under `RenderLock`, reselects by
  `(kind, id)` or clamps, calls `closeRouting()`, then `requestUpdate()` (`.cpp:37-52`).
- **Done stays out of the button ring.** `stepSelection` clamps over `listCount()` = `chips_.size()`
  (`.cpp:23`, `:232-241`), so a header-only Done is never focused.
- **Base render order:** `clearScreen`, `drawChrome`, `renderUi`, then the rebuild loop, the popup,
  `drawFooter`, `displayBuffer` and `PostedMessage::drawNext` (`.cpp:266-283`).
  - `drawChrome` and `buildHeaderExtras` both run on the render task, in that order, within one
    `render()`.
  - The grid never calls `screen.list()`, so `consumeRebuildNeeded` stays false and `drawChrome`
    runs once per render.

**Precedent for the subclass shape:** `TagFilterActivity` (`.h` 38 lines, `.cpp` 55 lines). It has
`onEnter` → base `onEnter` then `rebuildChips(filterKey(filter_))` (`.cpp:14-17`). Its `chipState`
reads one member written under `RenderLock` (`.cpp:25-27`, `:44-50`). Its `activateIndex` guards
`retireConfirmationOpen()` and clears the flash before it finishes (`.cpp:29-42`).

## Threads and locks

- **The render task holds `RenderLock` across `render()`.** `renderTaskLoop` takes it, then calls
  `currentActivity->render(std::move(lock))` (`src/activities/ActivityManager.cpp:47-59`). The
  parameter is `RenderLock&&`, so nothing move-constructs it and it is held until the loop
  iteration ends.
- **The keyboard round trip does not re-enter the picker.**
  - Push keeps the parent on the stack. Neither `onExit` nor `onEnter` runs on it
    (`ActivityManager.cpp:150-160`).
  - On pop, the parent's result handler runs on the loop task after `lock.unlock()`, followed by
    `requestUpdate()` (`:115-132`).
  - So the create handler can call `rebuildChips(ChipKey{Tag, id})` without self-deadlock, and a
    later `onEnter` will not reset `nav`.
- **`ReaderUtils::showMessage` only posts.** It is `PostedMessage::post` (`ReaderUtils.h:233`), drawn
  by the base render after `displayBuffer`, so calling it from the loop task is safe.
- **The render task reads, after the change:**
  - `chips_`, `widths_`, `page_`, `nav` and `buttonFocus_` (base).
  - `selectedIds_`, through `chipState` and the title.
  - The title buffer and the Done rect.
- **Only the render task touches the title buffer and Done rect.** `drawChrome` writes them and
  `buildHeaderExtras` reads them, so they need no lock. `selectedIds_` is the only picker state the
  loop task writes and the render task reads.
- **No render-task path reads the palette after the change (A1).**
  - `chipState` reads the chip and `selectedIds_`.
  - Labels are copied into `ChipEntry::label` on the loop task.
  - The old picker needed `RenderLock` around `addTagName` and `retireTag` (`:160-167`, `:264-277`)
    only because `buildScreen` borrowed `tags_[i].name.c_str()` (`:92`). Those locks have no
    reason to stay.

## The header and the Done pill

**`drawChrome` default.**
- It draws `GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight},
  title)` (`UiListActivity.cpp:142-147`).
- `GUI` is `UITheme::getInstance().getTheme()`, a `const BaseTheme&` (`src/components/UITheme.h:22`,
  `:49`).
- `drawHeaderWithAction` is a non-virtual `const` member, so `GUI.drawHeaderWithAction(...)`
  compiles as is.

**Precedent for overriding `drawChrome` and calling `headerTitle()`.**
- `PublicationsActivity.cpp:41-43`, `:48`.
- `MeetingsActivity.h:89-91`, `MeetingsActivity.cpp:60`.

**`headerTitle()` is `const`.**
- It is declared `const` (`UiListActivity.h:65`), and every override in `src` is `const`. Its only
  generic caller is the default `drawChrome` (`UiListActivity.cpp:143`).
- Formatting `Tags n/8` inside it would need a `mutable` buffer. `grep -rn "mutable char" src`
  finds none, so there is no repo precedent.
- The alternative is to format in the picker's own `drawChrome`, which is non-const and runs on the
  render task. The spec must pick one.

**String sources.**
- `tr(STR_TAGS)` is "Tags" (`english.yaml:372`), defined only in `english.yaml`. Other languages
  fall back to it.
- `PassageDoc::MAX_TAGS_PER_PASSAGE = 8` (`lib/StudyStore/StudyStore/PassageDoc.h:45`).
- `tr(STR_DONE)` is "Done" (`english.yaml:137`).

**Action rect per theme**, pinned by host tests:

| Theme | Header | Battery | Done rect | Tests |
|---|---|---|---|---|
| Lyra | 44 px | bar 44 px, right | `y = top + 4`, `h = 36`, left of the battery reserve | `HeaderLayoutTest.cpp:247-259` |
| Classic | 45 px | strip 20 px, right | `y = top + 20`, `h = 25`, bottom-right corner | `HeaderLayoutTest.cpp:261-270` |

Metrics: `LyraTheme.h:11-13`, `:32-33`; `BaseTheme.h:134-136`, `:154-155`. A detached strip as tall
as the band returns `height = 0` (`HeaderLayoutTest.cpp:335-338`). No shipped theme hits that, but
the picker should not draw or register Done for an empty rect.

**Drawing the pill.**
- `TagChipView::draw(screen, rect, tr(STR_DONE), ChipState::Action, ACTION_DONE, 0, fui::InputTouch,
  pad)` draws the Inactive outline with bold text (`TagChipView.cpp:90-93`, `:120`).
- Radius is `rect.height / 2`, and `minTouchSize = 0` (`:89`, `:124`).
- `fui::button` grows the hit rect by `hitPadding` per edge (`components/controls/button.h:36-43`).
- **Hit padding to the full band:** `top = rect.y − band.y` and
  `bottom = (band.y + band.height) − (rect.y + rect.height)`. That is 4 and 4 under Lyra, 20 and 0
  under Classic, so the hit is 44 or 45 px tall.
- **Nearest example of a short pill with a band-tall hit:** `EpubReaderMenuActivity.cpp:450-468`
  ("The pill is drawn at text height; its hit rect fills the band, a touch target tall").
- **The hit sits in the header band, above the content margin.**
  - The margin starts at `safe.y + topPadding + headerHeight` (`TagChipGridActivity.cpp:62-66`).
  - Line-0 chips get no top hit padding (`TagChipRow.h:351`), so Done cannot overlap a chip's hit.
  - `findTouch` scans newest-first (`FreeInkUICore.h:1167`), but nothing overlaps anyway.
- **Header touch is not intercepted.** `MappedInputManager` treats the top edge only as a swipe
  gesture (`src/MappedInputManager.cpp:211-243`); a tap there routes normally.
- **Long-press ignores Done.**
  - `findTouch` with `InputLongPress` skips an `InputTouch`-only hit. `acceptsTouchFallback` is
    false for a long-press (`FreeInkUICore.h:1171-1176`), and a release with `longPress` set asks
    for that kind (`:1246-1249`).
  - So an `InputTouch`-only Done drops a long-press by construction.
- **The app never clears the frame** (`grep -rn setClearColor src lib` is empty;
  `FreeInkApp.h:663-669`). The header `drawChrome` paints stays under the pill the app draws on top.
- **Classic is tight.**
  - The label is the 24 px uiScale small line (`HeaderLayoutTest.cpp:19-20`) in a 25 px pill whose
    content rect is inset 2 px top and bottom (`button.h:56`).
  - Text taller than its rect is drawn from the rect top and not clipped
    (`FreeInkUIGfxRenderer.h:172-180`), so the line box ends about 1 px below the pill.
  - "Done" has no descenders. This is the spec's Classic device check, not a blocker.
- **Registering the action.**
  - `app.on(ACTION_DONE, &trampoline, this)` after `UiListActivity::onEnter()` (which registers
    `ACTION_ROW`), as `HighlightsActivity.cpp:40-43` does.
  - The trampoline is a static `(const fui::ActionEvent&, void*)` (`HighlightsActivity.h:110-111`).
  - `onMoreEvent` shows the guard-then-act shape, including a popup check (`HighlightsActivity.cpp:140-148`).
  - `FreeInkApp<MAX_INTERACTIONS, 6>` allows 6 handlers (`UiAppHost.h:36`, `FreeInkApp.h:555`). The
    picker uses 2.
- **The open popup already swallows input.**
  - While `confirmPopup_` is active, `OptionPopup::handleInput` returns true for every touch and
    button (`src/components/OptionPopup.h:65-131`).
  - The base calls it first in `handleCustomInput` (`TagChipGridActivity.cpp:204-205`), ahead of
    `routeListTouch` (`UiListActivity.cpp:84-87`).
  - So a Done tap cannot route while the popup is open. A `retireConfirmationOpen()` guard in the
    handler is defence in depth, as in `TagFilterActivity::activateIndex`.
- **Interaction budget:** at most 64 chips a page (`TagChipRow.h:244`) plus Done is 65, under
  `MAX_INTERACTIONS = 96` (`UiAppHost.h:35`).

## Activation paths

- **A tap** reaches `activateIndex` through `rowActionTrampoline` → `onRowAction`, which sets
  `nav.selected` under `RenderLock` (`UiListActivity.cpp:33-37`, `TagChipGridActivity.cpp:150-161`).
  That runs inside `app.route()`'s dispatch (`FreeInkApp.h:729-745`).
- **Confirm** reaches `activateIndex(nav.selected)` through `UiListActivity::handleButtons`
  (`UiListActivity.cpp:48-59`), which the base keeps (`TagChipGridActivity.cpp:221`). On this board
  Confirm is a synthesised Right hold (`lib/hal/HalGPIO.cpp:213-214`).
- **`clearTapFlash`** drops the pending flash and suppresses the arm that follows the current
  dispatch (`FreeInkApp.h:755-759`; the arm after dispatch at `:705-715` and `:733-742`). Outside a dispatch it is harmless, because `render`
  and `route` reset `flashSuppressed_` (`:688`, `:734`).
- **A toggle changes no chip.** Labels carry names only, so a toggle needs `RenderLock`,
  `selectedIds_`, then `requestUpdate()`, and no `rebuildChips`. `chipState` and the title pick up
  the change at the next render.

## Palette operations the picker calls

- **`addTagName`** (`StudyStore.cpp:250-264`).
  - It returns nullopt when `saveDisabled`, or when the name is empty, too long or the palette is
    full.
  - It returns the existing id for an active name, with no save.
  - A retired name is revived under its own id and saved (`TagPalette.cpp:43-50`).
  - On a failed save it retires the new id and returns nullopt.
  - The returned id is therefore always active.
- **A revive does not rebuild ranks** (`TagPalette.cpp:46-50`; rebuild only at `:57`). Ranks cover
  retired entries (`TagPalette.h:64-66`), so a revived tag still sorts by name.
- **`retireTag`** (`StudyStore.cpp:266-273`) refuses `UNLABELLED` and `saveDisabled`. It retires in
  memory before saving and never rolls back.
- **`palette().activeIds()`** keeps allocation order (`TagPalette.h:57-58`).
- **`palette().activeCount()`** is a linear count (`TagPalette.cpp:81-87`). Reserving
  `activeIdsByName().size() + 1` gives the same number without a second walk.
- **Labels without a copy.**
  - `palette().name(id)` returns `const std::string&` (`TagPalette.h:54`).
  - `StudyStore::tagName` returns `std::string` by value (`StudyStore.h:57`). That is a heap
    allocation for any name past the 15-byte SSO buffer on 32-bit libstdc++.
  - `ChipEntry::label` is 40 bytes (`TagChipView.h:18-22`), and names are at most 24 bytes
    (`TagPalette.h:39`).

## Removals

- **`TagRowMapping.h`.** Its only users are the picker (`TagPickerActivity.cpp:12`, `:54`) and
  `TagRowsTest.cpp:3` (`grep -rn "TagRowMapping\|TagRows::" src test lib`).
- **`test/tag_rows/TagRowsTest.cpp`.**
  - It is one of three sources of the `TagRowsTest` executable (`test/tag_rows/CMakeLists.txt:1-5`).
    The others are `PassageActionsTest.cpp` and `TagChipRowTest.cpp`, so the target stays.
  - Its 7 tests, `TagRows.*` (`ctest --test-dir build/test -N | grep "TagRows\."`), go with it.
  - The shared `test/CMakeLists.txt:76` (`add_subdirectory(tag_rows)`) is untouched.
- **`STR_TAG_NEW`.**
  - `scripts/gen_i18n.py` collects `\bSTR_[A-Za-z0-9_]+\b` from `.cpp`, `.h` and `.c` files under
    `src` and `lib`, comments included (`scripts/gen_i18n.py:250-284`, default dirs `:831`). It runs
    as a PlatformIO `pre:` step (`platformio.ini:134`).
  - `grep -rnw STR_TAG_NEW --exclude-dir=.pio --exclude-dir=.git .` finds one code use,
    `TagPickerActivity.cpp:100`. The other hits are `english.yaml:373` (its only translation) and
    markdown under `docs/superpowers`, which the generator does not scan.
  - `-w` does not match `STR_TAG_NEW_CHIP`, because `_` is a word character.
  - So it can be removed with the picker.
- **The YAML is a shared file in `ui-dev.md`'s list.** `ui-dev.md` names
  `lib/I18n/translations/*.yaml` as an append point to report rather than edit. The brief instructs
  the removal, btrial-2 edited the same file directly in this batch (`68ce7058`), and no sibling
  task remains to collide with. So the edit is in scope.

## Memory

| Item | Size | Where | Evidence |
|---|---|---|---|
| `ChipEntry` | 44 B (`uint8_t` kind, padding, `uint16_t` id, `char[40]`) | — | `TagChipView.h:18-22` |
| Picker `chips_` at 48 tags | 49 × 44 = 2,156 B | internal SRAM, under the 4,096 B PSRAM threshold | `sdkconfig.defaults:2260-2261` (`CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`) |
| `chips_` at the 200-tag cap | 201 × 44 = 8,844 B | PSRAM | `TagPalette.h:38` |
| `widths_` | 4 B a chip | beside `chips_` | `TagChipGridActivity.cpp:44` |
| Removed `rowItems_[202]` | 202 `fui::ListItem` (three pointers, two asset refs, flags, each) | inside the activity object | `TagPickerActivity.h:57`, `:110`; `components/lists/list.h:8-24` |
| Removed `tags_` | one `TagView` (id + `std::string`) a tag, re-copied every render | heap | `TagPickerActivity.cpp:79` |
| Title buffer, Done rect | tens of bytes | inside the activity object | — |

`CLAUDE.md` cites `sdkconfig.x4pro:2146-2147` for the threshold. That file is generated and
gitignored (`.gitignore:33`, `/sdkconfig.*`). The tracked source is `sdkconfig.defaults`.

## Testability

- **No host target compiles an `Activity`, `UiAppHost` or FreeInkUI.**
  `grep -rn -e 'Activity\.cpp' -e UiAppHost -e FreeInkUI test --include=CMakeLists.txt` exits 1. So
  `TagPickerActivity` itself is verified by `pio run` and on the device.
- **Already covered:** `pickerState` at 7, at 8, after a deselect at 8, over the cap, and for
  `NewTag` (`TagChipRowTest.cpp:607-639`), and `findChip`, `reselect` and `filterKey` (`:641` on).
- **Host-testable only if extracted.** These picker decisions are pure. Under the TDD rule they
  belong in a FreeInkUI-free header the host suite already compiles, as `pickerState` and `reselect`
  are in `TagChipRow.h`:
  - Keep only active ids at open (D8). It needs an `isActive` predicate; `TagRowsTest` does not link
    `TagPalette.cpp`, per `test/tag_rows/CMakeLists.txt:1-16`.
  - Commit in allocation order: filter `activeIds` by the selection.
  - The create outcome: apply; already applied, so silent; or at the cap, so post the limit
    message.
  - Done's hit padding from the band and the returned rect.
  - Which of these to extract is a spec decision.
- **Baseline host suite**, before any change:
  - `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test -j 8`
    succeeds.
  - `ctest --test-dir build/test -j 8` reports "100% tests passed out of 1745".
  - `build/` is ignored (`.gitignore:13`), and `git status --short` stays empty.
  - Deleting `TagRowsTest.cpp` takes the count to 1738 plus whatever this task adds.
- **Baseline firmware build:** see "Tools and versions".

## Tools and versions

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version`. `pio` is not on `PATH` in this shell |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| CMake (host) | 4.4.2 | `cmake --version`. `ninja` is not installed, so the default generator is used |
| Host compiler | Apple clang 21.0.0 (clang-2100.0.123.102) | `c++ --version` |
| googletest / ArduinoJson (host) | v1.17.0 / v7.4.2 | `test/CMakeLists.txt:14-18`, `:28-33` |
| C++ | GNU++20 on device (`platformio.ini:40`); C++20 with `-Wall -Wextra -pedantic` on host | `test/CMakeLists.txt:4-6`, `:42-46` |
| clang-format | `.venv/bin/clang-format`, chosen by the wrapper; no `clang-format-21` or `clang-format` on `PATH` (`command -v` prints nothing) | `bin/clang-format-fix:3-16`. Not probed directly, per `CLAUDE.md` |
| Python | 3.14.7 | `python3 --version` |

**Baseline firmware build**, before any change:
- `~/.platformio/penv/bin/pio run -e x4pro` gives `[SUCCESS] Took 196.92 seconds`, a cold build.
- The log has two size reports, and the last one is the firmware: RAM 66,228 / 327,680 B and Flash
  5,563,766 / 6,553,600 B (84.9%).
- It has 248 `warning:` lines, all from third-party code: `managed_components/espressif__esp-sr` (204),
  `framework-espidf` `usb/hub.c` and `usb/ext_port.c` (30), `espressif__rmaker_common` (12), and
  `.pio/libdeps/x4pro/WebSockets` (2).
- None is under `src/` or our own `lib/`, so any warning there after the change is new.

## Nearest existing examples

| Need | Model |
|---|---|
| Subclass of the grid: `buildChips`, `chipState`, `onTagRetired` under `RenderLock`, `onHome`, `activateIndex` guard and flash clear | `TagFilterActivity.{h,cpp}` |
| A second action beside `ACTION_ROW`: static trampoline, registered in `onEnter`, popup guard | `HighlightsActivity.cpp:40-43`, `:136-148`; `HighlightsActivity.h:103-111` |
| `drawChrome` override that calls `headerTitle()` | `PublicationsActivity.cpp:39-49`; `MeetingsActivity.cpp:54-60` |
| A pill shorter than its band with a band-tall hit | `EpubReaderMenuActivity.cpp:450-468` |
| Keyboard create, dedupe and failure messages | today's `TagPickerActivity.cpp:144-198`, kept |
| Pure picker helper with host tests | `TagChips::pickerState` / `reselect` in `TagChipRow.h`, tests in `test/tag_rows/TagChipRowTest.cpp` |

## Scope and tier

- **The change stays on surface `ui`:**
  - `src/activities/reader/TagPickerActivity.{h,cpp}`
  - removal of `TagRowMapping.h`
  - `test/tag_rows/` (`TagRowsTest.cpp`, and its line in that directory's `CMakeLists.txt`)
  - one line out of `english.yaml`
  - optionally, pure helpers and tests in `TagChipRow.h` / `TagChipRowTest.cpp`
- **What it does not touch:** `TagSelectionResult`, either caller, the passage or palette file
  formats, a store version, or `src/main.cpp`.
- **The commit-order drift is not a reason to raise the tier.** Restoring allocation order is
  already this task's acceptance; the fix is in the picker.
- The tier stays `heavy`, and nothing found warrants raising it.

## Questions for the spec

1. **Where to format `Tags n/8`.** Use a `mutable` buffer inside `const headerTitle()` (no precedent
   in `src`), or format in the picker's `drawChrome` override and pass it to
   `drawHeaderWithAction`. Both run on the render task.
2. **Which picker decisions move into `TagChipRow.h`** so they get a failing host test first: the D8
   open filter, the allocation-order commit, the create outcome, and Done's hit padding.
3. **An empty Done rect** (`height == 0`, `HeaderLayoutTest.cpp:335-338`): skip drawing and
   registering Done, leaving Home as the commit.
4. **A tap on a Disabled chip:** should it also clear the flash? The flash would repaint the chip
   in its own Disabled look (`TagChipView.cpp:104-109`), so either choice looks the same. The brief
   requires the clear only for toggles.
5. **`StudyStore::activeTags()`** has no caller after this change. File a discovery for its removal,
   rather than edit `src/study` here.
