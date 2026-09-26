# PR #135 review — intent (pass 0)

PR: #135 `refactor: route every store version check through isKnownFormatVersion`
Against: issue #132, `docs/superpowers/specs/2026-09-26-issue-132-design.md`,
`docs/superpowers/plans/2026-09-26-issue-132-plan.md`.

## Findings

None at BLOCKER, MAJOR or MINOR.

## Evidence checked

### Issue acceptance criteria

1. **Every listed site routes through `persist::isKnownFormatVersion`.** Met.
   - `src/study/PubKeyRegistry.cpp:58-59` (`findBySymbol`), `:85-86` (`lookup`)
   - `src/study/MigrationRunner.cpp:69-70` (`readLedger`), `:97-98` (`appendLedger`)
   - `src/network/MeetingWeekCache.cpp:25-26`
   - `lib/Epub/Epub/HighlightDoc.cpp:122-123`
   - `lib/StudyStore/StudyStore/TagPalette.cpp:80-81`
   - `lib/StudyStore/StudyStore/ChapterCompletion.cpp:113-114`
   - `lib/StudyStore/StudyStore/PassageDoc.cpp:222-223`
   - `src/util/BookmarkDoc.cpp:32-33`

   A grep for `doc["v"] |` over `src` and `lib` finds no remaining hand-rolled
   comparison. Every hit feeds the helper. The one extra site,
   `PubKeyRegistry::record` (`src/study/PubKeyRegistry.cpp:29-30`), is the write-path
   twin of the two listed read paths. Leaving it out would have kept a fourth
   spelling of the rule, so including it is completion, not scope expansion. The PR
   body and spec (Problem section) both call it out.

2. **Absent-`"v"` behaviour preserved exactly.** Met.
   - Accepting stores before the change: `| 0` then `> 1`, so an absent `"v"` read as
     0 passed. After the change they read `| FORMAT_VERSION` or
     `| LEDGER_FORMAT_VERSION`, so it reads as 1, which is known. It still passes.
   - Refusing stores (`TagPalette`, `ChapterCompletion`, `PassageDoc`) keep `| 0`, and
     the helper refuses 0.
   - `BookmarkDoc` keeps `| FORMAT_VERSION`.

   All the defaults are `int` constants equal to 1: `HighlightDoc.h:22`,
   `PubKeyRegistry.cpp:14`, `MigrationRunner.cpp:32`, `MeetingWeekCache.cpp:14`,
   `BookmarkDoc.h:23`. So `operator|` converts `"v"` as `int`, and a written `-1`
   keeps its value rather than falling back to the default. An unsigned default
   would have silently accepted `-1` at the three untested `src/` sites. I checked
   this specifically, and it is not the case.

   The write-path subtlety (a missing file reaches the check with no `"v"`) is
   handled correctly by the accepting default, and the PR flags it in "Review focus".

3. **Refusal behaviour unchanged.** Met. Every `LOG_ERR` string and every
   `return false` / `std::nullopt` in the diff is byte-identical. Every check still
   sits before `doc["v"] =` and before the atomic rewrite (`PubKeyRegistry.cpp:29-34`,
   `MigrationRunner.cpp:97-102`), so a refused file is not overwritten. The "newer"
   wording now also covers `v <= 0`. That is a deliberate, documented choice
   (spec A5), because the issue pins the `LOG_ERR`.

4. **No format-version constant changes and no on-card change.** Met. No constant
   is touched in the diff, and no `toJson` or write body changes apart from the
   guard.

5. **Host tests cover absent, 0, -1, current and current + 1 per host-testable store.**
   Met.
   - `HighlightDoc`: absent, 0 and -1 are new (`test/highlight_doc/HighlightDocTest.cpp`
     `HighlightDocVersion.*`). Current and v2 already existed at `:60-70`.
   - `TagPalette`: absent, 0 and -1 are new. v+1 already existed at
     `TagPaletteTest.cpp:88-94`, and the round trip covers current.
   - `ChapterCompletion`: 0 and -1 are new. Absent and v2 already existed at
     `ChapterCompletionTest.cpp:266-274`.
   - `PassageDoc`: absent, 0, -1 and current are new. v+1 already existed.
   - `BookmarkDoc`: -1 is new. Absent, 0 and v+1 already existed at
     `BookmarkDocTest.cpp:81-118`.

   The tests go through each store's real `fromJson` with JSON input, so they
   exercise behaviour rather than restate the implementation. The PR body says
   plainly that only the `HighlightDoc` 0 and -1 cases were red first, and that the
   rest pin existing behaviour. The three stores with no host suite (`grep` of
   `test/` for `PubKeyRegistry|MigrationRunner|MeetingWeekCache` finds nothing) are
   named in the PR, and no harness was added, as the issue asks.

### Spec requirements

A1–A9 are all implemented:
- **A8:** the helper comment is rewritten (`lib/Serialization/FormatVersion.h:10-13`)
  and the assertion message is reworded (`test/format_version/FormatVersionTest.cpp:16`).
- **A4 / MAJOR 1 of the spec review:** the five include-path additions are present:
  `tag_palette`, `chapter_completion`, `passage_doc`, `highlight_doc` and
  `migration_planner`.
- **A9:** no new per-site comments.
- **A7:** `test/CMakeLists.txt` is untouched.

The spec's non-goals are respected: the binary-header checks, `writeReport`,
`MeetingWeekCache::save` and `PassageDocTest.cpp:410-412` are all untouched.

### Plan conformance

The commits map one-to-one onto plan Tasks 1–10, with the plan's exact commit
messages and in the plan's order. The spec's test table lists three new
`PassageDoc` cases, but the plan (`:476`) adds a fourth, `TheCurrentVersionIsAccepted`,
and explains why. The PR's count of 13 matches the plan's own count (`:828-829`).
No unexplained divergence.

### Not verified here

CI `Build x4pro`, `cppcheck` and `unit-tests` were still pending when this review was
written. `clang-format` and `lint-title` had passed. The PR's 965/965 host result
and the `pio run` success are the author's report, not re-run for this intent review.

VERDICT: CLEAR
