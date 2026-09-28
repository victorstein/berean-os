Tier: heavy

# PR #183 — intent review 0

Reviewed `gh pr diff 183` (`fix/182-sleep-screen-fit-passage`, HEAD `f6685f08`) against issue #182,
the spec `docs/superpowers/specs/2026-09-27-issue-182-design.md` and the plan
`docs/superpowers/plans/2026-09-27-issue-182-plan.md`.

I also ran the three affected host suites on this HEAD: `PassageDocTest` 62/62,
`PassageLabelTest` 7/7, `StudySleepPickTest` 44/44. All pass.

## Issue acceptance, item by item

| Issue item | Where it is met | Status |
|---|---|---|
| Fix 1: the sleep screen draws the full text, not the 120-byte label | `"w"` is written at `PassageDoc.cpp:225-228`. `offerRow` prefers it at `StudySleepScreen.cpp:109-118` | Met, up to the 384 B cap (see m2) |
| Fix 1: evaluate resolve-vs-store, pick one and justify it | Spec D1 (spec:111-127). It picks option B, with the I/O and write hazards of option A cited | Met |
| Fix 1: bump the version, re-check `SAVE_BYTE_BUDGET`, keep older files loadable | `FORMAT_VERSION = 3` (`PassageDoc.h:25`). The lowest sufficient version is written (`PassageDoc.cpp:30-34` of the diff, `:217`). The budget is re-checked in D4 and in the tests `worst < 2500`, `>= 80` and 63×full `< 56 KB` | Met |
| Fix 1: pre-fix passages keep their snippet | `offerRow` falls back to `"x"` (`StudySleepScreen.cpp:112-118`); spec A7 | Met |
| Fix 2: a page-spanning selection keeps its true start | `advancePage` feeds the outgoing page before the swap (`PassageSelectActivity.cpp:149`). `finalizeSelection` feeds the final page (`:373-375`). A single `setAnchor` (`:300-306`) also fixes the long-press `anchorOffset` bug, which the spec found beyond the issue (1b) | Met |
| Fix 3: the largest of 18/16/14/12 that fits, italic | `PASSAGE_FONT_IDS` and `fitPassage` (`StudySleepFit.h:101-138`), drawn with `EpdFontFamily::ITALIC` | Met |
| Fix 3: ellipsis only at the smallest size, on a word boundary; never mid-word or at the start | `StudySleepFit.h:119-137`. Capture also stops on a word (`PassageLabel.h:30`) | Met |
| Fix 3: the chrome keeps its sizes | `chromeHeight` (`StudySleepScreen.cpp:315-320`) and the unchanged draw calls | Met |
| Fix 3: the fitter is pure, host-tested, and takes metrics as input | `StudySleepFit.h` (no Arduino), `MeasureFn` plus `FitSize.lineHeight`, `StudySleepFitTest.cpp` | Met |
| Host test: largest size / ellipsis only at the smallest / word breaks | `PicksTheLargestSizeThatFitsNotTheSmallest`, `OnlyTheSmallestSizeEllipsizesAndOnAWordBoundary`, `AnEllipsizedTextStartsAtItsFirstWordAndKeepsWholeWords`, `NoEllipsisWhenTheTextJustFits` | Met |
| Host test: a page-spanning selection keeps its start | `PassageLabelTest.cpp:30` | Met in letter (see m1) |
| Host test: old files load; an older reader refuses the new file | `OlderFilesStillLoad`, `AVersionTwoReaderRefusesAFileCarryingIt` | Met |
| Device checks | Carried over as device checks 1–8 in the PR body | Pending (human) |

## Spec coverage

Every spec requirement I traced is implemented:

- D2: the version table and v1/v2/v3 selection.
- D3′: the 384 B cap.
- D4: the budget test, and the `MAX_LINKS_PER_PASSAGE` comment rewrite at `PassageDoc.h:44-46`.
- A2: summarise, then clear at ≤ 120 (`PassageDoc.cpp:122-123`).
- A4: a malformed `"w"` refuses the load (`PassageDoc.cpp:85-95,273`).
- A5: a lenient per-row `"w"`, read in place.
- A6 and A11: the `word…` form.
- A8: split on ASCII space only.
- A9: an over-wide word stays whole.
- A10: at least one line is shown.
- A13: feed only after `next` is non-null.
- A15: `setAnchor`.
- Architecture §5: `LABEL_SCAN_BYTES` is removed and the `:374-376` comment is rewritten.
- Architecture §6: `MAX_SNIPPET_LINES` is removed. The `StudySleepPick.h:19` capacity comment and the `offerRow` comment are rewritten.
- Documentation: a `/.berean/passages/<pubkey>.json` section is added to `docs/file-formats.md`.

The only divergences from the spec are the renames the plan declares under "Names that differ from
the spec": `passage_label::Builder` with `text() const`, and `MeasureFn(const void*)`.

## Plan conformance

I extracted the `StudySleepFit.h` and `PassageLabel.h` blocks from the plan (Steps 4.3 and 2.3) and
diffed them against the shipped headers. Both are identical. The Task 3 edits match the plan's
listing, including the placement of `appendWords` in `advancePage` and the `displayText = snippet`
line in `StudyStore.cpp:287`.

The plan adds one refinement over the spec's fitter description: a size is rejected when any single
word overflows the column (`StudySleepFit.h:112`). That refinement is in the plan (Step 4.3) and is
tested by `AWordTooWideForALargerSizeMovesToASmallerOne`, so it is not an unexplained divergence.

The spec's m4 item says the `PassageDoc.h:20-24` citation in `docs/file-formats.md:484` is fixed.
It is unchanged, but plan Step 7.2 explains why: lines 20-24 are still the version comment. I
confirmed that with `sed -n 20,24p`.

## Scope

- **No expansion.** The long-press `anchorOffset` fix (1b) goes beyond the issue's wording, but it
  is the same defect: the stored start is wrong. The spec review raised it as a blocker. It is
  justified in the spec and disclosed in the PR.
- **Nothing is silently reduced.** The two reductions below are labelled in the spec and disclosed
  in the PR body.

## Findings

No BLOCKER. No MAJOR.

### MINOR

**m1 — The cross-page host test covers the builder, not the feed that fixed the bug.**

`PassageLabelTest.cpp:30` hand-slices `anchorPage[anchorIndex..]` and `finalPage[0..end]`, then
checks that the builder concatenates them. The defect lived in which ranges the activity feeds:

- `PassageSelectActivity.cpp:149` in `advancePage`;
- `:373-375` in `finalizeSelection`.

Neither of those ranges is exercised on the host. A regression there, such as feeding from 0
instead of `anchorIndex` or feeding before the null check, would pass every host test.

This is the approach the spec prescribes (spec:444-446, and A15 at spec:263-264: nothing in `test/`
can construct the activity), and device check 4 covers it. It satisfies the acceptance line in
letter. No change is required. Just note that device check 4 is the only real guard on Fix 2.

**m2 — "Show the whole passage" is bounded, as the spec labels it.**

- **A1 (spec:180-189).** The issue's example of 5–7 verses shown whole cannot be met in the fixed
  layout: about 375 B at 12pt against a median of about 705 B. The PR applies the issue's own
  fallback: 12pt, from the true start, ending `word…`.
- **D3′ (spec:148-155).** Capture is capped at 384 B for internal-SRAM reasons (A16). So a passage
  of 385–580 B ends `word…` even when the layout has no date, tag or strip and 12pt could hold more.

Both are disclosed under "Decisions to know about" in the PR body, and both follow the issue's rules
(the 12pt floor, the fixed chrome, and truncation only at the smallest size). This is informational
for the human tester reading device checks 2–3, not a defect.

## Verdict

The work does what issue #182 asks, within the constraints the issue itself sets. Every spec item is
implemented and the shipped code matches the plan. The tests exercise behaviour through public
interfaces, with fake metrics for the fitter and real JSON for the store. The PR explains every
divergence.

VERDICT: CLEAR
