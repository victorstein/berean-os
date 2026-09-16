# PR #47 — stage-1 intent review

**Branch:** `fix/28-bookmark-save-budget`
**Reviewed against:** issue #28 (brief + acceptance criteria),
`docs/superpowers/specs/2026-09-16-issue-28-design.md`,
`docs/superpowers/plans/2026-09-16-issue-28-plan.md`
**Scope:** intent only — does the PR do what was asked, completely, and nothing more. Code quality is
stage 2 and is not covered here.

---

## Verification actually run

| Gate | Result |
|---|---|
| `cmake --build build/test -j && ctest --test-dir build/test --output-on-failure -j` | **560/560 pass** |
| `~/.platformio/penv/bin/pio run` | **SUCCESS**, exit 0, 48.9 s (RAM 19.5%, flash 81.2%) |
| `gh pr checks 47` | Title Check, clang-format, cppcheck, lint-title, unit-tests all **pass**; Build x4pro pending at review time and confirmed locally |
| `git status --short` | clean; no gitignored artefact staged |

---

## Acceptance criteria, one by one

**1 — `save` measures the serialised document and refuses rather than truncates.**
Met. `src/util/BookmarkFile.cpp:94` measures, `:97-103` decides and returns `SaveResult::TooLarge`
**before** `Storage.mkdir` (`:106`) or any write (`:107`), so a refusal creates and modifies nothing.
The rule itself is `src/util/BookmarkSaveAction.h:24-29`.

**2 — the write goes through `writeDocToFileAtomic`.**
Met, `src/util/BookmarkFile.cpp:107`. The `.tmp` window that atomic writes open
(`lib/Serialization/PersistableStore.cpp:38-39` removes the destination before renaming) is closed on
the load side by the `PromoteTempAndUseIt` arm, `src/util/BookmarkFile.cpp:67-76`, reusing the
already-tested `highlightLoadAction` (`src/util/HighlightFileAction.h:34-47`) rather than rewriting it.

**3 — a refusal is reported to the caller and reaches the user.**
Met on every path. `SaveResult` replaces `bool` (`src/util/BookmarkFile.h:34`). In the reader,
`addBookmark()` arms the popup before any early return (`EpubReaderActivity.cpp:1777-1778`), maps five
outcomes through `bookmarkToastString` (`:1732-1750`), and the popup is drawn at `:1285`. In the
bookmarks list, a refused delete shows a message at `EpubReaderBookmarksActivity.cpp:204` and a failed
load shows one at `:43`. The reader-menu `TOGGLE_BOOKMARK` path (`EpubReaderActivity.cpp:871-873`),
which previously produced no feedback at all, now gets it — spec A-6, and the PR flags it as
human-verifiable (item 6).

**4 — `load` distinguishes "no file yet" from "file unreadable", and a failed load is not overwritten.**
Met, and I checked the closure is total rather than partial. `LoadResult`
(`src/util/BookmarkFile.h:19-24`) is fed by `readDocFromFileChecked`, which is the call that carries the
distinction (`PersistableStore.cpp:46-61`). `grep` over the whole tree finds exactly two `save()` call
sites — `EpubReaderActivity.cpp:1840` and `EpubReaderBookmarksActivity.cpp:196` — and both are gated:
the reader latches `bookmarksSaveDisabled` on `Failed` (`EpubReaderActivity.cpp:1764-1769`) and refuses
before mutating (`:1779-1783`); the list activity leaves in `onEnter` before any row exists
(`EpubReaderBookmarksActivity.cpp:35-48`). There is no third writer.

The A-9 hazard is real and correctly handled: the bail sets `isCancelled` before `finish()`
(`:44-46`), and `progressChangeResultHandler` does `std::get<ProgressChangeResult>(result.data)` on the
not-cancelled branch (`EpubReaderActivity.cpp:688`), which under `-fno-exceptions` would abort. I also
traced the `finish()`-from-`onEnter` sequencing: `Activity::finish()` sets `PendingAction::Pop`
(`Activity.cpp:24`) and `ActivityManager::loop()` `continue`s inside the same
`while (pendingAction != None)` loop after `onEnter` (`ActivityManager.cpp:162-165`), so the activity's
`loop()` never runs against the un-built list. `PassageSelectActivity.cpp:29-33` is the existing
precedent for the same shape.

**5 — `pio run` succeeds.** Verified locally, exit 0.

**Brief constraints.** No diff touches `lib/I18n/translations/*.yaml` or
`lib/Serialization/PersistableStore.{h,cpp}`. The research requirement ("measure a real record, do not
estimate from the struct") was done and is now *pinned* rather than asserted:
`BookmarkDocTest.cpp:185-210` recomputes the per-record cost and fails if the budget stops holding the
218 records the no-cap decision rests on.

## Spec coverage

Every assumption A-1 … A-9 is implemented, not just the easy half. Spot-checks of the two that are
easiest to half-do:

- **A-5** (over budget on load is `Loaded`, not `Failed`) is a divergence *by omission* —
  `BookmarkDoc::fromJson` simply never measures — which is exactly the kind of thing that silently
  regresses. `BookmarkDocTest.cpp:162-183` guards it with a 300-record fixture asserted to exceed the
  read cap and required to load all 300.
- **A-3** (the shrink exception) is the decision the whole design turns on, and it is reachable end to
  end. Traced by hand for the 47,000-byte legacy file: load returns `Loaded` (under the 50,000 cap),
  one delete measures ≈46,800, `existingFileSize` (`BookmarkFile.cpp:18-26`) reports 47,000, and
  `bookmarkSaveAction` takes the `measuredBytes < bytesOnDisk` arm. The `"v":1` stamp adds 6 bytes,
  far less than the ~206 a deleted record returns, so the set genuinely walks back under budget one
  record at a time.

The rollbacks are correct, which matters because a wrong one would leave the page showing a bookmark
the card does not have. `addBookmark` collects `(index, entry)` pairs in ascending order before the
erase-remove (`EpubReaderActivity.cpp:1798-1806`) and re-inserts in that order (`:1851-1855`) — for a
match set at indices 1 and 3 of `[A,B,C,D]` that restores `[A,B,C,D]`, not `[A,B,D,C]`. The
insert-then-fail branch erases `begin()`, matching the `insert(begin(), …)` it undoes.
`deleteSelectedBookmark` restores at the saved index and rebuilds the row cache a second time, and
returns before the selector fix-up so `nav.selected` stays valid against the restored list.

## Plan divergences

One, and the PR explains it: `scripts/gen_i18n.py` greps every source file for the `STR_*` identifier
pattern and fails the build on a key absent from `english.yaml`, so the plan's step-7b comment — which
named the three unlanded keys — could not ship. The comment at `EpubReaderActivity.cpp:1743-1748`
describes them without naming them and says why. Everything else matches the plan step for step,
including the `test/CMakeLists.txt` wiring and both new `CMakeLists.txt` files.

## Scope

No reduction I can find, and the expansions are all spec'd and small: the format version (CLAUDE.md
storage rule 4 requires it of every new store), the `.tmp` recovery arm (forced by the move to atomic
writes), the load-path summary re-bound, and A-6's toast ownership. Nothing unrelated is touched.

---

## Findings

### MINOR 1 — the PR body's test arithmetic is wrong

The body says "560/560 host tests pass (549 before; 11 added)". The two new suites register **17**
tests, not 11:

```
$ ctest --test-dir build/test -N | grep -cE "BookmarkSaveAction\.|BookmarkDoc"
17
```

6 in `test/bookmark_save_action/BookmarkSaveActionTest.cpp` and 11 in
`test/bookmark_doc/BookmarkDocTest.cpp`. Since no other test file is touched by the diff, the baseline
was 543, not 549. The total (560) and the pass rate are right; only the two derived numbers are wrong,
and they understate the work. Worth correcting in a PR that is otherwise precise about its evidence.

### MINOR 2 — A-7's "three lines in one file" is not what shipped

Spec A-7 (`specs/2026-09-16-issue-28-design.md`, §Error handling) says the three placeholder strings are
"routed through one function, `bookmarkToastString()`, so the swap is three lines in one file." Two of
the five refusal messages do not go through it: `EpubReaderBookmarksActivity.cpp:43` and `:204` call
`tr(STR_ERROR_GENERAL_FAILURE)` directly. This follows the *plan* (steps 8b and 8c), and the PR's
hand-back states the real swap sites — "three lines in `EpubReaderActivity::bookmarkToastString` plus
two calls in `EpubReaderBookmarksActivity.cpp`" — so the i18n task is not misled. The stale claim is in
the spec, not the hand-back; fix is a one-line correction to A-7.

### MINOR 3 — `MAX_RECORD_BYTES` headroom does not deliver the guarantee its comment claims

`src/util/BookmarkDoc.h:36-41` says a new field on `BookmarkEntry` "must fail here first".
`MAX_RECORD_BYTES = 420` against a worst case of 386 (I reproduced the 386 by hand from the fixture at
`BookmarkDocTest.cpp:142-160`: 140-byte xpath + 144-byte escaped summary + 20-byte wrapper + the
numeric fields) leaves 34 bytes. A small numeric field — `,"ab":0` is 8 bytes — would not trip it; only
a string-shaped addition would. The plan set this ceiling deliberately at measurement + ~10% and the
plan review cleared it, so this is not a divergence — but the comment promises more than the 34 bytes
can deliver, and a one-clause softening ("a field of any size" → "a string-shaped field") would make
the guard honest about what it catches.

---

## Assessment

The three claims most likely to be wrong in work like this — "this already exists", "this is already
tested", and "this builds" — all check out. The data-loss chain the issue describes is closed at every
link, the fix is reachable rather than theoretical, the shrink exception that makes an inherited
over-budget file recoverable is implemented and exhaustively pinned, and the two rollbacks are actually
correct rather than plausible. The human-verification list is honest about what the branch cannot
claim. All three findings are cosmetic and none touches behaviour.

VERDICT: CLEAR
