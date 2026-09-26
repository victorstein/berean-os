# Spec review 1: issue #99, the in-memory HalStorage fake

Reviewed: `docs/superpowers/specs/2026-09-26-issue-99-design.md` at `8902092f`, against
issue #99 (`gh issue view 99 --repo victorstein/berean-os`), issue #98 (the consumer
Goal 3 is sized for), the research note
`docs/superpowers/research/2026-09-26-issue-99-research.md`, and pass 0
(`docs/superpowers/reviews/issue-99-spec-review-0.md`).

## Pass-0 fixes: all four applied correctly

- **BLOCKER 1 (`String` input path).** I wrote the spec's `String` block (spec
  :250-266) verbatim into a scratch `Arduino.h`, plus a stub `HalStorage.h` built the
  way A-2 describes: the real declarations minus `Print`, `readFileToStream`,
  `StorageLock` and `storageMutex`, with `override` dropped, `<fcntl.h>`, and
  `using oflag_t = int`. I then compiled the real sources with
  `c++ -std=c++20 -Wall -Wextra -pedantic` and ArduinoJson 7.4.2 from
  `build/test/_deps/arduinojson-src/src`.
  - `lib/Serialization/PersistableStore.cpp` gave no diagnostics.
  - `src/study/TagPaletteFile.cpp` gave one warning, `unused variable 'MODULE'`,
    because the no-op `LOG_ERR` stub discards it. That is harmless because the
    host build has no `-Werror`.
  - `lib/StudyStore/StudyStore/TagPalette.cpp` gave no diagnostics.

  The input specialisation the spec now cites is
  `Reader<TSource, void_t<typename TSource::const_iterator>>`
  (`Deserialization/Readers/IteratorReader.hpp:34-39`). The research note's §3.1 is
  corrected to match.
- **MAJOR 2 (sticky hooks versus the chained test).** `clearFailures()` is in A-10
  (:139), the controls block (:232), the data-flow note (:317-320), the
  AtomicWriteTest bullet (:413-416), and a HalStorageFakeTest pin (:404).
  - Traced against `PersistableStore.cpp:63-106`: the primary is Missing and the
    `.tmp` parses, so the action is `PromoteTempAndUseIt`. The code then calls
    `rename(tmp, path)`, and with the hook cleared it succeeds.
  - The assertion as written is reachable.
- **MINOR 3 (stale comments).** `test/stubs/Arduino.h:3-7`,
  `test/pagination/CMakeLists.txt:8-10,26` and `TempAdoptionTest.cpp:3-7` are in
  Files touched (:161-165). The claim that pagination names no `String`, `millis` or
  `micros` holds: `grep -nwE "String|millis|micros"` over `test/pagination/*.cpp`
  and every source that suite compiles returns 0 hits. The justification for
  leaving the other comments alone is wrong; see finding 1.
- **MINOR 4 (mechanical gaps).** `HalStorage::HalStorage()` and the `instance`
  definition are in the defined list (:209-210). The `override` drop is in A-2
  (:131).

## Other claims verified

- **Every other store loader compiles against the stub, not just TagPaletteFile.**
  Goal 1 and Goal 3 say the per-store loaders compile against the fake unmodified. I
  compiled the four loaders the first round doesn't touch against the same scratch
  stub, and none produced an error:
  - `src/study/ChapterCompletionFile.cpp`
  - `src/study/PassageFile.cpp`, including the `HalFileReader` at `:15-26`
  - `src/util/BookmarkFile.cpp`
  - `src/util/HighlightFile.cpp`
- **A-15, the pagination warning.** `lib/Epub/Epub/blocks/TextBlock.cpp` compiled
  against the new stub produces exactly one new diagnostic, `-Wsign-compare` at
  `:393`. `GfxRendererFake.cpp` fails only on the five `HalFile` bodies at
  `:127-131`, which the spec deletes.
- **No other suite breaks when the stub `Arduino.h` grows `String`.**
  `font_page_slots` compiles all four of its C++ TUs against the new `Arduino.h`
  without errors. The only suites with `test/stubs` on the include path are
  `pagination`, `minibidi_arabic`, `bible_search_index`, `bible_search_scanner` and
  `font_page_slots`. Of those, only `pagination` compiles a source that includes
  `<HalStorage.h>`, which confirms A-1's "only current consumer".
- **The include path is safe.** `crosspoint_test_common` adds only `${REPO_ROOT}`
  and `${REPO_ROOT}/lib` (`test/CMakeLists.txt:37-41`), so `<HalStorage.h>`,
  `<Arduino.h>` and `<Logging.h>` can't resolve past `test/stubs`.
  `<ObfuscationUtils.h>` resolves through `lib/Serialization`.
- **The link surface is complete for the first round.** `PersistableStore.cpp`
  references these `Storage` methods: `mkdir`, `writeFile`, `remove`, `rename`,
  `exists` and `readFile`. All of them are in the defined list.
  `classifyDocRead`, `tempAdoptionAction` and `adoptedReadStatus` are `constexpr`
  in headers (`DocReadStatus.h:18`, `TempAdoption.h:29,48`).
- **The obfuscation stub has the right signature.**
  `deobfuscateFromBase64(const char*, size_t, bool*, bool*)` matches
  `ObfuscationUtils.h:34`, and it is the only overload `PersistableStore.cpp:122`
  references.
- **Semantics match the SD code.**
  - `HalStorage.cpp:38-40,46,56,89-95` forwards each call straight to
    `SDCardManager`.
  - `readFile` stops at `maxSize = 50000` (`SDCardManager.cpp:202-204`).
  - `writeFile` does `exists` → `remove` → open with
    `O_RDWR|O_CREAT|O_TRUNC` (`:282-283`, `:337`).
  - `mkdir` with `pFlag` creates the missing intermediate directories, then calls
    `mkdir(parent, &fname)`. That call opens with `O_CREAT | O_EXCL`, so it fails
    when the final component already exists (SdFat 2.3.1 `FatFile.cpp:356-366,379`).
  - `oflag_t` is `typedef int` on SdFat's fcntl path (`FsApiConstants.h:43`).
- **The TagPaletteFile arms trace correctly.** `failReadsOf(tags.json)` gives
  Unreadable → ReportFailed → Failed (`TempAdoption.h:35-37`,
  `TagPaletteFile.cpp:59-60`). An over-budget palette returns before
  `mkdir("/.berean")` (`:69-74`), so "writes nothing" holds.
- **The truncation test holds.** For a primary over 50,000 bytes, a truncated
  document fails to parse, which classifies as ParseError. The 45,000-byte figure
  is `SaveBudget.h:23`.
- **Nothing here conflicts with #98.** #98's Verify line wants the helper to
  report empty and keep the `.tmp`. That matches this spec's `AdoptingReadTest`
  assertion, and no TagPaletteFile test touches `DeleteTempReportEmpty`.
- **One citation is off by one.** The spec and research cite `SDCardManager.cpp:293`
  for "returns true only on a full write". That line is `f.close()`; the return is
  at `:294`. It doesn't change anything and isn't raised as a finding.

## Findings

### 1. MINOR: comments that this change makes false were kept on a wrong premise, and three of them are in `lib/`/`src/`

**Claim.** "What changed" item 3 (spec :44-46) keeps the per-store comments and says
"They are still true, because those stores' `.cpp` files don't get host suites in
this change."

**Problem.** Those comments don't say the stores *are not* built on the host. They
say they *cannot* be, and they give the missing `Arduino.h` stub as the reason. After
this change that is false: there is a `String`-bearing stub `Arduino.h` and a
`Storage` fake. HighlightFile.cpp and BookmarkFile.cpp both compile against it (see
"Other claims verified"). Three of the false comments are in production headers,
which the "byte-identical" non-goal (:112-114) forbids editing. So the spec neither
fixes them nor records them. CLAUDE.md requires comments to be written "for the
merged state".

**Evidence.** Each of these becomes false after the change:

- `lib/Serialization/TempAdoption.h:12-14`: "so the branch logic is host-testable
  even though the I/O around it is not". That I/O is exactly what
  `AdoptingReadTest` now covers.
- `src/util/HighlightFileAction.h:10-15`: "HighlightFile.cpp itself cannot be built
  on the host … with no host stub anywhere in this repo and too costly to fake
  convincingly."
- `src/util/BookmarkSaveAction.h:9-11`: "cannot be built on the host".
- `test/highlight_file/CMakeLists.txt:1-3`: "cannot be built here".
- `test/highlight_file/HighlightFileActionTest.cpp:3-6`: "cannot be built on the
  host … no stub in test/stubs". This file wasn't in pass 0's list.
- `test/bookmark_save_action/BookmarkSaveActionTest.cpp:3-6`: "cannot be built on
  the host". This file wasn't in pass 0's list either.

**Fix.** Either option below keeps the change's behaviour and scope.

- **In `test/`,** add comment-only rewrites of the three test-side files to Files
  touched. For example: "not yet host-built; `test/stubs` now has the `Storage` fake
  (see `test/storage_io/`) if a suite is wanted".
- **In `lib/` and `src/`,** do one of these:
  - Narrow the non-goal to "no behavioural change to `lib/`/`src/`; comment-only
    corrections to `TempAdoption.h:12-14`, `HighlightFileAction.h:10-15` and
    `BookmarkSaveAction.h:9-11`". Comment-only edits need no `pio run`, per CLAUDE.md
    "Testing checklist" item 1.
  - Or list those three comments in the Shared-file report as a named follow-up.
- **Either way,** delete the "They are still true" sentence.

VERDICT: CLEAR
