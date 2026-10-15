# Research: the reader honours `OpenHref` and returns to Predicación (berean-o-20, t7)

Branch `feature/preaching-reader-return`, cut from `batch/20261015-predicaci-n-193x` at `61bb11f9`.
Spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §6, D8, D9. Every line number
below was read at that commit.

## What T3 already landed (the contract this task consumes)

- `ReaderEntryIntent` (`src/activities/reader/ReaderEntryIntent.h`) has `Kind::OpenHref`,
  `Route::Href`, `char href[HREF_BYTES]` with `HREF_BYTES = 64`, and a `PreachingOrigin origin`
  field. Its header comment already says `origin` is *not* consumed with the rest: "it must be
  copied out before onBookLoaded clears the intent". `route(OpenHref, true) == Route::Href`,
  `route(OpenHref, false) == Route::None`.
- `PreachingOrigin` (`src/activities/preaching/PreachingOrigin.h`): `Kind {None, Question, Tag}`,
  `question`, `tag`, `verse`, `isSet()`. Arduino-free, so a host test can include it.
- `ActivityManager::goToPreaching(const PreachingOrigin&)` (`src/activities/ActivityManager.cpp:232-239`)
  `makeUniqueNoThrow<PreachingActivity>`, logs on OOM, `replaceActivity`. `PreachingActivity`'s
  constructor already re-opens the topic at `origin.verse` (`PreachingActivity.cpp:32-55`).
- The sender, `PreachingTopicActivity::openBible` (`src/activities/preaching/PreachingTopicActivity.cpp:173-207`):
  - Question: `ReaderEntryIntent::openHref(href, originAt(verse), intent)` **and**
    `session.book()->resolveHrefToSpineIndex(href) >= 0`; on failure it stays on the card with
    `STR_PREACHING_VERSE_NOT_FOUND`. So the reader only ever receives an href whose *file*
    resolved (in the parked `Epub`, the same book the reader then opens).
  - Tag: `Kind::OpenAt` with `unit`, `spineHint` and `intent.origin = originAt(verse)` set by hand
    (lines 199-203) — the brief's "an `OpenAt` from a tag topic carries one too" is already true on
    the sending side.
  - Both end in `activityManager.goToReader(session.biblePath(), false, intent)` (line 206), which
    replaces Predicación (D8): `ActivityManager::goToReader` → `ReaderActivity::create` →
    `replaceActivity` (`ActivityManager.cpp:209-230`, `ReaderActivity.cpp:26-38`).
- Host test of the contract: `test/ui_layout/ReaderEntryIntentTest.cpp`, registered at
  `test/ui_layout/CMakeLists.txt:85-99`.

## Where the intent is consumed today

- Stored: `EpubReaderActivity` constructor copies it into `entryIntent`
  (`src/activities/reader/EpubReaderActivity.h:224-227`; member at line 60, commented "Consumed once
  by onBookLoaded(); None thereafter").
- Fired: `ReaderActivity::onEnter` → `loadBook()` → `onBookLoaded()` → `requestUpdate()`
  (`src/activities/reader/ReaderActivity.cpp:46-67`).
- Consumed: `EpubReaderActivity::onBookLoaded` (`EpubReaderActivity.cpp:1101-1135`) copies the
  intent, **clears the whole member** (`entryIntent = {};`, line 1103) and switches on `route()`.
  `Route::Href` is a stub today (lines 1132-1134): `LOG_INF("Href entry intent not honoured yet")`.
  So the origin is currently dropped at line 1103 for both `OpenHref` and `OpenAt`.
- `ReaderActivity.cpp` itself only forwards the intent (`create`, line 27); the wiring point is
  `onBookLoaded`, as for `Route::Locate`.
- `Route::Locate` (lines 1111-1121) is the nearest example: it calls `navigateTo` off-lock, before
  the first render is requested, and on failure `ReaderUtils::showMessage(renderer,
  tr(STR_LINK_TARGET_NOT_FOUND))`.

## `navigateToHref` and what a missing anchor does

`navigateToHref(const std::string&, bool savePosition)` is at `EpubReaderActivity.cpp:2128-2148`
(the brief's `:2125` has drifted three lines). It splits off the `#anchor`, resolves the spine with
`epub->resolveHrefToSpineIndex`, and on `< 0` only `LOG_DBG`s and returns — the reader then shows
its `progress.bin` position. Otherwise `navigateTo({.spineIndex, .anchor}, savePosition ? Push : Clear)`.

The anchor is resolved when the section is built (`EpubReaderActivity.cpp:1378-1465`):

- The build loop runs `while (!section->isBuildComplete() && !section->findAnchor(pendingAnchor))`
  (lines 1417-1419), so an anchor that does not exist builds the whole spine item.
- Then `section->currentPage = nextPageNumber` (lines 1441-1442); `NavTarget::pageNumber` defaults
  to 0 (`EpubReaderActivity.h:192-199`), so the page is the file's start.
- `if (!pendingAnchor.empty()) { page = findAnchor(...); if (page) {...} pendingAnchor.clear(); }`
  (lines 1458-1465) — a miss is **silent**: no log at any level.

So "open the file at its start" is already the behaviour on a missing anchor; the `LOG_ERR` is what
is missing, and it can only be emitted at line 1458-1465, where the miss is known. That is the
render task's section-load path, shared by every anchor jump (footnotes, cross-references, chapter
picker at line 1032), so a log there fires for all of them, not only Predicación's.

The unresolved-*file* branch (line 2140) cannot be reached from Predicación in practice (the sender
checked the same book), but a stale or different Bible is possible in principle; today it would
leave the reader on the saved position with a `LOG_DBG` only.

`navigateToHref` takes `const std::string&`; `intent.href` is a `char[64]`, so the call builds one
temporary `std::string` (heap for > 15 chars, the hrefs are ~44 B). One allocation per book open.

## The swipe today

On this device Back is a left-edge swipe: `MappedInputManager::wasReleased(Back)` returns true when
`wasBackGesture()` (`src/MappedInputManager.cpp:264-265`), which is `wasEdgeSwipe(ScreenEdge::Left)`
(lines 222-227); `wasEdgeSwipe` calls `rememberTouchHeldTime()` (line 218), which is what
`getHeldTime()` returns for a touch (lines 278-285).

`EpubReaderActivity::loop` (`EpubReaderActivity.cpp:512`, does not call `ReaderActivity::loop`):

```
694  if (returnStack.count() > 0 && mappedInput.wasReleased(Back) &&
695      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
696    restoreSavedPosition();
697    return;
698  }
700  if (handleBackNavigation()) {
701    return;
702  }
```

`handleBackNavigation` (`ReaderActivity.cpp:80-83`) → `ReaderUtils::handleBackNavigation`
(`src/activities/reader/ReaderUtils.h:247-266`) returns `false` for `wasBackGesture()` (line 255), so
with an empty stack the swipe falls through. Physical Back (not present on the X4 Pro) would still go
Home or to the file browser there.

Interaction with swipe page-turn mode: with `touchReaderControls == TOUCH_READER_SWIPE`,
`detectTouchPageTurn` (`ReaderUtils.h:79-95`, called at `EpubReaderActivity.cpp:590`) maps any
rightward swipe — a left-edge one included — to `prev`. Today a left-edge swipe with entries in the
stack is consumed at line 694 before the page turn (line 741); with an empty stack it pages back.
A new "empty stack and an origin" branch beside line 694 would likewise consume it, so in swipe mode
the left-edge swipe stops paging back for a reader opened from Predicación. That follows the
precedent of branch 1, but it is a visible consequence for the spec to state.

`ReturnStack` (`src/activities/reader/ReturnStack.h`): `CAPACITY = 16`, silently evicts the oldest
on overflow; `count()`, `pop()`, `oldest()`. After 17 cross-references, 16 swipes return to the
oldest surviving position, and the 17th goes to Predicación, not to the opened verse.

## What drops the origin, and whether anything persists it

- Home key: `ActivityManager::loop` (`ActivityManager.cpp:74-82`) checks `wasHomeGesture()` before
  `currentActivity->loop()` and calls `goHome()` unless `handleHomeGesture()` claims it;
  `EpubReaderActivity` does not override `handleHomeGesture` (overrides are only in
  `EpubReaderMenuActivity`, `PassageSelectActivity`, `TagChipGridActivity`, `PassageLinksActivity`,
  `BibleSearchActivity` — `grep -rn handleHomeGesture src`). The reader is destroyed; a RAM member
  dies with it.
- Sleep/wake: `goToSleep` replaces the activity (`ActivityManager.cpp:241-244`); on wake
  `BootRoute::ResumeReader` calls `activityManager.goToReader(path, fastRefresh)` with **no intent**
  (`src/main.cpp:594-601`; also `:586` for the silent reader). Only `APP_STATE.openEpubPath` is
  persisted (`ReaderActivity.cpp:62-63`).
- Nothing serialises `ReaderEntryIntent` or `PreachingOrigin`: every reference is listed by
  `grep -rn 'ReaderEntryIntent\|entryIntent\|PreachingOrigin' src` — none is in a store, `APP_STATE`
  or `progress.bin` writer.
- Other reader re-creations also drop it, by the same mechanism: tags from a non-Bible book
  (`EpubReaderActivity.cpp:435`), end-of-book "open book" (`ReaderActivity.cpp:102`).

## Place recording (D9 — must stay unchanged)

`navigateTo` captures the left place only when the spine changes and records it after the lock
(`EpubReaderActivity.cpp:2076-2125`, `recordPlace(std::move(left))` at line 2124). `pageShown`
(`EpubReaderActivity.h:61-64`) blocks recording before the first page has been drawn, so an
`onBookLoaded` `navigateTo` — `Locate` today, `Href` after this task — does not record the
`progress.bin` position the reader never showed. Leaving the reader for Predicación goes through
the same destructor/exit path as Home; the destructor saves progress at `returnStack.oldest()` when
the stack is non-empty (`EpubReaderActivity.cpp:168-170`), which the "empty stack" branch never hits.
No change to place recording is needed for either route.

## Nearest examples to mirror

- Pure decision, host-tested in `test/ui_layout`: `MenuCloseRestore::decide`
  (`src/activities/reader/MenuCloseRestore.h`, `constexpr`, firmware-free, one input struct, an
  `enum class Verdict`), used at `EpubReaderActivity.cpp:349-357`, tested by
  `test/ui_layout/MenuCloseRestoreTest.cpp`. `ReaderEntryIntent::route` is the same shape.
- A new executable in a suite's own `CMakeLists.txt`: `ReaderEntryIntentTest` block at
  `test/ui_layout/CMakeLists.txt:85-99` (include dirs `src`, `lib/StudyStore`; links
  `crosspoint_test_common`, `GTest::gtest_main`). `test/return_stack/CMakeLists.txt` is the
  alternative (include dir `src` only).
- `ReturnStack.h` needs no Arduino, so a decision function could take `returnStack.count()` and
  `origin.isSet()` as plain inputs and stay host-buildable.

## Installed tools

- `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19`
- `platformio.ini:15` → pioarduino `platform-espressif32` `55.03.37`
- `cmake --version` → `cmake version 4.4.2`
- googletest `v1.17.0` via FetchContent (`test/CMakeLists.txt:15-17`)
- No `.pio/` in this worktree yet: `./bin/bootstrap` has to run before the first `pio run`.

## Scope and tier

All changes fall in `src/activities/reader/` (`EpubReaderActivity.{h,cpp}`, one new header for the
decision) and `test/ui_layout/` (its own `CMakeLists.txt`). `ReaderEntryIntent.h`'s fields, the
sender, `goToPreaching` and `PreachingActivity` are used as landed. No store, format version,
string or shared file (`test/CMakeLists.txt`, translations, `src/main.cpp`) is touched — the
existing `STR_LINK_TARGET_NOT_FOUND` covers an unresolved file. Tier stays **heavy**; nothing found
makes it bigger.

## Open points for the spec

1. Where the missing-anchor `LOG_ERR` lives: the miss is only knowable at the section-load site
   (lines 1458-1465), which is shared by every anchor jump. Either log there for all anchors, or
   remember that the current jump came from an `OpenHref` intent and log only for that.
2. Whether an unresolved file in `Route::Href` shows `STR_LINK_TARGET_NOT_FOUND` like `Locate`
   (and logs `LOG_ERR` rather than `navigateToHref`'s `LOG_DBG`).
3. Swipe page-turn mode: the origin branch consumes the left-edge swipe that would otherwise page
   back (see above) — state it as intended, consistent with branch 1.
4. Keep the `getHeldTime() < GO_BACK_OR_HOME_MS` guard of line 695 on the new branch, so a long
   edge hold behaves the same for all three outcomes.
