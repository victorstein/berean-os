# Font prewarm page slots — stop the leak, size the cap to the demand

**Date:** 2026-09-17
**Status:** Design v1
**Target:** bereanOS, `x4pro` build target (ESP32-S3, 8 MB PSRAM, 800x480 e-ink)
**Delivery:** `origin` (victorstein/berean-os), branch `fix/58-font-prewarm-slots`
**Closes:** #58
**Builds on:** `docs/superpowers/research/2026-09-17-issue-58-research.md`
**Modelled on:** `docs/superpowers/specs/2026-09-16-issue-38-design.md` — the
nearest existing example: a small surgical change whose spec carries
Problem / Goal / Non-goals / Assumptions / Architecture / Data and control flow /
Error handling / Testing strategy / Risks. The test suite is modelled on
`test/pagination/CMakeLists.txt` (real `lib/` sources on the host, `test/stubs`
shadowing the Arduino chain, a link-time fake for a non-virtual class).

## Problem

`FontDecompressor::prewarmCache` refuses to prewarm and logs
`LOG_ERR("FDC", "All %u page buffer slots full, cannot prewarm fontData=%p")`
(`lib/EpdFont/FontDecompressor.cpp:256`) repeatedly during normal use, with the
same pointer recurring.

The research established two independent defects behind that one line.

**1. A caller leaks slots.** Slots are allocated in exactly one place —
`FontDecompressor::prewarmCache`, `pageSlotCount++` at
`lib/EpdFont/FontDecompressor.cpp:375` — and freed in exactly one:
`freePageBuffer` (`:26-33`), reachable only via `clearCache()` (`:21-24`) and
`deinit()` (`:16-19`). `FontCacheManager::PrewarmScope` brackets every reader
render with `clearCache()` in both its constructor
(`lib/GfxRenderer/FontCacheManager.cpp:113`) and its destructor (`:131`), so the
reader's slot count returns to zero every page.

`textsettings::renderPreview` (`src/activities/settings/TextSettingsPreview.cpp:105-111`)
is the only `FontCacheManager::prewarmCache` caller outside a `PrewarmScope`,
and `TextSettingsActivity` calls no cache release anywhere. It prewarms whenever
`PreviewKey` changes — `{fontId, fontPointSize, screenMargin, textWidth,
lineCompression, alignment, extraParagraphSpacing, focusReading, hyphenation}`
(`TextSettingsPreview.cpp:96-104`), i.e. every setting that screen exists to
change. `FontDecompressor` is a process-lifetime global (`src/main.cpp:46`), so
nothing reclaims those slots until the user leaves settings and the reader's next
`PrewarmScope` runs.

Four changes fill the cap; the fifth and every one after is denied. The denied
pointer is always `getReaderFontId()`'s **regular** data, which is why one
pointer repeats. The two logged pointers resolve, at an identical 72-byte link
offset, to `notosans_16_regular` and `notosans_14_regular` — two sizes of one
family, regular only. That is the preview's signature, not the reader's; a
reader-page overflow would have denied `notosans_8_regular`
(`src/main.cpp:117-118`, the status bar font). Research §3.

**2. The cap and its comment describe a world that ended in August.**
`MAX_PAGE_SLOTS = 4  // One per font style (R/B/I/BI)`
(`lib/EpdFont/FontDecompressor.h:10`) was introduced by `0c9e8b3e` (2026-03-22),
when `endScanAndPrewarm` made exactly one `FontCacheManager::prewarmCache` call
for one `scanFontId_`. `a0daab99` (2026-08-17) replaced that with a loop over
`MAX_SCAN_FONTS = 4` per-font scan entries
(`lib/GfxRenderer/FontCacheManager.h:66-76`, `.cpp:118-126`), each carrying its
own style mask, and `FontCacheManager::prewarmCache` expands a mask into up to
four `FontDecompressor::prewarmCache` calls (`.cpp:43-52`). The demand ceiling
went from 4 to **16**. The cap did not move. A slot is one `EpdFontData`, not one
style, and there are 37 built-in ones.

That is a live reader-path defect, not only a documentation one:
`EpubReaderActivity::renderContents` (`:1342-1349`) scans the page body and then
`renderStatusBar()` inside one scope. A page drawing regular, bold, italic *and*
bold-italic consumes all four slots, and the status bar is denied.

**3. Two smaller things the research turned up on the way.**

- `EpdFontFamily::getFont` falls back to `regular` for an absent style
  (`lib/EpdFont/EpdFontFamily.cpp:3-19`). For `ui10FontFamily` / `ui12FontFamily`
  (`src/main.cpp:122,126` — regular and bold only) and `smallFontFamily`
  (`:118` — regular only), `getData()` returns the *same* `EpdFontData` for two
  styles, so `FontCacheManager::prewarmCache` spends a second slot on a duplicate
  that `getBitmap` can never reach: it breaks after the first slot matching
  `fontData` (`lib/EpdFont/FontDecompressor.cpp:173`).
- `FontDecompressor::prewarmCache` returns `-1` for "slots full" (`:257`) but
  `FontCacheManager::prewarmCache` tests `if (missed > 0)` (`:49`). **The sentinel
  is swallowed.** The only evidence that anything went wrong is the FDC log line,
  which carries a raw pointer that took an ELF dump to decode.

## Goal

1. A settings session cannot accumulate page slots. After any number of preview
   re-prewarms, the live slot count attributable to the preview is 1.
2. A single scan pass cannot exhaust the cap. `MAX_SCAN_FONTS × 4` demand fits,
   and the two constants are wired together so they cannot drift apart again.
3. One `EpdFontData` occupies at most one slot.
4. When a prewarm is refused, the log names the font and style, not a pointer.
5. Host tests that fail before the change and pass after, for 1, 2 and 3.

## Non-goals

- **`MAX_SCAN_FONTS` itself.** Four scan entries is a separate sizing question
  (`FontCacheManager.h:66`); a fifth font id already degrades gracefully to the
  per-string prewarm (`.cpp:91-93`).
- **The `~2.8 KB` stack frame in `prewarmCache`.** `uint32_t
  neededGlyphs[MAX_PAGE_GLYPHS]` (512 × 4 = 2048 B, `:262`), `neededGroups[128]`
  (`:334`) and `groupAlignedTracker[128]` (`:395`) together violate CLAUDE.md's
  256-byte local rule against 2–4 KB task stacks. Pre-existing, unrelated to slot
  accounting, and moving those buffers is its own change with its own risk. Noted
  in Risks; this spec adds no stack.
- **The hot-group single-slot design** (`:184-208`) and `MAX_PAGE_GLYPHS`.
- **The SD-card font path.** `SdCardFont` has its own retention discipline
  (`lib/EpdFont/SdCardFont.cpp:116-147`) and consumes no page slots.
- **Measuring the page-turn cost on hardware.** Research §6 gives the mechanism
  and the arithmetic; only the human tester can time it. See Testing strategy.
- **Wiring the new suite into `test/CMakeLists.txt`.** `.claude/agents/ui-dev.md`
  names that file a shared append point: report the line, do not edit it.

## Assumptions

Every behavioural decision in this spec, stated so the review can attack it.

**A1 — The caller is fixed by releasing before prewarming, not by adopting
`PrewarmScope`.** `PrewarmScope` is a scan-then-prewarm-then-draw bracket: its
constructor sets `ScanMode::Scanning` (`FontCacheManager.cpp:112`), which makes
`GfxRenderer::drawText` return early after recording text
(`lib/GfxRenderer/GfxRenderer.cpp:659-662`). `renderPreview` has no scan pass —
it prewarms a constant string and then draws (`TextSettingsPreview.cpp:105-123`)
— so wrapping it in a scope would suppress its own drawing. *Decision:* release
explicitly before the prewarm, reproducing the scope constructor's
`clearCache()`-then-prewarm order without the scan mode. *Attack surface:* a
reviewer may prefer restructuring the preview into a real two-pass render for
consistency with the reader; that is a larger change to a screen this issue is
not about.

**A2 — The release is a new, narrow `FontCacheManager::releaseBuiltinGlyphCache()`,
not the existing `clearCache()`.** `clearCache()` (`FontCacheManager.cpp:15-20`)
also calls `SdCardFont::clearCache()` on every loaded SD font, which runs
`clearOverflow()`, `resetStyleMiniData()` and `applyGlyphMissCallback()`
(`lib/EpdFont/SdCardFont.cpp:1246-1256`). `resetStyleMiniData` normally *keeps*
the loaded data, but it frees outright below `MINI_RETAIN_MIN_FREE_HEAP` and
advances the underuse-hysteresis counter (`:116-147`). `TextSettingsPreview.cpp:90-95`
documents, in its own words, that its key-based cache reuse "relies on nothing
else evicting the SD glyph cache while this activity is up". Using `clearCache()`
would make the preview the thing that evicts it. *Decision:* add

```cpp
void FontCacheManager::releaseBuiltinGlyphCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
}
```

named to parallel the existing `releaseSdFontCaches()` (`:22-27`), which already
establishes "this class exposes cache-release variants". *Attack surface:* one
more public method on a class that already has three release-ish entry points; a
reviewer may argue the preview should just call `clearCache()` and accept the SD
side effects, since it re-prewarms immediately afterwards.

**A3 — `MAX_PAGE_SLOTS` becomes 16, derived from the scan ceiling, and a
`static_assert` welds the two constants together.** `MAX_SCAN_FONTS` (4) ×
four style bits = 16 is the exact upper bound on
`FontDecompressor::prewarmCache` calls per scan pass
(`FontCacheManager.cpp:43-52,118-126`). `PageSlot` is three pointers and a
`uint16_t` (`FontDecompressor.h:57-62`) — 16 bytes padded on this 32-bit target
— so the array grows from 64 to 256 bytes of `.bss` in the process-lifetime
global at `src/main.cpp:46`. Heap is unaffected until a slot is actually filled,
and a filled slot holds only glyphs the page is really drawing. `FontDecompressor`
must not include `FontCacheManager.h` (that inverts `lib/EpdFont` →
`lib/GfxRenderer`), so the assertion lives in `FontCacheManager.cpp`, which
already includes `<FontDecompressor.h>` (`:3`):

```cpp
static_assert(FontCacheManager::MAX_SCAN_FONTS * 4 <= FontDecompressor::MAX_PAGE_SLOTS,
              "one scan pass can request MAX_SCAN_FONTS x 4 styles; the page-slot cap must cover it");
```

`MAX_SCAN_FONTS` is currently `private` (`FontCacheManager.h:66`), so it moves to
the public section for the assertion to name it. *Attack surface:* 16 may be
judged over-provisioned against a realistic mix (reader font 4 styles + status
bar = 5), and 8 would halve the `.bss`. The counter-argument is that 8 is another
guess, and a guess is what produced this bug; 16 makes the refusal unreachable
from the scan path *by construction*, which is what the `static_assert` then
enforces.

**A4 — The cap raise must not land without the caller fix, and this spec ships
both.** Raising the cap alone makes the preview leak bigger — 16 slots of roughly
3 KB each instead of 4 (Spanish preview string,
`lib/I18n/translations/spanish.yaml:94`, ~35 unique glyphs at 16 pt). *Decision:*
the two changes are one commit-series and one PR; the plan phase must not
sequence them so that an intermediate commit has the new cap and the old caller.
*Attack surface:* none expected; recorded because it is the one ordering that
turns this fix into a regression.

**A5 — Deduplication lives in `FontDecompressor::prewarmCache`, keyed on the
`fontData` pointer, and returns "already warm" rather than rebuilding.** If a
slot already holds this `fontData`, return `0` before the cap check. This is
observably identical to today for the duplicate case: `getBitmap` breaks after
the first slot matching `fontData` (`FontDecompressor.cpp:173`), so a second slot
for the same font is already unreachable and its glyphs already fall through to
the hot group. Deduping therefore removes an allocation and changes no rendered
pixel. It also defends every caller, present and future, rather than only the two
that exist. *Attack surface:* a second call with *different* text for the same
font (body text then status bar in the same family, if that ever happens) still
serves the extra glyphs from the hot group instead of extending the slot. That is
today's behaviour, not a new loss — but a reviewer may want the slot extended
instead, which is a materially bigger change to the extraction loop
(`:463-500`).

**A6 — The refusal log moves from `FontDecompressor` to `FontCacheManager`, and
stays at `LOG_ERR`.** `FontDecompressor` knows only a pointer; `FontCacheManager`
knows `fontId` and the style index (`:43-46`). *Decision:* delete the `LOG_ERR`
at `FontDecompressor.cpp:256`, document `-1` as the slots-full sentinel on the
declaration (`FontDecompressor.h:28`), and have `FontCacheManager::prewarmCache`
distinguish it:

```cpp
const int missed = fontDecompressor_->prewarmCache(data, utf8Text);
if (missed < 0) {
  LOG_ERR("FCM", "Page slots full: font %d style %d not prewarmed", fontId, i);
} else if (missed > 0) {
  LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
}
```

This also fixes the swallowed sentinel (Problem §3). It stays `LOG_ERR` rather
than dropping to `LOG_DBG` because after A3's `static_assert` the scan path
cannot reach it, so a firing means a caller has broken an invariant — which is
CLAUDE.md's error pattern 2, "`LOG_ERR` + fallback". The issue's objection ("an
error that fires constantly teaches people to ignore the error channel") is
answered by making it not fire, not by muting it. *Attack surface:* a reviewer
may want the FDC line kept at `LOG_DBG` so a future direct caller of
`FontDecompressor` is not silent; the counter is that one event should produce
one line, and the return value is the contract.

**A7 — `usedPageSlots()` is added as a public accessor.** `uint8_t
usedPageSlots() const { return pageSlotCount; }`. The tests in Goal 1–3 assert on
slot occupancy, and there is no way to observe it today — `Stats`
(`FontDecompressor.h:30-41`) carries byte counts that `resetStats()` wipes
(`:510`), so it is the wrong home for live state. *Attack surface:* a reviewer
may prefer it folded into `Stats` and reported by `logStats` for on-device
diagnosis; that couples a live counter to a resettable struct.

**A8 — The new host suite lives at `test/font_page_slots/` and needs a new
`test/stubs/Arduino.h`.** `FontDecompressor.cpp:3` includes `<Arduino.h>` for
`millis()`/`micros()` only; `test/stubs` currently holds `HalDisplay.h`,
`HalStorage.h` and `Logging.h`. A new `Arduino.h` there is inert for the three
existing consumers (`test/pagination`, `test/launcher_refresh`,
`test/minibidi_arabic`) because none of them include it — `test/pagination`'s own
header comment says the point of the stubs directory is to keep `<Arduino.h>`
out. *Attack surface:* a suite-local stub directory would have zero blast radius;
the counter is that `test/stubs` is the established home and a header nobody
includes cannot break anyone.

**A9 — `test/CMakeLists.txt` is not edited; the line is reported.**
`.claude/agents/ui-dev.md` names it a shared append point for every surface and
requires the exact line go in the PR description for the orchestrator to apply.
The consequence is stated plainly: **until that line lands, CI does not run this
suite**, and the implementer verifies it by configuring the suite locally.
*Attack surface:* a reviewer may judge an unwired test worse than a one-line
merge conflict and ask for it committed. That is an orchestrator call, not mine;
the guide is explicit and I am following it.

**A10 — Nothing changes for the reader's two scoped callers.** Neither
`EpubReaderActivity` (`:1342-1349`, `:389-391`) nor `PassageSelectActivity`
(`:591-593`) is touched. Their behaviour changes only in that a fifth prewarm now
succeeds. *Attack surface:* that is a heap increase on reader pages that
previously got refused — bounded by the glyphs the page is already drawing, but
real. See Risks.

## Architecture

Four files change plus two new test files. No new dependency, no new on-disk
format, no new cross-cutting mechanism.

```
lib/EpdFont/FontDecompressor.h        MAX_PAGE_SLOTS 4 -> 16 (:10) + comment rewrite
                                      second stale "(4) styles" comment at :50-51
                                      usedPageSlots() accessor
                                      -1 sentinel documented on prewarmCache (:28)
lib/EpdFont/FontDecompressor.cpp      dedupe by fontData before the cap check
                                      drop the pointer LOG_ERR
lib/GfxRenderer/FontCacheManager.h    MAX_SCAN_FONTS private -> public
                                      releaseBuiltinGlyphCache()
lib/GfxRenderer/FontCacheManager.cpp  static_assert binding the two constants
                                      releaseBuiltinGlyphCache() body
                                      missed < 0 vs missed > 0
src/activities/settings/
  TextSettingsPreview.cpp             release before prewarm; comment updated

test/stubs/Arduino.h                  new: millis()/micros()
test/font_page_slots/                 new suite (CMakeLists.txt + test + fake)
```

### Ownership after the change

| Concern | Owner |
| --- | --- |
| How many slots exist | `FontDecompressor::MAX_PAGE_SLOTS` |
| How many a scan pass can demand | `FontCacheManager::MAX_SCAN_FONTS × 4` |
| That the first covers the second | `static_assert` in `FontCacheManager.cpp` |
| One slot per `EpdFontData` | `FontDecompressor::prewarmCache` dedupe |
| Releasing slots around a render | `FontCacheManager::PrewarmScope` (unchanged) |
| Releasing slots outside a render | `FontCacheManager::releaseBuiltinGlyphCache()` |

The invariant the whole change rests on, stated once: **a page slot is owned by
whoever created it, and every creator must have a release.** Today
`PrewarmScope` has one and `renderPreview` does not.

## Data and control flow

### Reader page render — unchanged shape, one more slot available

```
PrewarmScope ctor          clearCache()            -> usedPageSlots() == 0
page->render(...)          drawText records text   (GfxRenderer.cpp:659-662)
renderStatusBar()          drawText records text
endScanAndPrewarm()        per scan entry:
  FCM::prewarmCache          per set style bit:
    FD::prewarmCache           slot already holds this fontData? -> return 0   [NEW]
                               pageSlotCount >= 16 ? -> return -1              [was 4]
                               allocate, fill, pageSlotCount++
real draw pass             getBitmap() hits the slots
PrewarmScope dtor          clearCache()            -> usedPageSlots() == 0
```

Before: reader font at four styles took all four slots and `SMALL_FONT_ID` was
refused. After: five slots of sixteen, status bar served from its own slot.

### Text settings preview — the leak closed

```
TextSettingsActivity::render()                      (TextSettingsActivity.cpp:275)
  renderPreview(...)
    if (key != layout.key) {
      fcm->releaseBuiltinGlyphCache();   [NEW]      -> usedPageSlots() == 0
      fcm->prewarmCache(fontId, sample, mask);      -> 1 slot (or 2 with focus reading)
      relayout(...); layout.key = key;
    }
    draw the sample twice                           (TextSettingsPreview.cpp:116-123)
```

Before: slot 1, 2, 3, 4, then refusal for every subsequent change, with up to
~12 KB of internal SRAM held by three dead fonts. After: the live count returns
to 0 and rises to exactly the mask's width on every key change, whatever the
user does on that screen and for however long.

The `if (key != layout.key)` guard is untouched, so a redraw that changes no
setting still does no work — release included.

### The `-1` path

```
FD::prewarmCache  -> -1  (slots full; no slot consumed, nothing allocated)
FCM::prewarmCache -> LOG_ERR with fontId + style; loop continues to the next style
GfxRenderer       -> getBitmap() falls through to the hot group (:176-220)
```

No caller of `FontCacheManager::prewarmCache` inspects a return value — it is
`void` (`FontCacheManager.h:24`) — so the recovery is entirely inside
`getBitmap`, as today.

## Error handling

Per CLAUDE.md's four patterns.

| Condition | Handling | Pattern |
| --- | --- | --- |
| Slot already holds this `fontData` | return `0`, no log — it is a success | — |
| `pageSlotCount >= MAX_PAGE_SLOTS` | return `-1`; `FCM` logs `LOG_ERR` with font id + style; glyphs fall to the hot group | 2 (`LOG_ERR` + fallback) |
| `malloc` failure for buffer or lookup table | unchanged: `LOG_ERR("FDC", "Failed to allocate page buffer…")`, free both, `slot = {}`, return `glyphCount` (`:363-369`) | 2 |
| Per-group temp buffer OOM or inflate failure | unchanged: `free(tempBuf)`, `missed++`, continue (`:471-485`) | 2 |
| `fontDecompressor_` null in `releaseBuiltinGlyphCache()` | no-op, no log — mirrors `clearCache()` (`:16`) and `releaseSdFontCaches()` (`:23`) | — |
| `getFontCacheManager()` null in `renderPreview` | already guarded (`TextSettingsPreview.cpp:106`); the release goes inside the same `if` | — |

No new failure mode is introduced. Nothing here allocates, so nothing here can
fail: the release frees, the dedupe is a pointer comparison over at most 16
entries, and the `static_assert` is compile-time.

Two invariants worth stating because the tests assert them:

- `usedPageSlots()` never exceeds `MAX_PAGE_SLOTS`, on every path.
- No exit from `prewarmCache` increments `pageSlotCount` without both
  allocations having succeeded (already true — research §2 — and preserved: the
  dedupe returns before any allocation).

## Testing strategy

### What the host suite proves

New suite `test/font_page_slots/`, modelled on `test/pagination/CMakeLists.txt`:
real `lib/` sources compiled unmodified, `${REPO_ROOT}/test/stubs` first on the
include path so `<Logging.h>` and `<Arduino.h>` resolve to no-ops, and a
link-time fake for the non-virtual class the unit under test references.

Sources: `FontPageSlotsTest.cpp`, `SdCardFontFake.cpp`, plus
`lib/GfxRenderer/FontCacheManager.cpp`, `lib/EpdFont/FontDecompressor.cpp`,
`lib/EpdFont/EpdFont.cpp`, `lib/EpdFont/EpdFontFamily.cpp`,
`lib/InflateReader/InflateReader.cpp`, `lib/uzlib/src/tinflate.c`,
`lib/Utf8/Utf8.cpp`. All are Arduino-free except `FontDecompressor.cpp`, which
A8's stub covers; `uzlib` is vendored in-repo at `lib/uzlib` (not a `lib_deps`
download), so the suite needs no network beyond the existing GoogleTest fetch.

`SdCardFontFake.cpp` supplies bodies for the five `SdCardFont` methods
`FontCacheManager.cpp` references — `clearCache()` (`:18`),
`releaseResidentCaches()` (`:25`), `prewarm(const char*, uint8_t, bool, bool)`
(`:33`), `logStats(const char*)` (`:58`), `resetStats()` (`:64`). The tests pass
an empty `std::map<int, SdCardFont*>`, so no body ever runs; they exist because
the calls are non-virtual and must link — the same reason
`test/pagination/GfxRendererFake.cpp` exists, and with the same benefit: a new
`SdCardFont` call from `FontCacheManager` breaks this link instead of silently
diverging.

**Fixtures.** `lib/EpdFont/builtinFonts/notosans_8_regular.h` is a self-contained
`static const EpdFontData` with real deflate-compressed groups and no includes
beyond `EpdFontData.h`. Distinct-pointer cases use an array of copies of that
struct: identical content, N distinct addresses, which is exactly what
pointer-keyed dedupe and the cap need, without dragging seventeen 270 KB headers
into one translation unit.

| Test | Asserts | Red today because |
| --- | --- | --- |
| `OneSlotPerDistinctFontData` | two `prewarmCache` calls with the same `fontData` leave `usedPageSlots() == 1` | today it is 2 |
| `StyleFallbackDoesNotSpendTwoSlots` | `EpdFontFamily(regular, bold)` at mask `0x0F` leaves 2 slots, not 4 | `getFont()` returns `regular` twice and `bold` twice (`EpdFontFamily.cpp:3-19`) |
| `PreviewLoopDoesNotAccumulate` | five scope-less prewarms of five distinct fonts, each preceded by `releaseBuiltinGlyphCache()`, leave `usedPageSlots() == 1` | `releaseBuiltinGlyphCache()` does not exist; without it the count reaches 4 and the fifth is refused |
| `AScanPassWorthOfDemandFits` | `MAX_SCAN_FONTS × 4` distinct fonts all prewarm; none returns `-1` | cap is 4 |
| `SlotsFullIsVisibleToTheCaller` | one past `MAX_PAGE_SLOTS` returns `< 0`, and `usedPageSlots()` is still `MAX_PAGE_SLOTS` | `-1` is returned today, but is untested and unobservable — this pins A6 |
| `ScopeReleasesEverySlot` | `PrewarmScope` construct/destruct leaves 0 | passes today; a regression guard on the path that already works |
| `CapCoversScanCeiling` | runtime mirror of the `static_assert` | documents A3 where a reader will see it |

TDD order per CLAUDE.md and the repo workflow: each row's test is written and run
**red** before the corresponding change, then run **green**.

### What CI runs, and the gap

`.github/workflows/ci.yml` compiles `x4pro` and runs the format check; the host
suite is driven by `test/CMakeLists.txt`. Per A9 this spec does **not** edit that
file. The PR description must carry, verbatim, for the orchestrator to append in
alphabetical position:

```cmake
add_subdirectory(font_page_slots)
```

Until that lands, the implementer verifies the suite locally by configuring it
directly, and says so in the PR:

```bash
cmake -S test -B /tmp/bos-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/bos-test -j8 && ctest --test-dir /tmp/bos-test
```

with the line applied in the working tree and reverted before committing. The
research baseline to beat is **563 tests, 100% passed, 1.45 s**.

### Firmware gates

```bash
git submodule update --init --recursive   # issue #61: still needed by hand
pio run                                   # baseline: RAM 19.5%, Flash 81.1%
pio check
./bin/clang-format-fix                    # whole tree, as CI does, not -g
```

`pio run` is the gate that catches the `static_assert` and the `MAX_SCAN_FONTS`
visibility change. Expected deltas: `.bss` +192 bytes (A3), flash roughly
neutral.

### What only the human tester can verify — flag these in the PR

1. **The log line stops.** Open Text settings, change size, family, alignment,
   margin, line compression and hyphenation in one visit — six key changes,
   comfortably past the old cap of four — with `LOG_LEVEL=2`. Before: `[ERR]
   [FDC] All 4 page buffer slots full` from the fifth change on. After: nothing.
2. **The heap comes back.** `ESP.getFreeHeap()` on entering Text settings, after
   six changes, and on returning to the reader. Before: roughly 12 KB lower at
   the second reading. After: flat.
3. **The question the issue actually asks — does a denied prewarm cost a visible
   pause?** Research §6 says the mechanism is real but string-dependent: the
   English sample is pure ASCII and re-inflates one group; the Spanish sample
   (`spanish.yaml:94`) alternates ASCII with Latin-1 accented forms, so each
   alternation re-inflates a 9–11 KB group and the sample is drawn twice
   (`TextSettingsPreview.cpp:116-123`) — order of 20–30 inflations per redraw
   where a warm slot needs none. `stats.decompressTimeMs` is already accumulated
   and printed by `logStats` (`FontDecompressor.cpp:512-523`). **I cannot measure
   this; the PR will say so rather than claim a performance win.**
4. **The reader is not slower or heavier.** A4/A10: a page that previously
   refused the status bar's prewarm now allocates one more page buffer. Read the
   `Page render: prewarm=…ms` line (`EpubReaderActivity.cpp:1577`) and
   `ESP.getFreeHeap()` across a few page turns.
5. **No cache invalidation is needed.** Nothing here touches
   `BOOK_CACHE_VERSION` or `SECTION_FILE_VERSION`; `/.crosspoint/` does not need
   deleting.

## Risks

**R1 — Raising the cap raises peak heap during a render.** A page that used to be
refused now allocates. Bounded by the glyphs actually drawn, and the refusal it
replaces was paying for the same glyphs through repeated group inflation instead.
Verified by tester step 4.

**R2 — `MAX_SCAN_FONTS` becomes public.** It stops being an implementation
detail, so a future change to it is a header change. That is the point: the
`static_assert` is what stops the two constants drifting apart again, and drift
is what caused this bug.

**R3 — The dedupe changes what a repeated same-font prewarm means.** A5 argues it
is observably identical because the second slot is already unreachable
(`FontDecompressor.cpp:173`). If that reading of the `break` is wrong, the dedupe
is a behaviour change. `OneSlotPerDistinctFontData` plus a `getBitmap` assertion
on a glyph only the *second* call's text needed pins it either way.

**R4 — The suite is not in CI until the orchestrator appends one line (A9).**
Stated in the PR, not worked around.

**R5 — Pre-existing, untouched: the ~2.8 KB stack frame in `prewarmCache`.**
`neededGlyphs[512]` alone is 2048 bytes (`:262`) against 2–4 KB task stacks. Out
of scope per Non-goals, recorded here because anyone reading this function should
know. Worth its own issue.

## Open questions for the review

1. **A3's 16 versus 8.** Provably sufficient by construction, at 192 extra bytes
   of `.bss`, against a smaller number that covers the realistic mix.
2. **A2's new method versus reusing `clearCache()`.** Is one more release entry
   point on `FontCacheManager` worse than the preview evicting the SD glyph cache
   its own comment says nothing should evict?
3. **A6's single log line.** Is dropping the `FDC` line acceptable, given a future
   direct caller of `FontDecompressor` that ignores the return value would then
   be silent?
4. **A9.** Follow `ui-dev.md` and ship a test CI does not run yet, or commit the
   one-line `test/CMakeLists.txt` append and accept the collision risk?
