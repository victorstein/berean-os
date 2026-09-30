Tier: standard

# PR #231 review, pass 0: unify the Bible reference formatters (#225)

Reviewed `main...d6db1925` against issue #225, the design
(`docs/superpowers/specs/2026-09-30-issue-225-design.md`) and the plan
(`docs/superpowers/plans/2026-09-30-issue-225-plan.md`).

Host tests were rebuilt from a copy under `/private/tmp/claude-501/rev225`. `BibleReferenceTest`,
`PlacesDocTest`, `ReferenceConsistencyTest`, `TypedReferenceTest`, `PlacesStoreTest` and
`PlacesStoreLazyLoadTest` pass: 75 of 75. `pio` was not run, per the brief. The PR reports
`pio run -e x4pro` SUCCESS and a full `ctest` of 1501 tests.

## Intent

**Acceptance criteria.**

1. *One formatting function, with full-name and abbreviated variants, used by all five call sites.*
   Met. `BibleReference::format` (`src/util/BibleReference.h:32,35`) is now the only builder.
   - The reader title (`EpubReaderActivity.cpp:1799`) calls it.
   - `PlacesDoc::formatReference` and `formatChipLabel` delegate to it (`PlacesDoc.cpp:59-60,68`).
   - The search "Go to" row (`BibleSearchActivity.cpp:529-531`) and the hit row (`:600-601`) call it.
   - The two sites the issue did not list also go through it: the status bar (`EpubReaderActivity.cpp:1823`)
     and the tagged passage (`PassageSelectActivity.cpp:237-238`). Spec A1 justifies this, and the PR table
     marks them "not in the issue". It follows from the "one function" criterion rather than expanding scope.
   - A grep for `bibleReference(`, `formatTypedReference` or `reader/BibleReference.h` in `src`, `test` and
     `lib` finds nothing.
   - Spec A5 reads "variants" as the caller passing a full name or an abbreviation, with no style enum. That
     is a reasonable reading and the PR states it.
2. *A documented source for the book name.* Met in the header comment (`BibleReference.h:10-14`): TOC titles
   for full names, and nav link text for abbreviations (#206 / PR #90). The two full-name paths are kept, and
   their agreement is measured (spec Appendix A) rather than assumed. The PR says this plainly.
3. *A host test showing one place formats the same way across surfaces.* Met by
   `test/places_doc/ReferenceConsistencyTest.cpp`, which covers title, stored place, chip, "Go to" and hit row
   for three books. It also pins the "one chapter derivation" claim: `BibleChapterNumber::scan` must equal
   `placeUnit(...).unit.major` (`:59-66`). The test's header says it guards numbering and joining, not
   name-source agreement. See MINOR 1 for a limit on two of its surfaces.
4. *No change on the NWT unless it fixes an inconsistency; list any such case.* Met.
   - The PR lists three edge cases, each unreachable on the NWT.
   - I checked the old paths:
     - Stored place: same bytes, uncapped and then `utf8SafeSummary` as before. The new
       `AStoredReferenceCollapsesWhitespaceBeforeCapping` test pins this.
     - Chip: same `"%s %u[:%u]"` shape.
     - Search: same three shapes, with the same empty-name branch.
     - Passage: `VerseAnchors::format` wrote `"%u:%u"` (`VerseAnchors.cpp:120-125`) with the same
       empty-title fallback.
     - Title: `-1` and `0` map to the book alone, as before.
   - The only differences are the listed verse-0/chapter-0 and empty-title status-bar cases.

**Spec coverage.** Every row of spec §5 is implemented as written, and the §10 file list matches the diff.
Nothing was reduced. Spec §4.1 requires that no lone lead byte is left at index 0; this is implemented
(`BibleReference.cpp:31-43,63`) and tested (`BibleReferenceTest.cpp:66-69`). The three
`formatTypedReference` tests moved with their expected strings unchanged (`BibleReferenceTest.cpp:31-58`).
No store format, migration or translation was touched, as §3 and A8 require.

**Plan divergence.** None found. The code in Tasks 1–6 matches the plan's blocks, including `placeVerses`,
`knownChapter`, the `shown` alias and the passage rewrite. Task 7's verification and Task 8's PR notes are
reflected in the PR body.

## Quality

- **Pattern.** The formatter mirrors `PlacesDoc`: a namespace of pure functions under `src/util/`, free of
  Arduino, and host-tested. Moving it from `src/activities/reader/` is justified: `PlacesDoc` must include it
  and must not depend on an activity header. The fixed-buffer cut follows `copyUtf8Truncated`. The
  `utf8SafeTruncateBuffer` length is safe here, because `snprintf` has already written `outBytes - 1` bytes
  when truncation happens, so the "trusts len" hazard does not apply.
- **Adapters.** `formatReference` and `formatChipLabel` keep their signatures and their one policy each:
  the 48-byte collapse-then-cap, and the stored-reference fallback. The two wrappers with no policy were
  deleted. This matches the #127 "one helper replaces diverged copies" shape the spec cites.
- **Memory.** No new heap allocation. The scratch array is 18 bytes on the stack, and the fixed-buffer sites
  reuse their existing arrays. `reserve` is called before the `std::string` appends.
- **Comments.** Most earn their place: the `knownChapter` -1 note, the `formatReference` collapse-before-cap
  note, and the `Utf8.cpp:161` gotcha. One header sentence is inaccurate (MINOR 2).
- **Tests.**
  - The formatter tests are behavioural and probe real edges: a cut on a codepoint boundary, a lone lead
    byte at 0, `outBytes` of 0 and 1, the largest numbers, and agreement between the two overloads.
  - The new chip test (`PlacesDocTest.cpp:314-318`) failed before the change, so it pins a real fix.
- **Duplication.** One small re-implementation (MINOR 3).

## Findings

**MINOR 1: two consistency-test surfaces restate the call-site mapping instead of exercising it.**
`test/places_doc/ReferenceConsistencyTest.cpp:41-55` redefines `readerTitle`, `goToRow` and `hitRow` by
copying the `Verses{...}` mappings from `EpubReaderActivity.cpp:80-82`, `BibleSearchActivity.cpp:529-531`
and `:600-601`. If the firmware's `knownChapter`, for example, changed, this test would still pass. The
spec (§7 T3) acknowledges this, because those activity files cannot be linked on the host. The stored place
and the chip do go through the real `PlacesDoc` code, so the criterion is still met.

Cheap fix, optional: hoist the one non-trivial mapping into `BibleReference`, so the firmware and the test
share it. That mapping is `knownChapter`, the int-to-`Verses` clamp. For example
`BibleReference::Verses BibleReference::chapterOf(int)`. The two struct-field copies are trivial enough to
leave.

**MINOR 2: the header overstates where the numbers come from.** `src/util/BibleReference.h:14` says "Every
number comes from the chapter<N>_verse<M> markers VerseAnchors parses." The "Go to" row's numbers are the
user's typed reference. `ResolvedReference::reference` is "as typed, except a single-chapter book's lone
number" (`TypedReference.h:41-43`). Search only checks them against the index. Suggested wording: "Every
chapter a surface derives from a document comes from the chapter<N>_verse<M> markers VerseAnchors parses."

**MINOR 3: `isIncompleteLeadSequence` re-implements `utf8CodepointLen`.**
`src/util/BibleReference.cpp:31-43` copies the lead-byte length table from `utf8CodepointLen`
(`lib/Utf8/Utf8.cpp:74-80`). That function has external linkage but is not declared in `Utf8.h`. Two fixes
are possible:
- declare `utf8CodepointLen` in `Utf8.h` and call it, which reduces the helper to one line;
- or leave it, since the table is four lines and fixed by the UTF-8 standard.

The index-0 workaround itself is correct and tested.

VERDICT: CLEAR
