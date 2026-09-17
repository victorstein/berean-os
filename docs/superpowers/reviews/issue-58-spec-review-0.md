# Spec review — issue #58, font prewarm page slots

**Reviewed:** `docs/superpowers/specs/2026-09-17-issue-58-design.md` (Design v1)
**Against:** issue #58 (`gh issue view 58`), `docs/superpowers/research/2026-09-17-issue-58-research.md`
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/fix-58-font-prewarm-slots`, branch `fix/58-font-prewarm-slots` @ `1540e633`
**Method:** every cited line read; the slot accounting reproduced by building the real
`FontDecompressor.cpp` / `InflateReader.cpp` / `Utf8.cpp` / `uzlib` on the host with a two-line
`Arduino.h` stub and running it.

What holds up, so the findings below are read in proportion: the diagnosis of the **leak** is
correct and I reproduced its mechanism. `renderPreview` (`src/activities/settings/TextSettingsPreview.cpp:105-111`)
is the only `FontCacheManager::prewarmCache` caller outside a `PrewarmScope`, nothing in
`TextSettingsActivity` releases, and `PrewarmScope` is the only releaser
(`lib/GfxRenderer/FontCacheManager.cpp:113,131`). A1, A2, A4, A5, A6, A7, A9 and A10's
"don't touch the reader" are sound as written. The spec also silently **corrects** the research
note's claim that uzlib is a `lib_deps` download — it is vendored at `lib/uzlib/src`
(`lib/uzlib/library.json`), and the spec is right.

The findings are about the *second* half of the change — the cap raise — and about the test plan.

---

## BLOCKER 1 — The "live reader-path defect" that justifies `MAX_PAGE_SLOTS 4 → 16` cannot occur. The status-bar font never takes a page slot.

**Claim** (Problem §2, lines 64-68): "That is a live reader-path defect, not only a documentation
one: `EpubReaderActivity::renderContents` (`:1342-1349`) scans the page body and then
`renderStatusBar()` inside one scope. A page drawing regular, bold, italic *and* bold-italic
consumes all four slots, and the status bar is denied."

Restated in Data and control flow (lines 318-320): "Before: reader font at four styles took all four
slots and `SMALL_FONT_ID` was refused. After: five slots of sixteen, status bar served from its own
slot."

**Problem.** `SMALL_FONT_ID` is `notosans_8_regular`, and `notosans_8_regular` is an **uncompressed**
font: it has no DEFLATE groups at all. `FontCacheManager::prewarmCache` skips it before it ever
reaches `FontDecompressor`, and `getBitmap` serves it straight from flash. It consumes **zero**
page slots, cannot be "denied", and pays nothing when it isn't prewarmed. The same is true of both
UI fonts.

The consequence is larger than one sentence: **the slot demand of a single scan pass is ≤ 4, not
16.** `MAX_SCAN_FONTS × 4 = 16` is a ceiling on *calls into* `FontCacheManager::prewarmCache`, not
on *slots*, and the three extra font ids a reader page can carry are all slot-free. The cap of 4 has
been exactly sufficient since `a0daab99`, which is precisely why the issue's log contains only the
preview's signature and no `notosans_8_regular` pointer.

**Evidence.**

- `lib/EpdFont/builtinFonts/notosans_8_regular.h:3658-3677` — the initializer. Against the field
  order in `lib/EpdFont/EpdFontData.h:185-187` (`groups`, `groupCount`, `glyphToGroup`), fields 9-11
  are `nullptr, 0, nullptr`. Confirmed at runtime by the host harness:

  ```
  groups=0 glyphToGroup=0x0
  ```

  and every `prewarmCache` call against it returned `0` having allocated nothing.

- `lib/GfxRenderer/FontCacheManager.cpp:47` — `if (!data || !data->groups) continue;`. The
  status-bar entry never reaches `fontDecompressor_->prewarmCache` on line 48.

- `lib/EpdFont/FontDecompressor.cpp:147-150` —
  `if (!fontData->groups || fontData->groupCount == 0) { ...; return &fontData->bitmap[glyph->dataOffset]; }`.
  An unprewarmed uncompressed font costs one pointer add, not a group inflate.

- Which built-ins are compressed at all, over `lib/EpdFont/builtinFonts/`:

  ```
  none    notosans_8_regular.h               275789
  none    ubuntu_10_bold.h                   420006
  none    ubuntu_10_regular.h                375800
  none    ubuntu_12_bold.h                   481952
  none    ubuntu_12_regular.h                432530
  GROUPS  <the 32 notosans/notoserif 12/14/16/18 headers>
  ```

  (`grep -q EpdFontGroup` per header.) The four families that *could* be scanned alongside the
  reader font — `smallFontFamily` (`src/main.cpp:117-118`), `ui10FontFamily` (`:122`),
  `ui12FontFamily` (`:126`), and any SD fallback (which takes the early-return SD path at
  `FontCacheManager.cpp:31-38`) — are all slot-free. The reader font is one of the 8 compressed Noto
  families, each of which ships all four styles (`src/main.cpp:60-112`), so one scan pass demands
  **at most 4** slots, and `pageSlotCount >= MAX_PAGE_SLOTS` (`FontDecompressor.cpp:255`) is checked
  *before* the increment, so the fourth allocates.

**What this invalidates.** Problem §2's second paragraph; Goal 2's premise; A3's "16 is the exact
upper bound" as a *slot* bound; A10's "their behaviour changes only in that a fifth prewarm now
succeeds" (there is no fifth prewarm); R1 ("a page that used to be refused now allocates" — no such
page exists); tester step 4.

**Concrete fix.** This needs the human, not an inline edit, because it changes what ships:

1. Rewrite Problem §2 to what is true — the *constant and its comment* disagree about what a slot
   is, and `MAX_SCAN_FONTS` and `MAX_PAGE_SLOTS` drifted apart with nothing binding them. Delete the
   "live reader-path defect" claim and the `SMALL_FONT_ID` denial story. State that the slot demand
   of a scan pass is bounded by the number of *compressed* `EpdFontData`s a page touches, which is
   today `1 family × ≤4 styles = 4`.
2. Then decide, explicitly, between:
   - **Keep `MAX_PAGE_SLOTS = 4`** and make the `static_assert` express the real invariant (e.g.
     assert against a named `MAX_COMPRESSED_FONTS_PER_PAGE = 1` constant with the reasoning above),
     shipping only the leak fix, the dedupe and the log fix. Zero `.bss` cost.
   - **Raise it anyway** as insurance against a future second compressed font on one page, with the
     justification stated as insurance rather than as a live bug, and the 192-byte `.bss` cost
     accepted on those terms.

   Either is defensible. The spec must not present the second as fixing something observable.

---

## BLOCKER 2 — The test suite's fixture font has no compressed groups, so every slot test would pass vacuously.

**Claim** (Testing strategy, lines 406-411): "`lib/EpdFont/builtinFonts/notosans_8_regular.h` is a
self-contained `static const EpdFontData` with real deflate-compressed groups... Distinct-pointer
cases use an array of copies of that struct."

**Problem.** It has no groups (see BLOCKER 1). `FontDecompressor::prewarmCache` returns at
`FontDecompressor.cpp:252` — `if (!fontData || !fontData->groups || !utf8Text) return 0;` — before
the cap check and before any allocation. **Every one of the seven tests would observe
`usedPageSlots() == 0` and a return of `0`**, so the "Red today because" column is wrong for all of
them and the suite would prove nothing about the change. Goal 5 ("Host tests that fail before the
change and pass after") would be unmet while reporting green.

**Evidence.** Built and ran the real sources on the host (clang++ 17, `-std=c++20`, stub
`Arduino.h`, real `FontDecompressor.cpp` + `InflateReader.cpp` + `Utf8.cpp` + `lib/uzlib/src/tinflate.c`).

With `notosans_8_regular`:

```
same-ptr call1 missed=0
same-ptr call2 missed=0
copy 0 missed=0 ... copy 5 missed=0        <- six distinct pointers, cap of 4 never hit
groups=0 glyphToGroup=0x0
```

With `notosans_16_regular` (a grouped font) and the Spanish preview string
(`lib/I18n/translations/spanish.yaml:94`), the same harness reproduces the spec's expected
behaviour exactly:

```
call1 missed=0 pageBufferBytes=3969 pageGlyphsBytes=516 groups=3
call2 (same ptr) missed=0 pageBufferBytes=7938     <- today: a second, unreachable slot
copy 0 -> 0
copy 1 -> 0
copy 2 -> -1                                        <- cap of 4 reached
copy 3..7 -> -1
```

That second line is the red state `OneSlotPerDistinctFontData` needs, and it only appears with a
grouped font.

**Concrete fix.** Swap the fixture to a compressed header. `lib/EpdFont/builtinFonts/notoserif_12_regular.h`
is the smallest at 270,097 bytes — smaller than the 275,789-byte header the spec picked — and
carries `notoserif_12_regularGroups` with `groupCount = 13`, `glyphToGroup = nullptr`
(`notoserif_12_regular.h:3699-3711`), with no includes beyond `EpdFontData.h`. The "array of copies
for distinct pointers" technique is fine and was verified above; only the source struct changes.
Also correct the Fixtures paragraph's stated reason — the 8 pt header is not smaller than the
grouped ones, so header size was never the discriminator.

---

## MAJOR 3 — The listed test sources do not link. `tinflate.c` has two undefined references that only `--gc-sections` hides in the firmware build.

**Claim** (Testing strategy, lines 388-394): the suite's sources are `FontPageSlotsTest.cpp`,
`SdCardFontFake.cpp`, `FontCacheManager.cpp`, `FontDecompressor.cpp`, `EpdFont.cpp`,
`EpdFontFamily.cpp`, `InflateReader.cpp`, `lib/uzlib/src/tinflate.c`, `Utf8.cpp` — "`uzlib` is
vendored in-repo at `lib/uzlib` ... so the suite needs no network beyond the existing GoogleTest fetch."

**Problem.** Vendored, yes — but incompletely. `lib/uzlib/src` contains only `tinflate.c`; upstream's
`adler32.c` and `crc32.c` are absent (`ls lib/uzlib/src` → `defl_static.h tinf_compat.h tinf.h
tinflate.c uzlib_conf.h uzlib.h`). `uzlib_uncompress_chksum` at `lib/uzlib/src/tinflate.c:630` calls
`uzlib_adler32` (`:642`) and `uzlib_crc32` (`:646`), declared in `uzlib.h:165,167` and defined
nowhere in the repo. The firmware links because PlatformIO builds with `-ffunction-sections` +
`-Wl,--gc-sections` and that function is unreachable; a plain host link has no such escape.

**Evidence.**

```
$ clang++ ... -o proto proto.cpp FontDecompressor.cpp InflateReader.cpp Utf8.cpp tinflate.o
Undefined symbols for architecture arm64:
  "_uzlib_adler32", referenced from:
      _uzlib_uncompress_chksum in tinflate.o
  "_uzlib_crc32", referenced from:
      _uzlib_uncompress_chksum in tinflate.o
ld: symbol(s) not found for architecture arm64
```

GNU ld on the CI runner behaves the same way without `--gc-sections`, so this is not a macOS-only
artifact. Everything else in the source list is host-clean — I compiled all six C++ files with
`-Wall -Wextra -pedantic` and `tinflate.c` likewise, with zero errors, so A8's stub design is
otherwise correct and `SdCardFont.h` needs no stubbing (it includes only `<cstdint> <deque>
<string> <vector>` and the two Epd headers).

**Concrete fix.** Add one source to the suite and say why:

```cmake
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  UzlibChecksumStubs.c   # uzlib_uncompress_chksum is unreachable here; the firmware
                         # drops it via --gc-sections, a host link cannot.
```

with two three-line bodies returning their `prev_sum`/`crc` argument. (A `-Wl,--gc-sections` /
`-Wl,-dead_strip` flag pair would also work but is platform-forked and silently platform-specific.)
Note it in Architecture's file list too, since it is a new file.

---

## MAJOR 4 — Problem §3's duplicate-slot defect cannot occur either: the only families missing a style are exactly the uncompressed ones.

**Claim** (Problem §3, lines 71-77): "For `ui10FontFamily` / `ui12FontFamily` (`src/main.cpp:122,126`
— regular and bold only) and `smallFontFamily` (`:118` — regular only), `getData()` returns the
*same* `EpdFontData` for two styles, so `FontCacheManager::prewarmCache` spends a second slot on a
duplicate."

**Problem.** Those three families are `ubuntu_10`, `ubuntu_12` and `notosans_8` — the five
uncompressed headers. `FontCacheManager.cpp:47` drops them before a slot can be spent, so the
"second slot on a duplicate" is not reachable for any of the three families the spec names. Every
family that *does* reach `FontDecompressor` (the 8 Noto reading families) supplies all four distinct
styles (`src/main.cpp:60-112`), so `EpdFontFamily::getFont`'s fallback
(`lib/EpdFont/EpdFontFamily.cpp:8-18`) never collapses two mask bits onto one pointer in practice.

A5's dedupe is still worth having — it is cheap, it is provably behaviour-neutral (`getBitmap`
`break`s after the first slot matching `fontData`, `FontDecompressor.cpp:173`, which I confirmed by
reading the loop at `:153-174`), and it defends future callers. But it is **defensive**, not a fix
for an observed waste, and Goal 3 should say so.

`StyleFallbackDoesNotSpendTwoSlots` remains a valid unit test of the
`FontCacheManager` → `EpdFontFamily` → `FontDecompressor` composition once the fixture from
BLOCKER 2 is used; it just does not mirror any state this firmware can be in.

**Concrete fix.** Reword Problem §3's first bullet to "latent, and unreachable with the current
font set — every compressed family ships four styles and every style-incomplete family is
uncompressed", and reword Goal 3 as a property the code should guarantee rather than a leak it
closes. Add the same note to the test row so a future reader does not mistake it for a regression
guard on live behaviour.

---

## MAJOR 5 — The CI description and the `pio check` gate are both wrong, in the direction that lets a red CI look green locally.

**Claim** (Testing strategy, lines 428-429 and 450-455): "`.github/workflows/ci.yml` compiles `x4pro`
and runs the format check; the host suite is driven by `test/CMakeLists.txt`", and the firmware
gates are `pio run`, `pio check`, `./bin/clang-format-fix`.

**Problem.** `.github/workflows/ci.yml` has **five** jobs, not two. It runs `cppcheck` as

```yaml
      - name: Run cppcheck
        run: pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

and it runs the host suite itself:

```yaml
  unit-tests:
      - run: cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
      - run: cmake --build build/test
      - run: ctest --test-dir build/test --output-on-failure -j
```

Bare `pio check` — what the spec tells the implementer to run — **exits 0 regardless of defects**.
With `check_flags = --enable=all` (`platformio.ini:24`) that is a real gap: the local gate passes
and the CI job fails. This repo has already been bitten by exactly this class of fail-open gate.

The `unit-tests` job also makes A9's consequence sharper and *more* correct than stated: CI does run
`ctest`, so the new suite is not merely "unwired", it will sit next to 563 tests that do run, which
strengthens the case the spec puts in Open question 4.

**Evidence.** `.github/workflows/ci.yml` (jobs `clang-format`, `cppcheck`, `build`, `unit-tests`,
`test-status`); `platformio.ini:20,24-25`. `--suppress=unusedFunction` is present, so A7's
`usedPageSlots()` accessor will not trip the unused-function check — worth keeping, since that is
the one new symbol with no firmware caller.

**Concrete fix.** In Firmware gates, replace `pio check` with
`pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, and add
`cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test`. In
"What CI runs, and the gap", correct the job list.

---

## MINOR 6 — Per-slot cost is understated by ~50%, and tester step 2's expected "flat" reading is wrong.

Spec line 181 and research §4 put a preview slot at "roughly 3 KB"; line 337 puts the leak at
"~12 KB of internal SRAM held by three dead fonts"; tester step 2 (line 469) says "After: flat."

Measured, real code, Spanish sample (`spanish.yaml:94`) at 16 pt:

```
call1 missed=0 pageBufferBytes=3969 pageGlyphsBytes=516 groups=3
```

3,969 + 516 = **4,485 bytes** per slot, 43 unique glyphs (516 ÷ `sizeof(PageGlyphEntry)` = 12), so
four slots is ~17.9 KB and three dead ones ~13.5 KB. Both mallocs are under the 4,096-byte PSRAM
auto-routing threshold, so the "internal SRAM" reading holds — but only just; the page buffer is
3,969 bytes and a longer string would cross it and land in PSRAM, which would make the heap
measurement in tester step 2 read differently. Worth saying.

After the fix the live count is 1, not 0, so `ESP.getFreeHeap()` on the Text-settings screen will be
~4.5 KB below the on-entry reading, not flat. As written, a tester who sees 4.5 KB missing will
report the fix as not working. Restate step 2 as "one slot's worth (~4.5 KB) lower and **stable**
across further changes, instead of falling by another ~4.5 KB per change".

---

## MINOR 7 — `test/CMakeLists.txt` is append-ordered, not alphabetical.

Line 431 asks the orchestrator to append `add_subdirectory(font_page_slots)` "in alphabetical
position". The file is not alphabetical — it runs `streaming_json_parser, release_json_parser,
differential_rounding, hyphenation_eval, utf8_compose, …` and ends with `launcher_refresh,
bookmark_save_action, bookmark_doc`. Alphabetical insertion would put the line between
`differential_rounding` and `highlight_doc`, which is both wrong for the file's convention and a
worse merge-conflict position than the end. Say "append at the end", which is what every recent
suite did.

---

## MINOR 8 — "a fifth font id degrades gracefully to the per-string prewarm" is true only for SD fonts.

Non-goals line 97 cites `FontCacheManager.cpp:91-93` to argue `MAX_SCAN_FONTS` is safely out of
scope. That source comment is itself inaccurate for built-ins: the only per-string prewarm calls in
`GfxRenderer` are `lib/GfxRenderer/GfxRenderer.cpp:248` and `:277`, both inside
`sdCardFonts_.find(...)` guards (`:230-233`, `:262-265`). A built-in font that misses the scan-entry
cap gets no per-string prewarm at all — it degrades to `getBitmap`'s hot-group path
(`FontDecompressor.cpp:176-220`). Still graceful, still a fair non-goal; the mechanism named is
wrong. Fix the sentence (and consider fixing the source comment in the same PR, since it is three
lines away from code this change touches).

---

## MINOR 9 — A10 overstates what changes for the reader, in both directions.

Line 259: "Their behaviour changes only in that a fifth prewarm now succeeds." There is no fifth
prewarm (BLOCKER 1). Separately, the reader's *idle* prewarm
(`src/activities/reader/EpubReaderActivity.cpp:387-393`) declares `auto scope =
fcm->createPrewarmScope();` inside the `if (auto* fcm = …)` block, so the scope destructor runs at
that closing brace and calls `clearCache()` (`FontCacheManager.cpp:128-133`) — the built-in page
slots it just built are freed immediately. What the idle prewarm actually buys is SD mini-glyph
retention (`lib/EpdFont/SdCardFont.h:201-207`). Neither the spec nor the research notes this, and it
is one more reason the reader path is not slot-starved. Pre-existing and out of scope; worth one
sentence in A10 or Risks so the plan phase does not "fix" it by accident.

---

## MINOR 10 — A5's dedupe returns `0` where the declared contract is a miss count.

`FontDecompressor.h:27` documents the return as "the number of glyphs that couldn't be loaded (0 on
full success)". A5 returns `0` for "a slot already holds this `fontData`". If the second call's text
contains glyphs the first call's slot does not carry, those glyphs genuinely will not be served from
a slot, and `0` reports a success that is not one. It is the same *outcome* as today (the second
slot was unreachable anyway — `FontDecompressor.cpp:173`), so this is a documentation obligation,
not a behaviour objection: amend the doc comment on `FontDecompressor.h:25-28` to state the three
return values (`-1` slots full, `0` cached or already warm, `>0` glyph miss count) and say
explicitly that "already warm" does not re-scan the new text. A6 already touches that declaration to
document `-1`; fold this in.

---

## Verdict rationale

BLOCKER 1 removes the justification for the half of the change that carries the `.bss` cost and the
`static_assert`, and the replacement — ship the cap raise as insurance, or don't ship it — is a
scope decision the human owns. BLOCKER 2 would let the suite report green while testing nothing,
which defeats Goal 5. The three MAJORs and five MINORs are all fixable inline and none of them
touches the leak fix, which is correct and well-argued.

VERDICT: BLOCKER
BLOCKERS: 2
MAJORS: 3
