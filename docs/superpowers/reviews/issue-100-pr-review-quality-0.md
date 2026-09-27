Tier: heavy

# PR #145 — code quality review 0

Scope: `gh pr diff 145`, code and tests only (`lib/Serialization`, `src/study/PassageFile.cpp`,
`src/util/{Bookmark,Highlight}File.cpp`, `test/`). The planning documents under `docs/superpowers/`
were read only to check whether a decision was deliberate.

## What was checked and holds

- **Reuses the existing buffered writer; does not add a second one.** The streaming save goes through
  the existing `serialization::BufferedFileWriter` (`lib/Serialization/BufferedFile.h:26-70`), the
  same writer `BookMetadataCache` uses (`lib/Epub/Epub/BookMetadataCache.cpp:84,135,190`). Short
  writes are reported through its existing `okFlag`/`flush()` contract. They are not tracked a second
  time in the adaptor: `JsonFileWriter::write` returns the full length and says why
  (`lib/Serialization/PersistableStore.cpp:34-37`).
- **The reader was moved, not copied.** `HalFileReader` and `readInto` were deleted from
  `src/study/PassageFile.cpp` and now appear once, as `PersistableStoreBase::readDocFromFileStreamed`
  (`lib/Serialization/PersistableStore.cpp:140-160`). The body is the same apart from the log tag, and
  it matches `readDocFromFileChecked`'s shape: the same `classifyDocRead` triples, `LOG_ERR` before
  each error return, and the `"PERSIST"` tag. Both ArduinoJson adaptors stay in the one TU the header
  comment reserves for them (`PersistableStore.h:55-57`).
- **Consistent error handling.** `writeDocStreamed` logs and returns false on open failure, short
  write and close failure (`PersistableStore.cpp:76-89`). This matches the old
  `writeFile`-then-`LOG_ERR` path. The explicit `file.close()` falls under CLAUDE.md's "close before
  reopen/rename" exception, and the comment on it gives the reason (`:83-84`), so it is not
  redundant.
- **Resources.** The buffer is one 512 B `makeUniqueNoThrow` allocation, and the PR explains why it
  stays in internal SRAM (`:16-17`). The `BufferedFileWriter` is scoped inside `serializeInto` so it
  flushes before the close (`:63-70`), and a comment explains why.
- **Comments.** No comment restates the next line. The ones added carry a reason: the buffer size,
  unbuffered `storageMutex` cost, the ignored mkdir result, and close-before-rename. The stale
  comments at `BookmarkFile.cpp:64` and `HighlightFile.cpp:40` were deleted rather than left false.
  The per-store `Storage.mkdir` calls that remain are redundant, but the plan keeps them deliberately
  (plan `:651,662`, spec non-goal), so this review does not raise them. No dead or commented-out code.
- **Tests are designed, not just present.** `failWritesAfter` extends the fake's existing
  `failX`/`clearFailures` hook family (`test/stubs/HalStorageFake.cpp:30-33,65-68,84-87`) and has its
  own fake-level tests (`HalStorageFakeTest.cpp:113-133`). The short-write cases hit both paths: a
  short write inside the buffer (3 B, checks the primary is byte-identical and the `.tmp` is kept) and
  one past it (1500 B of a ~4 KB doc) (`AtomicWriteTest.cpp:71-85`). The PR states both tests were
  seen to fail without the `flush()` check. The 60 KB case asserts the exact byte count and a whole
  read-back (`:87-95`). The passage round trip checks its fixture really exceeds the cap before
  relying on it (`PassageFileTest.cpp:73`). A truncated streamed read would fail to parse and return
  `Failed`, so passage-count equality is enough to prove it here.

## Findings

### MINOR-1 — `readDocFromFileStreamed` is passed without `&`, unlike every sibling call site

`src/study/PassageFile.cpp:22` passes `PersistableStoreBase::readDocFromFileStreamed`. The other four
`loadAdopting` callers all take the address explicitly:
`&PersistableStoreBase::readDocFromFileChecked` (`src/study/TagPaletteFile.cpp:22`,
`src/study/ChapterCompletionFile.cpp:22`, `src/util/BookmarkFile.cpp:38`,
`src/util/HighlightFile.cpp:27`). The two forms behave the same. This PR is what made PassageFile
pass a `PersistableStoreBase` member, so it should match its siblings. Fix: add `&`.

### MINOR-2 — The corrected `TempAdoption.h` comment was not reflowed

`lib/Serialization/TempAdoption.h:26-30`: the edit ends a line early at
`// with O_TRUNC -- and a transient SD read`, and the sentence then carries on in a new line. The new
wording is correct: `SDCardManager::openFileForWrite` opens with `O_RDWR | O_CREAT | O_TRUNC`
(`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:337`). Only the line break is off.
Fix: reflow the comment block to the file's line width.

## Verdict

The change reuses the buffered writer the repo already has, moves the streaming reader instead of
duplicating it, and keeps the store's established error and logging shape. Its tests check the
failure paths the fix is about. The two MINORs are cosmetic and can be fixed inline.

VERDICT: CLEAR
