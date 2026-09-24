# Bible book grid with the publication's abbreviations

**Date:** 2026-09-23
**Status:** Design approved by the user
**Builds on:** `2026-09-13-bible-number-grid-design.md` (chapter and verse grids)

## Goal

The book level of `BibleNavigationActivity` is still a vertical list of full names
(`BibleNavigationActivity.h:21`). Reaching a book near the end of either testament means
scrolling through dozens of rows, while the chapter and verse levels already page as number
grids. The book level becomes a grid of abbreviated names, one page per testament.

The number-grid spec left books as a list because "a grid would force hard truncation or
abbreviations". Abbreviations turn out to be free: the publication already has them.

## Where the abbreviations come from

`biblebooknav.xhtml` in the NWT EPUB is itself a five-column table of abbreviations in the
publication's language, grouped under section headings. Measured in `nwt_S.epub`:

- 66 `<a>` links in canonical order; their text is the abbreviation: `Gén.`, `Éx.`, `1 Sam.`,
  `2 Juan`, `Apoc.`. Longest is `1 Crón.` / `2 Crón.`, 7 characters.
- Two `<strong>` headings: `Escrituras Hebreas` before book index 0, `Escrituras Griegas`
  before book index 39.

`BibleNav::Scanner` (`lib/Epub/Epub/BibleNavScanner.cpp:20-34`) already walks this page but
keeps only each link's `href` tail. It will keep the link text and the headings too.

Rejected alternatives:

- **Truncating the TOC name** collides ("Fil." is both Philippians and Philemon; "Jud"/"Jue"/
  "Juan") and gets the language's conventions wrong.
- **A built-in abbreviation table per language** needs maintaining for every downloadable
  language and can disagree with the publication.

## Design

### 1. Scanner: link text and section headings

`BibleNav::Scanner` gains character-data handling:

- **Link text:** text inside each `<a>`, with whitespace trimmed and collapsed, stored
  parallel to the existing link targets. An `<a>` with no text yields an empty string, keeping
  the two vectors the same length. An `<a>` without `href` yields neither a target nor a label.
- **Section headings:** text inside each `<strong>`, recorded with the index of the next link
  (the first book in that section). An `<a>` inside a `<strong>` stays a link; the heading keeps
  only the text around it.
- **Opt-in:** text is collected only by a scanner constructed with `collectText = true`, which
  only `loadBooks()` does. Every other scanner, including `loadChapters()` and the per-book
  scanners in `UnitIndexCache`, reserves and collects targets alone, as before.
- **Unchanged API:** the existing `take()` still returns only the targets, so the chapter-nav
  path (`loadChapters`, `dropBookNavLinks`) is untouched. The new data comes from a separate
  accessor, `takeBookNav()`. A failed feed still empties everything.
- **Bounds:** link and heading text is capped at 47 bytes (`MAX_TEXT_BYTES`), cut on a UTF-8
  boundary, so a malformed page cannot grow memory without limit. The link count stays capped at
  `MAX_LINKS_PER_PAGE`.

### 2. Book level becomes a grid

- **One page per section.** Page 1 holds the Hebrew Scriptures (39 books), page 2 the Greek
  Scriptures (27). The section title and a page indicator (`1/2`) sit in a band under the
  header. The band is drawn only when the page has headings (`sectionCount > 0`), in
  `drawFooter`: `UiListActivity::render` draws the chrome before the grid is built, so only
  after the build is the current layout known.
- **Columns follow the labels, not a constant.** The column count is the largest that fits the
  widest abbreviation, measured with the body font plus cell padding. It is clamped, and rows
  follow from the section size. In Spanish on the 480-wide portrait panel this is about 5
  columns (8 rows, then 6). No screen dimension is hardcoded.
- **Page size limit.** Each page must stay within `NumberGrid::MAX_CELLS` (48,
  `NumberGridLayout.h:19`), which keeps it inside the 64-interaction budget
  (`UiAppHost.h:35`). A section larger than one page (not the case for the NWT) pages within
  itself. A layout holds at most 12 pages; a rect too thin to cover every book within them is
  reported through `coveredBooks` and a `LOG_ERR`, and selection never moves past the last
  covered book.
- **Navigation.** Swipe up and down changes the page and stops at either end. The Previous and
  Next nav buttons move the selection by a row; a row step across pages keeps the column
  (`BookGrid::stepRow`) and stops at Genesis and Revelation. A held button pages by testament
  and wraps from the last page to the first and back. The chapter and verse levels keep
  `ButtonNavigator`'s wrapping held paging. This device has no Up/Down buttons. Tapping a book
  opens its chapter grid, or its verse grid for the five single-chapter books, as today.
- **Cell labels point into stored text.** Each cell label points directly into the stored
  abbreviation array, so the book level uses no per-cell copy.
- **Pure arithmetic in a new header.** Section-to-page mapping and the column choice go in
  `src/activities/reader/BookGridLayout.h`, beside `NumberGridLayout.h`, free of FreeInkUI,
  Arduino and GfxRenderer, so the host suite can exercise them. It also owns `pageOf` and
  `stepRow`. The column choice takes the widest label width in pixels as an input.
- **Fallback glyphs are prewarmed.** When the layout is built, every label's fallback glyphs
  are prewarmed in one SD pass (`prewarmFallbackText`), so repaints under heap pressure stay
  RAM-only.

### 3. Full names in the headers

- **Chapter grid:** the header shows the full TOC book name (`Sofonías`) instead of the
  generic "Select chapter" (`BibleNavigationActivity.cpp:437`).
- **Verse grid:** the header shows the book and chapter (`Sofonías 2`).
- **Single-chapter books:** the header shows the book and `1`.
- **Name source:** the full name is the TOC title the activity already stores in `bookName[]`
  (`BibleNavigationActivity.cpp:92-98`).

### 4. Fallbacks

| Condition | Behaviour |
|---|---|
| A link has no text | That cell shows the full TOC name, truncated UTF-8-safely to 15 bytes (not to the cell's pixel width); its measured width feeds the column choice |
| No headings | The grid pages continuously through all books, with no band and no page indicator |
| Headings present but unusable (not starting at book 0, not increasing, or out of range) | The grid pages continuously; the band shows an empty title and the page indicator |
| The books need more than 12 pages | The layout stops at 12 pages; the shortfall is logged and selection stays on the covered books |
| Book-nav page unreadable | The existing `loadBooks()` error path, unchanged |

### 5. Memory

- **Added:** all fixed storage inside the heap-allocated activity, with no per-repaint
  allocation:
  - `bookAbbrev[66][16]` (1,056 B);
  - four section titles of 48 B (192 B) and their four start indexes;
  - the cached `BookGrid::Layout`, under 128 B, and its cache key (body width and height, book
    count, and the load generation `loadBooks()` bumps as it finishes);
  - the 56 B `headerTitle` for the verse header.
- **Freed at the book level:** the list window (`windowLabels[24]` std::strings,
  `windowItems[24]`, `BibleNavigationActivity.h:71-75`) is no longer needed, because every level
  is now a grid. It is removed along with `refreshRowWindow`, and `cells[48]` is reused.
- **Net:** roughly zero, and fewer small heap allocations than the std::string window.
- **Scanner:** only the book-nav scan collects text and reserves its labels. The chapter-nav
  scans in `loadChapters()` and `UnitIndexCache` cost what they did before. There is no format
  change and no cache change.

### 6. Testing

Host tests:

- **Scanner:** abbreviations and headings extracted from a verbatim excerpt of `nwt_S`'s
  `biblebooknav.xhtml`, including:
  - accented text and leading-space `<a>`s;
  - a missing text;
  - a page with no headings;
  - a malformed page, which yields empty output;
  - the existing target-only behaviour, unchanged.
- **Section and geometry helper:**
  - 39/27 split into two pages;
  - no sections, which pages continuously;
  - an oversized section;
  - column choice across label widths;
  - every page stays at or under `MAX_CELLS`.

On the device:

- labels fit and are legible;
- tapping opens the right book;
- swiping switches testament;
- buttons move by a row and a page;
- the chapter and verse headers show the full name;
- single-chapter books work;
- heap is stable across open and close.

## Out of scope

- **Per-book reading progress on the cells.** `ChapterCompletion::readCountInBook()` from #35
  makes it cheap later.
- **Any change to chapter or verse behaviour** beyond their header text.
- **The reader's other navigation surfaces** (the TOC and chapter selection).
