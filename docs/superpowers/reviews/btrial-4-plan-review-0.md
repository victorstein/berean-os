Tier: heavy

# Plan review 0: btrial-4, `TagChipGridActivity` with `TagFilterActivity` as its first subclass

Reviewed: `docs/superpowers/plans/2026-10-12-btrial-4-plan.md` (commit `ee4d3e83`). I checked it
against `docs/superpowers/specs/2026-10-12-btrial-4-design.md`, the batch spec
(`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`, §3 to §5 and A1 to A6), the research
note and spec review 0. I also read the code on `feature/tag-chip-grid-base`. I did not apply or
build the plan.

## Verdict in one paragraph

The plan is sound. Every spec requirement maps to a step, and no step narrows one. The base's
`.cpp` is a faithful move of today's filter. I extracted the plan's block (plan:511-793), renamed
the class in today's `TagFilterActivity.cpp` to match, and diffed the two. The only differences are
the ones the plan lists at plan:796-805, plus three comment edits:

- `chipState`;
- the `buildGrid`/`buildHeaderExtras` split;
- reselect, `closeRouting` and `requestUpdate`;
- the locked `onRowAction`;
- the Home guard;
- `onTagRetired` and `STR_TAG_SAVE_FAILED`;
- the forwarding `handleButtons`.

Nothing in the moved code changed silently. The lock contract holds on every path I traced. The
`FILES:` lock covers every source path a step writes. I found no BLOCKER and no MAJOR. The two
MINORs are both about committing: one lets a formatter change outside the lock be committed, and
the other gives the implementer two different instructions for committing a build fix.

## What I checked and found to hold

- **Spec coverage.** Each item maps to a task:
  - **Goal bullets.** The base owns the §3 list (Task 4). It has six hooks with a per-hook task and
    lock contract in its header (plan:435-449). The filter becomes a thin subclass (Task 5).
    Alphabetical order comes from `buildEntries` (Task 3). Selection follows `(kind, id)`, clamps,
    or takes a requested key (Tasks 1, 2 and 4). Home is guarded and the retire message is fixed
    (Task 4).
  - **"Changes to the moved code" table** (spec:353-363). All nine rows are present in the diff
    above.
  - **A12's `final` list.** All eleven members, `handleButtons` included (review m2), are `final`
    at plan:429-430 and :452-465.
  - **Host tests.** Every `TagChipReselect` and `TagChipFilterKey` case in spec:513-524 is present.
    That includes the integration test mapped through `FakeChip` (review m3).
  - **Device checks.** All nine are handed to the owner unchanged (plan:1057-1058).
  - **Flash delta.** It is reported against the spec baseline (plan:986-988, :1053).
  - **Comments.** `PassageLinksActivity.h:24` is re-pointed (plan:945-955).
- **The tests can fail.** A `reselect` that ignored `key` would return 1, not 3, in
  `FollowsATagThatMovedInARebuild`. A `candidates` that dropped the zero-count filter fails the
  `ASSERT_EQ(chips.size(), 5u)` in `AZeroCountFilterIsSelectedAtItsNameOrderSlot`. `CHIPS`,
  `FakeChip`, `HOPE`, `MINISTRY`, `NAME`, `countsOf` and `candidatesOf` all exist where the plan
  says they do:
  - `TagChipRowTest.cpp:19-21`, `:326`, `:334`, `:642-650`;
  - `candidatesOf` has a defaulted cap, since `:344` calls it with three arguments.
- **Lock contract.** I traced each path:
  - **Retire.** `retireTag` runs from the popup callback inside `handleInput`, after `active` is
    cleared (`OptionPopup.h:80-82`, `:120-123`). It calls `onTagRetired` and then `rebuildChips`,
    and each takes its own `RenderLock` in disjoint scopes.
  - **Home.** `onHome` is reached only from `ActivityManager.cpp:76`, with no lock held.
  - **`rebuildChips()` reads unlocked.** It reads `chips_` and `nav.selected` without the lock.
    Both are written only by the loop task: `ListNav::reset` and the base's own locked writes. The
    render writes only `nav.top` (plan:601).
  - **Clamp source.** The retire path's key is the long-pressed chip, because `onRowAction` sets
    `nav.selected = event.value` before `onRowLongPress` (plan:660-671). The clamp therefore lands
    on the chip that slid into its slot, as today (`TagFilterActivity.cpp:46-47`).
- **The routing gate.** `renderUi` runs before `processRender` on every render (plan:780-788).
  - A rebuild's `closeRouting()` is therefore always undone by the render that `rebuildChips`
    requests (plan:558-561), so the grid cannot be left deaf to touch.
  - The close happens under the same lock as the swap, so an in-flight render built from the old
    list cannot reopen the gate after it.
  - Pop does not re-run `onEnter` (`ActivityManager.cpp:115-134`), so the base having no `onEnter`
    (A1) loses nothing on return from a child screen.
- **A8 narrowing.** `HighlightsActivity` writes `filterTagId_` only from a chip, from the filter's
  result, or in `dropRetiredFilter` (`HighlightsActivity.cpp:85-86`, `:166`, `:186-188`).
  `filter_ == id` therefore matches today's broader check on every reachable path.
- **Picker readiness (t5).** The `final` members still leave t5 what it needs:
  - `drawFooter`: the picker's footer labels are identical to the base's
    (`TagPickerActivity.cpp:59-64`).
  - `drawChrome` and `onEnter` stay open.
  - `chips()` and `retireConfirmationOpen()` cover its `activateIndex` and Done guards.
- **File lock.**
  - The five `FILES:` lines (plan:32-36) sit at column 0, outside any fence, with repo-relative
    paths.
  - They cover all eight paths that a step writes or `git add`s.
  - The pio log (plan:979) and `build/test` go under `build`, which is ignored (`.gitignore:13`).
- **Anchors.** Every anchor and find-string matches today's tree:
  - `TagChipRow.h:130`, `:138`;
  - `TagChipView.cpp:16`;
  - `TagFilterActivity.h:26`;
  - `PassageLinksActivity.h:24`.
- **No push or PR step.** The plan forbids push, PR, `hpipe`, `bd` and `bv` (plan:3-5).

## Findings

### MINOR 1: Task 6 Step 2 commits whatever the whole-tree formatter changed, with no lock guard

- **Claim.** "If the formatter changed a file, commit it as `style: format the tag chip grid` with
  the session trailer" (plan:1032-1033).
- **Problem.** `./bin/clang-format-fix` without `-g` formats every tracked file. A local
  clang-format that differs from CI's could reformat a file this change never touched. The step
  names no paths and says nothing about one outside the lock, so following it faithfully commits a
  path that is not on a `FILES:` line. Tasks 1 to 5 avoid this by naming their paths in `git add`.
  This is the only commit step that does not.
- **Evidence.** `bin/clang-format-fix:20-24` limits the run to modified files only under `-g`.
  The step is at plan:1028-1033, and the lock at plan:32-36. btrial-1 and btrial-2 plan review 0
  each raised the same gap as their MINOR 1.
- **Fix.** Replace the sentence at plan:1032-1033 with: "If the formatter changed a file on a
  `FILES:` line, stage it by path and commit it as `style: format the tag chip grid` with the
  session trailer. It needs no rebuild. If `git status --short` lists any other path, do not stage
  it. Restore it with `git checkout -- <path>` and report it as a discovery."

### MINOR 2: The build-failure fix is routed two ways, and Task 5's `git add` leaves a fix to Task 4's files unstaged

- **Claim.** The ground rules say "If that build fails, fix it in a new commit" (plan:54-55).
  Task 5 Step 6 says "A fix to Task 4's files belongs in this task's commit; say so in the commit
  body" (plan:992-994).
- **Problem.** The two instructions disagree. Step 6's version also cannot be carried out as
  written: Step 7's `git add` lists only `TagFilterActivity.h`, `TagFilterActivity.cpp` and
  `PassageLinksActivity.h` (plan:999).
  - A fix to `TagChipGridActivity.{h,cpp}` would therefore be left unstaged, while the commit body
    claims it is included.
  - Task 6 Step 2 would then show the file as ` M`, and its only instruction is to commit it as a
    `style:` change. The build fix would land under the wrong message.
- **Evidence.** plan:53-55, :992-994, :998-999, :1032-1033.
- **Fix.**
  - Drop the last sentence at plan:54-55 ("If that build fails, fix it in a new commit."), so
    Step 6 is the single rule.
  - In Step 7, change the `git add` line to
    `git add src/activities/reader/TagFilterActivity.h src/activities/reader/TagFilterActivity.cpp src/activities/reader/PassageLinksActivity.h src/activities/reader/TagChipGridActivity.h src/activities/reader/TagChipGridActivity.cpp`.
    Staging an unchanged file is a no-op.

VERDICT: CLEAR
