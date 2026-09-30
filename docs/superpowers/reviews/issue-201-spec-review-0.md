Tier: heavy

# Issue #201 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-30-issue-201-design.md` at `812ab4f9`, against issue #201
(`gh issue view 201 --repo victorstein/berean-os`), the research note, the mockup the issue cites
(artifact `4qvfHbNnJ77F2gM5DELQrQ`, read with the Artifact tool), and the code at the same commit.

## Findings

### 1. BLOCKER: the displayed reference and the Recent row contradict the owner-approved mockup, and the mismatch gets persisted

**Claim.** A7 makes `reference` chapter-level ("The chips show 'Genesis 1', not 'Genesis 1:5'"). A14
makes the chips equal-width like the tiles, with clipped labels. A11 says "No new translation keys".
The spec never cites the mockup (`grep -n -i "mockup\|artifact" …-design.md` finds nothing).

**Problem.** The issue says its design is the mockup ("Design and mockups:
https://claude.ai/artifact/4qvfHbNnJ77F2gM5DELQrQ", chosen by the owner on 2026-09-29). The mockup
contradicts the spec in three places:
- **Verse-level references.** The reader sheet's Recent chips are `'Rev 21:4', 'Gen 1', 'Ps 83:18'`.
  Home's Recent rows are `'Revelation 21:4', 'Genesis 1', 'Psalm 83:18'`. The "Taken out" table lists
  **"Continue · Isaiah 40:31"** as the replacement for the removed "chapters read" progress UI.
  Mixing "Gen 1" with "Rev 21:4" shows the issue's "record the chapter only" case as a separate kind
  of entry that the reader can see. A2 collapses that case into verse 1, and A7 then drops the verse
  everywhere.
- **Caption and chip shape.** The sheet has a `RECENT` caption (`<div … tb …>RECENT</div>` at
  `top+142`). The chips are pills sized to their content, and the labels are abbreviated. The spec has
  no caption, uses equal-width boxes and clips the labels. A caption needs a `tr()` key, which means a
  translation-YAML hand-off. That contradicts "No new translation keys".
- **The value is persisted.** `r` is written into every v1 `places.json`. #203 reads it for
  "Continue · <reference>" (#203: "the newest place from the places store"). Chapter-level
  references would ship in the one surface the owner named as the replacement for progress, and they
  would stay on cards until each place is recorded again.

**Evidence.** Mockup `menuProposed` and `homeProposed` script blocks, plus its "Taken out" table.
Spec lines 71-76 (A7), 101-105 (A14) and 91 (A11). `gh issue view 203` lines 25-26. No
`STR_RECENT*` key exists (`grep -n "STR_RECENT" lib/I18n/translations/english.yaml` returns nothing).

**Fix.** Either follow the mockup or get the owner to sign off on the deviation. Following the mockup
means:
- `r` = `bibleReference(book, major) + ":" + minor` when the page top resolved to a verse anchor.
- `r` = chapter-only ("Genesis 1") in the A2 case, where the page starts before the first anchor. The
  unit can still carry verse 1 at offset 0 for the jump.
- Add a `RECENT` caption key as a translation hand-off.
- Decide whether chip labels use the existing book abbreviations (`BibleBookNameTable`, and the
  project memory "Book abbreviation source decision") or are clipped.
- Make the budget table's `r` row cover the longer string. 48 B still holds, because
  "1 Corinthians 13:13" is 19 B.

### 2. MAJOR: `recordLeftPlace` reaches `UnitIndexCache` from the loop task without the lock the render task uses, and `placeAt` can build under a lock it would then retake

**Claim.** "`recordLeftPlace` must not take `RenderLock` itself" (spec :267). "Owning task: the
Arduino loop task" (:204-205). `placeAt` is "the one-entry cache hit … a build only happens for a
document never indexed, which cannot be a document just rendered" (:217-220).

**Problem.** There are two parts.
- **The race.** `placeAt` → `unitsFor` rewrites `cachedSpine_` and `cached_` on a miss
  (`UnitIndexCache.cpp:227-247`). The render task calls the same function under `RenderLock`
  (`EpubReaderActivity.cpp:1396-1401` → `StudyStore.cpp:215`). `pageTurn` and `skipPages` take the
  lock only around the spine mutation (`EpubReaderActivity.cpp:919, 937, 952, 962`), and the spec
  places the record before that. Here is one sequence that corrupts the cache:
  1. A forward turn crosses into N+1.
  2. The render task is inside `unitsFor(N+1)` loading anchors.
  3. A quick back-tap at page 0 runs `recordLeftPlace` → `unitsFor(N+1)` on the loop task.
  4. Both tasks move-assign `cached_`.

  Today no loop-task code touches the unit cache while the reader is the current activity: `locate()`
  runs under `HighlightsActivity`, and `repairTexts` runs in `loadBook` before the first render.
- **The build under the lock.** `indexFailed()` is true only while `cachedSpine_` still equals the
  spine (`UnitIndexCache.h:68`). A render-time build that failed leaves the entry unindexed
  (`UnitIndexCache.cpp:241-246`). If a sub-screen then moves the cache, `placeAt` rebuilds. That
  build goes through `SpineHtmlStream::stream` with the default `Inflate`, which takes `RenderLock`
  (`SpineHtmlStream.cpp:36-37`). On the `onExit` path the loop task already holds that lock
  (`ActivityManager.cpp:141-150, 176`), and the lock is a plain mutex (`ActivityManager.h:69`
  `xSemaphoreCreateMutex`, `ActivityManager.cpp:327-328`). The loop task deadlocks, and the 5 s task
  watchdog panics. `SpineHtmlStream.h:33-35` names this exact hazard. `bookFor` → `buildBookMap` can
  also stream (`UnitIndexCache.cpp:355-357`).

**Fix.**
1. Capture the place (the offset plus `STUDY.placeAt`) inside the `RenderLock` scope that `pageTurn`,
   `skipPages` and `navigateTo` already open, before the spine or section changes. In `navigateTo`,
   that is before `clearDeferredReposition()`, which is at `:1681`, not `:1679`.
2. Call `PLACES.record` after the lock is released, which keeps the SD write off the render path as
   the spec intends.
3. Make `placeAt` non-building: add a `UnitIndexCache` peek (a cache hit, or `readEntry` plus
   `loadAnchors` only, never `buildDocument` or `bookFor`) that returns empty otherwise. Then
   `onExit`, which already holds the lock, is safe.
4. Use the same peek for A14's "chapter on screen" test at menu open. The spec leaves it unspecified.
5. Make `pageShown` `std::atomic<bool>` or read it under the lock. It is written on the render task
   and read on the loop task.

### 3. MAJOR: a refused (`"v":2`) store still grows in RAM, so the spec's own acceptance check fails

**Claim.** The error table says: unknown `v` → "list stays empty in RAM; Recent row empty; file
untouched" (:314). The device check says: put `{"v":2,"places":[]}` on the card → "Recent empty"
after reading and leaving chapters (:369-370).

**Problem.** `record()` runs `PlacesDoc::record` on the in-memory vector, then `saveToFileAtomic()`
(:198-199). The base class refuses only the save (`PersistableStore.h:212-214`). The table's own "save
fails → in-memory list keeps the place" row (:317) confirms this. After the first chapter change the
Recent row shows places the card will never hold, and every later chapter change logs
`Refusing to save …` at `LOG_ERR`. The file is still protected, which is what the host test checks.
The claimed UI behaviour is wrong.

**Fix.** Have `PlacesStore::record` return early while `loadRefused` is set. The flag is protected
in `PersistableStoreBase` and readable from the derived class. Log it once. Add a host case: after a
refused load, `record()` leaves `places()` empty.

### 4. MINOR: the acceptance checks cannot be observed as written

- **The issue's "Recent shows both, newest first".** Under A14 the reader row never shows the
  chapter on screen, so this cannot be seen in #201. The spec quietly swaps in "Card holds both,
  Genesis newest" (:364). The owner cannot read that file over Wi-Fi either, because dot-paths are
  protected on every web route (`WebRouteUtils.cpp:9-10`, `CrossPointWebServer.cpp:588`).
- **Save counts.** "one per chapter change" (:362) is not visible in the log: `PersistableStore.cpp`
  logs only failures (`grep -n LOG_ lib/Serialization/PersistableStore.cpp`).
- **Intents.** The device checks for intents rest on an unspecified "temporary debug trigger or
  #203" (:365).
- **Fix.** On each successful save in `PlacesStore::record`, write one `LOG_DBG` line that dumps the
  list's references. That line becomes the check for both "both, newest first" and "one per chapter".
  Either name the debug trigger (for example a `platformio.local.ini` flag read at the Bible tile) or
  say plainly that intent device checks move to #203.

### 5. MINOR: adding `OPEN_RECENT_PLACE` touches more than the file list says

`labelFor` switches over `ReaderMenuAction` with no `default` (`EpubReaderMenuActivity.cpp:35-73`).
A new enumerator therefore raises `-Wswitch` under the per-source `-Wall`, and CLAUDE.md says not to
commit on warnings. `onReaderMenuConfirm(MenuAction)` (`EpubReaderActivity.cpp:678`,
`EpubReaderActivity.h:113`) has no parameter for `recentIndex`, but the spec's handler reads it
(:305). **Fix.** List the `labelFor` case. The chip is not a model item, so returning any `StrId` is
fine. Also say that the `openReaderMenu` handler passes `menu.recentIndex` through.

### 6. MINOR: the store's safety depends on a hand-off line that nothing enforces

If `PLACES.loadFromFile()` (the `main.cpp` hand-off, :131) is never applied, `loadRefused` stays
false and the first `record()` replaces whatever is on the card, a `"v":2` file included.
`main.cpp:420-422` documents this same hazard for `WIFI_STORE`. **Fix.** Add a `loaded` flag to
`PlacesStore`. `record()` should either no-op or load lazily until a load has run.

### 7. MINOR: the last chapter is lost at the end of the book

The call-site table records on a page turn only when it "crosses a spine" (:263). A forward turn
off the last page of Revelation 22 takes the end-of-book branch instead
(`EpubReaderActivity.cpp:925-929`, where `currentSpineIndex = getSpineItemsCount()`). After that,
`onExit`'s step 1 returns because the spine is out of range (:249-251). **Fix.** Record in that
branch too, before the assignment.

## Checked and holds

- The dedupe and eviction design, and the `v`-refusal path through `isKnownFormatVersion` and
  `loadRefusedAfter` (`FormatVersion.h:14-33`).
- The budget arithmetic: 19 + 11 + 12 × 164 = 1,998. `unitToCompact`'s format is confirmed at
  `Unit.cpp`.
- All eight `goToReader` call sites.
- `onEnter`'s lock release (`ActivityManager.cpp:159-160`) and the re-render after a pop (`:130-133`).
- The `navigateTo` capture-before-clear reasoning.
- That `STR_LINK_TARGET_NOT_FOUND` exists (`english.yaml:404`).
- The host-testability of a real store `.cpp`: `test/crosspoint_state` is the precedent.
- That the sheet still fits over the page with a 52 px chip band in the worst Bible case: 451 + 52
  = 503, and (800 − 503) × 3 ≥ 800.
- A3's departure from the issue's "chapter only". It is defensible, because `ready()` is false only
  when `begin()` failed (`StudyStore.cpp:66-68`).

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 2
