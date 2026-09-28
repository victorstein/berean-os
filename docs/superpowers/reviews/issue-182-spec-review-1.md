Tier: heavy

# Issue #182 — spec review 1

Reviewed: `docs/superpowers/specs/2026-09-27-issue-182-design.md` (at `f84f2748`), against
`gh issue view 182 --repo victorstein/berean-os`, the research note
`docs/superpowers/research/2026-09-27-issue-182-research.md`, and review 0. Every cited line below was
read in this worktree.

## Were review 0's fixes applied correctly?

- **B1 (long-press anchor): applied correctly.** There are exactly two anchor-setting sites:
  `PassageSelectActivity.cpp:76-79` (long-press) and `:309-313` (`commitAt`). `grep anchorIndex|anchorOffset` finds no
  third site, apart from the `-1` reset in `advancePage` (`:161`). A15's `setAnchor` covers both.
  With `anchorOffset` set, `selectionRange` takes the `anchorIndex >= 0` scan (`:181-190`) or the
  endpoint fallback (`:191-196`), so the saved start, the reference (`:384-385`) and the outline
  (`drawSelectionOutline`, `:488`) all begin at the anchor. The spec also explains why there is no
  host test, and device checks 4–5 cover the change.
- **m1: applied in A2 and in the tests, but not in the data flow.** See m1 below.
- **m2, m3, m4: applied.** The `word…` form is the same in A6 and A11. Both CMake edits are named, and
  `test/study_sleep_pick/CMakeLists.txt:5-8` does list its sources by name. The format-doc section and
  the stale citation are covered, and the stale citation really is `docs/file-formats.md:483-484`.
- **m5: applied.** `getLineHeight` is read at draw time (§6.2), the `offerRow` comment is reworded,
  `"w"` is read in place, and the bound is `< 2800u`.

## Other claims I checked, which hold

- **D4 arithmetic.**
  - The existing assertion keeps the worst case under 1,700 B (`test/passage_doc/PassageDocTest.cpp:603`).
  - A `"w"` of 512 `"` characters adds `,"w":"` (6 B), 1,024 escaped bytes and a closing `"`, which
    is 1,031 B. So the new worst case is at most 2,730 B, which is under 2,800.
  - 200,000 / 2,730 = 73, which is at least 70.
  - For the realistic test, 63 × (476 + 518) ≈ 62.6 KB, which is under 64 KB. The 476 B comes from
    `< 30000u` / 63 at `:274`.
- **A2 idempotence.**
  - `utf8SafeSummary` collapses whitespace, strips `'\n'`, trims, and only then cuts
    (`lib/Utf8/Utf8.cpp:185-201`).
  - A cut can leave a trailing space that a second pass would trim. But no cut ever happens here:
    the builder guarantees ≤ 512 B (A6).
  - So the rollback `add(backup)` (`src/study/StudyStore.cpp:304-309`) is byte-stable, as A2 claims.
- **The v1/v2/v3 rule.**
  - `toJson` picks the version per file (`PassageDoc.cpp:190`), and `fromJson` gates only on
    `isKnownFormatVersion` (`:223`, `FormatVersion.h:14-16`).
  - The existing expectations still hold: `v == 2` for a linked file (`PassageDocTest.cpp:413`), and
    `v == 1` for a linkless one (`:165`, `:427`).
  - The sleep gate reads `PassageDoc::FORMAT_VERSION` (`StudySleepScreen.cpp:142`), so it follows the
    bump to 3.
- **The sleep chrome.**
  - Chrome is 307 px: 47 + 51 + 47 + 49 + 113, from `StudySleepScreen.cpp:296-300`, `:236-238`, and
    the line heights in R§4. `areaBottom` is 699 (`:302-304`), which leaves 392 px for text.
  - `drawCenteredText` centres using `getTextWidth` (`GfxRenderer.cpp:632-633`), and
    `getTextWidth` reads glyph metrics only (`:600-627`). So measuring and drawing agree, as the
    spec says.
- **The `Candidate` growth.** 513 − 121 = 392 B per buffer, in the heap-allocated `Sampler`
  (`StudySleepScreen.cpp:348`, `StudySleepPick.h:84-90,136-137`).
- **The other `"x"` readers are unaffected.** They are listed under Non-goals, and I confirmed them
  by grep: `HighlightsActivity.cpp:105,109`, `PassageDoc.cpp:144`, `MigrationRunner.cpp:304` and
  `MigrationPlanner.cpp:37`. No other code builds a `TaggedPassage` and re-adds it without copying.
  The only re-add is `removePassage`'s rollback, which copies the whole struct.

## MAJOR

### M1 — `displayText` is a resident internal-SRAM cost that the Resources table never counts, and it grows with the number of passages

**Claim.** The Resources table lists the selection builder (≤ 512 B), the sleep `Candidate`s
(+784 B) and the fitter's lines. For the store, the only line is "Per long passage | ≤ ~520 B of raw
JSON | The user's store stays under 64 KB against 200 KB". That is the on-disk size.

**Problem.** The design adds a `std::string displayText` of up to 512 B to every long
`TaggedPassage`, and that string stays in RAM, in internal SRAM, for the whole reading session. The
spec never counts it. There are three costs.

- **Resident.**
  - `StudyStore::openPublication` loads the whole publication's `PassageDoc` into `passages_`
    (`src/study/StudyStore.cpp:49`). It stays there until `closePublication` (`:69-71`).
  - Each `displayText` is its own heap block of up to 512 B plus header. On this build, any block
    under 4,096 B is placed in internal SRAM:
    `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2153-2154` sets
    `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`.
  - R§4 shows that 57% of single verses and nearly all multi-verse passages exceed 120 B. So in
    practice most passages carry a `"w"`.
  - For the user's real store of 63 passages, that is up to about 32 KB more internal SRAM, held for
    as long as the Bible is open.
  - `SAVE_BYTE_BUDGET` does not bound this in any useful way. At about 1 KB per long passage,
    200 KB allows about 190 passages, which is about 100 KB of resident internal SRAM.
- **Transient, on every change.**
  - `add()` (`PassageDoc.cpp:102`), `linkPassages` (`:147`) and every save call `measureBytes()`
    (`:259-263`), and saving serialises the document again.
  - `measureBytes()` builds a full `JsonDocument` through `toJson` (`PassageDoc.cpp:259-263`).
    ArduinoJson 7.4.2 copies each string into its own pool node through `allocator->allocate(size)`
    (`.pio/libdeps/x4pro/ArduinoJson/src/ArduinoJson/Memory/StringNode.hpp:35-41`). It uses the
    default allocator, because no `JsonDocument` in `src/study` or `lib/StudyStore` takes a PSRAM
    allocator (grep).
  - So the resident copy is briefly duplicated at each of those calls. Together with the resident
    cost, that puts about 64 KB more internal SRAM at peak on the real store.
- **Transient, at sleep.**
  - `offerFile` parses each passages file whole into a `JsonDocument`
    (`StudySleepScreen.cpp:135-136`), so every `"w"` is copied into internal SRAM while that file is
    being scanned.

`CLAUDE.md` names internal SRAM as the primary constraint and sets a free-heap floor of ~50 KB. Its
Agent rules also say "justify any new heap allocation". The spec justifies the 512 B builder string
and the 784 B `Candidate` growth, but it leaves out the one cost that grows with the size of the
user's data.

This does not reverse D1 or D2: storing the text at save time is still the right call. But the
design is incomplete until this cost is either shown to be acceptable or moved.

**Evidence.**
- `src/study/StudyStore.cpp:49,69-71`: the lifetime of `passages_`.
- `lib/StudyStore/StudyStore/PassageDoc.cpp:102,147,259-263`: where `measureBytes` is called.
- `sdkconfig:2153-2154`: the 4,096 B internal-SRAM threshold.
- `ArduinoJson/Memory/StringNode.hpp:35-41`: strings are copied into their own nodes.
- `src/study/BibleSearchStore.cpp:97`: an existing PSRAM allocator, which shows that routing storage to
  PSRAM is already a pattern in this repo.

**Fix.**
1. Add Resources rows for:
   - the resident `displayText` cost;
   - the transient `JsonDocument` string copies on add, link and save;
   - the per-file copy at sleep.

   Give figures for the 63-passage store and for a store at the budget limit.
2. Choose one of these, and say why:
   - (a) accept the cost, with a device heap check: free heap with the user's store loaded, after
     adding a long passage, and at sleep; the figure must stay above ~50 KB;
   - (b) keep the `"w"` bytes in PSRAM: a PSRAM-backed `displayText` and a PSRAM `JsonDocument`
     allocator for `PassageDoc`'s load, measure and save, following `BibleSearchStore::psramAllocator`;
   - (c) lower D3's cap. The full layout holds only ~375 B at 12pt (R§4), so 384 B would cut the
     cost by a quarter.
3. Add the chosen heap check to the Device checks.

## MINOR

### m1 — The data flow still shows the normalisation order that review 0 m1 fixed

- **Claim.** The data flow says `PassageDoc::add: snippet=summary(label,120); displayText=label if >120 else ""`
  (spec line 330).
- **Problem.** This is the pre-fix order: it tests the raw label against 120. A2 now requires
  summarising to 512 first, and only then clearing the text when `size() <= 120`. That order is the
  only one under which A4's "≤ 120 is refused" cannot latch a good store. An implementer who works
  from the data flow reintroduces the bug. Review 0's fix was applied to A2 and the tests, but not
  here.
- **Fix.** Change line 330 to
  `displayText = summary(label,512); cleared if size() <= 120`.

### m2 — "Before the swap" must also be after the null checks, or a failed turn feeds the page twice

- **Claim.** Architecture §5 says "`advancePage` feeds the outgoing page's words … **before** the swap
  (`:150-163`)".
- **Problem.** `advancePage` returns false when there is no next page, for example at the end of a
  spine item (`PassageSelectActivity.cpp:138-149`). If the feed is placed "before the swap" but ahead
  of those checks, the failed swipe still appends `anchor..end-of-page`, and `anchorIndex` stays
  `>= 0`. A later finalize on the same page then appends `lo..hi` again, so the stored text repeats
  itself. The cited range `:150-163` is correct, but the prose does not say it.
  `PassageLabelTest` cannot catch this, because it tests only the builder.
- **Fix.** State that the feed happens only once `next` is non-null, just before the `RenderLock`
  swap at `:151`.

### m3 — Leftovers the spec should name

- `StudySleepScreen.cpp:51` `MAX_SNIPPET_LINES` becomes unused once `wrappedText` is dropped. Remove
  it.
- `StudySleepPick.h:19-21`'s comment ("MAX_SNIPPET_BYTES … plus a terminator") must follow the
  `TEXT_CAPACITY` rename.
- The comment at `PassageSelectActivity.cpp:374-376` ("the snippet starts at the top of the page the
  user finished on") describes the behaviour this fix removes. Reword or remove it along with the
  `lo`/`hi` change.

VERDICT: CLEAR
