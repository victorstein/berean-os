# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 10

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 10
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 37

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 37 increases the fixed-size footnote href field from 96 to 256 bytes.
This changes each serialized footnote record from 128 to 288 bytes, so older
section caches must be discarded and rebuilt.

Version 36 invalidates cached word positions after ruby and CJK justification
layout changes.

Version 35 adds a header offset and a `uint32_t` entry per page for the
visible-text offset LUT. The other section LUTs remain unchanged.

Version 34 is binary-identical to version 33. The version was bumped because
word-gap suppression was narrowed to tokens glued together in the source: v33
dropped the gap between any two words meeting at a CJK break opportunity, which
collapsed the spaces between Hangul words, so v33 word positions no longer match
what the layout engine now produces.

Version 30 is binary-identical to version 29. The version was bumped because
Arabic contextual shaping changed text measurement (`getTextAdvanceX` now
measures the shaped visual text), so word positions cached by v29 no longer
match what `drawText` renders.

Version 28 introduced serialized word style bits for underline, strikethrough,
superscript, and subscript. The format also includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- per-page visible-text offset LUT (zero-based Unicode codepoints in `<body>`)
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs retained for navigation and legacy sync fallback
- optional per-word Focus Reading split metadata
- per-page footnote entries
- serialized word style bits for underline, strikethrough, superscript, and
  subscript
- flat TextBlock word storage (v29): per-word arrays plus one shared
  NUL-terminated text blob, replacing v28's length-prefixed word strings. The
  on-disk order mirrors the in-RAM arena so the firmware reads a whole block
  payload with a single allocation and a single SD read

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 37
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 256

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
    }

    BlockStyle blockStyle;
};

struct ImageBlock {
    String imagePath;
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount;
    FootnoteEntry footnotes[footnoteCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphAlignment;
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering;
    bool focusReadingEnabled;

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;
    u32 visibleTextLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }

    if (visibleTextLutOffset != 0) {
	u32 visibleTextOffset[pageCount] @ visibleTextLutOffset;
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `/.berean/completion/<pubkey>.json` (retired)

Which Bible chapters the user had paged through. **No longer written or read** since #195: the
firmware never opens this path, and it never deletes existing files either. They stay on the card,
are harmless, and are safe to delete by hand. The path is reserved (`sdpaths::COMPLETION_DIR`), so
no new format may reuse it. It was written by the removed `ChapterCompletion` code (#78, retired in
#195). Only the shared Bible key (`bible`) was ever written, so in practice the file is
`/.berean/completion/bible.json`.

### Version 1

```json
{"v":1,"b":{"1":"0102","19":"ff"}}
```

- `v` — format version.
- `b` — one entry per canonical book (1-66, `biblebooknav.xhtml` order) with at
  least one chapter read. Books with nothing read are omitted.
- Each value is lowercase hex, two digits per byte, trailing zero bytes trimmed.
  Byte `k`, bit `j` (LSB = 0) is chapter `8k + j + 1`. `"0102"` above is Genesis
  1 and 10; `"ff"` is Psalms 1-8.

Firmware from #78 to #195 refused a larger version rather than overwrite it, rejected the whole file
for any key or value it would not write (a book outside 1-66, a bit past the book's last chapter
under English versification, 1,189 chapters, non-hex, an odd length), and budgeted saves at 4,096
bytes. Every chapter read serialises to under 1 KB.

## The `/.crosspoint/*.json` stores (shared rules)

Four JSON files inherited from CrossPoint, each a `PersistableStore` singleton
(`lib/Serialization/PersistableStore.h`). All four follow the same version rule:

- `v` — format version, written first. A file with no `v` was written before versioning
  and reads as 1. Any other value this build does not know (0, a negative, or a number
  above its own) is refused: the store runs on its defaults, and every save of that store
  is refused until a later load succeeds or finds no file. A build that knows the format
  then reads it intact. The rule lives in `lib/Serialization/FormatVersion.h`.
- Every save goes through `saveToFileAtomic()`: a temp file, then a rename, after a byte
  budget check.

Unparseable or unreadable files are not covered by the version rule. Those files load as
defaults and are overwritten by the next save.

## `/.crosspoint/settings.json`

Owned by `src/CrossPointSettings.{h,cpp}`. One key per `SettingsList.h` row, using that
row's `key` (an obfuscated string row is stored as `<key>_obf`), plus the hand-written
`frontButtonBack`, `frontButtonConfirm`, `frontButtonLeft`, `frontButtonRight`,
`fontFamily`, `fontSize` (a point size), `sdFontFamilyName` (only when set),
`longPressMenuFunction` and `language` (an ISO code such as `"EN"`).

### Version 1

- `v` — format version. Absent reads as 1. Any other value this build does not know is
  refused: the store runs on defaults and is not written until a later load succeeds or finds
  no file (`lib/Serialization/FormatVersion.h`).

```json
{"v":1,"sleepTimeoutMinutes":10,"fontFamily":0,"fontSize":14,"language":"EN"}
```

Values are clamped to their row's range on load. Persisted enums keep their numeric
values. Older shapes (`sleepTimeout`, a font size of 0-3, the OpenDyslexic family slot)
are upgraded in memory and resaved. Save budget: 4,096 bytes.

## `/.crosspoint/state.json`

Owned by `src/CrossPointState.{h,cpp}`. Runtime state: `openEpubPath`,
`bibleCoverPath`, the sleep-image history (`recentSleepImages` and
`recentOverlaySleepImages`, 16 entries each, with their `…Pos` and `…Fill` cursors), the study
sleep screen's history (`recentStudySleep`: 16 32-bit passage keys, FNV-1a over
`pubkey/start-unit/end-unit`, with `recentStudySleepPos` and `recentStudySleepFill`; absent in
files written before it and read as empty),
`readerActivityLoadCount`, `lastSleepFromReader` and `showBootScreen`.

### Version 1

- `v` — format version. Absent reads as 1. Any other value this build does not know is
  refused: the store runs on defaults and is not written until a later load succeeds or finds
  no file (`lib/Serialization/FormatVersion.h`).

```json
{"v":1,"openEpubPath":"/books/nwt_S.epub","bibleCoverPath":"","recentSleepImages":[0,0],"recentSleepPos":0,"recentSleepFill":0,"showBootScreen":true}
```

(Arrays shortened here; the file always writes 16 entries.) A legacy `lastSleepImage` is
read into the history when the history is empty. Save budget: 2,048 bytes.

## `/.crosspoint/wifi.json`

Owned by `src/WifiCredentialStore.{h,cpp}`. Loaded at boot alongside the other three.

### Version 1

- `v` — format version. Absent reads as 1. Any other value this build does not know is
  refused: the store runs on defaults and is not written until a later load succeeds or finds
  no file (`lib/Serialization/FormatVersion.h`).

```json
{"v":1,"lastConnectedSsid":"Home","credentials":[{"ssid":"Home","password_obf":"…","password_len":8,"password_crc32":305419896}]}
```

- `credentials` — at most 8. `password_obf` is the password XORed with a device-bound key
  and base64-encoded (`lib/Serialization/ObfuscationUtils`). It is not encryption.
- `password_len` and `password_crc32` let the loader discard a value that decodes but is
  corrupt. An entry without them, or with a plaintext `password`, is accepted and resaved
  in the current shape.

Passwords over 64 bytes are dropped on load. Save budget: 8,192 bytes.

## `/.crosspoint/recent.json`

Owned by `src/util/RecentBooksDoc.{h,cpp}` (format, host-tested) and
`src/RecentBooksStore.cpp` (storage).

### Version 1

- `v` — format version. Absent reads as 1. Any other value this build does not know is
  refused: the store runs on defaults and is not written until a later load succeeds or finds
  no file (`lib/Serialization/FormatVersion.h`).

```json
{"v":1,"books":[{"path":"/books/w_S_202601.epub","title":"La Atalaya","author":""}]}
```

At most 10 books. `title` and `author` are capped at 128 and 96 bytes on a codepoint
boundary; `path` is never shortened. The save budget, 9,967 bytes, is derived from those
caps (`RecentBooksDoc::worstCaseBytes()`).

Files written before #152 also carry `"coverBmpPath"`. It is ignored on load and dropped by the
next save. Removing it did not move the version, because no build loses or misreads anything: this
build ignores the key, and an older build reads the missing key as `""`, which is what it already
stored for a book with no thumbnail. A bump would only make a rolled-back build refuse the file and
refuse every recents save. The rule is the one `PassageDoc` follows
(`lib/StudyStore/StudyStore/PassageDoc.h:20-24`): bump only when an older build would lose or
misread data.

## `/.berean/passages/<pubkey>.json`

One publication's tagged passages. Owned by `lib/StudyStore/StudyStore/PassageDoc.{h,cpp}` (format,
host-tested in `test/passage_doc/`) and `src/study/PassageFile.cpp` (storage, atomic writes). Read
through a streaming parser, never `Storage.readFile`. Save budget: 200,000 bytes.

A file is written at the **lowest version that holds everything in it**, and a build refuses any
version newer than it knows (`lib/Serialization/FormatVersion.h`), so an older build never loads a
file only to drop a field and erase it on its next save:

| `v` | Written when | Adds |
| --- | --- | --- |
| 1 | no passage has links or a text | — |
| 2 | some passage has links, none a text | `"k"` |
| 3 | some passage has a legacy text, none a whole-verse one | `"w"` (121..384 bytes) |
| 4 | some passage has a whole-verse text | `"h"`, and an uncapped `"w"` |

```json
{"v":4,"p":[{"u":"v:48:5:4:12","e":"v:48:5:4:40","f":"134:a1b2c3d4","d":"1001061152-split5.xhtml","s":1152,
  "x":"Ustedes, los que tratan de ser declarados justos por medio de la ley, están separados de Cristo. Se han apartado de su",
  "w":"Ustedes, los que tratan de ser declarados justos por medio de la ley, están separados de Cristo. Se han apartado de su bondad inmerecida.",
  "h":true,"r":"Gálatas 5:4","t":[3]}]}
```

Row keys: `u` start unit, `e` end unit, `f`/`fe` start and end fingerprints, `d` document and `s`
spine (resolution hints), `x` snippet (≤ 120 bytes, for lists), `w` text, `h` whole-verse flag,
`r` reference (≤ 48 bytes), `t` tag ids (empty = unlabelled), `g` pending upgrade, `k` outgoing
links (≤ 8, each `u`/`s`/`r`).

`w` with `h:true` (v4) is the passage's **whole text**: the complete verse(s) its selection touches,
even when the selection starts or ends mid-verse, without verse numbers, footnote markers,
footnotes or acrostic headings. It is normalised (whitespace collapsed, trimmed) and has no cap of
its own: it is bounded by the file's 200,000-byte save budget and by ArduinoJson's 65,535-byte
string limit. A passage past either is refused, never cut. `x` is its first 120 bytes. A row with `h:true` and an empty or missing `w` fails
the whole load.

`w` without `h` is a **legacy** text, 121..384 bytes as v3 wrote it; any other length fails the whole
load. Such rows, and rows with no `w` at all, are rebuilt to whole-verse text when their publication
is opened (`StudyStore::repairTexts`, a few seconds per open until done). Until then the Study sleep
screen does not show them. A row whose verses cannot be resolved, or whose fingerprint no longer
matches the open edition, is left exactly as stored.

## `/.berean/search/bible.idx`

The Bible verse search index. Owned by `lib/BibleSearch/BibleSearch/IndexFormat.{h,cpp}`
(layout), `IndexBuilder`/`IndexReader` (write and read), and `src/study/BibleSearchIndexer.cpp`
/ `BibleSearchStore.cpp` (build and storage). Derived data: deleting it only costs a rebuild,
which the search screen offers. A build in progress checkpoints to `bible.partial` in the same
directory, in the same format with the complete flag clear, every 300 documents and on cancel.
Both files are written as `<path>.tmp` and then renamed into place. A checkpoint for another
Bible (a fingerprint mismatch), or with the complete flag set, is refused as a resume point.

### Version 1

Little-endian throughout; every field is copied with `memcpy`, never read unaligned.

| Section | Size | Content |
|---|---|---|
| Header | 48 B | see below |
| Verse table | `verseCount` × 9 B | canonical order: `book` u8 (1-66), `chapter` u8, `verse` u8, `spine` u16, `offset` u32 (VerseAnchors' visible-codepoint offset in the spine document) |
| Term table | `termCount` × 10 B | sorted by folded bytes (`memcmp`): `stringOffset` u32, `postingsOffset` u32, `postingCount` u16 |
| Term strings | variable | folded UTF-8, each NUL-terminated, in term-table order |
| Postings | variable | per term, in term-table order: ascending global verse numbers as LEB128 deltas (at most 3 bytes each), the first taken from 0 |

Header:

| Offset | Field |
|---|---|
| 0 | magic `BSIX` |
| 4 | `formatVersion` u16 |
| 6 | `flags` u16 (bit 0 = complete) |
| 8 | `fingerprint` u64 — FNV-1a over the EPUB size, spine count, verse-document hrefs and their count |
| 16 | `verseCount` u32 (at most 65,535) |
| 20 | `termCount` u32 |
| 24 | `docsDone` u32 — spine documents indexed; meaningful in a checkpoint |
| 28 | `verseTableOffset` u32 |
| 32 | `termTableOffset` u32 |
| 36 | `termStringsOffset` u32 |
| 40 | `postingsOffset` u32 |
| 44 | `fileSize` u32 — must equal the real file size |

A newer `formatVersion` is refused, never reinterpreted; an older one, a size mismatch, or any
offset or count outside the file makes it unreadable. A fingerprint mismatch makes it stale.
Neither is overwritten until the user confirms a rebuild. The Spanish NWT indexes to 31,078
verses, 23,568 terms and 1,469,729 bytes; the write budget is 8 MB.

## Catalog index (`catalog-<lang>.txt`, `catalog-<lang>.v2.txt`)

Published on the `catalog` GitHub release by `.github/workflows/catalog-index.yml`,
cached on the card as `/.berean/catalog-<lang>.idx`, and inflated into PSRAM while
Buscar is open. UTF-8 text, one record per line, fields separated by tabs. The
title is always the last field, so a tab inside a title can only land inside it.

Header, both versions:

    berean-catalog\t<version>\t<language>\t<manifestId>\t<builtOn YYYY-MM-DD>

### Version 1 (`catalog-<lang>.txt`)

    symbol\tissue\tyear\tkind\ttitle

`issue` is `YYYYMMDD` (semimonthly or weekly), `YYYYMM` (monthly), or empty for a
book. `kind` is `periodical` or `book`. This version is still published unchanged
for firmware that predates version 2 (v1.17.3 and older), which refuses any other
version.

It stops being published once both hold: it is on or after 2026-12-28, 13 weeks
after v1.17.4 (the first release that reads version 2) was published, and no
supported device is still on v1.17.3 or older. Release download counts cannot
show this, because the workflow re-uploads every asset weekly and that resets
them. To retire it (the same checklist is in the header of
`.github/workflows/catalog-index.yml`):

1. In `.github/workflows/catalog-index.yml`, drop `--out-v1`/`--previous-v1` from
   both builder calls, the `previous-$lang.txt` fetch, and `"catalog-$lang.txt"`
   from `publish()`'s indexes, leaving the v2 files there outside `hold`.
2. Delete the assets, one per call:
   `for a in catalog-S.txt catalog-S.txt.gz catalog-E.txt catalog-E.txt.gz; do gh release delete-asset catalog "$a" --yes --repo victorstein/berean-os; done`
3. In `scripts/build_catalog_index.py`, remove `--out-v1`/`--previous-v1`, the
   legacy `render_index(..., LEGACY_FORMAT_VERSION, ...)` and its write,
   `LEGACY_FORMAT_VERSION`, and the v1 wording in the module and `render_index`
   docstrings; drop their tests in `scripts/tests/test_build_catalog_index.py`.
4. Update this section and the comment in `CatalogIndexStore::assetUrl()`. The
   device still accepts a v1 file, so no firmware change is needed.

### Version 2 (`catalog-<lang>.v2.txt`)

    symbol\tissue\tyear\tkind\tepub\ttitle

`epub` is `1` (jw.org publishes an EPUB), `0` (probed, no EPUB), or empty (not yet
probed). Buscar never lists a `0` row. Firmware since issue #157 reads versions 1
and 2, and refuses anything newer (`catalog::indexAcceptable`).

### Probe cache (`catalog-<lang>.probes.tsv`, CI only)

    berean-probes\t1\t<language>
    symbol\tissue\tepub\tprobedOn

Never read by the device. It holds the builder's answers so that each entry is
asked about once. A cache with any other header, or a malformed row, fails the
build rather than being adopted.
