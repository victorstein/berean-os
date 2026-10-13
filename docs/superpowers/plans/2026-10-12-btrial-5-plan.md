# Tag picker on the chip grid (btrial-5) Implementation Plan

> **For agentic workers:** Work through the tasks in order. Steps use checkbox (`- [ ]`) syntax for
> tracking. Commit at the end of each task. Never push, never open a PR, never run `hpipe`, `bd` or
> `bv`.

**Goal:** Rebuild `TagPickerActivity` as a subclass of `TagChipGridActivity`:
- **+ New tag** first, then the active tags alphabetically, as Active, Inactive or Disabled chips;
- a `Tags n/8` title and a header **Done** pill;
- the selection committed in palette allocation order;
- the old row list, its row mapping and its tests removed.

**Architecture:**
- **The base owns the grid.** `TagChipGridActivity` (already on this branch) owns the grid, paging,
  retire, Back, the Home guard and the render.
- **The picker fills six hooks.** `buildChips`, `chipState`, `buildHeaderExtras`, `activateIndex`,
  `onTagRetired` and `onHome`, plus `headerTitle` and `drawChrome` for the header.
  - The header comes from `BaseTheme::drawHeaderWithAction`, which returns where Done goes.
  - The picker draws Done there as a chip, with its own `ACTION_DONE`.
- **Two pure helpers in `TagChipRow.h` carry the logic the host suite can test.** `selectedInOrder`
  covers the open filter and the commit order. `bandHitPadding` covers Done's tap area.

**Tech Stack:**
- C++20 (GNU++20 on device), ESP32-S3 Arduino/PlatformIO, FreeInkUI, FreeRTOS.
- GoogleTest via CMake for the host suite.

**Spec:** `docs/superpowers/specs/2026-10-12-btrial-5-design.md`. Read its "Assumptions" (A1–A22)
before Task 3: they explain every non-obvious line below.

**Trial build.** While planning, every code block in this plan was applied to the tree at `db967bb4`,
checked, and reverted:
- `./bin/clang-format-fix` changed none of it. The blocks below are the formatted text.
- The host suite passed 1748 of 1748. `TagRowsTest` ran 83 tests before Task 4 and `HeaderLayoutTest`
  ran 17.
- `pio run -e x4pro` succeeded:
  - RAM 66,228 B (unchanged);
  - Flash 5,560,938 B, 2,828 B less than the baseline 5,563,766;
  - no warning in `src/` or our `lib/`.
- `git grep -nw -e STR_TAG_NEW -e TagRowMapping -e 'TagRows::' -- src lib test` printed nothing.

If a step does not compile as written, the file it edits has changed since; see "Re-read every
file" below.

FILES: src/activities/reader/TagChipRow.h, test/tag_rows/TagChipRowTest.cpp, test/ui_layout/HeaderLayoutTest.cpp
FILES: src/activities/reader/TagPickerActivity.h, src/activities/reader/TagPickerActivity.cpp
FILES: lib/I18n/translations/english.yaml
FILES: src/activities/reader/TagRowMapping.h, test/tag_rows/TagRowsTest.cpp, test/tag_rows/CMakeLists.txt

---

## Ground rules for this repo

- **Where to work.** Use `/Volumes/stein/.herdr/worktrees/berean-os/feature-tag-picker-grid`, on
  branch `feature/tag-picker-grid`. All paths below are relative to it.
- **Re-read every file before you edit it.** A sibling task may have changed it since this plan was
  written. If an "old" block or an anchor below does not match the file, stop. Report the mismatch;
  do not adapt the plan silently.
- **Comments.**
  - Write them only for a non-obvious *why*. Do not add comments that restate the code.
  - The comments in the code blocks below are the ones to keep.
- **PlatformIO.**
  - `pio` is not on `PATH` in this shell. Use `~/.platformio/penv/bin/pio`.
  - Never run two `pio` builds at once: they share `~/.platformio`.
- **Formatting.**
  - Format with `./bin/clang-format-fix`, over the whole tree, as CI does. It prints nothing on
    success.
  - Never call `clang-format` directly.
- **One firmware build.** Per the repo's `CLAUDE.md`, the firmware is built **once, after the last
  firmware code edit** (Task 3, Step 7).
  - Tasks 1–2 are verified by host tests.
  - Task 4 deletes a header nothing includes any more, and edits tests only, so it needs no firmware
    build.
- **Generated files.**
  - `lib/I18n/I18nKeys.h`, `I18nStrings.h` and `I18nStrings.cpp` are generated and gitignored.
  - Never edit them. The `pio run` pre-step regenerates them from `english.yaml`.
- **Commits.**
  - Messages are conventional (`feat:`, `test:`, `refactor:`, `style:`).
  - Each ends with the trailer line
    `Claude-Session: https://claude.ai/code/session_01SUtwSmzjTfcpChiTfp6iXD`.
  - Stage by path, never `git add -A`.

## File map

| File | Responsibility after this change |
|---|---|
| `src/activities/reader/TagChipRow.h` | Pure chip logic. Gains `selectedInOrder` and `bandHitPadding`. |
| `test/tag_rows/TagChipRowTest.cpp` | Host tests for `TagChipRow.h`. Gains `TagChipSelectedInOrder.*` (5) and `TagChipBandHitPadding.*` (4). |
| `test/ui_layout/HeaderLayoutTest.cpp` | Host tests for the header geometry. Gains `HeaderLayout.BandHitPaddingSpansTheBandForEveryLayout`. |
| `src/activities/reader/TagPickerActivity.h` / `.cpp` | The picker as a `TagChipGridActivity` subclass. Rewritten whole. |
| `lib/I18n/translations/english.yaml` | Loses `STR_TAG_NEW`. |
| `src/activities/reader/TagRowMapping.h` | Deleted. |
| `test/tag_rows/TagRowsTest.cpp` | Deleted. |
| `test/tag_rows/CMakeLists.txt` | Loses the `TagRowsTest.cpp` source line. The `TagRowsTest` target stays. |

---

### Task 0: Check the starting point

**Files:** none changed.

- [ ] **Step 1: Confirm branch and a clean tree**

Run:
```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/feature-tag-picker-grid
git branch --show-current
git status --short
```
Expected: `feature/tag-picker-grid`, then no status lines.

- [ ] **Step 2: Confirm the host baseline**

Run:
```bash
cmake -S test -B build/test >/dev/null
cmake --build build/test --target TagRowsTest HeaderLayoutTest -j8 2>&1 | grep -E "error|Built target (TagRowsTest|HeaderLayoutTest)"
./build/test/tag_rows/TagRowsTest --gtest_brief=1 2>&1 | tail -1
./build/test/ui_layout/HeaderLayoutTest --gtest_brief=1 2>&1 | tail -1
```
Expected: `[100%] Built target TagRowsTest` and `[100%] Built target HeaderLayoutTest`, no `error`,
then `[  PASSED  ] 74 tests.` and `[  PASSED  ] 16 tests.`

- [ ] **Step 3: Confirm the anchors this plan edits still exist**

Run:
```bash
grep -n "^int reselect\|^inline Pad hitPadding(const Layout" src/activities/reader/TagChipRow.h
grep -n "TEST(TagChipFilterKey, AZeroCountFilterIsSelectedAtItsNameOrderSlot)" test/tag_rows/TagChipRowTest.cpp
grep -n "TEST(HeaderLayout, ActionMovesWithTheBatteryPercentOnTheTitleLine)" test/ui_layout/HeaderLayoutTest.cpp
grep -n "class TagPickerActivity final : public UiListActivity" src/activities/reader/TagPickerActivity.h
grep -n "virtual void buildHeaderExtras\|void rebuildChips(TagChips::ChipKey select);\|bool retireConfirmationOpen" src/activities/reader/TagChipGridActivity.h
grep -n "Rect drawHeaderWithAction" src/components/themes/BaseTheme.h
grep -n '^STR_TAG_NEW: \|^STR_TAG_NEW_CHIP: ' lib/I18n/translations/english.yaml
grep -n "TagRowsTest.cpp" test/tag_rows/CMakeLists.txt
```
Expected. The line numbers may differ; the text must not:
- `154:int reselect(...` and `356:inline Pad hitPadding(const Layout& layout, ...`
- `745:TEST(TagChipFilterKey, AZeroCountFilterIsSelectedAtItsNameOrderSlot) {`, the **last** test in
  that file.
- `341:TEST(HeaderLayout, ActionMovesWithTheBatteryPercentOnTheTitleLine) {`, the **last** test in
  that file.
- `46:class TagPickerActivity final : public UiListActivity {`
- `47:  virtual void buildHeaderExtras(UiScreen&) {}`, `78:  void rebuildChips(TagChips::ChipKey select);`,
  `81:  bool retireConfirmationOpen() const ...`
- `232:  Rect drawHeaderWithAction(const GfxRenderer& renderer, Rect rect, const char* title, const char* actionLabel) const;`
- `373:STR_TAG_NEW: "New tag..."` and `374:STR_TAG_NEW_CHIP: "+ New tag"`
- `2:  TagRowsTest.cpp`

If any line is missing, stop and report.

---

### Task 1: `selectedInOrder`

The open filter (spec A1) and the commit order (spec Problem 2, A1) both come from this one pure
function.

**Files:**
- Modify: `src/activities/reader/TagChipRow.h` (after `reselect`, around `:153-161`)
- Test: `test/tag_rows/TagChipRowTest.cpp` (append at the end)

- [ ] **Step 1: Write the failing tests**

Append this block to the **end** of `test/tag_rows/TagChipRowTest.cpp`, after the closing `}` of
`TEST(TagChipFilterKey, AZeroCountFilterIsSelectedAtItsNameOrderSlot)`. The file already defines
`HOPE` (1), `MINISTRY` (2), `NAME` (3), `CAP` (8), `toTagId` and `UNLABELLED`, and includes
`<vector>` and `<cstdint>`.

```cpp

namespace {

std::vector<uint16_t> rawIdsOf(const std::vector<TagId>& ids) {
  std::vector<uint16_t> out;
  out.reserve(ids.size());
  for (const TagId id : ids) out.push_back(study::toRaw(id));
  return out;
}

}  // namespace

TEST(TagChipSelectedInOrder, FollowsTheOrderNotTheSelection) {
  const std::vector<TagId> allocation{HOPE, MINISTRY, NAME};
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(allocation, {NAME, HOPE})), (std::vector<uint16_t>{1, 3}));
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(allocation, {HOPE, NAME})), (std::vector<uint16_t>{1, 3}));
}

TEST(TagChipSelectedInOrder, DropsIdsTheOrderDoesNotList) {
  const std::vector<TagId> active{HOPE, NAME};  // MINISTRY retired
  const TagId unknown = toTagId(99);
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(active, {MINISTRY, NAME, unknown, UNLABELLED})),
            (std::vector<uint16_t>{3}));
}

TEST(TagChipSelectedInOrder, EmitsEachIdOnce) {
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder({HOPE, NAME}, {NAME, HOPE, NAME})), (std::vector<uint16_t>{1, 3}));
}

TEST(TagChipSelectedInOrder, AnEmptySelectionOrOrderGivesNothing) {
  EXPECT_TRUE(TagChips::selectedInOrder({HOPE, NAME}, {}).empty());
  EXPECT_TRUE(TagChips::selectedInOrder({}, {HOPE, NAME}).empty());
}

TEST(TagChipSelectedInOrder, AFullSelectionComesBackInPaletteOrder) {
  std::vector<TagId> palette;
  palette.reserve(48);
  for (uint16_t raw = 1; raw <= 48; ++raw) palette.push_back(toTagId(raw));
  const std::vector<TagId> picked{toTagId(40), toTagId(3),  toTagId(17), toTagId(48),
                                  toTagId(1),  toTagId(25), toTagId(9),  toTagId(33)};
  ASSERT_EQ(picked.size(), CAP);
  EXPECT_EQ(rawIdsOf(TagChips::selectedInOrder(palette, picked)), (std::vector<uint16_t>{1, 3, 9, 17, 25, 33, 40, 48}));
}
```

- [ ] **Step 2: Run them to verify they fail**

Run:
```bash
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -m1 "error:"
```
Expected: one line containing `error: no member named 'selectedInOrder' in namespace 'TagChips'`.

- [ ] **Step 3: Write the helper**

In `src/activities/reader/TagChipRow.h`, replace:

```cpp
  const int last = static_cast<int>(chips.size()) - 1;
  return std::clamp(previous, 0, std::max(last, 0));
}

struct Placed {
```

with:

```cpp
  const int last = static_cast<int>(chips.size()) - 1;
  return std::clamp(previous, 0, std::max(last, 0));
}

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

struct Placed {
```

`<algorithm>` and `<vector>` are already included at the top of the header.

- [ ] **Step 4: Run them to verify they pass**

Run:
```bash
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -E "error|Built target TagRowsTest"
./build/test/tag_rows/TagRowsTest --gtest_brief=1 --gtest_filter='TagChipSelectedInOrder.*' 2>&1 | tail -1
./build/test/tag_rows/TagRowsTest --gtest_brief=1 2>&1 | tail -1
```
Expected: `[100%] Built target TagRowsTest` with no `error`, then `[  PASSED  ] 5 tests.`, then
`[  PASSED  ] 79 tests.`

- [ ] **Step 5: Commit**

```bash
git add src/activities/reader/TagChipRow.h test/tag_rows/TagChipRowTest.cpp
git commit -m "feat: keep a tag selection in palette order

selectedInOrder returns the ids of an order that a selection holds, in
that order and each once. The picker uses it with the palette's
allocation order to drop inactive ids at open and to commit in the
stored order.

Claude-Session: https://claude.ai/code/session_01SUtwSmzjTfcpChiTfp6iXD"
```

---

### Task 2: `bandHitPadding`

This grows Done's tap area to the full header band (spec A6). The HeaderLayout test pins that
against the real header geometry for every layout.

**Files:**
- Modify: `src/activities/reader/TagChipRow.h` (after the `Layout` overload of `hitPadding`, around
  `:356-358`)
- Test: `test/tag_rows/TagChipRowTest.cpp` (append at the end)
- Test: `test/ui_layout/HeaderLayoutTest.cpp` (append at the end)

- [ ] **Step 1: Write the failing tests**

Append this block to the **end** of `test/tag_rows/TagChipRowTest.cpp`, after
`TEST(TagChipSelectedInOrder, AFullSelectionComesBackInPaletteOrder)` from Task 1:

```cpp

TEST(TagChipBandHitPadding, LyraPillGrowsFourPixelsEachWay) {
  // HeaderLayout under Lyra: a band at y 5, 44 tall; the action at y 9, 36 tall.
  const TagChips::Pad pad = TagChips::bandHitPadding(5, 44, 9, 36);
  EXPECT_EQ(pad.top, 4);
  EXPECT_EQ(pad.bottom, 4);
  EXPECT_EQ(pad.left, 0);
  EXPECT_EQ(pad.right, 0);
}

TEST(TagChipBandHitPadding, ClassicCornerPillGrowsUpOverTheStrip) {
  // Classic: a band at y 5, 45 tall; the action below the 20 px battery strip, at y 25, 25 tall.
  const TagChips::Pad pad = TagChips::bandHitPadding(5, 45, 25, 25);
  EXPECT_EQ(pad.top, 20);
  EXPECT_EQ(pad.bottom, 0);
  EXPECT_EQ(pad.left, 0);
  EXPECT_EQ(pad.right, 0);
}

TEST(TagChipBandHitPadding, AChipFillingTheBandNeedsNone) {
  const TagChips::Pad pad = TagChips::bandHitPadding(5, 44, 5, 44);
  EXPECT_EQ(pad.top, 0);
  EXPECT_EQ(pad.bottom, 0);
}

TEST(TagChipBandHitPadding, AChipPastTheBandIsNeverPaddedNegative) {
  const TagChips::Pad above = TagChips::bandHitPadding(5, 44, 2, 30);
  EXPECT_EQ(above.top, 0);
  EXPECT_EQ(above.bottom, 17);
  const TagChips::Pad below = TagChips::bandHitPadding(5, 44, 30, 30);
  EXPECT_EQ(below.top, 25);
  EXPECT_EQ(below.bottom, 0);
}
```

Append this block to the **end** of `test/ui_layout/HeaderLayoutTest.cpp`, after
`TEST(HeaderLayout, ActionMovesWithTheBatteryPercentOnTheTitleLine)`. That file already includes
`activities/reader/TagChipRow.h` and defines `allLayouts`, `Inputs`, `Result` and `DONE_W`.

```cpp

// The picker grows its Done pill to the header band with bandHitPadding, so a tap anywhere in the
// band's height lands on it, under every layout that leaves the action any room.
TEST(HeaderLayout, BandHitPaddingSpansTheBandForEveryLayout) {
  const std::vector<Inputs> layouts = allLayouts(DONE_W);
  int checked = 0;
  for (size_t i = 0; i < layouts.size(); ++i) {
    SCOPED_TRACE(i);
    const Inputs& in = layouts[i];
    const Result r = HeaderLayout::layout(in);
    if (r.action.height <= 0) continue;
    const TagChips::Pad pad = TagChips::bandHitPadding(in.band.y, in.band.height, r.action.y, r.action.height);
    EXPECT_EQ(r.action.y - pad.top, in.band.y);
    EXPECT_EQ(r.action.height + pad.top + pad.bottom, in.band.height);
    ++checked;
  }
  EXPECT_EQ(checked, 14) << "every layout but Lyra's detached right strip, which fills the band";
}
```

- [ ] **Step 2: Run them to verify they fail**

Run:
```bash
cmake --build build/test --target TagRowsTest HeaderLayoutTest -j8 2>&1 | grep "error:" | sort -u | head -3
```
Expected: lines containing `error: no member named 'bandHitPadding' in namespace 'TagChips'`, from
`TagChipRowTest.cpp` and `HeaderLayoutTest.cpp`.

- [ ] **Step 3: Write the helper**

In `src/activities/reader/TagChipRow.h`, replace:

```cpp
inline Pad hitPadding(const Layout& layout, const int index, const int gap) {
  return hitPadding(layout.placed, layout.placedCount, layout.lines, index, gap);
}
```

with:

```cpp
inline Pad hitPadding(const Layout& layout, const int index, const int gap) {
  return hitPadding(layout.placed, layout.placedCount, layout.lines, index, gap);
}

// Grows a chip drawn shorter than its band to the band's full height, as the header's Done pill is.
inline Pad bandHitPadding(const int bandTop, const int bandHeight, const int chipTop, const int chipHeight) {
  Pad pad;
  pad.top = std::max(0, chipTop - bandTop);
  pad.bottom = std::max(0, bandTop + bandHeight - (chipTop + chipHeight));
  return pad;
}
```

- [ ] **Step 4: Run them to verify they pass**

Run:
```bash
cmake --build build/test --target TagRowsTest HeaderLayoutTest -j8 2>&1 | grep -E "error|Built target (TagRowsTest|HeaderLayoutTest)"
./build/test/tag_rows/TagRowsTest --gtest_brief=1 2>&1 | tail -1
./build/test/ui_layout/HeaderLayoutTest --gtest_brief=1 2>&1 | tail -1
```
Expected: both `[100%] Built target ...` lines with no `error`, then `[  PASSED  ] 83 tests.` and
`[  PASSED  ] 17 tests.`

- [ ] **Step 5: Commit**

```bash
git add src/activities/reader/TagChipRow.h test/tag_rows/TagChipRowTest.cpp test/ui_layout/HeaderLayoutTest.cpp
git commit -m "feat: grow a chip's hit rect to its band

bandHitPadding pads a chip drawn shorter than its band to the band's
full height. The tag picker uses it for its header Done pill; a
HeaderLayout test checks it against every header layout.

Claude-Session: https://claude.ai/code/session_01SUtwSmzjTfcpChiTfp6iXD"
```

---

### Task 3: The picker on the chip grid, then the firmware build

No host target compiles an activity, so this task is checked by the firmware build (Step 7). Its
logic is already pinned by tests: `pickerState` (existing), `selectedInOrder` and `bandHitPadding`
(Tasks 1-2), and `reselect` (existing).

**Files:**
- Rewrite: `src/activities/reader/TagPickerActivity.h`
- Rewrite: `src/activities/reader/TagPickerActivity.cpp`
- Modify: `lib/I18n/translations/english.yaml:373`

- [ ] **Step 1: Read what you are replacing**

Read `src/activities/reader/TagPickerActivity.h` and `.cpp` in full. Confirm:
- The class derives `UiListActivity` and holds `tags_`, `rowItems_` and `confirmPopup_`.
- `isSelected` and `setSelected` (`.cpp:38-49`) and `reportAddTagFailure` (`:183-198`) match the
  versions in Step 3. Their bodies are carried over; only `reportAddTagFailure`'s comment changes
  (spec, "TagPickerActivity.cpp, by member").

If either file already derives `TagChipGridActivity`, stop and report.

- [ ] **Step 2: Replace `TagPickerActivity.h` with exactly this**

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

Why it looks like this (spec section in brackets):
- **No overrides of the base's sealed members.** `TagChipGridActivity` declares `listCount`,
  `buildScreen`, `drawFooter`, `onBackButton`, `onRowAction`, `onRowLongPress`,
  `handleCustomInput`, `handleButtons`, `navigateButtons`, `handleHomeGesture` and `render`
  `final`. Overriding any of them does not compile. The held-Confirm retire, the picker's own
  popup and its render copy go with them (A19).
- **`mutable title_`** is written by `headerTitle() const` on the render task only (A3).
- **`components/themes/BaseTheme.h`** is for `Rect`, the type `drawHeaderWithAction` returns.
- **Dropped includes are safe.** The old header's `PassageDoc.h`, `OptionPopup.h` and
  `study/StudyStore.h` includes are gone. Both callers include what they use (the trial build
  passed).

- [ ] **Step 3: Replace `TagPickerActivity.cpp` with exactly this**

```cpp
#include "TagPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <StudyStore/PassageDoc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include "../util/KeyboardEntryActivity.h"
#include "ReaderUtils.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

TagPickerActivity::TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::vector<study::TagId> initialSelection)
    : TagChipGridActivity("TagPicker", renderer, mappedInput),
      selectedIds_(TagChips::selectedInOrder(STUDY.palette().activeIds(), initialSelection)) {}

void TagPickerActivity::onEnter() {
  TagChipGridActivity::onEnter();
  app.on(ACTION_DONE, &TagPickerActivity::onDoneEvent, this);
  rebuildChips();
}

bool TagPickerActivity::isSelected(const study::TagId id) const {
  return std::find(selectedIds_.begin(), selectedIds_.end(), id) != selectedIds_.end();
}

void TagPickerActivity::setSelected(const study::TagId id, const bool on) {
  const auto it = std::find(selectedIds_.begin(), selectedIds_.end(), id);
  if (on && it == selectedIds_.end()) {
    selectedIds_.push_back(id);
  } else if (!on && it != selectedIds_.end()) {
    selectedIds_.erase(it);
  }
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
  const fui::Rect rect{static_cast<int16_t>(doneRect_.x), static_cast<int16_t>(doneRect_.y),
                       static_cast<int16_t>(doneRect_.width), static_cast<int16_t>(doneRect_.height)};
  TagChipView::draw(screen, rect, tr(STR_DONE), TagChips::ChipState::Action, ACTION_DONE, 0, fui::InputTouch,
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
  // A toggle's feedback is its own repaint; a flash must never draw over it.
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

void TagPickerActivity::reportAddTagFailure(const std::string& name) {
  // Mirrors TagPalette::add's precedence, name checks before the full palette, so it needs no
  // failure code from addTag to say something true. The too-long branch is normally unreachable:
  // KeyboardEntryActivity's maxLength already blocks typing past MAX_TAG_NAME_BYTES.
  if (name.empty()) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_NAME_EMPTY));
  } else if (name.size() > study::TagPalette::MAX_TAG_NAME_BYTES) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_NAME_TOO_LONG));
  } else {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_PALETTE_FULL));
  }
  requestUpdate();
}

void TagPickerActivity::onTagRetired(const study::TagId id) {
  RenderLock lock(*this);
  setSelected(id, false);
}

void TagPickerActivity::onHome() { commitAndFinish(); }

void TagPickerActivity::onDoneEvent(const fui::ActionEvent&, void* user) {
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

What each piece carries (spec assumption in brackets):
- **Constructor.** It keeps only active ids, in allocation order (A1, A2).
- **`onEnter`.** It registers Done after the base registers `ACTION_ROW`, then builds the chips. The
  picker opens on page 1 with + New tag selected (A17).
- **`headerTitle` and `drawChrome`.**
  - Both run on the render task, `drawChrome` first.
  - The band is `UiListActivity::drawChrome`'s rect (A3, A4).
- **`buildChips`.** It uses `palette().name(id)` (a reference, no copy) and reserves
  `activeIdsByName().size() + 1` (A15, A16).
- **`buildHeaderExtras`.**
  - Done is an Action chip at the theme's rect, never inset (A5).
  - Its hit is grown to the band (A6).
  - An empty rect draws nothing (A7).
  - `InputTouch` only, so a long-press cannot commit (A8).
- **`activateIndex`.**
  - It recomputes `pickerState` for taps and Confirm alike (A11).
  - Disabled posts the limit message and changes nothing.
  - A toggle clears the flash, flips the id under `RenderLock`, and does **not** rebuild the chips.
    A rebuild would close touch routing for nothing (A11, A12).
- **The create handler.**
  - The outcome is `pickerState(Kind::Tag, ...)`: apply, limit message, or nothing for a deduped
    applied name (A13).
  - Every create that returns an id ends in `rebuildChips(ChipKey{Tag, id})`, so the grid lands on
    the chip.
  - No `RenderLock` around `addTagName`: no render-task path reads the palette any more (A14).
- **`onTagRetired`.** It deselects under `RenderLock` (A18).
- **`onDoneEvent` and `onHome`.**
  - Both commit through `commitAndFinish`, which clears the flash and commits in allocation order
    (A9, A10, A1).
  - The popup guard is defence in depth. The open popup already consumes every touch.

- [ ] **Step 4: Remove `STR_TAG_NEW` from `english.yaml`**

In `lib/I18n/translations/english.yaml`, delete exactly this line (`:373`), and nothing else:

```yaml
STR_TAG_NEW: "New tag..."
```

The next line, `STR_TAG_NEW_CHIP: "+ New tag"`, stays. No other translation defines `STR_TAG_NEW`.
Do not touch `lib/I18n/I18nKeys.h` or `I18nStrings.*`: they are generated. Step 7 regenerates them
(spec A20).

- [ ] **Step 5: Confirm the string and the old row code are gone from the firmware sources**

Run:
```bash
git grep -nw -e STR_TAG_NEW -e TagRowMapping -e 'TagRows::' -- src lib
grep -n "rowItems_\|tags_\|tagIndexForRow\|ENTER_DELETE_MODE_MS" src/activities/reader/TagPickerActivity.h src/activities/reader/TagPickerActivity.cpp
```
Expected: no output from either.
- `src/activities/reader/TagRowMapping.h` still exists until Task 4, but it contains neither pattern.
  It declares `namespace TagRows`, which `TagRows::` does not match.
- `test/` is left out here because `TagRowsTest.cpp` still uses the mapping until Task 4.

- [ ] **Step 6: Format**

Run:
```bash
./bin/clang-format-fix
git status --short
```
Expected: ` M` for `lib/I18n/translations/english.yaml`,
`src/activities/reader/TagPickerActivity.cpp` and `src/activities/reader/TagPickerActivity.h`, and
nothing else. If the formatter changed another file, restore it with `git checkout -- <path>` and
report it.

- [ ] **Step 7: Build the firmware (the one build after the last firmware code edit)**

Run:
```bash
~/.platformio/penv/bin/pio run -e x4pro > build/btrial-5-pio.log 2>&1; echo "exit=$?"
grep -E "^(RAM|Flash):" build/btrial-5-pio.log | tail -2
grep "warning:" build/btrial-5-pio.log | grep -E "^(src|lib)/" ; echo "repo-warnings-done"
grep -n "Compiling .*TagPickerActivity.cpp.o" build/btrial-5-pio.log
ls -l .pio/build/x4pro/firmware.bin
```
Expected:
- `exit=0`.
- The RAM and Flash lines.
  - Baseline: RAM 66,228 B, Flash 5,563,766 B (spec "Baseline").
  - The planning trial gave RAM 66,228 B (unchanged) and Flash 5,560,938 B (−2,828 B).
- `repo-warnings-done` with no warning line before it. The baseline has none in `src/` or our
  `lib/`. A `.pio/libdeps/...WebSockets...` deprecation warning is third-party and was in the
  baseline.
- One `Compiling .pio/build/x4pro/src/activities/reader/TagPickerActivity.cpp.o` line, proving the
  rewrite was compiled.
- `firmware.bin` present.

If the build fails:
1. Read the first `error:` line in `build/btrial-5-pio.log`.
2. Fix it in the file it names, which must be one of this task's two files.
3. Re-run this step.
4. Say what you fixed in the commit body.

`build/` is gitignored, so the log is never staged.

- [ ] **Step 8: Commit**

```bash
git add src/activities/reader/TagPickerActivity.h src/activities/reader/TagPickerActivity.cpp lib/I18n/translations/english.yaml
git commit -m "feat: put the tag picker on the chip grid

TagPickerActivity is now a TagChipGridActivity: + New tag, then the
active tags by name, each Active, Inactive or Disabled at 8/8. The
header reads Tags n/8 with a Done pill placed by the theme; Done and
Home commit, Back cancels.

The selection commits in palette allocation order again (the batch had
moved it to name order through activeTags()), inactive ids are dropped
at open, toggles write under RenderLock and show no tap flash, and a
create lands the grid on the new tag. The row list, its held-Confirm
retire and STR_TAG_NEW are gone.

Claude-Session: https://claude.ai/code/session_01SUtwSmzjTfcpChiTfp6iXD"
```

---

### Task 4: Remove the row mapping and its tests

**Files:**
- Delete: `src/activities/reader/TagRowMapping.h`
- Delete: `test/tag_rows/TagRowsTest.cpp`
- Modify: `test/tag_rows/CMakeLists.txt:2`

- [ ] **Step 1: Confirm nothing but the doomed test still uses the mapping**

Run:
```bash
git grep -n "TagRowMapping\|TagRows::" -- src lib test
```
Expected: only `test/tag_rows/TagRowsTest.cpp` lines, and no `src/` line. If a `src/` line appears,
Task 3 is incomplete: stop and report.

- [ ] **Step 2: Delete the header and the test file**

Run:
```bash
git rm -q src/activities/reader/TagRowMapping.h test/tag_rows/TagRowsTest.cpp
```

- [ ] **Step 3: Drop the source line from the test target**

In `test/tag_rows/CMakeLists.txt`, replace:

```cmake
add_executable(TagRowsTest
  TagRowsTest.cpp
  PassageActionsTest.cpp
  TagChipRowTest.cpp
)
```

with:

```cmake
add_executable(TagRowsTest
  PassageActionsTest.cpp
  TagChipRowTest.cpp
)
```

The target keeps its name, `TagRowsTest`. Do not touch the shared `test/CMakeLists.txt`: its
`add_subdirectory(tag_rows)` line stays.

- [ ] **Step 4: Reconfigure, build and run the target**

Run:
```bash
cmake -S test -B build/test >/dev/null
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -E "error|Built target TagRowsTest"
./build/test/tag_rows/TagRowsTest --gtest_brief=1 2>&1 | tail -1
./build/test/tag_rows/TagRowsTest --gtest_list_tests 2>&1 | grep -c "^TagRows\."
```
Expected:
- `[100%] Built target TagRowsTest` with no `error`.
- `[  PASSED  ] 76 tests.` That is 83 minus the 7 `TagRows.*` tests.
- `0`.

- [ ] **Step 5: Confirm the removals**

Run:
```bash
git grep -nw -e STR_TAG_NEW -e TagRowMapping -e 'TagRows::' -- src lib test
```
Expected: no output.
- `git grep` searches tracked files only. The generated `lib/I18n/I18nKeys.h` is not tracked, so it
  cannot match.
- If a plain `grep -r` ever shows `STR_TAG_NEW` in it, that file is stale until the next `pio run`.
  Never hand-edit it.

- [ ] **Step 6: Commit**

```bash
git add test/tag_rows/CMakeLists.txt
git commit -m "refactor: drop the tag picker's row mapping

The chip grid carries each chip's kind and id, so the Done/tag/New tag
row offset in TagRowMapping.h and the tests that pinned it have no
user.

Claude-Session: https://claude.ai/code/session_01SUtwSmzjTfcpChiTfp6iXD"
```

`git rm` already staged the two deletions, so they are in this commit.

---

### Task 5: Full verification

**Files:** none changed unless a check fails.

- [ ] **Step 1: The full host suite**

Run:
```bash
cmake -S test -B build/test >/dev/null
cmake --build build/test -j8 2>&1 | grep -E "error" ; ctest --test-dir build/test -j8 2>&1 | tail -3
ctest --test-dir build/test -N | grep -c "TagRows\."
```
Expected:
- No `error` lines.
- `100% tests passed out of 1748`. That is the 1745 baseline plus 10 new tests, minus 7:
  - +5 `TagChipSelectedInOrder.*`;
  - +4 `TagChipBandHitPadding.*`;
  - +1 `HeaderLayout.BandHitPaddingSpansTheBandForEveryLayout`;
  - −7 `TagRows.*`.
- Then `0`.

- [ ] **Step 2: Format over the whole tree, as CI runs it**

Run:
```bash
./bin/clang-format-fix
git status --short
```
Expected: no output from either.
- If the formatter changed a file on a `FILES:` line, stage it by path. Commit it as
  `style: format the tag picker`, with the session trailer. It needs no rebuild.
- If `git status --short` lists any other path, do not stage it. Restore it with
  `git checkout -- <path>`, and report it.

- [ ] **Step 3: Confirm the commit series**

Run:
```bash
git log --oneline -5
```
Expected, newest first, the four task commits below. A `style:` commit from Step 2 may sit on top,
and the fifth line is whatever preceded Task 1:
- `refactor: drop the tag picker's row mapping`
- `feat: put the tag picker on the chip grid`
- `feat: grow a chip's hit rect to its band`
- `feat: keep a tag selection in palette order`

- [ ] **Step 4: Report**

Report back with:
- the `ctest` tail;
- the RAM and Flash lines from Task 3 Step 7, with their deltas against 66,228 and 5,563,766;
- confirmation that `src/` and `lib/` have no warnings;
- the `git log` list.

Then list, unchanged, the device checks from the spec ("Device checks (owner)", items 1–10). They
need the owner and hardware, and this plan does not run them.

Do not run `hpipe`. The worker who verifies this work files the follow-up discovery the spec names
(Non-goals):
- `StudyStore::activeTags()`, now without a caller;
- the stale comments at `HighlightsActivity.h:30`, `:72-75` and `TagPalette.h:19-23`.

---

## Self-review against the spec

| Spec item | Where |
|---|---|
| Problem 1, Goal: chip grid, + New tag first, alphabetical, three states | Task 3 Steps 2-3 (`buildChips`, `chipState`) |
| Problem 2, A1, A2: allocation-order commit; open filter | Task 1; Task 3 (constructor, `commitAndFinish`) |
| Problem 3, A12: selection written under `RenderLock` | Task 3 (`activateIndex`, create handler, `onTagRetired`) |
| Problem 4, A11: no flash on a toggle | Task 3 (`activateIndex`) |
| Problem 5, A13, D7: Disabled tap message; create at the cap | Task 3 (`activateIndex`, create handler) |
| Problem 6, D8: inactive ids dropped at open | Task 3 constructor, via Task 1 |
| Problem 7, A19-A20: row mapping, held-Confirm retire, own render, `STR_TAG_NEW` removed | Task 3 Steps 2-4; Task 4 |
| A3, A4: title formatted in `headerTitle() const`; band from `UiListActivity` | Task 3 (`headerTitle`, `drawChrome`) |
| A5-A8: Done as an Action chip, band hit, empty rect, touch only | Task 2; Task 3 (`buildHeaderExtras`) |
| A9, A10: Done guard; shared commit clears the flash | Task 3 (`onDoneEvent`, `commitAndFinish`) |
| A14: no lock around palette edits | Task 3 (create handler) |
| A15-A17: labels by reference, reserve, entry selection | Task 3 (`buildChips`, `onEnter`) |
| A21: class comment rewritten | Task 3 Step 2 |
| A22: callers and `TagSelectionResult` unchanged | No task touches them; Task 3 Step 7 compiles both callers |
| Review 0 m3: `reportAddTagFailure` comment reworded | Task 3 Step 3 |
| Review 0 m2: `git grep` for removals | Task 3 Step 5; Task 4 Step 5 |
| Testing strategy: host tests first; 1748; one firmware build; format | Tasks 1-2; Task 3 Step 7; Task 5 |
| Device checks 1-10 | Reported unchanged in Task 5 Step 4 |

One divergence from the spec's sketch: the toggle's comment now reads "A toggle's feedback is its
own repaint; a flash must never draw over it". The spec's sketch said the flash would repaint "the
state it is leaving", but `TagChipView::draw` paints a flash in the chip's own, new look
(`TagChipView.cpp:104-109`). The spec was corrected in the same commit as this plan.
