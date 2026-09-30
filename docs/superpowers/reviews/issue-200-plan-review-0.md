Tier: heavy

# Issue #200 plan review 0

- **Plan:** `docs/superpowers/plans/2026-09-30-issue-200-plan.md`
- **Spec:** `docs/superpowers/specs/2026-09-30-issue-200-design.md`
- **Method:** I checked every code block against the tree on this branch. That covered the base classes
  (`UiListActivity`, `UiAppHost`, `FreeInkApp`, `list.h`, `FreeInkUICore.h`), the reader call sites, `HighlightsActivity`,
  `StudyStore::passagesInDocument`, the icon generator, `listIcons.h`, the translation files, `USER_GUIDE.md` and
  `test/ui_layout/CMakeLists.txt`. I also recomputed the arithmetic in both host tests by hand.

## Findings

No BLOCKER and no MAJOR. Three MINORs, most severe first.

### MINOR 1: the tag-filter rebuild site does not do the locked swap the spec requires

- **Claim.** Spec A-14 (spec:268-277, revision note spec:29-30) says every rebuild site uses the split: compute with no
  lock held, then swap `visibleIndices_` and call `rebuildRowItems()` under `RenderLock`. The spec names the tag-filter
  result at `HighlightsActivity.cpp:139` as one of those sites.
- **Problem.**
  - Step 5.2 does apply the lock to `applyTagEdit` (plan:1083-1095) and to `deleteHighlight` (plan:1109-1116).
  - For the tag-filter handler it swaps unlocked: `visibleIndices_ = computeVisibleIndices(); rebuildRowItems();`
    (plan:1060-1063).
  - That keeps today's unlocked rebuild (`HighlightsActivity.cpp:139-141`), but it does not match the spec. The plan's
    "four departures" list (plan:20-35) does not record it.
  - `rebuildRowItems` refills the `rowItems_` vector that `buildScreen` hands to the render task. The code's own comment
    at `HighlightsActivity.cpp:358-361` explains why that has to happen under the lock.
- **Evidence.**
  - plan:1050-1064
  - spec:271-275
  - `src/activities/reader/HighlightsActivity.cpp:139-141,356-365`
- **Fix.** Use the same shape as `applyTagEdit` in the handler:
  1. Compute with no lock held: `std::vector<size_t> next = computeVisibleIndices();`
  2. Swap and rebuild inside a lock: `{ RenderLock lock(*this); visibleIndices_ = std::move(next); rebuildRowItems(); }`
  3. Leave `moveSelectionTo(0)` outside that scope, because it takes the lock itself (`UiListActivity.cpp:76`).

  The handler runs with the lock released (`ActivityManager.cpp:125-127`), so the lock is safe to take there.

### MINOR 2: `chapterPassageCount` goes stale on the page paths that return early

- **Claim.** Step 6.4 assigns `chapterPassageCount = highlightRanges.size()` inside `renderContents`, just after the
  highlight loop (plan:1860-1877).
- **Problem.**
  - `renderBook` returns before calling `renderContents` in three cases:
    - an empty chapter (`EpubReaderActivity.cpp:1211-1217`);
    - a page index out of bounds (`:1219-1226`);
    - a page-load failure (`:1231-1248`).
  - In each case the count left over from the previous chapter survives. If that count is above 0, `openReaderMenu`
    passes it on, and "Tags here (n)" shows a count for a chapter whose page never painted. Tapping it filters on the
    new `currentSpineIndex`, so the list and n disagree. That is the mismatch V-6 checks for.
  - The spec's "reset to 0 whenever `highlightsLoaded` is false" (spec:260-262) is met, but only on the path that
    actually reaches `renderContents`.
  - The impact is low: a Bible chapter is never empty.
- **Evidence.**
  - `src/activities/reader/EpubReaderActivity.cpp:985,1209-1248,1336,1392-1401`
- **Fix.** Add `chapterPassageCount = 0;` in `renderBook` next to the `renderer.clearScreen()` at `:1209`, before the
  first early return. The assignment in `renderContents` then overwrites it on every page that does paint.

### MINOR 3: the icon byte-identity check does not look at the file that gets committed

- **Claim.** Step 4.5 is the A-26 / T-3 guarantee that the 26 existing `freeink::Icon` definitions stay
  byte-identical.
- **Problem.**
  - The step runs `git add` first, then `./bin/clang-format-fix`, and then diffs `--cached` (plan:869-873).
  - The formatter rewrites the working tree, not the index. So the check sees the pre-format splice.
  - Step 4.6's `git add -u` (plan:880) then stages the formatted file without checking it again.
  - `listIcons.h` is not excluded from formatting (`bin/clang-format-fix:49-54`). If the formatter ever touched an
    existing array, the check would pass and the change would still be committed.
- **Evidence.**
  - plan:869-881
  - `bin/clang-format-fix:44-54`
- **Fix.** In Step 4.5, run `git add -u` after `./bin/clang-format-fix` and before the `git diff --cached … | grep '^-'`
  check. Step 4.6 then only commits.

## What holds up

I checked each of these and found no problem.

**The four declared departures**

- **Returning `true` only on a dispatched action (departure 1) is correct and necessary.**
  - `TouchRoute::operator bool` is the dispatched event (`UiAppHost.h:61`).
  - The left-edge swipe ends in a routed release that dispatches nothing, and `wasReleased(Back)` reports it
    (`MappedInputManager.cpp:263-264`).
  - Returning `true` on `routed` would therefore stop swipe-to-close.
  - Routing the same non-dispatching snapshot again in `routeListTouch` is harmless. `routeAgainst` only resets
    `active_` (`FreeInkUICore.h:1229-1261`).
- **21,600 B (departure 2) is the right figure.** `max(61·349, 45·480) = 21,600`, which fits the portrait copy of
  `44 or 45 × 480`. `readFramebufferRegion`'s capacity check is at `GfxRenderer.cpp:1752`.
- **Splicing the icons (departure 3) matches the generator.**
  - `gen_icons.py` writes each array on one line as `// alias  (lucide: name)` followed by `static const` lines, so the
    splice regex matches.
  - The file header reads `Icons: 13.`.
  - `layout-grid`, `search`, `tag` and `x` all exist in the Lucide directory.
  - No other header defines `icon_{grid,search,tag,close}_*`. `search.h` defines `SearchIcon`.
- **Editing the keys in-branch (departure 4) has precedent.** The #178 plan declares both YAML files on `FILES:`
  (`2026-09-27-issue-178-plan.md:21`).

**The pure headers and host tests**

- The model reproduces `buildMenuItems` (`EpubReaderMenuActivity.cpp:41-82`) item for item.
- Every expected value in T-2 checks out by hand: tile 81, sheet 451, top 349, 580-px tiles ending at 582, and the
  20-row and 28-row fit boundaries.
- The test counts (12 and 11) match the `TEST` macros.
- The CMake blocks copy the `ListRowHeightTest` block exactly.

**The menu activity**

- Every API it calls exists with the signature used: `frame().hit`, `target().text/bitmap/lineHeight`, `ListProps`
  fields, `Screen::list(props, height)`, `setContentMargin`, `bitmapFromIcon` (reached through
  `OptionPopup.h → UiAppHelpers.h`), `readFramebufferRegion`/`writeFramebufferRegion`, `hasFrameBuffer`,
  `makeUniqueNoThrow<uint8_t[]>` and `RenderLock()`/`RenderLock(Activity&)`.
- Routing is newest-first (`FreeInkUICore.h:1167`), so the registration order gives the close / guard / control
  layering the spec wants.
- The app does not clear before painting by default (`FreeInkApp.h:800`), so the restored page survives `renderUi()`.

**The reader wiring**

- The call-site rewrite script's anchors each occur exactly once.
- The six `false` sites are exactly the six sub-screen result handlers (`:676,730,747,759,773,800`).
- The `openHighlights` and `renderContents` anchors match the tree verbatim.

**File lock and task hygiene**

- Every tracked file any step writes appears on a `FILES:` line (plan:8-16).
- The regenerated I18n headers and the `build/` tree are gitignored build outputs.
- Each task leaves the tree building and committable.
- Tasks 5 and 6 have no failing host test first, because activities have no host harness. The spec's testing strategy
  (T-3) gates them on the build, and the plan does the same.

## Verdict

The three MINORs can be fixed inline. None of them reverses a decision, changes scope, or needs the owner's judgment.

VERDICT: CLEAR
