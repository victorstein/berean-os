Tier: heavy

# PR #159 code quality review, pass 0

Scope: `git diff main...HEAD` over `lib/`, `src/`, `test/` and `docs/file-formats.md` on
`fix/152-batch-2-follow-ups`. Host suite rebuilt and run in this worktree
(`cmake --build build/test`, `ctest --test-dir build/test`): 998/998 pass.

## Summary

The change is almost entirely deletion, and the one new mechanism (item 1) copies a pattern already
in the repo instead of adding a second one. There are no BLOCKER or MAJOR findings.

What I checked and found sound:

- **The buffered reader mirrors the writer beside it.** `JsonFileReader`
  (`lib/Serialization/PersistableStore.cpp:22-33`) has the same shape as `JsonFileWriter`
  (`:39-55`): a thin ArduinoJson adapter that holds a reference to a `serialization::` buffered
  wrapper, with the buffer size a named `constexpr` next to `WRITE_BUFFER_BYTES` (`:15-16`). It reuses
  the existing `serialization::BufferedFileReader` (`lib/Serialization/BufferedFile.h:74-146`),
  which `BookMetadataCache.cpp:191-192` already uses, and does not add a new buffer class. The PR says
  a failed allocation falls back to unbuffered reads. That is true: `BufferedFile.h:77` sets
  `cap = 0` on OOM, and `read()` then takes the passthrough branch at `:82-87`. So the reader needs no
  OOM path of its own, the same way the writer needs none. Renaming `HalFileReader` to
  `JsonFileReader` makes the pair's names symmetric.
- **The `mkdir` removals are safe.** Both write entry points call `ensureParentDirectory`
  (`PersistableStore.cpp:94,104`), and `HalStorage::mkdir` defaults `pFlag = true`
  (`lib/hal/HalStorage.h:34`), so it creates parents recursively. Every removed call came directly
  before `writeDocToFileAtomic`. The `mkdir` calls that remain (`UnitIndexCache.cpp:92`,
  `CatalogIndexStore.cpp:206`, among others) sit in front of raw `openFileForWrite` or downloader
  paths, which do not create their own parent, so leaving them is correct. The includes that were
  dropped (`HalStorage.h`, `SdPaths.h`) were used only by those calls. The surviving includes are
  still used: `BookmarkFile.cpp:4` (`Storage.exists`, `:21`), `PubKeyRegistry.cpp:5` (`:72`), and
  `BookmarkUtil.h` (`:36,52`).
- **Nothing dead is left behind.** A tree-wide grep for `HomeMenuItem`, `getPressedFrontButton`,
  `rowTouch`, `colTouch`, `wasTapInRect`, `getDataFromBook`, `updateBook`, the no-argument
  `getThumbBmpPath()` and `getBookThumbBmpPath` finds no remaining uses outside `docs/superpowers/`.
  The `(void)initialMenuItem;` shim and the comment justifying it are gone with the parameter
  (`ActivityManager.cpp:241-245`). The callers of `updatePath` (`EpubReaderActivity.cpp:150`,
  `PublicationDownloader.cpp:120`) still compute `oldCachePath`/`newCachePath`, but they need those
  values for their own cache-directory rename, so they are not dead.
- **Budget constants are re-derived, not hand-edited.** `ENTRY_OVERHEAD_BYTES` drops to 34
  (`RecentBooksDoc.h:47`). The existing measurement test,
  `DocumentOverheadMatchesTheMeasuredConstants`, still checks that constant against `measureJson`, and
  the 9,967 figure is checked by `IsTighterThanTheDefaultAndClearOfTheReadCap`. The comments on the
  constant and on `normalise()` were updated with the field (`RecentBooksDoc.h:46,73`;
  `RecentBooksStore.h:22`).
- **The tests are designed, not just present.** `TheStreamedReadPullsTheFileInChunksNotBytes`
  (`test/storage_io/AtomicWriteTest.cpp:97-106`) checks what the change is for (reads per file, below
  600 against about 118 expected and 60,008 before), using a fake hook that has its own contract
  test (`HalStorageFakeTest.cpp:157-169`, which covers both `read` overloads, isolation by path, and
  `reset()`). The hook follows the fake's existing per-path `std::map` style (`writeLimit`,
  `HalStorageFake.cpp:33-34`), and `reset()` clears it with no extra code because it rebuilds
  `FakeCard{}` (`:63`). `AFileStillCarryingACoverPathLoadsAndDropsItOnTheNextSave` pins the
  decision-d1 contract: a legacy key does not force a resave, and the next save drops it. The older
  fixtures that still set `coverBmpPath` (`RecentBooksDocTest.cpp:231,308`) correctly model files
  already on cards in the field.

## Findings

### MINOR 1: `ToJsonWritesNoCoverPath` duplicates its neighbour and skips the file's helper

`test/recent_books_doc/RecentBooksDocTest.cpp:262-270` asserts that `toJson` writes no
`coverBmpPath`. The next test already makes that exact assertion on the resave
(`:288-290`, `EXPECT_TRUE(resaved["books"][0]["coverBmpPath"].isNull())`). The new test also builds
its `RecentBook` field by field, when every sibling in the file builds one with `makeBook(...)`
(22 uses). Folding the two into one test, or at least using `makeBook("/books/a.epub", "A", "")`,
would bring it in line. The test is not wrong, just redundant.

### MINOR 2: the `goHome` comment lists the launcher's tiles

`src/activities/ActivityManager.cpp:242-243` names the four tiles and Continue Reading. This is
accurate today (`LauncherActivity.cpp:500-513`), but it copies a list that belongs to
`LauncherActivity` into a routing function that only constructs the activity. The next tile change
will make it stale without anyone noticing. A shorter comment would not drift, for example
"bereanOS's home is the launcher", or the comment could simply be dropped, since the next line says
the same thing.

VERDICT: CLEAR
