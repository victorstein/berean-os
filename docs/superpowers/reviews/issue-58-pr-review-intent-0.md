# PR #65 — intent review pass 0 (issue #58)

**Branch:** `fix/58-font-prewarm-slots` · **PR:** #65 · **Closes:** #58
**Reviewed against:** issue #58,
`docs/superpowers/specs/2026-09-17-issue-58-design.md` (Design v3),
`docs/superpowers/plans/2026-09-17-issue-58-plan.md`
**Stage:** 1 — intent only. Does the PR do what was asked, completely, and
nothing it was not asked to do? Style, micro-optimisation and code quality are
stage 2 and are deliberately not raised here.

**Result:** 0 BLOCKER, 0 MAJOR, 2 MINOR.

---

## What I verified, and how

Everything below was run from the worktree. The tree was clean before and is
clean after, except this file; `test/CMakeLists.txt` was appended to locally to
run the new suite and reverted (`git status --short` shows only this file).

| Gate | Result | PR's claim |
| --- | --- | --- |
| `pio run -e x4pro` | SUCCESS — RAM 19.5% (64,052 B), Flash 81.1% (5,314,786 B) | identical |
| `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` | `No defects found`, PASSED | identical |
| `ctest` with `add_subdirectory(font_page_slots)` appended locally | **571/571 passed**, 0 failed, 0.63 s | identical (563 baseline + 8) |
| `git log origin/main..HEAD -- test/CMakeLists.txt` | empty | identical |

---

## Spec coverage — every requirement, checked against the diff

The spec's Architecture block lists seven modified files and five new ones. The
diff touches exactly those, plus the six design/research/review documents. No
file outside that list is modified.

| Spec item | Where it landed | Status |
| --- | --- | --- |
| **A1** release before prewarm in `renderPreview`, inside the existing `if (key != layout.key)` guard | `src/activities/settings/TextSettingsPreview.cpp:111` | done — release is inside both the key guard and the `fcm` null guard, exactly as specified |
| **A2** new narrow `FontCacheManager::releaseBuiltinGlyphCache()`, SD caches untouched | `lib/GfxRenderer/FontCacheManager.cpp:22-24`, declared `FontCacheManager.h:23` | done — body is `if (fontDecompressor_) fontDecompressor_->clearCache();` and nothing else, so `TextSettingsPreview.cpp:90-95`'s "nothing else evicts the SD glyph cache" invariant holds |
| **A3** `MAX_PAGE_SLOTS` stays 4 | `lib/EpdFont/FontDecompressor.h:17` (comment `:9-16`) | done — value unchanged, as the spec-review pass 0 BLOCKER required |
| **A4** rewrite both stale comments | `FontDecompressor.h:9-16` and `:66-68` | done — "(4) styles" is gone from both sites |
| **A5** dedupe by `fontData` before the cap check | `lib/EpdFont/FontDecompressor.cpp:254-260` | done — returns 0 before any allocation, ahead of `pageSlotCount >= MAX_PAGE_SLOTS` |
| **A6** drop the FDC pointer `LOG_ERR`; FCM logs font id + style and distinguishes the sentinel | `FontDecompressor.cpp:265-267` (the `LOG_ERR` formerly at `:256` is gone; the cap check now returns a bare `-1`), `FontCacheManager.cpp:52-62` | done — `missed < 0` → `LOG_ERR("FCM", …fontId, i)`, `missed > 0` → the old `LOG_DBG` |
| **A7** three-value return contract on the declaration | `FontDecompressor.h:36-43` | done — all four `0` paths enumerated, including "already warm does NOT re-scan" |
| **A8** `usedPageSlots()` accessor | `FontDecompressor.h:33-34` | done |
| **A9** `test/stubs/Arduino.h`, `UzlibChecksumStubs.c`, `SdCardFontFake.cpp`, suite `CMakeLists.txt` | `test/stubs/Arduino.h`, `test/font_page_slots/*` | done — and the stubs are inert as claimed: the suite links and all 571 tests pass |
| **A10** do not edit `test/CMakeLists.txt`; report the line | file untouched (verified by `git log`); line in the PR body | done — see MINOR 2 |
| **A11** fix the inaccurate scan-entry overflow comment | `FontCacheManager.cpp:102-106` | done, comment only |
| **A12** `TextSettingsActivity::onExit()` releases the last generation | `TextSettingsActivity.h:27`, `.cpp:265-270` | done |

### Goals

1. **"A settings session cannot accumulate page slots … and it is zero once the
   screen closes."** Both halves are present. I traced the lifecycle to confirm
   the second half actually fires: `Activity::finish()` →
   `ActivityManager::popActivity()` → the `PendingAction::Pop` branch →
   `ActivityManager::exitActivity(lock)` (`src/activities/ActivityManager.cpp:178-184`)
   calls `currentActivity->onExit()`. The `Replace`/`goHome` path also calls
   `onExit()` on the current activity and on every stacked one
   (`ActivityManager.cpp:147-152`). `TextSettingsActivity` is reached through
   `startActivityForResult` from both `EpubReaderActivity.cpp:772` and
   `SettingsActivity.cpp:334`, so both entry points exit through that path. The
   call also runs under the `RenderLock` the pop branch takes at
   `ActivityManager.cpp:96`, so it cannot race the render task.
2. **Comments state what a slot is.** Both sites rewritten; the "only compressed
   reading families take a slot" clause — the fact v1 got wrong — is in the
   constant's comment where the next person will look.
3. **Refusal names font and style, and the caller observes it.** Done at
   `FontCacheManager.cpp:52-62`.
4. **`prewarmCache` idempotent per `fontData`.** Done.
5. **Host tests red before, green after.** See below.

### Independent checks of the load-bearing claims

- **`renderPreview` really is the only scope-less `prewarmCache` caller.**
  `grep` over `src/` and `lib/` finds three `createPrewarmScope()` sites
  (`EpubReaderActivity.cpp:389,1343`, `PassageSelectActivity.cpp:591`) and
  exactly one bare `fcm->prewarmCache(...)` — `TextSettingsPreview.cpp:112`.
  Nothing else in the tree calls it.
- **The dedupe is behaviour-neutral.** The one way it could change behaviour is
  two `fontId`s resolving to the same `EpdFontData` inside one scan pass with
  different recorded text. `recordText` keys scan entries on `fontId`
  (`FontCacheManager.cpp:85-87`), so one entry per id; and the eleven
  `insertFont` registrations (`src/main.cpp:316-329`) each hand over a distinct
  family. Unreachable, as A5 says.
- **"A second compressed family cannot join a reader scan pass."** The spec rests
  A3 on this. `setFallbackFont` has exactly one call site,
  `src/SdCardFontSystem.cpp:166`, and its second argument is `sdFontId`.
  Confirmed.
- **`test/stubs/Arduino.h` cannot affect existing suites.** Adding a header to a
  shared stub directory is the one place this PR could have reached beyond its
  blast radius. It cannot: before this PR no host `Arduino.h` existed anywhere on
  those include paths, so any existing suite that included it would already have
  failed to compile. The full 571-test run confirms no behaviour change in the
  563 pre-existing tests. `test/stubs` is also not on `.claude/agents/ui-dev.md`'s
  shared-file list (`ui-dev.md:22-24` names `test/CMakeLists.txt`,
  `lib/I18n/translations/*.yaml` and `src/main.cpp`) — none of which this PR
  touches.

### Tests — behaviour, not restatement

Eight cases, matching the spec's seven-row table plus `TheCapCoversOneFamilysFourStyles`,
which is not spec drift: it is `plan:591-604`, Step 4. Fixtures are real
generated headers (`notoserif_12_regular` compressed, `notosans_8_regular`
uncompressed), which is the spec-review pass 0 BLOCKER 2 fix, so the suite cannot
pass vacuously — `AlreadyWarmDoesNotReallocate` asserts
`pageBufferBytes > 0` before comparing (`FontPageSlotsTest.cpp:86`).

The three red-before claims hold on inspection:
`OneSlotPerDistinctFontData` and `AlreadyWarmDoesNotReallocate` both require the
new dedupe loop (without it the second call falls through to allocation);
`StyleFallbackCollapsesToOneSlot` drives mask `0x0F` through a regular-only
family, which without the dedupe yields four slots for one pointer; and
`PreviewLoopDoesNotAccumulate` cannot compile without `releaseBuiltinGlyphCache()`.
The commits label the other cases as guards rather than implying they caught
something, which is what the plan asked for.

### Scope — nothing added that was not asked for

The only changes outside `FontDecompressor` / `FontCacheManager` /
`TextSettings*` / `test/` are comments. `MAX_PAGE_SLOTS` is unchanged, no
constant changes value, no format version moves, no allocation is added, and no
new dependency appears. The spec-review pass 0 BLOCKER that reversed the
`MAX_PAGE_SLOTS` raise is respected.

### The issue's open question

Issue #58's second ask — "whether a failed prewarm costs a visible pause on a
page turn, which would reframe this from noise to a performance bug" — is *not*
answered, and the PR says so in two places ("I am not claiming a performance
win", device-verification item 3). That is the honest outcome: it needs a stopwatch
on hardware, `stats.decompressTimeMs` is already plumbed for the tester, and the
spec put it in Non-goals before the plan was written. Recorded here so the human
knows it is still open, not raised as a finding.

---

## Findings

### MINOR 1 — `AlreadyWarmDoesNotReallocate` drops the return-value half of its spec row

`test/font_page_slots/FontPageSlotsTest.cpp:88` calls
`decompressor.prewarmCache(&notoserif_12_regular, SAMPLE);` and discards the
result. The spec's test table says this case asserts the second call "leaves
`stats.pageBufferBytes` unchanged **and returns `0`**"
(`2026-09-17-issue-58-design.md`, Testing strategy table, row 2).

Low impact: the `0` return on a repeat prewarm *is* asserted, one test earlier,
at `FontPageSlotsTest.cpp:77` (`OneSlotPerDistinctFontData`). So A7's contract is
covered; only this row is narrower than its spec text. Fix inline by adding
`EXPECT_EQ(...)` around the line, or leave it — the coverage exists either way.

### MINOR 2 — `TheCapCoversOneFamilysFourStyles`'s runtime assertion is redundant with its own `static_assert`

`FontPageSlotsTest.cpp:142-143` pairs
`static_assert(FontDecompressor::MAX_PAGE_SLOTS >= 4, …)` with
`EXPECT_GE(FontDecompressor::MAX_PAGE_SLOTS, 4);`. The `static_assert` already
fails the build if the constant drops, so the `EXPECT_GE` can never fire. The
case is really a documented compile-time guard wearing a test's clothes.

This is plan-sanctioned (`plan:591-604`) and the commit calls it a guard, so it
is not drift — it is noted only because "tests that exercise behaviour rather
than restating the implementation" is an explicit check at this stage, and this
is the one case of the eight that restates rather than exercises. The comment
above it carries the real value. No action needed unless the author wants to drop
the `EXPECT_GE`.

---

## Action required from the merger (not a defect)

Per A10 and `.claude/agents/ui-dev.md:22-24`, this PR deliberately does not touch
`test/CMakeLists.txt`. **The eight new tests do not run in CI until this line is
appended at the end of that file** (it is append-ordered, not alphabetical — it
currently ends `bookmark_doc`):

```cmake
add_subdirectory(font_page_slots)
```

I verified locally that with that one line the suite configures, builds and
passes: 571/571.

---

VERDICT: CLEAR
