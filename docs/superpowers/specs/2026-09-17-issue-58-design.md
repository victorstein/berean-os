# Font prewarm page slots — close the leak, tell the truth about the cap

**Date:** 2026-09-17
**Status:** Design v3 (v1 → v2 after a BLOCKER; v2 → v3 after pass 1's MAJOR)
**Target:** bereanOS, `x4pro` build target (ESP32-S3, 8 MB PSRAM, 800x480 e-ink)
**Delivery:** `origin` (victorstein/berean-os), branch `fix/58-font-prewarm-slots`
**Closes:** #58
**Builds on:** `docs/superpowers/research/2026-09-17-issue-58-research.md`
**Reviews:** `issue-58-spec-review-0.md` (BLOCKER, 2/3/5) and
`issue-58-spec-review-1.md` (CLEAR, 0/1/5), both under `docs/superpowers/reviews/`
**Modelled on:** `docs/superpowers/specs/2026-09-16-issue-38-design.md` — the
nearest existing example: a small surgical change whose spec carries
Problem / Goal / Non-goals / Assumptions / Architecture / Data and control flow /
Error handling / Testing strategy / Risks, and which records its review response
in a "Review pass 0 — what changed and why" table. The test suite is modelled on
`test/pagination/CMakeLists.txt` (real `lib/` sources on the host, `test/stubs`
shadowing the Arduino chain, a link-time fake for a non-virtual class).

**v1 shipped a second fix for a bug that cannot occur.** The review disproved it
and this version drops it. The one-line summary of the change in direction: *four
slots was never too few; the comment describing them was wrong, and one caller
never gave them back.*

## Problem

`FontDecompressor::prewarmCache` refuses to prewarm and logs
`LOG_ERR("FDC", "All %u page buffer slots full, cannot prewarm fontData=%p")`
(`lib/EpdFont/FontDecompressor.cpp:256`) repeatedly during normal use, with the
same pointer recurring.

### 1. A caller leaks slots — this is the bug

Slots are allocated in exactly one place — `FontDecompressor::prewarmCache`,
`pageSlotCount++` at `lib/EpdFont/FontDecompressor.cpp:375` — and freed in
exactly one: `freePageBuffer` (`:26-33`), reachable only via `clearCache()`
(`:21-24`) and `deinit()` (`:16-19`). `FontCacheManager::PrewarmScope` brackets
every reader render with `clearCache()` in both its constructor
(`lib/GfxRenderer/FontCacheManager.cpp:113`) and its destructor (`:131`), so the
reader's slot count returns to zero every page.

`textsettings::renderPreview` (`src/activities/settings/TextSettingsPreview.cpp:105-111`)
is the only `FontCacheManager::prewarmCache` caller outside a `PrewarmScope`, and
`TextSettingsActivity` calls no cache release anywhere. It prewarms whenever
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
family, regular only, which is the preview's signature (research §3).

Measured on the host against the real `FontDecompressor.cpp` with the Spanish
preview string (`lib/I18n/translations/spanish.yaml:94`) at 16 pt, one slot is
`pageBufferBytes=3969` plus `pageGlyphsBytes=516` — **4,485 bytes**, 43 unique
glyphs (516 ÷ `sizeof(PageGlyphEntry)` = 12). Four slots is ~17.9 KB; the three
dead ones are ~13.5 KB of internal SRAM held for the duration of a settings
visit.

### 2. The cap is right; its comment is wrong

`MAX_PAGE_SLOTS = 4  // One per font style (R/B/I/BI)`
(`lib/EpdFont/FontDecompressor.h:10`), and again at `:50-51`: "Up to
MAX_PAGE_SLOTS (4) styles can be prewarmed simultaneously." Both were written by
`0c9e8b3e` (2026-03-22), when `endScanAndPrewarm` made exactly one
`FontCacheManager::prewarmCache` call for one `scanFontId_`. `a0daab99`
(2026-08-17) replaced that with a loop over `MAX_SCAN_FONTS = 4` per-font scan
entries (`lib/GfxRenderer/FontCacheManager.h:66-76`, `.cpp:118-126`), each with
its own style mask, and `FontCacheManager::prewarmCache` expands a mask into up
to four `FontDecompressor::prewarmCache` calls (`.cpp:43-52`).

**A slot is one `EpdFontData`, not one style, and only *compressed* fonts take
one.** That second clause is what v1 missed and what makes the cap adequate:

```cpp
// lib/GfxRenderer/FontCacheManager.cpp:47
if (!data || !data->groups) continue;   // uncompressed font: never reaches FontDecompressor
```

Of the 37 built-in font headers in `lib/EpdFont/builtinFonts/`, **32 are grouped
and 5 are not** (`grep -l EpdFontGroup` over the directory; `all.h` is an
aggregator, not a font). The five ungrouped ones are `notosans_8_regular`,
`ubuntu_10_regular`, `ubuntu_10_bold`, `ubuntu_12_regular` and `ubuntu_12_bold` —
which is to say **every font drawn outside the reading area**:

| Family | Members | Compressed? | Takes a slot? |
| --- | --- | --- | --- |
| 8 Noto reading families, 4 styles each (`src/main.cpp:60-112`) | 32 | yes | yes |
| `smallFontFamily` = `notosans_8_regular` (`:117-118`) — the status bar | 1 | **no** | **no** |
| `ui10FontFamily`, `ui12FontFamily` = Ubuntu (`:122,126`) | 4 | **no** | **no** |
| SD-card fonts | n/a | n/a | no — early return at `FontCacheManager.cpp:31-38` |

`notosans_8_regular`'s initializer sets `groups`, `groupCount`, `glyphToGroup` to
`nullptr, 0, nullptr` (`builtinFonts/notosans_8_regular.h:3667-3669`, against the
field order at `lib/EpdFont/EpdFontData.h:185-187`), and `getBitmap` serves it
straight from flash with a pointer add (`FontDecompressor.cpp:147-150`).

So a scan pass's real slot demand is **one compressed family × at most 4 styles =
4**, and `pageSlotCount >= MAX_PAGE_SLOTS` (`:255`) is tested *before* the
increment, so the fourth allocates. The cap has been exactly sufficient since
`a0daab99`, which is why the log contains only the preview's signature and no
`notosans_8_regular` pointer.

### 3. The refusal is undiagnosable, and the caller never sees it

`FontDecompressor::prewarmCache` returns `-1` for "slots full" (`:257`) but
`FontCacheManager::prewarmCache` tests `if (missed > 0)` (`:49`). **The sentinel
is swallowed.** The only evidence that anything went wrong is the FDC line, which
carries a raw pointer — decoding the two in this issue took an ELF symbol dump
(research §3). Issue #58's own complaint, "an error that fires constantly teaches
people to ignore the error channel", is half about volume and half about this:
the line that does fire says nothing actionable.

## Goal

1. A settings session cannot accumulate page slots: after any number of preview
   re-prewarms the live count is exactly one generation (1 slot, or 2 with focus
   reading on), and it is **zero once the screen closes**.
2. `MAX_PAGE_SLOTS` and its two comments state what a slot actually is and what
   assumption makes 4 sufficient, so the next person to add a compressed font to
   a non-reading screen is warned in the place they will look.
3. If the refusal ever fires, it names the font id and the style instead of a
   pointer, and the caller observes it.
4. `FontDecompressor::prewarmCache` is idempotent per `fontData` — a guard on an
   API property, not a fix for an observed waste (A5).
5. Host tests that fail before the change and pass after, for 1 and 4, against a
   fixture that actually exercises the code.

## Non-goals

- **Raising `MAX_PAGE_SLOTS`.** Dropped from v1. Problem §2 shows the demand is 4
  and the cap is 4. See A3.
- **`MAX_SCAN_FONTS`.** A fifth font id in one pass is not batched
  (`FontCacheManager.cpp:91-93`); see A11 for what actually happens then.
- **The `~2.8 KB` stack frame in `prewarmCache`.** `uint32_t
  neededGlyphs[MAX_PAGE_GLYPHS]` (512 × 4 = 2048 B, `:262`), `neededGroups[128]`
  (`:334`) and `groupAlignedTracker[128]` (`:395`) together violate CLAUDE.md's
  256-byte local rule against 2–4 KB task stacks. Pre-existing and unrelated to
  slot accounting. Noted in Risks; this spec adds no stack.
- **The idle prewarm's wasted work for built-in fonts** (R5).
- **The hot-group single-slot design** (`:184-208`) and `MAX_PAGE_GLYPHS`.
- **The SD-card font path.** `SdCardFont` has its own retention discipline
  (`lib/EpdFont/SdCardFont.cpp:116-147`) and consumes no page slots.
- **Measuring the page-turn cost on hardware.** Research §6 gives the mechanism
  and the arithmetic; only the human tester can time it. See Testing strategy.
- **Wiring the new suite into `test/CMakeLists.txt`.** `.claude/agents/ui-dev.md`
  names that file a shared append point: report the line, do not edit it (A9).

## Assumptions

Every behavioural decision in this spec, stated so the review can attack it.

**A1 — The caller is fixed by releasing before prewarming, not by adopting
`PrewarmScope`.** `PrewarmScope`'s constructor sets `ScanMode::Scanning`
(`FontCacheManager.cpp:112`), which makes `GfxRenderer::drawText` return early
after recording text (`lib/GfxRenderer/GfxRenderer.cpp:659-662`). `renderPreview`
has no scan pass — it prewarms a constant string and then draws
(`TextSettingsPreview.cpp:105-123`) — so wrapping it in a scope would suppress
its own drawing. *Decision:* release explicitly before the prewarm, reproducing
the scope constructor's `clearCache()`-then-prewarm order without the scan mode.
*Attack surface:* a reviewer may prefer restructuring the preview into a real
two-pass render for consistency with the reader; that is a larger change to a
screen this issue is not about. *(Unchanged from v1; review pass 0 accepted it.)*

**A2 — The release is a new, narrow `FontCacheManager::releaseBuiltinGlyphCache()`,
not the existing `clearCache()`.** `clearCache()` (`FontCacheManager.cpp:15-20`)
also calls `SdCardFont::clearCache()` on every loaded SD font, which runs
`clearOverflow()`, `resetStyleMiniData()` and `applyGlyphMissCallback()`
(`lib/EpdFont/SdCardFont.cpp:1246-1256`). `resetStyleMiniData` normally *keeps*
the loaded data, but it frees outright below `MINI_RETAIN_MIN_FREE_HEAP` and
advances the underuse-hysteresis counter (`:116-147`). `TextSettingsPreview.cpp:90-95`
documents, in its own words, that its key-based cache reuse "relies on nothing
else evicting the SD glyph cache while this activity is up". Using `clearCache()`
would make the preview that evictor. *Decision:* add

```cpp
void FontCacheManager::releaseBuiltinGlyphCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
}
```

named to parallel the existing `releaseSdFontCaches()` (`:22-27`), which already
establishes "this class exposes cache-release variants". *Attack surface:* one
more public method on a class that already has two release entry points; a
reviewer may argue the preview should just call `clearCache()` and accept the SD
side effects, since it re-prewarms immediately afterwards. *(Unchanged from v1;
review pass 0 accepted it.)*

**A3 — `MAX_PAGE_SLOTS` stays at 4. The comments change; the constant does
not.** This reverses v1 and is the decision the review escalated, so the
reasoning is given in full.

*The evidence:* demand is 4 (Problem §2), cap is 4, and the check is
before-increment (`FontDecompressor.cpp:255`), so all four allocate. There is no
page in this firmware on which a fifth compressed `EpdFontData` is drawn.

*Why not raise it anyway as insurance:* CLAUDE.md requires a mechanism for any
claimed memory or performance gain ("No unfounded claims", "Resource
justification") and puts anything that adds RAM pressure without improving study
out of scope. Raising to 16 costs 192 bytes of `.bss` in a process-lifetime
global (`PageSlot` is three pointers and a `uint16_t`, `FontDecompressor.h:57-62`
— 16 bytes padded) to defend a configuration that does not exist. It would also
raise peak render heap on any page that did hit it, which is the opposite of what
this issue is about. And the regret is asymmetric: if 4 turns out to be wrong,
the correction is one constant; shipping 16 on a false premise leaves a spec
claiming a fix for a non-bug.

*What replaces the enforcement v1 wanted:* v1 proposed
`static_assert(MAX_SCAN_FONTS * 4 <= MAX_PAGE_SLOTS)`. That assertion is *false
arithmetic* — `MAX_SCAN_FONTS × 4` bounds calls into
`FontCacheManager::prewarmCache`, not slots, because uncompressed fonts are
filtered at `FontCacheManager.cpp:47`. The real invariant ("at most one
compressed family is drawn in one scan pass") is not expressible at compile time.
So enforcement moves to two honest mechanisms: a comment that states the
assumption where the constant lives, and A6's log line, which turns the next
occurrence from an ELF-dump investigation into one readable line naming the font.
*Attack surface:* this is the reviewer's escalated scope question, answered
without the human. A reviewer or the orchestrator may still want 16 shipped as
insurance; it is a one-constant change and this spec does not obstruct it. A
reviewer may also argue a comment is not enforcement — correct, and A6 is the
compensating control.

**A4 — New comment text for `MAX_PAGE_SLOTS`.** Concretely, `:10`:

```cpp
// One slot per distinct EpdFontData prewarmed in a scan pass. Four is enough
// because only the 8 compressed reading families take a slot at all: the status
// bar and UI fonts are uncompressed (groups == nullptr) and are filtered out in
// FontCacheManager::prewarmCache, and SD fonts take the SdCardFont path. One
// family x four styles is the ceiling. Drawing a second compressed family on one
// screen breaks that, and FCM logs which font was refused when it does.
static constexpr uint8_t MAX_PAGE_SLOTS = 4;
```

and `:50-51`'s "Up to MAX_PAGE_SLOTS (4) styles" becomes "Up to MAX_PAGE_SLOTS
distinct fonts". *Attack surface:* length — a reviewer may want it cut to two
lines. The counter is that the missing half of this sentence is the entire reason
v1 was wrong.

**A5 — Deduplication lives in `FontDecompressor::prewarmCache`, keyed on the
`fontData` pointer, and is a guard rather than a fix.** If a slot already holds
this `fontData`, return `0` before the cap check. *Status changed from v1:* v1
justified this by `EpdFontFamily::getFont`'s fallback to `regular`
(`lib/EpdFont/EpdFontFamily.cpp:8-18`) collapsing two mask bits onto one pointer
for `ui10`/`ui12`/`small`. Review pass 0 showed those three families are exactly
the uncompressed ones, so they never reach `FontDecompressor` and the waste is
unreachable — every compressed family ships four distinct styles
(`src/main.cpp:60-112`). *Decision:* keep it anyway, as ~6 lines that make the
function idempotent per font. It is provably behaviour-neutral: `getBitmap`
breaks after the first slot matching `fontData` (`FontDecompressor.cpp:173`), so
a second slot for the same font is already unreachable and its glyphs already
fall through to the hot group. *Attack surface:* a reviewer may reasonably ask
for it to be dropped as code with no reachable caller. I would not fight that;
the argument for keeping it is that the observed bug *was* an unreleased slot,
and this is the cheapest way to make a repeated identical prewarm free.

**A6 — The refusal log moves from `FontDecompressor` to `FontCacheManager`, and
stays at `LOG_ERR`.** `FontDecompressor` knows only a pointer; `FontCacheManager`
knows `fontId` and the style index (`:43-46`). *Decision:* delete the `LOG_ERR`
at `FontDecompressor.cpp:256`, document the return contract on the declaration
(`FontDecompressor.h:25-28`), and have `FontCacheManager::prewarmCache`
distinguish the sentinel:

```cpp
const int missed = fontDecompressor_->prewarmCache(data, utf8Text);
if (missed < 0) {
  LOG_ERR("FCM", "Page slots full: font %d style %d not prewarmed", fontId, i);
} else if (missed > 0) {
  LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
}
```

It stays `LOG_ERR` rather than dropping to `LOG_DBG` because after the leak fix
it is unreachable with the current font set (A3), so a firing means either a new
scope-less caller or a second compressed family on one screen — both worth an
error, and both recoverable via the hot group, which is CLAUDE.md's error pattern
2. The issue's objection is answered by making it not fire *and* by making the
one line it prints say something. *Attack surface:* a reviewer may want the FDC
line kept at `LOG_DBG` so a future direct caller of `FontDecompressor` is not
silent; the counter is that one event should produce one line, and the return
value is the contract.

**A7 — The return contract is documented as three values, not two.**
`FontDecompressor.h:27` currently says "the number of glyphs that couldn't be
loaded (0 on full success)". A5 adds a fourth path that returns `0` for "already
warm", which is not a glyph count. *Decision:* the doc comment becomes explicit —
`-1` slots full (nothing allocated); `0` nothing to do — an uncompressed font
(`:252`), empty text or no needed glyph (`:330`) — or already warm, or fully
prewarmed; `>0` glyphs that could not be loaded. It also states that "already
warm" does **not** re-scan the new text, so glyphs the first call did not need
are served from the hot group. The four distinct `0` paths are enumerated
deliberately: pass 0's MINOR 10 objected to `0` reporting a success that is not
one, and three of those four cached nothing. *Attack surface:* a reviewer may want a distinct sentinel (e.g. `-2`) for
"already warm" so the two successes are separable; that buys nothing today since
no caller branches on it.

**A8 — `usedPageSlots()` is added as a public accessor.** `uint8_t
usedPageSlots() const { return pageSlotCount; }`. The tests in Goal 1 and 4
assert on slot occupancy and there is no way to observe it today — `Stats`
(`FontDecompressor.h:30-41`) carries byte counts that `resetStats()` wipes
(`:510`), so it is the wrong home for live state. `platformio.ini:24` includes
`--suppress=unusedFunction`, so `pio check` will not flag it as the one new
symbol with no firmware caller. *Attack surface:* a reviewer may prefer it folded
into `Stats` and reported by `logStats` for on-device diagnosis; that couples a
live counter to a resettable struct.

**A9 — The new host suite lives at `test/font_page_slots/`, needs a new
`test/stubs/Arduino.h`, and needs two uzlib checksum stubs.**
`FontDecompressor.cpp:3` includes `<Arduino.h>` for `millis()`/`micros()` only;
`test/stubs` currently holds `HalDisplay.h`, `HalStorage.h` and `Logging.h`, and
a new `Arduino.h` there is inert for the three existing consumers because none of
them include it. Separately, `lib/uzlib/src` vendors only `tinflate.c` —
upstream's `adler32.c` and `crc32.c` are absent — and `uzlib_uncompress_chksum`
(`tinflate.c:630`) calls `uzlib_adler32` (`:642`) and `uzlib_crc32` (`:646`),
both declared at `uzlib.h:165,167` and defined nowhere in the repo. Nothing in
the firmware calls `uzlib_uncompress_chksum`, and the linker strips it: `nm` on
`.pio/build/x4pro/firmware.elf` finds **zero** occurrences of any of the three
symbols. A host link has no such escape. *Decision:* add
`test/font_page_slots/UzlibChecksumStubs.c` with two bodies returning their
`prev_sum` / `crc` argument, and a comment saying why. Rejected: a
`-Wl,--gc-sections` / `-Wl,-dead_strip` flag pair, which is platform-forked
between the CI runner and a macOS dev box and fails silently on the wrong one.
*Attack surface:* a suite-local `stubs/` directory for `Arduino.h` would have
zero blast radius; the counter is that `test/stubs` is the established home and a
header nobody includes cannot break anyone.

**A10 — `test/CMakeLists.txt` is not edited; the line is reported, and appended
at the end.** `.claude/agents/ui-dev.md` names it a shared append point for every
surface. The file is **not** alphabetical — it runs `streaming_json_parser,
release_json_parser, differential_rounding, …` and ends `launcher_refresh,
bookmark_save_action, bookmark_doc` — so the line goes at the end, which is both
the file's convention and the lowest-conflict position. The consequence is stated
plainly: **CI's `unit-tests` job runs `ctest` (`.github/workflows/ci.yml:168-194`),
so until that line lands the new suite sits beside 563 tests that do run.**
*Attack surface:* a reviewer may judge an unwired test worse than a one-line
merge conflict and ask for it committed. That is an orchestrator call; the guide
is explicit and I am following it.

**A11 — Nothing changes for the reader's three scoped callers.**
`EpubReaderActivity` (`:1342-1349` page render, `:387-393` idle prewarm) and
`PassageSelectActivity` (`:591-593`) are untouched, and — unlike v1's claim —
**their behaviour does not change at all**, because none of them was ever
slot-starved (A3). Two adjacent facts recorded so the plan phase does not
"improve" them: (a) a built-in font that misses the `MAX_SCAN_FONTS` cap gets no
per-string prewarm, because both `GfxRenderer::prewarmFallbackText` overloads
are no-ops for a built-in id (`GfxRenderer.cpp:230-233`; the second overload,
`:252-260`, has no lookup of its own and delegates to the guard at `:264-266`
via `ensureSdGlyphsResident` at `:258`) — it degrades to `getBitmap`'s hot-group path, which is still
graceful but is not what `FontCacheManager.cpp:91-93` says; (b) the idle
prewarm's `PrewarmScope` is destroyed at the closing brace of its own `if` block
(`EpubReaderActivity.cpp:387-393`), so the built-in page slots it just built are
freed immediately by `~PrewarmScope` → `clearCache()` (`FontCacheManager.cpp:128-133`).
What the idle prewarm actually buys is SD mini-glyph retention
(`SdCardFont.h:201-207`). *Decision:* fix the inaccurate source comment at
`FontCacheManager.cpp:91-93` (it is three lines from code this change touches);
leave the idle-prewarm behaviour alone and file it as R5. *Attack surface:* a
reviewer may want (b) fixed here rather than recorded.

**A12 — `TextSettingsActivity` gains an `onExit` that releases the last
generation.** *New in v3, from review pass 1's MAJOR 1.* The class declares only
`onEnter` (`src/activities/settings/TextSettingsActivity.h:26`, defined at
`.cpp:59`) and has no `onExit` override at all, so today nothing reclaims the
preview's final slot when `exitActivity()` deletes the activity
(`src/main.cpp`, the lifecycle in CLAUDE.md). CLAUDE.md's activity rule is
explicit: "Anything allocated in `onEnter()` MUST be freed in `onExit()`."
*Decision:*

```cpp
void TextSettingsActivity::onExit() {
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseBuiltinGlyphCache();
  UiTabListActivity::onExit();
}
```

`UiTabListActivity` does not override `onExit` either, so that call resolves to
`Activity::onExit()` (`src/activities/Activity.h:32`); naming the direct base
mirrors what `onEnter` already does at `TextSettingsActivity.cpp:60`. The
sibling settings screens that do override it — `ClearCacheActivity.cpp:28`,
`ButtonRemapActivity.cpp:34` — are `Activity` subclasses and call
`Activity::onExit()` directly, which is the same pattern one level up.

*Why this is not over-reach:* without it, A1's release-before-acquire leaves one
generation owned by nobody. It is bounded, and several existing transitions
happen to reclaim it — the reader's next `PrewarmScope`
(`EpubReaderActivity.cpp:1342-1349`), `BibleNavigationActivity.cpp:54`,
`EpubReaderChapterSelectionActivity.cpp:33`, and the heap-critical
`releaseSdFontCaches()` calls at `CalibreConnectActivity.cpp:90`,
`CrossPointWebServerActivity.cpp:76` and `SleepActivity.cpp:481`, all of which
route through `FontCacheManager.cpp:23` → `fontDecompressor_->clearCache()`. So
it is not a leak and not a crash risk. It is an allocation with no owner, in a
spec whose entire subject is that defect. *Attack surface:* a reviewer may call
this scope creep on a screen the issue does not name, or prefer the alternative —
weaken the Architecture invariant to "release-before-acquire bounds the preview
at one generation; the last is reclaimed by the next `PrewarmScope`" and ship
nothing here. That is one honest sentence instead of one line of code; I chose
the code because CLAUDE.md states the rule and a `onExit` that exists is cheaper
to keep true than an invariant with a carve-out.

## Architecture

Seven files change plus five new ones — one shared test stub and four in the new
suite. No new dependency, no new on-disk format, no new cross-cutting mechanism,
no constant changes value.

```
lib/EpdFont/FontDecompressor.h        MAX_PAGE_SLOTS comment rewritten (:10)      [A4]
                                      second stale "(4) styles" comment (:50-51)  [A4]
                                      return contract documented (:25-28)         [A7]
                                      usedPageSlots() accessor                    [A8]
lib/EpdFont/FontDecompressor.cpp      dedupe by fontData before the cap check     [A5]
                                      drop the pointer LOG_ERR (:256)             [A6]
lib/GfxRenderer/FontCacheManager.h    releaseBuiltinGlyphCache()                  [A2]
lib/GfxRenderer/FontCacheManager.cpp  releaseBuiltinGlyphCache() body             [A2]
                                      missed < 0 vs missed > 0 (:48-51)           [A6]
                                      fix the inaccurate comment at :91-93        [A11]
src/activities/settings/
  TextSettingsPreview.cpp             release before prewarm; comment updated     [A1]
  TextSettingsActivity.{h,cpp}        onExit() releases the last generation       [A12]

test/stubs/Arduino.h                  new: millis() / micros()                    [A9]
test/font_page_slots/CMakeLists.txt   new suite                                   [A9]
test/font_page_slots/FontPageSlotsTest.cpp
test/font_page_slots/SdCardFontFake.cpp
test/font_page_slots/UzlibChecksumStubs.c                                         [A9]
```

### Ownership after the change

| Concern | Owner |
| --- | --- |
| How many slots exist, and why that is enough | `MAX_PAGE_SLOTS` + its comment (A4) |
| Which fonts can take one | `FontCacheManager.cpp:47` (`!data->groups` filter) |
| One slot per `EpdFontData` | `FontDecompressor::prewarmCache` dedupe (A5) |
| Releasing slots around a render | `FontCacheManager::PrewarmScope` (unchanged) |
| Releasing slots outside a render | `FontCacheManager::releaseBuiltinGlyphCache()` (A2) |
| Releasing the preview's last generation | `TextSettingsActivity::onExit()` (A12) |
| Saying which font was refused | `FontCacheManager::prewarmCache` (A6) |

The invariant the whole change rests on, stated once: **a page slot is owned by
whoever created it, and every creator must have a release.** `PrewarmScope` has
one; the settings preview has none.

Meeting it takes two halves, because `renderPreview` is a free function called
per redraw and cannot own a lifetime:

- **Between redraws** — `renderPreview` releases before it acquires (A1), which
  bounds the preview at one live generation however long the user stays.
- **At screen close** — `TextSettingsActivity` releases in `onExit` (A12), which
  takes that last generation to zero.

Release-before-acquire alone would leave the final generation — the one allocated
on the last setting the user touched — owned by nobody: ~4.5 KB, or ~9 KB with
focus reading on, surviving in the process-lifetime `FontDecompressor`
(`src/main.cpp:46`) until some unrelated screen happens to clear the cache. That
is the same class of defect this spec exists to fix, so it does not ship half
done.

## Data and control flow

### Reader page render — unchanged, and it was already correct

```
PrewarmScope ctor          clearCache()            -> usedPageSlots() == 0
page->render(...)          drawText records text   (GfxRenderer.cpp:659-662)
renderStatusBar()          drawText records text   (SMALL_FONT_ID, uncompressed)
endScanAndPrewarm()        per scan entry:
  FCM::prewarmCache          SD font?        -> SdCardFont path, 0 slots  (:31-38)
                             !data->groups?  -> skipped, 0 slots          (:47)
                             per set style bit:
    FD::prewarmCache           slot already holds this fontData? -> return 0   [NEW A5]
                               pageSlotCount >= 4 ? -> return -1              (unreached)
                               allocate, fill, pageSlotCount++
real draw pass             getBitmap() hits the slots; uncompressed fonts
                           take the flash fast path (:147-150)
PrewarmScope dtor          clearCache()            -> usedPageSlots() == 0
```

Peak is 4 of 4: the reader family's four styles. The status bar contributes zero.

### Text settings preview — the leak closed

```
TextSettingsActivity::render()                      (TextSettingsActivity.cpp:275)
  renderPreview(...)
    label drawn with UI_10_FONT_ID                  (TextSettingsPreview.cpp:74, uncompressed)
    if (key != layout.key) {
      fcm->releaseBuiltinGlyphCache();   [NEW A2]   -> usedPageSlots() == 0
      fcm->prewarmCache(fontId, sample, mask);      -> 1 slot (2 with focus reading)
      relayout(...); layout.key = key;
    }
    draw the sample twice                           (TextSettingsPreview.cpp:116-123)
```

Before: slot 1, 2, 3, 4, then refusal for every subsequent change, with ~13.5 KB
held by three dead fonts. After: the live count returns to 0 and rises to exactly
the mask's width on every key change, whatever the user does on that screen and
for however long.

Leaving the screen:

```
TextSettingsActivity::onExit()      [NEW A12]
  fcm->releaseBuiltinGlyphCache();              -> usedPageSlots() == 0
  UiTabListActivity::onExit();
exitActivity() deletes the activity
```

The `if (key != layout.key)` guard is untouched, so a redraw that changes no
setting still does no work — release included. The release is inside the guard,
which is what keeps the file's documented invariant (`:90-95`) true: after the
branch, the cache holds exactly this font's glyphs.

### The `-1` path

```
FD::prewarmCache  -> -1  (slots full; no slot consumed, nothing allocated)
FCM::prewarmCache -> LOG_ERR naming fontId + style; loop continues to the next style
GfxRenderer       -> getBitmap() falls through to the hot group (:176-220)
```

`FontCacheManager::prewarmCache` is `void` (`FontCacheManager.h:24`), so recovery
stays entirely inside `getBitmap`, as today.

## Error handling

Per CLAUDE.md's four patterns.

| Condition | Handling | Pattern |
| --- | --- | --- |
| Slot already holds this `fontData` | return `0`, no log — it is a success (A5, A7) | — |
| `pageSlotCount >= MAX_PAGE_SLOTS` | return `-1`; `FCM` logs `LOG_ERR` with font id + style; glyphs fall to the hot group | 2 (`LOG_ERR` + fallback) |
| Uncompressed font passed to `FCM::prewarmCache` | skipped at `:47`, no log — correct and frequent | — |
| `malloc` failure for buffer or lookup table | unchanged: `LOG_ERR("FDC", "Failed to allocate page buffer…")`, free both, `slot = {}`, return `glyphCount` (`:363-369`) | 2 |
| Per-group temp buffer OOM or inflate failure | unchanged: `free(tempBuf)`, `missed++`, continue (`:471-485`) | 2 |
| `fontDecompressor_` null in `releaseBuiltinGlyphCache()` | no-op, no log — mirrors `clearCache()` (`:16`) and `releaseSdFontCaches()` (`:23`) | — |
| `getFontCacheManager()` null in `renderPreview` | already guarded (`TextSettingsPreview.cpp:106`); the release goes inside the same `if` | — |
| `getFontCacheManager()` null in `TextSettingsActivity::onExit` | same guard shape; the base `onExit` still runs (A12) | — |

No new failure mode is introduced. Nothing added here allocates: the release
frees, the dedupe is a pointer comparison over at most 4 entries, the accessor
returns a field.

Two invariants the tests assert:

- `usedPageSlots()` never exceeds `MAX_PAGE_SLOTS`, on every path.
- No exit from `prewarmCache` increments `pageSlotCount` without both allocations
  having succeeded (already true — research §2 — and preserved: the dedupe
  returns before any allocation).

## Testing strategy

### What the host suite proves

New suite `test/font_page_slots/`, modelled on `test/pagination/CMakeLists.txt`:
real `lib/` sources compiled unmodified, `${REPO_ROOT}/test/stubs` first on the
include path so `<Logging.h>` and `<Arduino.h>` resolve to no-ops, and a
link-time fake for the non-virtual class the unit under test references.

Sources: `FontPageSlotsTest.cpp`, `SdCardFontFake.cpp`, `UzlibChecksumStubs.c`
(A9), plus `lib/GfxRenderer/FontCacheManager.cpp`,
`lib/EpdFont/FontDecompressor.cpp`, `lib/EpdFont/EpdFont.cpp`,
`lib/EpdFont/EpdFontFamily.cpp`, `lib/InflateReader/InflateReader.cpp`,
`lib/uzlib/src/tinflate.c`, `lib/Utf8/Utf8.cpp`. All are Arduino-free except
`FontDecompressor.cpp`, which A9's stub covers; `SdCardFont.h` needs no stubbing
(it includes only `<cstdint> <deque> <string> <vector>` and the two Epd headers,
`SdCardFont.h:3-9`); `uzlib` is vendored in-repo at `lib/uzlib`, so the suite
needs no network beyond the existing GoogleTest fetch.

`SdCardFontFake.cpp` supplies bodies for the five `SdCardFont` methods
`FontCacheManager.cpp` references — `clearCache()` (`:18`),
`releaseResidentCaches()` (`:25`), `prewarm(const char*, uint8_t, bool, bool)`
(`:33`), `logStats(const char*)` (`:58`), `resetStats()` (`:64`). The tests pass
an empty `std::map<int, SdCardFont*>`, so no body ever runs; they exist because
the calls are non-virtual and must link — the same reason
`test/pagination/GfxRendererFake.cpp` exists, and with the same benefit: a new
`SdCardFont` call from `FontCacheManager` breaks this link instead of silently
diverging.

**Fixture.** `lib/EpdFont/builtinFonts/notoserif_12_regular.h`: a self-contained
`static const EpdFontData` with real DEFLATE groups — `notoserif_12_regularGroups`,
`groupCount = 13`, `glyphToGroup = nullptr` (`:3708-3710`) — no includes beyond
`EpdFontData.h`, and at 270,097 bytes the **smallest** header in the directory.
Distinct-pointer cases use an array of copies of that struct: identical content,
N distinct addresses, which is exactly what pointer-keyed dedupe and the cap need
without pulling several 270 KB headers into one translation unit. Text fixture:
the Spanish preview pangram (`lib/I18n/translations/spanish.yaml:94`), which
spans three groups and so exercises the extraction loop rather than one group.

| Test | Asserts | Red today because |
| --- | --- | --- |
| `OneSlotPerDistinctFontData` | two `prewarmCache` calls with the same `fontData` leave `usedPageSlots() == 1` | today it is 2, and `stats.pageBufferBytes` doubles |
| `AlreadyWarmDoesNotReallocate` | the second call leaves `stats.pageBufferBytes` unchanged and returns `0` | today it allocates a second, unreachable buffer |
| `PreviewLoopDoesNotAccumulate` | five scope-less prewarms of five distinct fonts, each preceded by `releaseBuiltinGlyphCache()`, leave `usedPageSlots() == 1` | `releaseBuiltinGlyphCache()` does not exist; without it the count reaches 4 and the fifth returns `-1` |
| `SlotsFullIsVisibleToTheCaller` | the fifth distinct font returns `< 0` and `usedPageSlots()` is still 4 | `-1` is returned today but untested — this pins **A7** only; see the note below |
| `UncompressedFontTakesNoSlot` | a `groups == nullptr` font leaves `usedPageSlots() == 0` (asserted against `FontDecompressor::prewarmCache`) | passes today — the regression guard on the fact v1 got wrong |
| `ScopeReleasesEverySlot` | `PrewarmScope` construct/destruct leaves 0 | passes today; regression guard on the path that works |
| `StyleFallbackCollapsesToOneSlot` | `EpdFontFamily(regular, nullptr, nullptr, nullptr)` at mask `0x0F` leaves 1 slot, not 4 | today it is 4. **Not a live scenario** — every compressed family ships four styles (A5) — but a real unit test of the `FCM → EpdFontFamily → FD` composition |

TDD order per CLAUDE.md and the repo workflow: each row's test is written and run
**red** before the corresponding change, then run **green**.

**What this suite cannot reach, stated plainly.** It pins the primitives, not the
call sites. A6 — `FontCacheManager::prewarmCache` distinguishing `missed < 0` and
naming the font and style — is unobservable here on both counts: that function
returns `void` (`FontCacheManager.h:24`) and `test/stubs/Logging.h` expands
`LOG_ERR`/`LOG_DBG` to nothing. And `PreviewLoopDoesNotAccumulate` exercises the
release-then-prewarm primitive, **not** `textsettings::renderPreview`, which needs
`GfxRenderer`, `SETTINGS` and `I18N` and is not host-testable. The one-line caller
edit that is the whole fix, and A6's log line, are covered only by tester steps 1
and 2. `UncompressedFontTakesNoSlot`
and `ScopeReleasesEverySlot` are green from the start and are labelled as guards,
not as evidence of a fix.

### What CI runs, and the gap

`.github/workflows/ci.yml` has **five** jobs: `clang-format` (`:33`), `cppcheck`
(`:58`, running `pio check --fail-on-defect low --fail-on-defect medium
--fail-on-defect high` at `:95`), `build` (`:97`), `unit-tests` (`:168`, running
`cmake -S test -B build/test -G Ninja` / `cmake --build` / `ctest --test-dir
build/test --output-on-failure -j` at `:188,191,194`) and `test-status` (`:198`).

Per A10 this spec does **not** edit `test/CMakeLists.txt`. The PR description must
carry, verbatim, for the orchestrator to append **at the end of the file**:

```cmake
add_subdirectory(font_page_slots)
```

Until that lands, `unit-tests` does not run the new suite. The implementer
verifies it locally with the line applied in the working tree and reverted before
committing:

```bash
cmake -S test -B /tmp/bos-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/bos-test -j8 && ctest --test-dir /tmp/bos-test
```

The research baseline to beat is **563 tests, 100% passed, 1.45 s**.

### Firmware gates

```bash
git submodule update --init --recursive     # issue #61: still needed by hand
pio run                                     # baseline: RAM 19.5%, Flash 81.1%
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
./bin/clang-format-fix                      # whole tree, as CI does, not -g
```

**Bare `pio check` exits 0 regardless of defects** — with `check_flags =
--enable=all` (`platformio.ini:24`) that is a fail-open gate that passes locally
and fails in CI. Use the `--fail-on-defect` form above, which is what `ci.yml:95`
runs.

Expected deltas: `.bss` unchanged (no constant changes value), flash roughly
neutral.

### What only the human tester can verify — flag these in the PR

1. **The log line stops.** Open Text settings, change size, family, alignment,
   margin, line compression and hyphenation in one visit — six key changes,
   comfortably past the old cap of four — with `LOG_LEVEL=2`. Before: `[ERR]
   [FDC] All 4 page buffer slots full` from the fifth change on. After: nothing.
2. **The heap stops falling.** `ESP.getFreeHeap()` on entering Text settings and
   after each of six changes, **and once more after leaving the screen**. Before:
   ~4.5 KB lower per change, ~13.5 KB down after four, and the residue survives
   the exit. After: **one slot's worth (~4.5 KB) below the on-entry reading and
   then stable** while the screen is up — not back to the entry value, because
   the live slot is real and wanted; a tester who expects "flat" here will read a
   working fix as broken — and **back to the on-entry value once Text settings is
   closed** (A12).
   Caveat: the page buffer measured 3,969 bytes, just under the 4,096-byte PSRAM
   auto-routing threshold; a longer preview string in another language would
   cross it and land in PSRAM, which changes what this reading shows.
3. **The question the issue actually asks — does a denied prewarm cost a visible
   pause?** Research §6: the mechanism is real but string-dependent. The English
   sample is pure ASCII and re-inflates one group; the Spanish sample alternates
   ASCII with Latin-1 accented forms across three groups, and the sample is drawn
   twice (`TextSettingsPreview.cpp:116-123`) — order of 20–30 group inflations
   per redraw where a warm slot needs none. `stats.decompressTimeMs` is already
   accumulated and printed by `logStats` (`FontDecompressor.cpp:512-523`).
   **I cannot measure this; the PR will say so rather than claim a performance
   win.**
4. **The reader is unchanged.** A11: no reader path was slot-starved, so page
   render time and heap should be identical. Read `Page render: prewarm=…ms`
   (`EpubReaderActivity.cpp:1577`) across a few page turns as a null result.
5. **No cache invalidation is needed.** Nothing here touches `BOOK_CACHE_VERSION`
   or `SECTION_FILE_VERSION`; `/.crosspoint/` does not need deleting.

## Risks

**R1 — A3 may be judged wrong by the orchestrator.** If 4 should become 16 as
insurance, it is a one-constant edit plus a comment rewrite, and nothing else in
this spec depends on the value. Recorded as the cheapest possible reversal.

**R2 — A4's comment is the only thing standing between the next person and this
bug's twin.** A comment is not enforcement. A6's log is the compensating control:
the first page that draws a second compressed family gets one readable line
naming the font id and style. That is a detection mechanism, not a prevention
one, and the review should say if that is not enough.

**R3 — A5's dedupe changes what a repeated same-font prewarm means.** The
argument that it is observably identical rests on reading the `break` at
`FontDecompressor.cpp:173` as making the second slot unreachable.
`OneSlotPerDistinctFontData` plus `AlreadyWarmDoesNotReallocate` pin it; if that
reading is wrong, both tests fail loudly rather than silently.

**R4 — The suite is not in CI until the orchestrator appends one line (A10).**
Stated in the PR, not worked around. `unit-tests` is a real job that runs `ctest`
(`ci.yml:168-194`), so the gap is a gap in coverage, not in machinery.

**R5 — Pre-existing, untouched.** (a) The ~2.8 KB stack frame in `prewarmCache`;
`neededGlyphs[512]` alone is 2048 bytes (`:262`) against 2–4 KB task stacks.
(b) The idle prewarm frees its own built-in page slots at the closing brace
(A11b), so for a built-in reader font it spends a full page render to retain
nothing. Both deserve their own issues; neither is in scope here.

## Review pass 0 — what changed and why

`docs/superpowers/reviews/issue-58-spec-review-0.md` returned **BLOCKER** with
2 BLOCKER, 3 MAJOR and 5 MINOR. All ten are applied. Every factual claim in the
review was re-verified against the code before being accepted — the findings are
correct.

| Finding | Change |
| --- | --- |
| **BLOCKER 1** | v1's headline "live reader-path defect" (a page drawing R/B/I/BI denies the status bar's prewarm) **cannot occur**: `notosans_8_regular` and both Ubuntu UI families are uncompressed, `groups == nullptr` (`notosans_8_regular.h:3667-3669`), and `FontCacheManager.cpp:47` filters them out. 32 of 37 built-in headers are grouped; the 5 that are not are exactly the non-reading fonts. Real slot demand per scan pass is 4, and the cap is 4. **`MAX_PAGE_SLOTS 4 → 16` is dropped**, with it the `static_assert` (whose arithmetic was wrong — `MAX_SCAN_FONTS × 4` bounds calls, not slots) and the `MAX_SCAN_FONTS` visibility change. Goal 2, A3, A11 and R1 rewritten; Problem §2 now says the constant is right and its comment is wrong. The escalated scope question is decided in A3 rather than sent to the human: keeping 4 is the narrower option, it is what CLAUDE.md's resource-justification rule requires absent a demonstrated bug, and the regret is asymmetric. |
| **BLOCKER 2** | The fixture was that same group-less font, so `prewarmCache` returned at `:252` and all seven tests would have observed `usedPageSlots() == 0` and passed vacuously. Fixture is now `notoserif_12_regular.h` (grouped, `groupCount = 13` at `:3708-3710`, and at 270,097 B actually the smallest header — v1's stated size reason was also wrong). Two tests were added, `AlreadyWarmDoesNotReallocate` and `UncompressedFontTakesNoSlot`, the latter a permanent guard on the fact v1 got wrong. |
| **MAJOR 3** | The listed test sources do not link: `lib/uzlib/src` vendors only `tinflate.c`, and `uzlib_uncompress_chksum` (`:630`) calls `uzlib_adler32`/`uzlib_crc32` (`:642,646`), declared at `uzlib.h:165,167` and defined nowhere. Confirmed independently: `nm` on `.pio/build/x4pro/firmware.elf` finds zero occurrences of all three symbols — the firmware link strips the dead function. A9 now adds `UzlibChecksumStubs.c` and records why the `--gc-sections` alternative was rejected. |
| **MAJOR 4** | v1's Problem §3 duplicate-slot waste is unreachable for the same reason as BLOCKER 1: the only style-incomplete families are the uncompressed ones, and every compressed family ships four styles. A5 is reframed from fix to guard and says a reviewer may reasonably ask for it to be dropped; Goal 4 is now a property, not a leak; the `StyleFallbackCollapsesToOneSlot` test row is labelled "not a live scenario". |
| **MAJOR 5** | CI has five jobs, not two. The gate list said `pio check`, which **exits 0 regardless of defects**, where `ci.yml:95` runs it with `--fail-on-defect low/medium/high`. Firmware gates corrected; the `unit-tests` job (`:168-194`) added to "What CI runs", which also sharpens A10. |
| **MINOR 6** | Per-slot cost was understated ~50%. Measured on the host: 3,969 + 516 = **4,485 bytes**, 43 glyphs. Problem §1 restated; tester step 2's "After: flat" was wrong and would make a working fix read as broken — it now says "~4.5 KB below entry and then stable", with the PSRAM-threshold caveat. |
| **MINOR 7** | `test/CMakeLists.txt` is append-ordered, not alphabetical (it ends `launcher_refresh, bookmark_save_action, bookmark_doc`). A10 now says append at the end. |
| **MINOR 8** | "A fifth font id degrades to the per-string prewarm" is true only for SD fonts — both `prewarmFallbackText` overloads are no-ops for a built-in id (`GfxRenderer.cpp:230-233`; the second, `:252-260`, delegates to `:264-266` via `:258`). Non-goals corrected, and A11 adds fixing the inaccurate source comment at `FontCacheManager.cpp:91-93`, which is three lines from code this change touches. |
| **MINOR 9** | A11 no longer claims "a fifth prewarm now succeeds" (there is none), and records that the idle prewarm's scope destructs at its own closing brace (`EpubReaderActivity.cpp:387-393`), freeing the built-in slots it just built. Filed as R5(b), not fixed. |
| **MINOR 10** | A7 is new: the return contract is documented as three values, and "already warm" is stated not to re-scan the new text. |

## Review pass 1 — what changed and why

`docs/superpowers/reviews/issue-58-spec-review-1.md` returned **CLEAR** with
0 BLOCKER, 1 MAJOR and 5 MINOR. All six are applied here; none reverses a
decision. The reviewer independently re-derived pass 0's findings rather than
assuming them, reproduced the uzlib link failure, the fixture swap and the
4,485-byte measurement from scratch, and confirmed A3's reversal by closing a
hole this spec left implicit: a second *compressed* font id cannot join a reader
scan pass, because `GfxRenderer::setFallbackFont` is only ever handed an SD font
id (`src/SdCardFontSystem.cpp:166`, `GfxRenderer.h:168-169`).

| Finding | Change |
| --- | --- |
| **MAJOR 1** | A1's release-before-acquire never releases the *last* generation, and `TextSettingsActivity` has no `onExit` at all (`TextSettingsActivity.h:26` declares only `onEnter`), so ~4.5 KB — ~9 KB with focus reading — outlived the screen. The Architecture section stated an absolute ownership invariant the design did not meet. **New A12** adds the one-line `onExit`; the invariant is now stated as two halves (between redraws, at screen close) with the reclaim points named; Goal 1, the ownership table, the flow section, the error-handling table and tester step 2 all updated. Took the reviewer's preferred fix rather than the reword, because CLAUDE.md states the rule outright. |
| **MINOR 2** | `SlotsFullIsVisibleToTheCaller` claimed to pin A6, which is unobservable from the suite: `FontCacheManager::prewarmCache` is `void` (`FontCacheManager.h:24`) and `test/stubs/Logging.h` expands the log macros to nothing. Row now pins A7 only; `UncompressedFontTakesNoSlot` says which function it asserts against; and a new paragraph states plainly that the suite pins the primitives while the `renderPreview` edit and A6's log line are covered only by tester steps 1 and 2. |
| **MINOR 3** | A7 enumerated three return values, but `0` is returned from four places — including `:252` (uncompressed font, which the suite depends on) and `:330` (no needed glyph), neither of which cached anything. The contract now names all four paths. |
| **MINOR 4** | "Four files change plus three new test files" contradicted its own list (five modified, five new). Corrected to five modified and five new, and A12 then takes it to seven modified (`TextSettingsActivity.{h,cpp}` is two files). |
| **MINOR 5** | The pass-0 table mixed v1 and v2 assumption numbers: the BLOCKER 1 row said "A10" for the reader-path assumption, which v2 renumbered to A11 (A10 is now the `test/CMakeLists.txt` question). Fixed to v2 numbering. |
| **MINOR 6** | A11 cited `GfxRenderer.cpp:230-232,264-266` for "both overloads return early". The first guard is `:230-233`; the second overload is `:252-260` and has no lookup of its own — it reaches `:264-266` through `ensureSdGlyphsResident` at `:258`. Re-cited, and the delegation is named. |

## Open questions for the review

1. **A3.** Keeping `MAX_PAGE_SLOTS = 4` is a decision I took rather than
   escalated, on the grounds that it is the narrower option and CLAUDE.md's
   resource rules answer it. If the orchestrator wants 16 shipped as insurance,
   say so — it is one constant and one comment.
2. **A12.** Is a one-line `onExit` on a screen the issue does not name the right
   call, or should the Architecture invariant simply have been reworded to admit
   a last generation owned by nobody? Pass 1 offered both; I took the code.
3. **A5.** Keep the dedupe as a guard with no reachable caller, or drop it?
4. **A6.** Is dropping the `FDC` line acceptable, given a future direct caller of
   `FontDecompressor` that ignores the return value would then be silent?
5. **A10.** Follow `ui-dev.md` and ship a suite CI does not run yet, or commit the
   one-line `test/CMakeLists.txt` append and accept the collision risk?
