Tier: heavy

# PR #221 review — intent (pass 0)

Scope: PR #221 (`feature/201-recent-places`, head `66c02a04`) against issue #201, the spec
`docs/superpowers/specs/2026-09-30-issue-201-design.md` and the plan
`docs/superpowers/plans/2026-09-30-issue-201-plan.md`.

## Acceptance criteria, one by one

| Criterion (issue #201) | Met? | Evidence |
|---|---|---|
| `/.berean/places.json`, last 12, newest first, dedupe (book, chapter) with move-to-front | Yes | `src/util/PlacesDoc.cpp:105-114` (`record`), `lib/Serialization/SdPaths.h` (`PLACES_FILE` + `static_assert`) |
| Place = language-free `study::Unit` + reference ≤ 48 B + `spineIndex`/`visibleTextOffset` hints | Yes | `src/Place.h:10-20`; cap asserted equal to `PassageDoc::MAX_REFERENCE_BYTES` at `src/study/StudyStore.cpp` (`static_assert` after `locatePlace`) |
| Verse from the unit index + the page's visible-text offset | Yes | `EpubReaderActivity::captureLeftPlace` → `STUDY.placeAt` → `PlacesDoc::placeUnit` (`src/util/PlacesDoc.cpp:39-49`) |
| Writes on chapter leave, `navigateTo` jump, reader exit (Home, sleep); never per page | Yes | `pageTurn` spine branches and end-of-book branch, `skipPages` both directions, `navigateTo` (captured before `clearDeferredReposition`), new `EpubReaderActivity::onExit`; the same-spine page-turn branches are untouched |
| `PersistableStore<T>`, `writeDocToFileAtomic`, explicit budget ≤ 4 KB, `v:1` refused by a future build, `loadRefused` kept, loop-task owner | Yes | `src/PlacesStore.h:13`, `src/PlacesStore.cpp:31-58` (`saveToFileAtomic`, three `static_assert`s including `≤ 4096`); `PlacesDoc.cpp:131-132` (`isKnownFormatVersion`); the refused store also records nothing in RAM (`PlacesStore.cpp:33-39`) |
| Entry intent `{OpenAt | BookGrid | Search | Tags}` on `goToReader`, RAM only, consumed once after load via existing `navigateTo`/menu paths | Yes, with a labelled change of shape | `ActivityManager.h` `goToReader(…, const ReaderEntryIntent& = {})`; `ReaderActivity.cpp:63` `onBookLoaded()`; `EpubReaderActivity::onBookLoaded` resets `entryIntent` before dispatch and reuses `openChapterPicker`/`openBibleSearch`/`openHighlights`/`navigateTo` |
| Recent row in the reader sheet, newest first, tap jumps | Yes | `EpubReaderMenuActivity::drawRecentBand`, `recentTrampoline` → `MenuResult.recentIndex` → `EpubReaderActivity::openRecentPlace` |
| Host tests: dedupe, eviction at 12, `"v":2` refused and not overwritten, byte budget | Yes | `test/places_doc/PlacesDocTest.cpp:58-106,183-214`; `test/places_store/PlacesStoreTest.cpp:55-61`; `test/places_store/PlacesStoreLazyLoadTest.cpp:10-24` |
| Each intent opens the right target; no intent unchanged | Routing host-tested; device check deferred to #203 (labelled) | `test/ui_layout/ReaderEntryIntentTest.cpp:8-41`; `Kind::None` routes to `Route::None` and returns without side effects |
| Heap ~1–1.5 KB, no PSRAM | Store resident cost yes; one labelled transient PSRAM table | spec "Memory"; PR body "Deviations" bullet 2 |

I ran the new suites. `ReaderEntryIntentTest` (6) and `ReaderMenuSheetLayoutTest` (15) pass in
the existing `build/test` tree. `PlacesDocTest` (28), `PlacesStoreTest` (4) and
`PlacesStoreLazyLoadTest` (1) pass from a scratch CMake root that adds the two hand-off
`add_subdirectory` lines. `test/CMakeLists.txt` was not touched.

## Spec requirements: both halves

Both the easy and the hard half of each requirement are in the code:

- A5, nothing before first render: `pageShown` is set only after `renderContents` in
  `renderBook`, and `captureLeftPlace` checks it first.
- A6, a redundant head is not rewritten: `PlacesDoc.cpp:107`. It is proven to cause no SD write by
  `PlacesStoreTest.cpp:73-79`, which arms a failing rename so an attempted save would be visible.
- A12, an intent-opened picker cancels to the page: `CancelTo::Page` in `openChapterPicker` and
  `openBibleSearch`.
- A13, an intent that does not apply is logged and ignored: in `onBookLoaded`.
- A15, lazy self-load: `PlacesStore::ensureLoaded`. It has its own executable so the singleton
  starts unloaded.
- A17, abbreviations use `WhenMissing::Fail` and fall back to `r`: `collectRecentChips` and
  `BibleBookNameTable::load`.
- A19, capture happens under the lock and the write after it: every call site uses the
  `std::optional<Place> left; { RenderLock …; left = captureLeftPlace(); … } recordPlace(left)`
  shape.
- A21, the hint is tried under the lock before the search: `openRecentPlace`.
- A never-building `peekUnits`: `UnitIndexCache.cpp`. It is refactored through
  `loadIndexedIntoCache`, so `unitsFor` keeps its exact behaviour.

A3 makes capture depend on the unit index having the chapter's entry. That entry exists in
practice: every page render with `highlightsLoaded` calls `STUDY.passagesInDocument`
(`EpubReaderActivity.cpp:1570`), which calls the building `unitsFor`
(`src/study/StudyStore.cpp:237`) before `pageShown` can be set. The non-building `peekUnits`
therefore hits a populated entry, and `captureLeftPlace` requires `highlightsLoaded` too.

## Scope

- No launcher or Home UI and no progress UI: nothing in `src/activities/home` or the launcher
  changed.
- `ReturnStack` is unchanged.
- `src/main.cpp` and `test/CMakeLists.txt` are left to the hand-off, as the plan says
  (plan:23-26).
- The one extra, `STR_RECENT`, is committed in both YAMLs as the plan decided and the PR body
  states.
- The `openChapterPicker`/`openBibleSearch` extraction is behaviour-preserving for the menu path
  (`CancelTo::Menu` keeps `openReaderMenu(false)`). It is the spec's stated mechanism for A12, not
  an unasked refactor.

## Plan divergence

The plan's one self-declared deviation, no `offsetHint` in the intent (plan:60-64), is
implemented (`ReaderEntryIntent.h:16-19`) and explained in the PR body. Every function the plan
names exists in the tree, with the plan's names (`getPlaces`, `pickRecent`, `peekUnits`,
`locatePlaceAtHint`, `fitChips`, `abbreviationFor`, `onBookLoaded`). I found no unexplained
divergence.

## Findings

### MINOR 1: the PR's "Deviations from the issue" list omits two narrowings the spec makes

The issue says "If the index isn't ready, record the chapter only" and "on a `navigateTo` jump".
The spec changes both:

- A3 (spec:87-91) records **nothing** when the index has no units.
- A4(ii) (spec:95-96) records on `navigateTo` only when the spine changes. The code matches:
  `if (target.spineIndex != currentSpineIndex) left = captureLeftPlace();` in `navigateTo`.

Both are reasoned in the spec, and both are behaviourally harmless:

- Without an index there is no book number to key (book, chapter) on.
- `ready()` is false only when `begin()` failed.
- A same-chapter jump leaves no chapter, and the dedupe is per chapter anyway.

The PR body lists three deviations and not these two, so a reader checking the PR against the
issue would see a silent change. **Fix inline:** add the two bullets to the PR body's "Deviations
from the issue".

## Checked, not a finding

- **"No PSRAM use."** The transient ~4.2 KB `BibleBookNameTable` at menu open is labelled in spec
  A17, in the spec's Memory section and in the PR body. It lives for the call only
  (`collectRecentChips`) and serves the owner-approved mockup's abbreviated chips. The issue's
  sentence reads "about 1–1.5 KB RAM while held", which is about the resident store, so this is a
  reasonable, disclosed reading. It is not a reversal that needs the owner.
- **Tests exercise behaviour.**
  - The budget test builds a real worst-case document and measures it with `measureJson`,
    rather than restating the constant.
  - The refusal tests assert the card's bytes are unchanged through the in-memory Storage fake.
  - `pickRecent` and `fitChips` are tested for order and drop semantics.
  - The capture wiring in the reader cannot run on the host. It is covered by the PR's seven
    device checks, which match the spec's list (spec:480-497).

VERDICT: CLEAR
