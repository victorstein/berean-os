# PR #47 — stage-2 code-quality review

**Branch:** `fix/28-bookmark-save-budget`
**Scope:** code quality only — is this written the way this codebase is already written? Intent,
scope and requirements were settled in `docs/superpowers/reviews/issue-28-pr-review-intent-0.md`
(CLEAR) and are not revisited here.
**Yardstick:** `CLAUDE.md` (comment convention, resource protocol, storage discipline) and the named
siblings `src/util/HighlightFile.{h,cpp}`, `src/util/HighlightFileAction.h`,
`src/study/PassageFile.{h,cpp}`, `src/study/TagPaletteFile.cpp`, `lib/Epub/Epub/HighlightDoc.{h,cpp}`,
`lib/StudyStore/StudyStore/PassageDoc.{h,cpp}`.

---

## Gates actually run

| Gate | Result |
|---|---|
| `cmake --build build/test -j && ctest --test-dir build/test --output-on-failure -j` | **560/560 pass** |
| `PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix` (whole tree, as CI does) | exit 0, tree left clean |
| `git grep bookmarkRemoved` (the field this PR removes) | no residue in `src/` or `test/` |

Firmware build and `pio check` were not re-run: no finding below depends on either, and stage 1
recorded both green.

---

## What the change gets right, briefly

The structural decisions are the ones this repo already makes, and they were verified against the
real siblings rather than assumed:

- `BookmarkFile::load` is `PassageFile::load`/`TagPaletteFile::load` line for line, and it **reuses**
  `highlightLoadAction` (`BookmarkFile.cpp:12,55`) instead of restating the `.tmp` decision — the
  same helper `PassageFile.cpp:72` and `TagPaletteFile.cpp:39` already switch on. No fourth copy of
  that switch was created.
- `LoadResult` / `SaveResult` carry the same enumerators and the same comment wording as
  `HighlightFile.h:21-37` and `PassageFile.h:21-32`, so a reader who knows one knows all three.
- `SAVE_BYTE_BUDGET` takes `persist::DEFAULT_SAVE_BUDGET` (`BookmarkDoc.h:30`) rather than repeating
  `45000` the way `HighlightFile.h:42` does — the newer, better of the two existing habits.
- The two new test directories mirror `test/highlight_file/` and `test/save_budget/` exactly in
  CMake shape, include dirs and link set, down to the header comment explaining why only the pure
  logic is host-buildable.
- Not one comment in the diff restates the line beneath it; every one carries a "why".
- The decision *not* to widen `highlightSaveAction` is argued in the spec
  (`specs/2026-09-16-issue-28-design.md:98-102`) with the correct user counts, so the second
  save-decision helper is a recorded choice rather than an oversight. I am not raising it.

All six findings below are MINOR and fixable inline.

---

## Findings

### MINOR 1 — the 72-byte summary bound is now stated twice, and the write path is the copy that is not named

`BookmarkDoc.cpp:43` re-bounds a summary read from the card with a named constant:

```cpp
bookmark.summary = utf8SafeSummary(obj["summary"] | "", MAX_SUMMARY_BYTES);   // 72
```

The *creation* path does not use that constant. `BookmarkUtil.cpp:14` relies on the library default:

```cpp
std::string BookmarkUtil::sanitizeBookmarkSummary(std::string summary) { return utf8SafeSummary(std::move(summary)); }
```

and `lib/Utf8/Utf8.h:32` is `utf8SafeSummary(std::string passage, size_t maxBytes = 72)`. The two
agree today only because both happen to be 72.

Both named siblings deliberately avoid this. `PassageDoc` passes `MAX_SNIPPET_BYTES` on *both* sides
(`PassageDoc.cpp:28` on add, `:116` on parse); `HighlightDoc` uses the bare default on *both* sides
(`HighlightDoc.cpp:62` and `:159`). Either idiom is drift-proof; this PR is the first place in the
tree where one side names a constant and the other takes an unrelated header's default.

It is latent, not a live defect, and the blast radius is cosmetic (a summary written longer than 72
would be silently clipped on the next read) — but `MAX_RECORD_BYTES`, the record arithmetic in
`BookmarkDocTest.cpp:203-206` and the ≈218-bookmark figure A-2 rests on are all calibrated on 72, so
a change to the `lib/Utf8` default would quietly invalidate them.

Fix is one line: `utf8SafeSummary(std::move(summary), BookmarkDoc::MAX_SUMMARY_BYTES)` in
`BookmarkUtil.cpp:14`.

### MINOR 2 — `bookmarkSaveAction`'s documented contract for `bytesOnDisk` does not describe how its only caller uses it

`BookmarkSaveAction.h:21` states the contract:

```
// `bytesOnDisk` is the size of the file being replaced, or 0 when none exists.
```

`BookmarkFile.cpp:97` deliberately breaks it:

```cpp
const size_t bytesOnDisk = (measured > BookmarkDoc::SAVE_BYTE_BUDGET) ? existingFileSize(path) : 0;
```

Under budget, a file that *does* exist is reported as 0. That is safe — and the optimisation is
worth having, since it keeps the common path off the SD bus — but it is safe only because of the
branch *order* inside the callee, which the contract comment does not license. A future edit that
reorders those three lines, or a second caller that reads the contract literally, would be reasoning
from a comment that is not true of the code.

The same gap shows up in the suite: `BookmarkSaveActionTest.cpp:21` asserts
`bookmarkSaveAction(13200, 47400, …) == Write` — a `(measured, bytesOnDisk)` pairing the production
caller can never produce. That is fine as a property of a pure function, but it means the test
suite does not distinguish the rule from the caller's use of it.

Fix: extend the contract line to "…or 0 when none exists **or when the caller has not measured it,
because a document at or under `budget` is written regardless**."

While in that file: `BookmarkSaveActionTest.cpp:14-15` is asymmetric —
`constexpr size_t BUDGET = 45000;` is a literal on the line above
`constexpr size_t CAP = persist::SD_READ_TRUNCATION_CAP;`. The production header was specifically
changed to stop repeating 45,000 (`BookmarkDoc.h:30`, and the plan review that forced it); the test
reintroduces the literal one line away from a constant it does take.

### MINOR 3 — test and source comments carry review-pass and spec-assumption provenance, which nothing else in the tree does

`CLAUDE.md` § Comments: *"write them for the merged state, as if the code had always worked this way.
Remove before/after narration, investigation measurements, and rationale that belongs in the commit
message."* Four places read as artifacts of the pipeline that produced them rather than of the code:

- `BookmarkSaveActionTest.cpp:33` — `// The pass-0 blocker, pinned: a 230-record legacy file minus one entry.`
- `BookmarkDocTest.cpp:163` — `// A-5, and the one place BookmarkDoc deliberately diverges from PassageDoc:` …
  `// without this test nothing stops the next person reintroducing it.`
- `BookmarkDocTest.cpp:186,203` — `// The figure the "no record cap" decision rests on` / `// 218 is the figure A-2 rests on`
- `EpubReaderActivity.cpp:1743-1744` — `// Bookmarks have no refusal strings of their own yet and this task must not`
  `// edit the translation YAML; the three keys they want are named in the PR.`

Evidence that this is new rather than the house style: grepping `test/` for `pass-0|pass 0|blocker|A-[0-9]`
matches **only** the two files this PR adds (plus a Swedish hyphenation fixture whose hit is the
letter A). `grep -rn "the spec" test` returns nothing.

The substance under these comments is worth keeping and mostly non-obvious — the `gen_i18n.py` grep
gotcha at `EpubReaderActivity.cpp:1745-1748` in particular is a real trap and must stay. What should
go is the half that points outward at ephemeral artifacts: "the pass-0 blocker", "A-5", "A-2", "this
task", "named in the PR". A reader at `git log` distance has no way to resolve any of them, and the
PR body is not durable. `HighlightsActivity.cpp:194` ("same as before this task") is a single
pre-existing instance of the same slip, not a precedent to build on.

### MINOR 4 — the bookmark-match predicate is written out twice in the same function, and the collecting loop has no `.reserve()`

`EpubReaderActivity.cpp:1801-1806` collects matches:

```cpp
std::vector<std::pair<size_t, BookmarkEntry>> erased;
for (size_t i = 0; i < cachedBookmarks.size(); ++i) {
  if (bookmarkMatchesProgress(cachedBookmarks[i], currentSpineIndex, currentPage, pageCount, pageRange)) {
```

and `:1810-1814` immediately re-derives the identical set through a lambda with the same five
arguments. The rollback at `:1849-1853` restores exactly what the first loop captured, so the two
must agree — if someone edits one call and not the other, the rollback silently reinserts the wrong
entries, and this is the one path in the change with no host test behind it. Erasing from the
collected indices (descending) would leave a single statement of the rule.

`erased` also misses `.reserve()` before an `emplace_back` loop, which `CLAUDE.md` § resource
protocol item 7 states without exception, and which the file itself honours two functions away
(`EpubReaderBookmarksActivity.cpp:69-70`, `EpubReaderActivity.cpp:1755`). The realistic count is 0 or
1, so this is a convention point rather than a memory one — the fix is `erased.reserve(1)` or
hoisting the predicate.

Related, one line down: the rollback sets `currentPageBookmarked = wasBookmarked;` (`:1856`) by hand
when `updateBookmarkFlag()` (`:1864`) exists to derive exactly that from `cachedBookmarks`, which the
rollback has just restored. The manual assignment matches what the pre-change code did, so it is not
a regression — but calling the deriving function on the path that has just mutated the vector is the
form that cannot drift.

### MINOR 5 — the latch's comment claims a session scope the field does not have

`EpubReaderActivity.h:49-51`:

```
// Latched when a bookmark file failed to READ: … nothing may be written over them for the rest of the
// session. Never cleared, exactly like StudyStore's own saveDisabled_.
```

and `EpubReaderActivity.cpp:1767` copies StudyStore's log line verbatim: `"Bookmarks unreadable;
saving disabled for this session"`.

The comparison does not hold. `StudyStore` is a singleton (`StudyStore.h:120`, `#define STUDY
StudyStore::getInstance()`) and `StudyStore.cpp:58-60` explicitly keeps `saveDisabled_` across
`closePublication()` — *"it is a property of the session's knowledge … not of one book."*
`EpubReaderActivity` is constructed per book open (`ReaderActivity.cpp:29`,
`makeUniqueNoThrow<EpubReaderActivity>`) and destroyed on exit, so `bookmarksSaveDisabled` is
per-open, not per-session.

The **behaviour is right** — a bookmark file is per book, so a different book must not inherit
another's latch, and re-opening re-runs `loadCachedBookmarks()` which re-latches if the file is still
unreadable. Only the comment and the log string overclaim. Reword to "for as long as this book is
open", and drop the StudyStore equivalence or state it as the deliberate difference it is.

### MINOR 6 — `MAX_RECORD_BYTES` has no runtime consumer, and `BookmarkDoc.h` includes `<string>` without using it

`BookmarkDoc.h:43` declares `MAX_RECORD_BYTES = 420` under the longest comment in the file (eight
lines). Grepping the tree, its only reader is `BookmarkDocTest.cpp:156`. Every other constant in
`BookmarkDoc.h` is enforced by the code — `FORMAT_VERSION` at `BookmarkDoc.cpp:31`,
`MAX_SUMMARY_BYTES` at `:43`, `SAVE_BYTE_BUDGET` at `BookmarkFile.cpp:97` — and the sibling caps it
sits beside (`HighlightDoc.h:23-30`) are all enforced in `HighlightDoc::fromJson`. This one is a test
expectation living in a production header.

This is a judgement call and there is a defensible reason to leave it: someone adding a field to
`BookmarkEntry` opens `BookmarkDoc.h`, not the test, and the constant is what tells them the ceiling
exists. If it stays, that is fine — but it is the one place where the header states something the
firmware never evaluates, and the alternative (constant plus comment in `BookmarkDocTest.cpp`,
referenced from a one-line note on `BookmarkEntry`) loses nothing.

Trivial, same file: `BookmarkDoc.h:7` includes `<string>`, which the header never names —
`std::string` arrives with `BookmarkEntry.h`. `<cstddef>` and `<vector>` are both used.

---

## Assessment

The three questions the brief puts first all come back clean. The change **reuses** rather than
re-implements: `highlightLoadAction` is included and switched on, not copied, which is what
`PassageFile` and `TagPaletteFile` already do with the same helper, and the one genuinely new rule
(the shrink exception) is isolated in a `constexpr` free function shaped like its neighbour and
exhaustively host-tested at its boundaries. Naming, enum shape, comment voice, CMake wiring and the
`Doc`/`File` split all match the siblings closely enough that the files read as though they were
written in the same sitting. The tests are designed rather than present: they pin the *decisions*
(the shrink arm, the read-cap hard stop, absent-vs-zero version, over-budget-still-loads-in-full) and
several would fail loudly for the right reason if someone regressed them. No dead code, no
commented-out code, no comment that paraphrases its next line, no residue from the removed
`bookmarkRemoved` field.

What is left is six small consistency items. The one worth doing first is MINOR 1 — a bound stated
twice where both siblings deliberately state it once — because it is a one-line fix that removes a
future trap rather than a stylistic preference. Nothing here reverses a decision, changes scope, or
needs the human's judgement.

VERDICT: CLEAR
