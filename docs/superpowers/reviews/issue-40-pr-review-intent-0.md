# PR #68 — intent review pass 0

Branch `fix/40-bound-recent-book-strings`. Reviewed against issue #40, the spec
`docs/superpowers/specs/2026-09-17-issue-40-design.md` and the plan
`docs/superpowers/plans/2026-09-17-issue-40-plan.md`. Stage 1 of 2: intent,
completeness and scope only — code quality is stage 2 and is not judged here.

## What I verified rather than took on trust

| Claim | Result |
|---|---|
| Host suite green | `cmake --build` + `ctest` in the implementer's configured tree (`CMAKE_HOME_DIRECTORY` = this worktree, ArduinoJson 7.4.2 per `_deps/arduinojson-src/library.properties`): **591/591 passed**, 15 of them `RecentBooksDoc*` |
| Firmware builds, `static_assert`s evaluated | `touch src/RecentBooksStore.cpp && ~/.platformio/penv/bin/pio run` — forced recompile of the TU that carries both asserts, `x4pro SUCCESS`, flash 81.1%, RAM 19.5% |
| Format clean | `./bin/clang-format-fix` (whole tree, main checkout's `.venv/bin` on `PATH`) exits 0 and leaves `git status --short` empty |
| The CMake line is load-bearing | `.github/workflows/ci.yml` is the only workflow that runs `cmake`/`ctest`, so without `test/CMakeLists.txt:116` the new suite would never run in CI — A13's reversal is correct on its own evidence |
| `test/recent_books_doc/CMakeLists.txt` mirrors `bookmark_doc` | `diff` against `test/bookmark_doc/CMakeLists.txt` differs only in the four target/file names |

## Issue #40, criterion by criterion

The issue is prose, not a checklist. Its three asks:

1. **"Truncate `title` and `author` at `addBook` to a documented maximum, at a
   UTF-8-safe boundary."** Met, and exceeded in the direction the spec argued for.
   `src/RecentBooksStore.cpp:38-40` builds the entry and calls
   `RecentBooksDoc::normalise` before inserting; `src/util/RecentBooksDoc.cpp:35-42`
   erases NULs then caps both fields through `utf8SafeSummary`
   (`lib/Utf8/Utf8.cpp:185-201`), which clamps before `utf8SafeTruncateBuffer` —
   the helper that indexes `buf[len - 1]` with no length check
   (`lib/Utf8/Utf8.cpp:148-165`). The maxima are documented at
   `src/util/RecentBooksDoc.h:33-34` with the reasoning above them.
   `test/recent_books_doc/RecentBooksDocTest.cpp:102-124` proves the cut lands on a
   codepoint boundary (126, not 128, for three-byte codepoints) rather than a byte one.

2. **"Once the fields are bounded… `SAVE_BUDGET` can be tightened to a real figure.
   Worth doing in that order."** Met, and the order is visible in the history:
   `235a990e` (cap) → `d28639bd` (load-side re-bound) → `27070d27` (NUL) →
   `e6609942` (budget + store wiring). `src/util/RecentBooksDoc.h:61-72` derives
   11,421 from named constants; `src/RecentBooksStore.h:25` consumes it;
   `src/RecentBooksStore.cpp:125-129` pins it with two `static_assert`s, one against
   the derived value and one against `persist::DEFAULT_SAVE_BUDGET`.

3. **"Pick the cap against what the list can actually display rather than against
   the budget arithmetic."** Met as far as a host suite can. The direction of
   dependence is right — the budget is computed *from* the caps
   (`worstCaseBytes()`), not the caps chosen to hit a budget — and
   `RecentBooksDocTest.cpp:148-176` pins the six real titles and three real authors
   the caps were chosen against, so lowering a cap fails a test naming the string it
   broke. The spec is candid (A1, *"This spec does not measure the row's capacity in
   bytes"*) that the 700 px row's true byte capacity is unmeasured; the spec review
   accepted that and I have no new evidence against it.

**No silent scope reduction.** Every one of A1-A13 is present in the tree:
128/96 (`RecentBooksDoc.h:33-34`), `utf8SafeSummary` not `resize`
(`RecentBooksDoc.cpp:28-33`), all three write paths — `addBook`
(`RecentBooksStore.cpp:39`), `updateBook` (`:61`), `fromJson`
(`RecentBooksDoc.cpp:65`) — the load-side `requestResave()`
(`RecentBooksStore.cpp:20`, reaching `PersistableStore.h:44` and consumed at
`:191-197`), `path` untruncated with a 512-byte allowance (`RecentBooksDoc.h:38-41`,
pinned by `RecentBooksDocTest.cpp:127-136`), the 128-byte cover allowance
(`:45`), `ESCAPE_FACTOR = 2` with NUL erased to make it hold (`:56`,
`RecentBooksDoc.cpp:12-18`), no `FORMAT_VERSION` added, the derived `constexpr`
budget, the `RecentBook.h` / `RecentBooksDoc.{h,cpp}` split, and the committed
`add_subdirectory` line. All 13 spec tests are present as 15 GTest cases — spec
test 11 carried two assertions and was split into `ErasesEmbeddedNuls` and
`AWorstCaseDocumentFitsTheDerivedBudget`, which is the plan's own step 8 shape.

**No scope expansion.** The changed source set is exactly A11's four files plus the
store, the test suite and the one CMake line. `getDataFromBook`
(`RecentBooksStore.cpp:105-123`) is still there and still uncalled —
`grep -rn getDataFromBook src lib test` finds only its declaration and definition —
which is the Non-goal the spec declared rather than an oversight.
`updatePath` (`:81-95`) is untouched, also as declared. No translation YAML, no
user-visible message (#39's scope), no format version.

**No unexplained divergence from the plan.** I read the plan's eleven steps against
the eight code commits; the test bodies, the `normalise` body, the two
`static_assert` strings and the `EXPECT_LT(measured, SAVE_BUDGET / 3)` in the
headroom test are the plan's text verbatim. The `test/CMakeLists.txt` decision
contradicts `.claude/agents/data-dev.md:22-27`, and both the spec (A13) and the PR
body say so and why.

**Tests exercise behaviour, not the implementation.** If `normalise` were reverted
to a no-op, `CapsTitleOnACodepointBoundary`, `CapsAuthorOnACodepointBoundary`,
`ReportsWhetherItChangedAnything`, `FromJsonReBoundsAnOverlongTitleFromTheCard` and
`ErasesEmbeddedNuls` all fail. The three change-detector assertions
(`EXPECT_EQ(expected, 541u)`, `EXPECT_EQ(SAVE_BUDGET, 11421u)`,
`EXPECT_EQ(measured, SAVE_BUDGET)`) are deliberate guards the spec argues for in
A10, each carrying a failure message that says to raise a named allowance rather
than loosen the assertion. That is the opposite of a test restating its code.

---

## Findings

### MINOR 1 — `normalise` is not idempotent when the cap lands on whitespace, so the documented "exactly one resave" can be two

`utf8SafeSummary` trims leading and trailing whitespace *before* it truncates
(`lib/Utf8/Utf8.cpp:193-199`: the two `erase`/`find_if` trims at `:193-198`, then
the `if (passage.size() > maxBytes)` cut at `:199`). So a cut that lands
immediately after a space returns a string that still ends in one, and the *next*
`normalise` of that stored value trims it and reports `changed == true` again.

I compiled `src/util/RecentBooksDoc.cpp` + `lib/Utf8/Utf8.cpp` on the host and ran
`normalise` repeatedly on `std::string(128,'a')` with byte 127 set to `' '`,
followed by 100 more characters:

```
first pass  size=128 endswithspace=1
second pass changed=1 size=127
third pass  changed=0
```

It converges at two — this is not a loop, and the in-memory list is correct
throughout. But spec A5 says *"it costs one SD write for any user with an over-long
title, and zero after that"*, and the PR's device-check item 2 tells the human
tester *"serial shows exactly one resave — a resave on every launcher entry means
the write is failing"*. A tester who sees a second resave on the second entry to
the launcher would be reading a correct system against an instruction that says it
is broken.

Nothing in the shipped behaviour needs to change for this to be fixed inline:
reword A5 and the PR's device-check item to "at most two resaves, then none" and
say why. (Making the cap idempotent would mean re-trimming after the cut inside
`utf8SafeSummary`, which is the shared helper A3 deliberately refuses to touch.)

`src/util/RecentBooksDoc.cpp:28-33`, `lib/Utf8/Utf8.cpp:193-199`,
`docs/superpowers/specs/2026-09-17-issue-40-design.md` A5.

### MINOR 2 — the spec's Risks table contradicts its own A1 on where the Bible-tile markers sit

The Risks row reads *"Markers sit at byte 14 / byte 4 of real NWT titles"*. A1 and
the header comment at `src/util/RecentBooksDoc.h:31-32` both say byte 16 and byte 0,
and those are the correct figures: in
`Traducción del Nuevo Mundo de las Santas Escrituras (revisión de 2019)` the `ó` is
two bytes, so `Nuevo Mundo` begins at byte 16, and `New World` begins at byte 0 of
the English title. Stale text from review pass 0. It changes no conclusion — both
figures are far under 128 — but the spec is the artifact this PR ships as its
reasoning, and a reader who recomputes will trust the wrong row. Fix the table.

`docs/superpowers/specs/2026-09-17-issue-40-design.md`, Risks table row 2, against
A1 and `src/util/RecentBooksDoc.h:31-32`.

---

## Accepted, not findings

Recorded so the next reviewer does not re-litigate them.

- **`PATH_BUDGET_ALLOWANCE` is an allowance, not a bound.** A path over 512 bytes
  can still push the document over 11,421 and earn a silent refusal. Spec A6 names
  this as the design's one genuinely unenforced bound, explains why truncating the
  store's key is worse (`pruneMissing` at `RecentBooksStore.cpp:99-103` would delete
  the entry on the next boot), and documents the one-line lever. The spec review
  answered this as open question 1. It is a live decision, taken with eyes open.
- **The `BookmarkDoc`-shaped refactor on a "good first issue."** A11's argument
  holds up in the tree: a test that only pinned arithmetic could not have caught a
  new string-shaped field on `RecentBook`, and `AWorstCaseDocumentFitsTheDerivedBudget`
  catches it because it goes through the real `toJson`.
- **`utf8SafeSummary` jams words when an OPF title contains a newline.** Confirmed
  by reading `lib/Utf8/Utf8.cpp:186-193` — the collapse keeps the run's first
  character and the `'\n'` removal then deletes it with nothing in its place. The
  spec carries it in A3, the before/after table, the Risks table and device-check
  item 1. Fixing it means changing a helper four other call sites share.
- **`test/CMakeLists.txt` committed against `.claude/agents/data-dev.md:22-27`.**
  Disclosed in both A13 and the PR body, and `ci.yml` is the evidence that
  withholding it would have shipped a suite CI never runs.

## Verdict

Both halves of issue #40 are implemented, in the order the issue asks for, with the
second half resting on constants the first half actually enforces. The load path —
the one the issue does not mention and the one that decides whether the tightened
budget is safe on an existing card — is covered and pinned. The two findings are
documentation accuracy in artifacts this PR carries; neither reverses a decision,
changes scope, or needs a judgment only the human can make.

VERDICT: CLEAR
