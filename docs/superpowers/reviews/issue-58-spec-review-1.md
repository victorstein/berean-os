# Spec review pass 1 — issue #58, font prewarm page slots

**Reviewed:** `docs/superpowers/specs/2026-09-17-issue-58-design.md` (Design v2)
**Against:** issue #58 (`gh issue view 58`), `docs/superpowers/research/2026-09-17-issue-58-research.md`,
pass 0 (`docs/superpowers/reviews/issue-58-spec-review-0.md`, BLOCKER 2/3/5)
**Worktree:** `/Volumes/stein/.herdr/worktrees/berean-os/fix-58-font-prewarm-slots`, branch `fix/58-font-prewarm-slots` @ `b344f693`
**Method:** every cited `file:line` opened. Pass 0's findings re-derived independently rather than
assumed. The slot accounting, the fixture swap, the uzlib link gap and the `SdCardFont` fake were all
rebuilt and run on the host against the real `lib/` sources (clang++ 17, `-std=c++20`, a two-function
`Arduino.h`, real `FontCacheManager.cpp` + `FontDecompressor.cpp` + `EpdFont.cpp` +
`EpdFontFamily.cpp` + `InflateReader.cpp` + `Utf8.cpp` + `lib/uzlib/src/tinflate.c`).

## The pass-0 response table checks out

The brief's highest-value target was the "Review pass 0 — what changed and why" table (spec
`:635-653`) and its claim that all ten findings were applied. **They are, and the substance behind
each is correct.** Verified individually:

- **BLOCKER 1 (the cap raise is unjustified).** `lib/EpdFont/builtinFonts/` holds 37 font headers
  plus `all.h`; `grep -l EpdFontGroup` matches 32, and the 5 that do not are exactly
  `notosans_8_regular`, `ubuntu_10_{regular,bold}`, `ubuntu_12_{regular,bold}` — the status bar
  (`src/main.cpp:117-118`) and the two UI families (`:122,126`). `FontCacheManager.cpp:47` filters
  them (`if (!data || !data->groups) continue;`), so they cost zero slots. I also closed the one hole
  the spec leaves implicit: a second *compressed* id cannot join a reader scan pass, because the only
  other ids a page records are `SMALL_FONT_ID`/UI (uncompressed) and the CJK fallback, and
  `GfxRenderer::setFallbackFont` is only ever handed an SD font id
  (`src/SdCardFontSystem.cpp:166`, `lib/GfxRenderer/GfxRenderer.h:168-169`). Demand really is
  1 family x <=4 styles, and `pageSlotCount >= MAX_PAGE_SLOTS` (`FontDecompressor.cpp:255`) is tested
  before the increment, so the fourth allocates. Confirmed on the host: with four distinct
  `EpdFontData` pointers warm, the fifth returns `-1`. **A3's reversal is right, and keeping
  `MAX_PAGE_SLOTS = 4` is the correct call.** Pass 0 asked for the human; the author decided and
  recorded the reversal cost in R1 and Open question 1. Deciding it was the right move — the evidence
  is unambiguous and the narrower option needs no judgment.
- **BLOCKER 2 (vacuous fixture).** `notoserif_12_regular` is grouped (`:3708-3710` =
  `notoserif_12_regularGroups, 13, nullptr`, against the field order at `EpdFontData.h:185-187`) and
  is the smallest header at 270,097 B (`ls -lS`; the 8 pt header is 275,789 B). Host run against it
  with the Spanish pangram: 3 groups touched, real allocations, cap reached at the fifth distinct
  pointer. The fixture works.
- **MAJOR 3 (uzlib link).** Reproduced independently:

  ```
  Undefined symbols for architecture arm64:
    "_uzlib_adler32", referenced from: _uzlib_uncompress_chksum in tinflate.o
    "_uzlib_crc32",   referenced from: _uzlib_uncompress_chksum in tinflate.o
  ```

  and `xtensa-esp32s3-elf-nm .pio/build/x4pro/firmware.elf | grep -c 'uzlib_adler32\|uzlib_crc32\|uzlib_uncompress_chksum'`
  returns `0`. Two three-line bodies made the link succeed. A9 is correct.
- **MAJOR 5 (CI).** `ci.yml` jobs at `:33,58,97,168,198`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
  at `:95`; `ctest` at `:194`. `platformio.ini:24` carries `--suppress=unusedFunction`, so A8's
  accessor is safe.
- **MINOR 6 (per-slot cost).** Reproduced exactly: `notosans_16_regular` + `spanish.yaml:94` gives
  `pageBufferBytes=3969 pageGlyphsBytes=516 groups=3` = **4,485 B**, and a second call on the same
  pointer takes it to `7938` — the red state `OneSlotPerDistinctFontData` needs.
- **MAJOR 4, MINOR 7, 8, 9, 10** all land where the table says. A11(b) in particular is right:
  `SdCardFont.h:201-212` states in the source that `clearCache() -> resetStyleMiniData()` keeps both
  the allocations and the loaded data, so SD mini-glyph retention really is all the idle prewarm buys.

I also built the whole proposed suite shape. `FontCacheManager.cpp` linked against a fake supplying
**exactly** the five methods A9 names — `clearCache`, `releaseResidentCaches`,
`prewarm(const char*, uint8_t, bool, bool)`, `logStats(const char*)`, `resetStats` — and nothing
else was undefined. The three test rows I could evaluate behaved as the table predicts, including
`StyleFallbackCollapsesToOneSlot` (an `EpdFontFamily` with only `regular`, mask `0x0F`, allocates
4 x 2,637 B today). The test plan is sound.

The findings below are one design gap and five accuracy items. None touches the leak fix, which is
correct.

---

## MAJOR 1 — After the fix, `renderPreview` still never releases its last generation, and `TextSettingsActivity` has no `onExit`. The spec's own ownership invariant is left unmet.

**Claim.** Architecture, `:396-397`: "The invariant the whole change rests on, stated once: **a page
slot is owned by whoever created it, and every creator must have a release.** `PrewarmScope` has one;
`renderPreview` does not." Goal 1 (`:118-121`): "After any number of preview re-prewarms, the live
slot count attributable to the preview is exactly one generation (1 slot, or 2 with focus reading
on)."

**Problem.** A1/A2 give `renderPreview` a *release-before-acquire*, not a release. The final
generation — allocated on the last setting the user touched — is never released by its creator. The
activity that owns the screen cannot release it either: `TextSettingsActivity` declares only
`onEnter` (`src/activities/settings/TextSettingsActivity.h:26`, defined at
`TextSettingsActivity.cpp:59`) and has no `onExit` override at all, so when
`exitActivity()` deletes it the slot survives in the process-lifetime `FontDecompressor`
(`src/main.cpp:46`). CLAUDE.md's activity rule is explicit — "Anything allocated in `onEnter()` MUST
be freed in `onExit()`" — and this change is the moment that rule attaches to this screen, because
before it there was no discipline here to be consistent with.

Measured size of the residue: 4,485 B for one slot (`notosans_16_regular` + the Spanish preview
string, host-measured above), so **~4.5 KB, or ~9 KB with focus reading on** (mask `0x03`,
`TextSettingsPreview.cpp:107`), held in internal SRAM after the screen is gone.

Calibration, so this is not read as bigger than it is: the residue is bounded at one generation and
several existing transitions reclaim it — the reader's next `PrewarmScope`
(`EpubReaderActivity.cpp:1342-1349`), `BibleNavigationActivity.cpp:54`,
`EpubReaderChapterSelectionActivity.cpp:33`, and the two heap-critical
`releaseSdFontCaches()` calls at `CalibreConnectActivity.cpp:90` and
`CrossPointWebServerActivity.cpp:76` (which route through
`FontCacheManager.cpp:23 -> fontDecompressor_->clearCache()`). So this is not a leak and not a crash
risk. It is an owned-by-nobody allocation that the spec's own headline invariant says should not
exist, in a spec whose entire subject is that exact class of defect.

**Evidence.**

- `src/activities/settings/TextSettingsActivity.h:26` — `void onEnter() override;` is the only
  lifecycle override; `grep -n "onExit" src/activities/settings/TextSettingsActivity.{h,cpp}` returns
  nothing.
- `lib/EpdFont/FontDecompressor.cpp:26-33` (`freePageBuffer`), reachable only from `clearCache()`
  (`:21-24`) and `deinit()` (`:16-19`) — nothing else frees a slot.
- Host measurement, real `FontDecompressor.cpp`, `spanish.yaml:94` at 16 pt:
  `call1 ret=0 pageBufferBytes=3969 pageGlyphsBytes=516 groups=3`.

**Concrete fix.** Either is inline; no human judgment needed.

1. *Preferred.* Add a one-line `void onExit() override` to `TextSettingsActivity` calling
   `renderer.getFontCacheManager()->releaseBuiltinGlyphCache()` before
   `UiTabListActivity::onExit()`, add `src/activities/settings/TextSettingsActivity.{h,cpp}` to the
   Architecture file list, and add a row to the Error-handling table for the null-`fcm` case (already
   the pattern at `TextSettingsPreview.cpp:106`). Then Goal 1 becomes "zero after the screen closes"
   and tester step 2 gains a third reading: heap back to the on-entry value on leaving Text settings.
2. *Or*, if the residue is deliberate, say so where the invariant is stated: reword `:396-397` to
   "release-before-acquire bounds the preview at one live generation; the last generation is
   reclaimed by the next `PrewarmScope` or `releaseSdFontCaches()`", and name those reclaim points.
   Do not leave the absolute form of the invariant standing beside a design that does not meet it.

---

## MINOR 2 — `SlotsFullIsVisibleToTheCaller` cannot pin A6, and no listed test covers the caller change that is the actual fix.

**Claim.** Testing strategy `:525`: the test asserts "the fifth distinct font returns `< 0` ... this
pins A6/A7". Goal 5 (`:130`): "Host tests that fail before the change and pass after, for 1 and 4."

**Problem.** A6's subject is `FontCacheManager::prewarmCache` distinguishing `missed < 0` and logging
the font id and style. That is unobservable from this suite on both counts:
`FontCacheManager::prewarmCache` returns `void` (`lib/GfxRenderer/FontCacheManager.h:24`), and
`test/stubs/Logging.h` expands `LOG_ERR`/`LOG_DBG` to nothing. The test can only call
`FontDecompressor::prewarmCache` directly, which pins **A7** (the return contract) and nothing of
A6. The same applies to `UncompressedFontTakesNoSlot`'s "and returns `0`" (`:526`).

Separately, Goal 1 is about a settings session, but `PreviewLoopDoesNotAccumulate` exercises the
*primitive* (`releaseBuiltinGlyphCache()` then `prewarmCache`), not `textsettings::renderPreview`,
which needs `GfxRenderer`, `SETTINGS` and `I18N` and is not host-testable. The one-line caller change
that is the whole fix is verified only by tester step 1.

**Concrete fix.** In the test table, change "pins A6/A7" to "pins A7"; drop "and returns `0`" from
the `UncompressedFontTakesNoSlot` row or state it is asserted against
`FontDecompressor::prewarmCache`. In Goal 5 or "What the host suite proves", say plainly that the
suite pins the primitives and that the `renderPreview` edit itself is covered only by tester step 1 —
the spec is honest elsewhere about what it cannot measure, and this is the one place it is not.

---

## MINOR 3 — A7's "three values" contract still misdescribes two live return paths.

**Claim.** A7 (`:286-295`): the doc comment becomes "`-1` slots full (nothing allocated), `0`
prewarmed or already warm, `>0` glyphs that could not be loaded".

**Problem.** `FontDecompressor::prewarmCache` returns `0` from four places, not two.
`FontDecompressor.cpp:252` returns `0` for a null `fontData`, a **null `groups`** (every uncompressed
font) or a null text pointer; `:330` returns `0` when the text needs no glyph from this font. Neither
is "prewarmed" and neither is "already warm" — nothing was cached and no slot exists. Pass 0's
MINOR 10 asked precisely for this contract to stop reporting a success that is not one; enumerating
three values leaves the same class of imprecision one layer down, and the spec's own
`UncompressedFontTakesNoSlot` test depends on the uncompressed case returning `0`.

**Evidence.** `lib/EpdFont/FontDecompressor.cpp:252`, `:330`. Host run:
`[ns8] groups=0x0 groupCount=0 ret=0` for `notosans_8_regular`.

**Concrete fix.** Make the `0` clause read "`0` — nothing to do (uncompressed font, empty text, or no
needed glyph), already warm, or fully prewarmed; in every case no slot was consumed by this call
beyond at most one newly allocated one".

---

## MINOR 4 — Architecture's file counts do not match its own list.

**Claim.** `:360`: "Four files change plus three new test files."

**Problem.** The block immediately below (`:363-382`) lists **five** modified files
(`FontDecompressor.h`, `FontDecompressor.cpp`, `FontCacheManager.h`, `FontCacheManager.cpp`,
`TextSettingsPreview.cpp`) and **five** new ones (`test/stubs/Arduino.h` plus the four under
`test/font_page_slots/`). MINOR 1's fix would make it six modified. A blast-radius sentence that
undercounts its own table is the kind of thing a plan phase copies forward.

**Concrete fix.** "Five files change plus five new files (one shared stub, four in the new suite)."

---

## MINOR 5 — The pass-0 response table switches assumption numbering mid-table.

**Claim.** The BLOCKER 1 row (`:644`) says "Goal 2, A3, **A10** and R1 rewritten"; the MINOR 9 row
(`:652`) says "**A11** no longer claims 'a fifth prewarm now succeeds'".

**Problem.** Both refer to the same assumption — the reader-path one. v2 renumbered it to A11 and
gave A10 to the `test/CMakeLists.txt` question. A reader auditing the table against the body follows
the BLOCKER 1 row to A10 and finds an unrelated assumption about a shared build file. The table is
the artifact whose job is to be auditable against the body.

**Concrete fix.** Use v2 numbering throughout the table: "Goal 2, A3, A11 and R1 rewritten".

---

## MINOR 6 — A11's citation for the second `prewarmFallbackText` overload points at the callee.

**Claim.** A11 (`:346-347`): "both `GfxRenderer::prewarmFallbackText` overloads return early unless
the id is in `sdCardFonts_` (`GfxRenderer.cpp:230-232`, `:264-266`)".

**Problem.** The substance is right — I confirmed a built-in id can never be a fallback target
(`setFallbackFont` is called only from `src/SdCardFontSystem.cpp:166` with an SD id, and
`GfxRenderer.h:168-169` documents it as such), so both overloads are no-ops for built-ins. But the
citation is not. The second overload is `GfxRenderer.cpp:252-260`; it has no `sdCardFonts_` lookup of
its own and reaches the one at `:264-266` only through the `ensureSdGlyphsResident` call at `:258`.
The first overload's guard is `:230-233`, not `:230-232`. In a repo whose CLAUDE.md requires "a file
and a line, and the line must have been read", a citation that names the callee reads as the caller
having been checked when it was not.

**Concrete fix.** Cite `GfxRenderer.cpp:230-233` and `:252-260 -> :264-266`, and say the second
overload delegates the guard.

---

## Verdict rationale

The reversal in A3 is the decision v2 exists to make, and it is correct: I re-derived the slot demand
from the font headers, the filter at `FontCacheManager.cpp:47`, the fallback-map wiring and a host
run, and a scan pass cannot ask for a fifth compressed `EpdFontData` on this firmware. Pass 0's ten
findings are genuinely applied, not paraphrased, and two of the three that were expensive to check —
the uzlib link gap and the fixture swap — I reproduced from scratch. The test plan links and behaves
as advertised.

MAJOR 1 is a design gap the spec invites by stating an absolute ownership invariant it then does not
meet, but both fixes (a one-line `onExit`, or an honest rewording) are inline and neither reverses a
decision, changes what ships materially, nor needs the human. The five MINORs are accuracy repairs.
Nothing here gates the pipeline. Counts: 0 BLOCKER, 1 MAJOR, 5 MINOR.

VERDICT: CLEAR
