Tier: standard

# Issue #205 spec review — pass 0

Reviewed: `docs/superpowers/specs/2026-09-30-issue-205-design.md` against issue #205, the research note
`docs/superpowers/research/2026-09-30-issue-205-research.md`, the code at `aa661ce5`, and the
linked mockup (read via the Artifact tool).

Overall the design holds up. A1 through A13 match the code they cite. Rebuild points
(`HighlightsActivity.cpp:34-35, 139-140, 363-364, 408-409`), the three `moveSelectionTo` call sites
(`:141, 369, 414`), the `listCount()` and `actionValue = i + 1` arithmetic (`:38, :112, :166-188`),
`rowActionTrampoline`'s bound (`UiListActivity.cpp:33-36`) and `MAX_INTERACTIONS = 64`
(`UiAppHost.h:35`) are all as stated. The mockup confirms the A1 order: `All 37, hope 12, ministry 9,
name 4, trust 5, study 3, Unlabelled 4, …`. Keeping `listCount()` as the ring size works with the
base `onRowAction`, because a passage row's `actionValue` is already its ring position.

There are two MAJORs, both about how the chips are drawn. Neither reverses a decision, and both can
be fixed in the spec.

## MAJOR

### M1. `hitPadding` cannot stop the `minTouchSize` expansion, so chips on the two lines steal each other's taps

- **Claim.** Architecture, `buildScreen` step 3: draw each chip with `screen.button(props, rect)` and
  "`hitPadding` splitting the inter-chip gap so the `minTouchSize` expansion (`button.h:38-44`) does
  not overlap neighbours across lines."
- **Problem.** `hitPadding` is applied first, and `ensureMinTouchRect` then grows the padded rect to
  `minTouchSize` anyway. `Screen::button(props, rect)` also overwrites the caller's `minTouchSize`
  with the theme token, so the spec's call cannot opt out. With a chip height of "small line height
  plus vertical padding" (A11) and `spaceSm` as the gap, the line pitch is below 44 px. The mockup
  draws it at 38 px (`y += 38`). Each line-2 chip's hit rect therefore grows upward over the bottom
  of the line-1 chip above it. Hits are resolved last-registered-first, so in that strip the line-2
  chip wins and a tap on a line-1 chip filters by a different tag.
- **Evidence.**
  - `FreeInkApp.h:223-230`: `themed.minTouchSize = theme_.minTouchSize;` runs unconditionally in
    `button(const ButtonProps&, Rect)`.
  - `button.h:37-41`: the padded rect is built first, then `ensureMinTouchRect(padded, props.minTouchSize, …)`.
  - `FreeInkUI.cpp:821-830`: the rect grows symmetrically to `minSize` in both axes.
  - `FreeInkUI.cpp:117` and `FreeInkUICore.h:637`: `minTouchSize` is at least 44 and at least
    `lineHeight + 14`.
  - `FreeInkUICore.h:1165-1178`: `findTouch` scans from the last interaction down to the first, so
    the last-registered rect wins.
- **Fix.** Pick one of these in the spec:
  - (a) Call `fui::button(screen.frame(), rect, props)` directly, as `buildTabBar` calls
    `fui::tabBar(screen.frame(), …)` (`UiTabListActivity.cpp:189`). Set `props.minTouchSize = 0`
    and use `hitPadding` of half the gap on each inner edge, so the hit bands tile with no overlap.
  - (b) Keep `screen.button`, but size the chips so that
    `chipHeight + gap >= screen.theme().minTouchSize` and every chip is at least
    `minTouchSize - gap` wide. The expansion then has nothing to add.

  Add a layout host test: for the chosen metrics, no two placed chips' hit rects intersect.

### M2. A bold selected chip is wider, so selecting a chip can reflow the row or push the chip behind `…`

- **Claim.** A13: selected means "black fill, white text and bold". `buildScreen` step 1: "Measure
  each candidate chip with `measureText(smallFont, label, style)`" and run `layout` on the result.
  A1 rejects sorting by count "because a chip would jump position after a retag".
- **Problem.** Bold lives in `ButtonProps.text` (`TextStyle.bold`), not in the per-state
  `StyleSet`. The selected chip must be measured bold, or its label is ellipsised. But once it is
  measured bold, its width depends on the selection, and `layout()` is a greedy wrap. Selecting a
  chip can then:
  - move later chips from line 1 to line 2, the jumping A1 set out to avoid;
  - or, for a chip near the end of line 2, make it no longer fit. It is dropped and A5 inverts `…`
    instead, so the chip the user just tapped disappears.
- **Evidence.**
  - `FreeInkUICore.h:534-544`: `TextStyle` carries `bold`.
  - `button.h:77-80`: the label is drawn with `props.text`; the state-resolved `BoxStyle` supplies
    only the paints.
  - The bold UI font is a separate face (`ubuntu_10_bold.h`, cited in research line 14), so its
    advances differ from the regular face.
- **Fix.** Measure every candidate with the bold style, so each width is fixed whatever is selected,
  and draw the unselected chips regular inside that width. Or drop bold from A13, since the inversion
  alone marks the selection. Add a layout test: the placements are identical whichever chip is
  selected.

## MINOR

### m1. `moveRingTo` copied from `UiTabListActivity` drops the RenderLock that `moveSelectionTo` holds

- **Claim.** "`moveSelectionTo` is replaced by a `moveRingTo` that mirrors
  `UiTabListActivity::moveRingTo` (`:49-62`)."
- **Problem.** The tab version writes `n.selected` and `n.top` with no lock. `moveSelectionTo`
  takes `RenderLock` for exactly this reason: "a press landing during a render would otherwise tear
  selection/viewport".
- **Evidence.** `UiTabListActivity.cpp:49-62` has no lock; `UiListActivity.cpp:72-82` has one.
- **Fix.** Specify that `moveRingTo` does its nav writes under `RenderLock` and calls
  `requestUpdate()` after releasing it.

### m2. `listCount()` becomes the ring size, but some base code still reads it as a row count

- **Problem.** The base swipe path calls `n.scrollBy(delta, listCount())`, which allows `top` one
  row past the end. The spec says `moveRingTo` "mirrors" the tab version, and that version passes
  `listCount()` to `listTopIndexFor` as the row count (`UiTabListActivity.cpp:58-59`). The tab
  version's `navigateButtons` uses `ringSize = listCount() + 1` (`:66`); copied verbatim here, that
  would add one ring slot too many. The spec also never says what a continuous hold does: the base
  pages, and the tab version steps tabs.
- **Evidence.** `UiListActivity.cpp:100`, `UiTabListActivity.cpp:58-59, 66-71`.
- **Fix.**
  - In `moveRingTo` and `syncRingViewport`, use `visibleIndices_.size()` as the row count.
  - Set the ring size in `navigateButtons` to `listCount()`.
  - Define a hold as a page jump by `pageRows()` through `moveRingTo`.
  - Say that `syncRingViewport` leaves `props.nav` null, as `syncTabListViewport` does.
    `ListNav::onListRendered` reads `selected` as a row index (`list.h:182-201`), so feedback would
    be off by one.

  A stray swipe at the end of the list otherwise costs one wasted refresh; `syncRingViewport`'s
  clamp corrects `top` on the next build.

### m3. A12's "every passage is then UNLABELLED" is false, because passages can carry retired ids

- **Claim.** A12: with zero active tags, `All n` and `Unlabelled n` "are equal, since every passage
  is then UNLABELLED (A3)".
- **Problem.** `retireTag` strips the id only from the open publication's passages. Passages in
  other publications keep retired ids, and the store says so. A publication can therefore have no
  active tags and still hold passages that carry a retired tag only. Those passages count under
  `All` and under no chip. That is correct behaviour, but the stated invariant is wrong, and the
  spec's own test "Ids that are not active (retired) are not counted" (line 288) contradicts it.
- **Evidence.**
  - `StudyStore.cpp:232-240`: `passages_.removeTagEverywhere(id)` runs on the loaded document only.
  - `StudyStore.h:56`: "a retired tag [resolves] to its real name."
  - `PassageDoc.cpp:199-205`.
- **Fix.** Reword A12: `Unlabelled n ≤ All n`, and passages carrying only a retired id are reached
  through `All`. Add a count test for a passage whose only tag is retired.

### m4. "Holds by construction" overstates A3

- **Claim.** A3 and Architecture: "both the filter and the counter call" `passageMatches`.
- **Problem.** `count()` as specified walks each passage's carried ids through a sorted id→slot
  lookup. It does not call `passageMatches`. The two agree only because `normaliseTags` removes
  duplicates on every write and load path (`PassageDoc.cpp:17-27, 151, 197, 329`).
- **Fix.** Say that the equivalence test, not shared code, guarantees criterion 1, and cite the
  deduplication invariant the counter relies on.

### m5. The functions declared in the header need `inline`, or a `.cpp` the test compiles

- **Problem.** `passageMatches` and `layout` are declared as non-template, non-inline functions in a
  header, and no `.cpp` is named. `NumberGridLayout.h`, the header the spec models itself on,
  defines its functions `inline` (`NumberGridLayout.h:34`).
- **Fix.** Define them `inline` in `TagChipRow.h`.

### m6. Resource table: one claim is wrong and O1 is left open

- **Problem.** The spec says every allocation "is under 4 KB". `chipTags_` at the palette cap is
  200 × `sizeof(TagView)`: a 2-byte id, padding, and a 24-byte `std::string` on this 32-bit target.
  That is about 5.6 KB, which routes to PSRAM. Names over 15 bytes also allocate outside SSO. And
  once `chips_` carries its own labels and ids, neither `chipTags_` nor `chipCounts_` is read after
  `rebuildRowItems`, so neither needs to be a member.

  O1 is deferred to the planner, but the spec already knows the answer. Its "reserve candidates + 2"
  rule contradicts the O1 bound.

  The `Chip[]` input to `layout()` has no stated home. A local of 25 × 12 B is 300 B, over the
  256 B locals rule, on the render task.
- **Evidence.** `TagPalette.h:38-39`, `StudyStore.cpp:129-133`, and the CLAUDE.md resource rule 1.
- **Fix.**
  - Make `chipTags_` and `chipCounts_` locals of `rebuildRowItems`.
  - Adopt O1 as decided: `chips_` holds at most `MAX_CHIPS + 1` candidates, reserved to exactly
    that, plus a `bool moreCandidates`.
  - Keep the `Chip` array beside `Layout` as a member of the same size.
  - Correct the table.

### m7. The ring-0 outline is effectively always shown on the touch device

- **Problem.** `nav.selected` is 0 on entry (`UiListActivity.cpp:22`, `list.h:162-170`) and again
  after every chip tap (A7). The X4 Pro is touch-first, so the 2 px band outline (A10) will be on
  screen nearly all the time. The mockup shows no outline; the inverted `All` is the only selection
  mark.
- **Fix.** Either accept this and say so explicitly, as a deliberate difference from the mockup, or
  draw the outline only after a button step has moved the focus. Record the choice for the owner's
  device check.

### m8. On overflow, `Unlabelled` always ends up behind `…`

- **Problem.** A1 puts `Unlabelled` after every tag and A4 drops chips from the end, so whenever the
  row overflows, `Unlabelled` is always hidden. That follows from the rules, but the spec does not
  state it, and for a study device "passages I have not labelled yet" is arguably the chip most
  worth keeping.
- **Fix.** State the consequence in A1 or A4, so the owner sees it at the device check.

VERDICT: CLEAR
