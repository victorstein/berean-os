Tier: standard

# Review: issue #225 design (`docs/superpowers/specs/2026-09-30-issue-225-design.md`)

Reviewed against `gh issue view 225 --repo victorstein/berean-os`, the research note
`docs/superpowers/research/2026-09-30-issue-225-research.md`, and the tree at `bcc2b9fc`. There are no
`src/`, `lib/` or `test/` changes since `e3e5c94e` (`git diff --stat e3e5c94e HEAD -- src lib test` is
empty), so the spec's line numbers hold.

## What was checked and holds

- **Appendix A measurement, re-run.** `python3 join.py nwt_S.epub` gives `books 66 differ 0`, and so
  does `nwt_S_slim.epub`. With a counter added, both cover `chapters 1189`, which matches §4.2's claim.
  The script follows the firmware. `Epub.cpp:456-465` prefers the nav TOC over the NCX. `TocNavParser.cpp:130-133`
  strips the fragment before `createTocEntry`, as the script's `split('#')` does. The spine rule
  matches `BookMetadataCache.cpp:237-245,303-316`, and `findTargetByHref` matches
  `BibleNavScanner.cpp:193-199`.
- **A4 (one chapter derivation).** On `nwt_S.epub`, 1,189 documents carry `chapter<N>_verse<M>`
  markers, and none of them carries more than one chapter number. So `currentBibleChapter()` (first
  marker) and `unit.major` (marker at the offset) cannot disagree on the NWT.
- **A7 (whitespace).** On the raw, unstripped `<a>` text of `toc.xhtml`, 0 of 196 titles (129 in
  slim) have leading, trailing or doubled whitespace. The only U+00A0 titles are the two B12 appendix
  entries. This matches `TocNavParser.cpp:118-120`, which appends the label verbatim.
- **Inventory.** There is no eighth builder. `LauncherActivity.cpp:148,309,337` and `HomeVerse.cpp:130`
  display stored references. `MigrationPlanner.cpp:19,73` builds a `C:V` comparison key, not a label.
- **The empty-book asymmetry in §1 is real.** `bibleReference("", 5)` returns `" 5"`
  (`BibleReference.h:9-10`), and the status bar calls it unguarded (`EpubReaderActivity.cpp:1815-1818`).
- **Abbreviations in T3.** The nav text for book 23 is `Is.` and for book 22 is `Cant.`, read from
  `nwt_S.epub`'s `biblebooknav.xhtml`. T3's expected strings are real.

## Findings

### MAJOR 1: `PlacesStoreTest` and `PlacesStoreLazyLoadTest` stop linking, and the spec's file list omits them

- **Claim.** §10 lists the CMake edits as `test/ui_layout/CMakeLists.txt` and
  `test/places_doc/CMakeLists.txt`. §7 T2 says "The `PlacesDocTest` target gains `BibleReference.cpp`".
- **Problem.** `PlacesDoc.cpp` is compiled by a second test directory. Once `formatReference` and
  `formatChipLabel` delegate to `BibleReference::format` (§5, A6), both executables there fail at link
  time with an undefined `BibleReference::format`. CI builds and runs every host target, so the
  host-test job goes red. The spec's `ctest` list (§7, "Build and format") does not name these
  targets either, so the implementer would not notice locally when running only the named suites.
- **Evidence.** `test/places_store/CMakeLists.txt:5-7,22-23` puts `${REPO_ROOT}/src/util/PlacesDoc.cpp`
  in `PLACES_STORE_SOURCES` for both `PlacesStoreTest` and `PlacesStoreLazyLoadTest`.
  `grep -rn "PlacesDoc.cpp" test --include=CMakeLists.txt` finds `test/places_doc` and
  `test/places_store`. CI runs `cmake --build build/test` and `ctest` (`.github/workflows/ci.yml:148,151`).
- **Fix.**
  - Add `${REPO_ROOT}/src/util/BibleReference.cpp` to `PLACES_STORE_SOURCES`.
  - Add `test/places_store/CMakeLists.txt` to §10's edited files. It is a subdirectory file, not the
    shared `test/CMakeLists.txt`, so no hand-off line is needed.
  - Run the full `ctest` in §7, not only the four named suites.

### MINOR 1: two of the §5 `Verses` mappings are ill-formed as written

- **Claim.** §5 maps the title and status bar to `{max(currentBibleChapter(), 0)}`, and the stored
  place and chip to `{unit.major, chapterOnly ? 0 : unit.minor}`. §7 says T3 "replicates those two
  one-line mappings".
- **Problem.**
  - `std::max(int, int)` is an `int`, and `chapterOnly ? 0 : unit.minor` promotes to `int`.
  - Brace-initialising a `uint16_t` member from a non-constant `int` is a narrowing conversion. That
    is ill-formed, and both host clang and the firmware toolchain reject it.
  - The other mappings are fine: `VerseEntry` and `TypedReference` fields are `uint8_t`
    (`IndexFormat.h:54-56`, `TypedReference.h:15-18`), which widen.
- **Evidence.** A scratch file with the spec's exact shapes, built with
  `c++ -std=gnu++2a -Wall -Wextra -pedantic`, fails with:
  `error: non-constant-expression cannot be narrowed from type 'int' to 'uint16_t' ... [-Wc++11-narrowing]`
  on both lines.
- **Fix.** Write the casts into the §5 table:
  - `{static_cast<uint16_t>(std::max(currentBibleChapter(), 0))}`
  - `{unit.major, static_cast<uint16_t>(chapterOnly ? 0 : unit.minor)}`

### MINOR 2: "byte-identical for every input" is wider than what the design delivers

- **Claim.** §6 says "The passage reference and the stored place are byte-identical for every input,
  including an empty title (A6)". The §5 table says the `formatReference` adapter uses the "fixed
  buffer" overload.
- **Problem.** There are two gaps. Neither is reachable on the NWT, but the sentence says "every
  input".
  - **`0` is a sentinel in `Verses` but a real value in markup.** `VerseAnchors` accepts `verse == 0`
    and `chapter == 0` (`VerseAnchors.cpp:36-38`, `%u` with no lower bound).
    - Today a `chapterN_verse0` anchor gives the passage `"Isaías 40:0"` (`VerseAnchors.cpp:123`).
      Under the new rule, `verse 0` means "the whole chapter", so it becomes `"Isaías 40"`.
    - A `chapter0_verseM` anchor collapses to the bare book name, losing the verse.
    - The same applies to a `bible.idx` entry with verse 0 in the search hit row.
    - I measured 0 such markers in `nwt_S.epub` and `nwt_S_slim.epub`.
  - **Cap-then-collapse ordering.** If `formatReference` formats into a fixed buffer and then applies
    `utf8SafeSummary`, it caps before it collapses whitespace. Today it collapses first and then caps
    (`Utf8.cpp:185-200`, `PlacesDoc.cpp:59`). The two orders differ for a reference longer than 48 B
    that contains a whitespace run.
- **Fix.**
  - Scope the §6 sentence to the NWT's markup, and add the verse-0 / chapter-0 case to the §6 list,
    unreachable, with the measurement.
  - Specify that `formatReference` uses the `std::string` overload followed by the existing
    `utf8SafeSummary(…, MAX_REFERENCE_BYTES)`. That is byte-identical to today by construction.

### MINOR 3: T3's derivation assertion needs the book stamped, and T3 cannot detect the join divergence the issue names

- **Claim.** §7 T3 asserts `PlacesDoc::placeUnit(study::scanUnits(doc), offsetOfVerse31)->unit.major`,
  and "the same holds for" the other names.
- **Problem.**
  - **As written, the call returns `nullopt`.** `scanUnits` never sets `book`, since the caller
    stamps it (`UnitAnchors.h:24-28`). `placeUnit` returns `nullopt` when `units.book == 0`
    (`PlacesDoc.cpp:40`). The `->` would dereference an empty optional.
  - **T3 feeds the same `name` string to every surface.** So it can only fail on numbering and joins.
    It cannot fail on the risk the issue actually names, where the TOC-inherited title and the
    nav-joined name differ. The spec does cover that risk: by measurement (Appendix A, re-verified
    above) and by `BibleBookJoinTest`'s doubled-TOC fixture. But T3 should not be read as the guard.
- **Fix.**
  - In T3, stamp `units.book = 23` (and 22) before calling `placeUnit`.
  - Say in T3's header comment, and in the PR, that name-source agreement is pinned by the Appendix A
    measurement and `test/bible_book_join/`, not by T3.

### MINOR 4: `utf8SafeTruncateBuffer` keeps a lone lead byte at index 0

- **Claim.** §4.1 says "Cut on a UTF-8 boundary to fit `outBytes`", via `utf8SafeTruncateBuffer` after
  a clamp. T1 includes "a buffer cut that would split `"É"` lands before it" and `outBytes == 1`.
- **Problem.** The helper only drops an incomplete trailing sequence when `leadPos > 0`
  (`Utf8.cpp:161`). A cut that leaves only the first byte of a leading multi-byte character, such as
  `format(out, 2, "Éxodo", {5})`, therefore writes `"\xC3"`, which is invalid UTF-8.
  `copyUtf8Truncated` inherits the same flaw. With the real buffers (49 and 64 B) this is
  unreachable, but it breaks the stated contract. A T1 case placed at index 0 would fail.
- **Evidence.** Linked against `lib/Utf8/Utf8.cpp`, `utf8SafeTruncateBuffer("\xC3\x89xodo", 1)`
  returns `1`.
- **Fix.** Pick one:
  - In `format`, after the helper, treat a result that ends in a lone lead byte at index 0 as length 0.
  - Or pin T1's split case with `outBytes = 2` on `"Éxodo 5"`, expecting `""`, so the implementer has
    to handle it.

### MINOR 5: factual slips in supporting claims

None of these changes a decision.

- **§4.1: "nothing in `src/util/` reaches into `src/activities/`" is false.** `src/util/ScreenshotUtil.cpp:15`
  includes `"activities/Activity.h"`. The move to `src/util/` is still right, because `PlacesDoc` must
  include the formatter. Restate the reason as "`PlacesDoc` must not depend on an activity header".
- **§1 table and §6 item 2: the search overflow cannot happen on any publication.** The spec lists
  search overflow as a user-visible change that is merely unreachable on the NWT, and §1 says both
  search sites "may split UTF-8".
  - `forBook` names are already cut to 47 B by `copyUtf8Truncated` (`BibleBookNameTable.cpp:64`,
    `NAME_BYTES = 48` at `BibleBookNameTable.h:31`).
  - The numbers are `uint8_t`, so they are at most `255:255-255` (11 B).
  - 47 + 1 + 11 = 59, which is less than the 63 usable bytes of `REFERENCE_BYTES = 64`
    (`BibleSearchActivity.h:87`).
  - Drop §6 item 2 and correct the §1 "Cut" cells.
- **§7 T1: "The five `formatTypedReference` tests" are three `TEST`s.** They are at
  `TypedReferenceTest.cpp:203,228,238`, with five expectations between them.
- **A7: the Appendix A script cannot measure whitespace.** A7 says it was "measured with the Appendix A
  script", but that script `.strip()`s every title (line 329), so it cannot see leading or trailing
  whitespace. The claim is still true on the raw text (see "What was checked"). Cite the actual
  check.

## Assumptions

- **A1: agree.** The status bar and passage sites are the same class of duplication, and the
  status-bar `" 5"` is a real inconsistency.
- **A2: agree.** "One source, two access paths" is a defensible reading of the acceptance criterion.
  The two paths are now measured to agree on both local NWT builds rather than assumed.
- **A3: agree.** The table's cost is real: `BibleBookNameTable.cpp:28-51` streams, and can inflate,
  the nav spine item.
- **A4: agree.** Confirmed above.
- **A5: agree.** A style enum would carry no logic.
- **A6: agree,** subject to MINOR 2's ordering fix.
- **A8: agree.** Stored labels are read back verbatim (`PlacesDoc.cpp:152`, `PassageDoc.cpp:323`).
- **A9: agree,** subject to MINOR 4.

MAJOR 1 is a missed build seam. It is fixable inline and reverses no decision.

VERDICT: CLEAR
