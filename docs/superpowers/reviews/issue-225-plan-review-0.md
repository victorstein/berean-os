Tier: standard

# Issue #225 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-30-issue-225-plan.md` ("P").
Spec: `docs/superpowers/specs/2026-09-30-issue-225-design.md` ("S").
Checked against `926f1dbd` (source identical to `e3e5c94e` for every file the plan touches).

## How this was verified

I did more than read the plan. I exported `src/`, `lib/` and `test/` at HEAD to
`/private/tmp/claude-501/r225plan/`, took every code block out of the plan by line number, and
applied it literally:

- Tasks 1 to 3: the new files, the `BibleReferenceTest` rewrite, the two `PlacesDocTest` additions,
  all three CMake edits, and the `PlacesDoc.cpp` include, helper and adapter replacement.
- Task 5: the host-side part, meaning the three `TypedReferenceFormat` deletions, the header
  declaration, the `.cpp` definition and `<cstdio>`.
- Task 4: deleted `src/activities/reader/BibleReference.h`.

Every "Old" block in P (P:145-153, P:403-430, P:737-738, P:753-762, P:829-840, P:692, P:697) occurs
exactly once in the current source. None of them has drifted.

Results in the copy:

- The full host suite configures, builds and passes: `100% tests passed out of 1499`. That covers
  all 11 `BibleReference.*` cases, the 2 new `PlacesDocDisplay` cases, the 4
  `ReferenceConsistency.*` cases, `PlacesStoreTest`/`PlacesStoreLazyLoadTest` with the added
  `BibleReference.cpp`, and `TypedReferenceTest` once its format tests are gone.
- `TypedReference.cpp` has no remaining `printf`/`scanf`/`FILE` use once the function is gone, so
  the conditional `<cstdio>` removal in P:785-788 applies.

I did not run `pio` (as the brief asked). The firmware edits in Tasks 4 to 6 were checked by hand:

- **Brace-inits.** `Verses{shown.chapter, …}` and `Verses{row.entry.chapter, row.entry.verse}`
  widen `uint8_t` to `uint16_t` (`TypedReference.h:15-20`, `IndexFormat.h:53-59`).
  `Verses{anchor->chapter, anchor->verse}` is `uint16_t` to `uint16_t` (`VerseAnchors.h:19-23`).
  `knownChapter` casts `std::max(int, int)` explicitly. None of them is a narrowing error.
- **`<algorithm>`.** It is already included in `EpubReaderActivity.cpp:18`.
- **`forBook` never returns null.** It goes through `at()`, which returns `""` when out of range
  (`BibleBookNameTable.cpp:68-70`), so building a `std::string_view` from it is safe. The old
  `nullptr` guard in `formatTypedReference` (`TypedReference.cpp:265`) is not needed.
- **Chapter 0.** A typed reference can never have chapter 0. `NumberCursor::number` rejects
  `value < 1` (`TypedReference.cpp:71`), so the chapter-0 shape change cannot reach "Go to".

## Spec → plan mapping

| Spec requirement | Plan step |
|---|---|
| §4.1 formatter, both overloads, `MAX_NUMBERS_BYTES`, clamp before `utf8SafeTruncateBuffer`, no lone lead byte at index 0 | Task 1 (P:180-299) |
| §4.2 / §4.3 name sources and chapter derivation documented in the header | P:190-198 |
| §5 reader title and status bar | Task 4 |
| §5 stored place, `utf8SafeSummary` after the uncapped string; chip keeps its fallback | Task 2 (P:433-447) |
| §5 "Go to" and hit row; `formatTypedReference` deleted | Task 5 |
| §5 passage, `title` empty when `tocIndex < 0`, `VerseAnchors::format` kept | Task 6 |
| §7 T1: every listed case, including `outBytes` 0 and 1, index-0 lead byte, 65535s, overload agreement, the moved `TypedReference` expectations | P:78-139 |
| §7 T2: `PlacesDocTest` and `PLACES_STORE_SOURCES` gain the source | P:341-361 |
| §7 T3: every listed assertion, both named books and the empty name | Task 3 |
| §7 build, whole-tree format, **full** `ctest` | Task 7 |
| §6 PR list, §7 device check | Task 8 |

Nothing in the spec is left without a step.

Types and names stay consistent from step to step: `BibleReference::format`, `Verses`,
`MAX_NUMBERS_BYTES`, `placeVerses` and `knownChapter` are spelled and typed the same everywhere
they appear.

The `FILES:` lines (P:14-20) are at column 0 and outside any fence. They use the comma-separated
form other plans in `docs/superpowers/plans/` already use. They cover every file a step touches:

- `PlacesDocTest.cpp`, which S §10 does not list but Task 2 edits.
- The deleted `src/activities/reader/BibleReference.h`.

**TDD.** Tasks 1 and 2 start red. Task 3 is a characterisation test with an explicit red run
(P:650-657). Tasks 4 to 6 edit activities that the host cannot link (S §7, "It cannot link
`BibleSearchActivity.cpp`"). They are build-verified, and T3 replicates their one-line mappings.
This follows from the spec's own constraint and is not a gap. Every commit boundary leaves the
tree building:

- Task 1 adds `src/util/BibleReference.h` next to the old reader header. The quoted
  `#include "BibleReference.h"` in `EpubReaderActivity.cpp:26` still resolves to the reader
  directory first.
- Task 4 deletes the old header and repoints its only includer in the same commit.

## Findings

### MINOR 1: two grep expectations will not match what the implementer sees

**Claim.** Step 4.2 (P:707-708) says the only match is `src/util/PlacesDoc.cpp`'s
`#include "BibleReference.h"`. Task 7 (P:893-894) says the `%u:%u` grep prints nothing except
log lines.

**Problem.** Both expectations are wrong.

- `src/util/BibleReference.cpp` begins with `#include "BibleReference.h"` (P:225), so the
  Step 4.2 grep prints two lines.
- The `formatNumbers` shapes `"%u:%u"` and `"%u:%u-%u"` (P:241, P:244) sit under `src/util`, so
  the Task 7 grep matches the new formatter itself.

The plan tells the implementer to stop on any mismatch (P:25-27). An implementer who follows it
literally will stop on a correct tree.

**Evidence.** I ran the Task 7 grep against the patched copy. It matched
`src/util/BibleReference.cpp` twice, at its `"%u:%u"` and `"%u:%u-%u"` lines.

**Fix.** At P:707, say the matches are `src/util/PlacesDoc.cpp` and `src/util/BibleReference.cpp`,
both resolving to `src/util/`. At P:893, also allow the two `formatNumbers` lines in
`src/util/BibleReference.cpp`, or narrow the grep to `src/activities`.

### MINOR 2: an unused include in the new test

**Claim.** `ReferenceConsistencyTest.cpp` includes `<cstring>` (P:491).

**Problem.** Nothing in the file uses it. `<algorithm>` is used, for `std::max` at P:521.

**Evidence.** P:478-601 calls no `mem*` or `str*` function.

**Fix.** Drop `#include <cstring>` from P:491.

## Verdict

The plan is literal, and it executes as written. Every host-side block compiles and the full host
suite passes. It maps onto every spec requirement. Both MINORs can be fixed inline.

VERDICT: CLEAR
