Tier: standard

# Spec review 0 — berean-o-14 (`lib/Preaching`: intro section and question parser)

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-14-design.md` against the brief
(`hpipe brief --task t1`), the research note
`docs/superpowers/research/2026-10-15-berean-o-14-research.md` and the parent spec
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §1, §4, §9.

## What holds up

I checked these and they are correct:

- **Structure and pairing on the real files.** I extracted the 21 files from `~/Downloads/nwt_S.epub` and ran
  the spec's rules over Q1–Q20 with ElementTree:
  - The spec's skip set, its A6 "only `h1`–`h6`/`p`" rule and the A7 reference test lose no visible
    text. No reference paragraph has non-link text or a `<q>`.
  - Every quote pairs. There is no orphan quote or reference. Q6 gives 20 blocks (10 `Verse`), Q19
    gives 32 with 0 `Verse`, and the most text in any file is 1,902 B, counting href.
  - Heading counts are Q6 `h2`×10, Q17 `h2`×3 and Q19 `h2`×2 + `h3`×8, so A3 is needed.
  - There is no `<br>`, `<em>` or `<li>` in any question file. Only the index has `<ol>`/`<li>`.
  - All 104 `<q>` are `class="scrp"`.
- **The follow-up table** (spec :98-100) matches parent §4 (:195-204) and the brief row for row.
- **Fixture values.** The fixture hrefs are verbatim (`1001061144-split26.xhtml#chapter26_verse14`),
  and the Q6 `<h2>` text is `PROFECÍA`/`CUMPLIMIENTO`.
- **Code citations.** They resolve: `BibleNavScanner.cpp:125,137-138,145`, `VerseTextScanner.h:58,70-71`,
  `VerseTextScanner.cpp:307-336`, `SpineHtmlStream.h:19,40`, `.cpp:14,66`, `UnitIndexCache.cpp:23-41`,
  `expat.h:762-763` (XML_ERROR_ABORTED), `Epub.h:93`, `test/CMakeLists.txt:42-46`, `platformio.ini:38`.
- **Test target wiring.** The planned include list (`lib/Preaching`, `lib/Epub`, `lib/Memory`,
  `lib/expat`, `test/stubs`) is what `<Preaching/…>`, `"Epub/htmlEntities.h"`, `<Memory.h>` and the
  no-op `Logging.h` need. That matches `test/bible_search_scanner/CMakeLists.txt`.

## Findings

### MAJOR 1 — The entity tests will fail as written. A9's mechanism is false for these files.

**Claim.**
- A9 (spec :303) says: "Under `XML_GE=0` … expat sends undeclared entities to the default
  handler."
- Fixtures are "wrapped in a minimal `<html><body>`" (:354-355).
- Tests expect `&nbsp;` → U+00A0 and an unknown `&foo;` kept literally (:393). The chunk-boundary
  fixture carries "an entity" (:395-397).

**Problem.** Expat only sends an undeclared entity to the default handler when the document declares
an external subset. Without one, an undeclared entity is a fatal `XML_ERROR_UNDEFINED_ENTITY`. A
minimal `<html><body>` fixture has no DOCTYPE, so all three expectations fail: `feed` returns false
and `take` returns false.

The chunk-equivalence test (:397) compares chunked output with the one-shot result. Both fail the
same way, so that test can pass while checking nothing.

The real question files have **no DOCTYPE either**:

```
<?xml version="1.0" encoding="utf-8" ?>
<html dir="ltr" …>
```

That is the head of `OEBPS/1001061251.xhtml`. So on the device, a named entity in a question would
refuse the whole question; it would not "pass through as text" as the brief says. Today this costs
nothing: `grep -c '&[a-zA-Z#0-9]*;'` finds zero entities in all 21 files. But the spec states the
opposite as fact, and an implementer who follows the fixture recipe gets red tests and no
explanation.

**Evidence.**
- `lib/expat/xmlparse.c:3037-3039`:
  ```
  if (!dtd->hasParamEntityRefs || dtd->standalone) {
    if (!entity) return XML_ERROR_UNDEFINED_ENTITY;
  ```
  The default-handler path (`:3042-3046`) is reached only in the `else` branch.
  `hasParamEntityRefs` is set by an external subset (`:4781`).
- The repo already documents this. `test/verse_anchors/VerseAnchorsTest.cpp:36-41` says "The
  DOCTYPE is load-bearing -- under XML_GE=0 expat only routes an undeclared entity to the default
  handler once the document declares an external subset". `:50-53` says "No DOCTYPE means an
  undeclared entity is a hard parse error". `test/unit_text/UnitTextTest.cpp:57` uses
  `<!DOCTYPE html SYSTEM "about:legacy-compat">` for the same reason.

**Fix (inline).**
1. Rewrite A9: entities expand only when the file declares an external subset. The NWT question
   files declare none and contain none, so a real entity would make the question refuse cleanly;
   that is accepted, because none occur.
2. Every fixture that contains an entity starts with
   `<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "xhtml11.dtd">`, as `VerseAnchorsTest.cpp:41`
   does. This includes the chunk-boundary fixture.
3. Add one case: an entity with no DOCTYPE gives `feed` false, `take` false and
   `outOfMemory()` false. Nothing partial is kept.
4. In the chunk-boundary test, assert that the one-shot parse succeeds and has the expected blocks
   before comparing, so the comparison cannot pass vacuously.

### MAJOR 2 — A bound tripped by the end-of-document flush is accepted (`feed` returns true)

**Claim.** The error table (:319) says "Over 64 blocks or 8 KB | false, latched | … | `LOG_ERR`
refusal". The brief says that past either bound the parser must "return false with nothing partial".

**Problem.** The `feed` pseudocode checks `overLimit` immediately after `XML_Parse`. It then does
the end-of-document flush:

```
if (isFinal) { flush the pending quote as Text (bounds apply); finished_ = !overLimit; }
return true;
```

That is spec :276. If the flush of a trailing unpaired quote is the 65th block, or the byte that
crosses 8,192, then:

- `overLimit` latches after the check, and `feed` returns **true**;
- `failed_` is not set and the state is not cleared;
- no `LOG_ERR` is written.

`take` still refuses because `finished_` is false, so T3 fails safely. But the contract in the
brief and in the spec's own table is broken. A later `feed` then reaches `XML_Parse` on a finished
parser and logs a misleading "parse failed" (`XML_ERROR_FINISHED`). The spec's bound tests use
65×`<p>x</p>`, which are appended inside `XML_Parse`, so the suite would not catch this.

**Evidence.** Spec :266-277 against :314-321 and the brief's Bounds line.

**Fix (inline).** Do either of these:

- Flush the pending quote in the end-element handler when the root element closes, which is inside
  `XML_Parse`, so the existing `overLimit` branch covers it.
- Or move the flush above the error check in `feed`.

Then add a bound test whose overflowing block is a trailing unpaired quote: 64 `Text` blocks, then
`<p><q class="scrp">x</q></p>`. It expects `feed` false on the final chunk and `take` false.

### MAJOR 3 — Nothing checks the library's warnings. The `LOG_ERR` sketch would warn on the device.

**Claim.** The spec says (:412-416): "the host suite's `-Wall -Wextra -pedantic` is the warning gate
for this code until then". "Then" means until T3 includes the library, because the device build is
unlikely to compile `lib/Preaching`. The brief's acceptance line is "builds into
`pio run -e x4pro` with no new warnings".

**Problem.** The host gate cannot see the logging code at all:

- The host stub defines `LOG_ERR(origin, format, ...)` as nothing (`test/stubs/Logging.h:6`), so
  its format string and arguments are never even parsed.
- On the device, `LOG_ERR` expands to `logPrintf("ERR", origin, format "\n", ##__VA_ARGS__)` with
  `__attribute__((format(printf, 3, 4)))` (`lib/Logging/Logging.h:42,46`). `-Wall`, and with it
  `-Wformat`, reaches every repo `lib/` (research §4.6).

The spec's own sketch (:270-271) is a single `LOG_ERR` with a ternary format: `"…%zu…%zu"` or
`"…%s"`, followed by one argument list. Under that macro:

- `"\n"` concatenates onto the second arm only;
- GCC checks both arms against the one argument list, so one of them warns.

Taken literally, the first device build to compile it is T3's. T3 would get new warnings in T1's
code, which neither suite can attribute. The host compiler is Apple clang; the device compiler is
xtensa GCC in `gnu++2a`. So the host gate also misses any GCC-only diagnostic.

**Evidence.** As cited above. Research §4.5 confirms that `platformio.ini` has no `lib_ldf_mode`
(`grep -n lib_ldf platformio.ini` finds nothing), so LDF chain mode builds `lib/Preaching` only when
`src/` includes it.

**Fix (inline).**
1. Specify two `LOG_ERR` calls, each with a literal format and its own arguments.
2. Make device compilation an implement-phase step. Add a temporary, **uncommitted**
   `#include <Preaching/QuestionParser.h>` plus one call to `preaching::parseQuestion` in a `src/`
   translation unit. Run `pio run -e x4pro`, check that the log compiles `lib/Preaching` with no
   warnings under it, then revert.
3. Record the result in the PR hand-off. The acceptance line is then actually verified, not
   inferred from clang.

### MINOR 1 — The memory section misplaces the 4.9 KB reservation

**Claim.** :331-337 says the `State` is "about 5 KB, mostly the reserved `blocks`", and that "All
of it is freed when T3 destroys the parser."

**Problem.** `take` moves `blocks` into `Question` (:287-288), and the 64-slot reservation (A13,
~4.9 KB on device) goes with it. So the reservation lives as long as T3's screen does, not as long
as the parser.

It is also over the 4,096 B auto-routing threshold (`CLAUDE.md`, `sdkconfig.x4pro:2146-2147`), so
it lands in PSRAM, not in the "internal heap" that parent §9 (:296) assumes. That is harmless,
but T3's memory budget and the parent spec's "under 5 KB of strings" should count the
`Question`'s vector at its reserved capacity.

**Fix.** Change the sentence to: "the result `Question` keeps the 64-slot block vector (~4.9 KB,
PSRAM-routed) plus its strings for the screen's life; the parser frees only the expat parser and
the rest of the `State`."

VERDICT: CLEAR
