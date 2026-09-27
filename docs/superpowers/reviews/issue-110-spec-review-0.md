Tier: heavy

# Spec review 0 — issue #110 (bookmark and auto-turn extractions)

Reviewed: `docs/superpowers/specs/2026-09-27-issue-110-design.md` against `gh issue view 110`,
`docs/superpowers/research/2026-09-27-issue-110-research.md`, and the code at HEAD `324adfca`.
`git diff 9a307341 -- src test` is empty, so every line number the spec cites against `9a307341`
still holds.

## What I verified and found correct

- Every bookmark and auto-turn line the spec cites matches the source:
  `grep -n` for each member in `EpubReaderActivity.cpp` gives exactly `:244, :317, :495-514,
  :521-522, :531, :557-558, :717, :890, :915-923, :1033, :1052, :1258, :1267, :1271, :1277,
  :1318-1319, :1323, :1659-1660, :1683` and the bodies at `:1765-1905`. Header fields are at
  `EpubReaderActivity.h:31-32, 40-42, 47-48, 55-56, 58`, methods at `:130-134`.
- A4: every path of `addBookmark` past the `:1802` guard ends in `requestUpdate()` (`:1810`,
  `:1875`, `:1892`).
- A5: the order the spec describes (arm toast `:1805-1806` → disabled exit `:1808-1812` → log →
  locked read `:1817-1821` → `getCurrentPosition` `:1823` → range `:1824` → match/mutate/save/
  rollback) is what the code does. The rollback's `updateBookmarkFlag()` (`:1889`) reads the live
  `section`, which the moved body can still do through `Section&`. `getTextFromSectionFile()` is
  non-const (`lib/Epub/Epub/Section.h:121`), so `Section&` rather than `const Section&` is required.
  `estimatedTotalPages()` and `getVisibleTextOffsetForPage()` are const (`Section.h:106, 162`), so
  `const Section*` works for `load` and `refreshPageFlag`.
- A7: `lastPageTurnTime` is the manual-turn debounce at `:615` and is written by `pageTurn` at
  `:947, :955, :961, :967, :975`. Keeping it on the activity is correct.
- A8/A9 models exist as described: `src/util/BookmarkSaveAction.h` is pure and header-only, and
  `src/activities/reader/ReturnStack.h` is header-only with trailing-underscore private fields
  (`slots_`, `top_`, `count_`). `BookmarkEntry.h` includes only `<cstdint>` and `<string>`.
- A10: `test/CMakeLists.txt:78` is `add_subdirectory(return_stack)` and `:127-128` are the two
  bookmark suites. `ui-dev.md:22-27` makes `test/CMakeLists.txt` a report-only shared file.
- `static constexpr int RATES[] = {1, 1, 3, 6, 12};` as a class member compiles under
  `-std=gnu++2a`, and `std::size(RATES) == 5` holds (checked with a scratch compile).
- `pagesPerMinute()` keeps `60 * 1000 / pageTurnDuration`'s `unsigned long` type (`:1660`), and the
  per-option values 1, 1, 3, 6, 12 check out: 60000/60000, 60000/20000, 60000/10000, 60000/5000.
- `-Wformat` is in `platformio.ini:73`. No existing symbol clashes with `ProgressRange`,
  `AutoPageTurn`, `ReaderBookmarks` or `BookmarkMatch` (`grep -rn` over `src lib test` only hits
  the anonymous-namespace originals and the substring `EpubReaderBookmarksActivity`).

## Findings

### 1. MAJOR — `ReaderBookmarks` as specified does not compile: two fields share names with accessors

**Claim.** Architecture §2 says `ReaderBookmarks` owns fields named "`toastVisible` (was
`showBookmarkMessage`)" and "`currentPageBookmarked`". Its API table adds
`bool toastVisible() const` and `bool currentPageBookmarked() const`.

**Problem.** In C++ a data member and a member function cannot have the same name. The spec is also
inconsistent with itself here: `AutoPageTurn` (§3) avoids the same clash with `active_` next to
`active()`, and cites `ReturnStack.h` for that convention, but `ReaderBookmarks` does not.

**Evidence.** A scratch compile of
`class B { bool toastVisible = false; public: bool toastVisible() const { return toastVisible; } };`
under `c++ -std=gnu++2a` fails:
`error: redefinition of 'toastVisible' as different kind of symbol`.

**Fix.** Give `ReaderBookmarks` the same trailing-underscore private fields as `AutoPageTurn`, for
example `cachedBookmarks_`, `saveDisabled_`, `toast_`, `toastVisible_`, `toastShownAtMs_` and
`currentPageBookmarked_`. Then state the convention once for both new types.

### 2. MAJOR — `Closes #110` with an *optional* follow-up can silently drop two-thirds of the issue

**Claim.** A1: the PR does the reader items only, uses `Closes #110`, and "the orchestrator decides
whether to file a follow-up issue for the remainder". The Goal section puts both extractions in one
PR as "each its own commit on this branch".

**Problem.** Issue #110 lists three pieces of work under "What it needs": the reader, the web-server
route groups (files, settings, fonts), and the `setup()` init stages. It asks for them "one piece at
a time, each a behaviour-preserving PR". This PR can therefore close the issue with most of it
undone. The only record of the web-server and `setup()` work would be a PR body, and whether that
survives depends on a follow-up the spec leaves optional. Two more gaps:
- PRs are squash-merged (CLAUDE.md, "CI and releases"), so "each its own commit" becomes one commit
  on `main`. That is not the one-PR-per-extraction delivery the issue asked for. The spec neither
  acknowledges this nor justifies it.
- The deliberate decision not to build a passage controller (A2) also lives only in the PR body.

**Evidence.**
- `gh issue view 110`, "What it needs": "Take one piece out at a time, each a behaviour-preserving
  PR", followed by the three pieces.
- Spec A1: "the orchestrator decides whether to file a follow-up issue".
- Research §8.1 names the web server and `setup()` as follow-ups, but nothing makes filing one a
  precondition.

**Fix.** This needs no change to the design, only to A1 and the Goal section:
1. Make filing a follow-up issue a **precondition of merge**, not an option. It should list the
   web-server route groups (files, settings, fonts; `src/network`, the `net` surface), the `setup()`
   named stages (`src/main.cpp`, orchestrator-owned), and a one-line record that the passage
   controller was considered and rejected, with a pointer to A2.
2. Have the PR body link that issue.
3. In the Goal section, say plainly that the two extractions ship as one PR of two commits, and give
   the reason (one pipeline task per branch). Also say that squash merge collapses them, and that
   the per-commit split exists for review through `git diff --color-moved`.

This keeps the owner's full list tracked without reversing any scope decision the spec makes.

### 3. MINOR — the `GfxRenderer&` constructor dependency is unnecessary, and the reason it can't be host-tested is misattributed

**Claim.** The API table has `explicit ReaderBookmarks(GfxRenderer&)`, "renderer for
`ReaderUtils::showMessage`, `:1796`". A3 models the class on `EndOfBookOptions` for that reason. The
Testing section says `ReaderBookmarks` can't be host-built because it needs "`Epub`, `Section`,
`GfxRenderer` and `BookmarkFile`, and no suite builds those (R§6)".

**Problem.**
- `ReaderUtils::showMessage` ignores its renderer. It just posts to `PostedMessage`, so the
  reference member and the constructor argument do nothing.
- `BookmarkFile` is not an obstacle either: the repo already has an in-memory Storage fake intended
  for building it. R§6 only covers `Activity` and `CrossPointWebServer`.
- The real obstacles are `Epub` and `Section`, since no suite compiles them.

**Evidence.**
- `src/activities/reader/ReaderUtils.h:233`:
  `inline void showMessage(const GfxRenderer&, const char* message) { PostedMessage::post(message); }`
- `src/util/BookmarkSaveAction.h:9-11`: "BookmarkFile.cpp itself has no host suite yet;
  test/stubs now carries an Arduino.h and an in-memory Storage fake it can be built against (see
  test/storage_io/)".
- `grep -rn 'Epub.cpp\|Epub/Section.cpp\|GfxRenderer.cpp\|BookmarkFile.cpp' test/*/CMakeLists.txt`
  matches only that comment.

**Fix.** Either drop the constructor argument and default-construct `ReaderBookmarks`, or keep it
for call-site symmetry and say it is unused. In the Testing section, say the reason is "`Epub` and
`Section` have no host build". Cut the `EndOfBookOptions` renderer rationale from A3; the owned-by-
value decision stands without it.

### 4. MINOR — the `millis()` wrap test in the auto-turn suite must be written width-agnostic

**Claim.** The Testing section says `due` is tested "across `unsigned long` wrap of `millis()`".

**Problem.** On the device `unsigned long` is 32 bits, so `millis()` wraps at 2^32. The host suites
build LP64, where `unsigned long` is 64 bits. A test written with `0xFFFFFFFFUL` as the pre-wrap
value would not wrap on the host at all: it would compute a huge positive difference and pass for
the wrong reason.

**Evidence.** A scratch program returning `sizeof(unsigned long)` on this host exits with 8.

**Fix.** Build the wrap case from `std::numeric_limits<unsigned long>::max()`, for example
`last = max - 99`, `now = duration - 100`, then expect `due`. It then exercises the modular
subtraction at whatever width the host uses.

### 5. MINOR — a comment naming `loadCachedBookmarks()` will go stale

**Claim.** The call sites at `:244` and `:717` become `bookmarks.load(...)`, and
`loadCachedBookmarks` goes away.

**Problem.** The comment in `openHighlights` refers to the old name by function. The spec's call-site
list doesn't mention it, and a verbatim move leaves a reference to a function that no longer exists.

**Evidence.** `EpubReaderActivity.cpp:381-383`: "Deliberately NOT progressChangeResultHandler
(used by the BOOKMARKS case below): that lambda calls loadCachedBookmarks() and reopens the reader
menu on cancel".

**Fix.** Add `:382` to the call-site list, rewording it to `bookmarks.load()`.

## Verdict rationale

The design is sound and its evidence holds up. A2 is supported by the code: the passage entry
points are thin `startActivityForResult` wrappers with almost no state. So are A5, where the
two-call toggle keeps the order of effects, and A7, where `lastPageTurnTime` stays on the activity.
The threading claim in A6 is stated honestly as "unchanged, not improved". Finding 1 is a naming
defect with a mechanical fix. Finding 2 makes a follow-up that the spec already anticipates
mandatory. Neither reverses a decision or needs the owner's judgment, so both can be fixed inline.

VERDICT: CLEAR
