# Plan review 0 — issue #98, one `.tmp` adoption policy

**Plan:** `docs/superpowers/plans/2026-09-26-issue-98-plan.md`
**Spec:** `docs/superpowers/specs/2026-09-26-issue-98-design.md`
**Base:** `e392d97e` (tree matches the plan's quoted old text at `6c57bcd0` for every file checked)

## Summary

The plan is sound. Every spec requirement maps to a step, the types and signatures stay the same
from step to step, and the old text each step quotes matches the tree. The planned tests use APIs
that exist and assert what the fake actually does. The test counts are also right. Two minors, both
fixable inline.

## Coverage — spec requirement → step

| Spec requirement | Step |
|---|---|
| A-1 rename to `KeepTempReportEmpty`, reason moved onto the enumerator | 1 (`TempAdoption.h`, `PersistableStore.cpp`, five `case` labels, both tests) |
| A-3/A-4 `AdoptedLoad` + `adoptedLoad`, exhaustive test | 2 |
| A-5/A-6 `DocReader`, `DocAcceptor`, `loadAdopting` signature | 4 (header), 5–9 (call sites) |
| A-7 `readDocFromFileAdopting` on the shared core, `AdoptingReadTest.cpp` unmodified | 4 (with the `git diff --stat` gate) |
| A-8 one `JsonDocument`; A-9 promote before accept | 4 (`readAdopting` / `loadAdopting` bodies) |
| A-10 `PERSIST` logs, TagPalette gains the rejected-after-recovery line | 4 (the core logs it for every caller) |
| A-12 `BookmarkFile` keeps `clear()` and the `LOG_DBG` | 8 |
| A-13 host coverage: `LoadAdoptingTest`, TagPalette, ChapterCompletion, Passage suites; >50 KB `.tmp` | 3, 5, 6, 7 |
| A-14 no `test/CMakeLists.txt` edit | Line 16, no step touches it |
| §Error handling: `PassageFile` zero-length `LOG_ERR` in the reader | 7 (`readInto` body) |
| §Call sites: stale comments `PersistableStore.h:73-74,76-78,80-87`, `HighlightFile.h:9-15`, `test/storage_io/CMakeLists.txt:1-5` | 4, 9, 7 |
| §Testing: build/format/`ctest`/flash before-after, device recipe | 0, 10, 11 (PR body) |

## Things verified that could have been wrong but are not

- **Test counts.** Base `StorageIoTest` has 36 tests (10+5+15+6 `TEST_F` across the four files) and
  `TempAdoptionTest` has 12. The planned counts check out: 36→47→48→53→59, 12→17, and a total of +28.
- **Fake behaviour matches every assertion.** `failReadsOf` keeps `exists()` true, makes `readFile`
  return `""` and makes `openFileForRead` fail (`test/stubs/HalStorageFake.h:27-29`,
  `HalStorageFake.cpp:121-125,149-152`). `failRenamesFrom` changes nothing (`:96-97`).
  `readFile` caps at 50,000 (`:25,124`), so the `PassageFile` >50 KB `.tmp` test really does show
  that the streaming reader handles the `.tmp`.
- **The >50 KB passage fixture fits.** Each snippet is 116 + 1–3 bytes, which is ≤
  `MAX_SNIPPET_BYTES` = 120 (`PassageDoc.h:32`), so nothing is truncated. `add()` does not dedupe
  (`PassageDoc.cpp:90-106`), and `fromJson` fails only on version, links or budget (`:219-256`).
  At roughly 250 B a row, 300 rows is about 75 KB, which is over the 50 KB cap and under the 200 KB
  budget. The plan also covers the fallback.
- **Fixture APIs exist exactly as written.** These are `Unit{…}`, `Fingerprint{…}` and `toTagId`
  (`test/passage_doc/PassageDocTest.cpp:13-20`), `markRead` / `toJsonWithinBudget` / defaulted
  `operator==` (`ChapterCompletion.h:39,45,54`), `BookmarkDoc::fromJson(JsonVariantConst,
  std::vector<BookmarkEntry>&)` (`src/util/BookmarkDoc.h:50`), and `HighlightDoc::fromJson`
  (`lib/Epub/Epub/HighlightDoc.h:69`).
- **Static-init order is safe.** `PATH = PassageFile::path(PUB_KEY)` at namespace scope reads only
  `inline constexpr char[]` constants (`lib/Serialization/SdPaths.h:30,32`).
- **Fixture names do not collide across TUs.** `LoadAdopting`, `ChapterCompletionFileIo` and
  `PassageFileIo` are new, and the existing names are `AdoptingRead`, `AtomicWrite`,
  `HalStorageFake` and `TagPaletteFileIo`.
- **Includes compile.** Test and firmware both have the `const char*` overload of `openFileForRead`
  (`test/stubs/HalStorage.h:45`, `lib/hal/HalStorage.h:40`). `lib/Utf8` is added for
  `PassageDoc.cpp`'s `<Utf8.h>`. The source list matches `test/passage_doc/CMakeLists.txt`.
  `TagPalette.cpp` is already present. Only `TagPaletteFileTest.cpp` includes one of the five
  headers from `test/`, so the new `<TempAdoption.h>` include in those headers reaches no suite
  that lacks `lib/Serialization`.
- **No caller breaks on the alias.** No declaration outside the five headers names `LoadResult`
  except `EpubReaderBookmarksActivity.cpp:34`, which uses it as a type and works unchanged through
  the alias.
- **Red and green hold where claimed.** Step 5 (TagPalette), step 6 (ChapterCompletion) and step 7
  (two Passage tests: a garbage `.tmp` and a zero-byte `.tmp`) each fail today because the
  hand-rolled loader removes the `.tmp`. Every other new test pins today's behaviour.
- **FILES lines.** Lines 9-14 sit at column 0, outside any fence, and use repo-relative paths. They
  cover every file any step edits or creates. `AdoptingReadTest.cpp` is not edited, and the PR
  body goes to the scratchpad.
- **TDD exceptions are sanctioned by the spec.** Steps 8–9 have no red because A-13 excludes
  `BookmarkFile`/`HighlightFile` from host builds. Step 3 is explicitly left uncommitted and paired
  with step 4, so no commit is ever non-compiling.

## Findings

### MINOR 1 — Step 1's "no output" grep check fails as written

- **Claim.** Step 1 ends: "Check nothing else names it: `grep -rn DeleteTempReportEmpty lib src
  test` → no output" (plan `:208`).
- **Problem.** `test/storage_io/TagPaletteFileTest.cpp:2` still says "The DeleteTempReportEmpty
  arm is deliberately not asserted". The plan does not edit that comment until step 5 (plan
  `:626-638`). An implementer running the check literally gets a hit and a failed gate at step 1.
- **Evidence.** `grep -rn DeleteTempReportEmpty lib src test` at base returns 12 lines. Step 1
  removes 11 of them and leaves `test/storage_io/TagPaletteFileTest.cpp:2`.
- **Fix.** Either say the expected output is exactly that one comment line (step 5 rewrites it),
  or move step 5's header-comment replacement into step 1. The file is already on a `FILES:` line.

### MINOR 2 — Step 10 pushes unconditionally, but step 11 is gated

- **Claim.** Step 10 ends "Push: `git push`." (plan `:1270`) with no condition. Step 11 opens the
  PR "Only when the pipeline's `implement` phase asks for it" (`:1276`).
- **Problem.** The project CLAUDE.md (Git workflow, rule 2) forbids pushing without explicit
  approval. Other plans that push record the authorisation, for example
  `2026-09-17-issue-40-plan.md:978` ("authorised by the pipeline brief"). This one does not, so an
  implementer following it literally pushes before anything asked for it.
- **Fix.** Gate the push the same way as step 11: "Push only when the pipeline brief authorises
  it", or move `git push` into step 11.

No BLOCKER and no MAJOR. Neither minor reverses a decision, changes scope, or needs a human
judgment.

VERDICT: CLEAR
