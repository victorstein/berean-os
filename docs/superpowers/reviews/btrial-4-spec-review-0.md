Tier: heavy

# Spec review 0: btrial-4, `TagChipGridActivity` with `TagFilterActivity` as its first subclass

Reviewed: `docs/superpowers/specs/2026-10-12-btrial-4-design.md` (commit `9f972810`). I checked it
against the brief (`hpipe brief --task t4`), the research note
`docs/superpowers/research/2026-10-12-btrial-4-research.md`, and the batch spec
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`, reading §3, §4, §5, A1 and A6 in
full. I also read the code on `feature/tag-chip-grid-base`. `git ls-remote origin` shows
`batch/20261012-tag-chip-picker-2l3m` still at `783a1ff4`. `feature/tag-chip-states` (`52cf3f8e`) is
an ancestor of `HEAD`, so this design's base has not moved.

## Verdict in one paragraph

The design is sound. Every acceptance item in the brief maps to a concrete mechanism:

- the base owns the §3 list;
- it has six hooks with a per-hook task and lock column;
- selection is restored by `(kind, id)` and otherwise clamped;
- `closeRouting()` runs under the same `RenderLock` as the swap;
- Home is guarded by the popup;
- a failed retire says `STR_TAG_SAVE_FAILED`;
- `ACTION_USER` is left free;
- `buildEntries` reads `activeIdsByName()`.

The design narrows or fills in the batch spec in places, and each of those places is a labelled
assumption. I attacked all eighteen. Each one holds against the code. The one behavioural
narrowing, A8, is unreachable, and I confirmed that by checking every write of Highlights' filter.
The base's hooks also serve the picker that comes next. I found no BLOCKER and no MAJOR. The three
MINORs below are a missing device check, an open override that weakens a stated invariant, and a
test sentence that will not compile as written. All three can be fixed inline at the plan phase.

## What I checked by running it

- **The two pure helpers**, copied verbatim from the design into
  `.../scratchpad/helpers/probe.cpp`, compile against the real `TagChipRow.h` with
  `c++ -std=c++20 -Wall -Wextra -pedantic` and give no warnings.
  - `static_assert`s pass for `filterKey(nullopt)` → All, `filterKey(UNLABELLED)` → Unlabelled and
    `filterKey(id)` → Tag, so `filterKey` is usable as a constant expression.
  - `reselect` follows a moved tag. It clamps a missing tag to the last chip, returns 0 for an empty
    list, and returns 0 for a negative `previous`.
  - `sizeof(ChipKey)` = 4 and `sizeof(std::optional<ChipKey>)` = 6, as "Memory and cost" states.
- **Baseline host target.** `./build/test/tag_rows/TagRowsTest` → `[  PASSED  ] 65 tests.`, which
  matches the research note.
- **Name collisions.** `grep -rn 'ChipKey\|filterKey\|reselect' src test lib` is empty.
  `TagChipRow.h` is also included by `test/ui_layout/HeaderLayoutTest.cpp:10`. The probe above shows
  the additions are clean there under the host flags.

## Claims verified against source

- **Home ordering.** `ActivityManager.cpp:75-80` calls `handleHomeGesture()` before `loop()`.
- **Today's filter Home handler.** It cancels unconditionally (`TagFilterActivity.cpp:143-148`).
- **The picker's popup guard** is at `TagPickerActivity.cpp:207-214`.
- **The render task holds `RenderLock`.** It takes the lock at `ActivityManager.cpp:52` and moves it
  into `render` at `:59`. The lock is a plain `xSemaphoreTake` mutex (`:333-336`), so it is not
  recursive.
  - A6 reasoning holds: closing the gate inside the swap's lock cannot race a render built from the
    old list.
- **Result handlers and `onEnter` run with the lock released.** See `ActivityManager.cpp:126-127`
  and `:159-160`.
  - So the picker's create handler can call `rebuildChips(key)` without self-deadlock.
- **Problem 4's sequence holds.** It runs:
  1. `renderUi` runs before `processRender` (`TagFilterActivity.cpp:268`, `:275`).
  2. The popup clears `active` before it fires the callback (`OptionPopup.h:80-82`).
  3. On the next pass, touch reaches `routeListTouch` with the gate still open.
  4. `rowActionTrampoline` bounds-checks only against the new count (`UiListActivity.cpp:35`).

  The design's A6 is also honest about the window that remains. `renderUi` reopens the gate before
  `displayBuffer()`, so a tap during the panel refresh routes against the new table. The batch
  spec's own A6 rationale ("before the user can see the new layout") is weaker than that.
- **A8.** `filterTagId_` is written only at `HighlightsActivity.cpp:166` (a chip), `:186`/`:188` (the
  filter grid's result) and in `dropRetiredFilter` (`:85-86`). `dropRetiredFilter` runs after both
  retiring screens (`:194`, `:497`).
  - `grep -rn 'retireTag\|\.retire(' src lib` finds only the two activities and `addTagName`'s
    rollback of a tag it has just appended (`StudyStore.cpp:260`).
  - `jumpToHighlight` `finish()`es Highlights (`:208-226`), so no other screen retires while
    Highlights is underneath.
  - The narrowed `filter_ == id` check therefore matches today's broader check on every reachable
    path.
- **A13.** `OptionPopup::handleInput` returns true on every pass while it is active
  (`OptionPopup.h:65-132`), so neither the `activateIndex` guard nor the Done guard is reachable
  through `loop()`.
- **A18.** `HalGPIO::isPressed` withholds Up/Down past 250 ms (`HalGPIO.cpp:198-199`,
  `NavKeyGestures.h:48`). `ButtonNavigator::shouldNavigateContinuously` needs a held time over 500 ms
  (`ButtonNavigator.cpp:69-75`, `.h:41`).
  - Held Right resolves to `BTN_CONFIRM` (`HalGPIO.cpp:213-214`), so device check 3's "Holding Right
    activates the focused chip" is right.
  - A17 holds as stated. `ButtonNavigator::Callback` is a type-erased reference, not a
    `std::function` (`ButtonNavigator.h:15-25`). The only `std::function` left is `OptionPopup`'s
    (`OptionPopup.h:42-43`, `:241`).
- **A4 and A12 overrides are legal.** Every member marked `final` is virtual in `UiListActivity` or
  `Activity`: `listCount`, `buildScreen`, `onRowLongPress`, `onRowAction`, `navigateButtons`,
  `handleCustomInput`, `onBackButton` and `drawFooter` (`UiListActivity.h:35-68`), and `render` and
  `handleHomeGesture` (`Activity.h:35`, `:55`).
  - No new name hides a base virtual, so GCC 14's `-Wall` (`-Woverloaded-virtual=1`) has nothing to
    warn about.
- **No clipping problem for the picker.** FreeInkUI's `Screen::setContentMargin` only moves
  `content_` and does not clip (`FreeInkApp.h:65-67`, `:107`). So the picker's `buildHeaderExtras`
  can draw and hit-register Done in the header band.
- **`buildEntries`.** It passes one id vector to both `count`/`countIn` and `candidates`
  (`TagChipView.cpp:16-20`). `candidates` emits All first and Unlabelled last (`TagChipRow.h:115`,
  `:123`), so A15 holds.

## Findings

### MINOR 1: No device check proves that "a retired selection clamps"

- **Claim.** The brief's acceptance requires that "a retired selection clamps". The design covers
  this with the `TagChipReselect` host tests and device check 5.
- **Problem.** The host tests reach only the pure `reselect`. The wiring is not host-testable: the
  research note's "Testability" section says no activity `.cpp` compiles on host. That wiring is:
  - `retireTag` → `rebuildChips()` derives its key from `chips_[nav.selected]`;
  - the long-press set `nav.selected` to the retired chip;
  - the clamped index then picks the page.

  Device check 5 tests only that the chip disappears and that counts, order and All's inversion are
  right. A long-press clears `buttonFocus_` (design line 321), so the clamped selection is
  invisible. It shows only as the page the grid stays on. If the wiring regressed, for example by
  deriving the key after the swap or passing `nullopt` with `previous = 0`, the grid would jump back
  to page 1 and every listed check would still pass.
- **Evidence.**
  - `TagFilterActivity.cpp:86-89` (`pageHolding` on `nav.selected` picks the page).
  - Design lines 296-297 and 337.
  - Device checks at lines 541-544.
- **Fix.** Add to device check 5: "On a grid with two or more pages, retire a chip on page 2. The
  grid stays on page 2. It does not return to page 1."

### MINOR 2: `handleButtons` stays overridable, which weakens A12's "Back returns a cancelled result" invariant

- **Claim.** A12 says the base's input members are `final` because each carries an invariant,
  including that Back returns a cancelled result. It names the members that stay open as the six
  hooks, `headerTitle`, `drawChrome` and `onEnter`.
- **Problem.** Back and Confirm reach `onBackButton` and `activateIndex` only through
  `UiListActivity::handleButtons` (`UiListActivity.cpp:48-59`). The design leaves that method open,
  and A12's list does not mention it. A subclass that overrides it can bypass both `final`
  members.
  - The picker overrides it today with the held-Confirm retire path (`TagPickerActivity.cpp:298-320`).
  - Batch §4 removes that path ("Removed: `handleButtons`' held-Confirm retire").
  - If t5 keeps the override, it still compiles: `onRowLongPress` is protected and callable.
- **Evidence.** The design header (lines 169-181) declares no `handleButtons`. The A12 text is at
  lines 457-462.
- **Fix.** Declare `bool handleButtons() final;` in the base and forward it to
  `UiListActivity::handleButtons()`, whose Back → `onBackButton` and Confirm →
  `activateIndex(nav.selected)` are exactly what both subclasses need (batch §4, "One check for tap
  and Confirm"). Then add it to A12's `final` list. If you prefer to keep it open, name it in A12 as
  deliberately open and say why.

### MINOR 3: The `TagChipFilterKey` integration test cannot be written as described

- **Claim.** In the testing strategy (lines 513-514), "`findChip(filterKey(filter))` finds it at its
  name-order position between All and Unlabelled", run on `candidates`' output.
- **Problem.** There are two mismatches, and the test fails to compile if it is taken literally:
  - `findChip` takes `(chips, kind, id)`, not a `ChipKey` (`TagChipRow.h:129-136`).
  - `candidates` produces `Candidate{kind, slot}`, which has no `id` member (`TagChipRow.h:101-105`),
    and `findChip` reads `chips[i].id`.
- **Fix.** Write the test as follows:
  1. Map each `Candidate` to a `FakeChip{kind, kind == Tag ? ids[slot] : UNLABELLED}`, as
     `buildEntries` does (`TagChipView.cpp:24-45`).
  2. Assert `reselect(mapped, filterKey(filter), 0)`, or call `findChip(mapped, key.kind, key.id)`.
     Either form also exercises the helper the base actually calls.

VERDICT: CLEAR
