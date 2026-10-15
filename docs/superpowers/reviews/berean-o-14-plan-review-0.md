Tier: standard

# Plan review 0: berean-o-14 (`lib/Preaching`, the intro section and its question parser)

Reviewed: `docs/superpowers/plans/2026-10-15-berean-o-14-plan.md` against
`docs/superpowers/specs/2026-10-15-berean-o-14-design.md`. I read the plan but did not apply or build it.

## Summary

The plan is sound. I traced the parser in Step 3.5 against every rule in the spec:

- skip depth, collect depth and link depth;
- the A7 → A11 classification order;
- pairing before a title, heading or text, and at the end of the document;
- the A12 byte accounting (`bytesInUse` covers committed + pending quote + open buffer; `admit(href)` before a `Verse` commit; title counted once);
- the 64-block bound;
- the end-of-document flush placed before the `overLimit` check (spec-review MAJOR 2);
- literal-format `LOG_ERR`s;
- `take`'s once-only, nothing-partial contract.

Every rule maps to code, and I found no behavioural defect.

The bound tests are not hollow:

- `CountsTitleAndHrefTowardsTheTextLimit` fails if either the title or the href goes uncounted.
- `RefusesWhenTheTrailingUnpairedQuoteCrossesTheBlockLimit` fails if the flush is moved after the check.

The device-warning check (Task 4) has teeth. Two things support that:

- `scripts/repo_warnings.py:30` gives `lib/Preaching` `-Wall`, because only `expat`, `miniz` and `uzlib` are excluded.
- `[env:x4pro]` defines `ENABLE_SERIAL_LOG` (`platformio.ini:173`), so the printf-checked `LOG_ERR` (`lib/Logging/Logging.h:42-46`) is the one compiled.

The release env compiles `LOG_ERR` away. The only local that appears inside a log call is `error` (Step 3.5, `feed`), and it is also used outside it, so the release env gets no unused-variable warning.

The file lock is complete. Every file a step touches is under one of these:

- `lib/Preaching/`
- `test/preaching/`
- `test/CMakeLists.txt`
- `src/PreachingBuildCheck.cpp` (temporary)

Both `FILES:` lines (plan lines 7-8) are at column 0, outside any fence. The build logs go to the gitignored `build/`.

No step pushes, opens a PR or runs `hpipe`. Step 5.4 says "Do not push".

There are no BLOCKERs and no MAJORs. The MINORs follow.

## MINOR

### 1. Two of the three skip rules have no test that could fail

- **Claim.** `FigureAddsNothing`, `PageNumberInsideAQuoteParagraphAddsNothing` and the `pageNum` `<h2>` in `KeepsQuestionSixHeadingsInOrder` are presented as tests of the A4 skips (plan lines 381-386).
- **Problem.** None of them can fail if the skip is removed:
  - The `<figure>` fixture (plan lines 478-481) holds only an `<img>`. Its `alt` is an attribute, and text sitting directly in `<figure>`/`<div>` is never collected anyway (A6).
  - Every `pageNum` fixture is an empty span: the number is in `data-before-text` (plan lines 473, 514, 554). The plan's own comment says so (Step 3.5, `isSkipped`: "empty spans carrying their number in an attribute").

  Deleting the `figure` or `pageNum` branch of `isSkipped` (Step 3.5) leaves all 32 tests green. Only the `contextTtl` skip is actually tested, by `TakesTheTitleAndSkipsTheQuestionLabel`.
- **Evidence.**
  - Plan lines 473, 478-481, 514, 554.
  - The `isSkipped` definition in Step 3.5.
- **Fix (inline).** Give each skip a fixture whose output changes if the skip goes:
  - In `FigureAddsNothing`, add `<figcaption><p>Pie</p></figcaption>` inside the `<figure>`.
  - Add one case with a non-empty `pageNum` span inside a quote paragraph, for example `<span class="pageNum">7</span>`, and assert that the quote text has no `7`.

  The real-markup excerpts stay as they are.

### 2. Step 5.1's warning check runs on a build that does not compile the library

- **Claim.** Step 5.1 greps the final `pio run` log for `lib/Preaching` warnings and says "the warning check is what matters" (plan lines 1228, 1235-1237).
- **Problem.** Once `src/PreachingBuildCheck.cpp` is deleted, nothing includes `<Preaching/…>`. The plan says so itself (line 1235), so PlatformIO does not compile the library and the grep cannot find a warning. "no Preaching warnings" in Step 5.1 is therefore vacuous. An implementer or tester reading the hand-off could take it as device-warning evidence for the committed tree.
- **Evidence.**
  - Plan lines 1228-1237.
  - Spec "Device build" (spec lines 435-443).
- **Fix (inline).** In Step 5.1, change the last expectation to say that Step 4.2 is the warning evidence. Step 5.1 only proves the committed tree builds, and its grep is not evidence about `lib/Preaching`. In Step 5.4, report Step 4.2's grep result rather than Step 5.1's.

### 3. The spec asks for a `static_assert` on every follow-up, but the plan asserts three

- **Claim.** The spec asks for `nextQuestion` for every n in 1–20 "as `static_assert`s and as runtime `EXPECT`s" (spec lines 377-378). Spec line 96 also cites compile-time checkability as the reason the function is `constexpr`.
- **Problem.** Step 1.1 has `static_assert`s for only 7, 12 and 20 (plan lines 83-85). The runtime loop covers all 20, so nothing ships wrong. The narrowing is silent, though.
- **Evidence.**
  - Plan lines 83-92.
  - Spec lines 96 and 377-378.
- **Fix (inline).** Either add a `constexpr` helper that checks all 20 against the expected array inside one `static_assert`, or note in the plan that the runtime loop is the full check and the three `static_assert`s are spot checks.

### 4. `buffer.reserve(1024)` is an allocation the spec's memory budget does not list

- **Claim.** The constructor reserves `PARAGRAPH_RESERVE_BYTES = 1024` for `buffer` (Step 3.5, `PARAGRAPH_RESERVE_BYTES` and the constructor).
- **Problem.** The spec's Memory section (spec lines 343-347) lists one State of about 5 KB, mostly `blocks`, plus the expat parser and at most 8 KB of strings.
  - At 1,025 B, this allocation is below the 4,096 B PSRAM threshold, so it lands in internal SRAM, which is the scarcer heap.
  - It is a default-allocator `std::string` reserve, so an OOM aborts instead of returning `valid() == false`.

  It is small and freed with the parser, so the risk is a drift between the spec's budget and the code, not a defect.
- **Evidence.**
  - The `PARAGRAPH_RESERVE_BYTES` constant and the constructor in Step 3.5.
  - Spec lines 337-347.
- **Fix (inline).** Do one of these:
  - Drop the reserve. The longest real paragraph is far below 1 KB, so `std::string` growth costs a few reallocations per document.
  - Keep the reserve, and add one line to the hand-off saying it is about 1 KB of internal SRAM per open parser.

VERDICT: CLEAR
