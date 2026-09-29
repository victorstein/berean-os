Tier: heavy

# PR #193 review — intent (pass 0)

Reviewed: `gh pr diff 193` (branch `fix/188-full-verse-passages`, head `aed25ca4`) against issue #188,
the spec `docs/superpowers/specs/2026-09-29-issue-188-design.md` and the plan
`docs/superpowers/plans/2026-09-29-issue-188-plan.md`. Host suites touched by the PR were rebuilt and
run locally (`ctest -R "UnitText|PassageDoc|StudySleep|PassageFile"`: 158/158 passed).

## Summary

The PR does what the issue decided, and the spec's harder parts are built too, not just the easy
half. It snaps to whole verses from the source XHTML, stores the text uncapped in a v4 file, repairs
old rows on open, and fits the sleep screen whole-or-skip with fit decided before sampling. The spec's
harder parts that are present: the PSRAM allocator threaded through every `JsonDocument`, the
`overflowed()` refusals, the fingerprint-mismatch guard, the two-phase bounded repair with a rotating
schedule, and the undo on a failed save. I found no BLOCKER and no MAJOR. One MINOR is below.

## Acceptance criteria (issue #188)

| Criterion | Met | Evidence |
|---|---|---|
| Mid-verse selection stores the full verse(s); host test | Yes | `StudyStore.cpp:316-326` (snap → `rangeText` → refuse on empty); `test/unit_text/PassageSpanTest.cpp:51` asserts both the snapped offsets and the extracted text `"Cuatro cinco. Seis siete."` with the footnote excluded |
| > 384 B stored whole, file round-trips with no loss; host test | Yes | `PassageDocTest.cpp:906` (`ALongWholeTextRoundTripsByteForByte`), `test/storage_io/PassageFileTest.cpp:162` (through the real file path, past the old cap) |
| Repair makes every passage whole; host test for a v1/v2 snippet-only row and a v3 capped `"w"` | Yes (planner level) | `TextRepairTest.cpp:61` and `:68`, plus `PassageDocTest.cpp:1055` (`SetWholeTextReplacesALegacyRow`). `repairTexts` itself is device-only; the PR body says so and names the four behaviours left to device check 3 |
| Sleep never shows `…` or a partial verse; unfit passage not picked; host test on fit and pick | Yes | `StudySleepFit.h` loses the ellipsis tail entirely; the gate runs before `offer` (`StudySleepScreen.cpp:225-230`); `offer(..., fits)` drops unfit rows; draw re-checks and returns false rather than cut (`:518`). Tests: `StudySleepFitTest.cpp:56,62,85`, `StudySleepPickTest.cpp:182,195` |
| Over-budget save refused with a UI report, not truncated | Yes | `PassageDoc::add` / `setWholeText` refuse (`PassageDocTest.cpp:951,1068`); capture maps a false `addPassage` to the existing `STR_HIGHLIGHTS_SAVE_FAILED` (`PassageSelectActivity.cpp`, unchanged path) |
| Device heap at the three points | Instrumented | `logStudyMemory("Study open")`, `("Passage added")`, `logSleepMemory("Study pick")` log internal free, internal min and PSRAM free. Device check 7 in the PR body |

The issue's constraints are met:

- **Format:** the version goes to 4, and older builds refuse the new file.
- **Docs:** `docs/file-formats.md` is updated.
- **Memory:** texts live in PSRAM (D1), with the mechanism justified.
- **Verse resolution:** it reuses `UnitAnchors` and `UnitTextScanner`, not new parsing.
- **Repair:** it is idempotent (whole rows are skipped), bounded (3 s, and at most 2 book searches),
  saved atomically through `PassageFile::save`, and never downgrades a row.
- **Known limit:** a row whose start can't be recovered is rebuilt and logged as `suspect start`.
- **List rows:** they still elide `"x"`.

## Spec coverage

I checked A1–A24 against the code. Each is implemented as written, including the parts most likely
to be dropped:

- **A2:** snapping keys on `units.kind`, uses `end - 1` with a clamp, and handles a start before the
  first anchor (`PassageSpan.h:412-431`).
- **A5 and A7:** the capture filter drops `aside`, `noteref`, `sup`, `w_ch` and `ss`/`sd`, plus the
  separator after a number. It adds one space at block ends and leaves the counting untouched
  (`UnitText.cpp`). `TheDefaultExtractionIsUnchanged` guards the unfiltered path.
- **A9 and A10:** there is a legacy v3 path inside v4 files. `"h"` in a pre-v4 file is refused, and
  `toJson` never writes `"h"` without text.
- **A13–A16:** the seven rungs shed chrome in the order the spec gives. The prefilter is applied
  before measuring, the draw-time rung is recomputed with the real chrome, and `Candidate::text` is
  bounded by the prefilter.
- **A17–A19:**
  - Repair only runs when saving is enabled.
  - Rows found at their spine hint go first. At most two rows per pass may search the whole book, and
    a deferred row is not marked attempted (`StudyStore.cpp:465`).
  - Each edit is aborted before any change if its backup copy fails.
  - A failed save restores every row in reverse order (`:525`).
- **A24:**
  - One `ArduinoJson::Allocator`, in `src/study/PsramJsonAllocator.cpp`.
  - `PassageDoc` has injected allocators, and `measureBytes` returns `SIZE_MAX` on overflow.
  - `PassageFile::save` refuses on `overflowed()`.
  - There is a new `loadAdopting` overload, and the sleep scan's `JsonDocument` is on PSRAM.

**Deviations from the spec, all explained:**

- The tests live in the existing `test/unit_text/` and `test/passage_doc/` suites, not the new
  `test/passage_span/` and `test/text_repair/` suites the spec named. The plan's "Shared file" note
  states this, and so does the PR body.
- `MigrationPlanner` is unchanged. The plan explains why: its rows never touch `displayText`.

**Scope:** it has not grown. The only additions beyond the spec are the heap and diagnostic log
lines, and the spec asked for them.

## Findings

### MINOR

1. **The legacy branch of `PassageDoc::add` still cuts text at a byte cap.**
   - `lib/StudyStore/StudyStore/PassageDoc.cpp:139-141` truncates a non-whole row's `displayText` to
     `V3_MAX_DISPLAY_TEXT_BYTES` through `utf8SafeSummary`. `test/passage_doc/PassageDocTest.cpp:676`
     (`IsCutAtTheCapWithoutSplittingACodepoint`) keeps that behaviour as a passing test.
   - No production caller can reach it today:
     - `MigrationPlanner` never sets `displayText`.
     - `removePassage`'s rollback re-adds a row that `fromJson` already limited to 121..384 bytes.
     - The repair undo uses `replace`, not `add`.
   - Still, it is the one place left where the code would cut passage text, against the issue's
     "No byte cap may cut passage text". Spec A9 only asks that such rows are "validated as today" on
     load.
   - Fix: make the legacy branch refuse a text over the cap, as `fromJson` does, instead of
     summarising it. Invert the test to assert the refusal.
   - It has no effect on user data, so it doesn't gate this PR.

## Checked, not findings

- **Word-end extension:** spec A4 says the word-end extension stops at "the first U+0020 or the
  unit's end (`unitEndOffset`)". The code stops at an ASCII space or when a block element closes
  (`UnitText.cpp` `shouldCapture` / `onEnd`), and the plan specifies it that way. In a Paragraph
  document the unit is the `data-pid` block, so the two rules land in the same place. A
  DocumentOffset document has no unit end at all.
- **Repair failures are only logged:** the repair's over-budget, save-failure and OOM outcomes go to
  the log, with no UI message. Spec A19 and the error-handling table decide that explicitly. The
  issue's "UI report" criterion is about a save the user starts, and capture meets it.
- **Render lock:** `repairTexts` runs from `EpubReaderActivity::loadBook`, which `ReaderActivity::onEnter`
  calls with no `RenderLock` held (`ReaderActivity.cpp:53`). A17's precondition holds.
- **Sleep-scan file cap:** the sleep scan's per-file cap (`MAX_FILE_BYTES = SAVE_BYTE_BUDGET + 4096`,
  `StudySleepScreen.cpp:45`) still covers the largest v4 file the budget allows.
- **Test quality:** the tests check behaviour, not how the code is written:
  - The span tests assert the extracted text, not only the offsets.
  - The repair tests run the real filter on an NWT-shaped fixture.
  - The sampler tests assert that an unfit passage is never picked.

VERDICT: CLEAR
