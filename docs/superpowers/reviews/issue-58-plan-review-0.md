# Plan review pass 0 — issue #58, font prewarm page slots

**Plan:** `docs/superpowers/plans/2026-09-17-issue-58-plan.md`
**Spec:** `docs/superpowers/specs/2026-09-17-issue-58-design.md` (Design v3, CLEAR at pass 1)
**Baseline:** `1e081585` on `fix/58-font-prewarm-slots`. Every "red today" claim was judged
against the committed baseline via `git show HEAD:<path>`, never against the dirty working
tree. The worktree was not modified by this review.

## What I did to check it

`git archive HEAD` into `/tmp/plan-review-58/base`, then the plan's step-1 support files
(`test/stubs/Arduino.h`, `UzlibChecksumStubs.c`, `SdCardFontFake.cpp`) written verbatim and
a probe `main()` that replicates the plan's eight test bodies with printed values instead of
GoogleTest assertions. Compiled against the **baseline** `lib/` sources named in step 1d with
`clang++ -std=c++20 -O2 -Wall -Wextra -pedantic -Wno-missing-field-initializers` plus
`clang -c` for `tinflate.c` and the checksum stubs, then again after applying step 3b's dedupe
and step 5's `releaseBuiltinGlyphCache()`.

**The plan's measured claims are correct, all of them.** Verbatim probe output, baseline:

```
UncompressedFontTakesNoSlot: ret=0 pageBufferBytes=0
OneSlotPerDistinctFontData: r1=0 r2=0 slots=2 bytes1=1430 glyphs1=288 bytes2=2860
ScopeReleasesEverySlot: inScope slots=1 / afterScope slots=0
StyleFallbackCollapsesToOneSlot: slots=4
SlotsFull: prewarm[0..3]=0, slots 1,2,3,4; fifth=-1 slots=4
PreviewLoop(no release): slots after each change 1,2,3,4,4
```

and after steps 3b + 5: `OneSlotPerDistinctFontData slots=1 bytes2=1430`,
`StyleFallbackCollapsesToOneSlot slots=1`, `PreviewLoop(with release) 1,1,1,1,1`.

That confirms, independently: `pageBufferBytes` 1430 doubling to 2860 (plan:497); slot
counts climbing 1,2,3,4 then refusing with `-1` (plan:659); the cap check being
before-increment; `UncompressedFontTakesNoSlot` and `ScopeReleasesEverySlot` green on arrival;
and that the whole source list in step 1d compiles **and links** on the host, including the
`SdCardFont` fake's five signatures and the two uzlib checksum stubs. The
`-Wno-missing-field-initializers` justification (plan:95-101) is also real: exactly 2
warnings without it, 0 with.

Anchor text checked line by line against `HEAD` and correct in every case: step 3b/8a's
`FontDecompressor.cpp:255-257` block, step 5c's `FontCacheManager.cpp:22`, step 8b's `:48-51`,
step 9a's `FontDecompressor.h:10`, 9b's `:25-28`, 9c's `:50-51`, 9d's `FontCacheManager.cpp:91-92`,
step 6a's `TextSettingsPreview.cpp:90-111`, step 7b's insertion point at
`TextSettingsActivity.cpp:260`, step 1f's append point after `add_subdirectory(bookmark_doc)`
(`test/CMakeLists.txt:113`, the line the suite is appended after). `--suppress=unusedFunction` is really at `platformio.ini:24`.

### Spec requirement → step

| Spec | Step | OK |
| --- | --- | --- |
| A1 release-before-acquire in `renderPreview` | 6 | yes |
| A2 `releaseBuiltinGlyphCache()` | 5 | yes |
| A3 `MAX_PAGE_SLOTS` stays 4 | 9a (comment only) | yes |
| A4 new comment text (`:10`, `:50-51`) | 9a, 9c | yes |
| A5 dedupe by `fontData` | 3 | yes |
| A6 log moves to FCM, `missed < 0` | 8 | yes |
| A7 three-value return contract, four `0` paths | 9b | yes |
| A8 `usedPageSlots()` | 2 | yes |
| A9 suite layout, `Arduino.h`, uzlib stubs | 1 | yes |
| A10 do not edit `test/CMakeLists.txt` | 1f, 12 | **see MAJOR 2** |
| A11 fix `FontCacheManager.cpp:91-93` comment | 9d | yes |
| A12 `TextSettingsActivity::onExit` | 7 | **see MAJOR 1** |
| 7 spec tests | 1e, 2a, 3a, 4a, 5a | all 7 present (+1 extra) |
| Firmware gates, PR body, tester steps 1-5 | setup, 10, 11, 13 | yes |

No spec requirement is unmapped, no step invents scope the spec rejected, and the type and
signature of every new symbol (`uint8_t usedPageSlots() const`,
`void releaseBuiltinGlyphCache()`, `void onExit() override`, `const int missed`) is identical
everywhere it appears across steps 2, 5, 7, 8 and the test file.

---

## Findings

### MAJOR 1 — Step 7 does not compile: `FontCacheManager` is an incomplete type in `TextSettingsActivity.cpp`

*Claim.* Step 7b (plan:872) gives the complete text to insert into
`src/activities/settings/TextSettingsActivity.cpp`, and step 7c (plan:897-899) states the gate
as `pio run` → `SUCCESS`.

*Problem.* The inserted body calls `fcm->releaseBuiltinGlyphCache()`, but
`TextSettingsActivity.cpp` does not include `<FontCacheManager.h>` and nothing in its include
chain supplies it. `GfxRenderer.h:14` only forward-declares `class FontCacheManager;` while
`GfxRenderer.h:135` returns `FontCacheManager*`, so this is a member call on an incomplete
type — a hard error. The step asks for no include edit, so an implementer executing it
literally hits a build failure at the one step whose stated expectation is SUCCESS. Step 6
works only because `TextSettingsPreview.cpp:7` already includes the header.

*Evidence.*
- `lib/GfxRenderer/GfxRenderer.h:14` — `class FontCacheManager;`
- `git show HEAD:src/activities/settings/TextSettingsActivity.cpp`, lines 1-19 — `<GfxRenderer.h>`,
  `<I18n.h>`, `"TextSettingsPreview.h"`, `"activities/UiTabListActivity.h"`, … and no
  `<FontCacheManager.h>`.
- `src/activities/settings/TextSettingsPreview.h:1-10` — forward-declares `class GfxRenderer;`,
  pulls no renderer headers, so it cannot supply it transitively.
- Every other caller includes it explicitly: `TextSettingsPreview.cpp:7`,
  `EpubReaderActivity.cpp:8`, `BibleNavigationActivity.cpp:4`, `SleepActivity.cpp:5`.
- The dirty tree corroborates: the previous executor's diff of
  `src/activities/settings/TextSettingsActivity.cpp` adds `+#include <FontCacheManager.h>` at
  line 3 — an edit the plan never asks for. That divergence is the plan's clarity gap, not the
  executor's invention.

*Fix.* Add a step 7a′ before the header edit (and keep the existing `git add` line, which
already names the file):

> In `src/activities/settings/TextSettingsActivity.cpp`, add `#include <FontCacheManager.h>`
> to the angle-bracket include block so lines 1-5 read:
>
> ```cpp
> #include "TextSettingsActivity.h"
>
> #include <FontCacheManager.h>
> #include <GfxRenderer.h>
> #include <I18n.h>
> ```
>
> `GfxRenderer.h:14` only forward-declares the class, so the call below needs the definition.

---

### MAJOR 2 — Step 10 stages the shared `test/CMakeLists.txt`, and step 12's guard cannot detect it

*Claim.* The plan's central process rule (plan:64-69) is: add `add_subdirectory(font_page_slots)`
in step 1, keep it in the working tree, **never stage it**, revert in step 12 — enforced by
"never run `git add -A`, `git add .`, or `git commit -a`". Step 12 (plan:1162-1171) is the
proof: `git status --short` "must print **nothing**".

*Problem.* Step 10 (plan:1124-1127) runs

```bash
git add -u lib src test        # NOT git add -A — test/CMakeLists.txt must stay unstaged
```

`git add -u <pathspec>` stages **every tracked modified file** under that pathspec.
`test/CMakeLists.txt` is tracked and, by the plan's own instruction, modified from step 1f
through step 12. So `git add -u test` stages exactly the file the comment on the same line
promises it will not — the prohibition names `-A`, `.` and `-a` but not the form the plan
itself then uses.

Worse, step 12's proof fails open. If step 10 commits that line, step 12's
`git checkout -- test/CMakeLists.txt` restores the file to a `HEAD` that now *contains* the
line, `git status --short` prints nothing, and the check passes. The step's own remedy text —
"If it shows that file as staged or committed" — describes a state `git status` cannot show
after a commit. The A10 violation ships silently, which is the one outcome the rule exists to
prevent.

*Evidence.* `test/CMakeLists.txt:114` is the appended line in the current tree
(`git diff test/CMakeLists.txt`), the file is tracked (`git status --short` reports it as
` M`, not `??`), and steps 1f (plan:288-295) and 12 (plan:1164) both depend on it staying
modified-but-unstaged for the entire run.

*Fix.* Two edits.

1. Step 10's command becomes path-explicit, never `-u test`:

```bash
git add -u lib src test/font_page_slots test/stubs   # never `-u test`: that stages test/CMakeLists.txt
```

2. Step 12's verification gains a check that survives a bad commit:

```bash
git checkout -- test/CMakeLists.txt
git status --short                                     # must print nothing
git log --oneline origin/main..HEAD -- test/CMakeLists.txt   # must print nothing
```

and the "If it shows that file as staged or committed" sentence moves onto the second command,
which is the one that can actually see a commit.

---

### MINOR 3 — "Steps 2-7 are TDD" is not true of steps 4, 6 or 7

Plan:11-13 states "Steps 2-7 are TDD: the test is written and run **red** first, then the
minimum code, then run **green**." Step 4 is explicitly a guard that passes on arrival
(plan:560, "green on arrival"; plan:607, "Both are guards"), and steps 6 and 7 have no test at
all — the plan itself says so (plan:748, "It is not host-testable"). My probe confirms
`SlotsFullIsVisibleToTheCaller` and `TheCapCoversOneFamilysFourStyles` are green against the
baseline. The genuinely red-first steps are 2, 3 and 5.

*Fix.* Reword plan:11-13 to: "Steps 2, 3 and 5 are TDD: the test is written and run **red**
first, then the minimum code, then run **green**. Step 4 adds guards that pass on arrival, and
steps 6-7 change code no host test can reach; their gate is `pio run` plus the device checks in
step 13."

---

### MINOR 4 — The "Files you will touch" table misattributes `FontDecompressor.h`

Plan:80 lists `lib/EpdFont/FontDecompressor.h` under steps "2, 3, 6, 9". Step 3 edits only
`FontDecompressor.cpp` (plan:539-585) and step 6 edits only `TextSettingsPreview.cpp`
(plan:753-800). The header is touched in steps 2 and 9 only. Every other row in the table
checks out.

*Fix.* Change that cell to `2, 9`.

---

### MINOR 5 — Step 9's line citations are stale by the time step 9 runs

Step 9b says "`lib/EpdFont/FontDecompressor.h` lines 25-28" and 9c "lines 50-51"
(plan:1031, 1056). Those are the `HEAD` positions, and they are right at `HEAD` — but step 2b
inserts four lines at line 24, so by step 9 the return-contract comment is at 29-32 and the
page-slot comment at 54-55 (confirmed against the dirty tree, whose `FontDecompressor.h` hunk
adds exactly four lines). The same applies to step 9d's "`FontCacheManager.cpp` lines 91-92" (plan:1073)
after steps 5 and 8 add nine lines above it. The quoted "Replace this / with this" text is
exact and unique in each case, so a literal implementer still lands correctly; the numbers just
mislead anyone who checks them first.

*Fix.* Drop the line numbers from 9b, 9c and 9d's headings (keep the file paths), or annotate
them "(`HEAD` line numbers; +4 after step 2)".

---

### MINOR 6 — Step 3's commit message re-asserts a claim the spec disproved

Step 3d's message (plan:551-553) says the change "**Also collapses the EpdFontFamily style
fallback**: a family without bold or italic resolves several mask bits to one EpdFontData,
which spent one slot each" — stated as a real waste being recovered. Spec A5 and the pass-0
table's MAJOR 4 row settled the opposite: the only style-incomplete families are the
uncompressed ones, which never reach `FontDecompressor`, so this is "**Not a live scenario**"
and A5 is a guard, not a fix. The plan's own test comment gets this right (plan:478-479, "Not a
state this firmware can reach") — only the commit message drifts. CLAUDE.md's "No unfounded
claims" rule applies to commit messages as much as to the code.

*Fix.* Replace that paragraph with: "The `StyleFallbackCollapsesToOneSlot` case pins the
`FCM → EpdFontFamily → FD` composition, where a style-incomplete family resolves several mask
bits onto one `EpdFontData`. No shipped compressed family is style-incomplete, so this is a
property guard, not a recovered leak."

---

## What I did not verify

`pio run` and `pio check` were not executed — the plan's baseline figures (RAM 19.5%, Flash
81.1% / 5,314,686 B) are unchecked; the only nearby datum is `.pio/build/x4pro/firmware.bin` at
5,315,296 B, which is the previous executor's build *with* the change applied, so it neither
confirms nor contradicts. `ctest`'s 563-test baseline is likewise unchecked (the host suite was
exercised here without GoogleTest, by direct compilation). Nothing in the plan's correctness
depends on those three numbers.

---

BLOCKERS: 0
MAJORS: 2
MINORS: 4

VERDICT: CLEAR
