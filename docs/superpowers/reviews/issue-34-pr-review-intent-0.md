# PR review pass 0 (intent) — issue #34, PR #73

Reviewed: PR #73, `feature/34-return-stack-capacity` → `main`, 8 files.
Against: issue #34, `docs/superpowers/specs/2026-09-19-issue-34-design.md`, and
`docs/superpowers/plans/2026-09-19-issue-34-plan.md`.
Scope: **intent only** — does the change do what was asked, completely, and
nothing else. Code quality is stage 2 and is not covered here.

Everything below was executed in this worktree, not read off the PR body.

---

## Verdict summary

No blockers and no majors. The shipped diff matches the plan's paste blocks
essentially verbatim, every issue and spec requirement is met, and the tests
survive mutation rather than restating the implementation. **2 MINORs**, both
documentation-accuracy nits that can be fixed inline.

---

## What was verified, and how

**Every PR-body claim I could re-run, I re-ran.**

| Claim | Result |
|---|---|
| 13 host tests green at the committed `CAPACITY = 16` | Reproduced — `[  PASSED  ] 13 tests.` |
| Capacity sweep 3 / 8 / 16 / 32 green | Reproduced, plus an extra `cap=5`, each with the shim's actual `CAPACITY` line echoed back so a no-op substitution could not pass as a sweep. All five green. |
| Mutant `oldest()` → `&slots_[0]` kills 3 tests at 3 and at 16 | Reproduced exactly: ` 3 FAILED TESTS` at both. |
| Full `ctest` 594/594 | Reproduced — `100% tests passed out of 594`. |
| `./bin/clang-format-fix` (whole tree) leaves the tree clean | Reproduced — exit 0, `git status --short` empty before and after. |
| `EpubReaderActivity.cpp` unmodified (Step 7's temporary A6 gate removed) | Confirmed — it is not in the PR's file list. |
| Three code/docs commits from Steps 3, 5, 6 | Confirmed — `3e9917d8`, `55f8c555`, `b22fd355`. |

**Beyond the PR body**, I added two mutants the plan did not run, to test whether
the rewritten suite is still sensitive after every bound became `CAPACITY`-relative:

```
no-count-clamp       (if (count_ < CAPACITY) count_++;  →  count_++;)   cap=3 -> 3 FAILED   cap=16 -> 3 FAILED
unpush-no-guard      (if (count_ > 0) count_--;         →  count_--;)   cap=3 -> 1 FAILED   cap=16 -> 1 FAILED
clear-no-top-reset   (drop top_ = 0 from clear())                       cap=3 -> PASSED     cap=16 -> PASSED
```

The first two kill cleanly at both capacities, which answers assumption **A4**
more broadly than Step 4b did. The third passing is not a finding: the PR, the
plan (`…plan.md:44-72`) and the corrected spec all state up front that `top_ = 0`
is unobservable through `count()`, `pop()` and `oldest()`, and that the
`ClearEmptiesAWrappedRing` rewrite is hygiene rather than recovered coverage.
That disclosure is accurate.

**The input-layer premise the whole change rests on** — that the left-edge swipe
is already this device's only Return, so eviction strands rather than costs a
step — I traced independently rather than taking the PR's word:

- `src/MappedInputManager.cpp:308-309` — `wasReleased` returns true for
  `Button::Back` on `wasBackGesture()` before consulting any GPIO.
- `src/activities/reader/EpubReaderActivity.cpp:547-551` — the reader's return
  branch gates on exactly that and does **not** exclude the gesture.
- `src/activities/reader/ReaderUtils.h:255-257` — the exit path immediately below
  **does** exclude it ("The reading surface deliberately has no left-edge
  swipe-to-exit path"), so an empty ring leaves the swipe inert, not an exit.
- `src/activities/reader/EpubReaderActivity.cpp:160-162` — the destructor saves
  progress to `returnStack.oldest()`, so a wrapped ring persists the wrong
  origin. This is a live second consequence, not a Phase 2 one.

All four check out. The reframe from "before Back becomes primary" to "already
broken today" is evidenced, not asserted.

---

## Issue #34 acceptance

The issue asks for a decision among three options and says *"Whatever changes
here should come with host tests in `test/`."*

- **Option chosen: raise `CAPACITY`** — the one the issue itself calls "the right
  first move given how little it costs", and the one that commits Phase 2 to
  nothing. `src/activities/reader/ReturnStack.h:19`, 3 → 16.
- **Cost as the issue priced it** — "8 or 16 costs 64 or 128 bytes". Shipped cost
  is `16 * 8 - 3 * 8 = 104` added bytes, consistent.
- **Host tests** — `test/return_stack/ReturnStackTest.cpp`, 12 → 13, all bounds
  re-expressed through `ReturnStack::CAPACITY`, verified green at five capacities
  and mutation-sensitive at two.
- **"The decision belongs with whoever designs the Phase 2 input model"** — the
  PR does not quietly step over this. It argues on evidence that the failure is
  already live (the four citations above), and it leaves the *other* half of the
  Phase 2 item — whether Back and Return need to be visibly different — untouched
  and open. The PR is authored by the repo owner, so the judgment call is made by
  the person the issue reserved it for. No escalation is missing.

No acceptance criterion is unmet.

---

## Spec and plan conformance

- Spec Goal 1 (move the boundary): `ReturnStack.h:19`. ✔
- Spec Goal 2 (semantics bit-for-bit unchanged): `push`, `pop`, `unpush`,
  `clear`, `count`, `oldest` and the private members are byte-for-byte identical
  to `main` in the diff. ✔
- Spec Goal 3 (every wrap-boundary assertion derived from `CAPACITY`): the five
  named tests plus the new one, all `CAPACITY`-relative; the sweep proves it. ✔
- Spec Goal 4 (`static_assert` on `sizeof(SavedPosition)`): `ReturnStack.h:61-64`.
  The doc reference in its message, `2026-09-13-berean-os-design.md:511`, lands on
  the right paragraph (`SavedPosition` is `(spineIndex, pageNumber)` and needs Unit
  addressing before persistence). ✔
- Spec "Comments — three that go false": `ReturnStack.h:5-7` re-stated without a
  count, `ReturnStackTest.cpp:5-8` likewise, `.claude/agents/ui-dev.md:43-44`
  updated to 16. All three done. ✔
- Every spec non-goal is honoured: no UI, no sticky-oldest, no input-layer edit,
  no persistence or format version, no `SavedPosition` widening, no new `unpush()`
  caller. `grep -rn "ReturnStack\|SavedPosition" src lib freeink-sdk test` returns
  only the pre-existing call sites. ✔
- Plan Steps 3 and 5 are pasted verbatim: I diffed the shipped
  `src/activities/reader/ReturnStack.h` against the plan's Step 5 block and the
  shipped test file against Step 3's; they match. No unexplained divergence. ✔
- **No scope expansion.** The non-test, non-doc surface is 12 added and 3 removed
  lines in one header. The four `docs/superpowers/` files are this repo's standard
  research → spec → review → plan trail, not extra product scope.

---

## MINOR 1 — `ROADMAP.md:73` and the design doc still say the capacity is 3

`.claude/agents/ui-dev.md:44` was updated precisely because, in the spec's own
words, "left stale, the next `ui-dev` task starts from a wrong number"
(`…design.md`, *Comments*, MAJOR 3). Three sibling statements carry the same stale
constant and were left:

- `ROADMAP.md:73` — "the `ReturnStack` capacity, **which is 3 today** and evicts
  the oldest silently, before it becomes the primary Back"
- `docs/superpowers/specs/2026-09-13-berean-os-design.md:214-216` — "has
  `CAPACITY = 3` and silently evicts the oldest on push … after a **fourth**
  citation, Back walks a path the user did not take"
- `docs/superpowers/specs/2026-09-13-berean-os-design.md:658` — "**`ReturnStack`
  capacity** (**currently 3**, silent eviction)"

The PR's stated reason for leaving them (`…design.md` *Comments*; plan Step 6) is
sound as far as it goes: the Phase 2 open item is genuinely only half closed, and
striking the lines wholesale would over-claim. But that argument is about the
item's *status*, not about the embedded number. "Currently 3" and "after a fourth
citation" are now simply false, and correcting the constant while leaving the open
item open claims nothing extra — which is exactly the reasoning applied to
`ui-dev.md`. `CLAUDE.md:2` points every session at the design doc, so this is the
same failure mode one file over.

Fix inline: update the constant in those three places and leave the open-item
framing alone. It is a MINOR, not a MAJOR, because the behaviour asked for is
fully delivered and the omission is disclosed with reasoning rather than silent.

## MINOR 2 — the new test-file header comment overclaims

`test/return_stack/ReturnStackTest.cpp:8` now reads:

```
// CAPACITY is: every bound here is derived from it, never written as a literal.
```

Literal bounds remain, deliberately and correctly: `:55`
`EXPECT_EQ(stack.count(), 3)` in `PopsInLifoOrder`, and `:169`
`EXPECT_EQ(stack.count(), 2)` in `UnpushOnEmptyLeavesTheRingUsable`. The plan
settles this — "`PopsInLifoOrder` keeps its literal 3 deliberately … it is not a
boundary test. Leave it" (`…plan.md:373-375`) — and the spec's actual Goal 3 is
scoped to *wrap-boundary* assertions, which is met. So the code is right and only
the sentence is wrong.

Given this PR spent a whole spec-review MAJOR on comments that go false, the new
comment should not itself be false. Narrow it to the wrap boundary, e.g. "every
*wrap-boundary* bound here is derived from it". Fix inline.

---

## Not this review's business, recorded so stage 2 does not re-derive it

- The device checks in the PR body's *Not verified here* list (four-deep chain and
  swipe behaviour, reopen position, heap) are correctly flagged as human-only and
  are not gaps in the PR.
- `pio run` / `pio check` were not re-run here: the branch is green in CI, no
  finding depended on a firmware build, and a cold build costs 10-20 minutes.
  `ctest`, the capacity sweep, the mutants and the format gate were all run.

Minors: 2, both documentation-accuracy and both fixable inline.

VERDICT: CLEAR
