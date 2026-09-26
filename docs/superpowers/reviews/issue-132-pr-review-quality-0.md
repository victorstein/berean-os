# PR #135 review — code quality, pass 0

Scope: `gh pr diff 135 --repo victorstein/berean-os`, the production and test changes under `lib/`, `src/`
and `test/`. The planning and review documents under `docs/superpowers/` were not reviewed for quality.

## Findings

None at BLOCKER, MAJOR or MINOR.

## What was checked

**It uses the existing pattern, not a second one.** Every changed site now has the same two lines that the
inherited stores already used:

    const int version = doc["v"] | <DEFAULT>;
    if (!persist::isKnownFormatVersion(version, <NEWEST>)) ...

The reference sites are `src/CrossPointSettings.cpp:110-111`, `src/CrossPointState.cpp:64-65`,
`src/WifiCredentialStore.cpp:32-33` and `src/util/RecentBooksDoc.cpp:57-58`. The PR adds the same shape at
`lib/Epub/Epub/HighlightDoc.cpp:122-123`, `lib/StudyStore/StudyStore/TagPalette.cpp:80-81`,
`lib/StudyStore/StudyStore/ChapterCompletion.cpp:113-114`, `lib/StudyStore/StudyStore/PassageDoc.cpp:222-223`,
`src/util/BookmarkDoc.cpp:32-33`, `src/network/MeetingWeekCache.cpp:25-26`,
`src/study/MigrationRunner.cpp:69-70,97-98` and `src/study/PubKeyRegistry.cpp:29-30,58-59,85-86`.

A grep for `["v"]` across `lib/` and `src/` finds no hand-rolled comparison left. The only reads of `"v"`
are writes (`doc["v"] = …`) or reads that feed the helper. `src/RecentBooksStore.cpp:18` passes the read
straight into a call without a named local. That line is from before this PR, and it is also routed
through the helper.

**Error handling keeps its shape.** Each site's refusal is unchanged: `return false`, `return std::nullopt`,
or `LOG_ERR` followed by the same return. The `LOG_ERR` strings are byte-identical
(`MeetingWeekCache.cpp:27`, `MigrationRunner.cpp:71,99`, `PubKeyRegistry.cpp:31`). The PR adds no new
error path.

**Naming and structure match the neighbouring code.** The local is named `version` everywhere, as in the
inherited stores. Each include is `#include <FormatVersion.h>`, placed in alphabetical order within the
angle-bracket group. In `TagPalette.cpp` and `ChapterCompletion.cpp` it sits in its own group ahead of the
standard headers, and in the other files it joins an existing third-party group. clang-format accepts all
of these.

**No dead code, commented-out code or narrating comments.** The only comment changed is the doc comment
at `lib/Serialization/FormatVersion.h:10-13`. It now explains a real "why": the default a caller reads
with decides what an absent `"v"` means. The old comment claimed every caller used `| FORMAT_VERSION`,
which was false for three stores. The comment above `BookmarkDoc.cpp:29-31` and the one above
`MigrationRunner.cpp:67-68` were already there and are still accurate.

**The tests are designed, not just present.**
- Each new case changes exactly one input, `"v"`, and keeps the payload minimal and valid. A refusal can
  therefore only come from the version check. This holds at `test/tag_palette/TagPaletteTest.cpp:96-117`,
  `test/passage_doc/PassageDocTest.cpp:53-82`, `test/highlight_doc/HighlightDocTest.cpp:73-93` and
  `test/chapter_completion/ChapterCompletionTest.cpp:276-284`.
- The positive controls are real:
  - `HighlightDocVersion.AnAbsentVersionIsReadAsTheCurrentOne` asserts that the parsed entry count is 1,
    not just that the parse succeeded.
  - `PassageDocVersion.TheCurrentVersionIsAccepted` stops the three refusal cases from passing vacuously.
- Existing cases are reused, not duplicated: the future-version cases, and
  `ChapterCompletionJson.RefusesAMissingVersion` at `ChapterCompletionTest.cpp:271`.
- Suite names follow each file's own convention. The `…Version` groups follow the existing
  `BookmarkDocVersion` suite. The new ChapterCompletion cases join that file's existing
  `ChapterCompletionJson` group, next to its other version cases.
- The assertion messages state the invariant being tested, not the mechanics. For example,
  `"the palette has always required \"v\""`.

**The CMake changes are minimal and necessary.** `lib/Serialization` is added only to suites that compile a
source file which now includes `<FormatVersion.h>`:
- `tag_palette`, `chapter_completion`, `passage_doc` and `highlight_doc` compile the file they test.
- `migration_planner` compiles `TagPalette.cpp` (`test/migration_planner/CMakeLists.txt:7`).
- `passage_doc` also compiles `TagPalette.cpp`.
- `storage_io` already had the include path.
- `bookmark_doc` needed no change.

The two single-line `target_include_directories` calls were reshaped into the multi-line form the other
suites already use (`test/highlight_doc/CMakeLists.txt:7-11`).

**Nothing is duplicated.** The PR uses the helper and does not re-implement it. It adds no new helper,
wrapper or constant.

## Note for the reader, not a finding

`PubKeyRegistry::record` and `appendLedger` must keep the `| FORMAT_VERSION` / `| LEDGER_FORMAT_VERSION`
default. The PR description explains why: a missing file arrives as an empty document. That is a behaviour
concern for the intent and correctness reviews. On quality, the code is correct, and it matches the
default that the read-side twins use at `PubKeyRegistry.cpp:58,85` and `MigrationRunner.cpp:69`.

VERDICT: CLEAR
