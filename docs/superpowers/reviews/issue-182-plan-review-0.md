Tier: heavy

# Plan review 0 — issue #182

Plan: `docs/superpowers/plans/2026-09-27-issue-182-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-182-design.md`

## How this was checked

I did not take the plan's "applied, built, run, reverted" claim (plan:6-11) on trust. I re-ran it.

- I exported `HEAD` with `git archive` into a scratch directory outside the worktree and copied in the
  `freeink-sdk` submodule.
- A script applied every `replace:` / `with:` pair, every `Create` block and the Step 1.1
  last-line replacement, in plan order.
  - Every `replace:` anchor matched exactly once.
  - Three anchors end part-way through a line: `PassageDoc.h` Edit 4 (plan:362-365), and
    `StudySleepScreen.cpp` Edits 4 and 8 (plan:1509-1512, 1578-1582). They match as substrings,
    which is how an Edit tool applies them.
- **Host tests:** `PassageDocTest` 62/62, `PassageLabelTest` 7/7 and `StudySleepPickTest` 41/41.
  The full `ctest` run passed 1187/1187.
- **Firmware:** `pio run -e x4pro` returned `SUCCESS`, with no `warning:` or `error:` from
  `src/activities`, `src/study` or `lib/StudyStore`.
- **Red phases:** Steps 1.2, 2.2, 4.2 and 5.2 fail to compile before their implementation, as the
  plan says.
- **The worktree was not modified.**

## Spec coverage

Every spec requirement maps to a step:

| Spec requirement | Plan step |
|---|---|
| D2 version table | Step 1.5, Edit 1 |
| D3′ cap of 384 | Step 1.4, Edit 2 |
| D4 budget test and `MAX_LINKS_PER_PASSAGE` comment | Step 1.1, Edits 1–2; Step 1.4, Edit 3 |
| 56 KB test | Step 1.1, `TheUsersRealStoreWithAWholeTextEachStaysUnder56KB` |
| A2 normalisation order and the two tests for it | Step 1.5, Edit 2; `WhitespaceAloneNeverMakesAWholeText`, `ReAddingABackupKeepsIt` |
| A4 three refusals | `displayTextFromJson` plus three tests |
| A5 | Task 6, Edits 4–6 |
| A6 / A11 `word…` | `Builder::addWord`, fitter tail |
| A8–A10 | fitter tests |
| A13 / m2 feed placement | Step 3.2, Edit 2, after both `if (!next) return false;` checks and before the `RenderLock` |
| A15 `setAnchor` | Step 3.2, Edits 1 and 5 |
| Arch §5 comment rewrite, `LABEL_SCAN_BYTES` removal | Step 3.2, Edits 3, 4 and 6 |
| Arch §6 rename, `static_assert`, `MAX_SNIPPET_LINES` removal, header comment, `getLineHeight` at draw time | Tasks 5–6 |
| Format doc and citation | Task 7 |
| Device checks 1–8 in the PR body | Step 8.4 |

The Step 7.2 claim holds. After Task 1, `PassageDoc.h:20-24` is exactly the version comment, so
`docs/file-formats.md:484` stays accurate.

## FILES lock

These files are touched: `PassageDoc.h`, `PassageDoc.cpp`, `TaggedPassage.h`,
`PassageDocTest.cpp`, `PassageLabel.h`, `test/passage_label/CMakeLists.txt`,
`PassageLabelTest.cpp`, `test/CMakeLists.txt`, `PassageSelectActivity.h`,
`PassageSelectActivity.cpp`, `src/study/StudyStore.cpp`, `StudySleepFit.h`,
`StudySleepFitTest.cpp`, `test/study_sleep_pick/CMakeLists.txt`, `StudySleepPick.h`,
`StudySleepPickTest.cpp`, `StudySleepScreen.cpp` and `docs/file-formats.md`.

Every one appears on a column-0 `FILES:` line outside any fence (plan:45-51), as a repo-relative
path. None is missing.

## Findings

No BLOCKER or MAJOR findings.

### MINOR 1 — the codepoint-truncation test cannot catch a split codepoint, and would reject a correct cut

- **Claim.** `IsCutAtTheCapWithoutSplittingACodepoint` (plan:149-160) shows that `add()` cuts `"w"`
  at the cap on a UTF-8 boundary.
- **Problem.** It does not test that, in either direction.
  - **No split to catch.** `wholeText(484)` is `"Ustedes"` (7 B) followed by repeats of
    `" transformación"` (16 B). The cut at 384 falls 9 bytes into a repeat, so it ends on `'r'` of
    `transfor`. No multibyte character is ever at the boundary, so a broken `utf8SafeTruncateBuffer`
    would still pass.
  - **A correct cut would fail.** The assertion `(last & 0xC0) != 0x80` rejects any string whose
    last character is a complete multibyte one (`…ó` ends in `0xB3`). A later change to the cap or
    to `wholeText` could therefore fail a correct cut.
- **Evidence.** plan:101-105 builds the text. `lib/Utf8/Utf8.cpp:198-200` truncates to exactly
  `maxBytes` after trimming.
- **Fix.** Build the input so the cap falls inside a two-byte character, for example
  `std::string(MAX_DISPLAY_TEXT_BYTES - 1, 'a') + "ó" + " fin"`. Then assert:
  - `stored.size() == MAX_DISPLAY_TEXT_BYTES - 1`;
  - `stored` is a prefix of the input;
  - the input byte at `stored.size()` is not a continuation byte.

  Drop the two last-byte heuristics.

### MINOR 2 — the firmware command is pinned to another session's `/tmp` scratchpad

- **Claim.** Conventions (plan:32) and Steps 3.4, 6.2 and 8.3 (plan:973, 1664, 1764) run
  `/private/tmp/claude-501/-Volumes-stein-Documents-development-personal-berean-os/3749f345-…/scratchpad/pio-locked.sh`,
  and plan:32 says "never call `pio` directly".
- **Problem.**
  - The path is a different session's temporary scratchpad. It exists today, but nothing keeps it:
    a reboot or a tmp sweep removes it.
  - An implementer with no other context then has no sanctioned way to build.
  - `CLAUDE.md` names `pio run` as the build command.
- **Evidence.** The file is a 6-line `mkdir` lock around `/Volumes/stein/.platformio/penv/bin/pio`.
- **Fix.** Add one fallback line: if the wrapper is absent, run
  `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`, since `pio` is not on PATH. Alternatively,
  name the wrapper by what it is rather than by the path alone.

### MINOR 3 — Task 5 ends on an uncommittable tree

- **Claim.** Step 5.4 (plan:1447-1448) leaves `StudySleepScreen.cpp` naming `SNIPPET_CAPACITY` and
  `->snippet`, so the firmware does not build until Task 6. The two tasks are committed together.
- **Problem.**
  - Every commit is green, so this is not a broken-history issue.
  - But the brief asks that every step leave the tree committable, and this pipeline enforces task
    boundaries.
  - The split is avoidable. After Task 1, `MAX_DISPLAY_TEXT_BYTES + 1 == 385 == TEXT_CAPACITY`.
- **Fix.** Move the mechanical `StudySleepScreen.cpp` renames into Task 5: Edit 3's
  `static_assert`, `->snippet` → `->text`, and the `offerRow`/`offer` argument name. Commit Task 5 on
  its own, and leave Task 6 as the behaviour change. The file is already on a `FILES:` line.

### MINOR 4 — the A5 fallback in `offerRow` has no test

- **Claim.** Task 6 changes which text the sleep screen shows: `"w"` when it is a 1..384-byte
  string, otherwise `"x"` (plan:1535-1545). No test is written first.
- **Problem.**
  - The spec's Testing section does not ask for one, so the plan is faithful to it.
  - But A5's leniency is exactly the kind of branch that regresses silently: a non-string or
    over-cap `"w"` should fall back to `"x"`, not blank the row.
  - The decision is pure. It is two `const char*` values and a cap.
- **Fix, optional.** Extract `std::string_view study_sleep::rowText(const char* whole,
  std::string_view snippet)` into `StudySleepPick.h`, and add three `StudySleepPickTest` cases:
  a valid `"w"`, an empty `"w"`, and an over-cap `"w"`. If that is out of proportion, say so in
  Task 6 and cover it with device check 7.

### MINOR 5 — the API names differ from the spec without saying so

- **Claim.**
  - Spec §1 names `PassageLabelBuilder` with `take()`, and Architecture §2 names
    `MeasureFn = int (*)(void* ctx, …)`.
  - The plan ships `passage_label::Builder` with `text() const` (plan:645, 669) and a
    `const void* ctx` (plan:1157).
- **Problem.**
  - The plan is internally consistent, and the build proves it.
  - An implementer checking the plan against the spec, or a later reader of the spec, meets names
    that do not exist.
- **Fix.** Add one line under "Modelled on" noting the renames: `Builder`/`text()` because the
  builder is not consumed, and `const void*` because the callback only reads.

## Verdict

The plan is executable literally: every anchor applies once, the stated test counts are exact, and
the firmware builds clean. Each spec requirement has a step, and the file lock is complete. The
findings are all MINOR and fixable inline; none changes scope or reverses a decision.

VERDICT: CLEAR
