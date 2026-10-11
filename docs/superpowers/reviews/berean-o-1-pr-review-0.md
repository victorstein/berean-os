Tier: standard

# berean-o-1 (R20) PR review 0

PR #255, `perf/r20-fast-entry-refresh` at `0f97d079`, against `main` (merge base `1ba1cd34`). Reviewed
against the brief (`hpipe brief --task t1`), the spec
(`docs/superpowers/specs/2026-10-11-berean-o-1-design.md`) and the plan
(`docs/superpowers/plans/2026-10-11-berean-o-1-plan.md`). Every claim below was checked in the
source at HEAD, not taken from the PR body.

The source change is three files, 9 lines removed and 11 added:

- `src/activities/catalog/PublicationsActivity.cpp`: `onEnter` no longer sets `halfRefreshPending`
  (`:52-58`).
- `src/activities/reader/BibleSearchActivity.cpp`: `enterState(State::Opening)` (`:114`); reason
  comments at `:250`, `:374-375` and `:447-448`; the `render()` comment at `:1017-1019`.
- `src/activities/UiListActivity.h`: the flag comment is mechanism-only (`:86-87`).

## Intent

**Acceptance criteria (brief).**

- **Both entries log a FAST from Home and from the reader, where the decision allows it.** Holds.
  Decision d1 = (a) allows it everywhere.
  - Publications has one constructor call, `LauncherActivity.cpp:434`, so Home is its only origin.
    `UiListActivity` defaults the flag to false (`UiListActivity.h:88`), and the first render
    consumes it at `UiListActivity.cpp:169`.
  - The flag stays false on the first `refresh()` too: the cover-change HALF is guarded by
    `!mastheadCover_.empty()` (`PublicationsActivity.cpp:117`), and that is empty on entry.
  - Bible search has two origins, both through `EpubReaderActivity::openBibleSearch`: the menu
    (`EpubReaderActivity.cpp:811`) and Home's Search route (`:1067`). Both now reach
    `enterState(State::Opening)` (`BibleSearchActivity.cpp:114`). That leaves `fullRefreshPending`
    false (`:137`, default `fullRefresh = false` at `BibleSearchActivity.h:177`), so `render()`
    takes `FAST_REFRESH` (`:1037`).
- **Every HALF left on these paths carries a one-line reason.** Holds.
  - Failed: `:250`.
  - Ready: `:374-375`.
  - Starting: `:447-448`.
  - The rule as a whole: `render()`'s header comment (`:1017-1019`).
  - I traced every caller to check each reason is true:
    - `showFailure` is reached only from `beginBuild` (`:261`, `:273`), `stepBuild` (`:305`) and
      `finishBuild` (`:364`). Each one replaces a progress-view frame.
    - Ready is reached only from `finishBuild` (`:376`), after Building/Finishing, and both of
      those are FAST. So "only FAST repaints have touched since Starting" is accurate.
- **No other screen's refresh choice changes.** Holds. I reran the spec's invariant grep at HEAD and
  got exactly the seven expected lines:
  - `MeetingsActivity.cpp:68`;
  - `BibleNavigationActivity.cpp:370`;
  - `PublicationsActivity.cpp:117`;
  - `BibleSearchActivity.cpp:137`, `:251`, `:376` and `:449`.

  `git diff main...HEAD -- src/` touches no other refresh decision. `UiListActivity.cpp` and the
  SDK are untouched.
- **The PR states the before and the expected after, and lists the device checks.** Holds.
  - The before is 1,506 ms. The expected after is ~560 ms, labelled an estimate, and the PR says
    no device run has been made.
  - The checks cover Home and the anti-aliased reader-menu path, with a ghost check on each.
  - I verified the log anchors:
    - `[MENU] heap closed:` is `logHeap("closed")` in the menu's `onExit`
      (`EpubReaderMenuActivity.cpp:181,214`).
    - `[READER] Book loaded in` is a `LOG_DBG` (`ReaderActivity.cpp:60`). It is printed because
      `x4pro` builds with `LOG_LEVEL=2` (`platformio.ini:174`).
    - `[PUBS] Memory on entry:` is at `PublicationsActivity.cpp:55`.
  - The menu anchor is followed directly by the search frame. The result handler's push sets
    `pendingAction`, so no reader re-render is requested in between
    (`ActivityManager.cpp:126-133,150-160`).
- **The PR title is `perf: …`, it targets `main`, and the body ends `Refs berean-o-1`.** Holds.

**Spec coverage.**

- **Goals 1–4:** implemented as above.
- **Goal 5:** the figures are given as estimates. R11 has not merged: `gh pr list --head
  perf/r11-search-index-resident` is empty, and its branch has no `src/` diff against `1ba1cd34`.
  So the baseline figures are the right ones to quote, and the PR says so.
- **A7 and A8:** the comment texts are the spec's, verbatim.
- **Non-goal, Meetings:** reported as the spec required. `hpipe discoveries` lists t1/x1, "perf:
  FAST, not HALF, entering Meetings (R20 follow-up)".
- **No silent scope reduction.** Each of the brief's audit items (`:114`, `:250`, `:373`, `:444`,
  the SSD1677 behaviour, and the owner decision) is answered in the research, the spec or the code.
- **No scope expansion.** The `UiListActivity.h` edit is comment-only. Spec review 0's MAJOR-1
  required it: left alone, the comment would have prescribed the HALF that A1 deletes.

**Tests.**

- No host test was added. Spec A13 explains why:
  - no host suite compiles either activity (`test/` mentions them only in comments, at
    `RecentBooksDocTest.cpp:205` and `ReferenceConsistencyTest.cpp:39`);
  - the `HalDisplay` stub has no recording seam (`test/stubs/HalDisplay.h:18-24`).
- The red/green source invariant stands in for a test, and it pins the actual risk: a call site
  that still requests a HALF. Spec review 0 and plan review 0 accepted this.
- It leaves no lasting regression guard in CI. But there is no cheap seam to build one, and a
  `LauncherRefresh.h`-style helper would only restate a per-state table, as A13 argues. I do not
  count this as a finding.

**Plan conformance.**

- Tasks 2–5 landed as four commits whose messages and code match the plan's text byte for byte.
- No `style:` commit was needed. CI `clang-format` passes on `0f97d079`.
- Task 6's build is supported: the touched objects in `.pio/build/x4pro` (05:35:55–56) postdate
  their sources (05:32:33–43).
- The PR body follows Task 7's template. It adds two `Verified` bullets, on R11 and on who wrote
  the code, which are disclosures rather than divergences.
- `git log HEAD..origin/main` over the three files is empty, so Task 0's merge was correctly not
  needed.

## Quality

- **It reuses existing mechanisms rather than adding a second one.**
  - Both edits work through the per-frame flags the screens already have: `fullRefreshPending`
    (`BibleSearchActivity.h:116`) and `halfRefreshPending` (`UiListActivity.h:88`).
  - There is no new type, parameter, constructor flag or log line. `enterState` keeps its
    `fullRefresh` parameter, which Starting and Ready still use.
- **Comments.**
  - Each new line gives the *why* for a HALF that would otherwise look arbitrary next to the FAST
    entry, and none of them restates the code.
  - `render()`'s header is written for the merged state with no before/after narration. "Entering
    the screen included" states a fact a reader would otherwise have to dig for.
  - The base-class comment no longer prescribes a rule that this PR's own change breaks.
  - Every remaining setter does say why, as the new base comment claims:
    `MeetingsActivity.cpp:67`, `BibleNavigationActivity.cpp:368-369`,
    `PublicationsActivity.cpp:115-116`.
- **Nothing else.** There is no dead or commented-out code, no stale comment (no other comment in
  `src/` or `lib/` describes either entry's refresh), no new allocation, no error path and no
  duplicated logic.
- **CI at review time:**
  - Passing: `clang-format`, `lint-title`, `Title Check` and `script-tests`.
  - Still running: `Build x4pro`, `cppcheck` and `unit-tests`.
  - Merging should wait for those as usual. The diff gives no reason to expect a failure: the only
    code changes are a deleted statement and a dropped argument that has a default.

## Findings

None. I looked for a wrong reason comment, a missed HALF call site, an unintended refresh change
on another screen, a wrong log anchor in the device checks, and drift from the plan, and found
none. The one open question is visual — whether a FAST ghosts over Home or the menu on either panel.
It belongs to the owner's device run, which the PR asks for, and decision d1 already names the
fallback (b).

VERDICT: CLEAR
