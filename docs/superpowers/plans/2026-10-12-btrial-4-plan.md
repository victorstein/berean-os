# Tag chip grid base (btrial-4) Implementation Plan

> **For agentic workers:** Work through the tasks in order. Steps use checkbox (`- [ ]`) syntax for
> tracking. Commit at the end of each task. Never push, never open a PR, never run `hpipe`, `bd` or
> `bv`.

**Goal:** Move the filter grid's body into a new base, `TagChipGridActivity`, with six subclass
hooks, so the tag picker can become its second subclass. `TagFilterActivity` becomes a thin first
subclass. The filter grid and the Highlights chip row list tags alphabetically.

**Architecture:** `TagChipGridActivity` is a `UiListActivity` that owns the chip list, paging, the
focus ring, swipe and button navigation, long-press retire with its popup, and the render. Subclasses
supply chips, chip states, header extras, activation, the retire follow-up and Home through virtual
hooks with a documented lock contract. Three pure helpers in `TagChipRow.h` (`ChipKey`, `filterKey`,
`reselect`) carry the only host-testable new logic.

**Tech Stack:** C++20 (GNU++20 on device), ESP32-S3 Arduino/PlatformIO, FreeInkUI, FreeRTOS, and
GoogleTest via CMake for the host suite.

**Spec:** `docs/superpowers/specs/2026-10-12-btrial-4-design.md`. Read its "Assumptions" (A1-A18)
before Task 4: they explain every non-obvious line below.

**Trial build.** While planning, every code block in this plan was applied to the tree at
`216c42ba`, checked, and reverted:
- `./bin/clang-format-fix` changed none of it;
- `TagRowsTest` passed 74 tests;
- `pio run -e x4pro` succeeded with no warning in `src/` or `lib/`.

If a step does not compile as written, the file it edits has changed since; see "Re-read every
file" below.

FILES: src/activities/reader/TagChipRow.h, test/tag_rows/TagChipRowTest.cpp
FILES: src/activities/reader/TagChipView.cpp
FILES: src/activities/reader/TagChipGridActivity.h, src/activities/reader/TagChipGridActivity.cpp
FILES: src/activities/reader/TagFilterActivity.h, src/activities/reader/TagFilterActivity.cpp
FILES: src/activities/reader/PassageLinksActivity.h

---

## Ground rules for this repo

- Work in `/Volumes/stein/.herdr/worktrees/berean-os/feature-tag-chip-grid-base`, on branch
  `feature/tag-chip-grid-base`. All paths below are relative to it.
- **Re-read every file before you edit it.** A sibling task may have changed it since this plan was
  written. If an "old" block below does not match the file, stop. Report the mismatch; do not adapt
  the plan silently.
- Comments only for a non-obvious *why*. Do not add comments that restate the code. The comments in
  the code blocks below are the ones to keep.
- `pio` is not on `PATH` in this shell. Use `~/.platformio/penv/bin/pio`. Never run two `pio`
  builds at once: they share `~/.platformio`.
- Format with `./bin/clang-format-fix` (the whole tree, as CI does). It prints nothing on success.
  Never call `clang-format` directly.
- Per the repo's `CLAUDE.md`, the firmware is built **once, after the last code edit** (Task 5).
  Tasks 1-2 are verified by host tests. Tasks 3-4 are compile-checked by Task 5's build. If that
  build fails, fix it in a new commit.
- Commit messages are conventional (`feat:`, `test:`, `refactor:`) and end with the trailer line
  `Claude-Session: https://claude.ai/code/session_019rwiRsbv1DarX5ZAbihujf`.

## File map

| File | Responsibility after this change |
|---|---|
| `src/activities/reader/TagChipRow.h` | Pure chip logic. Gains `ChipKey`, `filterKey`, `reselect`. |
| `test/tag_rows/TagChipRowTest.cpp` | Host tests for `TagChipRow.h`. Gains `TagChipReselect.*` and `TagChipFilterKey.*`. |
| `src/activities/reader/TagChipView.cpp` | `buildEntries` reads `activeIdsByName()`. |
| `src/activities/reader/TagChipGridActivity.h` / `.cpp` | New base: grid, paging, focus, input, retire, render. |
| `src/activities/reader/TagFilterActivity.h` / `.cpp` | Thin subclass: counts and scope via `buildEntries`, the filter, and its result. |
| `src/activities/reader/PassageLinksActivity.h` | One comment re-pointed at the moved "Both exits MUST set a result" note. |

---

### Task 0: Check the starting point

**Files:** none changed.

- [ ] **Step 1: Confirm branch and a clean tree**

Run:
```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/feature-tag-chip-grid-base
git branch --show-current
git status --short
```
Expected: `feature/tag-chip-grid-base`, then no status lines.

- [ ] **Step 2: Confirm the host baseline**

Run:
```bash
cmake -S test -B build/test >/dev/null
cmake --build build/test --target TagRowsTest -j8 2>&1 | tail -1
./build/test/tag_rows/TagRowsTest 2>&1 | tail -1
```
Expected: `[100%] Built target TagRowsTest`, then `[  PASSED  ] 65 tests.`

- [ ] **Step 3: Confirm the anchors this plan edits still exist**

Run:
```bash
grep -n "^int findChip\|^struct Placed {" src/activities/reader/TagChipRow.h
grep -n "STUDY.palette().activeIds()" src/activities/reader/TagChipView.cpp
grep -n "class TagFilterActivity final : public UiListActivity" src/activities/reader/TagFilterActivity.h
grep -n "for the reason TagFilterActivity gives" src/activities/reader/PassageLinksActivity.h
test -e src/activities/reader/TagChipGridActivity.h && echo EXISTS || echo absent
```
Expected:
- `130:int findChip(...` and `138:struct Placed {`. The exact line numbers may differ.
- `16:  const std::vector<study::TagId> activeIds = STUDY.palette().activeIds();`
- `26:class TagFilterActivity final : public UiListActivity {`
- `24:  // Both exits MUST set a result, for the reason TagFilterActivity gives.`
- `absent`

If any line is missing, stop and report.

---

### Task 1: `ChipKey` and `reselect`

**Files:**
- Modify: `src/activities/reader/TagChipRow.h` (insert after `findChip`, before `struct Placed`)
- Test: `test/tag_rows/TagChipRowTest.cpp` (append at the end of the file)

- [ ] **Step 1: Write the failing tests**

Append to the end of `test/tag_rows/TagChipRowTest.cpp`, after the last `TEST(TagChipFindChip, ...)`.
`FakeChip`, `CHIPS`, `HOPE`, `MINISTRY` and `NAME` are already defined earlier in the file.

```cpp

TEST(TagChipReselect, FollowsATagThatMovedInARebuild) {
  const std::vector<FakeChip> resorted{{TagChips::Kind::All, UNLABELLED},
                                       {TagChips::Kind::Tag, NAME},
                                       {TagChips::Kind::Tag, MINISTRY},
                                       {TagChips::Kind::Tag, HOPE}};
  EXPECT_EQ(TagChips::reselect(resorted, TagChips::ChipKey{TagChips::Kind::Tag, HOPE}, 1), 3);
}

TEST(TagChipReselect, ARetiredTagClampsThePreviousIndex) {
  // CHIPS with NAME (index 3) retired.
  const std::vector<FakeChip> afterRetire{{TagChips::Kind::NewTag, UNLABELLED},
                                          {TagChips::Kind::All, UNLABELLED},
                                          {TagChips::Kind::Tag, HOPE},
                                          {TagChips::Kind::Unlabelled, UNLABELLED}};
  EXPECT_EQ(TagChips::reselect(afterRetire, TagChips::ChipKey{TagChips::Kind::Tag, NAME}, 3), 3)
      << "the chip that slid into its slot";

  const std::vector<FakeChip> lastRetired{{TagChips::Kind::All, UNLABELLED}, {TagChips::Kind::Tag, HOPE}};
  EXPECT_EQ(TagChips::reselect(lastRetired, TagChips::ChipKey{TagChips::Kind::Tag, NAME}, 2), 1)
      << "past the end: the last chip";
}

TEST(TagChipReselect, SingletonKindsAreFoundByKindAlone) {
  EXPECT_EQ(TagChips::reselect(CHIPS, TagChips::ChipKey{TagChips::Kind::NewTag, UNLABELLED}, 4), 0);
  EXPECT_EQ(TagChips::reselect(CHIPS, TagChips::ChipKey{TagChips::Kind::All, UNLABELLED}, 4), 1);
  EXPECT_EQ(TagChips::reselect(CHIPS, TagChips::ChipKey{TagChips::Kind::Unlabelled, UNLABELLED}, 0), 4);
}

TEST(TagChipReselect, WithoutAKeyThePreviousIndexIsClamped) {
  EXPECT_EQ(TagChips::reselect(CHIPS, std::nullopt, 2), 2);
  EXPECT_EQ(TagChips::reselect(CHIPS, std::nullopt, 9), 4);
  EXPECT_EQ(TagChips::reselect(CHIPS, std::nullopt, -1), 0);
}

TEST(TagChipReselect, AnEmptyListSelectsZero) {
  EXPECT_EQ(TagChips::reselect(std::vector<FakeChip>{}, TagChips::ChipKey{TagChips::Kind::Tag, HOPE}, 3), 0);
  EXPECT_EQ(TagChips::reselect(std::vector<FakeChip>{}, std::nullopt, 0), 0);
}
```

- [ ] **Step 2: Run the build to see it fail**

Run:
```bash
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -m3 "error"
```
Expected: compile errors such as `no member named 'reselect' in namespace 'TagChips'` and
`no member named 'ChipKey' in namespace 'TagChips'`.

- [ ] **Step 3: Implement `ChipKey` and `reselect`**

In `src/activities/reader/TagChipRow.h`, find the end of `findChip`:

```cpp
template <typename Chips>
int findChip(const Chips& chips, const Kind kind, const study::TagId id) {
  for (size_t i = 0; i < chips.size(); ++i) {
    if (chips[i].kind != kind) continue;
    if (kind != Kind::Tag || chips[i].id == id) return static_cast<int>(i);
  }
  return -1;
}
```

Immediately after its closing `}` (and before `struct Placed {`), insert:

```cpp

// A chip's identity across rebuilds: All, Unlabelled and NewTag by kind alone, a Tag by its id.
struct ChipKey {
  Kind kind = Kind::All;
  study::TagId id = study::UNLABELLED;
};

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

`<algorithm>` and `<optional>` are already included at the top of the header.

- [ ] **Step 4: Run the tests to see them pass**

Run:
```bash
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -E "warning|error" ; ./build/test/tag_rows/TagRowsTest --gtest_filter='TagChipReselect.*' 2>&1 | tail -1 ; ./build/test/tag_rows/TagRowsTest 2>&1 | tail -1
```
Expected: no `warning`/`error` lines, then `[  PASSED  ] 5 tests.`, then `[  PASSED  ] 70 tests.`

- [ ] **Step 5: Format and commit**

```bash
./bin/clang-format-fix
git status --short
git add src/activities/reader/TagChipRow.h test/tag_rows/TagChipRowTest.cpp
git commit -m "feat: restore a chip selection by kind and id

Claude-Session: https://claude.ai/code/session_019rwiRsbv1DarX5ZAbihujf"
```
Expected: `git status --short` lists only those two files as modified (` M`).

---

### Task 2: `filterKey`

**Files:**
- Modify: `src/activities/reader/TagChipRow.h` (insert between `ChipKey` and `reselect`)
- Test: `test/tag_rows/TagChipRowTest.cpp` (append at the end)

- [ ] **Step 1: Write the failing tests**

Append to the end of `test/tag_rows/TagChipRowTest.cpp`:

```cpp

static_assert(TagChips::filterKey(std::nullopt).kind == TagChips::Kind::All);
static_assert(TagChips::filterKey(UNLABELLED).kind == TagChips::Kind::Unlabelled);
static_assert(TagChips::filterKey(study::toTagId(7)).kind == TagChips::Kind::Tag);

TEST(TagChipFilterKey, NoFilterIsTheAllChip) {
  const TagChips::ChipKey key = TagChips::filterKey(std::nullopt);
  EXPECT_EQ(key.kind, TagChips::Kind::All);
  EXPECT_EQ(TagChips::findChip(CHIPS, key.kind, key.id), 1);
}

TEST(TagChipFilterKey, UnlabelledIsTheUnlabelledChip) {
  const TagChips::ChipKey key = TagChips::filterKey(UNLABELLED);
  EXPECT_EQ(key.kind, TagChips::Kind::Unlabelled);
  EXPECT_EQ(TagChips::findChip(CHIPS, key.kind, key.id), 4);
}

TEST(TagChipFilterKey, ATagIsThatTagsChip) {
  const TagChips::ChipKey key = TagChips::filterKey(NAME);
  EXPECT_EQ(key.kind, TagChips::Kind::Tag);
  EXPECT_EQ(key.id, NAME);
  EXPECT_EQ(TagChips::findChip(CHIPS, key.kind, key.id), 3);
}

// candidates keeps a zero-count tag while it is the filter; the grid's entry selection must land on
// it at its name-order slot. Candidates carry a slot, not an id, so they are mapped the way
// TagChipView::buildEntries maps them.
TEST(TagChipFilterKey, AZeroCountFilterIsSelectedAtItsNameOrderSlot) {
  const std::vector<TagId> nameOrdered{NAME, HOPE, MINISTRY};
  const auto picked = candidatesOf(countsOf(3, 1, {2, 0, 1}), nameOrdered, HOPE);
  std::vector<FakeChip> chips;
  chips.reserve(picked.size());
  for (const TagChips::Candidate& candidate : picked) {
    chips.push_back(
        FakeChip{candidate.kind, candidate.kind == TagChips::Kind::Tag ? nameOrdered[candidate.slot] : UNLABELLED});
  }
  ASSERT_EQ(chips.size(), 5u) << "All, name, hope, ministry, Unlabelled";

  EXPECT_EQ(TagChips::reselect(chips, TagChips::filterKey(HOPE), 0), 2);
  EXPECT_EQ(TagChips::reselect(chips, TagChips::filterKey(std::nullopt), 3), 0);
  EXPECT_EQ(TagChips::reselect(chips, TagChips::filterKey(UNLABELLED), 0), 4);
}
```

`countsOf(all, unlabelled, perTag)` and `candidatesOf(counts, active, filter)` are helpers already
defined earlier in the file (around the `TagChipCandidates` tests).

- [ ] **Step 2: Run the build to see it fail**

Run:
```bash
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -m3 "error"
```
Expected: compile errors such as `no member named 'filterKey' in namespace 'TagChips'`.

- [ ] **Step 3: Implement `filterKey`**

In `src/activities/reader/TagChipRow.h`, between the `ChipKey` struct and the `reselect` comment
added in Task 1, insert:

```cpp

// The chip that shows `filter`: All for none, Unlabelled for UNLABELLED, otherwise that tag.
constexpr ChipKey filterKey(const std::optional<study::TagId> filter) {
  if (!filter) return ChipKey{Kind::All, study::UNLABELLED};
  if (*filter == study::UNLABELLED) return ChipKey{Kind::Unlabelled, study::UNLABELLED};
  return ChipKey{Kind::Tag, *filter};
}
```

The block then reads, in order: `struct ChipKey { ... };`, `filterKey`, `reselect`.

- [ ] **Step 4: Run the tests to see them pass**

Run:
```bash
cmake --build build/test --target TagRowsTest -j8 2>&1 | grep -E "warning|error" ; ./build/test/tag_rows/TagRowsTest --gtest_filter='TagChipFilterKey.*' 2>&1 | tail -1 ; ./build/test/tag_rows/TagRowsTest 2>&1 | tail -1
```
Expected: no `warning`/`error` lines, then `[  PASSED  ] 4 tests.`, then `[  PASSED  ] 74 tests.`

- [ ] **Step 5: Format and commit**

```bash
./bin/clang-format-fix
git status --short
git add src/activities/reader/TagChipRow.h test/tag_rows/TagChipRowTest.cpp
git commit -m "feat: name the chip that shows a tag filter

Claude-Session: https://claude.ai/code/session_019rwiRsbv1DarX5ZAbihujf"
```

---

### Task 3: List chips by name

**Files:**
- Modify: `src/activities/reader/TagChipView.cpp:16`

No host target compiles `TagChipView.cpp`. This change is compile-checked by Task 5's build and
checked on the device. `TagChipCandidates.NameOrderedIdsKeepAllFirstAndUnlabelledLast` already
pins that All and Unlabelled stay at the ends when the ids are name-ordered.

- [ ] **Step 1: Switch to display order**

In `src/activities/reader/TagChipView.cpp`, replace:

```cpp
  const std::vector<study::TagId> activeIds = STUDY.palette().activeIds();
```

with:

```cpp
  const std::vector<study::TagId> activeIds = STUDY.palette().activeIdsByName();
```

Nothing else in `buildEntries` changes. The same vector feeds both `count`/`countIn` and
`candidates`, so the counts stay aligned with their chips.

- [ ] **Step 2: Format and commit**

```bash
./bin/clang-format-fix
git status --short
git add src/activities/reader/TagChipView.cpp
git commit -m "feat: list filter and Highlights tag chips by name

Claude-Session: https://claude.ai/code/session_019rwiRsbv1DarX5ZAbihujf"
```
Expected: `git status --short` showed only ` M src/activities/reader/TagChipView.cpp` before the add.

---

### Task 4: The base, `TagChipGridActivity`

**Files:**
- Create: `src/activities/reader/TagChipGridActivity.h`
- Create: `src/activities/reader/TagChipGridActivity.cpp`

The body is `TagFilterActivity.cpp`'s grid, moved. `TagFilterActivity` stays unchanged in this task.
It is switched over in Task 5, so the tree compiles after each task.

- [ ] **Step 1: Create the header**

Create `src/activities/reader/TagChipGridActivity.h` with exactly:

```cpp
#pragma once

#include <StudyStore/TagPalette.h>

#include <optional>
#include <vector>

#include "TagChipRow.h"
#include "TagChipView.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

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
  virtual void buildHeaderExtras(UiScreen&) {}
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
  void buildGrid(UiScreen& screen);
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

- [ ] **Step 2: Create the source**

Create `src/activities/reader/TagChipGridActivity.cpp` with exactly:

```cpp
#include "TagChipGridActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/ActivityResult.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

TagChipGridActivity::TagChipGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(name, renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

int TagChipGridActivity::listCount() const { return static_cast<int>(chips_.size()); }

void TagChipGridActivity::rebuildChips() {
  // The loop task is the only writer of chips_ and nav.selected, so it reads them here unlocked.
  std::optional<TagChips::ChipKey> selected;
  if (nav.selected >= 0 && nav.selected < listCount()) {
    const TagChipView::ChipEntry& chip = chips_[static_cast<size_t>(nav.selected)];
    selected = TagChips::ChipKey{chip.kind, chip.id};
  }
  rebuildChipsSelecting(selected);
}

void TagChipGridActivity::rebuildChips(const TagChips::ChipKey select) { rebuildChipsSelecting(select); }

void TagChipGridActivity::rebuildChipsSelecting(const std::optional<TagChips::ChipKey> select) {
  std::vector<TagChipView::ChipEntry> next;
  buildChips(next);
  const int selected = TagChips::reselect(next, select, nav.selected);
  {
    RenderLock lock(*this);
    chips_ = std::move(next);
    widths_.assign(chips_.size(), 0);
    nav.selected = selected;
    // The published interaction table indexes the old list until a render replaces it; a tap routed
    // against it would land on whichever chip now holds that index.
    closeRouting();
  }
  // Only a render reopens routing.
  requestUpdate();
}

void TagChipGridActivity::buildScreen(UiScreen& screen) {
  buildGrid(screen);
  buildHeaderExtras(screen);
}

void TagChipGridActivity::buildGrid(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int count = static_cast<int>(chips_.size());
  if (count == 0 || static_cast<int>(widths_.size()) != count) return;
  const TagChipView::Metrics chip = TagChipView::metricsFor(screen);
  for (int i = 0; i < count; ++i) widths_[i] = TagChipView::measure(screen, chips_[i].label, chip);

  const int inset = screen.theme().listInset;
  const fui::Rect body = screen.body();
  gridLineWidth_ = body.width - 2 * inset;
  gridGap_ = chip.gap;
  gridLinesPerPage_ = TagChips::linesPerPage(body.height, chip.chipHeight, chip.gap);
  pageCount_ = TagChips::pageCountOf(widths_.data(), count, gridLineWidth_, gridGap_, gridLinesPerPage_);
  int stripHeight = 0;
  if (pageCount_ > 1) {
    // Only a paged grid shows the page strip; taking its height out can only keep the grid paged.
    stripHeight = metrics.tabBarHeight;
    gridLinesPerPage_ = TagChips::linesPerPage(body.height - stripHeight, chip.chipHeight, chip.gap);
    pageCount_ = TagChips::pageCountOf(widths_.data(), count, gridLineWidth_, gridGap_, gridLinesPerPage_);
  }

  const TagChips::PageSpan span =
      TagChips::pageHolding(widths_.data(), count, nav.selected, gridLineWidth_, gridGap_, gridLinesPerPage_);
  pageNumber_ = span.page;
  nav.top = span.first;
  TagChips::layoutPage(widths_.data(), count, span.first, gridLineWidth_, gridGap_, gridLinesPerPage_, page_);

  const int gridTop = body.y + stripHeight;
  fui::Rect focused{};
  for (int k = 0; k < page_.placedCount; ++k) {
    const TagChips::Placed& placed = page_.placed[k];
    const TagChipView::ChipEntry& entry = chips_[static_cast<size_t>(placed.chip)];
    const fui::Rect rect{static_cast<int16_t>(body.x + inset + placed.x),
                         static_cast<int16_t>(gridTop + TagChips::lineTop(placed.line, chip.chipHeight, gridGap_)),
                         static_cast<int16_t>(placed.width), static_cast<int16_t>(chip.chipHeight)};
    TagChipView::draw(screen, rect, entry.label, chipState(entry), ACTION_ROW, static_cast<int16_t>(placed.chip),
                      fui::InputTouch | fui::InputLongPress,
                      TagChips::hitPadding(page_.placed, page_.placedCount, page_.lines, k, gridGap_));
    if (placed.chip == nav.selected) focused = rect;
  }

  if (buttonFocus_ && focused.width > 0) {
    // Drawn in the gap around the chip, which hitPadding splits but nothing paints.
    // Clamped to the body horizontally: with a zero listInset an edge chip's outline would
    // otherwise fall off the screen, which HighlightsActivity's band outline avoids the same way.
    const int outset = std::max(gridGap_ / 2, 1);
    const int left = std::max(focused.x - outset, static_cast<int>(body.x));
    const int right = std::min(focused.x + focused.width + outset, body.x + body.width);
    const fui::Rect outline{static_cast<int16_t>(left), static_cast<int16_t>(focused.y - outset),
                            static_cast<int16_t>(right - left), static_cast<int16_t>(focused.height + 2 * outset)};
    screen.target().stroke(outline, fui::Paint::solid(fui::Color::Black), 2,
                           static_cast<uint8_t>(std::min(outline.height / 2, 255)));
  }
}

void TagChipGridActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (pageCount_ <= 1) return;

  char indicator[24];
  snprintf(indicator, sizeof(indicator), "%d/%d", pageNumber_ + 1, pageCount_);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawSubHeader(renderer,
                    Rect{safe.x, safe.y + metrics.topPadding + metrics.headerHeight, safe.width, metrics.tabBarHeight},
                    "", indicator);
}

void TagChipGridActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool TagChipGridActivity::handleHomeGesture() {
  // Consumed rather than left to ActivityManager's "go home", which would abandon the reader
  // entirely. With the retire confirmation open, Home must not act on the screen behind it.
  if (!confirmPopup_.isActive()) onHome();
  return true;
}

void TagChipGridActivity::onRowAction(const fui::ActionEvent& event) {
  {
    RenderLock lock(*this);
    buttonFocus_ = false;
    nav.selected = event.value;
  }
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

void TagChipGridActivity::onRowLongPress(const int index) {
  if (confirmPopup_.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  const TagChipView::ChipEntry& chip = chips_[static_cast<size_t>(index)];
  // All, Unlabelled and New tag are not palette entries; a long-press there must never reach the
  // retire path.
  if (chip.kind != TagChips::Kind::Tag) return;
  app.clearTapFlash();
  showRetireConfirmation(chip.id);
}

void TagChipGridActivity::showRetireConfirmation(const study::TagId id) {
  if (confirmPopup_.isActive()) return;
  if (STUDY.saveDisabled()) {
    // The file may still hold the user's data; never let a destructive palette
    // change through in that state.
    ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_LOAD_FAILED));
    requestUpdate();
    return;
  }

  pendingRetireId_ = id;
  confirmingDelete_ = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup_.show(tr(STR_CONFIRM_DELETE_TAG), options, 2, 0, [this](const int idx) {
    confirmingDelete_ = false;
    if (idx == 1) retireTag(pendingRetireId_);
    requestUpdate();
  });
  requestUpdate();
}

void TagChipGridActivity::retireTag(const study::TagId id) {
  const bool saved = STUDY.retireTag(id);
  // StudyStore retires in memory before it saves and never rolls back, so a failed save can still
  // have retired the tag.
  if (!STUDY.palette().isActive(id)) onTagRetired(id);
  rebuildChips();
  if (!saved) ReaderUtils::showMessage(renderer, tr(STR_TAG_SAVE_FAILED));
}

bool TagChipGridActivity::handleCustomInput() {
  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete_) {
    // Popup dismissed without choosing (Back, or a tap outside it): drop the
    // pending retire and stay here.
    confirmingDelete_ = false;
    requestUpdate();
    return true;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;
  // Consumed either way: the base loop would scroll by list rows, which a grid does not have.
  turnPage(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1, /*fromButton=*/false);
  return true;
}

bool TagChipGridActivity::handleButtons() { return UiListActivity::handleButtons(); }

void TagChipGridActivity::navigateButtons() {
  // Each handler takes RenderLock only when its button fires. Locking here would
  // block every loop pass for as long as a render (panel refresh included) runs.
  buttonNavigator.onNextRelease([this] { stepSelection(1); });
  buttonNavigator.onPreviousRelease([this] { stepSelection(-1); });
  buttonNavigator.onNextContinuous([this] { turnPage(1, /*fromButton=*/true); });
  buttonNavigator.onPreviousContinuous([this] { turnPage(-1, /*fromButton=*/true); });
}

void TagChipGridActivity::stepSelection(const int direction) {
  {
    RenderLock lock(*this);
    const int last = listCount() - 1;
    if (last < 0) return;
    nav.selected = std::clamp(nav.selected + direction, 0, last);
    buttonFocus_ = true;
  }
  requestUpdate();
}

void TagChipGridActivity::turnPage(const int direction, const bool fromButton) {
  bool moved = false;
  {
    // One lock around read and write: the render task rewrites the page geometry mid-build.
    RenderLock lock(*this);
    const int count = listCount();
    const TagChips::PageSpan span =
        TagChips::pageHolding(widths_.data(), count, nav.selected, gridLineWidth_, gridGap_, gridLinesPerPage_);
    int target = -1;
    if (direction > 0 && span.next < count) target = span.next;
    if (direction < 0 && span.first > 0) {
      target = TagChips::pageHolding(widths_.data(), count, span.first - 1, gridLineWidth_, gridGap_, gridLinesPerPage_)
                   .first;
    }
    if (target >= 0) {
      nav.selected = target;
      buttonFocus_ = fromButton;
      moved = true;
    }
  }
  if (moved) requestUpdate();
}

void TagChipGridActivity::render(RenderLock&&) {
  // Mirrors UiListActivity::render()'s body with the confirmation interleaved
  // between the app render and the footer; the base render has no seam for it.
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }

  if (confirmPopup_.processRender(renderer, mappedInput)) return;

  drawFooter();
  renderer.displayBuffer();
  PostedMessage::drawNext(renderer);
}
```

Compared with `TagFilterActivity.cpp` today, only these differ. Each is a spec decision, not a
style choice:
- the chip state comes from `chipState(entry)` (spec "Changes to the moved code");
- `buildScreen` is split so `buildHeaderExtras` runs on every build (A9);
- `rebuildChips` reselects by key, closes routing under the lock, and requests the render (A2, A5,
  A6);
- `onRowAction` writes `nav.selected` under the lock (A4);
- Home is guarded by the popup (A7);
- the retire path calls `onTagRetired` and says `STR_TAG_SAVE_FAILED` (A8);
- `handleButtons` is pinned `final` (A12).

- [ ] **Step 3: Check that the new files are well formed**

Run:
```bash
./bin/clang-format-fix
git status --short
```
Expected: exactly two untracked files, `?? src/activities/reader/TagChipGridActivity.cpp` and
`?? src/activities/reader/TagChipGridActivity.h`. The formatter leaves this code as written (trial
build).

- [ ] **Step 4: Commit**

```bash
git add src/activities/reader/TagChipGridActivity.h src/activities/reader/TagChipGridActivity.cpp
git commit -m "refactor: add TagChipGridActivity, the shared tag chip grid

Claude-Session: https://claude.ai/code/session_019rwiRsbv1DarX5ZAbihujf"
```

---

### Task 5: `TagFilterActivity` as the first subclass, then the firmware build

**Files:**
- Modify (rewrite): `src/activities/reader/TagFilterActivity.h`
- Modify (rewrite): `src/activities/reader/TagFilterActivity.cpp`
- Modify: `src/activities/reader/PassageLinksActivity.h:24`

- [ ] **Step 1: Rewrite the header**

Replace the whole of `src/activities/reader/TagFilterActivity.h` with:

```cpp
#pragma once

#include <StudyStore/TagPalette.h>

#include <cstddef>
#include <optional>
#include <vector>

#include "TagChipGridActivity.h"

// The highlights chip row's ellipsis: every chip the row would offer, uncapped, as a wrapping grid
// with counts, paged only when it outgrows one screen. Returns a TagSelectionResult holding no
// elements for "All" or exactly one id -- study::UNLABELLED for "Unlabelled"; a cancelled result
// means the caller keeps its current filter.
//
// Chips report a study::TagId, not an index. Ids are allocated once and never reused, so a
// retirement cannot renumber anything and the caller's captured filter stays valid.
class TagFilterActivity final : public TagChipGridActivity {
 public:
  // scope: nullopt counts every passage; otherwise only these passage indices, so the chapter view's
  // counts match the list it shows. Computed by the caller with no RenderLock held.
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

- [ ] **Step 2: Rewrite the source**

Replace the whole of `src/activities/reader/TagFilterActivity.cpp` with:

```cpp
#include "TagFilterActivity.h"

#include <I18n.h>

#include <cstdint>
#include <utility>

#include "activities/ActivityResult.h"

TagFilterActivity::TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::optional<std::vector<size_t>> scope, const std::optional<study::TagId> filter)
    : TagChipGridActivity("TagFilter", renderer, mappedInput), scope_(std::move(scope)), filter_(filter) {}

void TagFilterActivity::onEnter() {
  TagChipGridActivity::onEnter();
  rebuildChips(TagChips::filterKey(filter_));
}

const char* TagFilterActivity::headerTitle() const { return tr(STR_FILTER_BY_TAG); }

void TagFilterActivity::buildChips(std::vector<TagChipView::ChipEntry>& out) {
  TagChipView::buildEntries(scope_, filter_, SIZE_MAX, out);
}

TagChips::ChipState TagFilterActivity::chipState(const TagChipView::ChipEntry& chip) const {
  return TagChipView::isSelected(chip, filter_) ? TagChips::ChipState::Active : TagChips::ChipState::Inactive;
}

void TagFilterActivity::activateIndex(const int index) {
  if (retireConfirmationOpen()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  TagSelectionResult result;
  // "All" returns an empty selection; every other chip reports a tag ID, never
  // an index -- the caller holds that filter across screens where the palette
  // may be edited. Unlabelled's id is study::UNLABELLED.
  const TagChipView::ChipEntry& chip = chips()[static_cast<size_t>(index)];
  if (chip.kind != TagChips::Kind::All) result.tagIds.push_back(chip.id);
  setResult(std::move(result));
  finish();
}

void TagFilterActivity::onTagRetired(const study::TagId id) {
  if (filter_ != id) return;
  // A filter on a retired tag has no chip; fall back to All so one chip stays inverted, as
  // HighlightsActivity::dropRetiredFilter does on return.
  RenderLock lock(*this);
  filter_.reset();
}

void TagFilterActivity::onHome() {
  // Cancelling matches Back.
  onBackButton();
}
```

- [ ] **Step 3: Re-point the `PassageLinksActivity` comment**

In `src/activities/reader/PassageLinksActivity.h`, replace:

```cpp
  // Both exits MUST set a result, for the reason TagFilterActivity gives.
```

with:

```cpp
  // Both exits MUST set a result, for the reason TagChipGridActivity::onBackButton gives.
```

- [ ] **Step 4: Confirm nothing else referred to the removed members**

Run:
```bash
grep -rn "selectedChipIndex\|TagFilterActivity::rebuildChips\|TagFilterActivity::retireTag" src test
```
Expected: no output.

- [ ] **Step 5: Format**

Run:
```bash
./bin/clang-format-fix
git status --short
```
Expected: ` M` for `PassageLinksActivity.h`, `TagFilterActivity.cpp` and `TagFilterActivity.h`, and
nothing else.

- [ ] **Step 6: Build the firmware (the one build after the last code edit)**

Run:
```bash
~/.platformio/penv/bin/pio run -e x4pro > build/btrial-4-pio.log 2>&1; echo "exit=$?"
grep -E "^(RAM|Flash):" build/btrial-4-pio.log | tail -2
grep "warning:" build/btrial-4-pio.log | grep -E "^(src|lib)/" ; echo "repo-warnings-done"
ls -l .pio/build/x4pro/firmware.bin
```
Expected:
- `exit=0`
- the RAM and Flash lines. The baseline was RAM 66,228 B and Flash 5,563,214 B (spec "Baseline").
  A trial build of exactly this plan's code, made while planning, gave RAM 66,228 B (unchanged) and
  Flash 5,563,766 B (+552 B).
- `repo-warnings-done` with no warning line before it. The baseline has none in `src/` or `lib/`.
- `firmware.bin` present.

If the build fails, read the first `error:` line in `build/btrial-4-pio.log`, fix it in the file it
names, and re-run this step. A fix to Task 4's files belongs in this task's commit; say so in the
commit body.

- [ ] **Step 7: Commit**

```bash
git add src/activities/reader/TagFilterActivity.h src/activities/reader/TagFilterActivity.cpp src/activities/reader/PassageLinksActivity.h
git commit -m "refactor: make TagFilterActivity a TagChipGridActivity

The filter grid now inherits paging, focus, retire and render from the
base. Home no longer cancels from under the retire popup, and a failed
retire save says \"Could not save tag\".

Claude-Session: https://claude.ai/code/session_019rwiRsbv1DarX5ZAbihujf"
```

---

### Task 6: Full verification

**Files:** none changed unless a check fails.

- [ ] **Step 1: The full host suite**

Run:
```bash
cmake -S test -B build/test >/dev/null
cmake --build build/test -j8 2>&1 | grep -E "error" ; ctest --test-dir build/test -j8 2>&1 | tail -3
```
Expected: no `error` lines, and `100% tests passed out of 1745`. That is the 1736 baseline plus the 9
new tests: 5 `TagChipReselect` and 4 `TagChipFilterKey`.

- [ ] **Step 2: Format over the whole tree, as CI runs it**

Run:
```bash
./bin/clang-format-fix
git status --short
```
Expected: no output from either. If the formatter changed a file, commit it as
`style: format the tag chip grid` with the session trailer. It needs no rebuild.

- [ ] **Step 3: Confirm the commit series**

Run:
```bash
git log --oneline -6
```
Expected, newest first, the five task commits below. A `style:` commit from Step 2 may sit on top,
and the sixth line is whatever preceded Task 1:
- `refactor: make TagFilterActivity a TagChipGridActivity`
- `refactor: add TagChipGridActivity, the shared tag chip grid`
- `feat: list filter and Highlights tag chips by name`
- `feat: name the chip that shows a tag filter`
- `feat: restore a chip selection by kind and id`

- [ ] **Step 4: Report**

Report back with:
- the `ctest` tail;
- the RAM and Flash lines, with their deltas against 66,228 and 5,563,214;
- confirmation that `src/` and `lib/` have no warnings;
- the `git log` list.

Then list, unchanged, the device checks from the spec (section "Device checks (owner)", items 1-9).
They need the owner and hardware, and this plan does not run them.

---

## Self-review against the spec

| Spec item | Task |
|---|---|
| `ChipKey`, `reselect` + `TagChipReselect` tests | 1 |
| `filterKey` + `TagChipFilterKey` tests (the integration test mapped through `FakeChip`, review m3) | 2 |
| `buildEntries` uses `activeIdsByName()` (A15) | 3 |
| Base owns `chips_`, `widths_`, `page_`, geometry, `buttonFocus_`, the popup | 4 |
| Six hooks with the lock contract in the header | 4 |
| `rebuildChips()` / `rebuildChips(ChipKey)`: reselect, `closeRouting()` under the lock, request update (A2, A5, A6) | 4 |
| `buildHeaderExtras` on every build (A9) | 4 (`buildScreen` → `buildGrid` + `buildHeaderExtras`) |
| `onRowAction` locks `nav.selected` (A4) | 4 |
| Home guarded by the popup, then `onHome` (A7) | 4 |
| Retire: `onTagRetired` on `!isActive`, `STR_TAG_SAVE_FAILED` (A8) | 4 |
| `final` members including `handleButtons` (A12, review m2) | 4 |
| `chips()` and `retireConfirmationOpen()` (A13) | 4 |
| Render unchanged, with `drawNext` (A14) | 4 |
| `ACTION_USER` left free, documented (A11) | 4 (header comment) |
| Thin `TagFilterActivity`; entry selection via `filterKey` (A1, A10) | 5 |
| `onTagRetired` resets only on `filter_ == id` (A8) | 5 |
| Class comment split; `PassageLinksActivity.h:24` re-pointed (A16) | 4, 5 |
| `pio run` once after the last code edit; no `src/`/`lib/` warnings | 5 |
| Full host suite, whole-tree format | 6 |
| Device checks | 6, reported for the owner |
