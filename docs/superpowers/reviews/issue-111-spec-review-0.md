# Issue #111 spec review — pass 0

Reviewed: `docs/superpowers/specs/2026-09-26-issue-111-design.md` (commit `9db57c9a`)
against issue #111 (`gh issue view 111 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-26-issue-111-research.md`, `.claude/agents/data-dev.md`,
and the tree at HEAD (source identical to baseline `2f303f6f`; the two commits since are docs only).

## What was verified and holds

- **Site inventory is complete.** `grep -rn '/\.crosspoint\|/\.berean' src lib --include='*.cpp' --include='*.h'`
  returns 38 quoted-literal hits in 30 files. One of them is a comment (`src/util/RecentBooksDoc.h:41`);
  the other 37 break down as 13 bare `/.crosspoint` uses, 8 `/.crosspoint/...` literals, 5 `/.berean`
  roots and 11 `/.berean/...` constants. That matches research §5 and the spec's call-site map row by
  row. A wider search for either root outside a `"/`-prefixed literal (`src lib data scripts`, comments
  excluded) finds nothing. `freeink-sdk` has no occurrence, and `sdpaths`/`SdPaths` collides with no
  existing name.
- **Byte-identity of every swap.** `Epub(std::string, const std::string&)` (`lib/Epub/Epub.h:45-48`)
  builds a temporary from the pointer exactly as it does from the literal. `getFilePath()` still
  returns a static-storage `const char*` (`lib/Serialization/PersistableStore.h:107`).
  `std::string(UNITS_DIR) + "/" + …` already exists (`src/study/UnitIndexCache.cpp:48`,
  `PassageFile.cpp:54`, `ChapterCompletionFile.cpp:18`), so A4's `std::string(HIGHLIGHTS_DIR) + "/"`
  reproduces `"/.crosspoint/highlights/"`. `Storage.mkdir(highlightsDir().c_str())`
  (`HighlightFile.cpp:84`) and `BookmarkFile.cpp:106` keep their trailing slash.
  `String(sdpaths::CROSSPOINT_DIR) + "/" + itemName` is type-valid Arduino `String` and is not on a
  hot path (`ClearCacheActivity.cpp:114-120`, a user-initiated clear).
- **A5 aliases compile.** A `constexpr const char*` initialised with the address of an
  `inline constexpr` array is a constant expression. The class-scope `BibleSearchStore::SEARCH_DIR`
  initialiser is namespace-qualified, so it does not self-reference. Every caller of the aliases
  (`BibleSearchIndexer.cpp:93-279`, `CrossPointWebServer.cpp:1847,1853`,
  `MigrationRunner.cpp:64,91,117,172`) is unchanged.
- **`isUnder` / `static_assert`.** `std::string_view::starts_with` is C++20; the firmware builds
  `-std=gnu++2a` (`platformio.ini:40`) and the host tests `CMAKE_CXX_STANDARD 20`
  (`test/CMakeLists.txt:4`). The predicate rejects the root itself, a prefix-sharing sibling and an
  empty tail, as step 1 of the testing strategy requires.
- **Host-test blast radius is nil.** No test includes any header the spec edits, directly or
  transitively (grep of `test/` for the nine changed headers returns nothing, and the six src headers
  tests do include pull in none of them). Step 5's claim that the existing suites are unaffected holds.
- **`src/main.cpp` A8 alias.** `SLEEP_FRAME_FILE` is used only as a `const char*` argument
  (`main.cpp:231,238,243,246,272,274,522`) and never with `sizeof`, so the array→pointer change is safe.

## Findings

### MAJOR 1 — The pinning test cannot be run as the testing strategy describes, and CI won't run it on this PR

**Claim.** Testing strategy, steps 1–2: "Red: `test/sd_paths/SdPathsTest.cpp` … fails to build until
the header exists. Green: add `SdPaths.h`; the test passes. The literals in the test are the
byte-identical contract … the test, not the header, is the source of truth." A8 then keeps
`add_subdirectory(sd_paths)` out of the branch and puts it in the PR description.

**Problem.** Without that line, `test/sd_paths/` is never configured. The red step does not fail and
the green step does not pass: the suite simply isn't compiled. CI's `unit-tests` job
(`.github/workflows/ci.yml:168`) builds `test/CMakeLists.txt` as committed, so this PR's checks never
execute the one artifact the spec names as the on-card contract. The spec doesn't say when the
orchestrator's line lands relative to merge. The main duplicate-spelling guard becomes "trust the
orchestrator applied it".

**Evidence.** `test/CMakeLists.txt:101` wires each suite explicitly (`add_subdirectory(save_budget)`);
there is no glob. The precedent went the other way: `5c385845` (#94) added its `add_subdirectory` lines
in-branch and recorded that the agent definition reserves the file for the orchestrator, "the plan's
ground rules ask the task to add the line, and this worktree has no parallel agent to collide with."

**Fix (inline, no decision reversed).** Keep A8, and add to the testing strategy:
(a) red/green runs with `add_subdirectory(sd_paths)` applied locally and left uncommitted
(`git diff --stat test/CMakeLists.txt` must be empty at commit);
(b) the PR description says `SdPathsTest` is not exercised by CI until the orchestrator applies the line,
and the line must land before merge. If the pipeline doesn't guarantee (b), follow the #94 precedent
and commit the line.

### MINOR 1 — The sweep-completeness grep will always print a line the spec says must stay

**Claim.** Testing step 3: `grep -rn '"/\.crosspoint\|"/\.berean' src lib …` "must print only
`lib/Serialization/SdPaths.h` lines (plus `src/main.cpp:227` …)".

**Problem.** The pattern matches the comment at `src/util/RecentBooksDoc.h:41`
(`// coverBmpPath is bounded by construction at 57 bytes: "/.crosspoint" (12) +`). The non-goals keep
that comment, both under "Comments are not rewritten" and under the explicit `RecentBooksDoc.h:41-43`
bullet. The gate as written can't pass. An implementer who treats it literally will "fix" the comment,
which contradicts a non-goal.

**Evidence.** Running the spec's grep and filtering to comment lines at HEAD prints exactly
`src/util/RecentBooksDoc.h:41:// coverBmpPath is bounded by construction at 57 bytes: "/.crosspoint" (12) +`.

**Fix.** Either list `src/util/RecentBooksDoc.h:41` as an expected survivor, or filter comments:
`grep -rn '"/\.crosspoint\|"/\.berean' src lib --include='*.cpp' --include='*.h' | grep -v '^[^:]*:[0-9]*: *//'`.

### MINOR 2 — A4's "as today" understates the heap work for the two slash-terminated dirs

**Claim.** A4: `std::string(sdpaths::HIGHLIGHTS_DIR) + "/"` is "still one `std::string` construction
per call, as today."

**Problem.** It is one construction but not the same heap work. `"/.crosspoint/highlights"` (23 B) and
`"/.crosspoint/bookmarks"` (22 B) exceed libstdc++'s 15-byte SSO. So the construction allocates, and
appending `"/"` to a full-capacity rvalue reallocates. Today the 24-byte literal takes one allocation;
the replacement takes two allocations and one free per `highlightsDir()`/`getBookmarksDir()` call. The
cost is negligible, and the larger capacity can absorb the caller's later `+ stem + ".json"` append
(`HighlightFile.cpp:17`, `BookmarkUtil.cpp:13`). Even so, CLAUDE.md's "no unfounded claims" rule applies
to equivalence claims too.

**Evidence.** `src/util/HighlightFile.cpp:14`, `src/util/BookmarkUtil.cpp:10`. Both run on every
highlight or bookmark path build.

**Fix.** Reword A4 to "byte-identical result; heap work differs by at most one reallocation on a
non-hot path". Alternatively, build with `std::string dir; dir.reserve(sizeof(sdpaths::HIGHLIGHTS_DIR)); dir = sdpaths::HIGHLIGHTS_DIR; dir += '/';`.
Rewording is enough.

## Assumptions A1–A8

A1 (home), A2 (array form), A3 (written-out paths plus `static_assert`, no join helper), A5 (keep owner
aliases), A6 (delete TU-local duplicates) and A7 (catalog path stays runtime-composed) are sound and
supported by the cited lines. A3 correctly declines to invent a `consteval` join, which `data-dev.md`
("escalate before inventing") would require escalating.

A2's justification "carries its length for `std::string_view` in `static_assert`" is not the real
mechanism: the array decays and `char_traits::length` is used, which a `const char*` would also
support. The choice is still fine, since `main.cpp:227` and `SleepActivity.cpp:34-36` do use the array
form. The issue's wording "`static constexpr` roots" is correctly improved to `inline constexpr`,
which gives one definition instead of one per TU. A8 is dealt with under MAJOR 1.

## Trailer

The MAJOR is fixable inline: it adds an instruction and reverses no decision. No finding changes
scope or needs the human's judgment.

VERDICT: CLEAR
