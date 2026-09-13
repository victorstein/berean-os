# SD storage reporting, and downloading a publication by symbol

> **Parked 2026-09-14.** The blocking unknown is resolved and the two open design decisions are
> settled below, so this can be picked up without re-deriving anything. Nothing is implemented.
> **Part 1 (storage reporting) is independently useful and could ship on its own** — it has no
> dependency on the download feature and answers "will this fit?" and "how full is the card?"
> remotely, neither of which is possible today.

**Date:** 2026-09-14
**Status:** PARKED — feasibility confirmed, two decisions recorded, not scheduled
**Target:** CrossPoint Reader firmware, `x4pro` build target (ESP32-S3, 8MB PSRAM)
**Delivery:** `origin` (victorstein/crosspoint-x4pro), branch `feature/publication-download`
**Builds on:** the meeting downloader (#7, #11) — reuses its transport, parser and download loop

## Goal

Get arbitrary publications onto the device without a computer. Today only the two weekly meeting
publications can be fetched; everything else means downloading on a laptop and copying to the card.

Two pieces, in dependency order:

1. **Report SD capacity and free space**, over HTTP and on-device. Nothing can currently answer
   "will this fit?" — and the first publication we want is ~99 MB.
2. **Download a publication by its symbol**, resolving it through the same pub-media API the
   meeting downloader already uses.

## Non-goals

- **Searching for a publication on the device.** Not possible: the publisher's catalog is a
  **61.9 MB gzipped SQLite database** (`app.jw-cdn.org/catalogs/publications/v5/<uuid>/catalog.db.gz`)
  and `GETPUBMEDIALINKS` returns 400 without a known symbol. Discovery is a separate design — see
  Deferred.
- **Non-EPUB formats.** PDF, JWPUB, MP3 and DAISY are listed by the API and ignored; the reader is
  EPUB-only.
- **Changing the reader.** If a large EPUB reads badly, that is a reader problem, not a download one.

---

## Part 1: storage reporting

### What exists

`SDCardManager` already has both numbers (`SDCardManager.h:35-39`):

```cpp
uint64_t sdTotalBytes() const;   // capacity; cached at begin(); 0 if not mounted
uint64_t sdUsedBytes();          // used; 20-second TTL cache
```

`sdUsedBytes()` is **not `const`** and carries a TTL for a reason the header states: `freeClusterCount`
scans the FAT and is "too slow to call on every frame". That cache is the SDK's, and this design
relies on it rather than adding another.

### What is missing

**`HalStorage` does not expose either.** CLAUDE.md is unambiguous that `SDCardManager` must never be
called directly — every SD access goes through `HalStorage`'s `storageMutex`, because SdFat is not
thread-safe and a bypass trips the FreeRTOS priority-disinherit assert.

So add to `HalStorage`:

```cpp
uint64_t totalBytes();   // 0 when not mounted
uint64_t usedBytes();    // 0 when not mounted; SDK-side 20s TTL
uint64_t freeBytes();    // total - used, saturating at 0
```

Both take `storageMutex` like every other method. **Neither can be `const`** — `sdUsedBytes()` mutates
its cache, and `freeBytes()` calls it.

### Surfacing it

- **`/api/status`** gains `sdTotalBytes`, `sdUsedBytes`, `sdFreeBytes`. It already reports `freeHeap`
  and `uptime`, so this is the natural home and needs no new endpoint or route.
- **On-device**, the download screen shows free space beside the publication's size, so the decision
  is visible at the moment it matters.

`uint64_t` must not be truncated into the JSON — `ArduinoJson` handles 64-bit, but a careless
`(int)` cast would wrap a 64 GB card.

## Part 2: download by symbol

### The chain, already proven

`GETPUBMEDIALINKS?output=json&pub=<symbol>&langwritten=S&fileformat=EPUB` is exactly what the meeting
downloader calls, with `pub=` varying instead of fixed. Verified against the target publication:

```
pub=lff  ->  "¡Disfrute de la vida para siempre! …"   99.2 MB   lff_S.epub
```

So `PubMediaJsonParser` needs no new fields — it already extracts `pubName`, `url`, `filesize` and
`checksum`. It gains only a caller that passes an arbitrary symbol.

### Flow

1. **Enter a symbol** via the existing `KeyboardEntryActivity` (short, lowercase, ASCII — `lff`,
   `nwt`, `bhs`). It is visible in any share URL as `pub=<symbol>`.
2. **Resolve** and show `pubName`, size, and current free space. A bad symbol returns a JSON error
   array rather than an object — the parser must report "not found", not abort.
3. **Refuse when it will not fit.** Require `filesize + margin <= freeBytes()`. The margin exists
   because the reader's cache is *additional*: measured on this device, the 14.2 MB NWT carries a
   6.1 MB cache tree, ~43%. **Use 50% of the publication size as the margin** and state it as a
   heuristic, not a measurement — an image-heavy book caches differently from a text one.
4. **Download** through `HttpDownloader::downloadToFile` with progress, Back/cancel and
   `mappedInput.update()` inside the callback, exactly as `MeetingDownloadActivity` does.
5. **Verify the MD5** the API publishes and delete on mismatch; then `clearBookCache(destPath)`.
   Both are established behaviour from #11 and must not be re-derived.
6. **Name the file** with the same helper #11 introduced: `<pubName> <YYYY-MM>.epub` where an issue
   exists, else `<pubName>.epub`. `sanitizeFilename` already handles the punctuation in
   `¡Disfrute de la vida para siempre! …` and truncates on a UTF-8 boundary.

### Reuse, explicitly

Nothing here is new transport. `HttpDownloader`, `PubMediaJsonParser`, `meetingPublicationFilename`,
`matchesChecksum`, `clearBookCache` and the cancellable progress loop all exist. The new code is an
activity, a symbol prompt, the space check, and the HAL wrappers.

## Risks

- ~~A ~99 MB EPUB is entirely untested.~~ **Resolved: it works.** `lff_S.epub` (99.2 MB) was pushed
  to the card over the web server and the reader opens and reads it. Measured structure: **835 zip
  entries, 161 xhtml documents, 666 JPEGs (97.8 MB uncompressed), largest single xhtml 6,162 KB.**
  The size is almost entirely images; the spine is *small* (161 against the NWT's 3,941), which is
  why navigation is comfortable. Note the 6 MB single document is ~90x the NWT's largest chapter and
  paginated acceptably, so section size is not the limit it was feared to be.
- **Download duration.** ~99 MB at the measured ~415 KB/s is roughly **4 minutes** with the screen
  on and WiFi up. The activity must keep the watchdog fed and stay cancellable throughout, and should
  warn before starting something that long.
- **`sdUsedBytes()` is a FAT scan behind a 20-second cache.** Calling it in a render loop would
  stall the UI. Read it once when the screen opens and once after a download, never per frame.
- **No discovery.** You must know the symbol. That is the deliberate scope cut below, and it makes
  this feature useful to someone who already has a share link and useless for browsing.
- **Copyright.** These are the publisher's own public media endpoints, the same ones their app uses,
  for personal study material. This design moves files; it does not reproduce or redistribute
  content.

## The optimizer gap — a device-side download cannot use it

The web UI has an **"Optimize EPUB"** path (`FilesPage.html:1570`, `:1750`) backed by a browser-side
pipeline — JSZip unzips the archive and `processImage` / `applyGrayscale` / `createAutoCropPreview` /
`isSeparatorImage` / `showImagePicker` re-encode the images before upload.

**It is client-side JavaScript.** A download initiated *on the device* cannot reach it, so this
feature would fetch the raw archive: for `lff`, 666 full-resolution colour JPEGs onto a monochrome
panel — precisely what that tool exists to avoid.

This is a real gap in the feature, not a footnote: for image-heavy publications, device-side download
produces a strictly worse artifact than the path that already exists. Three directions, none chosen:

| Direction | Cost |
| --- | --- |
| Downscale on-device during download | Decode + re-encode 666 JPEGs and rewrite the zip on an ESP32. Large, and the slowest path on the least capable machine. |
| Accept the raw archive, document it | Free. Justified by the measurement above — the raw file reads fine — but wastes card space and download time on images the panel cannot show in colour. |
| Restrict to text-heavy publications | Avoids the problem by refusing the case that motivated the feature. |

**Decided: accept the raw archive.** Confirmed by reading `lff` un-optimized on the device —
legibility and page turns are unaffected. The web-UI optimizer stays available for when it is wanted;
the downloader does not try to reproduce it. This costs card space and transfer time on colour data a
monochrome panel cannot use, which is an acceptable trade against 16 GB and a ~4-minute transfer.

## Deferred: discovery

Three approaches were considered and none is in scope here:

| Approach | Why not now |
| --- | --- |
| On-device search | Impossible — 61.9 MB gzipped SQLite catalog, no search endpoint |
| Host-built index shipped to SD | Real search, but needs a periodic off-device refresh step |
| Curated in-firmware list | No typing, but needs a firmware change to extend |

**Decided: a curated in-firmware list is the preferred UX** — no typing, no index to refresh, and it
covers the publications actually in use. Its cost is that extending the list needs a firmware change,
which is acceptable for a set that changes rarely. Symbol entry remains the fallback for anything not
on the list, and the host-built index is the escape hatch if the curated set stops being enough.

## Testing

**Host** — the space arithmetic is the testable part and must be a free function over
`(filesize, freeBytes)` in a standalone header, or it cannot be built on host. Add
`test/storage_space/`: exact fit, off-by-one under, margin boundary, zero free, and a
`sdTotalBytes() == 0` (unmounted) case that must refuse rather than divide by zero.

**Device:**
1. `/api/status` reports the three fields; they match what the file listing implies and do not wrap.
2. Enter `lff` → name and size shown, free space shown, and the download is **refused** if it will
   not fit.
3. Enter a nonsense symbol → "not found", no crash.
4. Download something small first (`sjj`, a songbook) end to end: progress, cancel, MD5, cache clear.
5. Only then attempt `lff`, and confirm the reader opens it.

**Build:** `pio run -e x4pro` after the last edit. `./bin/clang-format-fix` (full tree).

## Open questions

None. **The card is 16 GB** (confirmed physically), against 32.4 MB in use — so a 99 MB publication
plus its cache is roughly **1% of the card**. Space is not a constraint for anything we currently
want, which reframes Part 1: the endpoint is worth having so the device can *refuse* an impossible
download and so questions like this are answerable remotely, not because free space is scarce.

That leaves the reader's behaviour on a ~99 MB EPUB as the only genuine unknown in this design.
