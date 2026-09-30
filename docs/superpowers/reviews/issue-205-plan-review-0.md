Tier: standard

# Issue #205 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-30-issue-205-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-205-design.md`
Base checked: working tree at `5a73dc57`.

## Summary

The plan is sound and can be executed literally. Every spec requirement maps to a step:

- A1/A2 candidate order and zero-count hiding: `rebuildChips`, plan:719-747.
- A3 predicate and counts: Steps 1–3.
- A4/A6 overflow and cap: Step 4.
- A5 inverted `…`: plan:858-868.
- A7 tap handling: `selectChip`, plan:771-788.
- A8 touch only: plan:874.
- A9 labels: plan:735, 745, 871.
- A10 ring, buttons and outline: Step 7.
- A11 metrics: plan:820-829.
- A11b hit tiling: Step 5, plan:879-884.
- A12 empty screen: plan:893-898, after the existing return.
- A13 styles and bold measurement: plan:818-856.
- Removal of `filterSubtitle_`/`computeFilterSubtitle`, and the class comment: Step 7.

Each of the five deviations is stated, and each is smaller than the spec's version.

I checked the parts an implementer is most likely to trip on against the code, and they hold:

- **Test expectations.** I recomputed the pixel expectations in Step 4 by hand (`308`, `194`, the drop-a-neighbour case) and in Step 5 (`NeighboursShareTheGapExactly`). They match the `layout()`/`hitPadding()` bodies as written.
- **API calls.** Every call exists with the signature the plan uses:
  - `fui::button(Frame&, Rect, const ButtonProps&)`, with `ButtonProps::hitPadding` and `minTouchSize` (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/button.h:8-35`)
  - `Insets{top,right,bottom,left}` (`FreeInkUICore.h:87-92`)
  - `TextStyle::bold` (`:539`)
  - the `StyleSet` members (`:588-617`)
  - `spaceSm`/`spaceMd`/`listInset`/`listRowRadius` (`:634-648`)
  - `DrawTarget::stroke(rect, paint, width, radius)` (`:698`)
  - `TEXT_ELLIPSIS` (`:714`)
  - `ActionHandler` (`FreeInkApp.h:560`)
  - `ListNav` members (`list.h:145-172`)
  - `STUDY.palette().activeIds()` and `STUDY.tagName()` (`src/study/StudyStore.h:40, 57`)
  - `study::toRaw`/`toTagId` (`TagPalette.h:26-27`)
- **Locking.** The new `RenderLock` in `onEnter` and in the picker result handler (deviation 5) cannot deadlock. `ActivityManager` releases the lock before both `onEnter()` (`ActivityManager.cpp:159-160`) and result handlers (`:126-127`). Touch routing holds no lock (`UiAppHost.cpp:29-40`). No new lock is taken while another is held: `moveRingTo` is always called after the rebuild scope closes.
- **Host-test target.** `TagPalette.h` needs only its constexpr free functions from the test, so linking `ArduinoJson` plus the `lib/StudyStore` include (plan:114-133) is enough, with no `TagPalette.cpp`. This mirrors `test/passage_doc/CMakeLists.txt`.
- **Working tree at every commit.** Step 6 adds chips while row 0 still exists, and Step 7 removes it, so each commit builds and works.
- **FILES lines.** Both `FILES:` lines (plan:11-12) are at column 0, outside any fence, and repo-relative. Together they cover every file any step edits: `TagChipRow.h`, `HighlightsActivity.h`, `HighlightsActivity.cpp`, `TagChipRowTest.cpp` and `test/tag_rows/CMakeLists.txt`. The Step 8 formatter runs over the whole tree, but the commit it makes names only those files.

Steps 2, 6 and 7 do not start with a failing test. Step 2 is a refactor that Step 1's tests already pin. Steps 6 and 7 are activity code, which the spec's testing strategy explicitly leaves to the firmware build and the device check, because no suite can host a renderer-dependent activity. I accept that and do not count it as a finding.

## Findings

### MAJOR 1: `layout()` returns a 412-byte `Layout` by value onto the render task stack, which undoes the spec's reason for making it a member

- **Claim.** The spec makes `chipInputs_` and `chipLayout_` members "filled in `buildScreen`, not locals, so the render task's locals stay under 256 B (resource rule 1)" (spec:216-218, resource table spec:299). Plan deviation 1 keeps that rationale (plan:18-21), and the header comment says "members rather than locals to keep its stack frame small" (plan:691).
- **Problem.** `chipLayout_ = TagChips::layout(...)` (plan:835) is an assignment, not an initialisation. The prvalue is materialised as a temporary on `buildChipRow`'s frame, with `out` NRVO'd into it inside `layout()` (plan:498-499), and then copy-assigned into the member. That is a full `Layout` on the render task stack for the duration of the call. The same frame also holds a `fui::StyleSet` (plan:842) and a `fui::ButtonProps` (plan:870). The member does not save the stack bytes it was introduced to save, and the function's locals are several times the 256 B rule in `CLAUDE.md` ("Stack safety").
- **Evidence.**
  - Measured on host with the same struct shapes: `Layout` is 412 B; `StyleSet` is 528 B and `ButtonProps` is 624 B. Both of the latter are smaller on the 32-bit target, but still well over 256 B.
  - `Placed` is 4 × `int` and `placed[MAX_CHIPS + 1]` has 25 entries (plan:484-496).
  - The render task stack is 8192 B (`src/activities/ActivityManager.cpp:32-33`), and `buildChipRow` runs nested under `renderUi` → `buildScreen`.
  - There is a precedent for large style locals: `UiTabListActivity::buildTabBar` holds a `StyleSet` and a `TabBarProps` (`src/activities/UiTabListActivity.cpp:150-189`). So the style/props part is existing practice. The `Layout` temporary is the part the spec explicitly designed out.
- **Fix.**
  - Change the signature to fill a caller-owned result:
    ```cpp
    inline void layout(const int* widths, int count, int moreWidth, int lineWidth, int gap, Layout& out)
    ```
    It starts with `out = Layout{}` or resets `placedCount`, `lines` and `overflow` in place.
  - In the activity, call `TagChips::layout(chipWidths_, count, moreWidth, lineWidth, gap, chipLayout_);`.
  - In Steps 4 and 5, adapt the test helper `layoutOf` and the direct call in `HitRectsNeverIntersect` (plan:578) to declare a local `Layout` and pass it in. Stack size does not matter on the host.
  - Optionally, build the chip `StyleSet` once into a member (it depends only on `chipHeight`), so each render has one `ButtonProps` on the stack rather than a `StyleSet` plus a `ButtonProps`.
  - Correct the resource note at plan:691 to match whatever is chosen.

### MAJOR 2: Step 8 tells the implementer to `git push`

- **Claim.** Step 8 ends "Then push: `git push`." (plan:1139).
- **Problem.** `CLAUDE.md`, Git workflow rule 2: "Never push to any remote, or open or close a PR, without explicit user approval. Complete local work and any requested local commit, then stop." An implementer who follows the plan literally breaks a project rule, and pushes before the pipeline's own review and merge gate.
- **Evidence.** plan:1139; `CLAUDE.md`, "Git workflow → Rules", item 2.
- **Fix.** Delete the line. If the pipeline needs a hand-off, replace it with "Stop; the orchestrator pushes and opens the PR."

### MINOR 1: Step 7's verification `grep` will not print nothing

- **Claim.** Step 7 expects `grep -n "filterSubtitle_\|computeFilterSubtitle\|moveSelectionTo" src/activities/reader/HighlightsActivity.*` to print nothing (plan:1110-1113).
- **Problem.** The comment above the `applyTagEdit` call site still reads `// filtered tag itself deleted); moveSelectionTo issues its own` / `// requestUpdate().`. The plan replaces the call on the next line but never that comment, so the grep matches it. A literal implementer then sees an unexpected result and has to guess whether the step failed.
- **Evidence.** `src/activities/reader/HighlightsActivity.cpp:366-369`; plan:982-987 changes only the call.
- **Fix.** In Step 7, also replace that comment, e.g. `// ...filtered tag itself deleted); moveRingTo clamps the ring position and issues its own requestUpdate().`

### MINOR 2: The focus outline is drawn `gap` px outside the body when the theme's `listInset` is 0

- **Claim.** The outline rect starts at `band.x + inset - gap` and spans `lineWidth + 2 * gap` (plan:1091-1092).
- **Problem.** The base theme has `listInset = 0` (`src/components/themes/BaseTheme.h:140`). There the outline's left and right edges fall 4 px outside `screen.body()`, into the safe-area margin or the bezel, and could be partly clipped. Lyra (`listInset = 20`, `LyraTheme.h:23`) is unaffected.
- **Evidence.** plan:833-834, 1091-1093; the `spaceSm` default is 4 (`FreeInkUICore.h:634`).
- **Fix.** Clamp the horizontal outset to the inset, e.g. `const int outset = std::min(gap, inset);`, and use `outset` for x and width. Keep `gap` for the vertical outset, which lands in the spacer and the `takeTop` gap, since `verticalSpacing` is ≥ 8.

### MINOR 3: Returning from the picker opened with Confirm drops the button-focus outline

- **Claim.** The picker's result handler calls `moveRingTo(0)` (plan:983), and `fromButton` defaults to `false`, which clears `buttonFocus_` (plan:999).
- **Problem.** A button user who walks to ring 0 and presses Confirm comes back from `TagFilterActivity` with the focus still on ring 0 but no outline. A10's intent is that the outline shows when the chip row was reached by buttons. A touch path into the picker (`…`) has already cleared `buttonFocus_` in `onMoreEvent` (plan:1068-1076), so keeping the flag is safe.
- **Evidence.** plan:983, 992-1010, 1068-1076; spec A10 (spec:105-110).
- **Fix.** Use `moveRingTo(0, buttonFocus_)` in the picker handler. Optionally do the same at the `applyTagEdit`/`deleteHighlight` sites (`moveRingTo(activeNav().selected, buttonFocus_)`), where it is harmless.

### MINOR 4: Stale row-0 comments survive Step 7

- **Claim.** Step 7 updates two comments that still describe row 0 as the filter control (plan:1100-1104).
- **Problem.** Others still describe the removed row:
  - `buildScreen`'s `// Nothing to browse or filter: skip the filter row entirely …` (`HighlightsActivity.cpp:477-478`)
  - `props.inputMask`'s `// Tap opens/cycles` (`:498`)
  - the header's `rebuildRowItems` comment, "Rebuilds rowTagValues_/rowItems_ … (onEnter, filter cycle, delete)" (`HighlightsActivity.h:104-107`). That function now also rebuilds `chips_`, and it runs on a chip tap and on retag.

  `CLAUDE.md` asks for comments written for the merged state.
- **Evidence.** The lines cited above; plan:1100-1104 names only `onRowLongPress` and `handleButtons`.
- **Fix.** Add these three edits to Step 7, e.g. "skip the chip row entirely", "Tap jumps; long-press opens the action menu", and "Rebuilds rowTagValues_/rowItems_ and chips_ … (onEnter, chip tap, picker result, retag, delete)".

## Verdict

No BLOCKER. The two MAJORs are mechanical and can be fixed inline: one changes a function signature and two call sites, the other deletes a line. Neither reverses a spec decision or needs the owner's judgment.

VERDICT: CLEAR
