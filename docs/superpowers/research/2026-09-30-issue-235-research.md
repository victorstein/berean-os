# Issue #235 research — tag actions outside the Bible

Branch `fix/235-bible-only-tags`, base `58546aaf` (release 1.29.2). Every citation below
was read in this worktree.

## Who owns the behaviour

| Concern | File |
|---|---|
| Which menu entries exist | `src/activities/reader/ReaderMenuModel.h:76-99` (`build`) |
| Menu actions → labels | `src/activities/reader/EpubReaderMenuActivity.cpp:44-45` (`HIGHLIGHT_PASSAGE` → `STR_TAG`), `:68-69` (`HIGHLIGHTS` → `STR_HIGHLIGHTS`) |
| Menu inputs | `EpubReaderActivity::openReaderMenu`, `src/activities/reader/EpubReaderActivity.cpp:269-302`; constructor forwards to `ReaderMenuModel::Inputs` at `EpubReaderMenuActivity.cpp:112-114` |
| What each action does | `EpubReaderActivity::onReaderMenuConfirm`, `EpubReaderActivity.cpp:789-800` |
| Tags list | `EpubReaderActivity::openHighlights`, `EpubReaderActivity.cpp:354-376` |
| Tagging a passage | `EpubReaderActivity::openHighlightPassage`, `EpubReaderActivity.cpp:326-346` |
| Finding the Bible | `LauncherActivity::resolveTargets`, `src/activities/launcher/LauncherActivity.cpp:109-127`, plus `LauncherBible.h:47-73` |
| Opening another book from the reader | `ActivityManager::goToReader`, `src/activities/ActivityManager.cpp:208-229` |
| Entry intent routing | `src/activities/reader/ReaderEntryIntent.h:36-50`, consumed in `EpubReaderActivity::onBookLoaded`, `EpubReaderActivity.cpp:937-969` |
| Host tests | `test/ui_layout/ReaderMenuModelTest.cpp`, `test/ui_layout/ReaderEntryIntentTest.cpp`, `test/launcher_bible/LauncherBibleTest.cpp` |

## Current control flow

### The menu model

`ReaderMenuModel::build` (`ReaderMenuModel.h:76-99`):

- `:82` `if (in.hasHighlights) m.addQuick(A::HIGHLIGHT_PASSAGE);` — any book.
- `:85` `TAGS_HERE` needs `isBible && hasHighlights && tagsHereCount > 0`.
- `:86` `if (in.hasHighlights) m.addRow(A::HIGHLIGHTS);` — any book.

`Inputs` (`:37-45`) has no notion of "a Bible exists elsewhere on the card". `hasHighlights`
is a compile-time `BOARD_HAS_PSRAM` constant in `openReaderMenu` (`EpubReaderActivity.cpp:274-278`),
so on the X4 Pro it is always `true`.

### Highlights (the Tags list)

`HIGHLIGHTS` → `openHighlights()` with no filter (`:793-795`). It pushes `HighlightsActivity`
over the reader (`:363-365`), and on a result `navigateTo`s inside **the current epub**
(`:366-375`). `HighlightsActivity` lists `STUDY`'s passages, and `STUDY.openPublication` is called
for every book in `loadBook` (`:226`), keyed by that book's pubkey
(`src/study/StudyStore.cpp:44-52`, `PassageFile::load(pubKey_, …)` at `:60`). So from a
Watchtower the list is the Watchtower's passages — empty for this owner.

### Highlight passage (the "Tag" quick action) — four entry points, not one

`openHighlightPassage()` pushes `PassageSelectActivity` for the current page
(`:340-345`), which calls `STUDY.addPassage(...)`
(`src/activities/reader/PassageSelectActivity.cpp:355`). Every passage write funnels through
`StudyStore::save()` → `PassageFile::save(pubKey_, …)` (`src/study/StudyStore.cpp:110-117`),
i.e. `/.berean/passages/<current pubkey>.json` (`src/study/PassageFile.h:10`).

The issue names only the menu tile, but `openHighlightPassage` is reached from **four** places,
none gated on `isBible()`:

1. Menu quick action `HIGHLIGHT_PASSAGE` — `EpubReaderActivity.cpp:789-791`.
2. Touch long-press on a word — `:462-465` → `openHighlightPassageAt` (`:318-321`).
3. Confirm long-press with `longPressMenuFunction == LP_MENU_HIGHLIGHT` — `:514-518`.
4. Home-key hold with `LP_MENU_HIGHLIGHT` — `:538-540`.

The acceptance criterion "neither creates a passage in the current publication. No
`/.berean/passages/<non-bible>.json` is written" is only met if 2–4 are also handled. The issue's
"Decided" section speaks only of the menu entries; what a long-press should do outside the Bible
(nothing, or also open the Bible's tags) is a question for the spec.

### Tags here

`TAGS_HERE` → `openHighlights(currentSpineIndex)` (`:797-799`), already Bible-only in the model
(`ReaderMenuModel.h:85`). Unaffected.

### How Home opens the Bible's tags

`HomeTargets.h:67-68`: `Target::Tags` → `reader(Kind::Tags)` when `state.hasBible`, else
`DownloadBible`. `LauncherActivity::openReader` (`LauncherActivity.cpp:429-445`) ends in
`activityManager.goToReader(biblePath, route.allowFastInitialRefresh, intent)` (`:444`). In the
new reader, `onBookLoaded` routes `Kind::Tags` → `Route::Highlights` → `openHighlights()`
(`EpubReaderActivity.cpp:965-967`). A passage picked there `navigateTo`s inside the Bible, which
is the issue's "Picking a passage opens it in the Bible".

Note: `ReaderEntryIntent::route(Kind::Tags, isBible)` returns `Route::Highlights` **regardless of
`isBible`** (`ReaderEntryIntent.h:44-45`). If the resolved "Bible" path were in fact a non-Bible,
the reader would open that book's own (empty) list rather than refuse. Every other Bible-only
kind (`OpenAt`, `Search`) returns `Route::None` outside a Bible (`:38-43`).

### How the launcher finds the Bible

`resolveTargets` (`LauncherActivity.cpp:109-127`) runs `resolveBible` (`LauncherBible.h:66-73`)
over `BIBLE_LOOKUP_ORDER` = Registry, CardScan, Recents (`:49-50`):

- Registry: `PubKeyRegistry::findBySymbol({BIBLE_SYMBOL})` (`LauncherActivity.cpp:116`). It reads
  the registry JSON from SD and calls `Storage.exists` on the hit
  (`src/study/PubKeyRegistry.cpp:53-76`).
- CardScan: `LauncherActivity::findBibleOnCard()` (`:201-207`), `CardBooks::list()` + `isCdnNamedCopyOf`.
  `CardBooks::list` is a directory listing, "cheap enough to run when a screen opens"
  (`src/util/CardBooks.h:6-11`).
- Recents: `LauncherActivity::findBibleInRecents(recents)` (`:211-218`), needs
  `RECENT_BOOKS.loadFromFile()` (`:101`).

`findBibleOnCard` and `findBibleInRecents` are **private static members of `LauncherActivity`**
(`LauncherActivity.h:60-65`). The reader cannot call them today; reusing "however the launcher
finds it" means lifting the lambda at `LauncherActivity.cpp:112-123` and those two helpers into
something both activities can call. `resolveBible` and the pure predicates are already
host-tested (`test/launcher_bible/LauncherBibleTest.cpp`). The rest of `resolveTargets` (cover
thumbnail, `APP_STATE.bibleCoverPath` write at `:128-135`) is launcher-only and must not move.

The reader also self-registers a Bible it opens (`EpubReaderActivity.cpp:244-253`), so after the
Bible has been opened once the Registry step hits.

## Leaving the publication saves its position

- Page position is saved on every render when it changed (`EpubReaderActivity.cpp:1395-1402`),
  so it is already on SD before the menu opens.
- `goToReader` → `replaceActivity` defers the swap (`ActivityManager.cpp:184-196`); the old
  activity's `onExit` then runs (`exitActivity`, `:176-182`).
- `EpubReaderActivity::onExit` records the place and calls `ReaderActivity::onExit`
  (`EpubReaderActivity.cpp:1507-1512`), which saves `APP_STATE` atomically
  (`src/activities/reader/ReaderActivity.cpp:67-76`).

The nearest existing reader-to-another-book handoff is the end-of-book menu,
`ReaderActivity::handleEndOfBookMenu` → `activityManager.goToReader(openPath)`
(`ReaderActivity.cpp:97-101`). The nearest with an entry intent is `LauncherActivity::openReader`
(`LauncherActivity.cpp:444`). No reader call site passes an intent today.

## Existing tests that the change touches

`test/ui_layout/ReaderMenuModelTest.cpp`:

- `NonBibleGetsThreeQuickActionsWithoutSearch` (`:70-73`) and `NonBibleRowOrder` (`:96-101`)
  expect `HIGHLIGHT_PASSAGE`/`HIGHLIGHTS` in a non-Bible — still true when a Bible resolves.
- `EveryLegacyItemSurvivesForEveryFlagCombination` (`:135-152`) enumerates six flags
  (`Flags`, `:12-19`) and asserts every legacy item survives. A new "Bible reachable" input that
  hides both entries in a non-Bible without one will need that test's expectation extended, not
  loosened.
- `WorstCaseCountsOnTheX4Pro` (`:124-132`): non-Bible is 3 quick + 11 rows today; `MAX_ROWS = 14`
  (`ReaderMenuModel.h:34`). Hiding items cannot overflow.

`ReaderMenuModelTest` is registered in `test/ui_layout/CMakeLists.txt:55-68`; a new test in that
file needs no CMake change.

## Labels

- English: `STR_TAG: "Tag"` (`lib/I18n/translations/english.yaml:371`), `STR_HIGHLIGHTS:
  "Highlights"` (`:380`), `STR_TAGS: "Tags"` (`:372`).
- Spanish: `STR_TAG: "Etiqueta"` (`spanish.yaml:424`), `STR_TAGS_HERE` (`:422`). **Spanish has no
  `STR_HIGHLIGHTS`** (`grep -n STR_HIGHLIGHT spanish.yaml` → no output), so the row already falls
  back to English "Highlights" on a Spanish device. If the spec keeps the label this is
  pre-existing; if it adds one, the YAML is a shared file (`.claude/agents/ui-dev.md`, "Shared
  files") and the key must be handed off in the PR body.

## Installed tools

- `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19` (not on `PATH`;
  see the fresh-worktree bootstrap note).
- `cmake --version` → `cmake version 4.4.2`.
- googletest `GIT_TAG v1.17.0` (`test/CMakeLists.txt:15-17`); ArduinoJson `v7.4.2` (`:31`).
- C++: `-std=gnu++2a` (`platformio.ini:40`, per `CLAUDE.md`).

## Nearest example of this kind of change

`75c65e93 feat: add recent bible places and reader entry intents (#221)` introduced
`ReaderEntryIntent`, `Kind::Tags` and `TAGS_HERE`; `829e7f71 feat: redesign Home for a reference
Bible (#223)` wired Home → Tags through `goToReader(biblePath, …, intent)`. Both keep the rule in a
pure header (`ReaderMenuModel.h`, `ReaderEntryIntent.h`, `HomeTargets.h`) with a host test in
`test/ui_layout/`, and do the I/O in the activity. The model for this change is:
`ReaderMenuModel::Inputs` gains an input, `build` gets the rule, `ReaderMenuModelTest` covers
Bible / non-Bible × Bible resolvable / not; `EpubReaderActivity` resolves the Bible and branches
the two actions to `activityManager.goToReader(biblePath, false, ReaderEntryIntent::of(Kind::Tags))`.

## Open questions for the spec

1. **Long-press tagging outside the Bible** (entry points 2–4 above). Recommend: make
   `openHighlightPassage` itself refuse outside the Bible so no route writes a non-Bible passage;
   whether a long-press then does nothing or also jumps to the Bible's tags is a UX call.
2. **Where the shared Bible resolver lives.** Lift the three-step lookup out of
   `LauncherActivity` into a helper both activities call (same `ui` surface; `LauncherBible.h`
   stays the pure host-tested half).
3. **When the reader resolves the Bible.** At menu open costs a registry JSON read + `exists`
   (+ a directory listing on a miss) each time; once in `loadBook` costs it on every book open.
   Resolving only when `!isBible()` bounds it to non-Bible books.
4. **`ReaderEntryIntent::route(Tags, false)`** returns `Highlights`. Consider `None` outside a
   Bible, matching `OpenAt`/`Search`, with a `ReaderEntryIntentTest` case.

## Tier

Standard holds: every file is under `src/activities` (reader, launcher) or `test/ui_layout` /
`test/launcher_bible`. No on-disk format, store, migration or other surface changes; no
`/.berean/passages/*.json` is touched. A new label, if chosen, is a hand-off line, not an edit.
