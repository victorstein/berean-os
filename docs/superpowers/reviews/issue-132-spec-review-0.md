# Issue #132 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-26-issue-132-design.md` against issue #132
(`gh issue view 132 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-26-issue-132-research.md`, at `de9a5d43`.

## What was verified and holds

- **All eleven sites** read exactly as the spec's table says:
  `PubKeyRegistry.cpp:28-29, 57, 83`, `MigrationRunner.cpp:68, 95`,
  `MeetingWeekCache.cpp:24`, `HighlightDoc.cpp:121-122`, `TagPalette.cpp:78-79`,
  `ChapterCompletion.cpp:111-112`, `PassageDoc.cpp:221-222`, `BookmarkDoc.cpp:31-32`.
  The issue's table does miss `PubKeyRegistry::record` (`:28-29`). A grep for
  `["v"] |` over `src` and `lib` finds no JSON store check beyond these eleven and
  the four #123 sites. `FontDownloadActivity.cpp:117` reads `doc["version"]` from a
  downloaded manifest, not a store, and is correctly out of scope.
- **Constants.** `HighlightDoc.h:22`, `BookmarkDoc.h:23`, `TagPalette.h:35`,
  `ChapterCompletion.h:30`, `PubKeyRegistry.cpp:13`, `MigrationRunner.cpp:31` and
  `MeetingWeekCache.cpp:13` are all `1`. `PassageDoc.h:24-25` has `2` and `1`.
- **A1/A3, the choice of `|` default.** With the defaults in the Architecture
  table, every input keeps its current answer. That covers absent, `0`, negative,
  a string, an out-of-range integer and a non-object document. For `HighlightDoc`,
  which has no `is<JsonObjectConst>()` guard, a non-object reads `0` today
  (`0 > 1` is false, so accepted) and `1` afterwards (still accepted).
- **A2 provenance.** `git show <first-add> | grep -c 'doc["v"] = '` returns 1 for
  `38dfbc1f`, `071259ce`, `610b993f` and `b1412ef7`, and `git log --follow`
  confirms each is the file's first commit.
- **A new refusal loses no data.** `HighlightDoc` refusing `v:0` goes through
  `loadAdopting` → `adoptedLoad(UseLoaded, false)` → `AdoptedLoad::Failed`
  (`TempAdoption.h:82-83`). `MigrationRunner.cpp:239-245` then leaves the legacy
  file untouched and out of the ledger. A `v:0` ledger takes the `nullopt` path,
  and `runIfPending` refuses to migrate (`MigrationRunner.cpp:198-202`). This is
  the protection the spec cites.
- **Error-handling table.** The `LOG_ERR` strings and return values match the code
  at each site.
- **Test citations.** `HighlightDocTest.cpp:59-70`, `TagPaletteTest.cpp:88-104`,
  `ChapterCompletionTest.cpp:266-274, 301`, `PassageDocTest.cpp:46-62`,
  `BookmarkDocTest.cpp:75-116`, `RecentBooksDocTest.cpp:309-321` and
  `FormatVersionTest.cpp:5-22` all say what the spec claims they say.
- **No host suite** for the three `src/` stores: running
  `grep -rln 'PubKeyRegistry\|MigrationRunner\|MeetingWeekCache' test` returns
  nothing (exit 1).
- **Precedent.** `CrossPointSettings.cpp:3` spells the include `<FormatVersion.h>`,
  and `:110-116` has the read-then-helper shape. #123 is `2674c3c2`.

## Findings

### MAJOR 1: A4/A7 miss a fifth host suite that compiles a changed source, so the host build breaks

**Claim.** A4: "Each of the four affected host suites adds
`${REPO_ROOT}/lib/Serialization` to its own `target_include_directories`." The
research note names them as `tag_palette`, `chapter_completion`, `passage_doc` and
`highlight_doc`.

**Problem.** `TagPalette.cpp` is also compiled by `test/migration_planner`, whose
include path has no `lib/Serialization`. Once `TagPalette.cpp` gains
`#include <FormatVersion.h>`, `MigrationPlannerTest` fails to compile. Following
the spec as written produces a red host build. The shared
`crosspoint_test_common` target does not rescue it. It adds only `${REPO_ROOT}` and
`${REPO_ROOT}/lib` (`test/CMakeLists.txt:38-41`), which resolves
`<Serialization/FormatVersion.h>` (the spelling `FormatVersionTest.cpp:3` uses) but
not `<FormatVersion.h>`.

**Evidence.** Running
`grep -rn 'TagPalette.cpp\|ChapterCompletion.cpp\|PassageDoc.cpp\|HighlightDoc.cpp\|BookmarkDoc.cpp' test --include=CMakeLists.txt`
lists seven suites:

- `tag_palette`, `chapter_completion`, `highlight_doc`, `passage_doc` and
  `bookmark_doc`, all accounted for.
- `storage_io`, which already has `${REPO_ROOT}/lib/Serialization`
  (`test/storage_io/CMakeLists.txt:29`).
- `migration_planner`, which compiles `TagPalette.cpp`
  (`test/migration_planner/CMakeLists.txt:7`). Its `target_include_directories`
  (`:15-20`) is `lib/StudyStore`, `lib/Epub`, `lib/Epub/Epub` and `lib/expat`,
  with no `lib/Serialization`.

**Fix.** In A4, name the per-suite `CMakeLists.txt` files that change: the four
listed plus `test/migration_planner/CMakeLists.txt`. Note that `storage_io` and
`bookmark_doc` already have the path. A7 still holds, because `migration_planner`
is registered at `test/CMakeLists.txt:113` and only its own file changes. Add
`migration_planner` to the step 1 verification, which already runs the whole
`ctest`.

### MINOR 1: A2 understates why the accepting default matters on the two write paths

**Claim.** A2 says that for `PubKeyRegistry`, `MigrationRunner`, `MeetingWeekCache`
and `HighlightDoc`, "no build ever wrote a file without `"v"`". That is, an absent
`"v"` is only a hand-edited or foreign file (research note, "Where an absent `"v"`
could come from").

**Problem.** That is true of the files on disk, but not of the code path. In
`PubKeyRegistry::record` and `appendLedger`, a missing file is allowed through
(`mayOverwriteAfterRead` accepts `Missing`, `DocReadStatus.h:32-34`). It leaves an
empty `JsonDocument` with no `"v"`, and the version check then runs on it
(`PubKeyRegistry.cpp:23-29`, `MigrationRunner.cpp:90-95`). So the absent-`"v"`
branch is the normal first write on every fresh card, not an edge case.

The `| FORMAT_VERSION` / `| LEDGER_FORMAT_VERSION` defaults in the Architecture
table are correct. But an implementer who "tidied" these two sites to `| 0` would
refuse every first registry write and every first ledger append. The registry
would never populate, and the migration would stop at its first file
(`MigrationRunner.cpp:247-252`). No host suite covers either path (see the grep
above), so nothing would catch it before the device.

**Fix.** Add a sentence to A2 and to "Write paths" in Data and control flow: on
the two write paths an absent `"v"` is also the missing-file first write, so their
default must be the accepting one. Name it in the PR's "untested sites" note as the
thing a reviewer must check by eye.

### MINOR 2: A4's premise about `lib/Epub` is factually wrong, though it helps the design

**Claim.** A4 says "Neither library includes anything from `lib/Serialization`
today". The research note says `HighlightDoc.cpp` gives `lib/Epub` its **first**
include of `lib/Serialization`.

**Problem.** `lib/Epub` already depends on `lib/Serialization`. The research grep
only listed five of the library's headers.

**Evidence.**
`grep -rn '#include [<"]\(Serialization\|BufferedFile\|…\)\.h' lib | grep -v ^lib/Serialization`
finds:

- `lib/Epub/Epub/Section.cpp:6` `<Serialization.h>`
- `Page.cpp:6`
- `BookMetadataCache.h:3` `<BufferedFile.h>`
- `BookMetadataCache.cpp:3, 5`
- `ContentOpfParser.cpp:5`
- `blocks/TextBlock.cpp:7`
- `blocks/ImageBlock.cpp:7`

The claim is true for `lib/StudyStore`.

**Fix.** Correct A4: the edge from `lib/Epub` to `lib/Serialization` already exists
under PlatformIO's LDF. Only `lib/StudyStore` → `lib/Serialization` is new, and
verification step 2 already says so. On the host side, `highlight_doc` still needs
the include path added, because its suite has none (`test/highlight_doc/CMakeLists.txt:7-10`).

### MINOR 3: A8 rewrites the helper comment but leaves the same idiom stated in its test

**Claim.** A8 says the helper's comment becomes wrong once three callers read
`| 0`, so it is rewritten to cover both defaults.

**Problem.** `FormatVersionTest.cpp:16` carries the same one-idiom statement in an
assertion message: `"an absent \"v\" reads as 1; a written 0 no build wrote"`. After
the change that is true only for the `| FORMAT_VERSION` callers.

**Fix.** Reword that message in the same edit, for example "a written 0 is refused;
whether an absent \"v\" reads as 0 or 1 is the caller's `|` default". Alternatively,
list the file as deliberately left alone.

## Assumptions not contested

- A1 (no flag argument or second helper): sound.
- A3: verified above.
- A5 (keep the "newer" `LOG_ERR` text): follows the issue's explicit "LOG_ERR …
  unchanged".
- A6, A7 (with MAJOR 1's addition) and A9: no objection.
- The non-goals match the issue. None of them is scope drift.

MAJOR 1 is a missed build-file edit with a mechanical fix. It does not reverse a
decision, change scope or need the human, so the spec can be fixed inline.

VERDICT: CLEAR
