Tier: heavy

# Issue #182 — spec review 0

Reviewed: `docs/superpowers/specs/2026-09-27-issue-182-design.md` (at `f27705b9`), against
`gh issue view 182 --repo victorstein/berean-os` and
`docs/superpowers/research/2026-09-27-issue-182-research.md`. Every cited line below was read in
this worktree.

Most of the design holds up when checked against the code. D1 (store at save time), D2 (a `"w"`
field written only when needed, with v1/v2/v3 chosen by content), A2–A5 and the version gate on the
sleep path (`StudySleepScreen.cpp:141-142` reads `PassageDoc::FORMAT_VERSION`, so bumping it to 3
covers the sleep scan) all match the code. The chrome arithmetic in R§4 checks out:
699 − 307 = 392 px. The U+2026 glyph is present at all four italic sizes (the `{ 0x2000, 0x2064 }`
interval in each `lib/EpdFont/builtinFonts/notoserif_{12,14,16,18}_italic.h`). A1 is a sound reading
of the issue: its fixed 12pt floor, its fixed chrome and its own ellipsis fallback outrank an example
that can't be met under them. D4's 1,030 B figure is correct (512 × 2 escaped bytes + key). The
research note's 784 B was wrong, and the spec has already corrected it.

One premise is false, though, and it is on the device's main way into selection.

## BLOCKER

### B1 — The long-press entry never sets `anchorOffset`, so the stored range is wrong as well as the text

**Claim.** Problem §1: "The stored unit range is correct, because `selectionRange` falls back to
`anchorOffset` (`:192-195`). Only the text is wrong." Architecture §5 hooks the builder reset into
`commitAt` "when the first anchor is set (`:308-315`)". This treats `commitAt` as the only place an
anchor is set.

**Problem.** There are two places that set an anchor, and only one of them sets `anchorOffset`:

- `commitAt` sets both `anchorIndex` and `anchorOffset`
  (`src/activities/reader/PassageSelectActivity.cpp:310-311`).
- The long-press path in `onEnter` sets `anchorIndex`, `cursor` and `phase = PickingEnd`, but never
  sets `anchorOffset` (`:74-81`). `anchorOffset` stays at `NO_ANCHOR` (`PassageSelectActivity.h`,
  `uint32_t anchorOffset = NO_ANCHOR`).

`git blame` shows the gap is a regression: `anchorOffset` came in with `2698b71c` (2026-09-08), and
long-press came later with `7087e6d6` (2026-09-14) without setting it.

`selectionRange` returns the end word alone whenever `anchorOffset == NO_ANCHOR`. It does this
**regardless of `anchorIndex`** (`:175-177`). So on every selection started by a long-press:

- the saved range is `{end, end+1}`, on the same page and across a page turn alike. `finalizeSelection`
  saves that range (`:372`, `:384`);
- `verseReference(range.start)` resolves the **end** word's verse (`:384-385`);
- the in-progress outline shows only the cursor word (`:489`).

Long-press is the documented, touch-first way to start a selection:

- `EpubReaderActivity.cpp:449-455` calls `openHighlightPassageAt`, which passes the touch coordinates
  through (`:307-313`, `:336-339`);
- `USER_GUIDE.md:141` lists "Start a passage selection | long-press a word outside the centre third".

With the spec as written, a long-press selection across a page turn would store:

- a `"w"` that begins at the anchor word (the builder is fed from `anchorIndex`);
- a start unit, fingerprint, reference and highlight paint that begin at the **end** word.

The passage shows text from, say, Gálatas 5:3 under the reference "Gálatas 5:4", and the reader
highlights only 5:4. Device check 4 ("confirm the text starts at the anchor word") would pass while
the saved passage is wrong. The issue's Fix §2 asks to "Keep the true start of the selection", and
the range start is part of that.

**Evidence.**
- `grep -rn "anchorOffset" src/` finds exactly one assignment, at `PassageSelectActivity.cpp:311`.
- `PassageSelectActivity.cpp:74-81` sets `anchorIndex` but not `anchorOffset`.
- `:175` has the test `if (phase == Phase::PickingStart || anchorOffset == NO_ANCHOR) return VisibleRange{endOffset, endOffset + 1};`.

**Fix.**
1. Amend Problem §1: the range is correct only when the anchor came from `commitAt`. After a
   long-press the range, the reference and the fingerprint all begin at the end word, even on the
   same page.
2. Add a single `setAnchor(int index)` in `PassageSelectActivity`. It sets `anchorIndex`,
   `anchorOffset = words[index].offset` and `phase`, and resets the `PassageLabelBuilder`. Call it from
   both `commitAt` (`:309-315`) and the `onEnter` long-press branch (`:76-80`). Architecture §5 should
   name both call sites.
3. Add device checks: long-press a word in verse N, turn the page, end in verse N+1, then save.
   - The Highlights list shows reference N.
   - The reader paints from verse N.
   - The outline covers the whole span while you pick.

   Do the same with the start and end on one page.
4. Say in the spec why there is no host test for this: the activity isn't host-testable, per R§5.
   The fix is two lines, and the device check covers it.

## MINOR

### m1 — The spec doesn't say when `add()` normalises `"w"` relative to the length check, and the choice decides whether A4 can latch a good store

- **Claim.** Data flow: "`displayText=label if >120 else ""`". A6 says `add()` also runs
  `utf8SafeSummary(displayText, 512)`.
- **Problem.**
  - `utf8SafeSummary` collapses whitespace runs, strips `'\n'` and trims (`lib/Utf8/Utf8.cpp:185-201`).
  - Suppose the > 120 test runs on the raw string and the summary runs after it. A text that is
    > 120 B raw but ≤ 120 B once summarised is then written as `"w"`.
  - A4 refuses such a `"w"` on load, and saving latches off (`FormatVersion.h:22-33`).
  - The rollback `passages_.add(backup)` in `StudyStore::removePassage` (`src/study/StudyStore.cpp:304-309`)
    also passes through `add()`. Its `displayText` must survive that second normalisation.
- **Fix.**
  - Specify the order: summarise, then clear when `size() <= MAX_SNIPPET_BYTES`.
  - Add two `PassageDocTest` cases next to the links rollback test (`test/passage_doc/PassageDocTest.cpp:570-572`):
    - a text that is > 120 B only because of doubled spaces writes no `"w"`;
    - `add(backup)` of an existing long passage keeps `displayText` byte-for-byte.

### m2 — A6 and A11 give two different ellipsis forms on the same screen

- **Claim.** A6 appends `" …"` (with a space) at capture. A11 renders the fitter's ellipsis as
  `word…` (no space).
- **Problem.**
  - Two truncation marks would look different to the reader.
  - The stored `" …"` is a separate space-delimited word, so the greedy wrap can leave `…` alone on
    the last line at 18/16pt.
- **Fix.** Make the builder append `"…"` to the last kept word, with no space, reserving 3 bytes.
  Change the `PassageLabelTest` expectation to match.

### m3 — The new test file isn't registered in the existing executable

- **Claim.** "`test/study_sleep_pick/StudySleepFitTest.cpp` (new file in the existing executable)".
  Registration lists only `add_subdirectory(passage_label)`.
- **Problem.** `test/study_sleep_pick/CMakeLists.txt:5-8` lists its sources explicitly (no glob). The
  new file is never compiled unless it is added there. A new `test/passage_label/CMakeLists.txt` is
  also needed; `test/return_stack/CMakeLists.txt` is the template.
- **Fix.** Name both CMake edits under "Registration and checks".

### m4 — The format change isn't documented in `docs/file-formats.md`

- **Problem.**
  - `CLAUDE.md` (Format versioning) says to document a binary or format change in
    `docs/file-formats.md`.
  - That file has no passages-file section. Its only `PassageDoc` reference cites
    `PassageDoc.h:20-24` (`docs/file-formats.md:483-484`), and those lines move once v3 and
    `LINKS_FORMAT_VERSION` are added.
- **Fix.** Add a short `/.berean/passages/<pubkey>.json` entry covering:
  - v1, v2 and v3, and the rule that the lowest sufficient version is written;
  - `"w"` and its 121..512-byte invariant;

  Also fix the line citation.

### m5 — Smaller clarifications

- The data flow lists `{51,45,40,34}` as line heights. The spec should say `FitSize.lineHeight` comes
  from `renderer.getLineHeight(fontId)` at draw time, not from literals. The UI rule is "No hardcoded
  dimensions". Today's code already reads it (`StudySleepScreen.cpp:289`).
- The `offerRow` comment "The same gates PassageDoc::fromJson applies" (`StudySleepScreen.cpp:92-93`)
  becomes more wrong under A5. Reword it to say the path is deliberately lenient per row.
- A5 needs no summary pass on `"w"`, so it can view `row["w"]` as a `const char*` plus length. That
  avoids a fresh ≤ 512 B `std::string` for every row in the scan loop (`:102`).
- D4 gives the new worst-case figure (~2,730 B) and the new ratio (≥ 70). It doesn't give the new
  `EXPECT_LT` bound that replaces `1700u` (`PassageDocTest.cpp:603`). State it, for example `< 2800u`.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 0
