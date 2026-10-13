# Design: `TagChipGridActivity`, with `TagFilterActivity` as its first subclass (btrial-4)

Surface `ui`. Branch `feature/tag-chip-grid-base`, on `batch/20261012-tag-chip-picker-2l3m` at
`783a1ff4`. Governing spec: `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (the
batch spec): Goal, D6, Modelled on, §1 `buildEntries`, §3, §4, §5, A1, A6. Research:
`docs/superpowers/research/2026-10-12-btrial-4-research.md` (the research note). Where this design
and the batch spec disagree, the batch spec wins; every place this design narrows or fills in the
batch spec is an assumption below.

Line numbers below are current on this branch. The batch spec's `TagFilterActivity.cpp` citations
are mostly two lines early (research note, "The spec's line numbers have drifted").

## Problem

1. **The chip grid lives in one screen.** Paging, the focus ring, swipe and button page turns,
   long-press retire with its popup, and the render that interleaves the popup are all private to
   `TagFilterActivity` (`TagFilterActivity.cpp:57-280`). The picker (batch §4, task `t5`) needs the
   same grid with different chips, states and exits, and today could only copy it.
2. **Tags are listed in allocation order** on the filter grid and the Highlights chip row, because
   `TagChipView::buildEntries` reads `STUDY.palette().activeIds()` (`TagChipView.cpp:16`).
3. **Two defects on the filter grid** (batch §5):
   - Home cancels the screen from under an open retire popup. `ActivityManager::loop` hands Home to
     `handleHomeGesture()` before the activity's own `loop()` (`ActivityManager.cpp:75-80`), and the
     filter's handler cancels unconditionally (`TagFilterActivity.cpp:143-148`).
   - A failed retire save says "Could not save highlight" (`TagFilterActivity.cpp:200`,
     `english.yaml:370`) instead of "Could not save tag" (`english.yaml:380`).
4. **A tap after a rebuild can land on the wrong chip.** `rebuildChips` swaps the list but never
   closes the routing gate (`TagFilterActivity.cpp:40-48`). Routing keeps using the last published
   interaction table until the next render publishes a new one (`UiAppHost.h:10-17`). Today only a
   retire rebuilds, from the popup's callback (`:181-185`). The popup's last render also published
   the grid's table (`renderUi` runs before `processRender`, `:268`, `:275`). On the next loop pass
   the popup is inactive, so `handleCustomInput` lets touch through (`:204-214`), and a tap routes
   against chip indices of the old list. `rowActionTrampoline` checks the index only against the new
   count (`UiListActivity.cpp:35`), so the tap activates whichever chip now holds that index — the
   retired chip's neighbour. The window lasts until the requested render publishes. The picker will
   also rebuild on create, re-sorting, which is why batch §3 closes the gate (review 0, m5).

## Goal

- A new `TagChipGridActivity` owns everything batch §3 "The base owns" lists, exposes the six hooks
  of the batch §3 table, and documents their lock contract in its header.
- `TagFilterActivity` becomes a thin subclass. Its counts, chapter scope, hidden zero-count chips
  and result contract are unchanged.
- The filter grid and the Highlights chip row list tags alphabetically, with All first and
  Unlabelled last.
- After a rebuild the selection follows the chip's `(kind, id)`; a retired selection clamps; a
  subclass can request a specific chip.
- Home under the retire popup does nothing; a failed retire save says "Could not save tag".

## Non-goals

- `TagPickerActivity`, `TagRowMapping.h`, `test/tag_rows/TagRowsTest.cpp`, `STR_TAG_NEW` and the
  header Done pill: task `t5` (`feature/tag-picker-grid`), which subclasses this base next.
- `BaseTheme::drawHeaderWithAction`: btrial-3, already on the batch (`src/components/HeaderLayout.h`).
- The Highlights chip row's layout, two-line limit, `…` chip, or its call to `TagChipView::draw`
  (`HighlightsActivity.cpp:707-710`). It changes only through `buildEntries`' order.
- `TagChipView::isSelected`, `draw`, `measure`, `metricsFor`: unchanged.
- No string, store, cache, or on-disk format changes. No new host-test target.
- The picker's missing `PostedMessage::drawNext` (`TagPickerActivity.cpp:322-341`): it goes away
  when `t5` moves the picker onto this base's `render`.

## Modelled on

- **`UiTabListActivity`** (`src/activities/UiTabListActivity.{h,cpp}`) for the shape: a non-final
  `UiListActivity` subclass that is itself a base, with one-line contracts on pure-virtual hooks
  (`UiTabListActivity.h:30-42`) and overrides of `onRowAction` and `navigateButtons` for the whole
  family (`:48-51`, `.cpp:44-47`, `:64-72`).
- **`TagFilterActivity.cpp:40-280`** for the body, moved nearly verbatim. Each change to the moved
  code is listed in "Changes to the moved code".
- **`ReaderActivity.h:61`** (`bool handleForcedRefresh() final;`) for marking a member `final` in a
  class that is itself a base.
- **`TagChips::findChip`** (`TagChipRow.h:126-136`) and its `FakeChip` tests
  (`TagChipRowTest.cpp:639-678`) for the new pure helpers and their tests.

## Architecture

### Files

| File | Change |
|---|---|
| `src/activities/reader/TagChipGridActivity.h` / `.cpp` | New. The base. |
| `src/activities/reader/TagFilterActivity.h` / `.cpp` | Shrinks to the subclass. |
| `src/activities/reader/TagChipRow.h` | Adds `ChipKey`, `filterKey`, `reselect`. |
| `src/activities/reader/TagChipView.cpp` | `buildEntries` reads `activeIdsByName()`. |
| `src/activities/reader/PassageLinksActivity.h` | Its comment at `:24` points at the moved "Both exits MUST set a result" note. |
| `test/tag_rows/TagChipRowTest.cpp` | Tests for the three helpers. |

PlatformIO compiles every file under `src/` (no `build_src_filter` in `platformio.ini`), and
`TagChipRowTest.cpp` is already in `TagRowsTest` (`test/tag_rows/CMakeLists.txt:1-5`). No shared
append point (`test/CMakeLists.txt`, `lib/I18n/translations/*.yaml`, `src/main.cpp`) changes.

### `TagChipRow.h`: three pure helpers

Placed after `findChip` (`TagChipRow.h:126-136`), FreeInkUI-free like the rest of the header.

```cpp
// A chip's identity across rebuilds: All, Unlabelled and NewTag by kind alone, a Tag by its id.
struct ChipKey {
  Kind kind = Kind::All;
  study::TagId id = study::UNLABELLED;
};

// The chip that shows `filter`: All for none, Unlabelled for UNLABELLED, otherwise that tag.
constexpr ChipKey filterKey(const std::optional<study::TagId> filter) {
  if (!filter) return ChipKey{Kind::All, study::UNLABELLED};
  if (*filter == study::UNLABELLED) return ChipKey{Kind::Unlabelled, study::UNLABELLED};
  return ChipKey{Kind::Tag, *filter};
}

// The selection after a chip list is rebuilt: the chip `key` names when the new list holds it,
// otherwise `previous` clamped into the list. 0 for an empty list.
template <typename Chips>
int reselect(const Chips& chips, const std::optional<ChipKey> key, const int previous) {
  if (key) {
    const int found = findChip(chips, key->kind, key->id);
    if (found >= 0) return found;
  }
  const int last = static_cast<int>(chips.size()) - 1;
  return std::clamp(previous, 0, std::max(last, 0));
}
```

The clamp is today's (`TagFilterActivity.cpp:46-47`). `<optional>` and `<algorithm>` are already
included (`TagChipRow.h:5`, `:8`).

### `TagChipGridActivity.h`

```cpp
// A full-screen, paged grid of tag chips, shared by TagFilterActivity and the tag picker so the two
// cannot drift in paging, focus or retire behaviour. Chips wrap into lines and page at line
// boundaries; the x/y strip shows only when there is more than one page. A swipe or a held button
// turns a page, a released button steps one chip, a tap or Confirm activates one.
//
// A long-press RETIRES a tag: it leaves the pickers and stops filtering, while every passage that
// carried it keeps its remaining tags and stays on the card. A destructive delete here would be
// device-wide now that the palette is global -- one tidy-up gesture would wipe work across every
// publication.
//
// Threads. The loop task builds the chip list; the render task draws it. Whatever the render task
// reads, the loop task writes only under RenderLock. RenderLock is a non-recursive mutex the render
// task holds for the whole render(), so a render-task hook must never take it, and every loop-task
// hook is called with it free.
//
// Chips dispatch only through ACTION_ROW, so a subclass's own actions start at ACTION_USER.
class TagChipGridActivity : public UiListActivity {
 public:
  bool handleHomeGesture() final;
  void render(RenderLock&&) final;

 protected:
  TagChipGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // --- subclass contract -----------------------------------------------------
  // Loop task, no lock held. Fills `out` (empty on entry; reserve first) from loop-owned state and
  // STUDY only.
  virtual void buildChips(std::vector<TagChipView::ChipEntry>& out) = 0;
  // Render task, inside the render: must not take RenderLock, and reads only state the loop task
  // writes under it.
  virtual TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const = 0;
  // Render task, after the grid on every build, chips or none. Same rules as chipState.
  virtual void buildHeaderExtras(UiScreen& screen) {}
  // activateIndex(int), from UiListActivity: loop task, no lock held; take RenderLock to write
  // anything the render task reads.
  // Loop task, no lock held, after a retire attempt left `id` inactive, failed save included.
  virtual void onTagRetired(study::TagId id) = 0;
  // Loop task, no lock held: Home while no retire confirmation is open.
  virtual void onHome() = 0;

  // --- base behaviour ------------------------------------------------------------
  int listCount() const final;
  void buildScreen(UiScreen& screen) final;
  void drawFooter() final;
  // Both exits MUST set a result. UiListActivity::onBackButton is a bare finish(), which leaves
  // ActivityResult default-constructed: isCancelled false and the variant holding monostate. A
  // consumer that then reads its own alternative calls std::get on the wrong one, and with
  // -fno-exceptions that aborts.
  void onBackButton() final;
  void onRowAction(const freeink::ui::ActionEvent& event) final;
  void onRowLongPress(int index) final;
  bool handleCustomInput() final;
  // UiListActivity's own: Back -> onBackButton, Confirm -> activateIndex(nav.selected).
  bool handleButtons() final;
  void navigateButtons() final;

  // --- helpers ---------------------------------------------------------------------
  // Loop task, no lock held. Rebuilds from buildChips, keeps the selected chip by (kind, id) or
  // clamps its index when it is gone, closes touch routing until the next render, and requests it.
  void rebuildChips();
  // As rebuildChips(), selecting `select` instead (clamping as above when it is not listed).
  void rebuildChips(TagChips::ChipKey select);
  // Loop task. The loop task is the only writer of the list, so reading it needs no lock.
  const std::vector<TagChipView::ChipEntry>& chips() const { return chips_; }
  bool retireConfirmationOpen() const { return confirmPopup_.isActive(); }

 private:
  void rebuildChipsSelecting(std::optional<TagChips::ChipKey> select);
  void stepSelection(int direction);
  void turnPage(int direction, bool fromButton);
  void showRetireConfirmation(study::TagId id);
  void retireTag(study::TagId id);

  // chips_ and widths_ are replaced together under RenderLock; widths_ is filled by the build.
  std::vector<TagChipView::ChipEntry> chips_;
  std::vector<int> widths_;
  TagChips::GridPage page_;
  // Page geometry the build writes and the loop task's page turns read, both under RenderLock.
  int gridLineWidth_ = 0;
  int gridGap_ = 0;
  int gridLinesPerPage_ = 1;
  int pageNumber_ = 0;
  int pageCount_ = 0;
  // The focused chip is outlined only after a button step, as HighlightsActivity's chip band is.
  bool buttonFocus_ = false;

  bool confirmingDelete_ = false;
  OptionPopup confirmPopup_;
  // Captured when the confirmation opens, so the callback retires the chip that was long-pressed
  // even if a rebuild moved the chips in between.
  study::TagId pendingRetireId_ = study::UNLABELLED;
};
```

The member comments are the filter's own (`TagFilterActivity.h:36-40`, `:66-83`), moved. The
constructor passes `/*wantsTouchLongPress=*/true` to `UiListActivity` (`TagFilterActivity.cpp:22`).
The base declares no `onEnter`: `UiListActivity::onEnter` already registers `ACTION_ROW` and the
screen (`UiListActivity.cpp:20-27`), and the base's state is fresh on construction.

### The hook table

| Hook | Called from | Task | Lock when called | Filter grid |
|---|---|---|---|---|
| `buildChips(out)` | `rebuildChipsSelecting` | loop | none | `buildEntries(scope_, filter_, SIZE_MAX, out)` |
| `chipState(chip)` | `buildScreen` | render | held by the render task | `isSelected(chip, filter_)` → Active, else Inactive |
| `buildHeaderExtras(screen)` | `buildScreen`, last | render | held by the render task | none (default) |
| `activateIndex(i)` | `onRowAction` (tap), `UiListActivity::handleButtons` (Confirm, `UiListActivity.cpp:53-56`) | loop | none | return the chip's filter and finish |
| `onTagRetired(id)` | `retireTag` | loop | none | `filter_ == id` → reset under `RenderLock` |
| `onHome()` | `handleHomeGesture` | loop | none | `onBackButton()` (cancel) |

### `TagFilterActivity` after the change

```cpp
// The highlights chip row's ellipsis: every chip the row would offer, uncapped, as a wrapping grid
// with counts. Returns a TagSelectionResult holding no elements for "All" or exactly one id --
// study::UNLABELLED for "Unlabelled"; a cancelled result means the caller keeps its current filter.
//
// Chips report a study::TagId, not an index. Ids are allocated once and never reused, so a
// retirement cannot renumber anything and the caller's captured filter stays valid.
class TagFilterActivity final : public TagChipGridActivity {
 public:
  // scope: as today (TagFilterActivity.h:28-29).
  TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::optional<std::vector<size_t>> scope,
                    std::optional<study::TagId> filter);

  void onEnter() override;

 private:
  const char* headerTitle() const override;
  void buildChips(std::vector<TagChipView::ChipEntry>& out) override;
  TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const override;
  void activateIndex(int index) override;
  void onTagRetired(study::TagId id) override;
  void onHome() override;

  const std::optional<std::vector<size_t>> scope_;
  // Written under RenderLock: chipState reads it on the render task.
  std::optional<study::TagId> filter_;
};
```

- `onEnter`: `TagChipGridActivity::onEnter()` (resolves to `UiListActivity::onEnter`), then
  `rebuildChips(TagChips::filterKey(filter_))`. That replaces `rebuildChips()` plus
  `selectedChipIndex()` (`TagFilterActivity.cpp:26-34`, `:50-55`).
- `activateIndex`: today's body (`:282-295`), reading `chips()` and guarding with
  `retireConfirmationOpen()`.
- `onHome`: `onBackButton()`, with today's comment (`:144-145`).
- `headerTitle`: `tr(STR_FILTER_BY_TAG)` (`:38`).

## Data and control flow

### Entry

1. `UiListActivity::onEnter`: `nav.reset()`, `resetUi()` closes the gate, `ACTION_ROW` and the
   screen are registered, an update is requested (`UiListActivity.cpp:20-27`).
2. The subclass calls `rebuildChips(key)`. With the list still empty, `reselect` finds `key` in the
   new list or falls back to index 0 (A2).
3. The deferred update (`ActivityManager.cpp:285-295`, consumed at `:167`) renders; `renderUi`
   opens the gate (`UiAppHost.cpp:17-27`).

### `rebuildChipsSelecting(select)` (loop task, no lock on entry)

1. `std::vector<ChipEntry> next; buildChips(next);` with no lock held.
2. `reselect(next, select, nav.selected)` with no lock held. Neither `next` (a local) nor
   `nav.selected` (A3) needs it.
3. Under one `RenderLock`: `chips_ = std::move(next)`, `widths_.assign(size, 0)`,
   `nav.selected = selected`, `closeRouting()`.
4. After the lock: `requestUpdate()` (A5).

`rebuildChips()` passes the key of `chips_[nav.selected]` when `nav.selected` is in range, else
`std::nullopt`. `rebuildChips(key)` passes `key`.

### `buildScreen` (render task, lock held)

Today's body (`TagFilterActivity.cpp:57-120`) with two changes:

- The chip state is `chipState(entry)` (replacing `:100-101`).
- The grid is drawn by the body up to the focus ring. Its early return for an empty or mismatched
  list (`:68`) no longer skips anything else: `buildHeaderExtras(screen)` runs after the grid on
  every build (A9).

### Input (loop task)

`UiListActivity::loop` order is unchanged (`UiListActivity.cpp:84-107`). What each stage does here:

1. **Home** reaches `handleHomeGesture` before `loop()` (`ActivityManager.cpp:75-80`). The base
   returns true. While the popup is open it does nothing more; otherwise it calls `onHome()` (A7).
2. **`handleCustomInput`** (final): the popup first (`TagFilterActivity.cpp:204-211`); then swipe
   Up/Down turns a page and is consumed (`:213-217`). While the popup is open, `handleInput`
   returns true on every pass (`OptionPopup.h:65-133`), so stages 3-5 do not run (A13).
3. **`handleButtons`** (final): forwards to `UiListActivity::handleButtons`
   (`UiListActivity.cpp:48-59`). Back → `onBackButton` (cancelled result, `:136-141`); Confirm →
   `activateIndex(nav.selected)`.
4. **Touch**: `routeTouch` (gated) → `rowActionTrampoline` (bounds-checked,
   `UiListActivity.cpp:33-37`) → the base's `onRowAction`:
   - under `RenderLock`: `buttonFocus_ = false; nav.selected = event.value;` (A4)
   - long-press → `onRowLongPress`, else `activateIndex`.
5. **`navigateButtons`** (final): release steps one chip; a held button turns a page (`:220-261`),
   which this board cannot produce (A18).

### Retire (loop task)

1. `onRowLongPress(i)`: ignored while the popup is open, out of range, or on a chip of kind other
   than `Tag`; `app.clearTapFlash()`; `showRetireConfirmation(chips_[i].id)` (`:158-166`).
2. `showRetireConfirmation`: with `STUDY.saveDisabled()`, posts `STR_HIGHLIGHTS_LOAD_FAILED` and
   returns (`:170-176`). Otherwise captures `pendingRetireId_` and shows the popup.
3. The popup's callback runs inside `handleInput`, on the loop task with no lock held
   (`OptionPopup.h:82`, `:123`). Delete calls `retireTag(pendingRetireId_)`.
4. `retireTag(id)`:
   - `const bool saved = STUDY.retireTag(id);` with no lock (batch A1).
   - `if (!STUDY.palette().isActive(id)) onTagRetired(id);` (A8).
   - `rebuildChips();` The retired chip is gone, so the selection clamps to its old index.
   - `if (!saved) ReaderUtils::showMessage(renderer, tr(STR_TAG_SAVE_FAILED));` The render that
     `rebuildChips` requested draws it through `PostedMessage::drawNext`.

### Render (render task)

Today's override (`TagFilterActivity.cpp:263-280`), moved unchanged: chrome, `renderUi`, the
bounded rebuild loop, then the popup or the footer, `displayBuffer()`, `PostedMessage::drawNext`.
`drawChrome()` stays a virtual call, so the picker's `drawChrome` override (batch §4) runs before
`buildScreen` in the same render.

## Changes to the moved code

| Where today | Change | Why |
|---|---|---|
| `rebuildChips`, `:40-48` | Reselect by `(kind, id)`; `closeRouting()` under the lock; request an update | batch §3; A2, A5, A6 |
| `selectedChipIndex`, `:50-55` | Removed; `filterKey` replaces it | A10 |
| `buildScreen`, `:100-101` | State from `chipState` | batch §3 hook |
| `buildScreen`, end | `buildHeaderExtras(screen)` on every build | batch §3 hook; A9 |
| `handleHomeGesture`, `:143-148` | Popup guard, then `onHome()` | batch §3, §5 |
| `onRowAction`, `:150-156` | Full override; `nav.selected` written under the lock | A4 |
| `retireTag`, `:189-201` | `onTagRetired` on `!isActive`; `STR_TAG_SAVE_FAILED` | batch §3, §5; A8 |
| `activateIndex`, `:282-295` | Stays in the filter; reads `chips()`, `retireConfirmationOpen()` | hook |
| `TagChipView.cpp:16` | `activeIdsByName()` | batch §1 |

## Error handling

No new failure path.

| Situation | Behaviour | Evidence |
|---|---|---|
| `saveDisabled` at long-press | `STR_HIGHLIGHTS_LOAD_FAILED`, no popup, nothing changes | `TagFilterActivity.cpp:170-176`, unchanged |
| Retire's palette or passage save fails | The tag stays retired in memory (`StudyStore.cpp:269-274`, no rollback); `onTagRetired` fires; the chip goes; "Could not save tag" | A8 |
| `retireTag` refused before retiring (`saveDisabled_`, or `UNLABELLED`, `StudyStore.cpp:267`) | Tag still active: no `onTagRetired`; the grid rebuilds unchanged; "Could not save tag". Unreachable in practice: the popup checks `saveDisabled` and only `Kind::Tag` chips, which never carry `UNLABELLED` (`TagPalette.h:29-31`), reach it | A8 |
| OOM in `buildChips`, `widths_.assign` or `activeIdsByName` | As today: those allocate through `std::vector`, which aborts under `-fno-exceptions`. The sizes are unchanged: one chip per active tag plus two | `TagFilterActivity.cpp:41-45`, `TagPalette.cpp:98-105` |

## Memory and cost

- **No new heap allocation.** `chips_`, `widths_` and the per-rebuild `next` vector are today's
  (`TagFilterActivity.cpp:41-45`). `activeIdsByName` allocates one `TagId` vector, as `activeIds`
  did (`TagPalette.cpp:89-96`, `:98-105`). `ChipKey` is 4 B, passed by value.
- **Object size.** The members move from the filter to the base unchanged; `TagFilterActivity`
  loses none of its own state except `selectedChipIndex`'s code. Adding a base class adds one vtable
  in flash and no per-object bytes beyond the existing vptr.
- **Stack.** `buildScreen` keeps today's locals on the 8,192 B render stack
  (`ActivityManager.cpp:32-33`). `rebuildChipsSelecting` adds a `std::optional<ChipKey>` (6 B) to
  today's `next` vector on the loop stack. `GridPage` stays a member (`TagChipRow.h:226`).
- **Interaction budget.** At most `GRID_MAX_CHIPS = 64` chips per page (`TagChipRow.h:219`,
  enforced at `:259`), nothing else on the filter screen registers one (research note,
  "Interaction budget"), and the picker's Done makes 65, under `MAX_INTERACTIONS = 96`
  (`UiAppHost.h:35`).
- **Flash.** Measured at implement against the baseline below; expected small, since the code moves
  rather than grows.

## Assumptions

- **A1. The base has no `onEnter`; a subclass builds its chips in its own `onEnter`.** The
  subclass calls `TagChipGridActivity::onEnter()` and then `rebuildChips(...)`. This is how both
  list screens that build data at entry do it (`TagFilterActivity.cpp:26-34`,
  `HighlightsActivity.cpp:40-48`). A base `onEnter` that built the list would build it twice for
  the filter, which wants a specific entry selection.
- **A2. A selection is a `ChipKey {kind, id}`, matched by `findChip`, else today's clamp.**
  - `rebuildChips()` keeps the selected chip; `rebuildChips(key)` asks for one.
  - A bare `TagId` cannot ask for the filter's All chip, which carries no tag id (research note,
    "Requesting a selection"). The picker's create asks with `{Kind::Tag, id}` (batch §4).
  - Not found → `previous` clamped into the new list. For a retire that is the chip that slid into
    the retired chip's slot, or the last chip, as today (`TagFilterActivity.cpp:46-47`).
  - With no previous chip (entry), the fallback is index 0, today's default for a filter with no
    chip (`:54`).
- **A3. `rebuildChips` reads `chips_` and `nav.selected` without the lock.** The loop task is their
  only writer: `chips_` is written only in `rebuildChipsSelecting` under the lock, and
  `nav.selected` only by loop-task code (the research note's table; `buildScreen` writes `nav.top`
  at `:89`, never `selected`). A reader on the writing task cannot see a torn value.
- **A4. The base's `onRowAction` writes `nav.selected` under `RenderLock`.** The inherited version
  writes it unlocked (`UiListActivity.cpp:40`) while the render task reads it (`:87`, `:105`). The
  base overrides `onRowAction` anyway, to clear `buttonFocus_` (`TagFilterActivity.cpp:150-156`),
  so it takes the one lock for both writes, as `UiTabListActivity` overrides the same method
  wholesale (`UiTabListActivity.cpp:44-47`). It is reached only through `app.route` on the loop
  task with no lock held (`UiAppHost.cpp:29-40`), so taking the lock cannot self-deadlock.
- **A5. `rebuildChips` requests the update itself.** The rebuild closes the gate, and only a render
  reopens it (`UiAppHost.cpp:26`). A caller that forgot to request one would leave the grid deaf
  to touch. Requests coalesce into one flag (`ActivityManager.cpp:290-294`), so the callers'
  existing extra requests (`TagFilterActivity.cpp:33`, `:184`) cost nothing.
- **A6. `closeRouting()` is called under `RenderLock`, on every rebuild.** The render task holds
  the lock across `renderUi` (`ActivityManager.cpp:52`, `:59`), so a render built from the old list
  either finishes before the close or starts after the swap. It cannot reopen the gate onto a
  stale table. Until the next render, taps are dropped (`UiAppHost.cpp:32`); that render is
  requested in the same call (A5). This is batch A6. On entry the gate is already closed by
  `resetUi` (`UiAppHost.cpp:12-15`), and closing it again is harmless. The gate reopens when
  `renderUi` publishes, before `displayBuffer()` (`TagFilterActivity.cpp:268`, `:278`). A tap during
  the panel refresh therefore routes against the new table while the panel still shows the old
  layout. Every `UiAppHost` screen has that window (`UiListActivity.cpp:157`, `:169`); this change
  neither opens nor narrows it.
- **A7. Home with the retire popup open is consumed and does nothing.** The popup stays open, and
  its Cancel, Delete, Back and outside-tap paths are unchanged (`OptionPopup.h:65-133`). This is
  the picker's guard today (`TagPickerActivity.cpp:207-214`), moved to the base for both screens
  (batch §3).
- **A8. `onTagRetired(id)` fires whenever `id` is inactive after the attempt, before the rebuild.**
  - It does not depend on `saved`: `retireTag` retires in memory before saving and does not roll
    back (`StudyStore.cpp:269-274`).
  - Before the rebuild, so `buildChips` and `chipState` see the subclass's updated selection.
    Today's order is the same (`TagFilterActivity.cpp:191-197`).
  - The filter resets `filter_` only when it equals `id`. Today's check is broader: any filter
    naming an inactive tag (`:195`). They agree, because the filter grid only ever opens with no
    filter, `UNLABELLED` or an active id. `HighlightsActivity` assigns its filter only from a chip
    of an active tag (`HighlightsActivity.cpp:166`, `:188`) and drops a retired filter after both
    screens that can retire (`:194`, `:497`). Only those two screens retire
    (`grep -rn "STUDY.retireTag" src`: `TagFilterActivity.cpp:190`, `TagPickerActivity.cpp:271`).
- **A9. `buildHeaderExtras` runs on every build, chips or none.** The picker's Done must not vanish
  if its list is ever empty; the filter draws nothing there.
- **A10. `filterKey(filter_)` selects the chip `isSelected` inverts.**
  `TagChipView::isSelected` (`TagChipView.cpp:48-60`) and `filterKey` agree case by case: no filter
  → All; `UNLABELLED` → Unlabelled; a tag → that tag. `findChip` matches All and Unlabelled by kind
  alone (`TagChipRow.h:126-136`). `candidates` always emits All (`:115`) and keeps a zero-count
  chip while it is the filter (`:118`, `:123`), so the filter's chip is listed whenever its tag is
  active.
- **A11. `ACTION_USER` is left free, with no named constant.** The base registers nothing past
  `ACTION_ROW`. The header says so. `UiTabListActivity` publishes `ACTION_TAB_USER`
  (`UiTabListActivity.h:26`), but nothing uses it (`grep -rn ACTION_TAB_USER src`). Batch §4 names
  the picker's action `ACTION_DONE = ACTION_USER`.
- **A12. The base's input and render members are `final`.** These are `listCount`, `buildScreen`,
  `drawFooter`, `onBackButton`, `onRowAction`, `onRowLongPress`, `handleCustomInput`,
  `handleButtons`, `navigateButtons`, `handleHomeGesture` and `render`. Each carries a base
  invariant: the popup sees input first, Home is guarded, Back returns a cancelled result, and only
  `Tag` chips retire. `handleButtons` is the only route from Back and Confirm to `onBackButton` and
  `activateIndex` (`UiListActivity.cpp:48-59`), so leaving it open would let a subclass bypass the
  other two. The picker's override today (`TagPickerActivity.cpp:298-320`) is the held-Confirm
  retire path that batch §4 removes.
  The six hooks, `headerTitle`, `drawChrome` and `onEnter` remain open. Those are what batch §3-§4
  need from the picker: `drawChrome` for the Done rect, `onEnter` for `app.on(ACTION_DONE, ...)`.
- **A13. Subclasses read the list through `chips()` and the popup through
  `retireConfirmationOpen()`.** `chips_` stays private so only the base writes it, under the lock.
  The filter keeps its defensive popup guard in `activateIndex` (`TagFilterActivity.cpp:283`). The
  picker's Done needs the same guard (batch §4, "Done does nothing while the retire popup is open").
  With the input order above ("Input", stage 2), neither guard is reachable while the popup is
  open. Both are kept in case a future input path bypasses that order.
- **A14. The render keeps the filter's shape.** It ignores `halfRefreshPending` and calls
  `PostedMessage::drawNext` after `displayBuffer()` (`TagFilterActivity.cpp:263-280`). The filter
  never sets `halfRefreshPending`. The picker's current render has the same `displayBuffer()` call
  (`TagPickerActivity.cpp:340`).
- **A15. `buildEntries` order is the only Highlights change.** `Counts::perTag` is aligned with the
  ids passed (`TagChipRow.h:24`, `:30-40`), and `buildEntries` passes one vector to both `count` and
  `candidates` (`TagChipView.cpp:16-20`). Name order therefore keeps counts aligned, and All and
  Unlabelled stay pinned (`TagChipRow.h:115`, `:123`). The existing test
  `NameOrderedIdsKeepAllFirstAndUnlabelledLast` (`TagChipRowTest.cpp:415`) pins the latter. The row
  keeps its two-line cap, so its first two lines become the alphabetically first tags (batch §5).
- **A16. The filter's class comment splits.** The retire paragraph (`TagFilterActivity.h:19-22`) and
  the "Both exits MUST set a result" note (`:36-40`) describe base behaviour and move to the base.
  The result paragraphs (`:14-17`, `:24-25`) stay. "paged only when it outgrows one screen" moves
  with the paging. `PassageLinksActivity.h:24` cites that note "for the reason TagFilterActivity
  gives" and is updated to name `TagChipGridActivity`.
- **A17. `OptionPopup`'s `std::function` callback stays.** The retire popup's API takes one
  (`OptionPopup.h:42-43`, `:241`). This moves an existing call (`TagFilterActivity.cpp:181-185`);
  it adds no new `std::function` signature. The hooks themselves are virtual functions.
- **A18. The held-button page turn moves as is, though this board cannot reach it.**
  - `navigateButtons` keeps `onNextContinuous` / `onPreviousContinuous` → `turnPage`
    (`TagFilterActivity.cpp:225-226`), as the brief lists it.
  - On the X4 Pro, a nav key held past 250 ms stops reporting `isPressed`
    (`NavKeyGestures.h:43-48`, `HalGPIO.cpp:198-199`). `ButtonNavigator` needs `isPressed` and a
    held time over 500 ms together (`ButtonNavigator.cpp:58-75`, `ButtonNavigator.h:41`), so it never
    fires here.
  - A hold of 850 ms or more resolves on release to Back (Left) or Confirm (Right) instead
    (`NavKeyGestures.h:12-25`, `:41`, `HalGPIO.cpp:210-214`).
  - The device checks therefore page by swipe and by stepping across a page boundary, not by holding
    a key.

## Testing strategy

### Host (`test/tag_rows/TagChipRowTest.cpp`, target `TagRowsTest`)

Written first. Each test fails to compile until its helper exists, then passes against it. The
`FakeChip` struct (`TagChipRowTest.cpp:642-645`) stands in for `ChipEntry`, as for `findChip`.

- `TagChipReselect`:
  - A tag that moved in a re-sort is followed to its new index.
  - A retired tag clamps: to `previous` while it is in range, to the last chip past the end.
  - All, Unlabelled and NewTag are found by kind alone.
  - No key → `previous`, clamped. A negative `previous` → 0. An empty list → 0.
- `TagChipFilterKey`:
  - `nullopt` → `{All}`; `UNLABELLED` → `{Unlabelled}`; a tag → `{Tag, id}`.
  - Integration with `candidates`: a zero-count tag that is the filter is kept by `candidates`.
    Map each `Candidate` to a `FakeChip{kind, kind == Tag ? ids[slot] : UNLABELLED}`, as
    `buildEntries` does (`TagChipView.cpp:24-45`); `Candidate` carries a slot, not an id
    (`TagChipRow.h:101-105`). Then `reselect(mapped, filterKey(filter), 0)` lands on that chip, at
    its name-order position between All and Unlabelled.

Not host-testable: no host target compiles an activity `.cpp`, `TagChipView.cpp` or `UiAppHost`
(research note, "Testability"). The base, the subclass and the `buildEntries` order are covered by
the build and the device checks.

### Build and format

- Full host suite: `cmake --build build/test && ctest --test-dir build/test -j8`. Baseline: 1736
  of 1736 pass (research note).
- `pio run -e x4pro`: still no warning in `src/` or `lib/` (Baseline, below), and the flash delta
  reported.
- `./bin/clang-format-fix` over the whole tree leaves `git status` clean.

### Device checks (owner)

On a publication with enough tags that the filter grid pages (the owner's ~48-tag palette):

1. **Order.** Highlights' chip row and the filter grid list tags alphabetically. Accented names sort
   among unaccented ones. All is first, Unlabelled last (when shown).
2. **Paging.** The `x/y` strip appears only when the grid has more than one page. A swipe up or
   down turns a page; at the first or last page it does nothing.
3. **Button stepping.** A tap on one nav key steps forward one chip, on the other back one, and
   draws the focus ring. Stepping past the last chip on a page turns the page. Holding Right
   (≥ 850 ms) activates the focused chip, as a tap on it would. Holding a key does not turn pages on
   this board (A18).
4. **Entry selection.** Opening the grid with a tag filter active shows that chip inverted, on the
   page that holds it.
5. **Retire.** Long-press a tag chip → the popup → Delete. The chip disappears, and counts and
   order stay correct. If it was the active filter, All becomes inverted. Long-press on All or
   Unlabelled does nothing. On a grid of two or more pages, retire a chip on page 2: the grid stays
   on page 2 and does not return to page 1. That is the clamp, wired through `rebuildChips()`.
   The host tests reach only the pure `reselect`.
6. **Home under the popup.** With the retire popup open, Home does nothing and the popup stays. Home
   with no popup cancels to Highlights, keeping its filter.
7. **Back.** Back (a left-edge swipe, `MappedInputManager.cpp:223`, `:257`, or Left held
   ≥ 850 ms) cancels to Highlights, keeping its filter.
8. **Tap.** Tapping a chip filters Highlights by it; All clears the filter.
9. **Heap.** `ESP.getFreeHeap()` over serial is flat across a grid enter/exit cycle.

A failed retire save cannot be forced on the device without a damaged card. "Could not save tag" is
checked by reading `retireTag`.

## Baseline

Measured before any change, on this branch at `c375e379`:

- Host: `ctest --test-dir build/test -j8` → 1736/1736 passed; `TagRowsTest` → 65 tests.
- Firmware, `~/.platformio/penv/bin/pio run -e x4pro` (PlatformIO 6.1.19): RAM 66,228 B of 327,680;
  Flash 5,563,214 B of 6,553,600 (84.9 %); `firmware.bin` 5,563,728 B. The log has 248 warning lines,
  all in `.pio/libdeps` and `managed_components`; none in `src/` or `lib/`
  (`grep "warning:" | grep -E "^(src|lib)/"` is empty). "No new warnings" therefore means none
  in `src/` or `lib/`.

## Review 0 outcome

Review: `docs/superpowers/reviews/btrial-4-spec-review-0.md`. Verdict CLEAR, with no BLOCKER or
MAJOR. All three MINORs were accepted and fixed:

| Finding | Change |
|---|---|
| m1. No device check shows the clamp's wiring | Device check 5 now retires a chip on page 2 and expects the grid to stay on page 2 |
| m2. An open `handleButtons` could bypass the `final` Back and Confirm routes | The base declares `bool handleButtons() final;` forwarding to `UiListActivity::handleButtons`. Added to A12 and to input stage 3 |
| m3. The `filterKey` integration test did not compile as written | It maps `Candidate` to `FakeChip` as `buildEntries` does, then asserts through `reselect` |
