Tier: standard

# Plan review 0: berean-o-15 (t2), the preaching topics store and the `bible.json` passes

Plan: `docs/superpowers/plans/2026-10-15-berean-o-15-plan.md`
Spec: `docs/superpowers/specs/2026-10-15-berean-o-15-design.md`
Read against the worktree at `9df4ce45`.

## Summary

The plan is sound. Every spec requirement maps to a step, and nothing is dropped or narrowed:

- §1's path, both static_asserts and the negative `PASSAGES_DIR` assert are in Step 1.1.
- §2's load rules, the latch (including the `tags.json` latch from M1), and all 7 save steps are in
  Steps 2.3 and 3.3. That includes the 200-id `TooLarge` guard from M2, the `overflowed()` check,
  the budget, and the atomic write.
- §3's shared read, row rules, both passes, the release of the parse document before the sort, and
  the `(spine, start)` stable sort are in Steps 4.3 and 5.3.
- Spec tests 1–15 and 1–9 all have a test in the plan.

The file lock is complete. Every file a step touches appears on the three `FILES:` lines
(plan:11-13): `SdPaths.h`, the four `src/study/Preaching*` files, `test/storage_io/CMakeLists.txt`
and the two test files. Step 6.2's log goes to `/tmp`, outside the repo. No step pushes, opens a PR
or runs `hpipe submit`.

I checked the plan's riskier assumptions against the source:

- **The "parse into a local, only assign on success" rule holds**, even though `parseTopics` writes
  `loaded` as it goes (plan:387-394). `loadAdoptingInto` calls `accept` only for `UseLoaded` and
  `PromoteTempAndUseIt`, and a refusal in either one becomes `Failed`
  (`lib/Serialization/PersistableStore.cpp:223-230`; `lib/Serialization/TempAdoption.h:79-92`).
  `Failed` is the only result where `loaded` can be partial, and plan:415 discards it there.
- **Test 15 (M2) is reachable.** `TagPalette::fromJson` has no active-tag cap
  (`lib/StudyStore/StudyStore/TagPalette.cpp:162-191`). The ~3.5 KB `tags.json` is read through
  `Storage.readFile`, well under its cap (`PersistableStore.cpp:124-139`).
- **The row rules match `PassageDoc::fromJson`.** That covers the `u` skip, `t | 0u`, the
  `UINT16_MAX` drop, dedupe and the cap of 8 (`lib/StudyStore/StudyStore/PassageDoc.cpp:17-27,310-327`).
  `rowTags` never yields `UNLABELLED`, which is harmless, because `isActive(UNLABELLED)` is false and
  so it can never be picked.
- **The shared read buffers on the heap, not the stack.** `BufferedFileReader` allocates its 512 B
  with `makeUniqueNoThrow` (`lib/Serialization/BufferedFile.h:76-77`), so `readFiltered`'s locals
  stay small.
- **The `rows` and `started` locals are safe when `LOG_INF` compiles out.** `[[maybe_unused]]`
  covers them (`test/stubs/Logging.h:7`), and `rowsSeen` is used through a reference.

No BLOCKER and no MAJOR. Three MINORs follow.

## MINOR

### m1. Step 6.2's flash delta measures nothing, because nothing calls the new code yet

- **Claim.** Step 6.2 (plan:1235-1236) says to "Record the `Flash:` line from the output, for A11's
  flash delta". The spec (A11, spec:406-410) expects "a few KB", which "the `pio run` size line will
  show".
- **Problem.** T2 adds no call site. The only callers are in T3 (spec:320-333). The Arduino-ESP32
  link uses `--gc-sections`, which drops unreferenced functions, so the filtered `deserializeJson`
  instantiation and both passes are discarded. The recorded delta will be close to zero.
- **Why it matters.** Someone could read that number as "the filtered template costs nothing". The
  plan also never records a baseline `Flash:` line to subtract from.
- **Fix (inline).** In Step 6.2, record the line only as T2's baseline, and add a note that A11's
  real delta has to be measured in T3, once `PreachingActivity` links `countByTag` and
  `passagesWithTag`.

### m2. The Bible-order test cannot tell spine-first from Unit-first

- **Claim.** `ATagsPassagesComeInBibleOrder` (plan:1009-1022) checks the spec's `(spine, start)` key
  (A8, spec:390-396).
- **Problem.** In every fixture row, spine order and book order agree (`v:1` on spine 12, `v:40` on
  spine 40). A comparator that sorts by `Unit` alone passes too. A8 says to key on spine first, and
  explicitly warns against the Unit-first variant, but no test pins that down. Stability on equal
  keys (A8, "two equal keys keep file order") is also untested.
- **Fix (inline).** Add a row whose spine disagrees with its book, for example `"u":"v:2:1:1:0","s":5`.
  Expect it first. Optionally, add two rows with identical `u` and `s` but different `r`, and expect
  them in file order.

### m3. The file-size cap in `readFiltered` has no test

- **Claim.** Spec §3 step 3 (spec:259-260) refuses a file over `PassageDoc::SAVE_BYTE_BUDGET + 4096`
  as `Unreadable`. Plan:897-901 implements the check.
- **Problem.** No test exercises the upper bound. `UnreadableFilesAreUnreadable` (plan:698-710)
  covers only the 0-byte case. This guard is what keeps an oversized file from being parsed into
  PSRAM on the device, so a typo in the comparison (for example `<`) would ship unseen.
- **Fix (inline).** In `UnreadableFilesAreUnreadable`, add a `putFile` of
  `std::string(study::PassageDoc::SAVE_BYTE_BUDGET + 4097, ' ')` and expect `Unreadable`.

VERDICT: CLEAR
