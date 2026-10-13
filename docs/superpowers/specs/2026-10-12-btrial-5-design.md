# Design: `TagPickerActivity` on the chip grid (btrial-5)

Surface `ui`. Branch `feature/tag-picker-grid`, level with `batch/20261012-tag-chip-picker-2l3m`
plus the research commit `8789923e`.

- **Governing spec:** `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (the batch
  spec): Problem, Goal, Non-goals, D3–D5, D7, D8, the §3 hook table, §4, the picker rows of §6, and
  A1, A3, A4.
- **Research:** `docs/superpowers/research/2026-10-12-btrial-5-research.md` (the research note).
- **Precedence:** where this design and the batch spec disagree, the batch spec wins. Every place
  this design narrows or fills in the batch spec is a labelled assumption below.

Line numbers are current on this branch.

## Problem

1. **The picker is a scrolling switch list.**
   - `TagPickerActivity` renders "Done", one toggle row per active tag, then "New tag..."
     (`TagPickerActivity.cpp:84-102`), through a 202-entry `rowItems_` array
     (`TagPickerActivity.h:57`, `:110`).
   - With the owner's ~48-tag palette, finding a tag means scrolling the whole list.
   - The filter grid already shows the same tags as a paged, alphabetical chip grid, on a shared
     base, `TagChipGridActivity` (`TagChipGridActivity.h:31`).
2. **The picker commits in name order on this base.**
   - `commitAndFinish` walks `tags_` (`TagPickerActivity.cpp:220-222`).
   - `tags_` is `STUDY.activeTags()` (`:27`), which walks `activeIdsByName()` since btrial-1
     (`src/study/StudyStore.cpp:160-164`). On `main` it walked `activeIds()`.
   - So the batch head stores a passage's tags alphabetically. The sleep screen shows a passage's
     first stored tag (batch Non-goals), so its pill would change. Research note, "The commit order
     has already drifted".
3. **The picker writes its selection unlocked.**
   - `toggleTag` and the create handler change `selectedIds_` without `RenderLock`
     (`TagPickerActivity.cpp:140`, `:173`).
   - The render task reads it at the same time (`:94`). This is batch review 0, M2.
4. **A toggle can flash the old state.** A tap arms a flash for the repaint that shows its result
   (`FreeInkApp.h:705-715`, `:733-742`), and `toggleTag` never clears it (`:130-142`). This is
   batch review 0, B1.
5. **At 8/8, extra tags look selectable.**
   - Unapplied tags look exactly like ones that can be added.
   - A tag created at the cap is left unapplied with no message (`TagPickerActivity.cpp:173`).
6. **Selections the picker cannot show still count.**
   - A selected id that is not active is kept unless it is `UNLABELLED` (`:29-30`).
   - It has no row, but counts toward 8 (batch D8).
7. **Dead weight around the old list.**
   - `TagRowMapping.h` exists only for the row offset. `TagRowsTest.cpp` tests it.
   - The held-Confirm retire (`TagPickerActivity.cpp:298-320`) cannot fire on this board, because
     a synthesised Confirm reports a 40 ms hold (`lib/Input/Input/NavKeyGestures.h:64-68`) and the
     threshold is 700 ms (`TagPickerActivity.cpp:21`).
   - The picker's own `render` copy (`:322-341`) lacks the base's `PostedMessage::drawNext`
     (`TagChipGridActivity.cpp:282`).

## Goal

The batch spec's Goal 2 for the picker, matching the brief's acceptance:

- `TagPickerActivity` subclasses `TagChipGridActivity`.
  - Chips: **+ New tag** first, then the active tags alphabetically.
  - Each tag chip is Active, Inactive or Disabled, from `TagChips::pickerState`.
- The header reads `Tags n/8`, and a **Done** pill sits at the rect `drawHeaderWithAction` returns.
  - A tap anywhere in the header band's height commits.
  - A long-press does nothing.
  - Done does nothing under the retire popup.
- **A tap or Confirm** on a Disabled chip shows the limit message and changes nothing.
- **A toggle** shows no tap flash, and a removed tag repaints white.
- **+ New tag** creates a tag and the grid lands on its page.
  - At 8/8, a new name shows the limit message.
  - A name that dedupes to an applied tag shows nothing.
- **Ids that are not active** are dropped at open.
- **Exits:** Done and Home commit; Back cancels.
- **The committed ids are in palette allocation order**, so stored order and the sleep-screen tag
  pill are unchanged.
- **Removed:** `tags_`, `rowItems_`, the held-Confirm retire, `TagRowMapping.h`, `TagRowsTest.cpp`
  and `STR_TAG_NEW`.

## Non-goals

- **Untouched types and callers:**
  - `TagSelectionResult` (`src/activities/ActivityResult.h:81-88`).
  - Both callers: `PassageSelectActivity.cpp:329-341`, and `HighlightsActivity.cpp:474-477` with
    `applyTagEdit`.
- **Untouched data:** the passage file, the palette file, stored tag order, the sleep screen, and
  any store or cache version.
- **Not added:** counts on picker chips, tag search, selected-first grouping (batch D5, Non-goals).
- **Other batch tasks' code:**
  - `TagChipGridActivity`, `TagFilterActivity` and `TagChipView` are btrial-4's and btrial-2's, and
    are unchanged.
  - `BaseTheme::drawHeaderWithAction` and `HeaderLayout` are btrial-3's, and are unchanged.
- **`StudyStore::activeTags()`** is left in place, though this change removes its last caller
  (research note). Removing it is `src/study` work, filed as a discovery when `implement` lands.
- **The stale comment in `TagPalette.h:19-23`** about "TagPickerActivity's selection today" is not
  this change.

## Modelled on

- **`TagFilterActivity.{h,cpp}`**, the first subclass of the base, for the class shape:
  - `onEnter` → base `onEnter` → `rebuildChips(...)` (`TagFilterActivity.cpp:14-17`).
  - `chipState` reads one member that the loop task writes under `RenderLock` (`:25-27`, `:44-50`).
  - `activateIndex` guards `retireConfirmationOpen()` and clears the flash before it finishes
    (`:29-42`).
  - `onHome` (`:52-55`).
- **`HighlightsActivity`** for a second action beside `ACTION_ROW`:
  - The ids `ACTION_CHIP = ACTION_USER` and `ACTION_MORE = ACTION_USER + 1`
    (`HighlightsActivity.h:103-104`).
  - Static trampolines `(const fui::ActionEvent&, void*)` (`.h:110-111`), registered with `app.on`
    right after `UiListActivity::onEnter()` (`HighlightsActivity.cpp:40-43`).
  - `onMoreEvent` checks its popup before it acts (`:140-148`).
- **`PublicationsActivity.cpp:39-49`** for a `drawChrome` override that draws its own header from
  `headerTitle()`.
- **`EpubReaderMenuActivity.cpp:450-468`** for a pill drawn shorter than its band, with a hit rect
  grown by `hitPadding` to the band ("The pill is drawn at text height; its hit rect fills the band,
  a touch target tall").
- **`UITheme.h:43-45` and `OptionPopup.h:244-245`** for `mutable` state written by a `const` member
  on the render path (A3).
- **`TagChips::pickerState` / `reselect`** (`TagChipRow.h:93-99`, `:151-161`) and their tests
  (`test/tag_rows/TagChipRowTest.cpp:603-639`, `:641` on) for the pure helpers and their host tests.
- **Today's `TagPickerActivity.cpp:144-198`** for the keyboard create flow and its failure messages,
  kept.

## Architecture

### Files

| File | Change |
|---|---|
| `src/activities/reader/TagPickerActivity.h` / `.cpp` | Rewritten as a `TagChipGridActivity` subclass. |
| `src/activities/reader/TagChipRow.h` | Adds `selectedInOrder` and `bandHitPadding`. |
| `src/activities/reader/TagRowMapping.h` | Deleted. |
| `test/tag_rows/TagRowsTest.cpp` | Deleted. |
| `test/tag_rows/CMakeLists.txt` | Drops the `TagRowsTest.cpp` line (`:2`); the target and its other two sources stay (`:1-5`). |
| `test/tag_rows/TagChipRowTest.cpp` | Tests for the two helpers. |
| `test/ui_layout/HeaderLayoutTest.cpp` | One test: `bandHitPadding` over every header layout. This target already includes `TagChipRow.h` (`HeaderLayoutTest.cpp:10`). |
| `lib/I18n/translations/english.yaml` | Drops `STR_TAG_NEW` (`:373`). |

**No new build wiring.**
- PlatformIO compiles every file under `src/`. There is no `build_src_filter` in `platformio.ini`
  (research note).
- The shared `test/CMakeLists.txt` keeps `add_subdirectory(tag_rows)` (`:76`). `src/main.cpp` is
  untouched.
- `english.yaml` is on `ui-dev.md`'s list of shared files. Its edit is in scope: the brief names it,
  btrial-2 edited the same file in this batch (`68ce7058`), and no sibling task remains (research
  note, "Removals").

### `TagChipRow.h`: two pure helpers

Both go beside the existing helpers. The header stays free of FreeInkUI (`TagChipRow.h:12-13`).

`selectedInOrder` goes after `reselect` (`:151-161`):

```cpp
// The ids of `order` that `selected` holds, in `order`'s order and each once. The picker passes the
// palette's allocation order, so what it commits and keeps does not depend on display order, tap
// order, or ids the palette no longer lists.
inline std::vector<study::TagId> selectedInOrder(const std::vector<study::TagId>& order,
                                                 const std::vector<study::TagId>& selected) {
  std::vector<study::TagId> out;
  out.reserve(std::min(order.size(), selected.size()));
  for (const study::TagId id : order) {
    if (std::find(selected.begin(), selected.end(), id) != selected.end()) out.push_back(id);
  }
  return out;
}
```

`bandHitPadding` goes after the `Pad` overloads of `hitPadding` (`:335-358`):

```cpp
// Grows a chip drawn shorter than its band to the band's full height, as the header's Done pill is.
inline Pad bandHitPadding(const int bandTop, const int bandHeight, const int chipTop, const int chipHeight) {
  Pad pad;
  pad.top = std::max(0, chipTop - bandTop);
  pad.bottom = std::max(0, bandTop + bandHeight - (chipTop + chipHeight));
  return pad;
}
```

`<algorithm>` and `<vector>` are already included (`TagChipRow.h:5`, `:10`).

The create outcome needs no helper of its own. It is `pickerState(Kind::Tag, ...)` with Active read
as "already applied" (A13).

### `TagPickerActivity.h`

```cpp
#pragma once

#include <StudyStore/TagPalette.h>

#include <string>
#include <vector>

#include "TagChipGridActivity.h"
#include "components/themes/BaseTheme.h"

// The tags of one passage, as a paged chip grid: + New tag, then every active tag alphabetically.
// A tag chip is Active when applied, Inactive when it can be added, and Disabled once the passage
// holds PassageDoc::MAX_TAGS_PER_PASSAGE; a tap on a Disabled chip explains the limit. Returns the
// chosen ids as a TagSelectionResult, in palette allocation order so a passage's stored order does
// not follow the display; the caller applies them. Ids that are not active are dropped on entry:
// they have no chip, and must not fill the cap.
//
// Done (in the header) and Home commit; Back cancels. Done is a touch target only: a button user
// commits with Home, which this board always has.
//
// + New tag creates the tag and persists the palette at once, before the passage is saved: the
// palette is global, so a passage edit cancelled afterwards must not take the new tag with it.
class TagPickerActivity final : public TagChipGridActivity {
 public:
  explicit TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::vector<study::TagId> initialSelection = {});

  void onEnter() override;

 private:
  static constexpr freeink::ui::ActionId ACTION_DONE = ACTION_USER;

  const char* headerTitle() const override;
  void drawChrome() override;
  void buildChips(std::vector<TagChipView::ChipEntry>& out) override;
  TagChips::ChipState chipState(const TagChipView::ChipEntry& chip) const override;
  void buildHeaderExtras(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTagRetired(study::TagId id) override;
  void onHome() override;

  static void onDoneEvent(const freeink::ui::ActionEvent& event, void* user);
  void startNewTagFlow();
  void reportAddTagFailure(const std::string& name);
  void commitAndFinish();
  bool isSelected(study::TagId id) const;
  void setSelected(study::TagId id, bool on);

  // Written under RenderLock: chipState and the title read it on the render task.
  std::vector<study::TagId> selectedIds_;
  // Render task only. headerTitle formats the title; drawChrome records where the theme put the
  // header and Done, and buildHeaderExtras draws Done there later in the same render.
  mutable char title_[32] = {};
  Rect headerBand_;
  Rect doneRect_;
};
```

These members of today's class go away:
- `MAX_ROWS`, `DONE_ROW` and `tagIndexForRow`.
- The `listCount`, `buildScreen`, `onRowLongPress`, `handleCustomInput`, `handleButtons`,
  `onBackButton`, `drawFooter`, `handleHomeGesture` and `render` overrides. The base declares each of
  these `final` (`TagChipGridActivity.h:33-34`, `:58-71`), so they cannot stay.
- `toggleTag`, `showRetireConfirmation` and `retireTag`.
- `tags_`, `confirmingDelete_`, `confirmPopup_`, `pendingRetireRow_` and `rowItems_`.

`Rect` is the firmware's (`src/components/themes/BaseTheme.h:11-18`), which `drawHeaderWithAction`
returns (`:232`). The include is there because the header names it.

### `TagPickerActivity.cpp`, by member

```cpp
TagPickerActivity::TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::vector<study::TagId> initialSelection)
    : TagChipGridActivity("TagPicker", renderer, mappedInput),
      selectedIds_(TagChips::selectedInOrder(STUDY.palette().activeIds(), initialSelection)) {}

void TagPickerActivity::onEnter() {
  TagChipGridActivity::onEnter();
  app.on(ACTION_DONE, &TagPickerActivity::onDoneEvent, this);
  rebuildChips();
}

const char* TagPickerActivity::headerTitle() const {
  snprintf(title_, sizeof(title_), "%s %u/%u", tr(STR_TAGS), static_cast<unsigned>(selectedIds_.size()),
           static_cast<unsigned>(study::PassageDoc::MAX_TAGS_PER_PASSAGE));
  return title_;
}

void TagPickerActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  headerBand_ = Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight};
  doneRect_ = GUI.drawHeaderWithAction(renderer, headerBand_, headerTitle(), tr(STR_DONE));
}

void TagPickerActivity::buildChips(std::vector<TagChipView::ChipEntry>& out) {
  const std::vector<study::TagId> ids = STUDY.palette().activeIdsByName();
  out.reserve(ids.size() + 1);
  TagChipView::ChipEntry newTag;
  newTag.kind = TagChips::Kind::NewTag;
  snprintf(newTag.label, sizeof(newTag.label), "%s", tr(STR_TAG_NEW_CHIP));
  out.push_back(newTag);
  for (const study::TagId id : ids) {
    TagChipView::ChipEntry chip;
    chip.kind = TagChips::Kind::Tag;
    chip.id = id;
    snprintf(chip.label, sizeof(chip.label), "%s", STUDY.palette().name(id).c_str());
    out.push_back(chip);
  }
}

TagChips::ChipState TagPickerActivity::chipState(const TagChipView::ChipEntry& chip) const {
  return TagChips::pickerState(chip.kind, isSelected(chip.id), selectedIds_.size(),
                               study::PassageDoc::MAX_TAGS_PER_PASSAGE);
}

void TagPickerActivity::buildHeaderExtras(UiScreen& screen) {
  if (doneRect_.width <= 0 || doneRect_.height <= 0) return;
  const freeink::ui::Rect rect{static_cast<int16_t>(doneRect_.x), static_cast<int16_t>(doneRect_.y),
                               static_cast<int16_t>(doneRect_.width), static_cast<int16_t>(doneRect_.height)};
  TagChipView::draw(screen, rect, tr(STR_DONE), TagChips::ChipState::Action, ACTION_DONE, 0, freeink::ui::InputTouch,
                    TagChips::bandHitPadding(headerBand_.y, headerBand_.height, doneRect_.y, doneRect_.height));
}

void TagPickerActivity::activateIndex(const int index) {
  if (retireConfirmationOpen()) return;
  if (index < 0 || index >= listCount()) return;
  const TagChips::Kind kind = chips()[static_cast<size_t>(index)].kind;
  const study::TagId id = chips()[static_cast<size_t>(index)].id;

  switch (TagChips::pickerState(kind, isSelected(id), selectedIds_.size(), study::PassageDoc::MAX_TAGS_PER_PASSAGE)) {
    case TagChips::ChipState::Action:
      startNewTagFlow();
      return;
    case TagChips::ChipState::Disabled:
      ReaderUtils::showMessage(renderer, tr(STR_TAG_LIMIT_PER_HIGHLIGHT));
      requestUpdate();
      return;
    case TagChips::ChipState::Active:
    case TagChips::ChipState::Inactive:
      break;
  }
  // The flash would repaint the chip for one frame in the state it is leaving.
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    setSelected(id, !isSelected(id));
  }
  requestUpdate();
}

void TagPickerActivity::startNewTagFlow() {
  app.clearTapFlash();
  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto& keyboard = std::get<KeyboardResult>(result.data);
    const std::optional<study::TagId> id = STUDY.addTagName(keyboard.text);
    if (!id) {
      reportAddTagFailure(keyboard.text);
      return;
    }
    switch (TagChips::pickerState(TagChips::Kind::Tag, isSelected(*id), selectedIds_.size(),
                                  study::PassageDoc::MAX_TAGS_PER_PASSAGE)) {
      case TagChips::ChipState::Inactive: {
        RenderLock lock(*this);
        setSelected(*id, true);
        break;
      }
      case TagChips::ChipState::Disabled:
        ReaderUtils::showMessage(renderer, tr(STR_TAG_LIMIT_PER_HIGHLIGHT));
        break;
      case TagChips::ChipState::Active:  // the name is a tag already on this passage
      case TagChips::ChipState::Action:
        break;
    }
    rebuildChips(TagChips::ChipKey{TagChips::Kind::Tag, *id});
  };
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TAG_NAME_PROMPT), std::string(),
                                              study::TagPalette::MAX_TAG_NAME_BYTES, InputType::Text),
      handler);
}

void TagPickerActivity::onTagRetired(const study::TagId id) {
  RenderLock lock(*this);
  setSelected(id, false);
}

void TagPickerActivity::onHome() { commitAndFinish(); }

void TagPickerActivity::onDoneEvent(const freeink::ui::ActionEvent&, void* user) {
  auto* self = static_cast<TagPickerActivity*>(user);
  if (self->retireConfirmationOpen()) return;
  self->commitAndFinish();
}

void TagPickerActivity::commitAndFinish() {
  app.clearTapFlash();
  TagSelectionResult result;
  result.tagIds = TagChips::selectedInOrder(STUDY.palette().activeIds(), selectedIds_);
  setResult(std::move(result));
  finish();
}
```

`isSelected`, `setSelected` and `reportAddTagFailure` are today's (`TagPickerActivity.cpp:38-49`,
`:183-198`), unchanged.

The comments in these sketches are the ones the code keeps. Each states a reason the code does not
show, as `CLAUDE.md` asks.

### The hook table, filled in

| Hook | Runs on | Lock | Picker |
|---|---|---|---|
| `buildChips(out)` | loop | none held | **+ New tag**, then `activeIdsByName()`, names only |
| `chipState(chip)` | render | inside the render | `pickerState(kind, isSelected(id), size, 8)` |
| `buildHeaderExtras(screen)` | render | inside the render | Done at `doneRect_`, padded to `headerBand_` |
| `activateIndex(i)` | loop | takes `RenderLock` to write | keyboard, limit message, or toggle |
| `onTagRetired(id)` | loop | takes `RenderLock` to write | deselect `id` |
| `onHome()` | loop | none | commit |

The picker also overrides two members outside the table:
- `headerTitle`, on the render task (called from `drawChrome`).
- `drawChrome`, on the render task (`TagChipGridActivity.cpp:270`, `:274`).

## Data and control flow

### Entry

1. A caller constructs the picker on the loop task:
   - `PassageSelectActivity.cpp:330` with no selection.
   - `HighlightsActivity.cpp:476` with the passage's tags.
2. The constructor sets `selectedIds_` to the selection, filtered to active ids and in allocation
   order (A1, A2). Nothing is rendering this object yet, because it is not the current activity
   until the push (`ActivityManager.cpp:150-160`).
3. `onEnter` runs on the loop task with no lock held (`ActivityManager.cpp:157-158`):
   - The base `onEnter` resets `nav`, rebinds the theme, registers `ACTION_ROW`, sets the screen and
     requests a render (`UiListActivity.cpp:20-27`).
   - `app.on(ACTION_DONE, ...)` registers Done.
   - `rebuildChips()` builds the list. With nothing selected yet, `nav.selected` stays 0, which is
     **+ New tag** on page 1 (A17).

### Render (render task, `RenderLock` held, `ActivityManager.cpp:52-59`)

The base `render` (`TagChipGridActivity.cpp:266-283`) runs in this order:

1. `clearScreen`.
2. `drawChrome()`:
   - It records `headerBand_` as the rect `UiListActivity::drawChrome` uses
     (`UiListActivity.cpp:145-146`).
   - It formats `Tags n/8` (`headerTitle`).
   - It calls `drawHeaderWithAction`, which draws the title and battery and returns `doneRect_`
     (`BaseTheme.cpp:291-294`, `:415`).
3. `renderUi()` → `buildScreen`:
   - `buildGrid` draws a page of chips, each as `chipState(chip)` (`TagChipGridActivity.cpp:96-106`).
   - `buildHeaderExtras` then draws Done at `doneRect_`, with a hit rect grown to the band's height
     (A5–A8).
4. The popup, if open, renders over everything and the base returns. Otherwise `drawFooter`,
   `displayBuffer` and `PostedMessage::drawNext` (`:278-282`).

### Input (loop task)

The base's order is unchanged:
1. The popup sees input first (`TagChipGridActivity.cpp:204-219`).
2. Then Back and Confirm (`:221`, `UiListActivity.cpp:48-59`).
3. Then touch routing (`UiListActivity.cpp:84-87`).
4. Then button stepping.

| Input | Path | Picker result |
|---|---|---|
| Tap a chip | `onRowAction` → `activateIndex(i)` (`TagChipGridActivity.cpp:150-161`) | per `pickerState` (A11) |
| Confirm (Right held, `HalGPIO.cpp:213-214`) | `activateIndex(nav.selected)` | same as a tap (batch A3) |
| Long-press a chip | `onRowLongPress` → popup, `Kind::Tag` only (`:163-172`) | retire, Disabled chips included (A18) |
| Tap Done | `onDoneEvent` | commit (A9, A10) |
| Long-press Done | `findTouch` skips it (`FreeInkUICore.h:1171-1176`) | nothing (A8) |
| Home | `handleHomeGesture` → `onHome()` unless the popup is open (`:143-148`) | commit |
| Back | `onBackButton` (`:136-141`) | cancelled result |
| Swipe up/down, step | base | page turn, chip step. Done is never focused (A4 of the batch) |

### Toggle (`activateIndex`, loop task, no lock on entry)

1. If the popup is open, return. This is defence only, because the popup has already consumed the
   input (`OptionPopup.h:65-131`).
2. Copy the chip's `kind` and `id` from `chips()`. The loop task is the only writer of `chips_`
   (`TagChipGridActivity.h:79-80`).
3. Recompute `pickerState` from `selectedIds_`, read unlocked (A12):
   - **Action:** `startNewTagFlow()`.
   - **Disabled:** post `STR_TAG_LIMIT_PER_HIGHLIGHT` and request a render, which shows it.
   - **Active / Inactive:** `app.clearTapFlash()`, then flip the id under `RenderLock`, then
     `requestUpdate()`.
4. No chip list changes and no rebuild. The title and every chip's state come from `selectedIds_` at
   the next render. So does reaching or leaving 8/8.

### Create (`startNewTagFlow`, loop task)

1. Clear the flash, then push `KeyboardEntryActivity`. This is today's flow
   (`TagPickerActivity.cpp:148`, `:177-180`).
2. On return, the handler runs on the loop task with no lock held. It runs after the picker is
   current again and before its render is requested (`ActivityManager.cpp:115-132`). No `onEnter`
   runs.
3. Cancelled: nothing changes.
4. `STUDY.addTagName(text)`, with no lock (A14):
   - It returns an active id, or nullopt (`StudyStore.cpp:250-264`).
   - nullopt → `reportAddTagFailure`, which is today's code. Done.
5. The outcome is `pickerState(Kind::Tag, isSelected(id), size, 8)`:
   - **Inactive:** select it under `RenderLock`.
   - **Disabled:** post the limit message. The tag stays unapplied.
   - **Active:** nothing. The name deduped to a tag already applied.
6. `rebuildChips(ChipKey{Tag, id})`, on every path that has an id:
   - It re-sorts in the new name.
   - It selects the id's chip, so the grid lands on its page.
   - It closes routing and requests the render (`TagChipGridActivity.cpp:37-52`).

### Retire (base, loop task)

The base handles the popup, `saveDisabled`, `STUDY.retireTag`, the save-failure message and the
rebuild (`TagChipGridActivity.cpp:174-202`). It calls `onTagRetired(id)` when the id is no longer
active, before the rebuild (`:199-200`). The picker deselects it under `RenderLock`. The count
drops, and Disabled chips may become Inactive again.

### Commit and cancel

- **Done and Home** both reach `commitAndFinish`:
  - Clear the flash.
  - `selectedInOrder(palette().activeIds(), selectedIds_)` → `TagSelectionResult`.
  - `setResult`, then `finish()`.
- **Back** is the base's cancelled result. The palette edits made while the picker was open (creates
  and retires) are already saved. This is today's contract (`HighlightsActivity.cpp:479-497`, "the
  picker persists palette changes itself").

## Error handling

No new failure path.

- **`addTagName` fails:** `reportAddTagFailure` says empty, too long, or palette full
  (`TagPickerActivity.cpp:183-198`, unchanged).
  - `saveDisabled` also returns nullopt (`StudyStore.cpp:251`), which would read as "palette full".
    That cannot happen here, because both callers refuse to open the picker while `saveDisabled` is
    set (`PassageSelectActivity.cpp:29-33`, `HighlightsActivity.cpp:461-468`).
- **A retire's save fails:** the base posts `STR_TAG_SAVE_FAILED` (`TagChipGridActivity.cpp:201`).
  It still calls `onTagRetired`, because `retireTag` does not roll back the in-memory retire
  (`StudyStore.cpp:266-273`).
- **`saveDisabled` at retire:** the base posts `STR_HIGHLIGHTS_LOAD_FAILED` and opens no popup
  (`TagChipGridActivity.cpp:176-182`).
- **Disabled tap or create at the cap:** `STR_TAG_LIMIT_PER_HIGHLIGHT` (`english.yaml:379`), as a tap
  at the cap does today (`TagPickerActivity.cpp:134-137`).
- **The picker writes no file of its own.** The result is applied and saved by the caller
  (`HighlightsActivity.cpp:479-491`, `PassageSelectActivity.cpp:341`).
- **Every message is posted, not drawn** (`ReaderUtils.h:233`). The base render draws it after
  `displayBuffer` (`TagChipGridActivity.cpp:282`), which the old picker render never did.

## Memory and cost

The PSRAM routing threshold is 4,096 B (`sdkconfig.defaults:2260-2261`).

| Item | Size | Where | Lifetime |
|---|---|---|---|
| `chips_` | (active + 1) × 44 B: 2,156 B at 48 tags, 8,844 B at the 200-tag cap | internal SRAM at 48 tags; PSRAM past 93 chips | the picker's visit (base member) |
| `widths_` | 4 B per chip: 196 B at 48 tags | internal SRAM | the visit |
| `activeIdsByName()` in `buildChips` | 2 B per active id: 96 B at 48 tags | internal SRAM | one rebuild |
| `activeIds()` at open and at commit | 2 B per palette entry | internal SRAM | one call |
| `selectedIds_` | ≤ 8 × 2 B | heap | the visit, as today |
| `title_`, `headerBand_`, `doneRect_` | 32 + 16 + 16 B | inside the activity object | the visit |

**Removed:**
- `rowItems_`: 202 `fui::ListItem`s inside the activity object (`TagPickerActivity.h:57`, `:110`).
- `tags_`: a `TagView`, an id plus a `std::string`, per tag, re-copied on **every render**
  (`TagPickerActivity.cpp:79`). Names over 15 bytes each allocate.

**Net.**
- At the owner's 48 tags the picker holds about 2.4 KB more internal SRAM than the `tags_` it
  replaces while it is open. The batch spec's §6 allows this (2.2 KB for `chips_`).
- The render path allocates nothing: no `tags_` copy, and labels are copied on the loop task at
  rebuild time.
- Rebuilds happen at entry, after a create and after a retire. A toggle does not rebuild.

**Interactions:** at most 64 chips a page (`TagChipRow.h:244`) plus Done is 65, under
`UiAppHost::MAX_INTERACTIONS` = 96 (`UiAppHost.h:35`). The handlers are `ACTION_ROW` and
`ACTION_DONE`, 2 of the 6 `FreeInkApp<96, 6>` allows (`UiAppHost.h:36`).

## Assumptions

- **A1. The open filter and the commit both use `selectedInOrder(palette().activeIds(), ...)`.**
  - The brief asks the constructor to keep ids that `isActive` accepts. Membership in `activeIds()`
    is the same test:
    - `isActive` returns the active flag of the entry with that id, false for none
      (`TagPalette.cpp:67-72`).
    - `activeIds` lists exactly the active entries' ids (`:89-96`).
    - Ids are unique: `add` allocates `nextRaw_++` (`:55`), and `fromJson` drops id 0 and duplicate
      ids (`:173-181`).
  - `UNLABELLED` is id 0 and never an entry (`TagPalette.h:29-31`), so it is dropped, as
    `TagPickerActivity.cpp:29-30` drops it today.
  - Using one helper for both keeps "visible" and "committed" the same definition.
- **A2. `selectedIds_`'s order is not observable.**
  - `chipState` and the title use only membership and size.
  - The commit re-derives allocation order from the palette.
  - So the constructor may reorder the caller's selection, which `selectedInOrder` does.
  - Duplicates cannot survive. `selectedInOrder` emits each id of `order` at most once, and
    `setSelected` never adds a present id (`TagPickerActivity.cpp:42-49`).
- **A3. `Tags n/8` is formatted inside `headerTitle() const`, into a `mutable` buffer.**
  - This is the brief's placement. `headerTitle` is `const` in the base (`UiListActivity.h:65`).
  - The repo already writes `mutable` state from `const` members on the render path:
    `UITheme.h:43-45` (cached metrics) and `OptionPopup.h:244-245` (an interaction table that
    `processRender() const` fills).
  - Only the render task calls it, from `drawChrome`, under the render's lock. The buffer has no
    other reader.
  - 32 bytes holds "Tags 8/8" with 23 bytes to spare for a translated `STR_TAGS`.
    `STR_TAGS` exists only in English today (`english.yaml:372`). `snprintf` truncates safely.
- **A4. The header band is `UiListActivity::drawChrome`'s rect, recorded every render.**
  - The rect is `Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}`
    (`UiListActivity.cpp:145-146`), so the picker's header sits where every other list screen's
    does.
  - `drawChrome` runs before `renderUi` within one `render()` (`TagChipGridActivity.cpp:270-271`,
    `:274-275`). `buildHeaderExtras` therefore reads this render's rect, never the previous one's.
  - The battery percent changes width, so the rect can move between renders (batch §4). It is
    recomputed every time.
- **A5. Done is a `TagChipView::draw` chip in `ChipState::Action`, at exactly the returned rect.**
  - This is batch §2's "Action: + New tag and the header Done pill".
  - The theme measured it in the same face. `drawHeaderBand` measures the label in `smallText`
    bold, through a target bound to the uiScale fonts (`BaseTheme.cpp:303`, `:311-319`). The app
    binds the same fonts (`UiAppHelpers.h:65-72`), and `draw` uses `smallText` bold for Action
    (`TagChipView.cpp:119-120`).
  - The rect is never inset. The theme says a bordered label in it "must not be inset vertically"
    (`BaseTheme.h:226-231`).
  - **Under Classic, text overflows by about 1 px.** The rect is 25 px tall
    (`HeaderLayoutTest.cpp:261-270`), and the 24 px line is drawn from the button's content top
    without clipping (`FreeInkUIGfxRenderer.h:172-180`, `button.h:56`). This is device check 5.
- **A6. Done's hit rect is the pill grown vertically to the full header band, and no wider.**
  - `bandHitPadding(band, rect)` sets top and bottom only. `fui::button` adds them to the visual
    rect (`button.h:36-43`), and `draw` sets `minTouchSize = 0` (`TagChipView.cpp:124`).
  - That is 44 px tall under Lyra (4 + 36 + 4) and 45 px under Classic (20 + 25 + 0)
    (`HeaderLayoutTest.cpp:247-270`). Both meet the 44 px touch minimum.
  - Under Classic the grown rect also covers the battery strip's columns above the pill. Nothing
    there is touchable: the header frame registers no hits (`BaseTheme.cpp:296-302` comment,
    "Non-interactive frame").
  - The grid's line-0 chips get no top padding (`TagChipRow.h:351`), and the body starts below the
    band (`TagChipGridActivity.cpp:62-66`). So Done's hit cannot overlap a chip's.
  - Horizontal growth is not asked for (batch §4: "grows the hit rect to the full header band
    (44 or 45 px)").
- **A7. An empty Done rect draws and registers nothing.**
  - `drawHeaderWithAction` returns height 0 when a detached strip fills the band
    (`HeaderLayoutTest.cpp:335-339`). No shipped theme does this: Lyra and Classic both have the
    battery on the right and attached (`LyraTheme.h:32-33`, `BaseTheme.h:154-155`).
  - In that case Home is still the commit (A4 of the batch).
- **A8. Done's input mask is `InputTouch` alone.** A long-press release asks `findTouch` for
  `InputLongPress`, and the touch fallback is disabled for that kind (`FreeInkUICore.h:1171-1176`,
  `:1246-1249`). So a long-press on Done matches nothing, by construction.
- **A9. Done checks `retireConfirmationOpen()` before it commits.**
  - While the popup is open, `OptionPopup::handleInput` returns true for every touch
    (`OptionPopup.h:72-109`). The base calls it before touch routing (`TagChipGridActivity.cpp:205`),
    so a Done tap never routes then.
  - The check is defence in depth, as `onMoreEvent` and the filter's `activateIndex` keep theirs
    (`HighlightsActivity.cpp:142`, `TagFilterActivity.cpp:30`).
- **A10. Done and Home share `commitAndFinish`, which clears the tap flash first.**
  - `UiListActivity.h:39-41` asks handlers that leave the screen to clear it.
  - From Home it is harmless: outside a dispatch, the next `render` or `route` resets
    `flashSuppressed_` (`FreeInkApp.h:688`, `:734`).
- **A11. `activateIndex` recomputes `pickerState` on every tap and Confirm (batch A3).**
  - **Disabled** posts the limit message and changes nothing.
    - It does not clear the flash. The flash paints the chip in its own Disabled look
      (`TagChipView.cpp:104-109`), so there is nothing to suppress.
    - The brief asks for the clear only on a toggle.
  - **Active and Inactive** toggle. They clear the flash during dispatch (`FreeInkApp.h:755-759`),
    so the next frame shows only the new state (batch B1). A removed tag therefore repaints white:
    Inactive is a white fill (`TagChipView.cpp:85-93`).
  - **Action** opens the keyboard, which clears the flash itself, as today.
  - **No `rebuildChips` on a toggle.** Labels carry names only, so the list does not change.
    Rebuilding would close routing (`TagChipGridActivity.cpp:48`) and drop taps until the next
    render, for no gain.
- **A12. The loop task reads `selectedIds_` without the lock.** It is the only writer:
  - the constructor, before the picker is current;
  - `activateIndex`, the create handler and `onTagRetired`, all loop task, all under `RenderLock`.
  - A reader on the writing task cannot see a torn value. This is btrial-4's A3 argument for
    `chips_`.
  - The render task reads it only inside `render()`, which holds the lock (`ActivityManager.cpp:52-59`).
- **A13. The create outcome is `pickerState(Kind::Tag, isSelected(id), size, 8)`.**
  - **Inactive** means below the cap and not applied: apply it.
  - **Disabled** means at the cap and not applied: post the limit message and leave it unapplied.
    This is the brief's "only if the returned id is not already selected".
  - **Active** means the name deduped to an applied tag: say nothing, because the user's intent is
    already met (batch §4).
  - One function decides both taps and creates, so they cannot disagree about the cap. Its tests
    cover 7/8, 8/8 and a deselect at 8/8 (`TagChipRowTest.cpp:609-626`).
  - Every create that returns an id ends in `rebuildChips(ChipKey{Tag, id})`, deduped names
    included, so the grid always shows the chip the user named.
- **A14. The picker no longer holds `RenderLock` around palette edits.**
  - Today it does (`TagPickerActivity.cpp:160-167`, `:264-277`), only because `buildScreen`
    borrowed `tags_[i].name.c_str()` on the render task (`:92`).
  - After this change, no render-task path reads the palette (batch A1):
    - `chipState` reads the chip and `selectedIds_`;
    - labels are copied into `ChipEntry::label` on the loop task (`TagChipView.h:17-21`);
    - `headerTitle` reads `selectedIds_`;
    - `drawHeaderWithAction` reads settings and the battery.
  - The base already retires without the lock on this argument (`TagChipGridActivity.cpp:196`).
- **A15. Labels come from `palette().name(id)`, a `const std::string&`** (`TagPalette.h:54`).
  - That avoids `StudyStore::tagName`'s by-value copy (`StudyStore.h:57`).
  - A name is at most 24 bytes (`TagPalette.h:39`), and the label holds 39 bytes plus NUL
    (`TagChipView.h:21`). `snprintf` never truncates a name.
  - `tr(STR_TAG_NEW_CHIP)` is "+ New tag" (`english.yaml:374`).
- **A16. `buildChips` reserves `ids.size() + 1`, where `ids` is `activeIdsByName()`.**
  - That equals the brief's `activeCount() + 1`, because `activeIdsByName` lists exactly the active
    entries (`TagPalette.cpp:98-105`).
  - It skips a second linear walk (`activeCount`, `:81-87`).
- **A17. The picker opens on page 1, with + New tag selected and no focus ring.**
  - The base's `onEnter` resets `nav` (`UiListActivity.cpp:22`).
  - `rebuildChips()` with an empty list carries no key, so `reselect` clamps 0 to 0
    (`TagChipGridActivity.cpp:25-33`, `TagChipRow.h:154-161`).
  - `buttonFocus_` starts false (`TagChipGridActivity.h:102`).
  - Opening on the page of the first applied tag is not asked for, and would differ by caller.
- **A18. Retire works on every tag chip, Disabled included.**
  - The base draws every chip with `InputTouch | InputLongPress` (`TagChipGridActivity.cpp:102-104`).
  - `draw` never sets `fui::StateDisabled` (`TagChipView.cpp:116-118`).
  - The base gates retire on `Kind::Tag` only (`:169`), so **+ New tag** cannot be retired.
- **A19. The held-Confirm retire is removed, not moved.**
  - It is unreachable on this board (Problem 7).
  - The base declares `handleButtons` `final` (`TagChipGridActivity.h:70`).
  - The filter grid retires by touch long-press alone, and so will the picker (batch §4).
  - Button users can still toggle (Confirm) and commit (Home). They cannot retire, as on the filter
    grid.
- **A20. `STR_TAG_NEW` goes with its only use.**
  - `scripts/gen_i18n.py` collects `STR_` names from `.cpp`, `.h` and `.c` files under `src` and
    `lib`, comments included (`:250-284`, `:831`).
  - Its only code use is `TagPickerActivity.cpp:100`. Its only definition is `english.yaml:373`.
    The other hits are markdown under `docs/` (research note, "Removals").
  - The implementer re-runs `grep -rnw STR_TAG_NEW src lib test` after the rewrite, and it must
    print nothing.
- **A21. The class comment is rewritten for the merged state.** Today's
  (`TagPickerActivity.h:15-45`) describes a per-book `HighlightDoc` palette, `removeTag` rollback
  and a held-Confirm delete, and stops mid-sentence at `:41`. The new comment keeps three things
  that still hold:
  - Home is the Done gesture.
  - Back cancels.
  - A created tag persists at once, independent of the passage.
- **A22. Both callers and `TagSelectionResult` compile unchanged.** The constructor keeps its
  signature (`TagPickerActivity.h:48-49`), and the result type is untouched.

## Testing strategy

### Host, written first

Each test fails to compile until its helper exists, then passes against it.

`test/tag_rows/TagChipRowTest.cpp` (target `TagRowsTest`), after the existing `reselect` tests,
with its `HOPE` / `MINISTRY` / `NAME` ids (`:19-21`):

- `TagChipSelectedInOrder`:
  - Keeps `order`'s order whatever order `selected` is in. `{3, 1}` against order `{1, 2, 3}`
    gives `{1, 3}`.
  - Drops ids that `order` lacks: a retired id, an unknown id, and `UNLABELLED`.
  - Emits each id once when `selected` repeats one.
  - Empty `selected` or empty `order` gives empty.
  - With 8 selected out of a 48-id order, all 8 come back in order (the cap case).
- `TagChipBandHitPadding`:
  - Lyra's numbers: band `{5, 44}`, chip `{9, 36}` gives top 4, bottom 4.
  - Classic's: band `{5, 45}`, chip `{25, 25}` gives top 20, bottom 0.
  - A chip filling the band gives 0 and 0.
  - A chip that pokes past the band clamps to 0 on that side.
  - Left and right are always 0.

`test/ui_layout/HeaderLayoutTest.cpp` (target `HeaderLayoutTest`), one test:

- `HeaderLayout.BandHitPaddingSpansTheBandForEveryLayout`, for each of `allLayouts(DONE_W)`
  (`:104-116`) whose action has a height:
  - The action grown by `bandHitPadding(band.y, band.height, action.y, action.height)` starts at
    `band.y` and is `band.height` tall.
  - This pins the brief's "commits on a tap anywhere in the header band's height" against the real
    geometry.

**What the host suite cannot reach.** No host target compiles an `Activity`, `UiAppHost` or
FreeInkUI (research note, "Testability"). The picker class is checked by the build and by the device
checks. Its decisions sit in tested pure functions:
- `pickerState` for taps, Confirm and the create outcome;
- `selectedInOrder` for open and commit;
- `bandHitPadding` for Done;
- `reselect` for landing on a created tag.

### Build and format

- **Host suite:** `cmake --build build/test && ctest --test-dir build/test -j 8`.
  - Baseline: 1745 of 1745 pass (research note).
  - Expected after: 1745 − 7 (`TagRows.*`) + the new tests, all passing.
  - `ctest -N | grep "TagRows\."` prints nothing.
- **Firmware:** `~/.platformio/penv/bin/pio run -e x4pro`.
  - No `warning:` line under `src/` or our own `lib/`. The baseline has none (research note).
  - Report the flash and RAM delta against RAM 66,228 B and Flash 5,563,766 B.
- **Format:** `./bin/clang-format-fix` over the whole tree leaves `git status` clean.
- **Removals:** `grep -rn -e TagRowMapping -e 'TagRows::' -e 'STR_TAG_NEW\b' src lib test` prints
  nothing.

### Device checks (owner)

On the owner's ~48-tag palette, from both callers: Highlights → a passage → Tags, and the reader's
passage selection → tag.

1. **Grid.**
   - **+ New tag** is first, then the tags alphabetically, with accented names among unaccented ones.
   - The grid pages, and the `x/y` strip shows.
   - Toggling a chip never moves another chip.
2. **Toggle.**
   - Applying a tag inverts it, and the title's count rises.
   - Removing one repaints it white, never inverted for a frame, and the count falls.
3. **Cap.**
   - At 8/8, every unapplied chip turns gray.
   - A tap on a gray chip, or Confirm on a focused one (step to it, then hold Right), shows "This
     highlight already has the maximum number of tags" and changes nothing.
   - Removing one tag turns them white again.
   - A long-press on a gray chip still offers retire.
4. **Create.**
   - + New tag with a new name creates it and applies it, on the page that holds it.
   - At 8/8, a new name creates it unapplied and shows the limit message.
   - The name of a tag already applied shows nothing.
   - The name of an existing unapplied tag, below 8, applies it.
5. **Done placement.**
   - Under Lyra and Classic, with the battery percentage on and off, Done clears the title and the
     battery.
   - A tap at the very top and the very bottom of the header band, in Done's columns, commits.
   - A long-press on Done does nothing.
   - Under Classic, check the label against the pill's bottom edge (A5).
6. **Exits.**
   - Done and Home commit; Back cancels.
   - With the retire popup open, Home does nothing and the popup stays.
   - A tap on Done with the popup open dismisses the popup without committing. This is the popup's
     outside-tap path.
7. **Order kept.**
   - Take a passage with two or more tags whose alphabetical order differs from the order they were
     created. Note its sleep-screen tag pill.
   - Open the picker, press Done with no change, and the pill is unchanged.
8. **Hidden tag.**
   - Retire a tag in another publication while it is on a passage of this one.
   - Opening the picker here shows a count that excludes it.
   - Done drops it from the passage. Back leaves the passage untouched.
9. **Retire.** Long-press a chip on page 2 → Delete. The chip goes, and if it was applied the count
   falls. The grid stays on page 2.
10. **Heap.** `ESP.getFreeHeap()` over serial is flat across a picker enter/exit cycle, and across a
    keyboard round trip.

## Baseline

Measured on this branch at `46b096bc`, before any change (research note):

- **Host:** `ctest --test-dir build/test -j 8` passes 1745 of 1745, 7 of them `TagRows.*`.
- **Firmware:** `pio run -e x4pro` gives RAM 66,228 / 327,680 B and Flash 5,563,766 / 6,553,600 B.
  The log has 248 `warning:` lines, all third-party, none under `src/` or our own `lib/`.
