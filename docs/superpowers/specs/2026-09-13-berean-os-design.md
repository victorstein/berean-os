# bereanOS — a study firmware for the Xteink X4 Pro

**Status:** design agreed, revised after adversarial review
**Supersedes for this device:** nothing. This is a new product, forked from `crosspoint-x4pro`.

> **Revision note.** The first draft of this document asserted that one scanner
> covered the whole library, that every publication had an addressable unit, and
> that NimBLE was already compiled in. All three were wrong. It also introduced
> three new on-disk stores without noticing that this repo has a documented
> silent-truncation hazard that has already cost it a byte budget. Where this
> document now makes a claim, the claim is cited.

## Why a new project

CrossPoint is a general e-reader. Its `SCOPE.md` closes two doors this device
needs open: new external network connectors (`SCOPE.md:35`) and interactive apps
(`SCOPE.md:56`). Nothing designed here can flow upstream, so building it as more
features in the fork means drifting further from upstream on every merge while
gaining nothing from the relationship. (`SCOPE.md:48` keeps *button mapping*
explicitly in scope — another reason the input rework below has to happen in a
fork.)

The second reason is that CrossPoint's constraints are written for the ESP32-C3 —
380 KB RAM, no PSRAM, single core — and the X4 Pro is an ESP32-S3 with 8 MB PSRAM
(`platformio.ini:239,250`).

**That is a licence to use PSRAM deliberately, not a licence to stop budgeting.**
S3 PSRAM is on an external SPI bus: roughly an order of magnitude slower than
internal SRAM, unusable from an ISR, unusable while the flash cache is suspended,
and DMA-constrained. The split this project holds to:

| Lives in PSRAM | Lives in internal SRAM |
|---|---|
| Catalog index while Buscar is open (~217 KB) | Framebuffer (48 KB) |
| Unit index pages being built or queried | Selection geometry, render hot path |
| Download and inflate buffers | ISR state, anything `IRAM_ATTR` touches |

Internal SRAM remains the same ~380 KB-class resource CrossPoint treats it as.

## Hardware, as confirmed on the bench

From `freeink-sdk/docs/xteink-x4pro-support.md`, section "Input — digital buttons
+ capacitive Home", marked *confirmed on hardware*:

| | |
|---|---|
| MCU | ESP32-S3, dual core, 8 MB PSRAM, 16 MB flash |
| Display | 800×480 e-ink, SSD1677 or UC8179 by production batch |
| Touch | GT911 capacitive |
| Buttons | **Left** (GPIO0), **Right** (GPIO7), **Power** (GPIO3) |
| Home | a capacitive key bit on the GT911 (status `0x814E & 0x10`), not a GPIO |
| Partitions | `app0`/`app1` 6,553,600 B each (`partitions.csv:4-5`) |
| Current firmware | `firmware-x4pro.bin` = 5,591,088 B — 962,512 B headroom, 14.7% |

There is **no Back button and no Confirm button.**

The Home key is live end to end today, not aspirational: `BoardConfig::hasHomeKey()`
→ `HalGPIO.cpp:166-170` → `MappedInputManager.cpp:280,283` → consumed at
`EpubReaderActivity.cpp:546` and `PassageSelectActivity::handleHomeGesture()`.

**BLE is not compiled in.** `sdkconfig.x4pro:1085` does say
`CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`, but that file is upstream Arduino-ESP32's
prebuilt config, not a statement about this firmware: zero NimBLE objects link
into the build and `lib_deps` (`platformio.ini:137-152`) contains no BLE library.
A BLE HID host costs the full NimBLE stack plus controller, and that cost is
currently unbudgeted.

## Scope

### In

- The Bible as the centre of the device: book → chapter → verse navigation
- The two weekly meeting publications, downloaded automatically
- Browsing and downloading any publication from the jw.org catalog
- Tagging passages, and managing tags globally
- Settings

### Out, deliberately

- **Notes.** Consequence: the only text ever typed is a tag name and a WiFi
  password, both of which the existing soft keyboard handles.
- **JW Library interop.** The device is standalone and is the source of truth for
  its own study data. No third-party schema to track as it moves.
- **Full-text search beyond Bible verses.** Search means the publication
  catalog and, since 2026-09-24, Bible verse text
  (`2026-09-24-bible-verse-search-design.md`). Other publications, footnotes and
  study notes stay out.
- **Everything CrossPoint carries that isn't JW study:** the OPDS browser,
  KOSync, the dictionary activities, the TXT and XTC readers.

### Deferred

- BLE keyboard (unbudgeted; see flash below).
- Drag-to-select as an alternative to two-tap selection.

### Flash, honestly

Measured from `.pio/build/x4pro/firmware.map` by summing `.text`/`.rodata`/`.literal`
per object:

```
Opds          72,492      Txt           13,127
KOReader      86,890      Xtc           29,850
Dictionary    21,036      MappedInput    2,868 + ButtonRemap 2,959
                                        ─────────
                                        229,222 B  ≈ 4% of firmware
```

Headroom goes from 962 KB to ~1.19 MB. Real, but not transformative — and it is
the entire budget a BLE stack would have to fit into. **Dropping the other board
targets frees no flash at all**; PlatformIO compiles one env at a time. They cost
repo surface, which is still worth removing, but not bytes.

## Product shape

A **launcher** home screen — four tiles and a resume strip:

```
┌──────────────────────────────┐
│ Inicio              WiFi 87% │
├──────────────────────────────┤
│  ┌────────────────────────┐  │
│  │ Biblia                 │  │
│  │ Trad. del Nuevo Mundo  │  │
│  └────────────────────────┘  │
│  ┌──────────┐ ┌───────────┐  │
│  │Reuniones │ │ Buscar    │  │
│  │Sem 14-20 │ │ Catálogo  │  │
│  └──────────┘ └───────────┘  │
│  ┌────────────────────────┐  │
│  │ Ajustes                │  │
│  └────────────────────────┘  │
├──────────────────────────────┤
│ Seguir leyendo · Sal 119:145 │
└──────────────────────────────┘
```

> **Superseded, 2026-09-16.** This tile was specified as "Etiquetas y ajustes" and shipped
> under that label. Tag management landed in the reader instead — you select a passage and
> choose its tags there (issue #20, closed) — so nothing tag-related is reachable behind this
> tile. It was relabelled to "Ajustes" in #53, and the diagram above reflects that. The rest of
> this section is the original design and is left as written.

A launcher is a screen you pass *through*, and on e-ink every pass costs a full
panel refresh. The resume strip is what pays for that: the common case — waking
the device to carry on reading — is one tap and never touches the tiles.

**Tiles show no counts.** An earlier draft put "12 etiquetas · 248 pasajes" here,
which required denormalised counters in `tags.json` that nothing reconciles; see
*Counts* below. Live state that is cheap and cannot drift (which meeting week is
loaded) stays.

`UiTabListActivity` is used *inside* sections. It is not the home screen.

## Input model

### What already exists, and is kept

The reading screen today is **three vertical tap zones** — outer thirds page,
centre third opens the menu (`ReaderUtils.h:106`, `isTouchMenuTap`). An earlier
draft replaced tap-to-page with horizontal swipe and gave a bare tap to the menu.
That was a regression on the single most frequent interaction on the device, and
it made any stray thumb contact cost two full panel refreshes. **The three-zone
tap stays.** Horizontal swipe is an additional affordance, not a replacement.

The reader menu is also reachable from a Confirm release and from a Home hold
when `SETTINGS.longPressMenuFunction == LP_MENU_READER_MENU`
(`EpubReaderActivity.cpp:564-566,580`), and the tap zones are gated on
`SETTINGS.touchReaderControls`. An earlier draft said the centre tap was the only
route; it is not.

### The model

| Input | Everywhere | In the reader |
|---|---|---|
| Tap | activates what you touched | outer thirds page, centre opens menu |
| Left / Right | previous / next in sequence | previous / next page |
| **Long-press Right** | **Confirm / activate** | — |
| **Left + Right chord** | **Back** | Back |
| **Left + Right, held** | **launcher** | launcher |
| Home, short | Back | closes overlay, else Back |
| Home, long | launcher | launcher |
| Power | sleep | sleep |
| Swipe ←→ | — | page |
| Swipe ↕ | — | previous / next **spine document** |
| Long-press a word | — | anchors a selection at that word |
| Tap, while selecting | — | sets the other end; then choose tags |

Two additions the first draft missed, both load-bearing:

**There must be a Confirm.** The first draft's table contained no input that
activated anything except the touchscreen, while claiming buttons were a
one-handed fallback. They were a scrollbar. Long-press Right is free — Left and
Right have no other long-press meaning.

**Back must not depend on the touch controller.** Home is a GT911 capacitive key
bit, so in the first draft both Back *and* the launcher died with the touch
controller — and Phase 0 deleted the remap settings screen, which is today's
recovery path. A GT911 that NAKs after an ESD event or a reset racing a panel
refresh would leave the user inside a settings sub-screen with no way out. The
Left+Right chord is hard-bound, routes through GPIO only, and needs no
configuration.

### Home precedence

"Home is always Back" has several claimants. The ladder, highest first:

1. **Modal overlay open** → dismiss it.
2. **Gesture in progress** (a half-made two-tap selection) → cancel the gesture,
   nothing else.
3. **Destructive operation in flight** (download, OTA) → Home is inert. OTA
   especially.
4. **ReturnStack non-empty** → return to where you jumped from.
5. Otherwise → up one level.

`ReturnStack` (`src/activities/reader/ReturnStack.h`) has `CAPACITY = 16` and
silently evicts the oldest on push. Promoting it to the device's primary Back
semantics without addressing that means that past the capacity, Back walks a path
the user did not take. Of the two answers this listed -- raise the capacity, or
show a visible affordance when the stack is non-empty so *Back* and *Return* are
distinguishable -- the first landed in #73, which moved the boundary well past
ordinary use without changing any semantics. The affordance is still open.
**Decide in Phase 2.**

### Navigation decisions

1. **The status bar is the jump control.** Tapping the book/chapter indicator
   opens the picker at the current position, so "where am I" and "take me
   somewhere" are one control.
2. **Long-press to start, tap to finish.** Long-press a word to anchor a
   selection there, tap another word to set the other end, then choose tags. Two
   deterministic refreshes, no lag semantics, and no mode to enter first — the
   long-press *is* the mode switch, so reading to tagged stays one continuous
   gesture. It is close to what `PassageSelectActivity` already does
   (`PassageSelectActivity.h:18-22`: a three-phase modal,
   `Phase { PickingStart, PickingEnd, ChoosingAction }`), differing only in how
   the first anchor is set — today it is reached from the reader menu or a
   long-press on Confirm/Home (`EpubReaderActivity.cpp:531-535,567-569,855`).

   A long-press on the page is otherwise unbound, so this costs nothing. It must
   not fire inside the centre menu zone, where a long contact would otherwise be
   ambiguous with the menu tap.

   Drag-to-select was considered and deferred: this unit's UC8179 is 1-bit with
   no grayscale and `HalDisplay` exposes no windowed black-and-white update, so
   the only drag feedback available is full-frame inversion of the selected run —
   the most expensive thing to redraw, several hundred ms behind the finger.
   Long-press-plus-tap keeps the gesture the product wants and pays two refreshes
   instead of one per finger sample.
3. **Vertical swipe moves by spine document.** Not "chapter": a Watchtower
   article, an `lff` lesson, and the NWT concordance have no chapters, and ~2,700
   of the NWT's spine documents have no verse markers either. Spine document is
   well-defined everywhere and coincides with chapter in the Bible.
4. **Home after a cross-reference returns to where you jumped from**, subject to
   the ladder above. This is a *change*: today that behaviour is wired to Back
   (`EpubReaderActivity.cpp:584-588`).
5. **The resume strip returns to the verse, not the book.**

### Orientation

`SETTINGS.touchReaderControls`' inverted-tap option and the side-button swap both
live in the layer being deleted. **Decision: portrait-only, and the four-orientation
line comes out of the testing section.** A fixed Left/Right mapping and
"supports four orientations" cannot both be true, and a device whose buttons
invert when you turn it over is worse than one that does not turn over.

## Data model

### What is wrong with the current model

`lib/Epub/Epub/HighlightEntry.h:10-19` anchors a mark to `spineIndex` plus a
codepoint offset, and states that `label` and `reference` are display-only and
must never be used to locate the passage. Correct for a general reader; wrong
here, because replacing the EPUB orphans every mark or leaves it pointing at
different text.

### Units, and where they actually are

Measured across four real publications (xhtml files containing each marker):

| publication | xhtml | `data-pid` | `data-pnum` | `chapter<N>_verse<M>` |
|---|---|---|---|---|
| `mwb_S_202601` | 21 | **9** | 8 | 0 |
| `w_S_202601` | 19 | **8** | 6 | 0 |
| `lff_S` | 161 | **79** | 54 | 0 |
| `nwt_S` | 3,937 | **1,342** | 0 | 2,445 |

Two corrections to the first draft:

**The attribute is `data-pid`, not `data-pnum`.** `data-pnum` sits on
`<span class="parNum">` — the *printed* paragraph number, a rendering artifact.
`data-pid` sits on the block element itself (`<p id="p7" data-pid="7">`). In
`mwb` the two sets are exactly complementary: every `data-pnum` is in a
`linear="no"` reference excerpt and the nine linear meeting documents carry
`data-pid` only. The first draft verified against the excerpts and generalised
from them.

(Those excerpt documents *are* reachable — `linear` is never read anywhere in
`lib/Epub/`, so the spine includes non-linear items. The problem was the
attribute, not reachability.)

**Not every location has a unit.** `lff` headings, summary boxes and captions on
666 images sit in no numbered paragraph, and ~2,700 NWT documents — front matter,
appendices, concordance, study notes — carry no verse marker. The first draft
claimed otherwise and had no case for the remainder.

### The address

```
Unit
  kind      Verse | Paragraph | DocumentOffset
  major     chapter (Verse) | 0
  minor     verse (Verse) | data-pid (Paragraph) | 0
  offset    codepoint offset — within the unit, or within the document
```

`DocumentOffset` is not a sentinel invented at the call site; it is a first-class
kind, and it is exactly what `HighlightEntry` stores today. The old model is the
correct degenerate case.

```
TaggedPassage
  pubkey         publication identity (below)
  document       filename inside the archive
  documentSpine  spine index, as a fallback if the filename moves
  start          Unit
  end            Unit
  fingerprint    length + CRC32 of the start unit's visible codepoints
  snippet        bounded passage text, for the tag list
  tags           global tag ids
```

**Resolution order** is filename, then spine index, then flag — the first draft
argued carefully for `pubkey` stability and then silently assumed the document
filename was stable too. Names like `202026008-extracted.xhtml` are JW's internal
document ids; whether they survive a corrected reissue under the same symbol is
**unverified, and must be checked against two issues of one publication before
Phase 1 freezes the format.**

**The snippet is not optional.** Without it the tag list cannot draw a row
without opening and paginating the publication, and there is nothing to verify
degradation against. `MAX_REFERENCE_BYTES = 48` is the right order of magnitude.

### pubkey

For non-Bible publications: symbol, issue tag and language (`mwb-202601-S`) —
derived from the three fields the catalog and `GETPUBMEDIALINKS` both key on, so
study data survives a re-download or a rename. This is deliberately unlike
CrossPoint's cache, which hashes the file path (`lib/Epub/Epub.h:48`, and four
other sites) and loses everything when a book moves.

**For the Bible, language is excluded: the pubkey is `bible`.** Salmos 119:145 and
Psalm 119:145 are the same verse. Keying Bible passages per-language would mean
switching language to compare a rendering makes every mark vanish — the single
most likely change for a bilingual user. The verse address *is* the identity;
language is a rendering choice. The tag list shows which language a non-Bible
passage belongs to.

### Degradation

On reopen, compare the stored fingerprint against the unit's current content:

| | Behaviour |
|---|---|
| Fingerprint matches, offsets fit | Paint the span. Normal. |
| Fingerprint matches, offsets don't fit | Paint the whole unit. Genuinely just coarser. |
| **Fingerprint differs** | **Paint nothing.** Surface in the tag list as *el texto cambió*, show the stored snippet, offer one-tap re-anchoring. |

The third row is the one the first draft got wrong. It degraded to "mark the
whole unit and flag it" in all cases — but if the text changed, the unit's
content is not what was tagged, so the mark is not coarser, it is about different
words. And the flag lives in a list the user has no reason to open while the ink
is on the page. Refusing to paint a claim the data cannot support is the only
honest option.

### On disk

```
/.berean/tags.json                 tags: id, name, retired flag; nextTagId; format version
/.berean/passages/<pubkey>.json    tagged passages for one publication
/.berean/tagindex/<tagid>.bin      reverse index: (pubkey, passageId) pairs
/.berean/units/<pubkey>.bin        unit index: header + per-document offset table
/.berean/migration-report.json     what the Phase 1 migration did
```

### Storage discipline — non-negotiable

`SDCardManager::readFile` (`freeink-sdk/.../SDCardManager.cpp:202`) hard-caps at
`constexpr size_t maxSize = 50000` and returns a **silently truncated** string,
read one byte at a time into an Arduino `String`. `PersistableStore::saveToFile`
uses `writeDocToFile` — the **non-atomic** variant (`PersistableStore.cpp:11`) —
while `writeDocToFileAtomic` exists beside it. This repo already carries the scar:
`src/util/HighlightFile.h:40-42`, `SAVE_BYTE_BUDGET = 45000`, commented as
headroom under that exact truncation.

Left unaddressed, the chain is: a year of tagging grows a passages file past
45 KB → it saves, because nothing checks → next boot it reads back truncated
mid-token → the parse fails → the store initialises empty → the next save
overwrites the real file with `{}`. Unrecoverable loss of the only data on this
device that cannot be re-downloaded.

Therefore, for every store this project introduces:

1. **Atomic writes only.** `writeDocToFileAtomic`, never `writeDocToFile`.
2. **An explicit serialised-byte budget, checked before writing**, modelled on
   `HighlightFileAction.h`. Refuse and report rather than truncate.
3. **Anything that can exceed ~40 KB does not use `Storage.readFile` at all.**
   Stream it. The catalog index and any large passages file are in this class.
4. **A format version that a future build refuses rather than reinterprets** —
   the discipline `HighlightDoc::fromJson` already has.
5. **A named owning task per store, and `storageMutex` held on write.**
   `PersistableStore.h` documents that the web server task saves settings while
   the main task can too, and `HighlightFile.h` warns that its helpers take no
   lock. This project adds background downloads and keeps the web server, so it
   has *more* concurrent writers than the model it replaces.

### Tag ids

Allocated once, never reused; deleting retires an id. That requires `nextTagId`
to be **persisted explicitly** and retired ids to be kept as tombstones (id, name,
retirement date). Otherwise the obvious recovery from a corrupt `tags.json` —
rebuild from the passages files — recovers used ids and loses retired ones, so
the next new tag gets an id that a passage somewhere still carries, and displays
as the wrong tag. That is the exact failure `HighlightDoc::removeTag`'s
renumbering was cited as fixing.

### Counts

Not stored. Computed when the tag list opens.

Denormalised counters in `tags.json` have no transaction with the passages files
and no reconciler, and `CrossPointWebServer.cpp:164` exposes `POST /delete` — so
anyone on the network can delete a passages file and the counters will never know.
A battery death between the two writes leaves a count permanently wrong. For a
device whose value is being a trustworthy record, a visibly wrong number is worse
than no number.

### The reverse index

`/.berean/tagindex/<tagid>.bin` exists because "show me everything tagged X" is
the primary reason global tags exist, and without it the query opens every
passages file. At 100 publications that is 100 × (directory scan + mutex acquire +
read + parse) — several seconds, on the render path, holding the SD mutex, with no
spinner on an e-ink panel. `PersistableStore.h` warns in as many words: locking
that mutex on a read path stalls rendering behind SD I/O. The index is appended on
tag and rewritten on untag — the same write, one more file.

### The unit index

Built **lazily, per document, on first use of that document** — not eagerly on
first open of the publication.

The first draft's eager build was, for the NWT: 3,941 documents × (SD read +
inflate + full expat parse with `VisibleOffsetCounter` per character) ≈ **10–13
minutes** at realistic per-document cost. The task watchdog fires at 5 s without a
`vTaskDelay`, and auto-sleep triggers on 10 minutes of *input* inactivity
(`src/main.cpp:652`), which a background build does not reset. One chapter's index
is ~30 ms, and it is exactly the document just opened.

**One file per publication**, with a fixed-width document offset table at the head
and a dirty bit per document. The first draft's file-per-document layout put 3,941
files in one FAT directory: with `USE_UTF8_LONG_NAMES=1` that is ~500 KB of
directory entries linearly scanned by every open, and ~126 MB of cluster slack on
a 32 KB-cluster card for ~2 MB of data.

**Invalidation** — the first draft specified none, and this repo has already been
bitten by exactly that: `BookMetadataCache.cpp:467` validates on cache version
only, never size or mtime, which is why replacing an EPUB in place kept serving a
stale TOC. The unit index is worse, because it is keyed on pubkey rather than file
path, so it does not even self-invalidate on a filename change. The header carries:

- format version
- source EPUB size and mtime
- per document: uncompressed length and a CRC32 of its visible codepoints

Any mismatch rebuilds that document. The content hash is the one that matters —
size and mtime both survive a same-length correction.

### Scanners

There are **two**, not one. The first draft claimed `VerseAnchors` covered the
whole library; `VerseAnchors.cpp:30-41` hardcodes `strcmp(atts[i], "id")`, a
`sscanf("chapter%u_verse%u%c")` grammar, a `break` after the first id, and
`anchors.reserve(176)` sized for Psalm 119. A `data-pid` scan needs a different
attribute, a different value grammar and a different anchor shape.

They share the `VisibleOffsetCounter` + three-expat-handler skeleton, which is
true of any expat scan in this repo. The first draft also justified reuse by
pointing at `ChapterHtmlSlimParser.cpp:108` (`span` is non-navigable inline) —
that is a real line, but it explains why verse *ids* are skipped by the
id-harvester, and `data-pid` is not an id, so it has no bearing.

**Order of work in Phase 1:** define the unified `Unit` type first, write the
`data-pid` scanner and its host tests second, and only then freeze the on-disk
record.

## Migration

From per-book `HighlightDoc` files to the global model. Resumable and idempotent
by construction:

1. **One source file at a time.** On success, rename it to `<name>.json.migrated`.
   One atomic rename per unit of work makes the whole pass resumable — a battery
   death mid-migration resumes exactly where it stopped.
2. **Never delete the old store.** It is the only rollback, and an OTA rollback
   to a Phase 0 build is a real path across `app0`/`app1`.
3. **Write `/.berean/migration-report.json`:** per source file, highlights in,
   passages out, and the reason for each drop. The web server already serves
   `/download`, so the result is inspectable without any UI.

Old `(spineIndex, offset)` pairs resolve through the freshly built unit index into
`Unit` addresses, degrading to `DocumentOffset` where no unit exists. Tag names
dedupe across books into the global store.

`ReturnStack`'s `SavedPosition` is `(spineIndex, pageNumber)` — file-relative, the
thing this section argues against. It is listed under carry-over below, but it
needs the same treatment before it is persisted anywhere.

## Buscar — the catalog

`GETPUBMEDIALINKS` resolves a publication you already name; it does not list what
exists. The list lives in the JW Library catalog:

- `https://app.jw-cdn.org/catalogs/publications/v4/manifest.json` → current id
- `.../v4/<id>/catalog.db.gz` → 57.6 MB gzipped, 214 MB SQLite, all languages

Measured 2026-09-13 (manifest `current` = `9c7204f8-45e7-43d8-afea-c14ecb035c55`):

| | |
|---|---|
| Publications, all languages | 319,871 |
| Publications, Spanish (`MepsLanguageId=1`) | 3,768 |
| Slim index — symbol, issue, year, type, title | 216,708 B (17,914 B gzipped) |
| Non-periodical only | 310 rows, 17,132 B |

Verified end to end: `lff` → `GETPUBMEDIALINKS?pub=lff&langwritten=S` →
`lff_S.epub`, 104,004,412 B. (Sizes in this document are decimal MB except where
noted; that file is 99.2 MiB / 104.0 MB.)

**Where the index lives at runtime.** Download ~18 KB gzipped, inflate **once into
PSRAM** via `lib/uzlib` on entering Buscar, scan RAM, free on exit. The first draft
conflated the 18 KB transfer with the 217 KB working set and implied an SD scan per
keystroke: `Storage.readFile` cannot even load it (it would silently truncate at
roughly row 850 of 3,768, reporting "not found" for three quarters of the catalog
with nothing logged), and a proper streaming read would still be ~217 ms of SPI per
character. In PSRAM it is single-digit milliseconds. **Debounce ~250 ms** — the
e-ink refresh, not the scan, is what the user feels.

**Publishing.** The index does **not** ship as a firmware release asset. An 18 KB
data change must not require a 5.6 MB OTA and an `app0`/`app1` flash cycle, and it
must not ride on the `releases/latest` semantics that already produced an
rc-versus-release ordering problem here
(`2026-09-13-fork-ota-releases-design.md:75`). Instead: a CI job publishes the
per-language indexes as static files, fetched with `ETag`/`If-Modified-Since`,
on their own schedule.

The CI job must not trust the manifest id alone. The `.gz` behind the unchanged id
above returned a *newer* `last-modified` at byte-identical length, so the id is
not reliably content-addressed. The job compares a content hash.

**Staleness must be visible.** The index header carries the manifest id it was
built from, its build date and its language. Buscar shows *Catálogo: 12 sep 2026*,
and *Actualización disponible* when the device's periodic `manifest.json` check
(cheap, same schedule as the OTA check) disagrees. A user told "not found" must be
able to distinguish "not published yet" from "your index is six months old" from
"the fetch failed."

**"Type a symbol" is always available**, independent of the index.
`GETPUBMEDIALINKS` resolves a named symbol directly (`src/network/PubMediaJson.cpp`),
which is the path already proven. That fallback survives the index being stale,
absent, or discontinued.

**Posture.** A recurring CI job pulling a 57.6 MB asset from `app.jw-cdn.org` and
republishing a derived index is a position worth naming: fetch at low frequency,
identify the client honestly, redistribute only symbols and titles — never
publication content — and keep the symbol path as the fallback that survives
losing the index.

## What carries over unchanged

The EPUB engine and renderer, FreeInkUI, `UiTabListActivity`, the HAL, the web
server, OTA, release-please, CI — and the Bible work already shipped:
`BibleNavScanner`, `BibleChapterNumber`, `NumberGridLayout`, `VerseAnchors`,
`ReturnStack`, `WolWeekScan`, `PubMediaJson`, `MeetingFilename`. Each has a host
test today (`test/bible_nav_scanner`, `test/number_grid`, `test/return_stack`,
`test/verse_anchors`, `test/wol_week_scan`, `test/pub_media_json`,
`test/meeting_filename`).

## Testing

The tag store, the `data-pid` scanner, the unified `Unit` type, the migration and
the catalog index parser are pure, dependency-free host-suite units — no Arduino,
no `HalStorage` — as `WolWeekScan` and `PubMediaJson` already are.

Device verification stays the human's: heap after each section, and a cache-clear
re-parse whenever a format version moves. **Not** four orientations — see the
orientation decision above.

## Error handling

`LOG_ERR` then return false; fall back rather than abort; never throw. A failed
catalog fetch leaves the previous index in place and says so. A failed unit-index
build leaves the publication readable with addressing degraded to
`DocumentOffset`, and says so. **A failed passages write must never leave a
partial file** — hence atomic writes — and must report to the UI rather than fail
silently.

**Power.** Auto-sleep triggers on input inactivity (`src/main.cpp:652`), which a
download does not reset, and `enterDeepSleep` would call `esp_deep_sleep_start`
mid-write. Suppress auto-sleep while any network operation is in flight, download
to `<name>.part` and rename on completion, and refuse to start a large download
below a battery threshold.

**Card full.** Every berean write path reports failure to the UI. The download
pre-flight check from the publication-download design extends to cover them.

**OTA across the format change.** Phase 1 changes storage while `app0`/`app1`
rollback to a Phase 0 build remains possible. Hence: never delete
`/.crosspoint/highlights/`, and stamp `/.berean/tags.json` with a format version
that an older build refuses rather than reinterprets.

## Build order

| | What | Acceptance |
|---|---|---|
| **0** | Delete OPDS, KOSync, dictionary, TXT, XTC and the other board targets. Rename to `berean-os`. Carry CI, release-please, OTA. **The input layer is not touched.** | Binary size before/after; every remaining activity entered and exited once on hardware |
| **1** | Unified `Unit` type, `data-pid` scanner, unit index, tag store, reverse index, migration | `migration-report.json` served over the web server; old store retained |
| **2** | Launcher shell, four sections, the new input model, two-tap selection, `MappedInputManager` deleted | The UX, diffable against the layer it replaces |
| **3** | Buscar: catalog CI job, PSRAM index, search, download | Any publication, on device |

Two ordering corrections from review:

**Phase 0 does not delete the input layer.** `MappedInputManager` is 418
references across 121 files, sits in the `Activity` base-class constructor
(`src/activities/Activity.h:12,22,28-29`), and *implements* this device's Back —
`MappedInputManager.cpp:266,301` routes a left-edge swipe to `Button::Back`, and
`PassageSelectActivity.h:25-28` documents depending on it. Deleting it in Phase 0
would leave the device with no Back for the whole of Phase 1, against an
acceptance criterion of "same behaviour" that could not be met. It moves to Phase
2, where its replacement lands in the same change.

**Phase 1 must be inspectable.** "It has to be right the first time" and "no UI
until Phase 2" contradict each other. The migration report is the answer, and the
web server already serves it.

BLE keyboard follows Phase 3, and needs a flash budget first.

**Each phase gets its own implementation plan.** Phase 0 is a deletion, Phase 1 a
data migration, Phase 2 UI, Phase 3 a network subsystem with a CI half. They share
no execution shape.

## Open items

- **Document-filename stability under a corrected reissue.** Partially resolved:
  comparing `mwb_S_202601` (`202026000…008`) against `mwb_S_202603`
  (`202026080…088`) shows the names are issue-scoped ids assigned by JW's content
  system, not positional slots — different issues never collide, and a different
  issue is a different `pubkey` anyway. That makes an id likely to survive a
  correction, but it is not proof, and no reissue has been available to compare.
  The spine-index fallback in the record stands on this uncertainty rather than
  on caution. Revisit if a reissue appears.
- **`ReturnStack` capacity** (raised 3 -> 16 in #73; eviction is still silent)
  before it becomes primary Back. The capacity half is settled; whether Back and
  Return need to be visibly different is not. Phase 2.
- Which languages CI builds catalog indexes for. Spanish required; English nearly
  free.
- Whether Buscar defaults to the 310 non-periodicals with periodicals behind a
  filter. Reuniones already covers periodicals, which argues for the filter.
- Retention policy for downloaded publications on a 16 GB card.
- Whether a passage can exist with zero tags ("mark it, label it later"). The
  current model says no — untag to zero and the record is gone — which forces
  taxonomy at the moment of reading. A reserved tag id 0 would represent it
  without special-casing.
  *Resolved (#32): `study::UNLABELLED`, stored as the v1 empty tag array; see
  `2026-09-23-untagged-passages-design.md`.*

## Evidence

Catalog and network measurements taken 2026-09-13 against the live endpoints.
Publication markup measured against `mwb_S_202601`, `w_S_202601`, `lff_S` and
`nwt_S`. Flash figures from `.pio/build/x4pro/firmware.map` and the published
`firmware-x4pro.bin`. Hardware facts cited to
`freeink-sdk/docs/xteink-x4pro-support.md`, which marks them confirmed on the
bench. Every code citation in this document was verified by adversarial review on
2026-09-13.
