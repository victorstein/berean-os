Tier: heavy

# PR #221 review: code quality, pass 0

Scope: `gh pr diff 221` on `feature/201-recent-places`. I read the source and test changes in full.
I compared them against the patterns they claim to follow: `RecentBooksStore`/`RecentBooksDoc`,
`HighlightsActivity::buildChipRow`, `StudyStore::locateUnit` and `UnitIndexCache::unitsFor`. I did
not review the docs (spec, plan and research).

## What mirrors existing patterns correctly

- **Store split.** The store is split as `Place.h` + `util/PlacesDoc.*` + `PlacesStore.*`. This is
  the `RecentBook.h` + `util/RecentBooksDoc.*` + `RecentBooksStore.*` shape exactly:
  - The same `toJson`/`fromJson` delegation.
  - The same `requestResave()` instead of saving under `storeMutex` (`src/PlacesStore.cpp:16-17`, cf.
    `src/RecentBooksStore.cpp:24`).
  - The same derived `SAVE_BUDGET` with its trio of `static_assert`s (`src/PlacesStore.cpp:53-58`, cf.
    `src/RecentBooksStore.cpp:103-107`).
  - The same `ESCAPE_FACTOR`/NUL argument (`src/util/PlacesDoc.h:34-36`).
- **Refusal log.** The version-refusal log uses `doc["v"] | 0` (`src/PlacesStore.cpp:12`), which is
  more accurate for an absent version than the sibling's `| FORMAT_VERSION`. It is not a
  divergence worth flagging.
- **`peekUnits`.** `UnitIndexCache::peekUnits` (`src/study/UnitIndexCache.cpp:241-257`) extracts
  `loadIndexedIntoCache` from `unitsFor` rather than copying the read-entry and load-anchors block.
  `unitsFor` now calls the helper too. That is the right refactor: one load path and two policies.
- **Locate helpers.** `StudyStore::locatePlace` delegates to the existing `locateUnit`
  (`src/study/StudyStore.cpp:101-104`) instead of re-implementing the book search.
  `locatePlaceAtHint` is the hint-only, non-building variant, and it exists because of the lock
  constraint documented at `src/study/StudyStore.h:92-94`.
- **Pickers.** `openChapterPicker`/`openBibleSearch` are straight extractions of the former
  `SELECT_CHAPTER`/`SEARCH_BIBLE` switch bodies. The explanatory comments moved with them, and the
  only addition is the `CancelTo` branch (`src/activities/reader/EpubReaderActivity.cpp:843-885`).
  The menu cases now call them, so there is one path, not two.
- **Recent action.** The Recent action follows the menu's existing trampoline shape
  (`recentTrampoline` beside `closeTrampoline`, `ACTION_RECENT = ACTION_USER + 2`). Its layout
  addition is a pure, host-tested extension of `ReaderMenuSheetLayout::compute`.
- **Error handling.** It matches the established shape:
  - `LOG_ERR` + return on a failed save (`src/PlacesStore.cpp:42-45`).
  - `LOG_ERR` + fallback on OOM of the name table: the chips fall back to full references
    (`EpubReaderActivity.cpp:894-897`).
  - A user-facing `tr()` message when a place cannot be located.
- **Tests.** The tests are designed, not just present:
  - The byte budget is pinned by measurement at exactly zero slack (`PlacesDocTest.cpp:58`).
  - Version refusal is tested at both the doc level and end to end against the storage fake, with
    byte-identity of the refused file (`PlacesStoreTest.cpp`, `PlacesStoreLazyLoadTest.cpp`).
  - "Recording the head again does not write" is tested by making any attempted save observable
    through a forced rename failure, rather than by trusting a return value.
  - Intent routing is a `constexpr` pure function with a case per kind × Bible/non-Bible.
- **Comments and dead code.** I found no commented-out code, and no comments that restate the next
  line. The comments that are there explain a lock or an ordering constraint, for example
  `EpubReaderActivity.cpp:1836`, "Before clearDeferredReposition()…".

## Findings

### MINOR 1: the pill-chip style block is a second copy of the Highlights chip style

`src/activities/reader/EpubReaderMenuActivity.cpp:421-443` repeats
`src/activities/reader/HighlightsActivity.cpp:714-747` line for line. It is the same `padX`/`gap`/
`chipHeight` derivation, the same width rule
`std::max(textWidth + 2 * padX, chipHeight)`, and the same 12-line `fui::StyleSet` (white pill,
1 px black border, radius `chipHeight / 2`, inverted selected/focused/active, disabled = normal). The
new code's own comment cites the source (`EpubReaderMenuActivity.cpp:408-409`, "as the Highlights
tag chips are drawn").

This is now the second way to spell the same visual component. The next change to chip styling
(for example, the Direction B compact metrics) has to find both copies.

**Fix inline:** lift the `StyleSet` construction into a small shared helper, for example
`pillChipStyles(int chipHeight)` beside `TagChipRow.h` or in the UI theme layer. Call it from both
sites.

### MINOR 2: `eraseNuls`/`capField` are copied verbatim from `RecentBooksDoc.cpp`

`src/util/PlacesDoc.cpp:15-29` is byte-for-byte `src/util/RecentBooksDoc.cpp:14-29`, and the new
comment says so: "The same two helpers as RecentBooksDoc.cpp". The copy is small, and both files
are host-tested. Still, the NUL erasure is load-bearing for each file's `ESCAPE_FACTOR` budget
proof, and two anonymous-namespace copies can drift.

**Fix inline:** move the pair into a shared header (for example `src/util/DocFieldBounds.h`, or
beside `utf8SafeSummary` in `lib/Utf8`), and include it from both docs. The explanatory comment
stays with the single definition.

### MINOR 3: `captureLeftPlace` is also used for the place still on screen

`captureLeftPlace()` (`src/activities/reader/EpubReaderActivity.h:123`, `.cpp:1465`) returns "the
Bible place the reader is on". The name describes only the recording call sites.
`openReaderMenu` calls it to compute the chapter on screen, which the Recent row skips
(`EpubReaderActivity.cpp:279`, `onScreen = captureLeftPlace();`). At that call site the name reads
as though the menu records a departure.

**Fix inline:** rename it to something neutral, such as `capturePlaceOnScreen()` or
`currentPlace()`. The header comment already describes it neutrally.

### MINOR 4: the "locate, else say so, else navigate" tail is written twice

`openRecentPlace` (`EpubReaderActivity.cpp:923-928`) and the `Route::Locate` case of
`onBookLoaded` (`EpubReaderActivity.cpp:948-953`) both end in the same three steps:
`if (!location) { showMessage(STR_LINK_TARGET_NOT_FOUND); return; } navigateTo({.spineIndex, .offsetJump})`.
They differ only in whether the hint is tried under the lock first.

**Fix inline:** a private `void goToLocation(const std::optional<StudyStore::Location>&)` would
keep the not-found message and the `NavTarget` shape in one place. This is optional polish, and
low risk either way.

## Considered and not raised

- **Five capture-and-record blocks.** The same block appears in `pageTurn`/`skipPages`
  (`EpubReaderActivity.cpp:1019-1090`):
  `std::optional<Place> left; { RenderLock lock; left = captureLeftPlace(); … } recordPlace(…)`.
  Each wraps a different set of state mutations that must stay inside the same lock. Folding them
  would need a callback (discouraged by CLAUDE.md's `std::function` rule) or a restructure of
  pre-existing code, so the repetition is the honest cost of the lock scoping.
- **`ReaderEntryIntent::of()`.** It has no production caller yet. The intent API is in the
  issue's scope, and #203 wires the launcher entry points, so it is not dead code.
- **`PlacesStore::ensureLoaded`/`loadAttempted`.** This is new to the store family. It is justified
  in the header (`src/PlacesStore.h:24-26`) and pinned by `PlacesStoreLazyLoadTest`. It is a
  defensive addition, not a competing load mechanism, because `load()` still goes through
  `loadFromFile()`.
- **`RECENT_LABEL_BYTES = 49`.** It is a literal in the layout header, tied to
  `PlacesDoc::MAX_REFERENCE_BYTES + 1` by a `static_assert` (`EpubReaderActivity.cpp:931-932`). That
  keeps the layout header free of `PlacesDoc`.

## Verdict

The change consistently mirrors the store, menu and layout patterns it extends. Its extractions
(`loadIndexedIntoCache`, `openChapterPicker`/`openBibleSearch`) remove duplication rather than add
it, and its tests are well aimed. The four findings are local duplication and naming, all fixable
inline. None reverses a decision or needs the owner's judgment.

VERDICT: CLEAR
