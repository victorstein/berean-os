Tier: standard

# Spec review 0: berean-o-15 (t2), the preaching topics store and the `bible.json` passes

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-15-design.md` at `114bc388`, against the t2 brief
(`hpipe brief --task t2`), the research note `docs/superpowers/research/2026-10-15-berean-o-15-research.md`
and the parent spec `docs/superpowers/specs/2026-10-14-preaching-tool-design.md`.

What I checked and found to hold:

- **The latch premise (A1).** It is correct. `persist::loadRefusedAfter` keeps the previous answer for
  `Unreadable` and `ParseError` (`lib/Serialization/FormatVersion.h:28-31`). `adoptedLoad` maps
  `ReportFailed` to `Failed` (`lib/Serialization/TempAdoption.h:88-90`). So keying on `Failed` is what
  makes a corrupt file latch.
- **The load path.** `loadAdopting` with `readDocFromFileChecked` matches `TagPaletteFile.cpp:18-23`, and
  the `.tmp` rules behave as the spec's table says (`PersistableStore.cpp:170-231`).
- **Row rules.** They match `PassageDoc::fromJson`: `PassageDoc.cpp:311-312` for `u`, `:325-329` and
  `normaliseTags` at `:17-27` for the tags.
- **Test seams.** `failReadsOf`, `failWritesTo` and `putFile` behave as the tests need
  (`test/stubs/HalStorageFake.h:28-38`). `BufferedFile.h` is header-only. The `Logging.h` and `Arduino.h`
  stubs cover `LOG_INF` and `millis()`. So both new `.cpp` files build in `StorageIoTest`.
- **The filter.** ArduinoJson 7.4.2's `DeserializationOption::Filter` takes an array's element filter
  from `[0]` (`build/test/_deps/arduinojson-src/src/ArduinoJson/Deserialization/Filter.hpp:39-45`). The
  parser returns `NoMemory` when an allocation fails (`Json/JsonDeserializer.hpp:177,280,…`).
- **Baseline.** It reproduces: `./build/test/storage_io/StorageIoTest --gtest_brief=1` gives
  `69 tests from 6 test suites ran … PASSED 69 tests`.
- **The brief's test cases.** The spec covers every store and pass case the brief lists.

No BLOCKERs. Two MAJORs, both fixable in the spec.

## MAJOR

### M1. A palette the caller gets wrong silently prunes every pick, which re-opens the hole the latch closes

**Claim.** A9 says `save` takes the palette from the caller. The one hand-check left to the caller is
to refuse to save after `TagPaletteFile::load` returns `Failed`.

**Problem.** Save step 2 drops every id that is not `palette.isActive`. An empty palette therefore turns
any selection into `{"v":1,"tags":[]}` and writes it atomically over the owner's picks. The brief's
reason for the latch is that "the picks are never overwritten with an empty list", and that "T5 must be
unable to save over a refused file by accident". The spec closes that path for `preaching.json` and
re-opens it one file over.

The prescribed hand-check also misses the most natural wrong choice:

- `StudyStore::getInstance().palette()` is filled only by `openPublication` (`src/study/StudyStore.cpp:72`).
- `closePublication` never refills it (`:100-109`).
- Predicación opens from Home, and after a boot no publication need have been opened (the spec says so
  itself, A9).
- In that state the palette is empty, and there is no `Failed` to check.

So a T3 or T5 that takes the obvious source of a palette wipes the picks on the first swipe back from
Elegir temas. The research says this is the exact pattern the brief wants removed: "every caller outside
`StudyStore` repeats the same `Failed` check by hand … exactly what the brief wants T5 unable to forget"
(research §2.1).

**Evidence.**

- `TagPalette::isActive` returns false for any id when `entries_` is empty
  (`lib/StudyStore/StudyStore/TagPalette.cpp:67-72`).
- `TagPaletteFile.h:19-21` says that the caller of `TagPaletteFile::load` MUST latch.
- `MigrationRunner.cpp:210-211` is the hand-repeated check.

**Fix.** Make the palette part of the session object, so there is one obvious way to hold both latches:

1. `PreachingTopicsFile::load()` also runs `TagPaletteFile::load` into a member `study::TagPalette`. A
   `Failed` from either file sets `saveRefused_`.
2. Expose `const study::TagPalette& palette() const`, which T3 needs anyway for `activeIdsByName` and
   T5 needs for the chips.
3. `save(picked)` prunes against that member.

Add three tests:

- a corrupt `tags.json` → `save` returns `Refused`, and `preaching.json`'s bytes are unchanged;
- a missing `tags.json` → `load` gives `Empty`;
- the prune test now seeds `tags.json` rather than building the palette by hand.

Update the A9 and Non-goals text to match. Both files are read on the loop task, so A10 is unchanged.
If the author keeps the palette as a parameter, the minimum fix is for `save` to take the palette's
`AdoptedLoad` and refuse on `Failed`. Then document plainly that `StudyStore::palette()` must not be
passed.

### M2. The store can write a list longer than 200 ids, which its own `load` then refuses and latches

**Claim.** Save step 2: "The result is at most 200 ids, because `isActive` bounds it at the palette's
active count." A14: "No pruned list can exceed 2048 B: at most 200 ids". A15: "The store never writes
one, so any longer file was written by something else."

**Problem.** The palette's active count is capped only when a tag is added, never when `tags.json` is
loaded:

- `TagPalette::add` refuses at `MAX_ACTIVE_TAGS` (`TagPalette.cpp:52`).
- `TagPalette::fromJson` accepts any number of active entries (`TagPalette.cpp:162-191`; there is no
  count check in the loop at `:170-185`).

Take a `tags.json` with more than 200 active tags, from a hand edit, an older build or a restored card.
If the owner picks more than 200 of them:

1. `save` writes more than 200 ids, within budget: 201 × 6 B ≈ 1.2 KB.
2. The next `load` refuses the file under the >200 rule and latches.
3. `tags()` comes back empty.
4. Every later session latches again.

The store would have written a file it refuses, so the picks become permanently unsaveable. The spec's
own invariant, that the store never writes a list longer than 200, is false.

**Evidence.** The line references are under the claim and problem above. The load-side refusal is the
spec's own rule (§2, "more than … 200 … elements"; test 5).

**Fix.**

- In `save`, after pruning and dedupe, refuse with `TooLarge` and a `LOG_ERR` when the kept list exceeds
  `TagPalette::MAX_ACTIVE_TAGS`. Refuse rather than truncate, per CLAUDE.md storage rule 2.
  `tags_` stays unchanged.
- Add a test: a palette built through `fromJson` with 201 active tags, all picked → `TooLarge`, and no
  file is written.
- Correct the premises in step 2, A14 and A15. A14's "the refusal path is reachable only with a smaller
  budget" also stops being true, since there is now a second refusal path.

## MINOR

### m1. A8 understates what a stale spine hint can misorder, and the obvious "improvement" is unsafe

**Claim.** A8: "For two passages in the same book, a stale hint after an edition change could misorder
them … `start` breaks the tie."

**Problem.** The spine is the primary sort key, and `bible.json` is shared across renderings:

- `BIBLE_PUB_KEY` excludes language "so marks follow the user between renderings"
  (`lib/StudyStore/StudyStore/PubKey.h:35-37`).
- `documentSpine` is a language-specific hint (`TaggedPassage.h:13-16`), and it is repaired only when
  the passage is reopened (`PassageDoc.h:116-120`).

So with hints from two editions, a misorder can cross books, not only verses within a book. The ordering
the brief asks for, spine then unit, is fine to keep.

Do not let the implementer "fix" this by comparing Verse to Verse by `Unit` and everything else by
spine. That comparator is not transitive. With hints from two editions, Genesis at spine 50, Matthew at
spine 10 and a front-matter paragraph at spine 30 form a cycle, and `std::stable_sort` on a cycle is
undefined behaviour.

**Fix.** Correct A8's rationale to "can misorder across books when the file mixes editions' hints", and
keep the single `(spine, Unit)` key.

### m2. The counts pass holds O(rows), not O(picked tags), and the spec never says so

**Claim.** The brief: the counts pass has "memory bounded by the number of picked tags". Parent spec §9:
"it holds one count per picked tag". The spec's A4 says only that it "costs one short string per row".

**Problem.** A filtered `JsonDocument` still materialises every row's `u` string and `t` array. That is
on the order of tens of KB for a near-cap file. It is in PSRAM only if T3 passes
`PsramJsonAllocator::json()`; ArduinoJson's pool blocks are under the 4,096 B routing threshold.

The research ruled out the in-tree SAX parser, `lib/JsonParser/StreamingJsonParser` (used by
`src/network/PubMediaJson.h:3`), only for the tag-passages pass. For counts, its behaviour of dropping
overflowed long strings (`StreamingJsonParser.cpp`, `emitToken` skips the callback on `tokenOverflow`)
is harmless, and it would meet the brief exactly.

**Fix.** Either state the counts peak and that it departs from the brief and §9, justified because the
brief itself offers the filter, or use the SAX parser for counts. Either way, the device check in
Testing should log PSRAM around the counts pass too, not only around a topic open.

### m3. "No path aborts" is overstated, and so is "internal SRAM should stay flat"

**Claim.** Error handling: "Every allocation is either an ArduinoJson allocation … or a
`PassageText::assign`."

**Problem.** Several allocations go through throwing `operator new`, which aborts under
`-fno-exceptions`:

- `out.reserve(n)` for both vectors;
- `utf8SafeSummary`'s `std::string reference`;
- `unitFromCompact`'s `std::string` argument (`Unit.cpp:59`).

These are the repo's normal practice, so the spec only needs to say so accurately. In addition, 50
`Passage`s at roughly 50–60 B each fall under the 4,096 B PSRAM routing threshold, so `out` and any
reference longer than the SSO limit live in internal SRAM while the topic is held. The device check
"internal SRAM should stay flat" will see a small, legitimate rise.

**Fix.** Reword the claim. Phrase the device expectation as "texts in PSRAM; internal SRAM grows only by
the result vector and references". Have the header comment say that the firmware caller must pass
`PsramJsonAllocator::json()` / `passageDoc()`, since nothing in t2 enforces it.

### m4. The single-task list in `PersistableStore.h` goes stale

**Problem.** A10 puts the loop-task constraint in `PreachingTopicsFile.h`. The authoritative list of files
that rely on it, `PersistableStore.h:106-114`, would then omit `preaching.json`. The research called that
comment "the place to name it" (§2.3).

**Fix.** Keep the header note, and add the one-line comment change to the hand-off notes for the
orchestrator to apply at landing, as data-dev's "report, do not edit" rule does for shared files. Do not
edit the file from t2.

VERDICT: CLEAR
