Tier: standard

# Issue #238 plan review 0

Plan: `docs/superpowers/plans/2026-09-30-issue-238-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-238-design.md`
Base: `2d033ef9` (branch `perf/238-go-to-latency`).

## How this was checked

- **Spec to plan mapping.** Every goal and assumption maps to a task:
  - goal 1 / A11 → Task 1;
  - goal 2 / A2 / A3 → Tasks 3 and 4;
  - goal 3 / A4–A8 → Tasks 5 and 6;
  - A9 → Tasks 2 and 7;
  - A10 → Task 9 (gated);
  - A10b / A12 → Task 10.
  - Nothing in `lib/Epub` is touched (non-goal), and neither are the shared append points.
- **Host tests executed.** Tasks 2, 3, 5 and 9.1–9.2 were applied verbatim to a scratch copy of
  `HEAD` (outside the worktree): every test file, header, and `CMakeLists.txt` append. All four
  suites build and pass with the counts the plan states: BookLabelsTest 5/5, SpineSearchTest
  13/13, BibleNavCacheTest 8/8, BandInkTest 4/4. Each one fails to compile before its header
  exists, so the red step is real.
- **Firmware `old_string`s checked against the tree.** Each of these matches exactly once, as
  quoted:
  - `BibleNavigationActivity.{h,cpp}`: constructor, `onEnter`, `loadBooks`, `loadChapters`, the
    constants block;
  - `BibleBookNameTable.{h,cpp}`: the declaration, `load()`'s tail, the `joinToc` loop;
  - `EpubReaderActivity.cpp:908-925` (`openChapterPicker`) and `:961-971` (`collectRecentChips`);
  - `EpubReaderActivity.h:14` and `:46`;
  - `ReaderActivity.cpp:55-58`, `LauncherActivity.cpp:410`;
  - `CoverBand.cpp:19-20, 54, 64-84`, `Masthead.cpp:78-85`.
- **APIs the plan relies on, confirmed:**
  - `Activity::render(RenderLock&&)` is virtual and `UiListActivity::render` is a non-final
    override that ends in `displayBuffer` (`Activity.h:35`, `UiListActivity.cpp:154-171`);
  - `TocEntry::spineIndex` is an `int16_t` (`BookMetadataCache.h:37`);
  - `Epub::getSpineItem`, `getTocItem` and `getSpineItemsCount` are `const` (`Epub.h:71-74`);
  - `BibleNav::filenameTail(std::string_view)` (`BibleNavScanner.h:75`);
  - `makeUniqueNoThrow` returns a default-deleter `std::unique_ptr<T>`, so it converts to
    `shared_ptr` (`Memory.h:25-27`);
  - `ReaderEntryIntent::Kind::GoTo` (`ReaderEntryIntent.h:13`) and `MODULE` in the launcher
    (`LauncherActivity.cpp:51`);
  - `BookGrid::MAX_SECTIONS` (`BookGridLayout.h:24`);
  - `Activity.h:2` brings `Logging.h` into `ReaderActivity.cpp`.
- **Consistency.** Member initialiser order in Task 6.3 matches the declaration order after
  Task 6.2: `entrySpine`, then `navCache`, then `goToStartMs`, so there is no `-Wreorder`. The
  `HrefAt` / `const void*` signature is used consistently across Tasks 3.1, 3.2 and 4.3. Task 4.1's
  `old_string` still matches after Task 2.3 inserts `setAbbreviations` below it.
- **Deviation 1 claim verified.** `Epub.h:3` includes `<Print.h>`, and `test/stubs` has no
  `Print.h`, so `BibleBookNameTable` cannot build on the host. The spec's own Testing 4 fallback
  applies.
- **`FILES:` lines** (plan lines 9–16) are at column 0, outside any fence, and use repo-relative
  paths. Every file any task touches appears on one, including Task 9.7's second edits to
  `BibleBookIndex.h` and `BibleNavigationActivity.cpp`. No task touches an unlisted file.

## Findings

### MINOR 1 — Task 9 changes the A10 mechanism, but the plan says its deviations are "packaging only"

- **Claim.** Plan line 40 says there are two deviations from the spec, "both packaging only".
- **Problem.** Task 9 (plan lines 1456–1464) swaps the spec's `readFramebufferRegion` /
  `writeFramebufferRegion` snapshot (spec A10) for an ink bitset recorded during the blit and
  replayed with `drawPixel`. That changes the mechanism, not the packaging. The reasoning holds:
  - `screenRectToAlignedMemRect` snaps memory-x, which is screen-y in portrait, outward to 8 px
    (`lib/GfxRenderer/GfxRenderer.cpp:348-352`);
  - the snapshot would be taken inside `drawFooter` after the grid has drawn
    (`BibleNavigationActivity.cpp:697-701`), so a restore could repaint stale rows outside the band.

  The spec's intent survives: reader-life lifetime in `BibleNavCache`, explicit PSRAM, no SD
  stream on repaint. The deviation is gated, too. But a reader of the deviations list would not
  learn that A10's mechanism changed.
- **Evidence.** Plan lines 40 and 1456–1464. Spec A10 (spec lines 153–160).
- **Fix.** Add a third entry under "Deviations from the spec":
  - what changes: A10's snapshot becomes a band-exact ink bitset, and the replay still plots
    ~56,400 pixels;
  - the reason, with the `GfxRenderer.cpp:348-352` citation;
  - and change the heading to drop "both packaging only".

### MINOR 2 — Three commits skip the formatter, so intermediate commits may not pass the CI format check

- **Claim.** Every step should leave the tree committable.
- **Problem.** Tasks 2.4, 3.3 and 5.3 commit newly created files without running
  `./bin/clang-format-fix`:
  - Task 2.4 commits `BookLabels.h` and `BookLabelsTest.cpp`;
  - Task 3.3 commits `SpineSearch.h` and `SpineSearchTest.cpp`;
  - Task 5.3 commits `BibleNavCache.h` and `BibleNavCacheTest.cpp`.

  The tasks that do format (1.6, 4.4, 6.6, 7) run `-g` before `git add`, which reaches only
  modified tracked files. Task 8's `git add -A && ./bin/clang-format-fix` repairs all of this
  before the PR, and the PR is squash-merged, so no breakage ships. The individual commits are
  still not CI-clean.
- **Evidence.** Plan lines 475, 740 and 1076. Root `CLAUDE.md`, "Formatting": `-g` skips new
  files once committed.
- **Fix.** In 2.4, 3.3 and 5.3, run `git add -A src test && ./bin/clang-format-fix`, then commit.
  Otherwise, state explicitly that Task 8 is the formatting gate for those commits.

### MINOR 3 — The comment on `joinToc` goes stale in Task 2

- **Claim.** Task 2.3 makes `setAbbreviations` the one filler of abbreviations, called by
  `load()` and, from Task 4.3, by the navigator.
- **Problem.** The header comment on `joinToc` still says "Clears every abbreviation; load()
  fills them afterwards." (`BibleBookNameTable.h:38-40`). After this change the navigator fills
  them too, through `setAbbreviations`. The repo's comment rule is to write comments for the
  merged state.
- **Evidence.** `src/activities/reader/BibleBookNameTable.h:38-40`. Plan Task 2.3 (lines 426–439)
  keeps that comment unchanged.
- **Fix.** In Task 2.3, change the comment's last sentence to "Clears every abbreviation; call
  setAbbreviations afterwards."

### MINOR 4 — The navigator's cache round-trip is verified only by the build and the device log

- **Claim.** Every step should start with a failing test.
- **Problem.** Tasks 1, 4, 6 and 7 have no host test. For Tasks 1, 4 and 7 that is inherent:
  logging, and wiring through `Epub` that cannot build on the host. The gap is Task 6's
  `copyBooksTo` / `adoptBooks` pair (plan lines 1248–1268). A field missed in one direction
  would compile, and it would surface only as a wrong grid on a warm Go to. The plan does say
  "firmware wiring … verified by the build and, on the device, by Task 10" (line 747). The spec's
  testing strategy asks no more than this, and a host test would need an `Epub`-free navigator,
  which is out of scope.
- **Evidence.** Plan lines 747 and 1248–1268. `lib/Epub/Epub.h:3` has no host stub.
- **Fix.** No code change is required. In Task 10 step 2, add an explicit check to the second
  Go to on each path: the log shows `Go to: books from cache` and
  `Go to: chapters of book N from cache`, and the grid's labels, section titles and entry
  selection match the first Go to.

## Verdict

The plan maps every spec requirement to a concrete step, with exact code and exact
`old_string`s that match the tree. The host-testable parts were executed and pass as stated.
The two declared packaging deviations are sound, and the one undeclared deviation (Task 9) is
correct and gated. None of the findings reverses a decision or changes scope, and all can be
fixed inline.

VERDICT: CLEAR
