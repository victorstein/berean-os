# Spec review 0 (claims): alphabetical tags and a chip-grid tag picker

Reviewed: `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (untracked, as of
`main` at 3f704d2e). This pass checks the spec's claims only: its file and line citations, its
statements about how the code works today, its numbers, and its "the only X" claims. Design
opinions belong to the separate design review.

Severity: **BLOCKER** means the design as written depends on the claim. **MAJOR** means a
factual error that changes scope, build or resource accounting. **MINOR** means citation drift
or an error the design survives.

## Wrong or unverifiable

### W1. BLOCKER: the header's right label is not a header-height slot beside the battery (§4 "Done pill", A6)

The spec says `BaseTheme::drawHeader` "computes the battery reserve inline
(`BaseTheme.cpp:313-321`) and places the right label beside it", and that
`GUI.headerRightSlot` will return "the same rect the right label would use". It also says the
pill "is the header's height: 44 px under Lyra … and 45 px under the base theme". The code does
not support either statement.

- **Classic (base) theme.** The battery strip is shorter than the band. `batteryBarHeight = 20`
  (`BaseTheme.h:135`) and `headerHeight = 45` (`:136`), so
  `const bool batteryLeavesCornerFree = metrics.batteryBarHeight < rect.height;`
  (`BaseTheme.cpp:335`) is true. That makes `manualRightLabel` true (`:336`), and
  `props.rightLabel = nullptr;` (`:338`) takes the label away from the component. The label is
  then drawn by hand in the band's **lower-right corner, under the battery**, at its text size:

  ```cpp
  const fui::Rect labelRect{static_cast<int16_t>(band.right() - tokens.headerSidePadding - labelSize.width),
                            static_cast<int16_t>(band.bottom() - tokens.headerUnderline - tokens.spaceSm - labelH),
                            labelSize.width, labelH};
  ```

  (`BaseTheme.cpp:393-395`). The comment at `:328-334` states this intent: "draw the label
  manually below instead, pinned in the band's lower-right corner, so it holds one position
  across battery states". Its x does not depend on `batteryReserve` at all. It shares its right
  edge with the battery (`band.right() - batteryEdgeInset - batteryReserve`, `:385-386`), which
  sits in the top 20 px. So under Classic the rect the right label uses is not beside the battery,
  and it is one small-text line tall, not 45 px.
- **Lyra.** `batteryBarHeight = 44` equals `headerHeight = 44` (`LyraTheme.h:12-13`), so the
  component places the label. `content.width` loses `rightReserve` (`header.h:78`), and the label
  rect is `Rect rightRect{content.right() - rightSize.width, titleTop + titleLh - rightSize.height, rightSize.width, rightSize.height}`
  (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/header.h:138-147`). It is
  beside the reserve, as the spec says. It is also text-sized and bottom-aligned to the title
  line, not header-height.

So "the same rect the right label would use" and "the header's height, just left of the
battery" describe different rects in both themes. In Classic, a rect shaped like the right label
does not sit beside the battery at all. A6 ("in both themes, with and without the battery
percentage") fails for Classic as written. Classic is a user-selectable theme: Lyra is the
default (`CrossPointSettings.h:331`), and `SettingsList` offers both (`CrossPointSettings.h:231-234`).

### W2. MAJOR: three other host-test targets compile `TagPalette.cpp` and would stop linking (§1, §6)

The spec names one CMake change: "`test/tag_palette`, whose CMake target adds
`lib/BibleSearch/BibleSearch/Fold.cpp`". `TagPalette.cpp` is compiled into **four** host
executables:

- `test/tag_palette/CMakeLists.txt:3`
- `test/migration_planner/CMakeLists.txt:7`
- `test/passage_doc/CMakeLists.txt:7`
- `test/storage_io/CMakeLists.txt:21`

None of them compiles `Fold.cpp` or has `lib/BibleSearch` on its include path. Only
`bible_search_index:6`, `bible_search_fold:3` and `ui_layout:37` compile it. Once
`TagPalette.cpp` calls `BibleSearch::foldAppend`, the MigrationPlanner, PassageDoc and StorageIo
suites fail to compile or link.

The precedent the spec cites is also weaker than it looks. `TagPalette.cpp:3` includes
`<FormatVersion.h>`, which is header-only (`constexpr bool isKnownFormatVersion`,
`FormatVersion.h:14`) and adds no link dependency. The real precedent is `PassageDoc.cpp:4`
`#include <Utf8.h>`: every target that compiles `PassageDoc.cpp` also lists
`lib/Utf8/Utf8.cpp` (`passage_doc/CMakeLists.txt:8`, `storage_io/CMakeLists.txt:25`).

### W3. MAJOR: not every reader of stored tag order sorts, because the sleep screen reads the first stored tag (§4 "Exits", D1)

The spec says "The result is a `TagSelectionResult` of ids in display order … the stored order
does not matter now that every reader sorts", and D1 says the order applies on "every tag
surface". The study sleep screen shows one tag pill, and it picks the passage's **first stored**
tag:

```cpp
uint16_t tag = 0;
for (const JsonVariantConst id : row["t"].as<JsonArrayConst>()) {
  const uint32_t raw = id | 0u;
  if (raw >= 1 && raw <= UINT16_MAX) {
    tag = static_cast<uint16_t>(raw);
    break;
  }
}
```

(`src/activities/boot_sleep/StudyPassageScan.cpp:81-87`), then
`content.tagName = tagName(passage.tag);` (`StudySleepScreen.cpp:266`).

Today the picker commits ids in palette order: "Reported in palette order rather than the order
they were checked" (`TagPickerActivity.cpp:218-222`). So the pill shows the earliest-allocated
tag. Under the spec it would show the alphabetically first tag, but only for passages re-tagged
after the change. Passages that are not re-tagged keep the old choice. This surface is missing
from D1's list and from §5. I found no other reader that depends on order: Highlights uses
`.tags` only for `passageMatches` and `initialSelection` (`HighlightsActivity.cpp:76`, `:475`),
and `TagChips::count` only tallies.

### W4. MAJOR: picker `chips_` lives in internal SRAM at real palette sizes, not in PSRAM (§6 memory table)

The arithmetic is right: `sizeof(ChipEntry)` is 44 B (measured on the host with the same layout),
and 201 × 44 = 8,844 B. The placement is wrong for the size that matters. The table says
"PSRAM (over 4 KB, `sdkconfig.x4pro:2146-2147`), as the filter grid's is today". But
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (`sdkconfig.x4pro:2147`) sends a block to PSRAM only
above 4,096 B, which takes more than 93 chips (4,096 / 44 = 93.1).

With the owner's 48-tag palette ("The user's real palette is 48", `TagPalette.h:36`), the
picker's `chips_` is 49 × 44 = **2,156 B**. The filter grid's today is 50 × 44 = **2,200 B**
(All + 48 + Unlabelled). Both are under 4,096 B, so both are internal SRAM.
`out.reserve(picked.size())` (`TagChipView.cpp:23`) allocates exactly that. The design survives,
but the resource justification CLAUDE.md requires should say internal SRAM at about 2.2 KB.

### W5. MINOR: two active entries can share a raw name (§1 "Collation key")

The spec says "Two active entries cannot share a raw name, because `add` dedupes by exact name
(`TagPalette.cpp:15-19`)". `add` does dedupe that way. `fromJson` does not: it dedupes by id only,
`for (const auto& e : entries_) duplicate = duplicate || e.id == tagId;` (`:96`). It also
truncates, `if (text.size() > MAX_TAG_NAME_BYTES) text.resize(MAX_TAG_NAME_BYTES);` (`:92`). A
hand-edited file, or two names that become equal once cut, therefore loads as two active entries
with the same name. Nothing depends on this, because the spec's final tiebreak on the raw id
already makes the order total.

### W6. MINOR: Highlights does not read `tagNamesFor` on its render path (§1 "Threading")

`tagNamesFor` is reached only through `tagsValueFor` (`HighlightsActivity.cpp:94`), from
`rebuildRowItems` (`:106`). Every caller of that (`:47`, `:168`, `:202`, `:506`, `:552`) runs on
the loop task under `RenderLock`. `buildScreen` (`:611-722`) reads only `STUDY.passages()`
(`:622`). The error is conservative: the threading conclusion holds more strongly than stated.

### W7. MINOR: the X4 Pro does have a logical Confirm, so A7 is right for a different reason

"The X4 Pro has no physical Confirm button" is true: "BTN_BACK and BTN_CONFIRM are
PIN_UNASSIGNED on this board" (`lib/hal/HalGPIO.h:59`). But a logical Confirm exists:

- A Right nav key held for at least 850 ms is synthesised as Confirm:
  `case BTN_CONFIRM: return right == NavEvent::Synth;` (`HalGPIO.cpp:213-214`), with
  `HOLD_MS = 850` (`lib/Input/Input/NavKeyGestures.h:41`).
- Power acts as Confirm when the setting is `PWR_CONFIRM` (`MappedInputManager.cpp:247-252`,
  `:259`, `:267`).

The held-Confirm retire is unreachable because of held time, not because Confirm is missing:

- A synthesised event reports `getHeldTime()` as `SYNTHETIC_HELD_MS = 40`
  (`NavKeyGestures.h:68`, `HalGPIO.cpp:253-254`).
- A Power click is capped at `X4PRO_POWER_CLICK_MAX_HOLD_MS = 300` (`main.cpp:59`, `:200`).

Both are under `ENTER_DELETE_MODE_MS = 700` (`TagPickerActivity.cpp:21`), so A7's conclusion
holds. The same evidence makes A3's "Confirm on a focused chip" a live path:
`UiListActivity::handleButtons` calls `activateIndex(selected)` on Confirm
(`UiListActivity.cpp:53-56`).

### W8. MINOR: Home is not the only way a button user commits today (§3 "Done is not in the button ring")

The spec says "Home is how the picker commits for a button user today
(`TagPickerActivity.cpp:207-214`), so keeping Done out of the ring loses nothing". There is a
second path: step to row 0 and synthesise Confirm (W7). `handleButtons` then calls
`activateIndex(0)` (`:304-315`), which commits through `if (row == DONE_ROW) { commitAndFinish(); … }`
(`:119-122`). The new design drops that path. Home remains, so the loss is small, but it is not
nothing.

### W9. MINOR: the tag summary joins names in stored order, not the order they were applied (§Problem 1)

`tagNamesFor` walks `passages_.passages()[passageIndex].tags` (`StudyStore.cpp:187`). For any
passage the picker set, that is palette (allocation) order (`TagPickerActivity.cpp:218-222`), not
the order the user checked the tags.

### W10. MINOR: citation drift (the claims hold, the lines are off)

- `TagChipView.cpp:86-88` ("Today both are the inverted pill"): `focused` is set at `:88` and
  `active` at `:89`.
- `TagFilterActivity.h:62-68` (the RenderLock rules): the page-geometry rule is at `:70-75`,
  outside the range.
- `TagFilterActivity.cpp:57-118`, `:201-259` are cited for "page strip … and the
  retire-confirmation `render()` override". The strip is drawn in `drawFooter` (`:120-132`), and
  the override is at `:261-278`; §3 cites `:261-278` correctly.
- `FreeInkApp.h:64-66` is cited for "content margins do not clip drawing or hits". Those lines
  only show `setContentMargin` narrowing `content_`. The claim does hold:
  - `Frame::hit` passes the rect straight to `addInteraction` (`FreeInkUICore.h:1417-1421`).
  - `InteractionBuffer::addInteraction` stores it unclipped (`:1014-1023`).
  - The device `safeArea` defaults to zero insets (`:135`, `:139`).
  - `button.h` draws inside the rect it is given.
- `FreeInkUICore.h:512-517`, given for "the theme's LightPill look", is `Paint::dither` itself.
  The LightPill expansion is `FreeInkApp.h:262-265` (`selected.background = Paint::dither(Color::LightGray)`,
  foreground black). It says nothing about borders, so "no border" is the spec's own choice.

### U1. UNVERIFIABLE: "~200 short-lived `std::string`s per frame" (§1 "Why cache")

This is the cost of an alternative that was never built. It also overstates: libstdc++'s
small-string buffer holds 15 bytes, so names up to 15 bytes would not allocate. The argument for
caching stands without this number.

### U2. UNVERIFIABLE: A5, the order the owner expects

That is a preference. What `foldAppend` does is confirmed below. Two further properties are
facts: ASCII punctuation and spaces pass through unchanged (`Fold.cpp:113-115`), and non-Latin
codepoints and `¡`/`¿` (U+00A1/U+00BF, below `LATIN_FIRST = 0x00C0`) pass through as raw bytes
(`:116-119`), so they sort after every ASCII letter.

## Found while checking completeness (information, not claim errors)

- **FreeInkUI's header already has a trailing action button.** It takes `trailingLabel` and
  `trailingAction`, is drawn at `rect.right() - 4 - btnW` and is `rect.height - 8` tall
  (`header.h:101-125`). `GUI.drawHeader` cannot use it as it stands: it renders through a
  non-interactive `GfxRendererFrame<1>` ("Non-interactive frame: no hit rects registered",
  `BaseTheme.cpp:287-292`).
- **Adding `TagChips::Kind::NewTag` reaches two `switch`es with no `default`**
  (`TagChipView.cpp:27-40` and `:46-53`). Under the repo's `-Wall`
  (`scripts/enable_repo_warnings.py`) that is a `-Wswitch` warning, and CLAUDE.md says not to
  commit when the build warns.
- **A proposed test already exists.** "`candidates` still pins All first and Unlabelled last" is
  `TagChipRowTest.cpp:342` `TagChipCandidates.AllThenTagsInActiveOrderThenUnlabelled`, and
  `candidates` does not care what order the ids arrive in.
- **The two grids lock retirement differently today.** The picker retires under `RenderLock`
  (`TagPickerActivity.cpp:270-271`). The filter grid calls `STUDY.retireTag(id)` with no lock
  (`TagFilterActivity.cpp:188`) and takes the lock only for `filter_` (`:192-193`). The base
  class will own one retire path. The spec states only the picker's locking.
- **`StudyStore::mutablePalette()`** (`StudyStore.h:172`, "For the migration runner") returns a
  non-const reference and has no callers. Any future caller still mutates through
  `add`/`retire`/`fromJson`. Only an assignment through it would bypass them, and implicit
  copy/move would carry a member cache along.

## Confirmed

Confirmed: 70 claims. These are listed so the next pass doesn't re-litigate them.

**Ordering, palette, store**

1. `TagPalette.h:57-58`: "Active tags in allocation order, for a picker."
2. `TagPalette.cpp:57-64`: `activeIds` walks `entries_` in insertion order.
3. `StudyStore.cpp:159-163`: `activeTags` iterates `palette_.activeIds()`.
4. `TagChipView.cpp:16`: `buildEntries` reads `STUDY.palette().activeIds()`.
5. `buildEntries` feeds both the Highlights row (`HighlightsActivity.cpp:133`) and the filter
   grid (`TagFilterActivity.cpp:42`).
6. `activeTags()` has one consumer, the picker (`TagPickerActivity.cpp:27`, `:35`, `:79`,
   `:166`, `:272`).
7. The only readers of `activeIds()` in `src/`, `lib/` and `test/` are `StudyStore.cpp:161` and
   `TagChipView.cpp:16`.
8. `tagNamesFor` has one caller, `HighlightsActivity.cpp:94`.
9. `StudyStore.h:42-49`: the `activeTags` comment says "allocation order".
10. `StudyStore.h:59-61`: retired tags still resolve in `tagNamesFor`.
11. `TagPalette.cpp:15-19`: `add` dedupes by exact name and revives a retired name.
12. `retire` is at `TagPalette.cpp:29-33` and `fromJson` at `:78-106`. `fromJson`'s early
    `false` returns (`:79`, `:81`) leave `entries_` untouched.
13. A2: `add`, `retire` and `fromJson` are the only mutations of `StudyStore::palette_`. There is
    no assignment to `palette_` anywhere. `TagPaletteFile::load` only calls `fromJson`
    (`TagPaletteFile.cpp:18-23`). `closePublication` does not touch the palette
    (`StudyStore.cpp:99-108`). `MigrationRunner.cpp:209`, `StudySleepScreen.cpp:169` and
    `MigrationPlanner.cpp:24` use their own instances through `fromJson`/`add`.
14. `addTagName`'s rollback calls `retire` (`StudyStore.cpp:254-257`).
15. `foldAppend` lower-cases ASCII, strips Latin-1 and Extended-A diacritics, and folds ñ→n
    (`Fold.h:18-23`; `Fold.cpp:61`, `:115-117`).
16. `foldAppend` is `BibleSearch::foldAppend` (`Fold.h:11`, `:23`).
17. `TagPalette.cpp:3` includes `<FormatVersion.h>` (true as written; see W2 for why it is a weak
    precedent).
18. `sdkconfig.x4pro:2146-2147`: `CONFIG_SPIRAM_USE_MALLOC=y`,
    `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`.
19. The rank cache for 48 entries is 48 × 2 = 96 B, "about 100 B". It stays under 4 KB:
    `DEFAULT_SAVE_BUDGET = 45000` (`SaveBudget.h`) holds the file to about 1,800 minimal entries,
    about 3.6 KB of cache.
20. `TagPickerActivity.cpp:79`: the picker refreshes `tags_` on every `buildScreen`.
21. `TagChips::candidates` pins All first (`TagChipRow.h:105`) and Unlabelled last (`:113`), with
    the tags in the order given (`:97-101`, `:107-111`).
22. The picker mutates the palette under `RenderLock` (`TagPickerActivity.cpp:160-167`,
    `:264-277`).
23. Both grids render from their chip snapshot. The filter grid's `buildScreen` reads `chips_`,
    `widths_` and `filter_` only.
24. Highlights never mutates the palette: it has no `addTagName` or `retireTag` call.
25. The owner's palette is 48 tags (`TagPalette.h:36`).

**Chip view, states, FreeInkUI**

26. `TagChipView.h:43-44`: `draw(..., bool selected, ...)`.
27. `TagChipView.h:40-41`: chips are measured bold so selection never reflows them
    (`TagChipView.cpp:67-72`).
28. Inactive matches today's unselected chip: white fill, 1 px black border, regular weight
    (`TagChipView.cpp:79-82`, `:99`).
29. Active matches today's selected chip: black fill, white bold text (`:84-86`, `:99`).
30. Today `focused` and `active` both use the inverted pill (`:88-89`).
31. `findTouch` skips `StateDisabled` for every input kind, long-press included
    (`FreeInkUICore.h:1165-1170`). The long-press path dispatches through it (`:1248-1249`).
32. `Paint::dither` exists (`FreeInkUICore.h:512-517`) and so does `Color::LightGray`
    (`:318-324`). LightPill is "LightGray dither fill, text stays black" (`:624`).
33. `TagChipView.h:17-22`: `ChipEntry{kind, id, label[40]}`.
34. `TagChipRow.h:89`: `enum class Kind : uint8_t { All, Tag, Unlabelled }`.
35. The only callers of `TagChipView::draw` are `HighlightsActivity.cpp:707` and
    `TagFilterActivity.cpp:100`.

**Grid and base class**

36. `TagFilterActivity.cpp:57-118` builds the grid and `:201-259` handles swipe and button paging
    (see W10 for drift).
37. `TagFilterActivity.cpp:106-117`: the focus ring is drawn in the gap.
38. `TagFilterActivity.cpp:156-199` and `:261-278`: long-press retire, the `saveDisabled` guard,
    the popup and the `render()` override.
39. `TagFilterActivity.cpp:161`: long-press acts only on `Kind::Tag`.
40. `TagFilterActivity.cpp:46-47`: the selection clamps after a rebuild.
41. Both activities pass `wantsTouchLongPress = true` (`TagFilterActivity.cpp:22`,
    `TagPickerActivity.cpp:26`).
42. Back returns a cancelled result in both (`TagFilterActivity.cpp:134-139`,
    `TagPickerActivity.cpp:200-205`).
43. Home cancels the filter grid (`TagFilterActivity.cpp:141-146`).
44. The `x/y` page strip appears only with more than one page (`TagFilterActivity.cpp:79-84`,
    `:123`).
45. The filter grid has no held-Confirm path; it inherits `UiListActivity::handleButtons`
    (`UiListActivity.cpp:48-59`).
46. `TagFilterActivity.h:14-25` is the class comment.
47. `TagChipRow.h:204-211`: `GridPage` is a member, never a render-task local. Measured size is
    1,036 B, "~1 KB".
48. `TagChipRow.h:197`: `GRID_MAX_CHIPS = 64`.
49. `UiAppHost.h:35`: `MAX_INTERACTIONS = 96`. 64 + 1 = 65 < 96.
50. `UiListActivity.h:26-28`: `ACTION_ROW = 1`, `ACTION_USER = 2`.
51. `UiTabListActivity.h:25-26`: `ACTION_TAB = ACTION_USER`, a `UiListActivity` base for one
    screen shape.
52. `HighlightsActivity.h:103` declares `ACTION_CHIP = ACTION_USER`, and
    `HighlightsActivity.cpp:42` registers it with `app.on`.
53. TagFilterActivity's chip grid arrived in PR #245 (`b5425810`).
54. `TagRowMapping.h:3-7` states the offset rationale. Its only users are
    `TagPickerActivity.cpp:12` and `:54`, and `test/tag_rows/TagRowsTest.cpp`, which tests
    nothing else.
55. Content margins do not clip drawing or hits (evidence in W10). The header is painted before
    `renderUi` (`TagFilterActivity.cpp:264-266`).

**Picker today**

56. `TagPickerActivity.cpp:80-109`: Done, a toggle per tag, then "New tag...".
57. `TagPickerActivity.cpp:322-341`: every render clears and redraws the whole frame.
58. `TagPickerActivity.cpp:144-181`: the new-tag flow. At the cap a new tag is left unapplied with
    no message (`:173`), and a name that dedupes selects the existing tag.
59. `TagPickerActivity.cpp:183-198`: the add-failure messages.
60. `TagPickerActivity.cpp:207-214`: Home commits, and does nothing while the popup is open.
    `ActivityManager.cpp:75-80` routes `wasHomeGesture` there, and `wasHomeGesture` is the
    capacitive Home key on this board (`MappedInputManager.cpp`, `gpio.hasHomeKey()`).
61. `TagPickerActivity.cpp:216-225`: the result is built in palette order.
62. `TagPickerActivity.h:110`: `rowItems_[MAX_ROWS]`, with `MAX_ROWS = 200 + 2 = 202` (`:57`).
63. `TagPickerActivity.cpp:298-320` and `:304-316`: the held-Confirm retire.
64. The callers are `PassageSelectActivity.cpp:330` and `HighlightsActivity.cpp:476`.
65. `ActivityResult.h:81-88`: `TagSelectionResult` (struct at `:85-87`).

**Numbers, strings, themes**

66. `MAX_TAGS_PER_PASSAGE = 8` (`PassageDoc.h:45`), `MAX_ACTIVE_TAGS = 200`
    (`TagPalette.h:38`), `MAX_TAG_NAME_BYTES = 24` (`:39`). A 40-byte label holds a 24-byte
    name.
67. `ChipEntry` is 44 B (measured), and 201 × 44 = 8,844 B ≈ 8.8 KB.
68. `STR_TAG_NEW: "New tag..."` is at `english.yaml:373`, defined only in `english.yaml` and used
    only at `TagPickerActivity.cpp:100`. `STR_TAG_LIMIT_PER_HIGHLIGHT` exists (`:378`, used at
    `TagPickerActivity.cpp:135`), and so does `STR_TAGS` (`:372`).
69. Both themes put the battery on the right: `headerBatterySide = 0`, "0 = right edge"
    (`BaseTheme.h:59`, `:154`; `LyraTheme.h:32`). Lyra does not override `drawHeader`.
    `BaseTheme.cpp:313-321` computes the battery reserve inline.
70. The header height is 44 under Lyra (`LyraTheme.h:13`) and 45 under the base theme
    (`BaseTheme.h:136`). `drawChrome` passes `metrics.headerHeight` (`UiListActivity.cpp:146`).
    `minTouchSize` defaults to 44 (`FreeInkUICore.h:637`) and nothing in `src/` overrides it,
    so both heights meet it.

The mockups directory `.superpowers/brainstorm/32741-1791777805/content/` exists, holding
`chip-states.html`, `picker-layout.html` and `waiting.html`.
