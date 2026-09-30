Tier: heavy

# PR #193 code-quality review, pass 0 (issue #188)

Scope: `gh pr diff 193`, the code and test changes under `lib/`, `src/` and `test/`. The spec, plan,
research and earlier reviews were read only to check whether a pattern was a deliberate decision. I
did not review them.

## Overall

The change is sound. It mostly extends existing shapes rather than inventing new ones:

- `TextRepairInputs` copies `MigrationInputs`' context pointer plus function pointers, and even its
  comment (`lib/StudyStore/StudyStore/TextRepair.h:56-57` vs `MigrationPlanner.h:40`).
- `TextAllocator` has the same shape as `BibleSearch::BuildAllocator` and says so
  (`PassageText.h:452-455` in the diff; `lib/BibleSearch/BibleSearch/Allocator.h:13`).
- `rangeText` is a sibling of `unitText` in `src/study/UnitIndexCache.cpp:400-407`.
- The `loadAdopting` overload shares one body through `loadAdoptingInto`
  (`lib/Serialization/PersistableStore.cpp:215-246`) instead of copying it.
- Error handling follows the repo's shape throughout: `LOG_ERR` plus return false, refuse and never
  cut, and roll back in memory when a save fails.
- Dead code was removed rather than left behind: `PassageLabel.h`, `appendWords`, the `label`
  member, `FIT_ELLIPSIS`, `TEXT_CAPACITY` and its `static_assert`, and the ellipsis branch of
  `fitPassage`.
- The tests are well designed:
  - Behaviour-named cases, with the budget arithmetic spelled out in comments (`StudySleepFitTest.cpp`).
  - An invariant sweep over heights (`NeverEndsOnAnEllipsisAndAlwaysKeepsEveryWord`).
  - A chunked-feed equivalence check (`AScannerWithTheFilterMatchesTheWholeDocumentPass`).
  - A starvation test for `RepairSchedule`.
  - Allocation failure injected through the allocator seam, not a global `operator new` override.
  - Fixtures follow the public-repo excerpt rule (`PassageTextFilterTest.cpp:10-13`).

Nothing below reverses a design decision, changes scope or needs a judgment only the owner can make.
The findings are listed most severe first.

---

## MAJOR

### M1: `copyPassage` replaces the compiler's copy with a hand-kept field list that no test holds complete

`lib/StudyStore/StudyStore/TaggedPassage.h:50-63` copies twelve fields one by one. Before this PR the
rollback paths used the implicit copy: `const study::TaggedPassage backup = passages_.passages()[index];`
(removed at `src/study/StudyStore.cpp:351`). That copy picked up every field automatically.

`TaggedPassage` had to become move-only (`PassageText` cannot report a failed copy), so the helper
itself is justified. The problem is what it now carries:

- It is the only thing that keeps the undo in `removePassage` (`StudyStore.cpp:351-362`) and in
  `repairTexts` (`StudyStore.cpp:496-497, 525-526`) complete.
- If a thirteenth field is added to `TaggedPassage` and nobody touches `copyPassage`, a failed save
  "restores" the row with that field reset to its default, silently.
- The tests that use `copyPassage` (`test/passage_doc/PassageDocTest.cpp:571, 700, 874, 1107`) each
  check only the field they care about (links, text, `whole`/snippet). None would fail if a field
  were dropped.

**Fix, inline:** add one test that fills every field of a `TaggedPassage` with a non-default value,
`copyPassage`s it, and compares the two rows through `toJson` (or field by field). A cheaper companion
guard is a `static_assert(sizeof(TaggedPassage) == N, "update copyPassage")` next to the helper.
Either one turns the drift into a build or test failure.

---

## MINOR

### m1: Two identical memory-logging helpers, and a pick log that prints the same numbers twice

- `logStudyMemory` (`src/study/StudyStore.cpp:24-31`) and `logSleepMemory`
  (`src/activities/boot_sleep/StudySleepScreen.cpp:148-153`) have the same body: same tag, format and
  three `heap_caps_*` calls. `src/study/BibleSearchIndexer.cpp:322-325` is a third variant of the
  same line. One shared helper (for example beside `Logging.h`, or in `src/util/`) would do.
- `StudySleepScreen.cpp:488-495`: the widened `Study pick` line still ends with `free heap` and
  `free PSRAM`. The very next line, `logSleepMemory("Study pick")`, prints internal and PSRAM free
  again.
- The line also mixes `ESP.getFreeHeap()` with `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` for the
  two halves. Dropping the heap fields from the `Study pick` format string removes both problems.

### m2: The capture filter re-implements private helpers and part of a rule list that `VerseTextScanner` already has

- `attribute` and `hasToken` (`lib/StudyStore/StudyStore/UnitText.cpp:37-55`) are second copies of
  the anonymous-namespace helpers of the same names in
  `lib/BibleSearch/BibleSearch/VerseTextScanner.cpp:44-62`.
- `skipOf` and `isBlockElement` (`UnitText.cpp:57-76`) are a second statement of "what in NWT markup
  is not verse text". The first is `VerseTextScanner.cpp:24-30` (`SKIPPED_CLASSES`,
  `SKIPPED_ELEMENTS`, `BLOCK_ELEMENTS`).
- The lists already differ:
  - The filter keeps `sw`, `w_navigation` and `header`.
  - The filter's block list has no `br`, `blockquote` or table cells.
- Some of that looks deliberate: spec A2 wants a selection that starts in a superscription to keep
  it. None of it is recorded where a future editor of either list would see it.

Not a blocker: the spec (A1, A5) placed the filter in `UnitTextScanner` on purpose, and the two
libraries do not depend on each other. Worth one comment above `skipOf` naming
`VerseTextScanner.cpp:27-30` and why the lists differ. Otherwise the next NWT markup fix lands in one
list and not the other.

### m3: A flag argument whose only job is to make `offer` do nothing

- `Sampler::offer(..., const bool fits)` opens with `if (!fits) return;`
  (`src/activities/boot_sleep/StudySleepPick.h:108-109`).
- The one production caller already knows `fits` (`StudySleepScreen.cpp:230, 252`). The test caller
  always passes `true` (`StudySleepPickTest.cpp:112, 165`).
- Calling `offer` only when the passage fits keeps `Sampler` a plain reservoir sampler, and the
  "fit is decided before sampling" rule stays where it is decided.
- As written, the new parameter widens every call site to say nothing.

### m4: The prefilter is checked twice on the same path

`offerRow` computes `underPrefilter` (`StudySleepScreen.cpp:229`) and then calls `fitsAtAll`, which
begins by checking `withinPrefilter` again (`:205-206`). Keep the check in one place. The simplest fix
is for `fitsAtAll` to assume its caller did it.

### m5: `repairTexts` computes `atHint` twice per row

The classification loop (`src/study/StudyStore.cpp:449-452`) and the main loop (`:461-463`) repeat
the same `documentSpine < indexedDocumentCount() && documentOffsetOf(unitsFor(...), start)`
expression. Each evaluation reads the hint's unit index entry, so every row pays for it twice.

Carrying the result into the second loop fixes it: two ordered ranges, or a `std::vector<std::pair<size_t, bool>>`. That removes the duplicated condition and the repeat lookups.

### m6: Two comments narrate this PR's test plan rather than the code

- `StudySleepScreen.cpp:478`: "Device check 6: what the floor rung really holds…"
- `src/study/StudyStore.cpp:24`: "Device check for issue #188: internal SRAM must stay flat…"

"Device check 6" points at a numbered list in the PR body, which will not exist next to the merged
code. Per CLAUDE.md ("write them for the merged state"), say what the log line is for, for example
"Logged so the prefilter can be checked against what the floor rung really holds". Or drop the
comments; the log formats already say it.

### m7: `PsramJsonAllocator` names one of its three products, and its class uses a second spelling

- The namespace hands out a JSON allocator, a text allocator, and a bundled
  `PassageDoc::Allocators` (`src/study/PsramJsonAllocator.h:16-22`). Two of the three have nothing to
  do with JSON.
- The implementing class is spelled `SpiramJsonAllocator` (`PsramJsonAllocator.cpp:9`).
- A name such as `PsramAllocators` (file and namespace) would read truthfully at call sites like
  `PsramJsonAllocator::passageDoc()` (`StudyStore.h:178`, `MigrationRunner.cpp:278, 326`).
- This is cosmetic, but it is a new pattern (the PR body calls it out as one), so this is the cheapest
  time to name it.

---

## Checked and not raised

- **`PassageDoc::replace`** is used only by the repair undo. It is documented as that and is tested,
  including out of range (`PassageDocTest.cpp:1103-1114`). It is not dead.
- **`toJson` writing `"h"` only through `isWholeWithText`** has a true "why" comment and a test
  (`NeverWritesTheWholeFlagWithoutText`).
- **`FitGate` / `fitRung` / `Layout`** is a refactor of `drawScreen`'s inline geometry into reusable
  pieces, not a second layout path. The draw path and the gate share `chromeHeight`.
- **`vTaskDelay(1)` per row in `repairTexts`** mirrors `MigrationRunner` and says so.
- **`test/CMakeLists.txt`** only removes the deleted suite. The new tests join existing suites.

## Summary

BLOCKERS: 0. MAJORS: 1 (M1, fixable inline). MINORS: 7.

VERDICT: CLEAR
