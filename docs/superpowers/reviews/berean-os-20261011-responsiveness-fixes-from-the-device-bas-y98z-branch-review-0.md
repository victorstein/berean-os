Tiers: t1 standard, t2 standard, t3 heavy, t4 standard, t5 standard, t6 standard, t7 heavy

# Whole-branch review 0: responsiveness fixes from the device baseline (berean-o-1 to berean-o-7)

- **Range:** `1ba1cd34..36920945` on `main`. The batch is the seven squash merges #255, #257, #258, #259,
  #263, #262 and #266, plus the docs PR #253 and the release commits.
- **SDK:** the `freeink-sdk` gitlink moved `67f7e012 → 7dde1349`.
- **Released:** v1.29.10 to v1.29.15 are published, and "Publish Release Firmware" passed for each.
- **Host suite on `main` at 36920945:** built into a scratch directory, `100% tests passed out of 1678`.
- **CI:** `CI (build)` passed on 36920945.
- **Device:** no device run has been made for any of the seven changes. Every PR says so.

**Method.** I read:
- every new source file: `ParkedBook*`, `PageFrameCache`, `MenuCloseRestore`, `CoverBandCache`, `CoverBand`,
  `BandInk`, `BibleSearchStore`, `BibleSearchResidency`, `BibleSearchFingerprintFile`, `HomeVerse*`,
  `StudyReuse`;
- the batch diff of every touched firmware file, and the SDK diff;
- the seven PR bodies, the roadmap's re-rank, Phase 6 and handoff, and the baseline note.

Then I walked each event that could invalidate a holder across all five new long-lived holders. Every claim
below cites a line I read on `main`.

**Overall.** The code is sound at the seams. I found no use-after-free across activity teardown, no store that
breaks the storage rules, and no unchecked allocation. Each holder is either released or re-validated on
every event that matters (matrix at the end).

What the per-task reviews could not see is on the documentation and verification side:
- the batch has no single device run sheet, and the per-PR sheets now contradict each other;
- the roadmap records nothing as landed;
- one release note entry was dropped;
- two near-identical idioms were each added more than once;
- one key decision was forced by a file lock that no longer applies.

---

## MAJOR

### 1. Nothing records what landed, and the per-PR device checks now contradict each other

**Evidence.**
- **The roadmap is not marked.** The handoff checklist asks for `Done: #PR, measured X → Y` when an item
  lands (`docs/superpowers/plans/2026-10-11-responsiveness-roadmap.md:561`).
  - "Done" appears only at `:160` and `:561`.
  - All seven items in the re-ranked table landed (`:32-54`): R20, R9, R12 with R5, R21, R2, R11 and R22.
  - None is marked, and the batch never edited the roadmap. `git log 1ba1cd34..main` on that file shows
    only #253.
- **No PR has an after figure.** Every PR body leaves its "after" column as "to measure", "not measured" or
  "an estimate": #255, #257, #258, #259, #262, #263 and #266. The roadmap's protocol asks for ≥ 10 samples
  per scenario with raw log lines in the PR (`roadmap.md:104-107`), and none of them has any.
- **t2's wake check now expects a paint that t7 removed.**
  - #262's device step 1, and its spec at `2026-10-11-berean-o-2-design.md:488-489`, expect
    `[LAUNCH] Band drawn` on the wake's first frame, "then `[LAUNCH] Band replayed in N ms` on the
    post-verse paint".
  - Since #263, a same-day wake restores the verse: `HomeVerse::isCurrent` → `restoreOnce`
    (`src/activities/launcher/HomeVerse.cpp:72-76,87-101`).
  - So `versePending` is false (`LauncherActivity.cpp:149`). `loop()` never takes the post-verse branch
    (`:446-452`), and `onEnter` makes the only paint (`:85`).
  - #263's "For the orchestrator" section flagged exactly this. #262 merged afterwards with its step 1
    unchanged.
- **t1's ghost check is now a post-release check.**
  - t1's own gate says: "If any ghost shows, the PR is not merged as is"
    (`2026-10-11-berean-o-1-design.md:267`).
  - #255 merged as 106dfc4b and shipped as v1.29.10 before any device check.

**Failure scenario.**
- The owner flashes v1.29.15 and follows #262 step 1 on a same-day wake. No `Band replayed` line appears,
  and the band cache looks broken when it is fine.
- Separately, the next agent reads the roadmap and sees R9, R11 and R12 still open. It either re-opens them,
  or re-ranks the next round with no after figures, against the roadmap's "Measure first" rule
  (`roadmap.md:25-30`).

**Fix (doc only; no user decision needed).**
1. Mark each roadmap item, e.g.:
   - R20: `Done: #255 (v1.29.10), measured: pending device run`;
   - R2 #257, R21 #258, R11 #259, R22 #263, R9 #262, R12 and R5 #266, likewise.
2. Add one run sheet for the batch on the v1.29.15 `x4pro` dev build, as an "After the batch" section in
   `docs/superpowers/notes/responsiveness-baseline.md`. Draw its scenarios and lines from the seven PR bodies,
   with these corrections:
   - **Wake:** a same-day wake logs `Verse restore: restored, current 1` and one `[LAUNCH] Band drawn`.
     `Band replayed` appears on Home selection moves, and on a new-day wake's post-verse paint.
   - **Ghosting (t1):** a ghost now calls for the decided fallback (b), a HALF for Home-origin entries, as a
     new PR. "Do not merge" no longer applies.
   - **SD clock:** record the `[SD] SDMMC clock` line with every run. Every SD-bound figure depends on it,
     and #259 asks for it.
3. Once the run exists, fill in `measured X → Y` on each roadmap item.

---

## MINOR

### 2. The CHANGELOG and the v1.29.11 notes omit #258, and the batch shipped as six releases

**Evidence.**
- The two merges and the release:
  - #257 merged at 10:35:44 (b1a9f1af);
  - #258 merged at 10:36:21 (9d6f0c0d);
  - the release `33cde5ba chore(main): release 1.29.11` was cut ten seconds later, on top of both.
- `CHANGELOG.md:24-29` lists only #257 under 1.29.11, and `gh release view v1.29.11` agrees.
- #258 ("close the reader menu without re-rendering the page") appears in no release notes, although v1.29.11
  ships it.
- This is the back-to-back-merge loss the project's release notes warn about. The batch also cut six
  releases (v1.29.10 to v1.29.15) where the recorded practice for a batch is one stacked release with a
  `BEGIN_COMMIT_OVERRIDE` block.

**Failure scenario.** An OTA user, or the owner bisecting a menu-close regression from release notes, sees
no change to the reader menu in any 1.29.x release.

**Fix.**
- Add the missing line under 1.29.11 in `CHANGELOG.md` in the follow-up PR. release-please keeps manual
  edits below its new entries.
- Editing the published v1.29.11 GitHub release body is a remote change, so it is the user's call.

### 3. The batch's PSRAM residents were never budgeted together, and CLAUDE.md's PSRAM table is now wrong

**Evidence.**
- **What is now held across Home:**
  - the search term index (~440–444 KB), its posting page (4,096 B) and book names (4,228 B). They are kept
    across visits by `BibleSearchStore::park` (`src/study/BibleSearchStore.cpp:376-391`).
  - up to 98,304 B of band ink (`src/components/CoverBandCache.h:17-20`; 38,512 B expected, per #262).
  - the parked `BibleNavCache`, ~6 KB (`src/activities/reader/EpubReaderActivity.cpp:157-161`).
- **While reading,** `PageFrameCache` adds 48,000 or 144,000 B (`src/activities/reader/PageFrameCache.h:9-11`).
- **Totals:** about 0.5 MB at Home and up to ~0.7 MB while reading, against ~8.2 MB free. Nothing fails
  today.
- **CLAUDE.md is out of date.**
  - It still says "Search term index (~444 KB, while search is open)" (`CLAUDE.md:42`).
  - It lists none of the other three residents.
  - It puts the 48 KB framebuffer in internal SRAM (`CLAUDE.md:38`). But `allocFrameBufferStorage` is a
    plain `malloc(bufferSize)` (`freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:368-380`),
    and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (`sdkconfig.x4pro:2147`) routes a 48,000 B block to
    PSRAM. That is berean-o-11's point.
  - Its cache layout (`CLAUDE.md:641`) lacks `search-fingerprint.bin`.
- **t3's no-eviction argument rests on that stale row.** t3 dropped R12's "PSRAM pressure" drop
  (`roadmap.md:361-362`). Its reason was that the term index "therefore never compete[s] with it", citing
  the CLAUDE.md row (`2026-10-11-berean-o-3-design.md:184-186`). t6 then made the index resident across
  Home.
- **Only one holder has a floor.** t6's floor is checked only at the search screen's exit
  (`src/study/BibleSearchResidency.h:13-19`). The bands, the parked book and the page frames have none.

**Failure scenario.** The next PSRAM item sizes its budget from CLAUDE.md and leaves out ~0.5–0.7 MB that
is now always held. That item could be R10 (catalog index resident), R14 (144–288 KB of pre-rendered
frames) or R8 (a ~1 MB glyph LRU). This is the "two floors do not see each other" gap t6 documented, now
across four holders.

**Fix.**
- Replace the CLAUDE.md row with one row per resident: size, owner file, and what releases it (sleep reset,
  `onExit`, park floor, take-time validation).
- Correct the framebuffer row, which closes or answers berean-o-11.
- Add `search-fingerprint.bin` to `:641`.

### 4. One PSRAM buffer idiom, written five times

**Evidence.** These are byte-identical or near-identical allocate/free pairs for explicit PSRAM:

| Where | Added by | What |
|---|---|---|
| `src/network/CatalogIndexStore.h:86-89` and `.cpp:40,46` | before the batch | `PsramFree` / `PsramBuffer` |
| `src/components/CoverBandCache.h:50-55` and `.cpp:35,76` | t2 | the same |
| `src/activities/reader/PageFrameCache.h:31-37` and `.cpp:9-13` | t4 | the same |
| `src/activities/reader/EpubReaderActivity.cpp:150-161` | t3 | a destroying variant for an object |
| `src/study/BibleSearchStore.cpp:31-33` | t6 | `psramAllocate` / `psramRelease` |

- The host stub `test/cover_band/esp_heap_caps.h` exists only in t2's suite.
- The roadmap told every worker to copy the idiom (`roadmap.md:96-98`), and three did.

**Failure scenario.** A fix to the idiom, for example adding `MALLOC_CAP_8BIT` or logging the largest free
block on OOM, has to be found and repeated in five places. The next PSRAM item adds a sixth.

**Fix.**
- Add one `PsramBuffer` / `makePsramBuffer(bytes)` (and a `makePsramObject<T>()` for the nav cache) to
  `lib/Memory/`.
- Move the stub to `test/stubs/`.
- Convert the four in-tree copies in a `refactor:` PR.

### 5. Two resident copies of the Bible's book-name table, each loaded by its own SD scan, each with its own key

**Evidence.**
- **Go to's copy (t3).** `BibleNavCache` holds a `BibleBookNameTable` (`src/activities/reader/BibleBookIndex.h:10-11`).
  t3 now parks it with the `Epub`, and it is dropped whenever another book is opened.
- **Search's copy (t6).** `BibleSearchStore::names_` (`BibleSearchStore.h:166`, `.cpp:403-416`) is keyed on
  path and spine count, and dropped only for another Bible.
- **Both scan the same document.** Each fills its copy by streaming `biblebooknav.xhtml` and joining the TOC:
  - `BibleNavigationActivity::loadBooks` (`BibleNavigationActivity.cpp:123-146`);
  - `BibleBookNameTable::load` (`BibleBookNameTable.cpp:22-43`), called from
    `BibleSearchActivity::openIndexAndNames` (`BibleSearchActivity.cpp:245-247`).
- After a boot, the first Go to and the first search each pay that scan, even though either could seed the
  other.

**Failure scenario.** No wrong output. Each first use per wake costs the cost of the scan twice, and two
invalidation rules (Epub lifetime and Bible identity) cover one fact about the Bible.

**Fix.** berean-o-12, which persists the table beside `book.bin`, is the natural single owner. Until then,
seeding one from the other is a small follow-up. Low priority.

### 6. `search-fingerprint.bin`'s key omits `BOOK_CACHE_VERSION` because of a file lock that no longer applies

**Evidence.**
- **Why the version is missing.** t6's spec left `BOOK_CACHE_VERSION` out of the key because "The constant
  is file-local to `BookMetadataCache.cpp:13-14`, a file t3 holds"
  (`2026-10-11-berean-o-6-design.md:201-202`).
- **The key and the record survive an in-place rebuild.**
  - The key is EPUB size, spine count and `UNIT_INDEX_VERSION`
    (`src/study/BibleSearchFingerprintFile.h:36-40`).
  - A version mismatch rebuilds `book.bin` in the same directory (`lib/Epub/Epub.cpp:417-418` onward), so
    the record survives it.
- **The coupling lives only at the record.** It is written at `BibleSearchFingerprintFile.h:18-21` and
  `docs/file-formats.md:647-649`. The place someone bumping the version would look, `BookMetadataCache.cpp:14`
  and CLAUDE.md's "Format versioning" (`CLAUDE.md:671-674`), does not mention it.
- **t3 then edited that file without adding a pointer.** It added a second by-hand decoder of the spine
  layout (`BookMetadataCache.cpp:71-91`).

**Failure scenario.** A future `BOOK_CACHE_VERSION` bump that changes spine hrefs leaves the record in place.
Searches keep using the pre-bump fingerprint "from record" until a build's `documents()` rewrites it. t6
judged this benign, since it is the same EPUB in the same spine order, but nothing enforces it.

**Fix.**
- Expose the constant, e.g. `BookMetadataCache::FORMAT_VERSION`, and add it to `Key`.
- Bump the record `VERSION` to 2. A v1 record then decodes as Corrupt and is recomputed once.
- The minimum alternative is a pointer comment at `BookMetadataCache.cpp:14` and in CLAUDE.md's
  format-versioning list.

### 7. `docs/file-formats.md` overstates WebDAV coverage, and the berean-o-10 gap is wider than COPY

**Evidence.**
- `docs/file-formats.md:644-646` says every on-device replace of the Bible removes its cache directory,
  "That covers web upload, delete, move and rename, WebDAV…".
- WebDAV MOVE with `Overwrite: T` removes the destination file without `clearBookCache(dstPath)`
  (`src/network/WebDAVHandler.cpp:524-526`). It clears only the source's cache before the rename.
- COPY does the same at `:604-606`; that case is berean-o-10.
- **Effect on each holder:**
  - The destination keeps its old `book.bin`, sections, thumbnails and `search-fingerprint.bin`.
  - t2's epoch is not bumped on either path. A replay shows the same stale thumbnail a fresh draw would,
    so t2 adds no new staleness here.
  - t3's take is protected only by the EPUB size check (`ParkedBookOf.h:55-58`).
  - The fresh load that follows reads the stale `book.bin`.

**Failure scenario.** The user WebDAV-moves a newer Bible EPUB onto the existing Bible's path. The reader
opens it against the old spine table. This predates the batch, but the batch documents it as covered.

**Fix.**
- Widen berean-o-10 to MOVE, and call `clearBookCache(dstPath)` before both removals.
- Correct `file-formats.md:644-646` until that lands.
- t3's spec rejected per-site hooks for exactly this reason: they have to be remembered at every mutation
  site (`2026-10-11-berean-o-3-design.md`, A5). Making `clearBookCache` the one choke point for the
  destination would cover t2's epoch too.

---

## Seams checked and found sound

| Event | Band cache (t2) | Parked book + Go-to cache (t3) | Page frames (t4) | Search reader + names (t6) | Home verse (t7) |
|---|---|---|---|---|---|
| Sleep | chip reset | chip reset | freed in `onExit` (`EpubReaderActivity.cpp:1735`) before the destructor parks (`:174-186`) | chip reset | persisted, keyed by date, `bible.json` size and build |
| Settings → Clear Cache | epoch (`ClearCacheActivity.cpp:138`) | validated at take; `book.bin` gone or rebuilt is caught by `reopen` (`BookMetadataCache.cpp:538-566`) | no reader alive | fingerprint kept in RAM; record recomputed next boot; index is in `/.berean`, untouched | not under `/.crosspoint` |
| Book deleted or replaced (web, file browser, downloader, Publications) | `clearBookCache` (`BookCacheUtils.cpp:30`), `CardBooks.cpp:59`, thumbnail generation (`CoverThumb.cpp:48`) | path, size and `reopenCache` at take (`ParkedBookOf.h:41-65`) | n/a | `identify` (`BibleSearchStore.cpp:92-110`); header and size re-check (`:358-374`) | key on `bible.json` |
| Reader menu → Delete cache | epoch (`EpubReaderActivity.cpp:961`) | not parked (`:180-182`) | n/a | record removed with the directory | n/a |
| Web server start | PSRAM only | the `Epub` was already held by `StudyStore`'s `UnitIndexCache` (`UnitIndexCache.h:87`), so no new internal SRAM | n/a | PSRAM; `IndexReader` ~184 B internal | n/a |
| Font, theme or layout change | key includes band rect and style | `Epub` holds no layout state; CSS re-checked under current `embeddedStyle` (`Epub.cpp:520-531`) | any menu action re-renders (`MenuCloseRestore.h:23-32`) | names do not depend on the font | build id covers fonts |
| Index rebuild | n/a | n/a | n/a | indexer closes the store first (`BibleSearchIndexer.cpp:57-58`) | n/a |
| PSRAM OOM | draws uncached | `OOM: Go to cache` | fails once, not retried | exit floor | PSRAM JSON, retried next entry |

**Other contradictions checked.**
- **t3 and t6 on `book.bin`.** t3 bumped no `BOOK_CACHE_VERSION`: it is still 10 (`BookMetadataCache.cpp:14`;
  `docs/file-formats.md:9`). t3 changed how `book.bin` is read, not its layout, so nothing t6 keys on changed
  meaning.
- **t3's study-state reuse.** It is safe against Home-side writers:
  - every `STUDY` caller is under `src/activities/reader/`;
  - Home's Tags tile opens the reader (`LauncherActivity.cpp:380-421`);
  - migration runs only at boot (`main.cpp:483-513`).
- **t1 and t2.** No interaction: the replay writes exactly the pixels a fresh draw would, whatever the
  refresh mode.
- **`docs/file-formats.md`.** The two new sections (t6, t7) agree with their constants:
  - `RECORD_BYTES = 40`, `VERSION = 1`;
  - `HomeVerseDoc::FORMAT_VERSION = 1`, with the budget derived.
  - t3 rightly made no change.
- **Stores.**
  - `home-verse.json` uses the atomic write, a derived budget, a refuse-newer version and the loop task
    (`HomeVerseFile.cpp:51-67`).
  - `search-fingerprint.bin` is a fixed 40-byte binary record. It is written temp-then-rename with
    refuse-newer (`BibleSearchFingerprintFile.cpp:78-97,120-154`). That is not `writeDocToFileAtomic`, but
    that function is the JSON writer and this file is not JSON.

## Filed discovery beads

- **berean-o-9: raise its priority.** `blitCropped` reads every row of the cover (`CoverBand.cpp:41-61`).
  After t2 and t7, that first-paint decode is the only band cost left on every wake (the cache does not
  survive the reset). It is also the cost of the first Go to, Publications and Meetings entry per wake, and
  t2 adds recording to it.
- **berean-o-10: widen it** to WebDAV MOVE with `Overwrite: T` (finding 7).
- **berean-o-11: the code supports it** (finding 3). It can close with the CLAUDE.md correction, after one
  device log of where the framebuffer actually is. t4's per-turn capture estimate (≤ 2 ms per plane)
  assumed an SRAM-to-PSRAM copy.
- **berean-o-12: the natural fix for finding 5.** Keep it.
- **berean-o-8 and berean-o-13:** keep them as filed.

VERDICT: CLEAR
