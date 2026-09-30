Tier: standard

# Issue #235 spec review, pass 0

Spec: `docs/superpowers/specs/2026-09-30-issue-235-design.md`.
Checked against issue #235 (`gh issue view 235 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-30-issue-235-research.md`, and the code at `e43bb20c`
(`git diff --stat 58546aaf HEAD` shows only the two docs files, so the spec's base is the code).

## What was checked and holds

- **The four passage-write entry points.** `grep -rn "openHighlightPassage(" src` finds exactly the
  menu case (`EpubReaderActivity.cpp:790`), `openHighlightPassageAt` (`:321`, called from the touch
  block at `:466`), the Confirm hold (`:516`) and the Home-key hold (`:540`).
  `grep -rn "PassageSelectActivity>" src` has one hit (`:347`), and `STUDY.addPassage` is only called
  from `PassageSelectActivity.cpp:355`. So the A5 guard at the top of `openHighlightPassage` really
  is a single choke point in front of every passage creation, and acceptance criterion "no
  `/.berean/passages/<non-bible>.json` is written" holds. Nothing else writes on an empty document:
  `StudyStore::save()` is reached from mutators that need an existing passage, and the
  `repairTexts` save at `StudyStore.cpp:506` only runs when `undo` is non-empty.
- **Menu model.** `ReaderMenuModel.h:82` / `:86` are the unconditional adds and `:85` is the only
  `isBible` gate, as the spec says. `tagTarget` as written gives the issue's three outcomes, and the
  eight-case `TagTargetRule`, the two new build tests and the 128-mask extension of
  `EveryLegacyItemSurvivesForEveryFlagCombination` (`ReaderMenuModelTest.cpp:135-152`; removing an
  absent item with `std::remove` is harmless when `hasHighlights` is false) cover the acceptance
  criterion "Bible and non-Bible, with and without a resolvable Bible". A two-tile sheet is already
  a live case (`TagQuickActionNeedsHighlights`, `:75-80`) and the sheet layout clamps generically
  (`ReaderMenuSheetLayout.h:78`).
- **Handoff and position (A11, A12).** `goToReader`'s default is `allowFastInitialRefresh = false`
  (`ActivityManager.h:86`). `ReaderActivity::create` only constructs (`ReaderActivity.cpp:25-37`);
  the book loads in `onEnter` after `replaceActivity`'s deferred swap runs `exitActivity` on the old
  reader (`ActivityManager.cpp:143-160`, `:176-196`), so two books are never loaded at once. The
  end-of-book `goToReader(openPath)` (`ReaderActivity.cpp:100`) is a precedent for a reader
  replacing itself. Position is saved on render (`EpubReaderActivity.cpp:1395-1402`) and at
  `onExit` (`:1507-1512`, `ReaderActivity.cpp:67-76`). The result handler at `:292-300` does nothing
  after `onReaderMenuConfirm`, so `return` there is safe.
- **Resolver (A9).** `resolveTargets` (`LauncherActivity.cpp:100-137`), `findBibleOnCard` (`:201-207`),
  `findBibleInRecents` (`:211-218`) and the private statics (`LauncherActivity.h:60-65`) are as
  cited. `RECENT_BOOKS.loadFromFile()` runs at boot (`main.cpp:420`), and the open book is added
  on reader entry (`ReaderActivity.cpp:62`), so the reader can read recents without reloading.
  The reader already includes `LauncherBible.h` (`EpubReaderActivity.cpp:48`).
- **A6.** `route(Tags, …)` is unconditionally `Highlights` (`ReaderEntryIntent.h:44-45`); `OpenAt`
  and `Search` are `None` outside a Bible (`:38-43`); `Route::None` logs and opens normally
  (`EpubReaderActivity.cpp:943-948`). The only producers of `Kind::Tags` are Home
  (`HomeTargets.h:68`) and the new reader handoff, and `HomeTargetsTest.cpp:46` only asserts the
  intent kind, so no other test pins the old routing. "Bible" is one consistent notion across the
  firmware (`EpubReaderActivity.h:94`, `StudyStore.cpp:45`, `PubKey.h:24`), so a book for which
  `isBible()` is false never holds the Bible's passages, and `None` cannot hide real Bible tags.
- **Research corrections.** The touch call is at `:466` in `:461-469`, and the construction at
  `:346-351`. Both corrections are right.

## Findings

### MINOR 1 — `Inputs` is built positionally; "after `hasHighlights`" shifts every later argument

- **Claim.** "`Inputs` gains `bool bibleReachable = false;` after `hasHighlights`" (spec :133), and
  `EpubReaderMenuActivity` "forward[s it] into `Inputs`" (:176-177).
- **Problem.** The only production construction is positional aggregate initialisation. Inserting a
  field after `hasHighlights` without rewriting that line moves `Frontlight.present()` into
  `bibleReachable`, the rotation flag into `hasFrontlight` and `tagsHereCount` into `hasRotation`.
  The `int`→`bool` narrowing probably stops the build, but the spec does not mention the hazard,
  and a fix that appends the argument to the wrong position would still compile.
- **Evidence.** `EpubReaderMenuActivity.cpp:112-114`:
  `ReaderMenuModel::Inputs{isBible, hasFootnotes, hasBookmarks, hasHighlights, Frontlight.present(), BEREAN_CAP_ROTATION != 0, tagsHereCount}`.
  The test helper sets each field by name (`ReaderMenuModelTest.cpp:44-54`), so the host suite
  would not notice a wrong mapping.
- **Fix.** Say in the spec that the construction at `:112-114` switches to designated initialisers
  (`.isBible = …, .bibleReachable = …`). C++20 is enabled (`-std=gnu++2a`) and the field order
  already matches.

### MINOR 2 — The "No change to Home" non-goal contradicts A6

- **Claim.** Non-goals: "No change to Home, …" (spec :37). A6: "This also applies to Home → Tags"
  (:76).
- **Problem.** A6 is sound, and the spec says it will be called out in the PR. But it does change
  what Home → Tags does when Home's `biblePath` is a mistaken Recents guess: today that book's own
  tag list opens, and after A6 the book opens normally. The non-goal reads as if Home's behaviour is
  untouched, and a plan writer or the PR reviewer can take the two lines as contradicting each
  other.
- **Evidence.** `HomeTargets.h:68` → `LauncherActivity.cpp:444` → `onBookLoaded`
  (`EpubReaderActivity.cpp:942`). `Home` itself ships the Recents guess
  (`LauncherActivity.cpp:119-120`, `LauncherBible.h:75-81`).
- **Fix.** Change the non-goal to "No change to Home's code (`HomeTargets`, `LauncherActivity`
  beyond the lift in A9); A6 changes what the reader does with Home's Tags intent when the path is
  not a Bible."

### MINOR 3 — A10 rejects after the fact, so the open book can shadow a real Bible later in recents

- **Claim.** A10: "A resolved path equal to the open book is 'no Bible'" (spec :92-94).
- **Problem.** `findBibleInRecents` returns the *first* match. The open book is always at the front
  of recents, because `ReaderActivity::onEnter` adds it (`ReaderActivity.cpp:62`). If the open
  non-Bible's path contains `nwt` or its title contains "New World" / "Nuevo Mundo", the Recents step
  returns it every time. A10 then declares "no Bible" even when an unregistered Bible sits further
  down the same list. The impact is small: the registry step usually hits first, because the reader
  registers every Bible it opens (`EpubReaderActivity.cpp:244-253`).
- **Evidence.** `LauncherActivity.cpp:211-218` (first match wins); `LauncherBible.h:78-81`.
- **Fix.** Give `BibleFinder::find` an optional path to skip (`find(std::string_view exclude = {})`),
  and have the Recents step `continue` past it, instead of post-filtering the result. The launcher
  passes nothing, so its behaviour is unchanged. Keep A10's log for the case where the registry or
  card scan somehow returns the open book.

### MINOR 4 — Device step 6's expected log line is over-specified

- **Claim.** "Serial: `Bible for tags: <path> (registry)` once per Watchtower open" (spec :273).
- **Problem.** A8 makes resolution lazy: it runs on the first menu open or Tag hold, not when the
  Watchtower opens. Also, the lookup step is `(card scan)` or `(recents)` on a card whose Bible was
  never opened on the device. A tester who follows the step literally will report a false failure.
- **Evidence.** Spec A8 (:80-84) and the `bibleReachableForTags` description (:163-166);
  `bibleLookupName` (`LauncherBible.h:52-62`).
- **Fix.** "Serial: one `Bible for tags: <path> (<registry|card scan|recents>)` on the first menu
  open or Tag hold in each Watchtower session, and none on later menu opens."

VERDICT: CLEAR
