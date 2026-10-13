Tier: heavy

# PR review 0: `TagChipGridActivity` and `TagFilterActivity` as its first subclass (btrial-4)

Range: `origin/batch/20261012-tag-chip-picker-2l3m...origin/feature/tag-chip-grid-base`, head
`a19868a8`. I read it against the brief (`hpipe brief --task t4`), the design
(`docs/superpowers/specs/2026-10-12-btrial-4-design.md`), the batch spec
(`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` §3-§5, A1, A6) and the plan
(`docs/superpowers/plans/2026-10-12-btrial-4-plan.md`).

The branch has no worker reply commits. Every commit after the plan review is one of the five plan
tasks.

What I ran:

- **Host suite.** I built it fresh in a scratch directory (`cmake -S test -B <scratch>`, then
  `ctest -j10`): `100% tests passed out of 1745`. That is the 1736 baseline plus the 9 new tests.
  `TagRowsTest --gtest_filter='TagChipReselect*:TagChipFilterKey*'` passed all 9.
- **Format.** I copied the seven changed sources and the test file into a scratch git repo with the
  repo's `.clang-format` and `bin/clang-format-fix`, and ran the wrapper there. `git status` stayed
  clean.
- **Firmware.** I did not run `pio`, as instructed. The worker's log `build/btrial-4-pio.log`
  (18:58) and `.pio/build/x4pro/firmware.bin` (19:00) postdate the head commit (18:58:15), and the
  tree is clean. The log compiles `TagChipGridActivity.cpp.o` and `TagFilterActivity.cpp.o`
  (`:558`, `:560`) and reports `SUCCESS`, with RAM at 66,228 B (unchanged) and Flash at 5,563,766 B
  (+552 B over the 5,563,214 baseline). Its only warning is in `.pio/libdeps/.../WebSockets`; there
  is none in `src/` or `lib/`.
- **Plan match.** I pulled the plan's code blocks out of the plan and diffed them against
  `TagChipGridActivity.{h,cpp}` and `TagFilterActivity.{h,cpp}`. All four files are byte-identical
  to the plan. `TagChipRow.h`, the tests and the `TagChipView.cpp` change also match Tasks 1-3.

## Intent

**Acceptance criteria. All are met.**

- **The base exists and owns the §3 list.**
  - `chips_`, `widths_`, `page_` (a member), the geometry, page number and count, and
    `buttonFocus_` are in `TagChipGridActivity.h:87-98`, and the popup state follows at `:100-104`.
  - `rebuildChips` is at `TagChipGridActivity.cpp:25-52`.
  - `buildScreen` → `buildGrid` + `buildHeaderExtras` is at `:54-120`, with the strip only past one
    page (`:81-86`). Swipe is at `:204-219`, step and held page turns at `:223-264`, and the footer
    at `:122-134`.
  - Retire, with the popup, the `saveDisabled` guard and the interleaved `render`, is at
    `:163-202` and `:266-283`. Back returns a cancelled result (`:136-141`).
  - The six hooks are declared at `TagChipGridActivity.h:37-51`. Each comment names its task and the
    lock state when it is called. The general rule is in the class comment (`:23-26`).
- **`TagFilterActivity` is thin, with its contract unchanged.**
  - Counts and chapter scope still flow through `buildEntries(scope_, filter_, SIZE_MAX, out)`
    (`TagFilterActivity.cpp:21-23`). Zero-count chips are still hidden by `candidates`
    (`TagChipRow.h:115-123`).
  - The result contract is unchanged (`TagFilterActivity.cpp:29-42`): All is empty, any other chip
    is one id, and Back and Home are cancelled.
  - The constructor signature is unchanged, so `HighlightsActivity.cpp:178` is untouched.
- **Alphabetical order.** `TagChipView.cpp:16` now reads `activeIdsByName()`, and the same vector
  feeds both the counts and `candidates`, so the counts stay aligned. All first and Unlabelled last
  are pinned by `candidates` (`TagChipRow.h:115`, `:123`) and by the existing
  `NameOrderedIdsKeepAllFirstAndUnlabelledLast`.
- **The selection follows `(kind, id)`, a retired chip clamps, and a subclass can request one.**
  - `rebuildChips()` keys on `chips_[nav.selected]` (`TagChipGridActivity.cpp:25-33`).
  - `rebuildChips(ChipKey)` (`:35`) is the request path, and the filter uses it on entry
    (`TagFilterActivity.cpp:16`).
  - `reselect` (`TagChipRow.h:154-161`) is findChip-or-clamp.
  - I checked the entry selection against the removed `selectedChipIndex`. After `nav.reset()`,
    selected is 0. `filterKey` + `findChip` then lands on the same chip that `isSelected` inverts,
    and falls back to 0 as before.
- **`closeRouting()` under `RenderLock`, reopened by the next render.**
  - It is called inside the swap's lock (`TagChipGridActivity.cpp:41-49`), and the render is
    requested after the lock is released (`:51`).
  - `render` always calls `renderUi()` (`:271`) before `processRender` (`:278`), and `renderUi` sets
    `uiReady = true` (`UiAppHost.cpp:26`), so a rebuild can never leave the grid deaf.
  - The retire callback runs from `handleCustomInput`, which returns true. The rebuild therefore
    closes the gate in the same pass, before `routeListTouch` (`UiListActivity.cpp:85-87`) could
    route against the stale table.
- **Home under the popup; the save-failure message.**
  - `handleHomeGesture` calls `onHome()` only when the popup is not active, and always returns true
    (`TagChipGridActivity.cpp:143-148`). `ActivityManager::loop` reaches it before `loop()` with no
    lock held (`ActivityManager.cpp:75-80`).
  - A failed save posts `STR_TAG_SAVE_FAILED` (`TagChipGridActivity.cpp:201`;
    `english.yaml:380`, "Could not save tag").
- **Retire acts only on `Kind::Tag`, and `onTagRetired` fires on `!isActive`.**
  - The kind check is at `TagChipGridActivity.cpp:169`.
  - `onTagRetired` fires whenever `!isActive(id)` after the attempt (`:199`), before the rebuild.
    That matches `StudyStore::retireTag`, which retires in memory before saving and never rolls back
    (`StudyStore.cpp:266-274`).
- **`ACTION_USER` is free.** The base registers only `ACTION_ROW`, through `UiListActivity::onEnter`,
  and documents the free id (`TagChipGridActivity.h:28`).
- **Host suite, build and format.** All three pass, as listed above. The design's "Device checks
  (owner)" items 1-9 cover order, paging, swipe, button stepping, retire, and Home under the popup
  (`2026-10-12-btrial-4-design.md:538-565`).

**The design is implemented in full, not just its easy half.**

- Every row of the design's "Changes to the moved code" table (`:353-363`) is present.
- The parts that are easy to skip are all there:
  - `buildHeaderExtras` runs even when the grid returns early (A9, `TagChipGridActivity.cpp:54-57`);
  - `onRowAction` writes `nav.selected` under the lock (A4, `:150-161`);
  - `handleButtons` is pinned `final` (A12, `:221`);
  - `PassageLinksActivity.h:24` is re-pointed (A16).

**The narrowed `onTagRetired` check is behaviour-preserving.** The old code reset any inactive
filter. The new `filter_ != id` early return (`TagFilterActivity.cpp:44-50`) only differs if the
grid opens with a retired filter. `HighlightsActivity` assigns `filterTagId_` only from a chip, from
this screen's result, or by clearing it (`HighlightsActivity.cpp:82-88`, `:166`, `:186-194`), so
that cannot happen.

**Scope.** Nothing beyond the brief was added. `ChipKey`, `filterKey` and `reselect` are the "pure
helper in `TagChipRow.h`" that batch §3 asks for. They compose the existing `findChip` rather than
re-implementing it. The picker, `TagRowMapping.h` and the header seam are untouched.

**The base serves the picker (t5).** The base leaves open what §4 needs:
- `drawChrome` and `onEnter` stay open;
- `chips()` and `retireConfirmationOpen()` cover the toggle and Done guards;
- `rebuildChips(ChipKey{Tag, id})` covers selection after a create;
- `ACTION_USER` is free for `ACTION_DONE`;
- `render` keeps `PostedMessage::drawNext`, which the picker's current render lacks.

The result handler and `onEnter` both run with `RenderLock` released (`ActivityManager.cpp:125-127`,
`:157-158`), so `rebuildChips` from either cannot self-deadlock.

**Tests.** The 9 tests exercise behaviour: following a moved tag, the clamp in range and past the
end, singleton kinds, no key, and the empty list. A `reselect` that ignored its key would fail
`FollowsATagThatMovedInARebuild` (it would return 1, not 3). The base's wiring is not host-testable,
because no host target compiles an `Activity`. That is covered by the build and by device check 5
(retire on page 2 stays on page 2).

**Plan divergence.** None.

## Quality

- **One existing pattern, not a second one.** The base follows `UiTabListActivity`: a non-final
  `UiListActivity` subclass with a `--- subclass contract ---` block and family-wide overrides of
  `onRowAction` and `navigateButtons`. Sealing with `final` follows `ReaderActivity.h:61`. The body is
  a verbatim move: of the old `TagFilterActivity.cpp`, only the lines the design lists changed.
- **Naming and structure.** These match the siblings: trailing-underscore members as in
  `TagFilterActivity`, `TagPickerActivity` and `HighlightsActivity`, and the same `RenderLock` scopes.
  `rebuildChipsSelecting` keeps the `optional` out of the public overloads.
- **No dead code or noise.**
  - Every new member has a caller.
  - `selectedChipIndex` is removed, and nothing else referred to it.
  - The new comments carry a reason: why the gate closes, why a failed save can still have retired
    the tag, and why Home is guarded.
- **Error handling has today's shape.** The `saveDisabled` bail, the posted message, and no new
  failure path. `ReaderUtils::showMessage` only posts (`ReaderUtils.h:233`), so calling it after the
  rebuild is safe.
- **The concurrency trace holds.**
  - Every loop-task write to state the render reads is under `RenderLock`: `chips_`, `widths_`,
    `nav.selected`, `buttonFocus_` and `filter_`.
  - The render task never writes `nav.selected`. The grid never calls `screen.list()`, so
    `ListNav::onListRendered` is not reached, which justifies the unlocked read in `rebuildChips()`.
  - `STUDY.retireTag` runs unlocked, and that is safe because no render-task path in either grid
    reads the palette (batch A1). `chipState` reads only `filter_` and the chip.
- **The tests are well built.** They use the existing `FakeChip`, `CHIPS`, `countsOf` and
  `candidatesOf` fixtures. The three `static_assert`s at `TagChipRowTest.cpp:719-721` overlap the
  EXPECTs below them, but they also pin `filterKey` as `constexpr`. That is acceptable.

## Findings

No BLOCKER, no MAJOR.

**MINOR 1. The base's class comment says a held button turns a page; on this board a hold activates
or goes Back.**

- **Evidence.** `TagChipGridActivity.h:15-16` says: "A swipe or a held button turns a page". The
  design's A18 (`2026-10-12-btrial-4-design.md:494-504`) establishes the opposite:
  - a nav key held past 250 ms stops reporting `isPressed` (`NavKeyGestures.h:41-48`), so
    `ButtonNavigator::onContinuous` never fires (`ButtonNavigator.cpp:58-75`);
  - a hold of 850 ms or more resolves on release to Back or Confirm (`NavKeyGestures.h:12-25`, `:41`).
  The sentence is new; the old `TagFilterActivity.h` class comment made no such claim.
- **Impact.** This is the first thing the t5 implementer reads. A tester who follows it holds
  Right, and activates the focused chip instead of turning the page. The design's device check 3
  had to say the opposite explicitly.
- **Fix.** "A swipe turns a page; a released button steps one chip, crossing pages as it goes; a tap
  or Confirm activates one. The held-button page turn in navigateButtons is kept for boards whose
  keys report a hold."

**MINOR 2. The `activateIndex` note runs into `onTagRetired`'s comment, and only `activateIndex`
says "take RenderLock to write".**

- **Evidence.** `TagChipGridActivity.h:46-49`. The `activateIndex(int)` comment (`:46-47`) has no
  declaration of its own. It runs straight into `onTagRetired`'s comment (`:48`) and reads as part of
  it. `onTagRetired`'s own line says only "Loop task, no lock held". The batch spec's hook table
  marks `onTagRetired` as "takes `RenderLock` to write" (batch spec §3). The picker's override will
  write `selectedIds_`, which `chipState` reads on the render task. The class comment (`:23-26`)
  does state the general rule.
- **Fix.** Put a blank line or a separator after `:47`. End `:48` with "; take RenderLock to write
  anything the render task reads", as the filter's own override already does
  (`TagFilterActivity.cpp:44-50`).

VERDICT: CLEAR
