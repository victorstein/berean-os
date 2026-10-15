# Plan — berean-o-19 (T6): the Predicación Home card and the cover guard

Spec: `docs/superpowers/specs/2026-10-15-berean-o-19-design.md`. It is the source for every
number and name here. The model for the card is the Meetings card throughout.

FILES: src/activities/launcher/HomeLayout.h, src/activities/launcher/HomeTargets.h
FILES: src/activities/launcher/LauncherActivity.cpp, src/activities/launcher/LauncherActivity.h
FILES: src/components/icons/preaching.h
FILES: test/home_layout/HomeLayoutTest.cpp, test/home_layout/HomeTargetsTest.cpp

## Ground rules for every step

- Re-read each file before you edit it. A sibling task may have changed it since this plan was
  written. Every "replace" below quotes the exact current text. If that text is not found, stop and
  report; do not improvise.
- **Code must explain itself.** Use meaningful names. Write a comment only for a non-obvious *why*:
  a constraint, a gotcha or a workaround. Never write one that restates the code or narrates
  history.
- **Repo rules from `CLAUDE.md`:**
  - `tr()` for all user-facing text.
  - All rendering through the existing renderer and box helpers.
  - No hardcoded screen sizes.
  - No new allocations.
  - No `std::function`.
- Commit at the end of each task with the message given. Conventional commits only. **Never push.**
  End every commit message with this trailer line:
  `Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a`
- Do not edit `test/CMakeLists.txt`, `test/home_layout/CMakeLists.txt`, any translation YAML or
  `src/main.cpp`. None of them needs a change.

### How to run the host suite

From the worktree root. `build/` is gitignored (`.gitignore:13`).

```bash
cmake -S test -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --target HomeLayoutTest -j8
./build/host/home_layout/HomeLayoutTest
```

Baseline before any change: `[  PASSED  ] 42 tests.` A compile error counts as "red" for a test
that names something that does not exist yet.

---

## Task 1 — The card's height, boxes and the fifth gap (`HomeLayout.h`)

### Step 1.1 — Failing layout tests

Edit `test/home_layout/HomeLayoutTest.cpp`.

(a) In `FixedSectionHeights`, replace

```cpp
  EXPECT_EQ(HomeLayout::meetingsHeight(LINES), 94);
```

with

```cpp
  EXPECT_EQ(HomeLayout::meetingsHeight(LINES), 94);
  EXPECT_EQ(HomeLayout::preachingHeight(LINES), 63);
```

(b) Replace the whole `TheHeroTakesTheRemainder` test body:

```cpp
TEST(HomeLayout, TheHeroTakesTheRemainder) {
  EXPECT_EQ(layoutFor(LyraMetrics::values).hero.height, 179);
  EXPECT_EQ(layoutFor(BaseMetrics::values).hero.height, 199);
}
```

(c) In `TheGapsLeaveTheSectionsBelowTheHeroInPlace`, replace `? 258 : 280` with `? 187 : 207`.

(d) Replace the whole `TheHeroKeepsItsArtAboveThePlate` test with:

```cpp
TEST(HomeLayout, ThePlateSitsAtTheHerosFoot) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const auto layout = layoutFor(*metrics);
    EXPECT_EQ(bottomOf(layout.plate), bottomOf(layout.hero));
  }
}
```

(e) In `SectionsStackWithoutOverlap`, replace

```cpp
    EXPECT_EQ(layout.icons[0].y, bottomOf(layout.meetings) + gap);
```

with

```cpp
    EXPECT_EQ(layout.preaching.y, bottomOf(layout.meetings) + gap);
    EXPECT_EQ(layout.icons[0].y, bottomOf(layout.preaching) + gap);
```

(f) Directly after the `TheMeetingsCardKeepsTheRangeBelowTheStrip` test (it ends
`EXPECT_LE(bottomOf(layout.meetingsPercent), layout.meetingsRange.y);` then `}`), add:

```cpp

TEST(HomeLayout, ThePreachingCardHoldsItsIconTitleAndSubtitle) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const auto layout = layoutFor(*metrics);
    EXPECT_EQ(layout.preaching.height, 63);
    EXPECT_TRUE(within(layout.preachingIcon, layout.preaching));
    EXPECT_TRUE(within(layout.preachingTitle, layout.preaching));
    EXPECT_TRUE(within(layout.preachingSubtitle, layout.preaching));
    EXPECT_EQ(layout.preachingTitle.width, 408);
    EXPECT_EQ(layout.preachingSubtitle.y, bottomOf(layout.preachingTitle));
    EXPECT_LE(bottomOf(layout.preachingSubtitle) + HomeLayout::PAD, bottomOf(layout.preaching));
    EXPECT_LE(bottomOf(layout.preachingIcon) + HomeLayout::PAD, bottomOf(layout.preaching));
    EXPECT_LE(layout.preachingIcon.x + layout.preachingIcon.width + HomeLayout::PAD, layout.preachingTitle.x);
  }
}
```

Run the host suite. Expected: **compile error**. `preachingHeight`, `preaching`, `preachingIcon`,
`preachingTitle` and `preachingSubtitle` don't exist yet.

### Step 1.2 — Implement the layout

Edit `src/activities/launcher/HomeLayout.h`.

(a) Delete these two lines. The cover guard in Task 2 replaces the floor (spec A5).

```cpp
// Below this the hero's cover is a sliver above its plate.
inline constexpr int MIN_HERO_ART = 96;
```

(b) In `struct Layout`, replace

```cpp
  Box meetingsRange;
  Box icons[ICON_TILES];
```

with

```cpp
  Box meetingsRange;
  Box preaching;
  Box preachingIcon;
  Box preachingTitle;
  Box preachingSubtitle;
  Box icons[ICON_TILES];
```

(c) Directly after the `meetingsHeight` function (it ends
`return PAD + std::max(lines.ui10 + lines.small, stripHeight(lines)) + lines.small + PAD;` then `}`),
add:

```cpp

constexpr int preachingHeight(const LineHeights& lines) {
  return PAD + std::max(ICON, lines.ui10 + lines.small) + PAD;
}
```

(d) In `compute`, replace

```cpp
  const int fixed =
      recentHeight(metrics, lines) + verseCardHeight(lines) + meetingsHeight(lines) + iconRowHeight(lines);
  const int heroHeight = bottom - top - fixed - 4 * gap;
```

with

```cpp
  const int fixed = recentHeight(metrics, lines) + verseCardHeight(lines) + meetingsHeight(lines) +
                    preachingHeight(lines) + iconRowHeight(lines);
  const int heroHeight = bottom - top - fixed - 5 * gap;
```

(e) In `compute`, replace

```cpp
  out.meetingsRange = Box{left + PAD, bottomOf(out.strip), width - 2 * PAD, lines.small};
  y += meetingsHeight(lines) + gap;
```

with

```cpp
  out.meetingsRange = Box{left + PAD, bottomOf(out.strip), width - 2 * PAD, lines.small};
  y += meetingsHeight(lines) + gap;

  out.preaching = Box{left, y, width, preachingHeight(lines)};
  out.preachingIcon = Box{left + PAD, y + PAD, ICON, ICON};
  const int preachingTextWidth = left + width - PAD - textX;
  out.preachingTitle = Box{textX, y + PAD, preachingTextWidth, lines.ui10};
  out.preachingSubtitle = Box{textX, bottomOf(out.preachingTitle), preachingTextWidth, lines.small};
  y += preachingHeight(lines) + gap;
```

`textX` is the local already declared for the meetings card (`left + PAD + ICON + PAD`). Reuse
it. Do not redeclare `textX` or `textWidth`.

Run the host suite. Expected: `[  PASSED  ] 43 tests.` That is 42, plus the new card test.
`ThePlateSitsAtTheHerosFoot` replaced the old test one for one.

### Step 1.3 — Commit

```bash
git add src/activities/launcher/HomeLayout.h test/home_layout/HomeLayoutTest.cpp
git commit -m "feat(home): make room for the Predicación card

Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a"
```

The launcher still builds: nothing reads the new boxes yet, so for now Home shows an empty 63 px
gap above the icon tiles.

---

## Task 2 — The cover guard (`HomeLayoutTest.cpp`)

### Step 2.1 — Write the guard

Edit `test/home_layout/HomeLayoutTest.cpp`.

(a) In the anonymous namespace, after the line
`constexpr int STRIP_DAY_HEIGHT = LINES.small + LINES.ui10;`, add:

```cpp

// The NWT cover as CoverBand thumbnails it for the 464 px hero, and where its
// "LA BIBLIA" ink sits in that thumbnail (measured from the cover JPEG scaled to 535x773).
constexpr int NWT_THUMB_W = 535;
constexpr int NWT_THUMB_H = 773;
constexpr int TITLE_FIRST_ROW = 167;
constexpr int TITLE_LAST_ROW = 214;
```

(b) Directly after the `ThePlateSitsAtTheHerosFoot` test, add:

```cpp

// Every section added to Home shrinks the hero. This fails before a taller section can crop
// the Bible's title out of the artwork above the plate.
TEST(HomeLayout, TheCoverStillShowsTheBibleTitle) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const auto layout = layoutFor(*metrics);
    const int visible = layout.hero.height - layout.plate.height;
    ASSERT_EQ(CoverBandGeometry::thumbHeightFor(layout.hero.width, layout.hero.height), NWT_THUMB_H);
    const auto crop = CoverBandGeometry::crop(NWT_THUMB_W, NWT_THUMB_H, layout.hero.width, layout.hero.height,
                                              visible, CoverBandGeometry::BOOK_TITLE_BAND);
    ASSERT_TRUE(crop.fits);
    EXPECT_LE(crop.yOffset, TITLE_FIRST_ROW);
    EXPECT_GE(crop.yOffset + visible - 1, TITLE_LAST_ROW);
  }
}
```

Run the host suite. Expected: `[  PASSED  ] 44 tests.` The windows are rows 154–231 (Lyra) and
145–241 (Base).

### Step 2.2 — Prove the guard bites

The guard passes against today's layout, so show that each of its two bounds can go red. The
first-row bound is the one a taller card trips (Lyra past +26 px, spec Testing), so prove it with
the card itself:

1. Temporarily change `constexpr int TITLE_LAST_ROW = 214;` to `= 240;`. Run the suite.
   Expected: `TheCoverStillShowsTheBibleTitle` **fails** on the `TITLE_LAST_ROW` check for Lyra,
   whose window ends at 231. Restore `214`.
2. Temporarily change `preachingHeight` in `src/activities/launcher/HomeLayout.h` to return
   `PAD + std::max(ICON, lines.ui10 + lines.small) + PAD + 30;`. Run the suite. Expected:
   `TheCoverStillShowsTheBibleTitle` **fails** on the `TITLE_FIRST_ROW` check for Lyra (visible
   48 px, window starts at 169). Base passes that check (visible 67 px, window starts at 160).
   Other tests that pin the exact heights fail too, which is expected. Restore the `+ 30`-free line.
3. Run the suite again. Expected: `[  PASSED  ] 44 tests.` `git diff` shows only the Step 2.1
   test additions.

### Step 2.3 — Commit

```bash
git add test/home_layout/HomeLayoutTest.cpp
git commit -m "test(home): fail if a section crops the cover's title

Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a"
```

---

## Task 3 — `Target::Preaching` and its route (`HomeTargets.h`, the launcher's switches)

### Step 3.1 — Failing target tests

Edit `test/home_layout/HomeTargetsTest.cpp`.

(a) Directly after the `MeetingsPublicationsAndSettingsNeedNoBible` test, add:

```cpp

TEST(HomeTargets, PreachingOpensWithABibleAndDownloadsWithout) {
  const auto withOne = HomeTargets::route(Target::Preaching, withBible(false, 0, false));
  EXPECT_EQ(withOne.action, Action::OpenPreaching);
  EXPECT_EQ(withOne.intent, Kind::None);
  EXPECT_FALSE(withOne.allowFastInitialRefresh);

  const State noBible{false, true, 3, true};
  EXPECT_EQ(HomeTargets::route(Target::Preaching, noBible).action, Action::DownloadBible);
  EXPECT_TRUE(HomeTargets::isActive(Target::Preaching, noBible));
}
```

(b) Replace the whole `TheRingWalksActiveTargetsInReadingOrder` test with:

```cpp
TEST(HomeTargets, TheRingWalksActiveTargetsInReadingOrder) {
  EXPECT_EQ(ringOf(withBible(true, 3, true)),
            (std::vector<Target>{Target::Continue, Target::GoTo, Target::Recent0, Target::Recent1, Target::Recent2,
                                 Target::Verse, Target::Meetings, Target::Preaching, Target::Tags, Target::Search,
                                 Target::Publications, Target::Settings}));
  EXPECT_EQ(ringOf(withBible(false, 0, false)),
            (std::vector<Target>{Target::Continue, Target::GoTo, Target::Meetings, Target::Preaching, Target::Tags,
                                 Target::Search, Target::Publications, Target::Settings}));
  EXPECT_EQ(ringOf(State{false, false, 0, false}),
            (std::vector<Target>{Target::Continue, Target::Meetings, Target::Preaching, Target::Tags, Target::Search,
                                 Target::Publications, Target::Settings}));
}
```

Run the host suite. Expected: **compile error**, because `Target::Preaching` and
`Action::OpenPreaching` don't exist yet.

### Step 3.2 — Implement the target and the route

Edit `src/activities/launcher/HomeTargets.h`.

(a) In `enum class Target`, replace

```cpp
  Meetings,
  Tags,
```

with

```cpp
  Meetings,
  Preaching,
  Tags,
```

(b) Replace

```cpp
enum class Action : uint8_t { None, OpenReader, DownloadBible, OpenMeetings, OpenPublications, OpenSettings };
```

with

```cpp
enum class Action : uint8_t {
  None,
  OpenReader,
  DownloadBible,
  OpenMeetings,
  OpenPreaching,
  OpenPublications,
  OpenSettings
};
```

clang-format may put it back on fewer lines. That's fine.

(c) In `route`, replace

```cpp
    case Target::Meetings:
      return Route{Action::OpenMeetings};
```

with

```cpp
    case Target::Meetings:
      return Route{Action::OpenMeetings};
    case Target::Preaching:
      return state.hasBible ? Route{Action::OpenPreaching} : Route{Action::DownloadBible};
```

Run the host suite. Expected: `[  PASSED  ] 45 tests.`

### Step 3.3 — Cover the new enumerators in the launcher's switches

Edit `src/activities/launcher/LauncherActivity.cpp`. Both switches are over the full enum. Without
these cases the firmware build warns under `-Wswitch`, and a tap or Confirm on the card would do
nothing.

(a) In `LauncherActivity::boxFor`, replace

```cpp
    case Target::Meetings:
      return &layout.meetings;
```

with

```cpp
    case Target::Meetings:
      return &layout.meetings;
    case Target::Preaching:
      return &layout.preaching;
```

(b) In `LauncherActivity::activate`, replace

```cpp
    case HomeTargets::Action::OpenMeetings:
      openMeetings();
      return;
```

with

```cpp
    case HomeTargets::Action::OpenMeetings:
      openMeetings();
      return;
    case HomeTargets::Action::OpenPreaching:
      activityManager.goToPreaching();
      return;
```

`goToPreaching` is declared in `src/activities/ActivityManager.h:88` with a defaulted
`PreachingOrigin`. `activityManager` is the same member `OpenSettings` uses
(`activityManager.goToSettings()`), so no new include is needed. If the build later reports
`goToPreaching` undeclared, add `#include "activities/ActivityManager.h"` with the other
`activities/` includes. Do not add it otherwise.

### Step 3.4 — Commit

```bash
git add src/activities/launcher/HomeTargets.h src/activities/launcher/LauncherActivity.cpp test/home_layout/HomeTargetsTest.cpp
git commit -m "feat(home): route the Predicación card

Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a"
```

---

## Task 4 — The icon (`src/components/icons/preaching.h`)

Create `src/components/icons/preaching.h` with exactly this content. The bytes are Lucide
`messages-square`, rasterised at 32 px on white, thresholded at luminance 110, and packed in
`drawIcon`'s rotated layout. They were round-trip checked when this plan was written: decoded through
`drawIcon`'s mapping, they print two upright speech bubbles (spec A8, A9).

```cpp
#pragma once
#include <cstdint>

// size: 32x32
static const uint8_t PreachingIcon[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x07, 0xFF, 0xF0, 0x00, 0x03, 0xFF, 0xE0, 0x00, 0x03, 0xFF, 0xE3, 0xFF,
    0xE3, 0xFF, 0xE7, 0xFF, 0xC3, 0xFF, 0xFF, 0xFF, 0x87, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x1F, 0xF8, 0x03,
    0xFF, 0x1F, 0xE0, 0x00, 0xFF, 0x1F, 0xC0, 0x00, 0x7F, 0x1F, 0xCF, 0xFC, 0x7F, 0x1F, 0x8F, 0xFE, 0x7F, 0x1F, 0x8F,
    0xFE, 0x7F, 0x1F, 0x8F, 0xFE, 0x7F, 0x1F, 0x8F, 0xFE, 0x7F, 0x1F, 0x8F, 0xFE, 0x7F, 0x1F, 0x8F, 0xFE, 0x7F, 0x1F,
    0x8F, 0xFE, 0x7F, 0x3F, 0x8F, 0xFE, 0x70, 0x3F, 0x8F, 0xFE, 0x70, 0x7F, 0x8F, 0xFE, 0x79, 0xFF, 0x8F, 0xFE, 0x7F,
    0xFF, 0x8F, 0xFE, 0x3F, 0xFF, 0x8F, 0xFF, 0x1F, 0xFF, 0x8F, 0xFF, 0x8F, 0xFF, 0xCF, 0xFF, 0xC7, 0xFF, 0xC0, 0x00,
    0x07, 0xFF, 0xE0, 0x00, 0x07, 0xFF, 0xF8, 0x00, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
```

### Step 4.1 — Check the bytes before committing

Run this from the worktree root. It must print the icon upright, and then `128` on the last line:

```bash
python3 -I - src/components/icons/preaching.h <<'EOF'
import re, sys
data = [int(v, 16) for v in re.findall(r"0x[0-9A-F]{2}", open(sys.argv[1]).read())]
size = 32
screen = [[" "] * size for _ in range(size)]
for row in range(size):
    for col in range(size):
        if (data[row * 4 + (col >> 3)] >> (7 - (col & 7))) & 1 == 0:
            screen[col][size - 1 - row] = "#"
print("\n".join("".join(line) for line in screen))
print(len(data))
EOF
```

Expected: a speech bubble at the top left, with a second bubble behind it to the lower right. The
last line is `128`.

### Step 4.2 — Commit

```bash
git add src/components/icons/preaching.h
git commit -m "feat(home): add the Predicación icon

Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a"
```

---

## Task 5 — Draw the card (`LauncherActivity.{h,cpp}`)

There's no host test for drawing; it needs the renderer and is checked on the device. The boxes it
draws into are pinned by Task 1's tests.

### Step 5.1 — Declare `drawPreaching` and update the class comment

Edit `src/activities/launcher/LauncherActivity.h`.

(a) Replace

```cpp
// bereanOS's home screen (issue #203, Direction B v2): the Bible's cover with
// Continue and Go to on its plate, the recent places, a verse from the user's
// tags, this week's meetings, and Tags, Search, Publications and Settings.
```

with

```cpp
// bereanOS's home screen (issue #203, Direction B v2): the Bible's cover with
// Continue and Go to on its plate, the recent places, a verse from the user's
// tags, this week's meetings, Predicación, and Tags, Search, Publications and
// Settings.
```

(b) Replace

```cpp
  void drawMeetings() const;
```

with

```cpp
  void drawMeetings() const;
  void drawPreaching() const;
```

### Step 5.2 — Define and call it

Edit `src/activities/launcher/LauncherActivity.cpp`.

(a) Replace

```cpp
#include "components/icons/library.h"
```

with

```cpp
#include "components/icons/library.h"
#include "components/icons/preaching.h"
```

(b) Directly after the end of `LauncherActivity::drawMeetings()`, and before
`void LauncherActivity::drawIconTile(`, add:

```cpp

void LauncherActivity::drawPreaching() const {
  const Box& card = layout.preaching;
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, isSelected(Target::Preaching) ? 3 : 1, RADIUS,
                           true);
  renderer.drawIcon(PreachingIcon, layout.preachingIcon.x, layout.preachingIcon.y, HomeLayout::ICON);
  renderer.drawText(UI_10_FONT_ID, layout.preachingTitle.x, layout.preachingTitle.y, tr(STR_PREACHING), true,
                    EpdFontFamily::BOLD);
  drawTextIn(renderer, SMALL_FONT_ID, layout.preachingSubtitle, tr(STR_PREACHING_SUBTITLE), 0, true);
}
```

(c) In `LauncherActivity::render`, replace

```cpp
  drawMeetings();
  drawIconTile(layout.icons[0], BookmarkIcon, tr(STR_TAGS), isSelected(Target::Tags));
```

with

```cpp
  drawMeetings();
  drawPreaching();
  drawIconTile(layout.icons[0], BookmarkIcon, tr(STR_TAGS), isSelected(Target::Tags));
```

### Step 5.3 — Build the firmware

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro.log | tail -5
grep -nE "warning:" build/pio-x4pro.log | grep -E "launcher/|icons/preaching" || echo "no warnings in touched files"
```

Expected: `SUCCESS`, then `no warnings in touched files`. Don't run two `pio run` at once; they race
on `idf_component.yml`. If `pio` reports a missing submodule or dependency, run `./bin/bootstrap` and
retry.

### Step 5.4 — Commit

```bash
git add src/activities/launcher/LauncherActivity.h src/activities/launcher/LauncherActivity.cpp
git commit -m "feat(home): add the Predicación card

Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a"
```

---

## Task 6 — Final verification

### Step 6.1 — Format the whole tree

```bash
git status --short
./bin/clang-format-fix
git status --short
```

All new files are already committed, so the unsuffixed form reaches them. If formatting changed
anything, re-run the host suite (Step 6.2) and then commit:

```bash
git add -u
git commit -m "style: clang-format the Predicación Home card

Claude-Session: https://claude.ai/code/session_01VovztC6xbLTvtsx31ZEb9a"
```

If nothing changed, skip that commit.

### Step 6.2 — Host suite

```bash
cmake --build build/host --target HomeLayoutTest -j8 && ./build/host/home_layout/HomeLayoutTest
```

Expected: `[  PASSED  ] 45 tests.` That is the 42 baseline, plus the card layout test, the cover
guard and the Preaching route test.

### Step 6.3 — Clean tree

`git status --short` shows nothing tracked as modified. `build/` is ignored.

**What only the owner can check on the device, at the end of the batch:**
- The cover still reads "LA BIBLIA" under Lyra and under Base.
- The card shows the two-bubble icon, "Predicación", and "Preguntas de la Biblia · tus temas"
  untruncated.
- A tap on the card opens Predicación, and so does Confirm with the card selected in the ring.
- With no Bible on the card, the card opens the Bible download.
