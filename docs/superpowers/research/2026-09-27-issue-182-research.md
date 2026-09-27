# Issue #182 — research

The Study sleep screen cuts passages instead of fitting them. This note records how the code behaves
on `main` (`7f44be57`) and the measurements behind the choice of fix. Every claim below cites a
line I read or a command I ran.

## 1. Which files own the behaviour

| Concern | File | Lines |
|---|---|---|
| Sleep-screen pick and draw | `src/activities/boot_sleep/StudySleepScreen.cpp` | 94-117 (row → candidate), 276-341 (layout), 345-380 (entry) |
| Pure pick helpers (host-tested) | `src/activities/boot_sleep/StudySleepPick.h` | 22-23 (capacities), 84-140 (`Candidate`, `Sampler`) |
| Host test for the pick | `test/study_sleep_pick/StudySleepPickTest.cpp` | 177-179 (snippet capacity) |
| Selection and label capture | `src/activities/reader/PassageSelectActivity.cpp` | 135-168 (`advancePage`), 170-198 (`selectionRange`), 245-268 (`selectionLabel`), 371-392 (`finalizeSelection`) |
| Selection state | `src/activities/reader/PassageSelectActivity.h` | 139-146 (`anchorOffset`, `anchorIndex`) |
| Passage write path | `src/study/StudyStore.cpp` | 276-299 (`addPassage`) |
| Store format and caps | `lib/StudyStore/StudyStore/PassageDoc.{h,cpp}` | `.h:24-43`, `.cpp:91-107`, `189-257` |
| Row type | `lib/StudyStore/StudyStore/TaggedPassage.h` | 36 (`snippet`) |
| Word wrap | `lib/GfxRenderer/GfxRenderer.cpp` | 1777-1795 (`truncatedText`), 1797-1859 (`wrappedText`) |
| Dispatch into the study screen | `src/activities/boot_sleep/SleepActivity.cpp` | 548-550 |

## 2. Current control flow

### Save (reader → store)

1. `commitAt` records the first anchor as both `anchorIndex` (page-local word index) and
   `anchorOffset` (absolute visible-codepoint offset) (`PassageSelectActivity.cpp:308-315`).
2. A right-to-left swipe while picking the end calls `advancePage` (`:430`). It swaps in the next
   page, re-extracts `words`, and sets `anchorIndex = -1` (`:161`). It can run repeatedly, so a
   selection can span more than one page turn. It only moves forward: there is no backward turn in
   this activity (`:135-168`, `:426-430`).
3. `finalizeSelection` computes the stored **range** from `selectionRange`, which falls back to
   `anchorOffset` after a page turn (`:192-195`). The range, and therefore the stored start unit,
   is correct.
4. The **label**, though, uses page-local indices. With `anchorIndex == -1` it sets `lo = 0`
   (`:377-378`), so the label starts at the first word of the page the user **ended** on. The
   anchor's page is gone from memory by then (`:153-156`). **This is cause 1a, and the stored
   snippet loses its start.**
5. `selectionLabel(lo, hi)` walks the current page's blocks and stops at
   `LABEL_SCAN_BYTES = 128` (`:250`). Its comment says `HighlightDoc::addHighlight` truncates to
   72 bytes (`:247-249`). That comment is stale: the consumer is now `PassageDoc::add`, and it caps
   at 120.
6. `StudyStore::addPassage` copies the label into `passage.snippet` unchanged (`StudyStore.cpp:286`).
   `PassageDoc::add` passes it through `utf8SafeSummary(..., MAX_SNIPPET_BYTES = 120)`
   (`PassageDoc.cpp:96`, `.h:32`). `utf8SafeSummary` collapses whitespace and **cuts the tail**,
   byte-wise and UTF-8-safe, with no ellipsis and no regard for word boundaries
   (`lib/Utf8/Utf8.cpp:185-201`).

### Load

`PassageDoc::fromJson` truncates `"x"` to 120 bytes **again** on load (`PassageDoc.cpp:237`). A
build that meets a longer `"x"` therefore cuts it silently in memory, and its next save makes the
cut permanent.

### Sleep (store → screen)

1. `render` heap-allocates a `Sampler` and scan buffers (`StudySleepScreen.cpp:348-353`). It then
   streams each `/.berean/passages/*.json`, skipping any file over `SAVE_BYTE_BUDGET + 4096`
   (`:42`, `:122-126`), and stops at `MAX_TOTAL_BYTES = 262144` across files (`:45`, `:127-130`).
   It uses `PersistableStoreBase::readDocFromFileStreamed`, not `PassageFile::load`, because
   `load` can rename a `.tmp` and **this path must never write to the study store** (`:133-136`).
2. `offerRow` truncates `"x"` to 120 bytes a third time (`:102`) and copies it into a fixed
   `Candidate::snippet[121]` (`StudySleepPick.h:22,85,127`). A `static_assert` ties the two
   together (`StudySleepScreen.cpp:63`).
3. `drawScreen` wraps the snippet at `NOTOSERIF_18_FONT_ID`, italic, capped at
   `MAX_SNIPPET_LINES = 6` (`:51`, `:287-288`). `wrappedText` breaks on spaces. On the last
   allowed line it hands the remainder to `truncatedText` (`GfxRenderer.cpp:1807-1812`), which
   removes **characters**, not words, until the text plus U+2026 fits (`:1790-1792`). The result
   is a mid-word cut. **This is cause 2.**
4. The block is centred vertically between the viewable top and `areaBottom = markY - SECTION_GAP`
   (`:302-305`). Nothing checks whether `blockHeight` exceeds that space. The `std::max(0, …)`
   only clamps the start position.

### What the device photo shows

The screen showed *"ley, están separados de Cristo. Se han apartado de su bondad inmerecida."*
That is about 75 bytes, well under 120, so the 120-byte cap did not cut it, and 6 lines at 18pt
did not clip it (see §4). A start at *"ley,"* followed by the verse's true end is the signature of
step 4 in the save flow: the selection crossed a page turn after *"mediante la"*. The issue names
this as the likely cause, and the numbers support it.

## 3. Installed tool and package versions

| Tool | Version | Source |
|---|---|---|
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| ArduinoJson | 7.4.2 | `platformio.ini:155` |
| CMake (host tests) | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 (clang-2100.0.123.102) | `c++ --version` |

Fonts: all four reader serif sizes are registered with italic faces
(`src/main.cpp:68-92`, `:306-311`). 12, 16 and 18pt sit inside `#ifndef OMIT_FONTS` (`:307`), and
14pt is registered unconditionally. `OMIT_FONTS` is not set in `platformio.ini` (grep returns
nothing), so every size is available at sleep.

## 4. Measurements

### Line heights and available height

`getLineHeight` returns the regular face's `advanceY` (`GfxRenderer.cpp:2141-2148`). From the
`EpdFontData` initialisers in `lib/EpdFont/builtinFonts/`:

| Font | advanceY |
|---|---|
| Noto Serif 18 | 51 |
| Noto Serif 16 | 45 |
| Noto Serif 14 | 40 |
| Noto Serif 12 | 34 |
| Ubuntu 12 (`UI_12`) | 29 |
| Noto Sans 8 (`SMALL`) | 23 |

The italic faces have the same `advanceY`.

The X4 Pro profile never sets `viewableInsets`. A grep of `BoardConfig.h` finds only the
declaration, with its `{}` default (`freeink-sdk/.../BoardConfig.h:675`), so the insets are taken
as 0. That leaves 480×800 portrait and `width = 480 - 2*24 = 432` px.

Take the full layout `drawScreen` builds, with a date, reference, tag and progress strip
(`:296-305`), and a quote mark kept at 18pt:

- `areaBottom = 800 - 16 - 23 - 40 - 4 - 18 = 699`
- fixed chrome = date 47 + quote 51 + reference 47 + tag pill 49 + progress 113 = 307
- **space left for passage text ≈ 392 px**

| Size | Lines in 392 px |
|---|---|
| 18pt | 7 |
| 16pt | 8 |
| 14pt | 9 |
| 12pt | 11 |

Without the date, tag or strip, the space grows by that element's height.

### How much text each size holds

I simulated greedy word wrap at 432 px using the real italic `advanceX` values (12.4 fixed-point,
`EpdFontData.h:134`) from each `notoserif_<n>_italic.h`, without kerning, over the Spanish NWT on
disk (`/Volumes/stein/Downloads/nwt_S.epub`). The scripts are `verses.py` and `fit.py` in this
session's scratchpad. Verse text is split on `id="chapterN_verseM"`, with tags stripped and
footnote/xref anchors dropped. This approximates the device's extraction; it does not reproduce it.

- 31,078 verses. Bytes per verse: median 132, p90 237, p95 286, p99 658. Only 42.8% fit in 120
  bytes.
- Galatians 5:4 in this edition is 138 bytes. Its wording differs from the device's edition
  ("tratan de…por medio de" against "intentan…mediante"), but the length is similar. **So even a
  correctly started single verse overflows the 120-byte cap today.**
- Consecutive windows, joined with spaces (median / p95 bytes):

  | Verses | Median | p95 |
  |---|---|---|
  | 3 | 419 | 687 |
  | 5 | 705 | 1063 |
  | 7 | 993 | 1434 |

- Capacity of the full layout (a Galatians 5 run): 18pt ≈ 150 B, 16pt ≈ 200 B, 14pt ≈ 283 B,
  12pt ≈ 375 B.
- The largest size that fits the whole text, over 3,000 sampled windows per row:

  | Window | 18pt | 16pt | 14pt | 12pt | Fits at none |
  |---|---|---|---|---|---|
  | 1 verse | 67% | 18% | 10% | 3% | 2% |
  | 2 verses | 8% | 16% | 24% | 37% | 15% |
  | 3 verses | 1% | 2% | 8% | 30% | 59% |
  | 5 verses | 0% | 0% | 0% | 3% | 96% |
  | 7 verses | 0% | 0% | 0% | 1% | 99% |

**Finding for the spec:** the issue's device check expects "a long passage (for example 5–7
verses) shows whole at a smaller size". With the 18 → 12pt floor and the chrome held at fixed
sizes, that holds for about 1–3% of such passages. At 12pt the full layout holds about 375 bytes,
roughly 2–3 median verses. The spec has to settle this. The options are to accept the word-boundary
ellipsis for long passages as the designed outcome, or to change a constraint the issue fixes (the
12pt floor, or the chrome). Nothing in this phase settles it.

### Two ways to get the full text

**A. Resolve the text from the unit range at sleep.** The only producer of a unit's text is
`UnitIndexCache::unitText` (`UnitIndexCache.cpp:388-397`). Using it on the way to sleep needs:

- a `std::shared_ptr<Epub>` for the passage's publication. The only path lookup runs from book
  path to pubkey (`PubKeyRegistry.h:23-29`); nothing maps pubkey to path. `BookPathIndex` does
  not help here: it recovers a book path from a legacy highlight file's flattened name by walking
  the card (`src/study/BookPathIndex.h:4-25`). So this means scanning `pubkeys.json` or the card.
- `Epub::load`, which can **write**. On a stale CSS cache it rebuilds the cache and runs
  `Storage.removeDir(".../sections")` (`lib/Epub/Epub.cpp:374-398`).
- `UnitIndexCache::begin` and `unitsFor`. The header comment says `begin` "reads or writes"
  the index header (`UnitIndexCache.h:29-31`), and `unitsFor` builds and **persists** a missing
  document (`:33-36`). Both write under `/.berean/units`, which the sleep path must never do.
- `SpineHtmlStream::stream`. Without a cached HTML file it inflates from the zip, draws an
  "Indexing" popup and takes the render lock (`SpineHtmlStream.cpp:28-44`). `WhenMissing::Fail`
  avoids that, but then the text is simply unavailable whenever the section cache was evicted.
- a full expat pass over the spine item (`UnitText.h:34-50`). Psalm 119 is about 69 KB
  (`SpineHtmlStream.h:22-24`).
- a new range API: `unitText` returns one unit, and a passage spans `start`..`end`
  (`TaggedPassage.h:29-30`).

This path is read-only only if every write-capable branch is refused. It is multi-second in the
worst case, and at sleep time it depends on caches that can be evicted. It is not bounded and cheap
in the sense the issue asks for.

**B. Store a longer display text at save time.** The text is already in hand at save time
(`selectionLabel`, `PassageSelectActivity.cpp:245-268`), so no extra I/O is needed. The costs, all
bounded:

- **Store size.** A realistic passage measures about 476 B: the test asserts 63 of them stay under
  30,000 B (`test/passage_doc/PassageDocTest.cpp:271-275`). A cap of about 384–512 B, which is
  what 12pt can show (above), adds at most about 400 B raw per passage. The user's 63-passage store
  would move from under 30 KB to under about 56 KB, against `SAVE_BYTE_BUDGET = 200000`
  (`PassageDoc.h:30`).
- **Worst-case test.** The worst-case passage test (`PassageDocTest.cpp:580-605`) fills the
  snippet with `"`, which JSON escapes to 2 bytes. It asserts under 1,700 B and at least 100
  worst-case passages per budget. A 512-B text adds about 784 B escaped, so a worst-case passage
  is about 2.5 KB and the budget holds about 80. **That assertion (`:604`) would have to change,
  or the cap be set lower** (at 384 B: about 2.23 KB, about 89 passages). The spec must choose one
  and say so.
- **Sleep scan.** The scan budget is in bytes, so larger files cover fewer files per sleep
  (`StudySleepScreen.cpp:45`). One Bible store at about 56 KB is still far under 262,144.
- **Candidate buffers.** `Candidate` holds two fixed buffers per slot and the `Sampler` holds two
  slots (`StudySleepPick.h:84-90,136-137`), heap-allocated with `makeUniqueNoThrow`
  (`StudySleepScreen.cpp:348`). A 513-B capacity adds about 780 B of heap on the sleep path,
  none of it on the stack.
- **Compatibility.** Every older build truncates `"x"` to 120 on load (`PassageDoc.cpp:237`). A
  longer text therefore needs either:
  - a new field an older build refuses. That is the v2 links pattern: the format version is raised
    only when the new field is present, so a linkless file stays v1 (`PassageDoc.h:20-25`,
    `PassageDoc.cpp:189-190`).
  - or a longer `"x"` under a bumped version. That also changes every other `"x"` consumer: the
    highlights list subtitle and label (`HighlightsActivity.cpp:105,109`), the link label fallback
    (`PassageDoc.cpp:144`), and migration (`MigrationRunner.cpp:304`,
    `MigrationPlanner.cpp:37`).

  Older files load unchanged under either choice, because `isKnownFormatVersion` accepts
  `1..newest` (`lib/Serialization/FormatVersion.h:13-15`).

**Research leans towards B.** A has unbounded I/O and write-capable branches on the sleep path. B
costs a bounded store growth that fits the existing budget. The one real trade is the worst-case
test at `:604`, and that is a spec decision, not a blocker. Passages saved before the fix keep
their short `"x"` under B. The issue explicitly accepts that.

## 5. The cross-page selection fix

- **No state survives the turn.** `advancePage` discards the anchor page's `page` and `words`
  (`:153-156`). Keeping the true start therefore means capturing the anchor page's text from
  `anchorIndex` to the page end **before** the swap, and carrying it, bounded, across later turns.
  Turns only go forward (`:135`), so the carried text is always a prefix of the label.
- **Word order.** `selectionLabel` walks blocks in the same order as `extractWords`
  (`:245-252`, `:94-120`), so it can produce that suffix with the existing walk.
- **No host test today.** Nothing in `test/` covers `PassageSelectActivity`, `selectionLabel` or
  `anchorOffset` (grep over `test/`). A host test needs the label assembly pulled into a pure
  helper, the way `StudySleepPick.h` was split out of the screen (`StudySleepPick.h:14-16`).

## 6. Nearest existing examples

- **A `PassageDoc` format change:** `0bbcd47d feat: link passages to each other (#86)`
  (`git show --stat 0bbcd47d`). It added the `"k"` field with conditional v1/v2 writing
  (`PassageDoc.cpp:189-190`), refused rather than repaired bad data (`:55-83`), and added host
  tests. The tests cover the old-reader refusal (`PassageDocTest.cpp:420-445`), the v1-when-unused
  write (`:421-428`) and the worst-case budget (`:580-605`). This is the model for the store half.
- **A pure helper beside the sleep screen, tested on the host:** `StudySleepPick.h` and
  `test/study_sleep_pick/` (`CMakeLists.txt:1-20`). It includes no Arduino, HAL or ArduinoJson
  and links only `CatalogLabel.cpp`. This is the model for the fitter. The fitter can take font
  metrics as a function pointer plus context, the same shape as `Sampler`'s `RandomFn`
  (`StudySleepPick.h:82,98`), so it needs no `GfxRenderer`.
- **Font step-down:** no existing helper. Grep for `fitFont|largestFit|shrinkToFit` over `src`
  and `lib` finds nothing. The serif size map lives in `CrossPointSettings.cpp:370-372`. The fitter
  is new code, but it follows the `StudySleepPick.h` pure-helper pattern, so it invents no new
  mechanism.
- **Host test registration:** `test/CMakeLists.txt:115,126`. The batch context allows this task to
  add its own suites there.

## 7. Scope and tier

The fix touches three areas:

- `src/activities` (ui): the sleep screen and the selection;
- `lib/StudyStore` (data): the `PassageDoc` format, if option B is chosen;
- `src/study/StudyStore.cpp`: `addPassage`, which passes the text through.

The format change and its migration concern are what the batch context already anticipated. The
tier is already `heavy`, the highest tier, so it is not raised. Nothing reaches the network, the
HAL or the input layer.
