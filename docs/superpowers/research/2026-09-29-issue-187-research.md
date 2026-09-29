# Issue #187 research — open Select chapter at the current book's chapter grid

Branch `feature/187-select-chapter-current-book`, base `a4e2288d` (release 1.19.5). Every claim
below cites a line read on this branch or a command run on 2026-09-29.

## Which files own the behaviour

| File | Role |
|---|---|
| `src/activities/reader/EpubReaderActivity.cpp:714-742` | `SELECT_CHAPTER` handler. Captures `spineIdx = currentSpineIndex` (`:715`), then builds `BibleNavigationActivity(renderer, mappedInput, epub)` **without** it (`:728-729`). The non-Bible branch passes `spineIdx` (`:731`). Cancel reopens the reader menu (`:733-735`). |
| `src/activities/reader/BibleNavigationActivity.h` | `Level level = Level::Book` (`:47`). State the mapping can reuse: `bookTargetSpine[MAX_BOOKS]` (`:53`), `bookIsDirect[]` (`:56`), `selectedBook` (`:76`), `chapterSpine[MAX_CHAPTERS]` (`:78`), `chapterCount` (`:79`), `selectedChapterRow` (`:83`). The constructor takes only `epub` (`:29`). |
| `src/activities/reader/BibleNavigationActivity.cpp` | `onEnter` (`:36-50`), `loadBooks` (`:52-106`), `loadChapters` (`:108-136`), `enterLevel` (`:188-204`), `activateIndex` (`:228-254`), `onBackButton` (`:348-366`), `drawChrome` (`:507-526`). |
| `src/activities/UiListActivity.cpp:47-51` | `handleButtons`: `wasReleased(Back)` → `onBackButton()`. |
| `src/MappedInputManager.cpp:264-265` | `wasReleased(Back)` is also true on the left-edge back gesture, so the swipe and a Back press take the same path into `onBackButton`. |

## Current control flow

1. Reader menu → `SELECT_CHAPTER` (`EpubReaderActivity.cpp:714`). `releaseSectionKeepingPosition()`
   (`:722`), then the Bible gate `epub->getBibleBookNavSpineIndex() >= 0` (`:727`), which is memoised
   (`Epub.h:89-91`).
2. `BibleNavigationActivity::onEnter` (`:36`): `UiListActivity::onEnter()` resets nav and calls
   `requestUpdate()` (`UiListActivity.cpp:19-26`), clears the font cache (`:43-45`), then
   `loadBooks()` (`:47`). That is one SD stream of `biblebooknav.xhtml` plus one
   `resolveFilenamesToSpineIndices` pass (`:61`, `:76`). `level` stays `Book` and `nav.selected`
   stays 0.
3. The render is **deferred**: a non-immediate `requestUpdate()` only sets `requestedUpdate`
   (`ActivityManager.cpp:289-293`), and the render task is notified after the current loop pass
   (`:167-172`). Any extra loading done synchronously in `onEnter` is therefore finished before the
   first paint, and no book grid is drawn and then replaced.
4. Tapping a book (`activateIndex`, `:232-246`) sets `selectedBook`. A direct book opens its verse
   list (`:236-238`). Otherwise `loadChapters(index)` streams that book's chapter-nav page and
   resolves its links into `chapterSpine[]` (`:108-136`), then `enterLevel(Level::Chapter, 0)`
   (`:245`).
5. `enterLevel` (`:188-204`) switches level under `RenderLock`, clamps `selected` and calls
   `placeSelectionLocked`, which puts `nav.top` on the page holding the selection (`:263-268`).
6. Back (`:348-366`): Chapter → `enterLevel(Level::Book, selectedBook)` (`:353-354`); Book →
   `cancel()` (`:350-351`); Verse → Chapter, or Book when `selectedChapterRow < 0` (`:356-364`).
7. The chapter header is `bookNames.at(selectedBook)` (`drawChrome`, `:511-512`). It already
   depends only on `selectedBook`.

## How spine index maps to (book, chapter), measured

I found no NWT EPUB on disk (`find /Volumes/stein -iname "nwt*.epub"` returned nothing), so I
downloaded both from the catalog URL the 2026-09-12 spec cites. The sizes match that spec's
measurement: EN 15,652,378 B, sha256 `2d406bb4…`; ES 14,945,282 B, sha256 `f8ff312c…`. A Python
script (scratchpad `spine.py`) parsed `content.opf`, `biblebooknav.xhtml`, every
`biblechapternav*.xhtml` and `toc.xhtml`, joining on filename tails as the firmware does. The
results were identical for EN and ES:

```
spine 3941 (ES 3937)      books 66      booknav spine 2
bookTargetSpine ascending: True
chapters 1189 ascending True
gaps [] 0                 # every book's chapters are contiguous, and a multi-chapter
                          # book's nav page sits immediately before its chapter 1
range-test violations []  # book i's chapters all lie in [bookTargetSpine[i], bookTargetSpine[i+1])
first/last 29 1343        # Genesis nav page at spine 29, Revelation 22 at spine 1343
spine after rev ['1001061171.xhtml', '1001061172.xhtml']   # appendices follow the Bible
toc covering-entry mismatches 0
```

What follows from that:

- **Book lookup is pure arithmetic on `bookTargetSpine`**, which `loadBooks` already fills
  (`:80-83`). The book is the last `i` with `bookTargetSpine[i] <= spine`. Anything before
  `bookTargetSpine[0]` (front matter, and the book-nav page at spine 2) maps to no book.
- **The last book has no upper bound in `bookTargetSpine`.** Spines after Revelation 22 (1344+)
  are appendices, and the range rule alone would assign them to Revelation. The bound has to come
  from the chosen book's `chapterSpine[]`: `loadChapters(book)` resolves that array, and a spine
  absent from it is not a chapter. This one stream of a chapter-nav page (738 B to 13,940 B, per
  the 2026-09-12 spec) is the same work `activateIndex` does when the user taps the book, so it is
  not a second SD sweep.
- **The chapter row is the index `k` with `chapterSpine[k] == spine`.** For these EPUBs that is
  also `spine - bookTargetSpine[i] - 1`, but matching against `chapterSpine` needs no layout
  assumption and handles the nav page (`spine == bookTargetSpine[i]`) by not matching it.
- **Direct (single-chapter) books:** `bookTargetSpine[i]` *is* the chapter spine
  (`BibleNavScanner.h`, `isChapterNav` comment; `:236-238`). They have no chapter level, so "open at
  the chapter grid" has no screen for them. The mapping reports (book i, no chapter row), and the
  spec must choose what to open: the book grid with that book selected, or today's book-level
  entry with nothing selected. The issue asks for them in the host test but says nothing about
  their UX.
- The reader's cached `bibleChapterNumber` (`EpubReaderActivity.h:75-76`,
  `EpubReaderActivity.cpp:1592-1607`) is **not** a usable input. It reads with
  `WhenMissing::Fail` and stays -1 whenever a finished layout cache meant the HTML was never read
  (`:1597-1601`). It gives the chapter number without the book anyway.
- The TOC's covering entry for every chapter spine is its book entry (0 mismatches above, matching
  `BookMetadataCache.cpp:236-316`'s "last TOC entry at or before" fill). That offers a second route
  to the book, but it would need a TOC-href join against `bookTargetSpine`. The range rule on data
  already in memory is simpler.

## Things the design has to handle

- **Book grid layout is built lazily on the render task** (`buildGrid`, `:395-401`). If the
  navigator opens at Chapter, `bookLayout.pageCount` is 0 when Back first calls
  `enterLevel(Level::Book, selectedBook)`. At that point `lastSelectableIndex()` is -1 (`:259`), and
  `placeSelectionLocked` returns without clamping (`:265`). `nav.selected` has already been set to
  `selectedBook` by `enterLevel` (`:195`), and the next `buildGrid` builds the layout, then picks
  the page from `BookGrid::pageOf(bookLayout, nav.selected)` (`:405-407`, `BookGridLayout.h:104-110`).
  Back should therefore land on the right testament page with Genesis selected. This path has
  never run before (today Book is always painted before Chapter), so the device check has to
  cover it.
- **Chapter grid geometry is also lazy.** `grid` is zero until the first number-level build.
  `enterLevel(Chapter, k)` with zero geometry leaves `nav.top = 0` (`NumberGridLayout.h:56-64`
  return 0 for `cellsPerPage <= 0`). The first build then sees
  `nav.visibleRows != grid.cellsPerPage()` and re-pages around `nav.selected` (`:414-417`),
  which is how the current chapter's page ends up in view.
- **Failure fallbacks already exist.** `loadChapters` returns false on OOM or a stream failure
  (`:108-136`). On that path the entry must stay at Book level, as the issue's third acceptance
  criterion asks.
- **Memory:** no new buffers are needed. `chapterSpine` (300 B) and `bookTargetSpine` (132 B) are
  already members. The only new state is the entry spine index, one `int`.
- The font-cache clear in `onEnter` (`:39-45`) must stay ahead of any load, per the
  reader-overlay rule.

## Nearest existing examples

- **Opening an overlay at the reader's position:** `EpubReaderChapterSelectionActivity` takes
  `currentSpineIndex` in its constructor (`EpubReaderChapterSelectionActivity.cpp:14-20`). In
  `onEnter` it maps it with `epub->getTocIndexForSpineIndex` and sets `nav.selected`
  (`:41-46`), falling back to 0 when unmapped (`:43-45`). The Bible branch should take the same
  constructor argument and follow the same "map it, fall back when unmapped" shape.
- **Host-testable pure helpers for this activity:** `src/activities/reader/NumberGridLayout.h` and
  `BookGridLayout.h` are header-only arithmetic namespaces kept free of FreeInkUI and Arduino so
  that `test/number_grid` can include them (`NumberGridLayout.h:6-9`; CMake at
  `test/number_grid/CMakeLists.txt`, include root `${REPO_ROOT}/src`, no extra sources). A
  spine → (book, chapter row) helper over `const int16_t*` arrays fits that pattern and can be
  tested from arrays built from the measured layout above. Fixtures stay synthetic (spine
  numbers only), which satisfies the no-publisher-text rule for public test fixtures.
- **Level entry with a preselection:** the Verse → Chapter branch of `onBackButton`
  (`enterLevel(Level::Chapter, selectedChapterRow)`, `:362`) already enters the chapter grid with a
  non-zero selection. Opening at Chapter is the same call made from `onEnter`.

## Installed tool and package versions

```
$ ~/.platformio/penv/bin/pio --version    → PlatformIO Core, version 6.1.19
$ cmake --version                         → cmake version 4.4.2
test/CMakeLists.txt:16-17                 → googletest GIT_TAG v1.17.0
platformio.ini:15                         → platform-espressif32 55.03.37 (pioarduino)
$ git -C freeink-sdk log -1               → 310ec61 Remove row rectangle tracking from list component
```

## Scope and tier

The change stays on the `ui` surface. It touches `src/activities/reader/` (the navigator, the
reader call site and a new header-only helper) plus a new host test directory. The one shared-file
touch is an `add_subdirectory` line in `test/CMakeLists.txt`, which `ui-dev.md` says to report,
not edit. There is no on-disk format, no persisted state, no input-layer change and no new
dependency. Tier stays `standard`.

## Open question for the spec

- What opens when the reader is in a single-chapter book (Obadiah, Philemon, 2–3 John, Jude),
  which has no chapter grid? My recommendation is the book grid with that book selected: it is
  the nearest level that exists, and it still saves the page hunt. The alternative is today's
  unselected book grid.
- When the reader is on a book's own chapter-nav page, the mapping finds the book but no chapter.
  The same choice applies: select the book on the book grid, or fall back entirely.
