# Font prewarm exhausts its four page slots — investigation

Investigated 2026-09-16/17 on `70041d90`, the tip of `main`, for issue #58.

**Verdict: reading #1 — the slots leak — but not where the issue says to look.**
`FontDecompressor::prewarmCache` is clean on every path. The leak is a *caller*:
`textsettings::renderPreview` allocates a page slot on every settings change and
never releases one, because it is the only `FontCacheManager::prewarmCache`
caller outside `PrewarmScope`, and `PrewarmScope` is the only thing that frees
slots.

The two pointers in the log resolve to **Noto Sans 16 Regular** and **Noto Sans
14 Regular** — two sizes of the same family, regular only. That is the signature
of the Text-settings preview (which prewarms exactly `getReaderFontId()` at
style mask `0x01`), not of a reader page (which would deny the status bar's
`notosans_8_regular`). Section 3 shows the arithmetic.

Reading #2 (four is too few) is a *separate, latent* defect on the reader path —
real, structurally provable, but not what fired in this log. Reading #3 (benign)
is wrong: the denied prewarm has a measurable cost, though only the human tester
can say whether it is visible.

---

## 1. Ownership and control flow

| File | Role |
| --- | --- |
| `lib/EpdFont/FontDecompressor.{h,cpp}` | Owns `pageSlots[4]`, `pageSlotCount`, and the error line |
| `lib/GfxRenderer/FontCacheManager.{h,cpp}` | Owns the scan→prewarm pass and `PrewarmScope`, the only slot *releaser* |
| `src/activities/settings/TextSettingsPreview.cpp` | The scope-less prewarm caller — the leak |
| `src/activities/reader/EpubReaderActivity.cpp` | The two scoped callers (page render, idle prewarm) |
| `src/activities/reader/PassageSelectActivity.cpp` | The third scoped caller |

Slots are allocated in exactly one function and freed in exactly one:

- allocate — `FontDecompressor::prewarmCache` (`FontDecompressor.cpp:251`),
  `pageSlotCount++` at `:375`
- free — `FontDecompressor::freePageBuffer` (`:26-33`), reached only from
  `clearCache()` (`:21-24`) and `deinit()` (`:16-19`)

`FontCacheManager` calls `fontDecompressor_->clearCache()` from
`FontCacheManager::clearCache()` (`FontCacheManager.cpp:15-20`) and
`releaseSdFontCaches()` (`:22-27`). `FontCacheManager::clearCache()` has three
callers in the firmware — `PrewarmScope`'s constructor (`:113`) and destructor
(`:131`), plus `BibleNavigationActivity.cpp:54` and
`EpubReaderChapterSelectionActivity.cpp:33`.

One scoped render therefore looks like:

```
PrewarmScope ctor      -> clearCache()  -> pageSlotCount = 0
  page->render(...)    -> drawText() returns early at GfxRenderer.cpp:659-662,
                          accumulating text into FontCacheManager::scanEntries_
endScanAndPrewarm()    -> one FontCacheManager::prewarmCache() per scan entry
  ...                  -> one FontDecompressor::prewarmCache() per set style bit
  real draw pass       -> getBitmap() hits the slots
PrewarmScope dtor      -> clearCache()  -> pageSlotCount = 0
```

## 2. `prewarmCache` itself does not leak

Every exit from `FontDecompressor::prewarmCache` was read:

| Line | Exit | Increments `pageSlotCount`? |
| --- | --- | --- |
| `:252` | null `fontData` / `groups` / `utf8Text` | no |
| `:258` | slots full (the logged error) | no |
| `:330` | `glyphCount == 0` | no |
| `:368` | `malloc` failure — frees both buffers, `slot = {}` | no |
| `:505` | success | yes, at `:375`, after both allocations succeeded |

The increment happens after the last fallible operation, and the two mid-loop
failure paths in step 4 (`:472-476`, `:481-485`) `free(tempBuf)` and count a
miss rather than returning. Nothing in this function can strand a slot.
**Reading #1 as the issue frames it — "something allocates without a matching
`freePageBuffer`" — is true of the caller, not of this function.**

## 3. The two logged pointers are Noto Sans 16 and 14, Regular

Built this worktree at `70041d90` and read the ELF
(`xtensa-esp32s3-elf-nm --defined-only .pio/build/x4pro/firmware.elf`):

| Logged pointer | Nearest `EpdFontData` symbol in this build | Δ |
| --- | --- | --- |
| `0x3c31c104` | `0x3c31c14c` `notosans_16_regular` | `0x48` |
| `0x3c35de0c` | `0x3c35de54` `notosans_14_regular` | `0x48` |

The delta is **identical** for two independent pointers, so the user's build
differs from this one by a uniform 72 bytes of preceding rodata — the ordinary
drift of a dev build against a slightly different tree. (Restoring the pre-#57
translations and rebuilding did *not* reproduce the shift, so the drift is from
some other local delta; the identical-delta argument does not depend on
identifying it.) The nearest font-data symbols are 60 KB+ apart, so the mapping
is unambiguous.

`sizeof(EpdFontData)` is ~84 bytes here (`notosans_18_regular` at `0x3c2d38b0`,
its next table at `0x3c2d3904`), so `0x48` is not an adjacent-struct artifact.

The full built-in set, for reference: 8 Noto families × 4 styles, plus
`ubuntu_10/12_regular|bold` and `notosans_8_regular`. `SMALL_FONT_ID` is
`notosans_8_regular` (`src/main.cpp:117-118`), at `0x3c3cfce4` — **neither logged
pointer**. That rules out the reader-page path, whose only possible overflow
victim is the status bar (section 5).

## 4. The leak: `renderPreview` prewarms without a scope

`src/activities/settings/TextSettingsPreview.cpp:105-111`:

```cpp
if (key != layout.key) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->prewarmCache(fontId, I18N.get(StrId::STR_FONT_PREVIEW_TEXT),
                      SETTINGS.focusReadingEnabled ? 0x03 : 0x01);
  }
  relayout(layout, renderer, fontId, textWidth);
  layout.key = key;
}
```

No `PrewarmScope`, and `TextSettingsActivity` calls no `clearCache` anywhere
(`onEnter` at `TextSettingsActivity.cpp:59` does not, and there is no `onExit`
override that does). `renderPreview` is called from
`TextSettingsActivity::render` (`:275`), i.e. once per full screen redraw, and
prewarms whenever `PreviewKey` changes. `PreviewKey` (`TextSettingsPreview.cpp:96-104`)
is `{fontId, fontPointSize, screenMargin, textWidth, lineCompression,
alignment, extraParagraphSpacing, focusReading, hyphenation}` — that is, every
setting this screen exists to change.

So with focus reading off (mask `0x01`, regular only):

| Setting change | Slots used |
| --- | --- |
| 1st | 1 |
| 2nd | 2 |
| 3rd | 3 |
| 4th | 4 |
| 5th and every one after | denied — `LOG_ERR` |

The denied pointer is always `getReaderFontId()`'s **regular** data, which is
why the same pointer repeats. Changing size changes `fontId`, which is why the
log shows Noto Sans 14 once and then Noto Sans 16 three times: a size change
followed by three more adjustments at 16pt. The observed timestamps
(73745 / 78752 / 84902 / 86394 ms — 5.0 s, 6.2 s, 1.5 s apart) fit hand
adjustments on a settings screen.

With focus reading on the mask is `0x03`, so each change costs **two** slots and
the ceiling arrives after two changes.

The issue's "same pointer repeating argues against a benign one-off" is correct,
and this is the mechanism: it is not one font retried, it is a cap that has been
permanently consumed by four dead slots.

**Heap held, not lost.** The slots survive until the next `PrewarmScope` —
returning to the reader clears them. Each slot is `malloc(totalBytes)` plus
`glyphCount * 12` bytes. The Spanish preview string
(`lib/I18n/translations/spanish.yaml:94`, a 99-character pangram) has ~35 unique
glyphs; at 16 pt that is roughly 3 KB per slot, so on the order of 12 KB of
internal SRAM (under the 4096-byte PSRAM auto-routing threshold, so it is
internal) held for the duration of a Text-settings visit. Bounded, but three
quarters of it is for fonts the user already moved off.

## 5. Reading #2 is real, separate, and did not fire here

`MAX_PAGE_SLOTS = 4` and its comment — "One per font style (R/B/I/BI)" —
(`FontDecompressor.h:10`) were correct when written. `git log -S` gives one
commit each:

- `0c9e8b3e` (2026-03-22) `fix: Fix prewarm perf when a page contains many styles (#1451)` — introduced `MAX_PAGE_SLOTS = 4`
- `a0daab99` (2026-08-17) `fix: Slow CJK TOC & Lists (#3071)` — introduced `MAX_SCAN_FONTS = 4`

Before `a0daab99`, `endScanAndPrewarm` made exactly **one**
`FontCacheManager::prewarmCache` call, for a single `scanFontId_`
(`git show a0daab99 -- lib/GfxRenderer/FontCacheManager.cpp`). One font × four
styles = four slots, exactly. `a0daab99` replaced that with a loop over four
`ScanEntry` slots (`FontCacheManager.h:66-76`, `.cpp:118-126`), each carrying its
own style mask. The demand ceiling went from 4 to **4 font ids × 4 styles = 16**;
`MAX_PAGE_SLOTS` was not revisited. **The constant and its comment disagree
about what a slot is, exactly as the issue suspects** — a slot is one
`EpdFontData`, and there are 37 built-in ones.

The concrete reader-path overflow: `EpubReaderActivity::renderContents`
(`:1342-1349`) scans the page body and then `renderStatusBar()` inside one
scope. Body font = `getReaderFontId()`, a Noto family with all four styles
present (`src/main.cpp:68-112`); status bar = `SMALL_FONT_ID`
(`BaseTheme.cpp:917-918,955,965,1005,1016`). A page using regular, bold, italic
*and* bold-italic consumes all four slots at `FontCacheManager.cpp:43-52`, and
the status bar is denied. Not observed in this log (the pointer would be
`notosans_8_regular`), and whether a JW publication page actually reaches all
four styles on one page is **unverified**.

A second, quieter defect in the same loop: `EpdFontFamily::getFont`
(`EpdFontFamily.cpp:3-19`) falls back to `regular` for an absent style. For
`ui10FontFamily` / `ui12FontFamily` (`main.cpp:122,126` — regular and bold only)
and `smallFontFamily` (`main.cpp:118` — regular only), a style mask with the
italic bits set makes `getData()` return the *same* `EpdFontData` for two
styles, and `FontCacheManager::prewarmCache` spends a second slot on a duplicate
that `getBitmap` (`FontDecompressor.cpp:153-174`) can never reach — it breaks
after the first slot matching `fontData`.

## 6. Is a denied prewarm actually cheap? (reading #3)

No — but the size of the cost depends on the string, and only hardware can say
whether it is visible.

On a slot miss `getBitmap` takes the hot-group path
(`FontDecompressor.cpp:176-220`): a *single* group is kept decompressed
(`hotGroup`), so every switch between groups re-inflates. Built-in fonts use the
contiguous-group layout — `glyphToGroup` is `nullptr`
(`builtinFonts/notosans_16_regular.h:4388`) — with 13 groups, group 0 being
`{offset 0, compressed 3626, uncompressed 9181, 97 glyphs, first 0}`
(`:3305-3306`).

- **English preview** ("The quick brown fox…") is pure ASCII: one group, one
  inflate. Genuinely near-free.
- **Spanish preview** ("Benjamín pidió…", `spanish.yaml:94`) alternates ASCII
  (group 0) with Latin-1 accented forms in a later group. Each alternation
  re-inflates ~9–11 KB, and the preview draws the sample twice
  (`TextSettingsPreview.cpp:116-123`). Order of 20–30 inflations per redraw
  where a warm slot would need zero.

`ensureCapacity` (`:46-54`) is high-water, so the malloc churn is one-time; the
recurring cost is inflate time plus `getAlignedOffset` (`:90-115`), which on the
contiguous path is O(glyphs before this one *in its group*) per glyph — bounded
by a group's 97–256 glyphs, so minor beside the inflate.

**Only the human tester can close this.** `stats.decompressTimeMs` is already
accumulated and printed by `logStats` (`:512-523`), so a before/after on the
Text-settings screen with `LOG_LEVEL=2` would measure it directly.

## 7. Environment and the nearest existing example

Verified by running, in this worktree, after
`git submodule update --init --recursive` (issue #61 — a fresh worktree still
needs this by hand):

```
PlatformIO Core, version 6.1.19
clang-format version 21.1.8      (main checkout's .venv/bin, per issue #61)
cmake version 4.4.2
Python 3.14.7
esptool v5.1.2
pio run  -> SUCCESS, RAM 19.5% (64052 B), Flash 81.1% (5314686 B)
```

Host suite baseline (`cmake -S test -B <scratch> && cmake --build && ctest`):
**563 tests, 100% passed, 1.45 s.**

**No host test touches `FontDecompressor` or `FontCacheManager`** — `grep -rn
"FontDecompressor\|FontCacheManager" test/` returns nothing, and neither name
appears in `test/CMakeLists.txt`. There is no existing test of slot accounting
to extend.

The nearest existing example of testing real `lib/` rendering-path code on the
host is **`test/pagination`** (`test/pagination/CMakeLists.txt`): it compiles the
real `lib/Epub` sources unmodified, puts `${REPO_ROOT}/test/stubs` first on the
include path so `<Logging.h>`, `<HalDisplay.h>` and `<HalStorage.h>` resolve to
no-ops and `<Arduino.h>` never enters the build, and supplies `GfxRendererFake.cpp`
as a **link-time** substitute for the non-virtual `GfxRenderer` — so a new
renderer call breaks the link instead of silently diverging. `test/return_stack`
and `test/launcher_refresh` are the simpler pattern for a header-only unit with
no includes.

Two obstacles a design has to face if it wants a host test here:

- `FontDecompressor.cpp:3` includes `<Arduino.h>` (for `millis()`/`micros()`),
  which `test/stubs` does not currently provide.
- `FontDecompressor.h:3` includes `<InflateReader.h>`
  (`lib/InflateReader/InflateReader.h:3`), which includes `<uzlib.h>` — a
  PlatformIO `lib_deps` package, not vendored under `lib/`.
- `FontCacheManager.cpp:5` includes `<SdCardFont.h>`, which drags in the whole
  SD font stack.

`FontCacheManager`'s slot *accounting* — how many `FontDecompressor::prewarmCache`
calls one scan pass issues, and whether a caller releases them — is the part
worth testing and the part least entangled with `uzlib`.

---

## What the next phase has to decide

1. Whether `renderPreview` takes a `PrewarmScope` (which also clears the SD glyph
   cache it currently relies on keeping — see its own comment at
   `TextSettingsPreview.cpp:90-95`) or gets a narrower release.
2. Whether to raise `MAX_PAGE_SLOTS` and rewrite its comment, given section 5.
3. Whether the "slots full" line stays `LOG_ERR`. Section 6 says it is not
   benign, so lowering it alone is not the fix — but once the leak is closed it
   fires only on genuine over-demand, which is arguably a `LOG_DBG`.
4. What is testable on the host given the `uzlib` / `Arduino.h` obstacles above.
