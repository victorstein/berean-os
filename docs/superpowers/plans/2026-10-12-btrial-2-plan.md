# Plan: tag chip states and the pure chip helpers (btrial-2)

Spec: `docs/superpowers/specs/2026-10-12-btrial-2-design.md` (cleared review 0). Research:
`docs/superpowers/research/2026-10-12-btrial-2-research.md`. Work in
`/Volumes/stein/.herdr/worktrees/berean-os/feature-tag-chip-states`, on `feature/tag-chip-states`.
Commit after each task; **never push**.

FILES: src/activities/reader/TagChipRow.h, src/activities/reader/TagChipView.h, src/activities/reader/TagChipView.cpp
FILES: src/activities/reader/HighlightsActivity.cpp, src/activities/reader/TagFilterActivity.cpp
FILES: test/tag_rows/TagChipRowTest.cpp, lib/I18n/translations/english.yaml

## Ground rules for the implementer

- **Re-read every file before you edit it.** A sibling task may have changed it since this plan
  was written. The anchors below are quoted text, not line numbers; if one no longer matches, stop
  and report rather than guess.
- Code style: no comment that restates the code. Comments only for a non-obvious why. Keep the
  existing comments you do not touch.
- Commit messages: conventional, and end each with a blank line then
  `Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo`. No co-author trailer.
- `pio` is at `~/.platformio/penv/bin/pio`. Run one `pio run` at a time.
- Host test commands, used throughout:
  - Configure once: `cmake -S test -B build/test`
  - Build the suite's target: `cmake --build build/test --target TagRowsTest -j 8`
  - Run a filter: `./build/test/tag_rows/TagRowsTest --gtest_filter='<Suite>.*'`
- Why one task carries both the enum change and the `TagChipView.cpp` switch cases: `-Wall` (so
  `-Wswitch`) applies to `src/` in the firmware build (`scripts/enable_repo_warnings.py:26-30`), so a
  new `Kind` enumerator without its two `case`s would leave a commit that warns.

---

## Task 1 — `Kind::NewTag`, `ChipState` and `pickerState`

### Step 1.1 — write the failing tests (3 min)

In `test/tag_rows/TagChipRowTest.cpp`, add this include beside the existing
`#include "activities/reader/TagChipRow.h"` (clang-format regroups includes later):

```cpp
#include <StudyStore/PassageDoc.h>
```

Append to the **end of the file**:

```cpp
namespace {

constexpr size_t CAP = study::PassageDoc::MAX_TAGS_PER_PASSAGE;

}  // namespace

static_assert(TagChips::pickerState(TagChips::Kind::NewTag, false, CAP, CAP) == TagChips::ChipState::Action);

TEST(TagChipPickerState, BelowTheCapUnappliedTagsAreInactive) {
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP - 1, CAP), TagChips::ChipState::Inactive);
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, true, CAP - 1, CAP), TagChips::ChipState::Active);
}

TEST(TagChipPickerState, AtTheCapUnappliedTagsAreDisabled) {
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP, CAP), TagChips::ChipState::Disabled);
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, true, CAP, CAP), TagChips::ChipState::Active);
}

// At the cap one applied tag is removed: the chip that was Disabled and the chip just removed are
// both unapplied with one slot free, so both are Inactive.
TEST(TagChipPickerState, ADeselectAtTheCapMakesUnappliedTagsInactiveAgain) {
  ASSERT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP, CAP), TagChips::ChipState::Disabled);
  ASSERT_EQ(TagChips::pickerState(TagChips::Kind::Tag, true, CAP, CAP), TagChips::ChipState::Active);
  const size_t afterDeselect = CAP - 1;
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, afterDeselect, CAP), TagChips::ChipState::Inactive);
}

TEST(TagChipPickerState, OverTheCapUnappliedTagsStayDisabled) {
  EXPECT_EQ(TagChips::pickerState(TagChips::Kind::Tag, false, CAP + 1, CAP), TagChips::ChipState::Disabled);
}

TEST(TagChipPickerState, NewTagIsAnActionAtAnyCount) {
  for (const size_t applied : {size_t{0}, CAP - 1, CAP}) {
    EXPECT_EQ(TagChips::pickerState(TagChips::Kind::NewTag, false, applied, CAP), TagChips::ChipState::Action)
        << applied << " applied";
  }
}
```

### Step 1.2 — run them, see them fail (1 min)

```bash
cmake -S test -B build/test && cmake --build build/test --target TagRowsTest -j 8
```

Expected: compile errors — `no member named 'pickerState' in namespace 'TagChips'`, `no member
named 'NewTag'`, `no member named 'ChipState'`.

### Step 1.3 — implement in `TagChipRow.h` (3 min)

In `src/activities/reader/TagChipRow.h`, replace

```cpp
enum class Kind : uint8_t { All, Tag, Unlabelled };
```

with

```cpp
enum class Kind : uint8_t { All, Tag, Unlabelled, NewTag };

enum class ChipState : uint8_t { Inactive, Active, Disabled, Action };

// A tag the passage lacks is Disabled once `cap` tags are applied. Disabled is a look, not an
// untappable chip: a tap still explains the limit and a long-press can still retire the tag.
constexpr ChipState pickerState(const Kind kind, const bool selected, const size_t selectedCount, const size_t cap) {
  if (kind == Kind::NewTag) return ChipState::Action;
  if (selected) return ChipState::Active;
  return selectedCount < cap ? ChipState::Inactive : ChipState::Disabled;
}
```

### Step 1.4 — handle `NewTag` in both switches in `TagChipView.cpp` (2 min)

In `src/activities/reader/TagChipView.cpp`, in `buildEntries`, replace

```cpp
      case TagChips::Kind::Tag:
        chip.id = activeIds[candidate.slot];
        snprintf(chip.label, sizeof(chip.label), "%s %u", STUDY.tagName(chip.id).c_str(),
                 static_cast<unsigned>(counts.perTag[candidate.slot]));
        break;
    }
```

with

```cpp
      case TagChips::Kind::Tag:
        chip.id = activeIds[candidate.slot];
        snprintf(chip.label, sizeof(chip.label), "%s %u", STUDY.tagName(chip.id).c_str(),
                 static_cast<unsigned>(counts.perTag[candidate.slot]));
        break;
      case TagChips::Kind::NewTag:
        // candidates never emits one; skipping it cannot show an empty chip.
        continue;
    }
```

In `isSelected`, replace

```cpp
    case TagChips::Kind::Tag:
      return filter == chip.id;
  }
```

with

```cpp
    case TagChips::Kind::Tag:
      return filter == chip.id;
    case TagChips::Kind::NewTag:
      return false;
  }
```

### Step 1.5 — run the tests, see them pass (1 min)

```bash
cmake --build build/test --target TagRowsTest -j 8 && ./build/test/tag_rows/TagRowsTest --gtest_filter='TagChipPickerState.*'
```

Expected: `[  PASSED  ] 5 tests.` Then the whole target:
`./build/test/tag_rows/TagRowsTest` → every test passes.

### Step 1.6 — commit (1 min)

```bash
git add src/activities/reader/TagChipRow.h src/activities/reader/TagChipView.cpp test/tag_rows/TagChipRowTest.cpp
git commit -m "feat: add chip states and the picker state rule

Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo"
```

---

## Task 2 — `findChip`

### Step 2.1 — write the failing tests (3 min)

Append to the **end of** `test/tag_rows/TagChipRowTest.cpp`:

```cpp
namespace {

// findChip reads only .kind and .id, so a plain struct stands in for TagChipView::ChipEntry.
struct FakeChip {
  TagChips::Kind kind;
  TagId id;
};

const std::vector<FakeChip> CHIPS{{TagChips::Kind::NewTag, UNLABELLED},
                                  {TagChips::Kind::All, UNLABELLED},
                                  {TagChips::Kind::Tag, HOPE},
                                  {TagChips::Kind::Tag, NAME},
                                  {TagChips::Kind::Unlabelled, UNLABELLED}};

}  // namespace

TEST(TagChipFindChip, FindsATagById) {
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Tag, HOPE), 2);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Tag, NAME), 3);
}

TEST(TagChipFindChip, FindsTheSingletonKindsByKindAlone) {
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::NewTag, UNLABELLED), 0);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::All, UNLABELLED), 1);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Unlabelled, UNLABELLED), 4);
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::All, HOPE), 1) << "the id is ignored for All";
}

TEST(TagChipFindChip, ARetiredTagOrAnEmptyListIsNotFound) {
  EXPECT_EQ(TagChips::findChip(CHIPS, TagChips::Kind::Tag, MINISTRY), -1);
  EXPECT_EQ(TagChips::findChip(std::vector<FakeChip>{}, TagChips::Kind::Tag, HOPE), -1);
  EXPECT_EQ(TagChips::findChip(std::vector<FakeChip>{}, TagChips::Kind::All, UNLABELLED), -1);
}

TEST(TagChipFindChip, TheKindMustMatch) {
  const std::vector<FakeChip> tagOnly{{TagChips::Kind::Tag, UNLABELLED}};
  EXPECT_EQ(TagChips::findChip(tagOnly, TagChips::Kind::Unlabelled, UNLABELLED), -1);
  const std::vector<FakeChip> unlabelledOnly{{TagChips::Kind::Unlabelled, UNLABELLED}};
  EXPECT_EQ(TagChips::findChip(unlabelledOnly, TagChips::Kind::Tag, UNLABELLED), -1);
}
```

### Step 2.2 — run them, see them fail (1 min)

```bash
cmake --build build/test --target TagRowsTest -j 8
```

Expected: compile error `no member named 'findChip' in namespace 'TagChips'`.

### Step 2.3 — implement in `TagChipRow.h` (2 min)

In `src/activities/reader/TagChipRow.h`, directly after the closing `}` of `inline void
candidates(...)` (the function ending with
`if (counts.unlabelled > 0 || filter == study::UNLABELLED) out.push_back(Candidate{Kind::Unlabelled, 0});`
then `}`), insert:

```cpp

// Restores a selection after the chip list is rebuilt: the index of the chip of `kind` (and, for a
// Tag, of `id`), or -1 when it is gone. All, Unlabelled and NewTag occur at most once in a list, so
// they match by kind alone. A template over the caller's chip type keeps FreeInkUI out of this header.
template <typename Chips>
int findChip(const Chips& chips, const Kind kind, const study::TagId id) {
  for (size_t i = 0; i < chips.size(); ++i) {
    if (chips[i].kind != kind) continue;
    if (kind != Kind::Tag || chips[i].id == id) return static_cast<int>(i);
  }
  return -1;
}
```

### Step 2.4 — run the tests, see them pass (1 min)

```bash
cmake --build build/test --target TagRowsTest -j 8 && ./build/test/tag_rows/TagRowsTest --gtest_filter='TagChipFindChip.*'
```

Expected: `[  PASSED  ] 4 tests.`

### Step 2.5 — commit (1 min)

```bash
git add src/activities/reader/TagChipRow.h test/tag_rows/TagChipRowTest.cpp
git commit -m "feat: find a tag chip again after a rebuild

Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo"
```

---

## Task 3 — pin `candidates` on name-ordered ids (characterisation test)

This test passes on its first run: `candidates` already emits slots in the order it is given
(`TagChipRow.h:100-114`). It pins that for the rank task (spec, review 0 m1).

### Step 3.1 — add the test (3 min)

In `test/tag_rows/TagChipRowTest.cpp`, directly **before**
`TEST(TagChipGeometry, ChipHeightIsNeverBelowTheTouchMinimum) {`, insert:

```cpp
// The rank task passes name-ordered ids; All and Unlabelled must stay pinned around them, and a
// zero-count filter keeps its name-order slot.
TEST(TagChipCandidates, NameOrderedIdsKeepAllFirstAndUnlabelledLast) {
  const std::vector<TagId> nameOrdered{NAME, HOPE, MINISTRY};
  const std::vector<FakePassage> passages{{{NAME}}, {{NAME, MINISTRY}}, {{UNLABELLED}}};
  const TagChips::Counts counts = TagChips::count(passages, nameOrdered);
  EXPECT_EQ(counts.perTag, (std::vector<uint16_t>{2, 0, 1}));

  const auto out = candidatesOf(counts, nameOrdered, HOPE);
  ASSERT_EQ(out.size(), 5u);
  EXPECT_EQ(out[0].kind, TagChips::Kind::All);
  for (uint16_t slot = 0; slot < 3; ++slot) {
    EXPECT_EQ(out[1 + slot].kind, TagChips::Kind::Tag);
    EXPECT_EQ(out[1 + slot].slot, slot);
  }
  EXPECT_EQ(out[4].kind, TagChips::Kind::Unlabelled);
}
```

`FakePassage` and `candidatesOf` are already defined earlier in the file, in anonymous namespaces.

### Step 3.2 — run it (1 min)

```bash
cmake --build build/test --target TagRowsTest -j 8 && ./build/test/tag_rows/TagRowsTest --gtest_filter='TagChipCandidates.*'
```

Expected: `[  PASSED  ] 7 tests.` (the six existing ones plus this one).

### Step 3.3 — commit (1 min)

```bash
git add test/tag_rows/TagChipRowTest.cpp
git commit -m "test: pin chip candidates on name-ordered tag ids

Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo"
```

---

## Task 4 — `TagChipView::draw` takes a `ChipState`

No host target compiles `TagChipView.cpp` or the activities; this task is checked by the firmware
build in Task 6. Make all three file edits before building, so no commit has a broken call site.

### Step 4.1 — header (2 min)

In `src/activities/reader/TagChipView.h`, replace

```cpp
// Measured bold, the selected weight, so the selection never changes a width or reflows the chips.
int measure(UiAppHost::UiScreen& screen, const char* label, const Metrics& metrics);

void draw(UiAppHost::UiScreen& screen, const freeink::ui::Rect& rect, const char* label, bool selected,
          freeink::ui::ActionId action, int16_t value, uint16_t inputMask, const TagChips::Pad& pad);
```

with

```cpp
// Measured bold, the heaviest weight any ChipState draws, so a state change never changes a width or
// reflows the chips.
int measure(UiAppHost::UiScreen& screen, const char* label, const Metrics& metrics);

void draw(UiAppHost::UiScreen& screen, const freeink::ui::Rect& rect, const char* label, TagChips::ChipState state,
          freeink::ui::ActionId action, int16_t value, uint16_t inputMask, const TagChips::Pad& pad);
```

### Step 4.2 — `draw` (4 min)

In `src/activities/reader/TagChipView.cpp`, replace the whole `draw` function — from
`void draw(UiAppHost::UiScreen& screen, const fui::Rect& rect, const char* label, const bool selected,`
through its closing `}` (the one after `fui::button(screen.frame(), rect, props);`) — with:

```cpp
void draw(UiAppHost::UiScreen& screen, const fui::Rect& rect, const char* label, const TagChips::ChipState state,
          const fui::ActionId action, const int16_t value, const uint16_t inputMask, const TagChips::Pad& pad) {
  const auto& theme = screen.theme();
  fui::StyleSet styles;
  styles.explicitlySet = true;
  fui::BoxStyle& look = styles.normal;
  look.background = fui::Paint::solid(fui::Color::White);
  look.foreground = fui::Paint::solid(fui::Color::Black);
  look.border = fui::Paint::solid(fui::Color::Black);
  look.borderWidth = 1;
  look.radius = static_cast<uint8_t>(std::min(rect.height / 2, 255));
  switch (state) {
    case TagChips::ChipState::Inactive:
    case TagChips::ChipState::Action:
      break;
    case TagChips::ChipState::Active:
      look.background = fui::Paint::solid(fui::Color::Black);
      look.foreground = fui::Paint::solid(fui::Color::White);
      break;
    case TagChips::ChipState::Disabled:
      look.background = fui::Paint::dither(fui::Color::LightGray);
      look.border = fui::Paint::none();
      look.borderWidth = 0;
      break;
  }
  // A tap flash paints the focused style; every slot keeps the chip's own look so a toggled chip is
  // never repainted in the state it just left.
  styles.selected = look;
  styles.focused = look;
  styles.active = look;
  styles.disabled = look;

  fui::ButtonProps props;
  props.label = label;
  props.action = action;
  props.value = value;
  props.inputMask = inputMask;
  // Never fui::StateDisabled: findTouch skips such a hit for every input, so a Disabled chip's tap and
  // long-press would never arrive.
  props.state = state == TagChips::ChipState::Active ? fui::StateSelected : fui::StateNormal;
  props.text = theme.smallText;
  props.text.bold = state == TagChips::ChipState::Active || state == TagChips::ChipState::Action;
  props.styles = styles;
  // The chip already meets minTouchSize (TagChips::chipHeight); growing the hit rect would overlap
  // the neighbouring line, so the padding tiles the gaps instead.
  props.minTouchSize = 0;
  props.hitPadding = fui::Insets{static_cast<int16_t>(pad.top), static_cast<int16_t>(pad.right),
                                 static_cast<int16_t>(pad.bottom), static_cast<int16_t>(pad.left)};
  fui::button(screen.frame(), rect, props);
}
```

`look` is a reference to `styles.normal`, not a new `BoxStyle` local (spec, review 0 m2).

### Step 4.3 — Highlights call site (2 min)

In `src/activities/reader/HighlightsActivity.cpp`, replace

```cpp
    TagChipView::draw(screen, rect, isMore ? fui::TEXT_ELLIPSIS : chips_[static_cast<size_t>(placed.chip)].label,
                      selected, isMore ? ACTION_MORE : ACTION_CHIP, static_cast<int16_t>(isMore ? 0 : placed.chip),
```

with

```cpp
    TagChipView::draw(screen, rect, isMore ? fui::TEXT_ELLIPSIS : chips_[static_cast<size_t>(placed.chip)].label,
                      selected ? TagChips::ChipState::Active : TagChips::ChipState::Inactive,
                      isMore ? ACTION_MORE : ACTION_CHIP, static_cast<int16_t>(isMore ? 0 : placed.chip),
```

The following line (`fui::InputTouch, TagChips::hitPadding(chipLayout_, k, chip.gap));`) is unchanged.

### Step 4.4 — filter grid call site (2 min)

In `src/activities/reader/TagFilterActivity.cpp`, replace

```cpp
    TagChipView::draw(screen, rect, entry.label, TagChipView::isSelected(entry, filter_), ACTION_ROW,
                      static_cast<int16_t>(placed.chip), fui::InputTouch | fui::InputLongPress,
```

with

```cpp
    const TagChips::ChipState state =
        TagChipView::isSelected(entry, filter_) ? TagChips::ChipState::Active : TagChips::ChipState::Inactive;
    TagChipView::draw(screen, rect, entry.label, state, ACTION_ROW, static_cast<int16_t>(placed.chip),
                      fui::InputTouch | fui::InputLongPress,
```

The following line (`TagChips::hitPadding(page_.placed, page_.placedCount, page_.lines, k, gridGap_));`)
is unchanged.

### Step 4.5 — check nothing else calls `draw` with a bool (1 min)

```bash
grep -rn "TagChipView::draw" src
```

Expected: exactly two hits, `HighlightsActivity.cpp` and `TagFilterActivity.cpp`, both edited above.

### Step 4.6 — commit (1 min)

The firmware build in Task 6 verifies this task. If it fails there, fix it in a new commit; do not
rewrite this one.

```bash
git add src/activities/reader/TagChipView.h src/activities/reader/TagChipView.cpp \
  src/activities/reader/HighlightsActivity.cpp src/activities/reader/TagFilterActivity.cpp
git commit -m "feat: draw tag chips in four states

Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo"
```

---

## Task 5 — `STR_TAG_NEW_CHIP`

### Step 5.1 — add the key (1 min)

In `lib/I18n/translations/english.yaml`, replace

```yaml
STR_TAG_NEW: "New tag..."
```

with

```yaml
STR_TAG_NEW: "New tag..."
STR_TAG_NEW_CHIP: "+ New tag"
```

Edit no other YAML and no generated header (`lib/I18n/I18nKeys.h`, `I18nStrings.*` are generated
and gitignored).

### Step 5.2 — check the generator accepts it (1 min)

```bash
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/ 2>&1 | tail -5
grep -n "STR_TAG_NEW_CHIP" lib/I18n/I18nKeys.h
```

Expected: the script finishes without an error, and `I18nKeys.h` has `STR_TAG_NEW_CHIP` (the
standalone run does not strip unused keys; the firmware build will, until the picker uses it).
`git status --short` must not list `lib/I18n/I18nKeys.h` or `I18nStrings.*` (gitignored).

### Step 5.3 — commit (1 min)

```bash
git add lib/I18n/translations/english.yaml
git commit -m "feat: add the + New tag chip string

Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo"
```

---

## Task 6 — verify the whole change

### Step 6.1 — full host suite (3 min)

```bash
cmake -S test -B build/test && cmake --build build/test -j 8 && ctest --test-dir build/test --output-on-failure -j 8 2>&1 | tail -4
```

Expected: `100% tests passed`, 1,678 + 10 = **1,688** tests (5 `TagChipPickerState`, 4
`TagChipFindChip`, 1 `TagChipCandidates`).

### Step 6.2 — firmware build, no new warnings (4 min; longer if the framework rebuilds)

```bash
~/.platformio/penv/bin/pio run -e x4pro > build/pio-btrial-2.log 2>&1; echo exit=$?
grep "warning:" build/pio-btrial-2.log | grep -E "^src/|^lib/" ; echo "repo warnings above (expect none)"
grep -E "^(RAM|Flash):" build/pio-btrial-2.log | tail -2
```

Expected: `exit=0`; no warning lines under `src/` or `lib/` (baseline had none); RAM about 66,220 B
and flash within a few hundred bytes of 5,558,994 B (spec, Testing strategy). A larger flash delta
means something beyond the style table changed — investigate before going on.
`build/` is gitignored; check with `git check-ignore build/pio-btrial-2.log`.

### Step 6.3 — format the whole tree (1 min)

```bash
./bin/clang-format-fix
git status --short
```

Expected: either no change, or changes only in the files this plan declares (include regrouping in
`TagChipRowTest.cpp`, line wrapping). If anything changed, rebuild the host target and rerun
`./build/test/tag_rows/TagRowsTest`, then commit:

```bash
git add -u
git commit -m "style: format the chip states change

Claude-Session: https://claude.ai/code/session_01YarvAZjfcPz19axP8tUPvo"
```

Run `git status --short` again: the tree must be clean, with nothing gitignored staged.

### Step 6.4 — report (1 min)

Report the commits (`git log --oneline origin/feature/tag-chip-states..HEAD`), the ctest summary
line, the pio `exit=` and RAM/Flash lines, and the list of repo warnings (expected empty). Do not
push.
