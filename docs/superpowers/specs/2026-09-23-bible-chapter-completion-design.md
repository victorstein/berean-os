# Bible chapter completion

**Date:** 2026-09-23
**Issue:** #35
**Status:** Implemented on `feature/35-bible-chapter-completion`

## Decisions the issue left open

The user chose **completion only**: chapters read, no plan, no calendar, no
nagging. "Read" means the user paged forward off the chapter's last page. No
dwell time — the user is the only audience.

### Where the record lives and its shape

- `/.berean/completion/<pubkey>.json`, keyed on the publication identity the
  study store already resolves (`StudyStore.cpp` `openPublication`, via
  `study::resolvePubKey`). Only the language-free `bible` key
  (`lib/StudyStore/StudyStore/PubKey.h`, `BIBLE_PUB_KEY`) records anything,
  because only a canon-verified Bible has canonical book numbers.
- Entries are canonical **book + chapter**, the same address a Verse unit
  carries (`Unit.h`), never a spine index. A replacement edition — including the
  other language — reads the same record.
- **JSON, not binary.** It reuses the exact read/adopt/atomic-write path the tag
  palette uses (`src/study/TagPaletteFile.cpp`), including `.tmp` promotion
  (`lib/Serialization/TempAdoption.h`), so no new storage mechanism is
  introduced. Per-book hex keeps a full record under 1 KB; the format is in
  `docs/file-formats.md`.
- Bounded at 4,096 bytes (`ChapterCompletion::SAVE_BYTE_BUDGET`), far below
  `SDCardManager::readFile`'s 50,000-byte cap, so the ordinary read is safe and
  no streaming reader is needed.

### RAM

The resident record is a dense bitmap, one bit per canonical chapter: 149 bytes
(`std::array<uint8_t, 149>`), a member of the `StudyStore` singleton — static
storage, no heap, alive for the process. It is loaded when the Bible opens and
cleared by `closePublication`. The issue's ~1.2 KB figure assumed a fixed
66 x 150 stride; the canonical chapter table (`canonicalChapterCount`, summing
to 1,189 and `static_assert`ed) makes the dense layout possible and also gives
the launcher its denominator.

### When it writes (the debounce)

`EpubReaderActivity::pageTurn` calls `StudyStore::markDocumentRead` from both
forward branches that leave the document, using `study::forwardTurnLeavesDocument`
as the branch condition itself so the trigger cannot drift from the turn. A
write happens **only when a bit flips** (`study::recordDocumentRead`), which is
at most once per chapter, at a chapter boundary where the reader is already
tearing down one section and loading the next. Re-reading writes nothing.

Batching to reader exit was rejected for the reason `StudyStore.h` already gives
for tag edits: a deferred write is lost on Power-off, and the next boot would
show chapters the user read as unread.

`skipPages` (a whole-chapter skip) and jumps from the chapter list do not count:
nothing was paged through.

### Concurrency and the owning task

Main task only. The call is made with the reader's `RenderLock` held — the
existing forward branch already took it (`EpubReaderActivity.cpp`, `pageTurn`),
and the end-of-book branch now does too — because the render task reaches the
same `UnitIndexCache` through `StudyStore::passagesInDocument`. The chapter is
resolved from `units_->unitsFor(spine)`, which the page just rendered asked
for, so it is normally the cached document and costs no I/O. SD access is
through HalStorage, which holds `storageMutex` per call.

### Failure

- Load `Failed` (unreadable, unparseable, newer version, or anything this
  firmware would not write) latches `completionSaveDisabled_` for the session,
  separately from the passage latch. The reader says so once per session, and
  only when the Bible is the book being opened
  (`StudyStore::takeCompletionLoadFailureNotice`).
- A failed save rolls the in-memory marks back so memory matches the card, and
  the reader shows a popup.

### Popups that could not be seen

`ReaderUtils::showMessage` drew its popup immediately, and every caller -- this
feature's, the highlight, tag and bookmark refusals -- then requested a render
or `finish()`ed, which painted straight over it. It now posts to
`src/activities/PostedMessage`, and the reader (`ReaderActivity::render`) and
every list screen (`UiListActivity::render`) draw the oldest posted message
after their own `displayBuffer`. The popup stays until the next render. The
queue holds two, so the highlights and chapter load notices raised by the same
book open show one after the other rather than one covering the other.

### Launcher

`LauncherActivity` said tiles carry no counts because a denormalised counter can
drift. This count is not denormalised: it is a popcount of the record, taken
each time the launcher opens. The Bible tile's subtitle becomes
"212 of 1189 chapters read" once any chapter is recorded, replacing the edition
title — static text the cover already shows. With nothing recorded, or no
readable record, the subtitle is unchanged. The resume strip is untouched.

### Not done

- **Per-book progress in the chapter-list footer.** That hint (Feature 2 of
  `2026-09-13-bible-chapter-status-and-hint-design.md`) does not exist:
  `BibleNavigationActivity` overrides `drawChrome` (`:435`) but has no footer
  override. Adding it is a separate UI change; `readCountInBook` is ready for it.
