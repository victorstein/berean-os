Tier: standard

# Plan review 0: berean-o-26, the Tarjetas tab

Reviewed: `docs/superpowers/plans/2026-11-22-berean-o-26-plan.md` against
`docs/superpowers/specs/2026-11-22-berean-o-26-design.md`, checked against the current
`src/activities/preaching/*`, `src/study/PreachingHandouts.{h,cpp}`,
`src/activities/UiTabListActivity.cpp`, `test/ui_layout/ReaderEntryIntentTest.cpp` and `USER_GUIDE.md` §11.

## Spec coverage

Every spec item maps to a step:

- Goal 1, A1, A2 (first and default tab, rows with an always-shown count, two-line labels, no
  "+ choose" row): Task 4 Steps 2 and 4. `rebuildRows` reserves both vectors before the push loops
  and returns before the Mis temas branch.
- Goal 2, A11 (Heading card on each section change, only when non-empty; header = title): Task 3
  Step 2. The `cards` views stay valid: `handout.headings` and `handout.verses` are complete before
  `cards` is built, and `TopicCards` stays non-movable (`PreachingContent.h:31-35`). The card layout
  already handles `CardKind::Heading` (`PreachingCardLayout.cpp:18,32,51`), and show mode counts only
  Verse cards (`PreachingShowLayout.cpp:87,96`), so ordinals line up with `verseSource`.
- Reference tap: Task 3 Step 3 sends a handout verse down the href path, and only a tag keeps
  `OpenAt`. That fixes the spec's "falls into the `else` and indexes an empty `passages`" case
  (`PreachingTopicActivity.cpp:217-230` today).
- Goal 3, A10 (three-way wrap, Confirm steps forward): Task 4 Step 4's `stepTab`. `direction < 0`
  gives `+2 mod 3`, and anything else gives `+1`. That matches `UiTabListActivity.cpp:70-71` and
  `PreachingActivity.cpp:157`.
- Goal 4, A3 (Question origin now set explicitly, Tag unchanged): Task 4 Step 3.
- Entry steps 1–4, `onTopicResult`, swipe back from the reader: Task 4 Steps 3 and 6. The reader
  needs no change. Nothing outside `activities/preaching/` reads `PreachingOrigin::Kind`, and there
  is no `switch` on it that would warn on the new enumerator.
- Error table: Missing or Ok-empty → NO_CARDS, and TooNew, Unreadable or OOM → UNREADABLE (Task 4
  Step 5). On open, a file problem → `HandoutsUnavailable` → CARDS_UNREADABLE toast, and OOM →
  OPEN_FAILED (Task 3 Step 2, Task 4 Step 6). An index past the end gets `Unreadable` from `load`
  (`PreachingHandouts.cpp:129-131`) and so the unreadable toast.
- A5 static_asserts, A12 USER_GUIDE (both the tab bullet and the "A topic" sentence), A13 strings,
  and the host test from the Testing strategy: Tasks 3, 5, 2 and 1.

No step drops or narrows a requirement.

## Behaviour the tests cannot see

I traced these paths by hand because no host suite reaches them:

- **Render-task safety of the summaries.** `ensureHandouts` writes `handoutSummaries` outside the
  `RenderLock` (Task 4 Steps 3 and 4). That is safe for two reasons. It writes only once, and by then
  `tab` is not yet `Handouts` under the render task: `showTab` sets `tab` under the lock after the
  load, and in `onEnter` the load happens before `UiTabListActivity::onEnter` starts rendering. Later
  calls return at `handoutsLoaded`. The row pointers into `Summary::title` and `rowValues` are stable
  until the activity exits.
- **Return from the reader when the file changed or went missing.** The list shows the right notice
  or rows, `seedSelection` falls back to the tab bar when `listCount() <= row`, and `openHandout`
  toasts. This matches the spec's error table.
- **Heap across Tarjetas → Bible → back.** The summaries belong to the `PreachingSession` member, so
  they are freed when `PreachingActivity` is deleted on `goToReader`. Nothing new outlives the
  activity.

## The file lock

Every file a step edits is on a column-0 `FILES:` line (plan lines 30–36). The build and check logs
go to `build/`, which is gitignored (`.gitignore:13`), and the i18n headers are generated and
unstaged. No finding.

## Findings

No BLOCKERs and no MAJORs.

### MINOR 1 — `loadHandout`'s negative-index guard returns without a log, and gives the wrong toast

- **Claim.** `if (handout < 0) return LoadResult::Unreadable;` (plan line 237) is an error return
  with no `LOG_ERR`.
- **Problem.** The repo rule is "always log before an error return". `Unreadable` also maps to the
  generic `STR_PREACHING_OPEN_FAILED`, not the handout toast. No caller can pass a negative index
  today: rows give `actionValue >= 0`, and an origin's `uint8_t` is never negative. So the guard
  never trips. It is dead weight, not a shipped bug.
- **Evidence.** Repo `CLAUDE.md`, "Error handling"; `PreachingActivity.cpp:198-209`;
  `PreachingContent.cpp:105-107` (`failureMessage`).
- **Fix.** Return `LoadResult::HandoutsUnavailable` and add a `LOG_ERR(MODULE, "No handout %d",
  handout)`, or delete the guard and convert with `static_cast<size_t>`. A negative value would then
  wrap past the end, where `load` already logs and returns `Unreadable`
  (`PreachingHandouts.cpp:130`).

### MINOR 2 — Task 6 Step 3's `git add -u` can commit files outside the lock

- **Claim.** "If it changed files: `git add -u`" (plan lines 722–727) stages every modified tracked
  file.
- **Problem.** If the whole-tree formatter rewrites a file this plan does not own, that file is
  committed under this task, outside the `FILES:` lock. This only happens if `main` was unformatted,
  which CI normally prevents, so the risk is low.
- **Evidence.** Plan lines 30–36 (the lock) against lines 718–727.
- **Fix.** Stage only the locked paths:
  `git add -u -- src/activities/preaching/ test/ui_layout/ReaderEntryIntentTest.cpp USER_GUIDE.md lib/I18n/translations/`.
  If the formatter touched anything else, report it instead of committing it.

---

VERDICT: CLEAR
