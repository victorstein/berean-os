# Design: the reader opens a preaching reference and swipes back to its card (berean-o-20, t7)

Parent spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §6, D8, D9.
Research: `docs/superpowers/research/2026-10-15-berean-o-20-research.md`. Line numbers are at
`ae18217f` on `feature/preaching-reader-return`.

## Changes since pass 0

Review: `docs/superpowers/reviews/berean-o-20-spec-review-0.md` (BLOCKER 1, MINOR 1–3).

- **BLOCKER 1 — the href no longer goes through `navigateToHref`.** Verified: NWT verse markers are
  empty `<span id="chapterC_verseV">` (`lib/Epub/Epub/VerseAnchors.h:8-10`; e.g.
  `1001061251.xhtml` links `1001061123-split83.xhtml#chapter83_verse18`), and
  `ChapterHtmlSlimParser` keeps span ids out of the anchor map
  (`lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:109`, used at `:454`). `findAnchor` can never
  resolve a verse, so `navigateToHref` would open every reference at its chapter's top. The repo
  settled this in `docs/superpowers/specs/2026-09-12-bible-verse-navigation-design.md:72-80`.
  `Route::Href` now resolves the verse to a visible-text offset with `VerseAnchors::Scanner`, as the
  verse grid does (`src/activities/reader/BibleNavigationActivity.cpp:250-264`, offset handed back at
  `:429`), and navigates by offset. This departs from the mechanism the brief and the parent spec §6
  name; it is raised with `hpipe decide` (see "Departure from the brief").
- Consequently **A8 (log on the shared anchor site) and A10 (`navigateToHref` returns `bool`) are
  withdrawn.** The section-load path and `navigateToHref` are not touched. The miss is now known
  inside the route, off the render path, and logged there (new A8).
- **MINOR 1** — `ReaderUtils.h:249-254` and `ReturnStack.h:15-18` are added to the file table, their
  comments reworded for the origin.
- **MINOR 2** — A6 now says a long edge hold behaves as today: inert in tap mode, a page back in
  swipe mode.
- **MINOR 3** — the "no new side effect" argument no longer calls `getHeldTime()` a const read
  (it writes the mutable `touchHeldOverrideValid`, `src/MappedInputManager.cpp:283`); see "The swipe".
- Device check 6 now checks the verse is on screen, not that a log line is absent.

## Problem

Predicación already sends the reader an intent (`PreachingTopicActivity::openBible`,
`src/activities/preaching/PreachingTopicActivity.cpp:173-207`): `Kind::OpenHref` for a question's
reference, `Kind::OpenAt` for a tag topic's passage, each carrying a `PreachingOrigin`. The reader
ignores what is new:

- `Route::Href` is a stub that logs and opens normally (`src/activities/reader/EpubReaderActivity.cpp:1132-1134`).
- `onBookLoaded` clears the whole intent, origin included (`EpubReaderActivity.cpp:1102-1103`).
- The left-edge swipe with an empty return stack is inert (`EpubReaderActivity.cpp:694-702`,
  `src/activities/reader/ReaderUtils.h:255`). D8 replaced Predicación with the reader, so there is
  nothing underneath to pop to, and no way back to the card.

## Goal

1. An `OpenHref` intent opens the Bible at that verse. A file that resolves with a verse that does
   not opens at the file's start and logs `LOG_ERR`.
2. The reader keeps the intent's `PreachingOrigin` for its whole life.
3. The left-edge swipe pops `returnStack` when it has entries; with it empty and an origin set,
   calls `activityManager.goToPreaching(origin)`; with no origin, behaves exactly as today.
4. The swipe decision and the href's verse parsing are pure functions, host-tested in existing
   suites: `test/ui_layout` and `test/verse_anchors`.

## Non-goals

- `ReaderEntryIntent`'s fields, `PreachingOrigin`, `goToPreaching`, Predicación's screens and the
  sender — used as landed by T3 (parent spec §10).
- `lib/Epub`: no change to `ChapterHtmlSlimParser`'s span rule, `VerseAnchors` or
  `SECTION_FILE_VERSION`.
- Place recording (D9): unchanged (research, "Place recording").
- Persisting the origin. It is RAM only; Home and sleep drop it by destroying the reader.
- A visible "back to Predicación" affordance. The swipe is the way back (parent spec §6).
- Physical Back-button behaviour beyond A5. The X4 Pro has none (`CLAUDE.md`, "Input hardware").

## Departure from the brief

The brief and the parent spec §6 say the `OpenHref` route calls `navigateToHref(href, false)`. On
this EPUB that cannot reach a verse (BLOCKER 1 above). The brief's acceptance line is "opens the
Bible at that verse"; this design keeps the goal and changes the mechanism to the one every other
verse jump in the reader already uses — the verse grid (`BibleNavigationActivity.cpp:429` →
`EpubReaderActivity.cpp:1030-1032`), search (`:1047`) and `Route::Locate` (`:1119`) all navigate by
`offsetJump`. The alternative, recording verse span ids in `ChapterHtmlSlimParser`, is `lib/Epub`
(epub-dev's surface), needs a `SECTION_FILE_VERSION` bump that invalidates every cached section, and
grows the anchor map. The worker raises this with `hpipe decide --task t7`, recommending the offset
route.

## Architecture

| File | Change |
|---|---|
| `src/activities/reader/ReaderBackSwipe.h` (new) | `ReaderBackSwipe::decide`, the three-way swipe decision |
| `src/activities/reader/VerseHref.h` (new) | `VerseHref::parse` (an href's `chapterC_verseV` fragment) and `VerseHref::find` (its anchor among a scan's) |
| `src/activities/reader/EpubReaderActivity.h` | a `PreachingOrigin preachingOrigin` member copied from the intent in the constructor; a private `openReference(const char* href)` |
| `src/activities/reader/EpubReaderActivity.cpp` | `Route::Href` → `openReference`; the swipe branch at line 694 routed through `decide` |
| `src/activities/reader/ReaderUtils.h` | the comment at lines 249-254 reworded: the reader surface's own swipe handling may leave for Predicación |
| `src/activities/reader/ReturnStack.h` | the comment at lines 15-18 reworded: once the ring empties the swipe goes back to Predicación when there is an origin, else inert |
| `test/ui_layout/ReaderBackSwipeTest.cpp` (new) + `test/ui_layout/CMakeLists.txt` | the swipe decision's tests, its own executable |
| `test/verse_anchors/VerseHrefTest.cpp` (new) + `test/verse_anchors/CMakeLists.txt` | `VerseHref`'s tests, its own executable beside `VerseAnchorsTest` |

`ReaderActivity.cpp` needs nothing: it only forwards the intent (`ReaderActivity.cpp:26-38`) and calls
`onBookLoaded()` after `loadBook()` (`ReaderActivity.cpp:56-66`).

### `ReaderBackSwipe.h` — modelled on `src/activities/reader/MenuCloseRestore.h`

Firmware-free namespace, one input struct, an `enum class` verdict, `constexpr` — the shape of
`MenuCloseRestore::decide`, tested by `test/ui_layout/MenuCloseRestoreTest.cpp`.

```cpp
namespace ReaderBackSwipe {

struct Swipe {
  bool shortBackRelease = false;  // Back released (button or left-edge swipe) under GO_BACK_OR_HOME_MS
  int returnDepth = 0;            // returnStack.count()
  bool hasOrigin = false;         // preachingOrigin.isSet()
};

enum class Action : uint8_t { PassOn, PopReturn, ToPreaching };

constexpr Action decide(const Swipe& swipe) {
  if (!swipe.shortBackRelease) return Action::PassOn;
  if (swipe.returnDepth > 0) return Action::PopReturn;
  if (swipe.hasOrigin) return Action::ToPreaching;
  return Action::PassOn;
}

}  // namespace ReaderBackSwipe
```

`PassOn` falls through to `handleBackNavigation()`, today's behaviour for everything that is not a
pop: nothing for the gesture (`ReaderUtils.h:255`), Home or the file browser for a physical button
(`ReaderUtils.h:258-265`).

### `VerseHref.h` — modelled on `VerseAnchors`' id grammar

```cpp
namespace VerseHref {

struct Verse {
  uint16_t chapter;
  uint16_t verse;
};

// The `chapter<N>_verse<M>` fragment of a NUL-terminated href, in the grammar VerseAnchors reads ids
// with; nullopt for no fragment, another fragment, trailing junk, or a number past uint16_t.
std::optional<Verse> parse(const char* href);

// The scanned anchor for `verse`, or nullptr.
const VerseAnchors::VerseAnchor* find(const std::vector<VerseAnchors::VerseAnchor>& anchors, Verse verse);

}  // namespace VerseHref
```

`parse` uses the same `sscanf("chapter%u_verse%u%c")` test as `VerseAnchors.cpp:35-36` on the text
after the first `#`, with the same `<= UINT16_MAX` bounds, so the href and the scan agree on what a
verse marker is. Both are `inline` in the header; `find` is a linear scan (at most 176 anchors,
`VerseAnchors.cpp:68`). Header-only and Arduino-free: `VerseAnchors.h` is plain C++
(`lib/Epub/Epub/VerseAnchors.h:1-6`).

### `EpubReaderActivity` changes

**The origin member**, beside `entryIntent` (`EpubReaderActivity.h:60`):

```cpp
  // Where in Predicación this reader was opened from; kept for its whole life, unlike entryIntent.
  PreachingOrigin preachingOrigin;
```

initialised in the constructor's member-init list from `intent.origin`
(`EpubReaderActivity.h:224-227`), so `entryIntent = {}` (`EpubReaderActivity.cpp:1103`) cannot reach
it. `PreachingOrigin.h` is already included through `ReaderEntryIntent.h:10`. Cost: 6 bytes of the
heap-allocated activity.

**`Route::Href`**, replacing `EpubReaderActivity.cpp:1132-1134`:

```cpp
    case ReaderEntryIntent::Route::Href:
      openReference(intent.href);
      return;
```

```cpp
void EpubReaderActivity::openReference(const char* href) {
  const int spine = epub->resolveHrefToSpineIndex(href);
  if (spine < 0) {
    LOG_ERR("ERS", "Reference did not resolve: %s", href);
    ReaderUtils::showMessage(renderer, tr(STR_LINK_TARGET_NOT_FOUND));
    return;
  }
  const auto anchor = findReferencedVerse(spine, href);  // optional<uint32_t> offset
  if (!anchor) {
    LOG_ERR("ERS", "Verse of %s not found; opening spine %d at its start", href, spine);
    navigateTo({.spineIndex = spine});
    return;
  }
  navigateTo({.spineIndex = spine, .offsetJump = *anchor});
}
```

`findReferencedVerse` (private, or folded into `openReference` — the plan decides) does, mirroring
`BibleNavigationActivity::loadVerses` (`BibleNavigationActivity.cpp:250-264`, sink at `:37-39`):

1. `VerseHref::parse(href)`; none → nullopt.
2. A local `VerseAnchors::Scanner`; `!valid()` → `LOG_ERR("ERS", "OOM: verse scanner")`, nullopt.
3. `SpineHtmlStream::stream(epub, spine, renderer, feedVerseScanner, &scanner)` with the default
   `WhenMissing::Inflate`; false → nullopt.
4. `VerseHref::find(scanner.take(), *verse)`; its `offset`, or nullopt.

Each nullopt path is reported by `openReference`'s one `LOG_ERR` (A8); the OOM case logs twice, as
`loadVerses` does.

It runs in `onBookLoaded`, on the loop task, with no lock held and before the first render is
requested — the same place and conditions as `Route::Locate`'s `STUDY.locatePlace`, which may build
index entries (`EpubReaderActivity.cpp:1111-1121`). That matters because `WhenMissing::Inflate` takes
the render lock to borrow the framebuffer for its popup (`src/activities/reader/SpineHtmlStream.h:27-34`,
`SpineHtmlStream.cpp:37-40`). `epub` is a `std::shared_ptr<Epub>` (`EpubReaderActivity.h:26`), as
`stream` takes. `resolveHrefToSpineIndex(const std::string&)` (`lib/Epub/Epub.h:88`) builds one
temporary string from the ~44-byte href, as the sender does with the same href
(`PreachingTopicActivity.cpp:191`).

The offset is the unit `offsetJump` expects: the verse grid hands `verseAnchors[index].offset`
straight back as `ChapterResult::offsetJump` (`BibleNavigationActivity.cpp:429`, `:390`) and the
reader navigates with it unchanged (`EpubReaderActivity.cpp:1030-1032`).

`navigateTo`'s default `ReturnPolicy::Clear` matches what `navigateToHref(href, false)` would have
used (`EpubReaderActivity.cpp:2145-2146`); the stack is empty on entry anyway.

**The swipe**, replacing `EpubReaderActivity.cpp:694-698`:

```cpp
  const auto backSwipe = ReaderBackSwipe::decide(
      {.shortBackRelease = mappedInput.wasReleased(MappedInputManager::Button::Back) &&
                           mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS,
       .returnDepth = returnStack.count(),
       .hasOrigin = preachingOrigin.isSet()});
  if (backSwipe == ReaderBackSwipe::Action::PopReturn) {
    restoreSavedPosition();
    return;
  }
  if (backSwipe == ReaderBackSwipe::Action::ToPreaching) {
    LOG_INF("ERS", "Back to Predicación");
    activityManager.goToPreaching(preachingOrigin);
    return;
  }

  if (handleBackNavigation()) {   // unchanged, line 700
```

Side effects. Today `returnStack.count() > 0` short-circuits first, so with an empty stack neither
input call runs at line 694. Now `wasReleased(Back)` runs every loop. It is the same call
`handleBackNavigation` makes two lines later, through `wasBackGesture()` (`ReaderUtils.h:255`,
`:258`), and on a hit it refreshes the touch held-time override (`MappedInputManager.cpp:218`). The
`&&` still short-circuits `getHeldTime()` to Back releases only. For a gesture, the override was set
in that same frame, so the read returns it without clearing it (`MappedInputManager.cpp:278-282`).
For a button, `handleBackNavigation` calls `getHeldTime()` too (`ReaderUtils.h:261`). So the reads
add no state change `handleBackNavigation` would not make anyway. `getHeldTime()` is not a pure read
— it can clear the mutable `touchHeldOverrideValid` (`MappedInputManager.cpp:283`) — so a later
change must not move it ahead of the gesture check.

## Data and control flow

**Opening a reference.** Topic card → `openBible` → `goToReader(biblePath, false, intent)`
(`PreachingTopicActivity.cpp:206`) → `replaceActivity` defers the swap to the end of the loop
(`src/activities/ActivityManager.cpp:185-197`); Predicación is deleted, the reader constructed with
the intent → **`preachingOrigin = intent.origin`** → `onEnter` → `loadBook` → `onBookLoaded`:
`entryIntent` cleared, `Route::Href` → `openReference` → resolve the spine → scan its verse markers
(inflating the chapter to the HTML cache if it is not there) → `navigateTo({spine, offsetJump})` →
`requestUpdate` → the render task builds the section until it reaches the offset
(`EpubReaderActivity.cpp:1417-1419`, `buildReachedVisibleTextOffset`) and shows that page. The
HTML the scan inflated is the one the build then reads (`SpineHtmlStream.h:36-38`). A tag topic
takes `Route::Locate` unchanged; only its origin is newly kept.

**Swiping back.**

| `returnStack` | origin | left-edge swipe does |
|---|---|---|
| > 0 | any | pops one position (`restoreSavedPosition`, `EpubReaderActivity.cpp:2150`), as today |
| 0 | set | `goToPreaching(preachingOrigin)` → reader deleted (Bible parked; destructor `EpubReaderActivity.cpp:165-190`) → `PreachingActivity(origin)` reopens the topic at `origin.verse` (`PreachingActivity.cpp:32-58`) |
| 0 | none | falls through to `handleBackNavigation`, as today |

**Dropping the origin.** Home: `ActivityManager::loop` handles `wasHomeGesture()` before the
activity's loop and calls `goHome()` (`ActivityManager.cpp:74-82`); `EpubReaderActivity` does not
override `handleHomeGesture`. Sleep: `goToSleep` replaces the reader (`ActivityManager.cpp:241-244`)
and wake reopens it with no intent (`src/main.cpp:594-601`, `:586`). The reader is destroyed and the
member with it. Nothing writes `ReaderEntryIntent` or `PreachingOrigin` to any store (research,
"What drops the origin").

## Assumptions (each a behavioural decision, for review)

- **A1 — The origin is copied in the constructor, not in `onBookLoaded`.** No ordering in
  `onBookLoaded` can lose it; a book that fails to load (`ReaderActivity.cpp:56-59`, `finish()`)
  holds it harmlessly until deletion.
- **A2 — The origin is kept whatever the jump's outcome.** An unresolved file, a verse not found, an
  `OpenAt` passage not found: the reader still swipes back to the card, which is the right place
  after a failed open.
- **A3 — The origin is kept across every in-reader navigation.** Go to, a chip, search or a tag that
  clears `returnStack` (`ReturnPolicy::Clear`, `EpubReaderActivity.cpp:2088-2090`) leaves it, so the
  next swipe on an empty stack goes back to Predicación even from another chapter. The brief's "for
  its whole life", read literally.
- **A4 — The return stack always wins.** With entries, the swipe pops even with an origin set. Past
  `ReturnStack::CAPACITY` (16) the oldest entries are evicted silently (`ReturnStack.h:21-22`), so
  the last pop lands on the oldest surviving position and the next swipe goes to Predicación, not to
  the opened verse.
- **A5 — Button and gesture are treated alike.** `wasReleased(Back)` is true for both
  (`MappedInputManager.cpp:264-265`), as branch 1 already treats them. With an origin and an empty
  stack, a short physical Back would go to Predicación instead of Home; the X4 Pro has no such
  button.
- **A6 — The `getHeldTime() < GO_BACK_OR_HOME_MS` guard applies to all three outcomes**, as it does
  to branch 1 today (line 695). A long edge hold passes on and behaves as today: inert in tap mode, a
  page back in swipe mode (`detectTouchPageTurn` sets `prev`, `ReaderUtils.h:88-92`; acted on at
  `EpubReaderActivity.cpp:741-742`).
- **A7 — In swipe page-turn mode, the origin branch consumes the short left-edge swipe.** With
  `TOUCH_READER_SWIPE` a rightward swipe, an edge one included, is also `prev` (`ReaderUtils.h:85-95`).
  Branch 1 already returns before the page turn; branch 2 does the same, so with an origin and an
  empty stack a short left-edge swipe goes back to the card instead of paging back. A mid-screen
  rightward swipe still pages back: `wasBackGesture` is edge-anchored
  (`MappedInputManager.cpp:222-227`). `ReaderUtils.h:249-254`'s "a right swipe must page back" is
  reworded to say so.
- **A8 — A reference whose verse is not found opens its file at the start**, with one `LOG_ERR` in
  `openReference`. "Not found" covers: no `chapterC_verseV` fragment, a fragment outside the grammar,
  the scanner's OOM, a failed stream (no SD / inflate failure), and a verse absent from the scan. No
  message is shown: the reader is in the right chapter, which is the brief's "open that file at its
  start". The section-load path is untouched, so footnotes and the chapter picker log as today.
- **A9 — An unresolved file shows `STR_LINK_TARGET_NOT_FOUND`** and logs `LOG_ERR`, as
  `Route::Locate` shows on a miss (`EpubReaderActivity.cpp:1116`); the reader stays at its saved
  position. The sender checks the same file in the same book first
  (`PreachingTopicActivity.cpp:190-191`), so this is defensive. No new string.
- **A10 — The scan may inflate, with the indexing popup over the previous screen.** If the chapter's
  HTML is not cached, `stream` inflates it and, over 32 KB, draws `STR_INDEXING`
  (`SpineHtmlStream.h:24`, `SpineHtmlStream.cpp:37-40`). The section build that follows reuses
  that HTML, so the inflate is paid once, not twice. This is the verse grid's cost profile.
- **A11 — The verse grammar is duplicated, not shared.** `VerseHref::parse` repeats
  `VerseAnchors.cpp:35-36`'s format string rather than adding a parse function to `lib/Epub`
  (epub-dev's surface). A host test pins both readings of the same id against each other: a
  `VerseAnchors::scan` of a one-span excerpt and `parse` of an href to that id agree.
- **A12 — A verse matches on chapter and verse.** `find` compares both, so a file holding parts of two
  chapters cannot match the wrong one.
- **A13 — `goToPreaching` failing (OOM) leaves the reader on screen.** It logs and returns without
  replacing (`ActivityManager.cpp:232-239`); the next swipe retries. No message, as every other
  `goTo*` OOM path there.
- **A14 — Place recording is untouched (D9).** `navigateTo` from `onBookLoaded` records nothing
  before `pageShown` (`EpubReaderActivity.h:61-64`); leaving for Predicación records as leaving for
  Home does.

## Error handling

| What fails | What happens |
|---|---|
| The href's file does not resolve | `LOG_ERR`, `STR_LINK_TARGET_NOT_FOUND`, reader at its saved position; origin kept (A2, A9) |
| No verse fragment, scanner OOM, stream fails, verse absent | `LOG_ERR`, the file opens at its start (A8) |
| The Bible is not a Bible (`route` → `None`) | Today's `LOG_INF` and a normal open (`EpubReaderActivity.cpp:1105-1110`); origin kept |
| `goToPreaching` OOM | `LOG_ERR` in `ActivityManager`, reader stays; the swipe retries (A13) |
| The book does not load | `finish()` as today (`ReaderActivity.cpp:56-59`); the origin dies with the reader |

**Resources.** All transient, freed before `navigateTo` returns control to the loop:

- the scanner's expat parser and its `State`, whose anchor vector is reserved to 176 × 8 B up front
  (`VerseAnchors.cpp:65-68`), allocated with `new (std::nothrow)` and checked by `valid()`;
- `SpineHtmlStream`'s 2,048-byte chunk buffer, `makeUniqueNoThrow` (`SpineHtmlStream.cpp:14`, `:51-53`);
- the `std::vector` `take()` returns, moved out of the scanner;
- one temporary `std::string` for `resolveHrefToSpineIndex`.

On the stack: the `Scanner` object (two pointers and a bool) and the vector header — far under
256 B. The origin is a by-value member of an activity already on the heap. The same allocation
pattern is what the verse grid already runs on every chapter tap.

## Testing

**Host (TDD)**, two new executables in two existing suites, each registered in that suite's own
`CMakeLists.txt`. No new top-level suite, so nothing to hand off for `test/CMakeLists.txt`.

`test/ui_layout/ReaderBackSwipeTest.cpp` — modelled on `MenuCloseRestoreTest.cpp`, registered by
copying the `ReaderEntryIntentTest` block (`test/ui_layout/CMakeLists.txt:85-99`) with include dir
`${REPO_ROOT}/src`:

- no short Back release → `PassOn`, for every depth and origin;
- a short release with a non-empty stack → `PopReturn`, with and without an origin (A4);
- a short release, empty stack, origin → `ToPreaching`;
- a short release, empty stack, no origin → `PassOn`;
- `static_assert`s on `decide` for the same four.

`test/verse_anchors/VerseHrefTest.cpp` — registered by copying the `VerseAnchorsTest` block
(`test/verse_anchors/CMakeLists.txt:4-24`: `VerseAnchors.cpp`, `htmlEntities.cpp` and expat built
from the repo's sources with `XML_GE=0 XML_CONTEXT_BYTES=1024`), adding include dirs
`${REPO_ROOT}/src` and `${REPO_ROOT}/lib/Epub` so `activities/reader/VerseHref.h` and its
`<Epub/VerseAnchors.h>` include resolve:

- `parse`: `"1001061123-split83.xhtml#chapter83_verse18"` → {83, 18}; no `#`; `#` with nothing
  after; another fragment (`#p12`); trailing junk (`#chapter1_verse2x`); a number past `UINT16_MAX`;
  the longest real href within `ReaderEntryIntent::HREF_BYTES`;
- `find`: hit; miss; the same verse number in another chapter does not match (A12);
- agreement (A11): `VerseAnchors::scan` of a short excerpt with
  `<span id="chapter83_verse18"></span>` yields an anchor `find` returns for `parse` of the href.
  The excerpt is a few lines, per the public-repo fixture rule.

Run (`test/README:5-7`): `cmake -S test -B build/test && cmake --build build/test && ctest
--test-dir build/test --output-on-failure -R 'ReaderBackSwipe|VerseHref|VerseAnchors|ReaderEntryIntent|MenuCloseRestore'`.

**Build:** `pio run -e x4pro` with no new warnings; `./bin/clang-format-fix` (whole tree) clean.

**On the device** (the owner's check at the end of the batch; `EpubReaderActivity` is Arduino-bound):

1. Predicación → a question → a reference past verse 10 → the Bible with that verse on screen →
   swipe → the same card.
2. The same with a tag topic's verse.
3. From the opened verse follow a cross-reference, swipe (back to the verse), swipe (back to the card).
4. From the opened verse press Home, reopen the Bible from Home; the swipe is inert (tap mode).
5. Free heap equal across Predicación → Bible → back (serial `LOG_DBG("MEM", ...)`), several loops.
6. For several of the 20 questions, the verse each reference names is on the page that opens, and
   serial shows `Back to Predicación` on each return and no `Verse of ... not found`.
