# R9: cache the cover band — design (berean-o-2)

| | |
|---|---|
| Bead / task | `berean-o-2` / `t2`, branch `perf/r9-cover-band-cache` |
| Surface | ui (`src/components`), with one-line calls in three `src/util` files and two activities, and a comment in `lib/GfxRenderer` |
| Research | `docs/superpowers/research/2026-10-11-berean-o-2-research.md` (cited below as **R§n**) |
| Prior design reused | #238 Task 9, `docs/superpowers/plans/2026-09-30-issue-238-plan.md:1465-1859` (never built) |
| Review tier | standard |

Every `file:line` here was re-read on this branch. The cost figures in §4 come from the firmware
built on this branch (`pio run -e x4pro`, SUCCESS). They are **estimates from instruction counts,
not device measurements**. Only the owner's device run measures anything.

---

## 1. Problem

Every paint of a cover band streams the whole thumbnail from SD and decodes it pixel by pixel.

- `blitCropped` reads every row of the file (`src/components/CoverBand.cpp:39-56`): 773 rows for a
  band 120 or 272 rows tall (R§4).
- It plots each inked pixel with `drawPixel` (`:54`).
- On the owner's unit this is `[MASTHEAD] Band drawn in 223–229 ms` on every Go to step,
  Publications paint and Meetings paint, n=11. Those figures are from the bead brief. The baseline
  note itself states 225 ms for Go to and Publications (`:45-46,50`), with the representative
  line `[155060] [DBG] [MASTHEAD] Band drawn in 228 ms` (`:72`).
- Home repaints its hero on entry and on every selection move (`LauncherActivity.cpp:353-355,477-484`).
  The baseline puts Home's paint at 241–257 ms (`:42`), but there is no band log to show how much
  of that the band is.

The callers are destroyed on every navigation. Nothing that lives as long as they do can carry the
band forward.

## 2. Goal

| # | Acceptance criterion (brief) | How this design meets it |
|---|---|---|
| G1 | Home's band gets a `Band drawn in N ms` log | `[LAUNCH] Band <drawn\|replayed\|declined> in N ms` from `drawHero` (§8) |
| G2 | A repeat paint of the same band replays in a few ms, on Home and on every masthead | Ink recorded during the first blit and held in PSRAM. Replayed byte-wise in Portrait: ≤ 16,240 byte ANDs, estimated well under 2 ms (§4) |
| G3 | Replayed pixels are identical to a fresh draw, proven by a host test | `CoverBandReplayTest` compares framebuffers byte for byte, fresh against replayed (§11) |
| G4 | The PSRAM held is bounded and stated | Hard cap 4 × 24,576 = **98,304 B**; expected 38,512 B (§10) |

## 3. Non-goals

- **The first paint's cost.** It stays a full-file stream, plus the recording (§4). Seeking to the
  crop rows is filed as `t2/x1`.
- **Any on-disk cache.** The band lives in RAM for one wake. Nothing new is written to SD, so the
  storage-discipline rules for stores do not apply.
- **The sleep screen's cover.** `SleepActivity::drawBibleCover` (`SleepActivity.cpp:602-…`) has
  its own path.
- **How thumbnails are generated or named.**
- **A new `GfxRenderer` primitive.**
- **WebDAV COPY not clearing the destination's cache.** That is an existing bug, filed as `t2/x2`.
  This cache mirrors whatever thumbnail is on the card, so it is no worse for it (A9).

## 4. The costs that pick the replay mechanism

| Path | Work per paint | Basis |
|---|---|---|
| Today: fresh draw | 773 `HalFile::read` calls of ≥ 60 B each, ≥ 358,672 pixel decodes, and up to 55,680 (masthead) or 126,208 (Home) `drawPixel` calls | R§4 |
| #238's replay: a bit test plus `drawPixel` per pixel | up to 55,680 / 126,208 `drawPixel` calls | — |
| This design's Portrait replay | one `&=` per mask byte: ≤ 464 × 16 = 7,424 (masthead), ≤ 464 × 35 = 16,240 (Home) | §6 |

What one `drawPixel` costs is read from this branch's firmware:

- `GfxRenderer::drawPixel` sits at `0x420468d0`, read with `xtensa-esp32s3-elf-objdump`.
- Its in-range Portrait path is about **40 instructions**: an `entry`, phyX and phyY spilled to the
  stack and reloaded, the bounds compares, the strip test, then a byte read-modify-write and `retw`.
- The CPU runs at 240 MHz: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240` in
  `framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h`, with no override in
  `platformio.ini:94-…`.

So one `drawPixel` is ≈ 0.2 µs. #238's replay would cost about **6–12 ms per masthead and 13–26 ms
on Home**, depending on how much of the cover is ink. That misses G2's "a few ms" on Home. The byte
replay does ≤ 16 K simple operations, walking both the mask and the framebuffer in physical-row
order, which is well under 2 ms.

**The cost of recording on the first paint is unmeasured.** The mask is column-major (A11), but the
blit walks row-major, so each inked pixel's bit lands `stride` bytes (16 or 35) from the previous
one, in PSRAM. The framebuffer is probably in PSRAM too:

- `FreeInkDisplay::allocFrameBufferStorage` falls back to `malloc(bufferSize)` unless
  `FREEINK_FB_PSRAM` is set (`freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:368-380`);
- this build has `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
  (`framework-arduinoespressif32-libs/esp32s3/sdkconfig:2259-2260`), so a 48,000 B `malloc` lands
  in PSRAM.

One blit row can therefore touch about 29.7 KB of mask and framebuffer lines through the 32 KB data
cache. The PR reports the first paint's `Band drawn in N ms` against the 225 ms baseline (§12).

If recording visibly regresses the first paint, the fallback is to stage 8 rows of a column in one
internal byte and flush them to the mask once per 8-row group. That cuts mask traffic 8×.

---

## 5. Assumptions (each one is a decision the review can attack)

**A1. The cache key is every argument of `CoverBand::draw`, plus the cover epoch.**
- The arguments are the cover path, the full band `Rect`, and the whole `Style`.
- Nothing narrower is attempted, such as dropping `band.x` or `cornerRadius`. "Same arguments, same
  epoch, same pixels" then needs no reasoning about which fields affect the ink.
- The decode depends on nothing else: no runtime setting reaches it (R§3, `BitmapHelpers.cpp:9,43-44`;
  `Bitmap.h:68`).

**A2. File size is not part of the key.**
- A thumbnail is a 1-bit BMP of `62 + rowBytes × height` bytes (`JpegToBmpConverter.cpp:88-90`,
  `PngToBmpConverter.cpp:129`).
- Its size is fixed by its dimensions, so it cannot tell two covers apart (R§5).
- The brief's "or another cheap identity" is met by the epoch (A3).

**A3. A process-wide atomic epoch, bumped wherever a thumbnail's content can change.** A band is
valid only while its recorded epoch equals the current one. The bump sites, each called **after**
its removal or write completes:

| Bump site | Why |
|---|---|
| `clearBookCache` (`src/util/BookCacheUtils.cpp:23-29`), after `Epub::clearCache()` | Every replace, upload, move or delete over the web server, WebDAV, the file browser or the downloader runs through it (`PublicationDownloader.cpp:110,252`, `CrossPointWebServer.cpp:622,691`, `FileRoutes.cpp:398,549,639,740`, `WebDAVHandler.cpp:376,425,534`, `FileBrowserActivity.cpp:169,227`) |
| `CardBooks::remove` (`src/util/CardBooks.cpp:57`), after `removeDir` | A delete from Publications drops the cache directory without `clearBookCache` |
| `CoverThumb::pathFor` (`src/util/CoverThumb.cpp:36-46`), after a successful `generateThumbBmp` | A new thumbnail file now exists at a path |
| `ClearCacheActivity::clearCache` (`ClearCacheActivity.cpp:97-130`), after its loop | Drops every book's cache directory |
| The reader's delete-cache action (`EpubReaderActivity.cpp:898`), after `epub->clearCache()` | Drops the open book's cache directory |

The last two rows were added after review 1 (MINOR 4). With them, **every removal of a cache
directory or thumbnail bumps.** A thumbnail is never
overwritten in place: generation returns early when the file exists (`Epub.cpp:670-674`). So its
content changes only after a removal, which bumps, or a directory move. The two moves don't need
their own bump:

- the move of a finished book into `/Read` (`EpubReaderActivity.cpp:121-134`) renames to a path
  that did not exist (`:114-117`);
- the downloader's cache-directory move (`PublicationDownloader.cpp:116`) comes after its
  `clearBookCache(destPath)` at `:110`.

**A4. The bump comes after the change, never before.** The render task reads the epoch before it
opens the file (§7). If the bump came first, a fill could read the epoch after the bump and the
pixels before the removal, and hold stale ink that nothing invalidates. With the bump after, a fill
that raced the change either:

- fails, because the file is gone (blit returns false, nothing held); or
- commits the old epoch, so the next lookup misses.

**A5. Sleep, theme and orientation need no handling of their own.**
- Deep-sleep wake is a chip reset (`src/main.cpp:269-270`), so the singleton and its PSRAM are gone.
- A live theme switch (`SettingsActivity.cpp:183-192`) changes `headerHeight`, the hero height and
  the plate height, so the band `Rect` and `Style` change and the key misses (A1).
- An orientation change moves `getScreenWidth()` and the insets, and so the `Rect` (`Masthead.cpp:20-29`).

**A6. A replay does not check that the file still exists.** A band is held only for a file that was
read whole. By A3 and A4, a file that disappears and comes back different always passes a bump
first. Checking would cost an SD path walk on every paint, which is what this bead removes. The
consequence is accepted: if a thumbnail is deleted and not regenerated before a band paints, the
band shows the book's cover as it last was, where today it would show the fallback header. Every
band-painting screen re-resolves its cover on entry before it paints:

- Home: `resolveTargets` (`LauncherActivity.cpp:117-128`), and in result handlers that run before
  the repaint (`ActivityManager.cpp:120-133`);
- Go to: `BibleNavigationActivity.cpp:88-89`;
- Publications: `PublicationsActivity.cpp:113`;
- Meetings: `MeetingsActivity.cpp:164`.

**A7. Only the render task touches the slots.** One task paints every activity
(`ActivityManager.cpp:32-38,47-60`). `drawHero` and every `drawFooter` run inside `render`
(`LauncherActivity.cpp:353-371`, `UiListActivity.cpp:154-170`). The slots therefore need no lock.
The epoch is the only state written from other tasks: the loop task, and the web server handlers.
It is a namespace-scope `std::atomic<uint32_t>`, constant-initialised, so there is no
static-initialisation race. `std::atomic` is already in use (`ActivityManager.h`, `UiAppHost.h`).

**A8. Four slots, least recently used evicted.** One wake can paint four distinct bands: the Home
hero, the Go to masthead, the Publications masthead and the Meetings masthead (R§4). One slot would
thrash on Home → Go to → Home. A fifth distinct band, for example after a theme switch, evicts the
least recently used.

**A9. The replay mirrors the card, not the book.** When a stale thumbnail sits on the card, such as
after a WebDAV COPY (`t2/x2`), the cache replays that same stale thumbnail. That is exactly what a
fresh draw would show.

**A10. Portrait replay is byte-wise and ink-only, with an exact fallback.**
- The replay writes the framebuffer through the renderer's raw-writer accessors
  (`GfxRenderer.h:223-230`), which exist "for raw writers (DirectPixelWriter) that bypass drawPixel
  for speed".
- It is modelled on `DirectPixelWriter` (`lib/Epub/Epub/converters/DirectPixelWriter.h:38-57,170-185`)
  and on `ImageBlock`'s replay of its RAM pixel cache (`ImageBlock.cpp:169-188`).
- It only clears bits, so it adds ink exactly as the blit does (`CoverBand.cpp:54`).
- It is used only when the orientation is Portrait and the band lies entirely inside the logical
  screen. Portrait is the orientation every non-reader screen uses (`GfxRenderer.cpp:563`; UI
  themes render in forced Portrait, `:1375-1377`).
- Otherwise the replay calls `drawPixel(x, y, true)` for each set bit, which is the blit's own call.

**A11. Mask layout: column-major, byte-aligned to Portrait's physical rows** (§6). This departs from
#238's row-major bitset (plan `:1569-1591`). It makes the Portrait replay a straight byte AND, with
no per-pixel transposition. The fallback reads the same layout one bit at a time.

**A12. Per-slot cap of 24,576 B (`MAX_SLOT_BYTES`).**
- Larger than any band this firmware draws: Home Classic needs ≤ 16,240 B.
- A band whose mask would exceed it is drawn fresh every time, logged, and not held. That keeps the
  PSRAM bound hard (G4).
- Slot buffers are sized exactly. A slot reused for a different size frees and reallocates, as
  #238's `reserveInk` does (plan `:1753-1766`). This only happens on a miss, which already costs
  the full ~225 ms stream.

**A13. On PSRAM OOM, draw anyway.** If the slot buffer cannot be allocated, the blit runs exactly as
today without recording. The result is `Drawn`, nothing is held, and an error is logged. The screen
never loses its cover because the cache failed.

**A14. The slot keeps the cover path as a `std::string`.**
- Lookup compares it against the caller's `const std::string&` without allocating. The replay path
  constructs no strings.
- Assignment reuses capacity after the first fill. The first fill allocates once, about 55 B of
  internal heap.
- The alternative is a 64-bit path hash, as in `ImageBlock.cpp:71-78`. It was rejected: a collision
  would show the wrong cover, and the string costs four small allocations per wake.

**A15. `CoverBand::draw` returns an outcome instead of `bool`.** The outcome is
`enum class Result : uint8_t { Declined, Drawn, Replayed }`. Both callers need `Replayed` for their
logs (G1, G2). It replaces the `bool` at `CoverBand.h:27` and both call sites (`Masthead.cpp:84`,
`LauncherActivity.cpp:248`). Callers treat `Declined` exactly as they treat `false` today.

---

## 6. Data layout: `BandInk` (`src/components/BandInk.h`, header-only, pure)

This is modelled on #238's `BandInk` (plan `:1563-1591`): `bytesFor`, `set`, `test`, with no
renderer, for `test/cover_band`. The layout changes per A11.

For a band `Rect{x, y, width, height}`:

| Field | Value | Meaning |
|---|---|---|
| `lead` | `y & 7` | Bit position of the band's first row inside its framebuffer byte in Portrait. There `phyX = y` (`GfxRenderer.cpp:565`) and the bit is `7 - phyX % 8` (`:591`). |
| `stride` | `(lead + height + 7) / 8` | Bytes per band column |
| `bytes` | `width × stride` | Total mask size |
| bit of `(column c, row r)` | byte `c × stride + ((lead + r) >> 3)`, mask `0x80 >> ((lead + r) & 7)` | 1 = the blit inked that pixel |

**Invariant:** in every column, the bits before `lead` and after `lead + height − 1` are 0. They hold
because the buffer is zeroed before recording and `set` is only called for `r ∈ [0, height)`.

**Portrait correspondence.** Band column `c` is physical row `panelHeight − 1 − (x + c)`
(`GfxRenderer.cpp:566`). Mask byte `k` of that column covers physical columns
`8 × ((y >> 3) + k) … +7`, because `y − lead = 8 × (y >> 3)`. So mask byte `k` sits exactly over
framebuffer byte `(y >> 3) + k` of that row. A mask bit of 1 is a framebuffer bit to clear: black is
0 (`GfxRenderer.cpp:593-594`).

**Sizes** at the band width of 464 (R§4), with the worst-case `lead` of 7:

| Band | Mask size |
|---|---|
| Masthead, 464 × 120 | ≤ 464 × 16 = **7,424 B** |
| Home Classic, 464 × 272 | ≤ 464 × 35 = **16,240 B** |
| Home Lyra, 464 × 250 | ≤ 464 × 33 = **15,312 B** |

## 7. Control flow

### `CoverBand::draw(renderer, coverPath, band, style)` (`CoverBand.cpp:64-84`, rewritten)

```
if coverPath is empty                       -> Declined   (cache untouched; today's blitCropped:21)
layout = BandInk::layoutFor(band)
if (ink = cache.find(coverPath, band, style)):          // render task only (A7)
    replayInk(renderer, band, layout, ink)              // §7, replay
    finishBand(renderer, band, style)                   // corner mask + plate, unchanged code
    -> Replayed
inkOut = cache.reserve(coverPath, band, style, layout.bytes)  // zeroed PSRAM, or null (A12, A13)
if !blitCropped(renderer, coverPath, band, visibleHeight, focusBand, layout, inkOut):
    cache.abandon()                                     // the slot stays invalid
    -> Declined                                         // band already cleared, as today (:42)
cache.commit()                                          // no-op when inkOut was null
finishBand(renderer, band, style)
-> Drawn
```

`finishBand` is today's `CoverBand.cpp:67-82`, moved into a helper as #238 did (plan `:1657-1674`).
It runs after both the blit and the replay, with the caller's current style.

### `blitCropped` (`CoverBand.cpp:19-58`)

The only change is the recording. Where it plots (`:54`), it also calls
`BandInk::set(inkOut, layout, column, screenY − band.y)` when `inkOut` is set. Reading, cropping,
row order and the failure path are untouched. The pixels a first paint draws are therefore exactly
today's.

### `replayInk(renderer, band, layout, ink)` (new, `CoverBand.cpp`, anonymous namespace)

```
if orientation == Portrait and band ⊂ [0, screenW) × [0, screenH):
    target = getWriteTarget(); originY = getWriteOriginY(); rows = getWriteRows()
    stride = getDisplayWidthBytes(); panelH = getDisplayHeight()
    for c in [0, band.width):
        sy = (panelH − 1 − (band.x + c)) − originY
        if (unsigned)sy >= rows: continue               // strip clip, as drawPixel :581-587
        dst = target + sy × stride + (band.y >> 3)
        for k in [0, layout.stride): dst[k] &= ~ink[c × layout.stride + k]
else:
    for each (c, r) with BandInk::test(ink, layout, c, r): drawPixel(band.x + c, band.y + r, true)
```

The fast path writes exactly what `drawPixel(x, y, true)` writes for each set bit:

- same target and strip clip (`GfxRenderer.h:228-230` against `GfxRenderer.cpp:577-587`);
- same byte (`:590`);
- same bit (`:591`);
- same polarity (`:593-594`).

A one-line constraint comment at `GfxRenderer::drawPixel` names this replay and
`test/cover_band/GfxRendererFake.cpp` as copies of its Portrait mapping. A change to `drawPixel`
must change both (review 1, MINOR 7).

`drawPixel`'s bounds drop (`:572-575`) can never trigger, because the band is inside the screen.
`drawPixel` ignores `renderMode` and the font-scan state, and so does the fast path. That is unlike
`DirectPixelWriter` (`:149-166`) and `fillRectImpl` (`:993`).

### `CoverBandCache` (new, `src/components/CoverBandCache.{h,cpp}`)

Modelled on two existing classes:

- `HomeVerse`, a singleton holding RAM state that outlives the screen that filled it, owned by one
  task (`HomeVerse.h:12-18,26,49`);
- `CatalogIndexStore`, for explicit PSRAM: the `PsramFree` deleter (`CatalogIndexStore.h:83-89`),
  and `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` (`CatalogIndexStore.cpp:40-47`).

```cpp
class CoverBandCache {
 public:
  static constexpr uint8_t SLOTS = 4;                 // A8
  static constexpr size_t MAX_SLOT_BYTES = 24 * 1024; // A12
  static CoverBandCache& getInstance();
  // Render task only (A7).
  const uint8_t* find(const std::string& coverPath, const Rect& band, const CoverBand::Style& style);
  uint8_t* reserve(const std::string& coverPath, const Rect& band, const CoverBand::Style& style, size_t bytes);
  void commit();
  void abandon();
  size_t bytesHeld() const;
  // Any task (A7). Call after the change completes (A4).
  static void noteCoversChanged();
};
```

| Operation | Behaviour |
|---|---|
| Slot | path, `Rect`, `Style`, epoch, last-use tick, PSRAM buffer and its size, valid flag |
| `find` | Hit = valid, key equal, epoch equal. Bumps the last-use tick. |
| `reserve` | Picks an invalid slot, else the least recently used one. A slot whose epoch differs from the current one counts as invalid (review 1, MINOR 8). Marks it invalid. Frees and reallocates when the size differs. `memset`s to 0. Stores the key and **the epoch read now, before the blit opens the file** (A4). Returns null over the cap or on OOM, both logged. |
| `commit` | Marks the reserved slot valid, and logs the slot and the total bytes held. |
| `abandon` | Leaves it invalid. |

`CoverBand.h` gets `void noteCoversChanged();`, which forwards to the cache. The three `src/util`
bump sites therefore include only the feature's public header. A `src/util` file including a
`components` header already has precedent: `src/util/QrUtils.h:7`.

## 8. Logging: the measurements this bead is judged by

All timings are `LOG_DBG`, as the brief requires. `millis()` brackets the `CoverBand::draw` call.

| Line | Where | When |
|---|---|---|
| `[MASTHEAD] Band drawn in N ms` / `Band replayed in N ms` / `Band declined in N ms` | `Masthead.cpp:83-85`, extended with `replayed` | every masthead paint |
| `[LAUNCH] Band drawn in N ms` / `replayed` / `declined` | `LauncherActivity::drawHero`, new, same shape | every Home paint with a cover path |
| `[COVER_BAND] Held band WxH in slot S: B B, T B held` | `CoverBandCache::commit` | each fill: states the PSRAM held (G4) |
| `[COVER_BAND] OOM: B B band ink in PSRAM` | `reserve` | `LOG_ERR`, A13 |
| `[COVER_BAND] Band ink B B over the 24576 B cap; not held` | `reserve` | `LOG_ERR`, A12 |

The masthead line keeps its module and wording. Grepping the baseline's `[MASTHEAD] Band drawn in`
still finds first paints, and `Band replayed in` finds repeats.

## 9. Error handling

| Case | Behaviour | Pixels |
|---|---|---|
| Empty path | `Declined` with no cache access (today `:21`) | as today |
| Missing, unreadable or too-small cover | `Declined`, slot abandoned | as today |
| Read fails mid-stream | band cleared to paper (`:42`), `Declined`, slot abandoned | as today |
| PSRAM OOM | `Drawn` without recording, nothing held, `LOG_ERR` (A13) | as today |
| Over the per-slot cap | `Drawn`, not held, `LOG_ERR` (A12) | as today |
| Thumbnail changed (bump) | next paint misses and redraws from the card (A3, A4) | from the new file |
| Thumbnail deleted, no bump, no re-resolve | replays the last-read cover (A6) | as the file last read |

No path refuses to paint, and none leaves partial state. A slot is valid only after a whole-file
read and `commit`.

## 10. Memory

| Item | Size | Where | Justification |
|---|---|---|---|
| Slot masks | Σ exact sizes, ≤ **4 × 24,576 = 98,304 B** hard cap. Expected 3 × 7,424 + 16,240 = **38,512 B**. | explicit PSRAM | Read only by the render task's replay. Kept out of internal SRAM, which the render path needs. PSRAM free is ~8.2 MB (baseline `:17`). The framebuffer itself is probably in PSRAM as well (§4). |
| Singleton | ~4 × (string 32 B + `Rect` 16 + `Style` 12 + epoch, tick, size, flag ~16) ≈ 0.3 KB | internal `.bss` | Lookup state |
| Slot path strings | ~55 B each, allocated once per slot | internal heap | A14 |
| Recording | none extra | — | It writes into the slot buffer itself |

The two existing row buffers in `blitCropped` (`:32-37`) are unchanged. Stack use adds a handful of
scalars, far under 256 B.

## 11. Testing

All tests are host tests, in `test/cover_band/`, which is already registered at
`test/CMakeLists.txt:126`. That directory's own `CMakeLists.txt` gains the new executables, so the
shared `test/CMakeLists.txt` is not edited. TDD order: each test is written first and fails.

### `BandInkTest.cpp` (pure, header-only; modelled on #238's `BandInkTest`, plan `:1482-1534`)

- Sizes: masthead 464 × 120 at `y = 14` → stride 16 → 7,424 B. Home 464 × 272 at `y = 22` → 16,240 B.
  `lead` 0 and 7 give the extreme strides. A zero or negative width or height gives 0.
- `set`/`test` round trip at corners and on every `lead` from 0 to 7. Neighbouring rows and columns
  stay clear.
- The A11 invariant: after setting every pixel of a band, bits outside `[lead, lead+height)` in each
  column are 0.
- The Portrait correspondence of §6: for each `lead`, setting `(c, r)` lights the bit that the
  `drawPixel` formula gives for `phyX = y + r`, and no other.

### `CoverBandReplayTest.cpp` (links the real code)

The production sources it compiles are `src/components/CoverBand.cpp`, `src/components/CoverBandCache.cpp`,
`lib/GfxRenderer/Bitmap.cpp` and `lib/GfxRenderer/BitmapHelpers.cpp`. The fakes:

- **`test/cover_band/GfxRendererFake.cpp`**, a link-time substitute in the manner of
  `test/pagination/GfxRendererFake.cpp`:
  - `begin()` points `frameBuffer` at a 48,000 B test buffer and sets the device's panel, 800 × 480
    with a 100 B stride. The host `HalDisplay` stub declares 480 × 800 (`test/stubs/HalDisplay.h:22-25`).
  - `getScreenWidth`/`getScreenHeight` are defined by orientation.
  - `drawPixel` is a **line-for-line copy of `GfxRenderer.cpp:559-598`**, minus the log call. So is
    the file-static `rotateCoordinates` (`:283-313`) it calls for non-Portrait orientations, which
    linking cannot supply. The file says so and names the lines.
  - `beginStripTarget`/`endStripTarget` copy `:1685-1701`.
  - `fillRect`, `maskRoundedRectOutsideCorners`, `fillRoundedRect` and `drawLine` append their
    arguments to a call log.
- **`test/stubs/HalStorageFake.cpp`** gains `HalFile::seekCur`. It is declared in
  `test/stubs/HalStorage.h:82` and called by `Bitmap::parseHeaders` (`Bitmap.cpp:88,110,115`). The
  addition is additive.
- **`test/cover_band/esp_heap_caps.h`**, a test-local stub in the manner of
  `test/ditherers/FailingNothrowNew.cpp`. `heap_caps_malloc` and `heap_caps_free` wrap `malloc` and
  `free`, with a flag that makes the next allocation fail.
- **`test/cover_band/CoverThumbStub.cpp`**: `CoverThumb::pathFor` returns `{}`, because
  `CoverBand::thumbPathFor` references it.

Fixtures are synthetic 1-bit BMPs built in the test, with no publisher art: 480 × 773, a
pseudo-random pattern with solid and dithered regions. Each is written top-down **and** bottom-up
into the fake card.

**The framebuffer is seeded, never blank** (review 1, MAJOR 1). Before the fresh draw and again
before the replay, every case that compares buffers (1, 2, 4, 5, 7, 9) fills all 48,000 bytes with
the same seeded pseudo-random pattern. The comparison covers all 48,000 bytes. Against a white
buffer, a broken replay that stores whole bytes (`dst[k] = ~ink[…]`) or restores paper is
byte-identical to the correct `&=`. Against a seeded one, it destroys:

- the pre-existing ink inside the band;
- the head and tail bits that each band column shares with up to 7 rows above and below it.

Case 1 also asserts directly that, with `lead != 0`, a black pixel placed at
`(band.x, band.y − 1)` before the replay is still black after it.

| # | Case | Asserts |
|---|---|---|
| 1 | Masthead style `{0.25, 0, 46}`, Home style `{0.25, 8, plate}`, Meetings `{0.0, 0, 46}`; band `y` over all 8 alignments | The first draw returns `Drawn`, the second `Replayed`. **The framebuffer after the replay is byte-identical to the one after the fresh draw.** The `finishBand` call logs are equal. |
| 2 | Bottom-up BMP | as 1 |
| 3 | The replay touches no storage | `storage_fake::readCallsOn(path)` is unchanged across the `Replayed` draw (`HalStorageFake.h`) |
| 4 | Fallback: `PortraitInverted` orientation; band partly off-screen | `Replayed`, framebuffer equal to the fresh draw in the same orientation |
| 5 | Strip target active (`beginStripTarget`) | fresh and replay under the same strip are equal; pixels outside the strip are untouched |
| 6 | Each key field differs: path, `x`, `y`, `width`, `height`, focus, radius, plate | `Drawn`, never `Replayed` |
| 7 | `noteCoversChanged()`, then the same file path with new content of the same size | `Drawn`, and the framebuffer matches a fresh draw of the new content |
| 8 | Five distinct bands, then the first again | the least recently used is evicted. `bytesHeld()` ≤ 4 × `MAX_SLOT_BYTES` and equals the sum of the held masks. |
| 9 | PSRAM OOM (stub flag) | `Drawn` twice, both framebuffers equal to a normal fresh draw, `bytesHeld() == 0` |
| 10 | Truncated BMP | `Declined`, band cleared (call log shows `fillRect` white). The next draw is also `Declined`, not `Replayed`. |
| 11 | Empty path | `Declined`, `bytesHeld()` unchanged |
| 12 | Mask over `MAX_SLOT_BYTES` (a tall band) | `Drawn` twice, nothing held |

**What the host test cannot prove.** That the fast path matches the *real* `drawPixel` rests on the
fake's copy of `GfxRenderer.cpp:559-598`. No host target builds `GfxRenderer.cpp` (R§7). The device
check in §12 covers the remaining step: a mapping error would shear or mirror the cover visibly.

Commands, after `./bin/bootstrap`:

```
cmake -S test -B build/test && cmake --build build/test --target BandInkTest CoverBandReplayTest CoverBandGeometryTest -j 8
./build/test/cover_band/BandInkTest && ./build/test/cover_band/CoverBandReplayTest && ./build/test/cover_band/CoverBandGeometryTest
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
```

## 12. Device verification (for the PR body; the owner runs it)

**Before**, from the baseline (`x4pro` dev build of `1ba1cd34`, UC8179):

- `[MASTHEAD] Band drawn in 223–229 ms` on every masthead paint, n=11 (the brief; the note states 225 ms, `:45-46,50`). Representative line (`:72`):
  `[155060] [DBG] [MASTHEAD] Band drawn in 228 ms`.
- Home has no band line. Its paint is 241–257 ms.

**After.** Flash the branch's `x4pro` dev build, by cable or by side-load (`CLAUDE.md`, Flashing),
and capture serial. Then, with ≥ 10 samples each:

1. **Wake to Home.** Expect `[LAUNCH] Band drawn in N ms` on the first frame, then
   `[LAUNCH] Band replayed in N ms` on the post-verse paint (`LauncherActivity.cpp:439-446`).
2. **Move the Home selection** with the side buttons: `[LAUNCH] Band replayed in N ms` on each move.
3. **Home → Go to**, and step through the picker: the first is `[MASTHEAD] Band drawn in ~225 ms`,
   every later one `[MASTHEAD] Band replayed in N ms`. Go back Home: `replayed`.
4. **Publications twice, Meetings twice:** `drawn` on the first entry of each, `replayed` after.
5. Each fill logs `[COVER_BAND] Held band …`. The final `T B held` is the PSRAM figure for the PR.
6. **By eye:** each replayed cover is identical to its first paint, with no shear, no mirroring, no
   missing plate rule and intact rounded corners on Home. Check both themes. After a theme switch,
   the first paint of each band logs `drawn` again (A5).
7. **Invalidation** (review 1, MINOR 5). In Publications, delete a publication whose cover is on the
   masthead (this goes through `CardBooks::remove`), or re-download it. The next paint of that band
   must log `drawn` and show the current cover, or the fallback header.
8. **First paint.** Report the first `Band drawn in N ms` against the 225 ms baseline, which shows
   what recording costs (§4).

Generating any thumbnail flushes every held band (A3, row 3). That includes Publications' row
thumbnails (`PublicationsActivity.cpp:124`) and Meetings' card cover (`MeetingsActivity.cpp:253`).
So the first paint after a screen that generated thumbnails logs `drawn`, not `replayed`. That is
expected, not a failure.

The criterion is that `replayed` reads a few ms on Home and on every masthead. The expected figure is
≤ 2 ms (§4). It has not been measured.

---

## 13. Rejected alternatives

| Alternative | Why not |
|---|---|
| #238's replay: `drawPixel` per set bit (plan `:1699-1706`) | About 40 instructions per pixel, so ~6–12 ms per masthead and ~13–26 ms on Home (§4). Misses G2 on Home. It survives as the exact fallback (A10). |
| Framebuffer snapshot (`readFramebufferRegion`/`writeFramebufferRegion`) | Snaps to 8 px in panel memory, so in Portrait it spills up to 7 list rows below the band, already drawn by `drawFooter` time (plan `:1472-1476`). It also restores paper, where the blit only adds ink. |
| File size in the key | Fixed by the dimensions (A2) |
| An owner that lives with the activity (#238's `BibleNavCache`) | Destroyed on every navigation, which is the problem this bead exists for |
| A new `GfxRenderer` ink-mask primitive | Needs a renderer API this work does not otherwise need. The documented raw-writer accessors (A10) already serve a caller-side writer. A renderer primitive would be no more host-testable, since no host build compiles `GfxRenderer.cpp`. |
| Reusing `DirectPixelWriter` | Per pixel (~20 instructions). Its `init` takes a non-const `GfxRenderer&` (`DirectPixelWriter.h:38`). In gray modes it skips writes `drawPixel` makes (`:149-166`). |
| Checking the file on each replay | An SD path walk per paint (A6) |
| Seeking to the crop on the first paint | Out of scope, filed as `t2/x1` |

## 14. Files

| File | Change |
|---|---|
| `src/components/BandInk.h` | new: §6 |
| `src/components/CoverBandCache.h`, `.cpp` | new: §7 |
| `src/components/CoverBand.h`, `.cpp` | `Result` (A15), recording, replay, `finishBand`, `noteCoversChanged` |
| `src/components/Masthead.cpp` | `replayed` in the existing log (`:83-85`) |
| `src/activities/launcher/LauncherActivity.cpp` | `drawHero` (`:246-267`): `Result`, timing log |
| `src/util/CoverThumb.cpp`, `src/util/BookCacheUtils.cpp`, `src/util/CardBooks.cpp`, `src/activities/settings/ClearCacheActivity.cpp`, `src/activities/reader/EpubReaderActivity.cpp` | one `CoverBand::noteCoversChanged()` each (A3) |
| `lib/GfxRenderer/GfxRenderer.cpp` | a constraint comment at `drawPixel` only (§7) |
| `test/cover_band/CMakeLists.txt` | two executables |
| `test/cover_band/BandInkTest.cpp`, `CoverBandReplayTest.cpp`, `GfxRendererFake.cpp`, `CoverThumbStub.cpp`, `esp_heap_caps.h` | new: §11 |
| `test/stubs/HalStorageFake.cpp` | `HalFile::seekCur` (§11) |

`LauncherActivity.cpp` is shared with roadmap R7. That item touches `onEnter` and `resolveTargets`;
this one touches only `drawHero`.

**For the orchestrator.** Neither `test/CMakeLists.txt` nor `src/main.cpp` needs a line.
`test/stubs/HalStorageFake.cpp` is a shared test stub. The change is one added method, but a sibling
editing the same file should land first or second, not in parallel.

## 15. Changes after review 1 (`docs/superpowers/reviews/berean-o-2-spec-review-1.md`, CLEAR)

Every finding was accepted and applied.

| Finding | Change |
|---|---|
| MAJOR 1: G3 can't tell `&=` from a whole-byte store against a white buffer | §11: the framebuffer is seeded with the same pattern before both draws, the comparison covers all 48,000 bytes, and a black pixel above the band must survive the replay |
| MINOR 2: the cost of recording on the first paint | §4: labelled unmeasured; caveat that the framebuffer is probably in PSRAM; staged-byte fallback named. §10 corrected. §12 step 8 reports the first paint. |
| MINOR 3: the fake's `drawPixel` also needs `rotateCoordinates` | §11: copies `:283-313` too |
| MINOR 4: two removals don't bump | A3: bumps added in `ClearCacheActivity::clearCache` and after `EpubReaderActivity.cpp:898`, so every removal now bumps. §14 updated. |
| MINOR 5: device verification never tests invalidation | §12 step 7, plus a note that thumbnail generation flushes every held band |
| MINOR 6: baseline citations | §1 and §12 cite the brief for 223–229 / n=11 / Meetings, and the note for 225 and 228 ms |
| MINOR 7: no guard against `drawPixel` drifting | §7: a constraint comment at `drawPixel`. §14 lists `GfxRenderer.cpp`. |
| MINOR 8: `reserve` treats stale-epoch slots as live | §7: a stale-epoch slot counts as invalid |
