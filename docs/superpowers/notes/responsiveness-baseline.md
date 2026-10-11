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
