# Design: the reader opens a preaching reference and swipes back to its card (berean-o-20, t7)

Parent spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §6, D8, D9.
Research: `docs/superpowers/research/2026-10-15-berean-o-20-research.md`. Line numbers are at
`61a1fc1a` on `feature/preaching-reader-return`.

## Problem

Predicación already sends the reader an intent (`PreachingTopicActivity::openBible`,
`src/activities/preaching/PreachingTopicActivity.cpp:173-207`): `Kind::OpenHref` for a question's
reference, `Kind::OpenAt` for a tag topic's passage, each carrying a `PreachingOrigin`. The reader
ignores both halves of what is new:

- `Route::Href` is a stub that logs and opens normally (`src/activities/reader/EpubReaderActivity.cpp:1132-1134`).
- `onBookLoaded` clears the whole intent, origin included (`EpubReaderActivity.cpp:1102-1103`), so
  nothing remembers where the reader came from.
- The left-edge swipe with an empty return stack is inert (`EpubReaderActivity.cpp:694-702`,
  `src/activities/reader/ReaderUtils.h:255`), so a reader opened from Predicación has no way back to
  the card. D8 replaced Predicación with the reader, so there is nothing underneath to pop to.

## Goal

1. An `OpenHref` intent opens the Bible at that verse via `navigateToHref(href, false)`. A file that
   resolves with an anchor that does not opens at the file's start and logs `LOG_ERR`.
2. The reader keeps the intent's `PreachingOrigin` for its whole life.
3. The left-edge swipe: pops `returnStack` when it has entries; with it empty and an origin set,
   calls `activityManager.goToPreaching(origin)`; with no origin, behaves exactly as today.
4. The swipe decision is a pure function, host-tested in `test/ui_layout`.

## Non-goals

- `ReaderEntryIntent`'s fields, `PreachingOrigin`, `goToPreaching`, Predicación's screens and the
  sender — used as landed by T3 (parent spec §10).
- Place recording (D9): unchanged. The research shows it needs no change (research, "Place
  recording").
- Persisting the origin anywhere. It is RAM only; Home and sleep drop it by destroying the reader.
- A reader-menu entry or any visible affordance for "back to Predicación". The swipe is the only
  way back, as the parent spec §6 says.
- Physical Back-button behaviour beyond what falls out of A5. The X4 Pro has none (`CLAUDE.md`,
  "Input hardware").

## Architecture

Three changes in `src/activities/reader/`, one new header, one new host test.

| File | Change |
|---|---|
| `src/activities/reader/ReaderBackSwipe.h` (new) | `ReaderBackSwipe::decide`, the pure three-way decision |
| `src/activities/reader/EpubReaderActivity.h` | a `PreachingOrigin preachingOrigin` member, copied from the intent in the constructor; `navigateToHref` returns `bool` |
| `src/activities/reader/EpubReaderActivity.cpp` | `Route::Href` honoured; the swipe branch at line 694 routed through `decide`; `LOG_ERR` on an unresolved anchor |
| `test/ui_layout/ReaderBackSwipeTest.cpp` (new) + `test/ui_layout/CMakeLists.txt` | the decision's tests, as their own executable |

`ReaderActivity.cpp` needs nothing: it only forwards the intent (`ReaderActivity.cpp:26-38`) and calls
`onBookLoaded()` after `loadBook()` (`ReaderActivity.cpp:62-66`). The wiring point is
`EpubReaderActivity::onBookLoaded`, beside `Route::Locate`.

### `ReaderBackSwipe.h` — modelled on `src/activities/reader/MenuCloseRestore.h`

Same shape as `MenuCloseRestore::decide` (a firmware-free namespace, one input struct, an
`enum class` verdict, `constexpr`), tested the same way by `test/ui_layout/MenuCloseRestoreTest.cpp`.

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

`PassOn` means "fall through to `handleBackNavigation()`", which is today's behaviour for every case
that is not a pop: inert for the gesture (`ReaderUtils.h:255`), Home or file browser for a physical
button (`ReaderUtils.h:258-265`).

### `EpubReaderActivity` changes

**The origin member.** In `EpubReaderActivity.h`, beside `entryIntent` (line 60):

```cpp
  // Where in Predicación this reader was opened from; kept for its whole life, unlike entryIntent.
  PreachingOrigin preachingOrigin;
```

initialised in the constructor's member-init list from `intent.origin`
(`EpubReaderActivity.h:224-227`), so `onBookLoaded`'s `entryIntent = {}` (line 1103) cannot reach it.
`PreachingOrigin.h` is already included through `ReaderEntryIntent.h:10`. Cost: 6 bytes of the
heap-allocated activity (`sizeof(PreachingOrigin)` — `uint8_t`, `uint8_t`, `uint16_t`, `uint16_t`).

**The swipe**, replacing lines 694-698:

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

The condition is evaluated in the same order as today's `&&`: `returnStack.count() > 0` is no longer
evaluated first, so `wasReleased(Back)` and `getHeldTime()` now run every loop. Both are `const`
reads (`src/MappedInputManager.cpp:263-285`); `wasReleased(Back)` → `wasBackGesture()` →
`wasEdgeSwipe()` → `rememberTouchHeldTime()` on a hit (`MappedInputManager.cpp:218`), which the next
line, `handleBackNavigation()`, also triggers by calling `wasBackGesture()` (`ReaderUtils.h:255`). So
no new side effect is introduced; see A7.

**`Route::Href`**, replacing lines 1132-1134:

```cpp
    case ReaderEntryIntent::Route::Href:
      if (!navigateToHref(intent.href, /*savePosition=*/false)) {
        LOG_ERR("ERS", "Href entry intent did not resolve: %s", intent.href);
        ReaderUtils::showMessage(renderer, tr(STR_LINK_TARGET_NOT_FOUND));
      }
      return;
```

modelled on `Route::Locate` (lines 1111-1121): off-lock, before the first render is requested, the
same failure message. `intent.href` is NUL-terminated by `ReaderEntryIntent::openHref`
(`ReaderEntryIntent.h:43-51`); it converts to the `const std::string&` parameter once (one heap
allocation per book open for a ~44-byte href, freed on return).

**`navigateToHref` returns `bool`** (`EpubReaderActivity.h:204`, `.cpp:2128-2148`): `false` when
there is no `epub` or the spine does not resolve (the two early returns at lines 2129 and 2140),
`true` after `navigateTo`. Its three existing callers (lines 711, 718, 883) ignore the result and are
unchanged.

**The anchor miss.** In the section-load path (lines 1458-1465):

```cpp
    if (!pendingAnchor.empty()) {
      const auto page = section->findAnchor(pendingAnchor);
      if (page) {
        section->currentPage = *page;
        LOG_DBG(...);  // unchanged
      } else {
        LOG_ERR("ERS", "Anchor '%s' not found in spine %d; opening at its start", pendingAnchor.c_str(),
                currentSpineIndex);
      }
      pendingAnchor.clear();
    }
```

The page is already the file's start in that case: the build ran to completion looking for the
anchor (lines 1417-1419) and `currentPage = nextPageNumber`, which `NavTarget::pageNumber`'s default
of 0 set (lines 1441-1442; `EpubReaderActivity.h:192-199`). Only the log is new.

## Data and control flow

**Opening a reference.** Topic card → `openBible` → `goToReader(biblePath, false, intent)`
(`PreachingTopicActivity.cpp:206`) → `replaceActivity` defers the swap to the end of the loop
(`src/activities/ActivityManager.cpp:185-197`); Predicación is deleted, the reader is constructed
with the intent → **`preachingOrigin = intent.origin`** → `onEnter` → `loadBook` →
`onBookLoaded`: `entryIntent` cleared, `Route::Href` → `navigateToHref(href, false)` →
`navigateTo({spine, anchor}, ReturnPolicy::Clear)` (`EpubReaderActivity.cpp:2145`) →
`requestUpdate` → the render task builds the section until the anchor is found and shows that page.
A tag topic takes `Route::Locate` unchanged; only its origin is newly kept.

**Swiping back.**

| `returnStack` | origin | left-edge swipe does |
|---|---|---|
| > 0 | any | pops one position (`restoreSavedPosition`, line 2150), as today |
| 0 | set | `goToPreaching(preachingOrigin)` → reader deleted (Bible parked, progress saved by the destructor's normal path, `EpubReaderActivity.cpp:165-190`) → `PreachingActivity(origin)` reopens the topic at `origin.verse` (`PreachingActivity.cpp:32-58`) |
| 0 | none | falls through to `handleBackNavigation`; inert for the gesture, as today |

**Dropping the origin.** Home: `ActivityManager::loop` handles `wasHomeGesture()` before the
activity's loop and calls `goHome()` (`ActivityManager.cpp:74-82`); `EpubReaderActivity` does not
override `handleHomeGesture`. Sleep: `goToSleep` replaces the reader (`ActivityManager.cpp:241-244`)
and wake reopens it with no intent (`src/main.cpp:594-601`, `:586`). In both the reader is
destroyed and the member with it. Nothing writes `ReaderEntryIntent` or `PreachingOrigin` to any
store (research, "What drops the origin").

## Assumptions (each a behavioural decision, for review)

- **A1 — The origin is copied in the constructor, not in `onBookLoaded`.** It never passes through
  `entryIntent`'s consumption, so no ordering in `onBookLoaded` can lose it, and a book that fails to
  load (`ReaderActivity.cpp:56-59`, `finish()`) holds it harmlessly until deletion.
- **A2 — The origin is kept whatever the jump's outcome.** If the href's file does not resolve, the
  `OpenAt` passage is not found, or the anchor is missing, the reader still swipes back to the card.
  After a failed open the card is the right place to return to.
- **A3 — The origin is kept across every in-reader navigation.** Go to, a chip, search or a tag that
  clears `returnStack` (`ReturnPolicy::Clear`, `EpubReaderActivity.cpp:2088-2090`) leaves the
  origin, so the next swipe on an empty stack goes back to Predicación even from another chapter.
  This is the brief's "for its whole life", read literally.
- **A4 — The return stack always wins.** With entries, the swipe pops even when an origin is set; the
  origin is reached only after the stack is empty. If more than `ReturnStack::CAPACITY` (16)
  cross-references were followed, the oldest are evicted silently (`ReturnStack.h`), so the last pop
  lands on the oldest *surviving* position and the next swipe goes to Predicación, not to the
  opened verse.
- **A5 — Button and gesture are treated alike.** `wasReleased(Back)` is true for both
  (`MappedInputManager.cpp:264-265`), as branch 1 already treats them. With an origin and an empty
  stack, a short physical Back would go to Predicación instead of Home. No such button exists on the
  X4 Pro; the alternative (`wasBackGesture()` only) would make branch 2 differ from branch 1 for no
  device benefit.
- **A6 — The `getHeldTime() < GO_BACK_OR_HOME_MS` guard applies to all three outcomes**, as it does
  to branch 1 today (line 695). A long edge hold passes on, so it stays inert.
- **A7 — In swipe page-turn mode, the origin branch consumes the left-edge swipe.** With
  `TOUCH_READER_SWIPE`, a rightward swipe — an edge one included — is also `prev`
  (`ReaderUtils.h:85-95`, read at `EpubReaderActivity.cpp:590`, acted on at line 741). Branch 1
  already returns before the page turn; branch 2 does the same, so in a reader opened from
  Predicación with an empty stack the left-edge swipe goes back to the card instead of paging back.
  A mid-screen rightward swipe still pages back: `wasBackGesture` is edge-anchored
  (`MappedInputManager.cpp:222-227`).
- **A8 — The missing-anchor `LOG_ERR` is logged for every anchor jump**, not only Predicación's:
  footnotes, cross-references and the chapter picker's anchor (`EpubReaderActivity.cpp:711, 718,
  883, 1032`) share the site, and a link to an anchor that is not there is an error for all of them.
  The alternative — a flag remembering the jump came from an `OpenHref` intent — adds state to the
  render path for a log line. Release builds have `LOG_LEVEL=0` (`CLAUDE.md`, "Environments").
- **A9 — An unresolved file shows `STR_LINK_TARGET_NOT_FOUND`**, as `Route::Locate` does on a miss
  (line 1116), and logs `LOG_ERR`; the reader is left at its saved position. The sender checks the
  same file in the same book first (`PreachingTopicActivity.cpp:190-191`), so this is defensive. No
  new string.
- **A10 — `navigateToHref` reports success by return value** rather than the `Href` route resolving
  the spine twice. `resolveHrefToSpineIndex` (`lib/Epub/Epub.h:88`) searches the spine, and a miss
  walks all of it (`lib/Epub/Epub.h:33-35`), so a second call is not free; a return value also keeps
  one source of truth for "did it navigate".
- **A11 — `goToPreaching` failing (OOM) leaves the reader on screen.** It logs and returns without
  replacing (`ActivityManager.cpp:232-239`); the next swipe retries. No message is added — the same
  as every other `goTo*` OOM path there.
- **A12 — Place recording is untouched (D9).** `navigateTo` from `onBookLoaded` records nothing
  before `pageShown` (`EpubReaderActivity.h:61-64`); leaving for Predicación records like leaving for
  Home. No code in `recordPlace` or `captureLeftPlace` changes.

## Error handling

| What fails | What happens |
|---|---|
| The href's file does not resolve | `LOG_ERR`, `STR_LINK_TARGET_NOT_FOUND`, reader at its saved position; origin kept (A2, A9) |
| The anchor is not in the file | `LOG_ERR`, the file opens at its start (A8) |
| The Bible is not a Bible (`route` → `None`) | Today's `LOG_INF` and a normal open (line 1105-1110); origin kept |
| `goToPreaching` OOM | `LOG_ERR` in `ActivityManager`, reader stays; the swipe retries (A11) |
| The book does not load | `finish()` as today (`ReaderActivity.cpp:56-59`); the origin dies with the reader |

No allocation is added beyond the one temporary `std::string` for the href. The origin is a by-value
member of an activity already on the heap.

## Testing

**Host (TDD), `test/ui_layout/ReaderBackSwipeTest.cpp`** — modelled on `MenuCloseRestoreTest.cpp`,
registered in `test/ui_layout/CMakeLists.txt` as its own executable, copying the
`ReaderEntryIntentTest` block (lines 85-99) with include dir `${REPO_ROOT}/src`. No new top-level
suite, so no hand-off for `test/CMakeLists.txt`.

- No short Back release → `PassOn`, for every depth and origin.
- A short release with a non-empty stack → `PopReturn`, with and without an origin (A4).
- A short release, empty stack, origin → `ToPreaching`.
- A short release, empty stack, no origin → `PassOn` (today's inert swipe).
- `static_assert`s on `decide` for the same four, since it is `constexpr`.

Run: `cmake -S test -B build/test && cmake --build build/test --target ReaderBackSwipeTest &&
ctest --test-dir build/test -R ReaderBackSwipe`, plus the existing `ReaderEntryIntentTest` and
`MenuCloseRestoreTest` to show nothing else in the suite moved.

**Build:** `pio run -e x4pro` with no new warnings; `./bin/clang-format-fix` (whole tree) clean.

**Not host-testable** (Arduino-bound `EpubReaderActivity`), for the owner's device check at the end of
the batch:

1. Predicación → a question → a reference → the Bible at that verse → swipe → the same card.
2. The same with a tag topic's verse.
3. From the opened verse follow a cross-reference, swipe (back to the verse), swipe (back to the card).
4. From the opened verse press Home, then reopen the Bible from Home; swipe is inert.
5. Free heap equal across Predicación → Bible → back (serial `LOG_DBG("MEM", ...)`), several loops.
6. Serial shows `Back to Predicación` on each return and no `Anchor ... not found` for the 20
   questions' references.
