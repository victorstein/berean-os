Tier: standard

# berean-o-1 (R20) plan review 0

Plan: `docs/superpowers/plans/2026-10-11-berean-o-1-plan.md`, reviewed against
`docs/superpowers/specs/2026-10-11-berean-o-1-design.md` (which cleared spec review 0). Reviewed at
`102a627e`. Everything below was checked in the source, not taken from the plan.

## What was verified, and holds

- **Every `old` snippet matches HEAD byte for byte.** I applied all seven edits with exact-match,
  count-1 string replacement to a scratch copy of `src/` (`git archive HEAD src`). The sites:
  - Task 2: `PublicationsActivity.cpp:52-61`.
  - Task 3: `BibleSearchActivity.cpp:112-114`.
  - Task 4a–4d: `:243-252`, `:372-373`, `:442-445` and `:1011-1014`.
  - Task 5: `UiListActivity.h:86-90`.

  No snippet was ambiguous or missing.
- **The red/green invariant does what the plan says.**
  - Task 1's command, run verbatim at HEAD, exits 1. It prints exactly the two `<` lines the plan
    shows, with the same `1d0`/`5d3` hunks (plan :82-87).
  - Run against the scratch copy with every edit applied, it exits 0 with no output (plan :166).
  - The heredoc is already in `LC_ALL=C sort` order. None of the new comment texts matches the grep
    patterns, so Tasks 4 and 5 cannot disturb it.
- **Spec → step mapping is complete.**

  | Spec | Plan step |
  |---|---|
  | A1 | Task 2 |
  | A2 | Task 3. The `:112-113` comment is kept. |
  | A4, A5, A6 | Tasks 4c, 4b, 4a. A4 and A6 are the spec's text verbatim. A5 keeps the existing Ready line, as the spec's Architecture table requires. |
  | A7 | Task 4d |
  | A8 | Task 5. This is the spec's text verbatim. |
  | Testing 1 | Tasks 1, 3 and 6 |
  | Testing 2 | Tasks 0 and 6 |
  | Testing 3 | The PR body's device checks |
  | Risks (R11) | The ground rules, Task 0 and Task 7 |

  - The spec's Non-goal "Meetings … reported with `hpipe discover`" (spec :51-53) needs no step:
    `hpipe discoveries` already lists `t1/x1 — perf: FAST, not HALF, entering Meetings (R20 follow-up)`.
- **Goal 3 is complete.** The only HALF requests in `BibleSearchActivity.cpp` are `:114`, `:250`,
  `:373` and `:444`, plus the mechanism at `:137`. Every caller of `showFailure` (`:260`, `:272`,
  `:304`, `:363`) is on the build path, so 4a's comment is true at every caller.
- **Names and signatures are consistent across steps.**
  - `enterState(State next, bool fullRefresh = false)` is at `BibleSearchActivity.h:177`.
  - `fullRefreshPending` is at `.h:116`. `halfRefreshPending` is at `UiListActivity.h:90` and is read
    at `UiListActivity.cpp:169`.
  - `UiListActivity::onEnter` (`UiListActivity.cpp:20-27`) does not set the flag.
- **The `FILES:` lines (plan :7-8) are well formed.**
  - Both are at column 0, outside any fence, comma-separated and repo-relative.
  - `planDeclaredFiles` (`stein.pipeline…/src/lib/gating.ts:34-58`) splits them into exactly
    the three files the steps edit.
  - Nothing else is written. `.venv/` and `.pio/` are ignored, the build log goes to `$TMPDIR`, and
    any whole-tree `clang-format-fix` change outside the three files is restored (plan :346-347).
- **The tooling paths and anchors are correct.**
  - `~/.platformio/penv/bin/pio` exists, `[env:x4pro]` is at `platformio.ini:164`, and `bin/bootstrap`
    exists.
  - The branch tracks `origin/perf/r20-fast-entry-refresh`, so the plain `git push` and Task 0's
    `git merge` both work.
  - The device-check log anchors are real: `[PUBS]` (`PublicationsActivity.cpp:27`), `[MENU] heap
    closed:` (`EpubReaderMenuActivity.cpp:181,214`), and `[READER] Book loaded in`
    (`ReaderActivity.cpp:60`). The last is `LOG_DBG`, and it prints under `x4pro`'s `LOG_LEVEL=2`
    (`platformio.ini:174`).
- **Every step leaves a buildable, committable tree.**
  - Task 2 alone and Tasks 2–3 together are each coherent states. The only stale text between commits
    is the `render()` comment, which Task 4 fixes before the squash.
  - Formatting precedes the single build, so nothing is rebuilt after formatting (CLAUDE.md, Testing
    checklist 1).
- **TDD shape.** The spec (A13) settles that no host suite can compile either activity. The plan's
  substitute — red at Task 1, green at Task 3 — is real and reproducible. Tasks 4 and 5 are
  comment-only and carry no red check; see MINOR-3.

## Findings

No BLOCKERs, no MAJORs.

### MINOR-1 — Task 7 defers the end-to-end figure when R11 has landed, where the spec says to recompute it, and the stand-in wording reads as a measurement

- **Claim.** If `main` has an R11 commit, "replace the end-to-end search line with 'measured on device
  against `main` at `<sha>`'" (plan :392-394).
- **Problem.** There are two.
  - The spec says the PR *recomputes* that figure against the `main` it is rebased on (spec :46-47).
    Spec review 0's fix is the same: "Have the PR compute the end-to-end … figure"
    (`berean-o-1-spec-review-0.md:177`).
  - "Measured on device" in the "Expected after" column contradicts the same body's "No device run
    has been made for this PR" (plan :435), and the spec's "never claimed in the PR" (spec :253).
  - The inputs to recompute already exist. R11's spec gives its expected frame → keyboard: ~0.5 s
    for the first search after boot, and well under 100 ms later, from 2,436 ms and ~500 ms
    (`git show origin/perf/r11-search-index-resident:docs/superpowers/specs/2026-10-11-berean-o-6-design.md`,
    lines 19-22 and 49-51).
  - Separately, "no berean-o-6 (R11) commit" asks the worker to recognise R11 from a squash-merge
    subject, which will not carry the bead id.
- **Evidence.** Plan :387-394 and :435; spec :46-47 and :253; spec review 0, :177; R11 spec lines
  19-22 and 49-51.
- **Fix.** Replace the second sub-bullet with:
  - "If the log is non-empty, recompute and label the result an estimate. The new figure is
    (baseline end-to-end − baseline frame → keyboard) − ~950 ms + R11's expected frame → keyboard.
  - Use R11's merged spec or PR for its inputs, and show the arithmetic in one line under the table.
  - If R11 states no frame → keyboard figure, write 'not estimated: depends on R11's index open;
    owner measures against `main` at `<sha>`'."

### MINOR-2 — Task 6's fallback scope check has no path filter, so its "exactly three files" expectation fails

- **Claim.** "If the Task 0 merge brought in sibling changes, compare against `origin/main` instead:
  `git diff --stat origin/main...HEAD`" (plan :371-372). The expectation is "exactly three files"
  (plan :366-369).
- **Problem.** Unlike the primary command at :363, this one has no `-- src/ lib/ freeink-sdk test/`.
  It also lists every docs artifact of this task. A literal implementer sees more than three files
  and either stops or ignores the check.
- **Evidence.** Running `git diff --stat origin/main...HEAD` now lists four files, all under
  `docs/superpowers/`: the research, spec, spec review and plan. Their review files will add to that.
- **Fix.** Use `git diff --stat origin/main...HEAD -- src/ lib/ freeink-sdk test/`.

### MINOR-3 — Tasks 4 and 5 have no red-then-green check, so Goal 3, A7 and A8 are never verified mechanically

- **Claim.** After Task 4: "Rerun the Task 1 command. Expect it still green (exit 0): comments do not
  change it" (plan :278). Task 5 says the same (plan :322).
- **Problem.** A check that cannot change cannot show that the step did anything. The brief asks
  that each step start from a failing check. Goal 3 ("Every HALF left … carries a one-line reason"),
  A7 and A8 are only ever verified by eye. A cheap exact check exists for both tasks.
- **Evidence.** I ran the commands below at HEAD and on the scratch copy with the edits applied:

  | Count | HEAD (red) | After the edits (green) |
  |---|---|---|
  | The four new `BibleSearchActivity.cpp` comment phrases | 0 | 4 |
  | `setter says why` in `UiListActivity.h` | 0 | 1 |
  | `replaces cover art` in `UiListActivity.h` | 1 | 0 |

- **Fix.** Open Task 4 with this command, expecting `0` before the edits and `4` after:

  ```bash
  grep -c "Closes the build's progress view\|only FAST repaints have\|The build's first clean paint\|FAST otherwise, entering the screen included" src/activities/reader/BibleSearchActivity.cpp
  ```

  Open Task 5 with the following two commands. Expect `0 1` before the edit and `1 0` after.

  ```bash
  grep -c "setter says why" src/activities/UiListActivity.h
  grep -c "replaces cover art" src/activities/UiListActivity.h
  ```

### MINOR-4 — Task 6 pushes unconditionally, while Task 7 gates the PR on the implement-phase prompt

- **Claim.** Task 6 ends with a bare `git push` (plan :374-376). Task 7 says "The worker opens the
  PR … only when its implement-phase prompt says to" (plan :382).
- **Problem.** CLAUDE.md, Git workflow rule 2, puts pushing and opening a PR under the same approval:
  "Never push to any remote, or open or close a PR, without explicit user approval." The plan gates
  one and not the other. Earlier pipeline plans that push say where the authority comes from
  (`2026-09-17-issue-40-plan.md:978`: "authorised by the pipeline brief").
- **Evidence.** Plan :374-376 and :382; CLAUDE.md, "Git workflow → Rules", 2.
- **Fix.** Prefix the push with the same condition: "Push only when the implement-phase prompt
  authorises it (it normally does); otherwise stop after the local commits."

### MINOR-5 — Line citations and one cross-reference are off by one

- **Claim.**
  - "As of `1ba1cd34` this is `:85-89`" (plan :294).
  - "`:1011-1015`" (plan :258).
  - "Tasks 2, 3, 5 and 6 rerun it verbatim" (plan :93).
- **Problem.** The `UiListActivity.h` block the snippet quotes is `:86-90`. The comment ends at
  `:89` and `halfRefreshPending` is at `:90`. The `render()` block is `:1011-1014`, because `:1015`
  is `const State drawn = state;`. Task 4 also reruns the invariant (plan :278). None of these
  affects execution, because the snippets are authoritative and every task re-reads the file first.
- **Evidence.** `grep -n halfRefreshPending src/activities/UiListActivity.h` gives `:90`. `grep -n
  fullRefreshPending.exchange src/activities/reader/BibleSearchActivity.cpp` gives `:1016`.
- **Fix.** Change the three references to `:86-90`, `:1011-1014`, and "Tasks 2 to 6".

## Verdict

The plan implements the spec exactly, and its central check works as written. Every snippet applies
cleanly, the invariant goes red and then green as described, the file lock is complete, and nothing
reverses a decision or changes scope. The five MINORs are wording, a missing path filter, two
cheap grep checks, and an approval gate. All can be fixed inline.

VERDICT: CLEAR
