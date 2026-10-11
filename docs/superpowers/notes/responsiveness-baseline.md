# Responsiveness baseline

Measured 2026-10-11 on the owner's X4 Pro, `x4pro` dev build of `1ba1cd34`
(`1.29.8-dev-main-1ba1cd34`, `LOG_LEVEL=2`), cable-flashed. Serial captured with
`cat /dev/cu.usbmodem*`. The research and roadmap this feeds are
`docs/superpowers/research/2026-10-11-responsiveness-research.md` and
`docs/superpowers/plans/2026-10-11-responsiveness-roadmap.md`.

| | |
|---|---|
| Panel controller | **UC8179** (`XTDET promoted SSD1677 -> UC8179 (LUT_VER=01)`) |
| Reading font | Lexica Ultralegible 16, an **SD-card font** (`/.fonts/`) |
| Text anti-aliasing | on for run 1, off for run 2 |
| Night mode | off |
| Bible | NWT Spanish, 3,937 spine items, 129 TOC entries |
| Internal free heap while reading | 191–262 KB (`MEM Free`) |
| PSRAM free | ~8.2 MB |
| Compiler optimisation | `-Os` (`pio run -e x4pro -v`) |
| Flash mode | DIO, 80 MHz, bootloader and app (`esptool image-info`) |

Every figure below is a median of the logged timestamps unless marked otherwise.
"Settled" means the last panel waveform of that interaction has completed.

## Panel waveforms on this unit

| Waveform (`Wait complete:` label) | Duration | When |
|---|---|---|
| `8179_DRF`, FAST | 551–566 ms | most screen changes |
| `8179_gray_pre_DRF` | 674 ms | the first refresh of an anti-aliased page, and the first refresh *leaving* one |
| `8179_gray_DRF` | 300 ms | the gray pass of an anti-aliased page |
| `8179_DRF`, HALF | 1,506 ms | wake, search entry, Publications entry, a section's first page |
| `8179_PON` | 129 ms | first refresh after boot or wake |

## Interactions

| Scenario | n | Tap → settled | Panel share | Firmware share |
|---|---|---|---|---|
| Page turn, AA on | 21 | 1,378 ms (text readable at 863 ms) | 674 + 300 ms | `prewarm` 8, `bw_render` 75, `gray_lsb`+`gray_msb` 132, transfers ~130 ms |
| Page turn, AA off | 9 | **742 ms** | 565 ms | `prewarm` 64, `bw_render` 44, transfers ~65 ms |
| Reader menu open | 9 | 733 ms | 674 ms | ~25 ms |
| Reader menu close (AA on) | 6 | 1,160 ms | 551 + 300 ms | a full page re-render, gray pass included |
| Reader → Home | 11 | 1,042 ms | 674 ms (565 from a non-AA page) | ~330 ms; Home's paint is 241–257 ms of CPU |
| Home → Bible, straight to the page | 5 | 1,887 ms (readable 1,365 ms) | 551 + 300 ms | `Book loaded in` **505 ms** every time |
| Home → Bible via Go to | 3 | 1,721 ms to the picker | 551 ms | book load 505 + `books ready` 260 + band 225 ms |
| Go to picker from the reader menu | 3 | ~880 ms | 551 ms | `Band drawn in` **225 ms** |
| Each further picker step | — | ~800 ms | 551 ms | band 225 ms again |
| Go to chapter → page, chapter not yet built | 5 | 2.5–3.4 s | 551 + 300 ms | section build ~1.2–1.8 s |
| Bible search → keyboard, first after boot | 2 | 4.6 s | **HALF 1,506** + 551 ms | index open 2.2 s (`BSEARCH 1189 verse documents`) |
| Bible search → keyboard, later | 4 | ~2.7 s | **HALF 1,506** + 551 ms | index reopen ~500 ms |
| Publications entry | 3 | 1,940 ms | **HALF 1,506** ms | ~110 ms setup + band 225 ms |
| Wake → Home, first frame | 1 clean capture | 3,126 ms from reset | 129 + 1,506 ms | ~1.1 s boot, 250 ms paint |
| Wake → Home, settled | 1 | 4,427 ms | + 566 ms | verse scan 405 ms + second paint 257 ms |
| Sleep | 3 | ~2.9 s | 566 + 1,507 ms | `Study pick` 445 ms |

### Input latency (F13)

After an idle pause, the firmware saw the touch-down 35–145 ms before the tap
fired in 16 of 17 taps (`PWR Restoring normal CPU frequency` to the action). Taps
fire on release (`InputManager.cpp:568`), and the touch-down already restores the
10 ms loop, so the 50 ms idle delay costs nothing unless a tap is shorter than
it. One tap showed 12 ms. F13's "50–100 ms on nearly every page turn" does not
hold for touch.

### Representative lines

```
[104020] [DBG] [ERS] Page render: prewarm=61ms bw_render=74ms display=742ms bw_store=2ms gray_lsb=64ms gray_msb=68ms gray_display=364ms bw_restore=3ms total=1378ms
[479697] [DBG] [ERS] Page render: prewarm=65ms bw_render=45ms display=631ms total=741ms
[198857] [DBG] [BMC] Loaded cache data: 3937 spine, 129 TOC entries      (276 ms after "Loading ePub")
[199079] [DBG] [MEM] Study open: ...                                     (209 ms after "Loaded 1 bookmarks")
[199085] [DBG] [READER] Book loaded in 506 ms
[155060] [DBG] [MASTHEAD] Band drawn in 228 ms
[218816]   Wait complete:  8179_DRF (1507 ms)                            (Bible search "Opening" frame)
[435791]   Wait complete:  8179_DRF (1506 ms)                            (Publications entry)
[3539] [INF] [HOMEV] Verse scan: 43864 bytes, ..., 405 ms                (every wake)
[5503] [DBG] [SLP] Study pick: 1 files, 43864 bytes parsed, ..., 445 ms  (every sleep)
```

## What it changes

- **SSD1677-only items do nothing on this unit:** R1, R15, R16. The reader's
  overlap path is SSD1677-only too (F6), so R4 does nothing here, and internal
  heap never fell near its gate anyway.
- **R8 does not apply to an SD-card reading font.** `prewarm=` is 8 ms on
  in-chapter turns with AA on; with AA off it is ~64 ms, almost all of it the SD
  font's per-page `Built mini kern` pair.
- **R3 and R19 drop to the bottom** (see Input latency).
- **New, and larger than most of the roadmap:** the HALF on entering Bible
  search and Publications (~950 ms each over a FAST), the menu close re-render,
  and the verse file parsed once at sleep and again at wake.
- **Anti-aliasing is the largest single page-turn lever:** off settles a page
  ~640 ms (46 %) sooner and shows text ~120 ms sooner, because this panel's
  anti-aliased first waveform is 674 ms against FAST's 565 ms.

## Not measured

- The catalog search screen and its index load (R10). Publications was measured;
  the catalog search is one level deeper.
- An SSD1677 unit. Every SSD1677-specific item stays unranked until one is
  measured.
- Night mode.

## After the batch

Measured 2026-10-11 on the same unit, `x4pro` dev build of `36920945`
(`1.29.14-dev-main-36920945`; its code is v1.29.15's), cable-flashed, **anti-aliasing off**. The
seven fixes are #255 (R20), #257 (R2), #258 (R21), #259 (R11), #262 (R9), #263 (R22) and #266
(R12 with R5). Every boot logged `[SD] SDMMC clock 40000 kHz (requested 40000 kHz, attempt 1/4)`.

"Before" is the AA-off baseline (run 2) where one exists. The row marked *AA on* only has an
AA-on baseline, so part of its gain is the gray pass, not the fix. Search, Publications, the
picker and Home are not anti-aliased, so their baseline holds either way.

| Scenario | n | Before | After | Item, and the line that shows it |
|---|---|---|---|---|
| Bible search entry, first frame settled | 6 | 1,579 ms (HALF) | **604–606 ms** (FAST) | R20 |
| Publications entry | 3 | 1,940 ms (HALF) | **727–728 ms** | R20; band replayed |
| Bible search → keyboard, first after boot | 1 | 4.6 s | **1,574 ms** | R11: `Fingerprint from record in 3 ms`, `Index opened in 127 ms` |
| Bible search → keyboard, later | 4 | ~2.7 s | **1,253–1,258 ms** | R11: `Index resumed in 1 ms` |
| Home → Bible, `Book loaded in` | 9 | 505 ms | **17 ms** parked; 272 ms first after boot | R12 + R5: `Reusing the parked book` |
| Home → Bible, tap → page readable | 6 | 1,447 ms (n = 1) | **751 ms** parked; 1,064 ms first after boot | R12 + R5 |
| Reader → Home, settled | 7 | 944 ms | **711 ms** | R9 |
| Home paint, CPU | 13 | 241–257 ms | **19–22 ms** | R9: `Band replayed`; the three first paints after a boot were 277 ms |
| Go to picker from the reader menu, first frame | 5 | +866–888 ms | **+660–705 ms** | R9: `Band replayed in 1–2 ms`, books from cache |
| Each further picker step | — | ~800 ms | **~600 ms** | R9 |
| Reader menu close | 10 | 1,160 ms, *AA on* | **628–632 ms** | R21: `Page restore: copy=2ms`, no page render; one was the periodic HALF (1,604 ms) |
| Wake → Home settled, same-day wake | 3 | 4,427 ms from reset | **3,103–3,106 ms** | R22: `Verse restore: restored, current 1, 4 ms`, one paint |
| Page turn | 34 | 742 ms (n = 9) | 706 ms | none directly; `prewarm` 64 → 21 ms, `bw_render` 44 → 30 ms |
| Sleep | 3 | ~2.9 s | 2.9–3.0 s | unchanged; `Study pick` still 452 ms (berean-o-13) |

- **Menu close with AA off** would otherwise have re-rendered the page, about a page turn
  (~710–740 ms), so R21's AA-off gain is ~80–110 ms. Its AA-on gain, where the gray pass is
  skipped too, was not measured.
- **Wake** is one HALF paint now. Its 241 ms cover-band decode is the cost left on every wake and
  on each first masthead after it (berean-o-9).
- **The first search after the flash** computed the fingerprint (1,463 ms, keyboard at 3,034 ms)
  and wrote the record. Every later boot read the record.
- **A section's first page** still builds the section (cache miss → page shown: median 1,613 ms,
  n = 13, against 2,633 ms with AA on before). The SD clock and AA both moved, so this is not
  credited to R2 alone.
- **PSRAM held at Home** with search, bands and the parked Bible resident: free fell from 8,278,216
  B at boot to 7,707,960 B, about 570 KB. Internal free stayed within 191–262 KB.
- **Back from the chapter grid to the book grid** is a HALF (1,507 ms,
  `BibleNavigationActivity.cpp:370`), deliberate since #219 because the cover art would ghost. The
  baseline never took that path.

Not re-measured: anything with anti-aliasing on (the owner reads with it off), so t1's AA-on ghost
check and R21's AA-on figure are open; the catalog search; an SSD1677 unit.
