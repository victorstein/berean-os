Tier: heavy

# Issue #201 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-30-issue-201-plan.md`, reviewed against the spec
`docs/superpowers/specs/2026-09-30-issue-201-design.md` (cleared at pass 1). The tree is the
branch head (`4a6ba115`). I checked every code excerpt the plan quotes or replaces against the file
it names.

## Findings

### 1. MAJOR — `OneEntryMeasuresTheNamedOverheads` asserts the wrong size, and Step 2.6 tells the implementer to "fix" correct constants

- **Claim.** Plan `:255-261` expects one entry to serialise to
  `DOC_WRAPPER_BYTES + ENTRY_OVERHEAD_BYTES + 9 + 5 + 1 + 1` = **65** bytes. The comment says "the
  zero spine and offset one each".
- **Problem.** The test builds its place with `makePlace(1, 1, 1, "")`, and `makePlace` sets
  `p.spineIndex = 100` (plan `:232`). So `"s"` serialises as `100`, which is 3 bytes, not 1. The real
  document is `{"v":1,"places":[{"u":"v:1:1:1:0","r":"","c":false,"s":100,"o":0}]}`, which is **67**
  bytes (measured with `python3 len(...)`). Both `EXPECT_EQ`s fail. Step 2.6 (`:744-747`) then says:
  if this test fails, "re-measure and fix the **constant**, never loosen the assertion". The
  constants are correct: 19 / 30 / 28 / 2,118 all check out, and `AWorstCaseDocumentMeasuresExactlyTheBudget`
  pins them. An implementer who follows the plan literally would bump `ENTRY_OVERHEAD_BYTES` or a
  sibling constant. That breaks the worst-case test's hard-coded `2118u` (`:277`) and leaves two
  tests that contradict each other.
- **Evidence.** Plan `:226-235` (`makePlace`, `spineIndex = 100`) and `:255-261`. `unitToCompact`
  gives `v:1:1:1:0`, 9 B (`lib/StudyStore/StudyStore/Unit.cpp`, `unitToCompact`).
- **Fix.** In that test, use a place with a zero spine: `Place p = makePlace(1, 1, 1, ""); p.spineIndex = 0;`
  and pass `{p}`. The existing expressions (`… + 9 + 5 + 1 + 1`, `65u`) then hold. The other way is
  to keep `makePlace` and assert `… + 9 + 5 + 3 + 1` and `67u`. Either is an inline fix.

### 2. MINOR — `RecordingTheHeadAgainDoesNotWrite` cannot fail

- **Claim.** Plan `:884-890` arms `failWritesTo(PATH)` and `failWritesTo(PATH + ".tmp")`, records the
  head again, and asserts the file still exists ("a write was attempted and removed the file").
- **Problem.** `writeDocToFileAtomic` writes `<path>.tmp` first and returns false when that fails,
  before it ever touches `path` (`lib/Serialization/PersistableStore.cpp:109-111`). So with these
  hooks armed, an attempted save leaves `PATH` intact, and the test passes whether or not `record()`
  wrote. The failure message describes something the fake cannot do. The rule itself is covered by
  `PlacesDocRecord.RecordingTheHeadAgainChangesNothing`, so this is MINOR.
- **Evidence.** `PersistableStore.cpp:111` returns before the `Storage.remove(finalPath)` at `:116`.
  `test/stubs/HalStorageFake.h` (`failRenamesFrom`: "rename() away from this path fails and changes
  nothing").
- **Fix.** Replace both `failWritesTo` lines with `storage_fake::failRenamesFrom(PATH + ".tmp");`.
  An attempted save then writes the `.tmp`, removes `PATH` (`:116`) and fails the rename, so
  `bytesOn(PATH)` becomes `"<absent>"` and the assertion catches it.

### 3. MINOR — Tasks 2 and 3 commit firmware sources without building the firmware

- **Claim.** Step 2.7 commits `src/util/PlacesDoc.cpp` and Step 3.6 commits `src/PlacesStore.cpp`
  (`:749-755`, `:1047-1053`), each after host tests only.
- **Problem.** PlatformIO compiles all of `src/`, so both commits change the firmware build. The
  first `pio run` is Step 4.5 (`:1165-1167`), which itself expects a possible include-path error
  ("`PlacesDoc.cpp` and `PlacesStore.cpp` are compiled into the firmware here for the first time").
  Any such error would be fixed in Task 4's commit, so Tasks 2 and 3 are not verified as committable.
- **Fix.** Add `~/.platformio/penv/bin/pio run -e x4pro` to Step 2.6 and Step 3.5, before their
  commits. Step 4.5's note about include paths then moves to Step 2.6.

### 4. MINOR — Step 11.8 calls `python`, which is not on this host, for a step the build already does

- **Claim.** Step 11.8 (`:2237-2242`) runs `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`.
- **Problem.** `which python` finds nothing on this host; only `python3` exists (`/opt/homebrew/bin/python3`).
  Run literally, the step fails. It is also redundant: `platformio.ini:134` runs
  `pre:scripts/gen_i18n.py` on every `pio run`.
- **Fix.** Drop the line (the `pio run` on the next line regenerates the tables), or write `python3`.

### 5. MINOR — The Recent-chip selection rule is pure logic with no host test

- **Claim.** A14 (spec `:133-140`) defines which places become chips: newest first, skip the
  (book, chapter) on screen, at most `MAX_RECENT_CHIPS`. The plan implements this inline in
  `EpubReaderActivity::collectRecentChips` (`:2188-2196`), which is firmware-only.
- **Problem.** Tasks 4, 5, 7, 9, 10 and 11 start without a failing test. For most of them that is
  justified, because they wrap `Epub`, `UnitIndexCache` or FreeInkUI. The chip selection is not in
  that category: it is a filter over `std::vector<Place>`, of the same kind as `PlacesDoc::record`,
  and no test covers it. A slip such as comparing the whole `Unit` instead of (book, chapter), or
  applying the cap before the skip, would only show up on the device.
- **Fix.** Add
  `size_t PlacesDoc::pickRecent(const std::vector<Place>&, const std::optional<study::Unit>& onScreen, Place* out, size_t max)`
  to Task 2 with `PlacesDocTest` cases (the on-screen chapter is skipped; any verse of it is
  skipped; order is kept; the cap applies after the skip). `collectRecentChips` then calls it.

## Checked and not findings

- **Spec coverage.** Every assumption A1–A21, every error-table row and every host test in the
  spec's Testing strategy maps to a step. Capture sites: `pageTurn` ×3, `skipPages` ×2,
  `navigateTo` with the capture before `clearDeferredReposition`, and `onExit`.
  - `pageShown` is set where the plan says, after `lastRenderCompleteMs` (`EpubReaderActivity.cpp:1262`).
  - The self-load and refusal guard are covered, including the lazy-load test as its own executable.
  - `peekUnits` is a faithful extraction of `unitsFor`'s cache and entry path
    (`UnitIndexCache.cpp:225-247`).
  - `WhenMissing::Fail` returns before any lock or framebuffer loan (`SpineHtmlStream.cpp:28-31`).
  - The intent routing is host-tested.
- **Ordering.** `onBookLoaded` runs after `loadBook` has set `epub` and opened `STUDY`
  (`ReaderActivity.cpp:55-61`, `EpubReaderActivity.cpp:145-220`). `ActivityManager` holds
  `RenderLock` around both `exitActivity` and the stacked `onExit` (`ActivityManager.cpp:141-150`,
  `:175-181`), so `captureLeftPlace` in `onExit` has the lock it needs.
- **Consistency.** Names and signatures stay the same across tasks: `getPlaces`, `placeAt`,
  `locatePlaceAtHint`, `locatePlace`, `formatReference(book, unit, chapterOnly)`,
  `formatChipLabel`, `RecentChipLabels`, `CancelTo` and `onReaderMenuConfirm(const MenuResult&)`.
  The quoted "before" code matches the tree (`pageTurn`, `skipPages`, the `SELECT_CHAPTER`/`SEARCH_BIBLE`
  cases, `openReaderMenu`, `ReaderActivity::create`). No other `switch` over `ReaderMenuAction`
  lacks a `default`: `tileIconFor` and `refreshRowStates` both have one.
- **Layout arithmetic.** The expected `plate.y`, `recent.{x,w}` and the `fitChips` cases all agree
  with `compute` and with `x4pro()` in `ReaderMenuSheetLayoutTest.cpp`.
- **`test/CMakeLists.txt` is not on a `FILES:` line.** Step 0.2 edits it locally, and every commit
  reverts it first. The `--files` lock only schedules sibling tasks whose path prefixes overlap
  (herdr-pipeline skill). An edit in an isolated worktree that is never committed cannot collide
  with a sibling. The same pattern is established in the plans for #101, #108, #111, #185, #194 and
  #204. Every committed path is covered by a `FILES:` line (`:8-21`).
- **Committing the translation keys.** The spec lists them as a hand-off. The plan commits them
  because the firmware does not build without them, which is what #217 and #180 did, and it lists
  them in `FILES:` and in the PR body.
- **Dropping `offsetHint`.** A labelled deviation (`:53-56`). The reader resolves `OpenAt` through
  `locateUnit(unit, spineHint)`, which never reads an offset, and #203 builds its intents through
  `openAt` or the public fields. No decision is reversed.
- **`STR_RECENT` is stored as "RECENT".** The renderer has no case transform, so this is how the
  caption is drawn upper-case.
- **`git push` in Step 12.4.** This matches the pipeline plans for #199 and #200.

VERDICT: CLEAR
