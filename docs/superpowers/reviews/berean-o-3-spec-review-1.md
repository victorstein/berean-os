Tier: heavy

# berean-o-3 spec review 1

Reviewed: `docs/superpowers/specs/2026-10-11-berean-o-3-design.md` at `6c20ac2c`, against the brief
(`hpipe brief --task t3`), the research note, the baseline log
(`/private/tmp/claude-501/.../ee8ac7a1-.../scratchpad/baseline.log`), the code on this branch, and SdFat in
the primary checkout's `.pio/libdeps/x4pro/SdFat`. No `berean-o-3-spec-review-0.md` exists on disk
(`hpipe show --task t3` lists the slot; `find` over every worktree and the primary checkout finds no
file), so I had no earlier pass to check fixes against. The spec has a single commit.

## Verified and sound (not findings)

I checked the claims the design rests on, and they hold:

- **The cold-load mechanism (A3, component 1).** The loop at `BookMetadataCache.cpp:492-494` makes four
  `HalFile` reads per entry (`Serialization.h:22-25,46-51`), and each read takes `storageMutex`
  (`HalStorage.cpp` `HAL_FILE_WRAPPED_CALL`). `BufferedFileReader` handles a window refill and a
  free in-window `seek` (`BufferedFile.h:81-124`). The SDMMC device bounces every transfer through its
  own 4 KiB DMA buffer (`freeink-sdk/.../SdmmcBlockDevice.cpp:66-71,130-142`), so where the 4,096-byte
  buffer lives does not affect DMA. Internal SRAM is still the right choice for it.
- **The version-bump cost that A3 relies on.** A rebuild with `!skipLoadingCss` runs `parseCssFiles()`
  and `Storage.removeDir(<cache>/sections)` (`Epub.cpp:498-503`). The spec cites `:497-501`.
- **"Home already holds the book".** `closePublication` is called only from `openPublication`
  (`grep -rn closePublication src lib`). `UnitIndexCache::epub_` is a `shared_ptr<Epub>`
  (`UnitIndexCache.h:87`).
- **A6.** On FAT and exFAT, closing a read-only handle writes no directory entry: `FatFile::close` →
  `sync()` writes only under `FILE_FLAG_DIR_DIRTY` (`FatFile.cpp:128-133,1231-1259`), and
  `ExFatFile::close` → `sync()` does the same (`ExFatFile.cpp:75-79`, `ExFatFileWrite.cpp:367-378`).
- **A13.** The boot `ResumeReader` route is the only place the crash-loop counter is incremented
  (`main.cpp:594-601`).
- **A17.** Every SD mutator runs on the loop task. The web server is the synchronous `handleClient`
  pumped from the activity loop (`CrossPointWebServer.cpp:234-254`), and the render task is the only
  task `src/` creates (`ActivityManager.cpp:32`). Park runs under `RenderLock` (`ActivityManager.cpp:141-145`)
  and take after `lock.unlock()` (`:159-160`). Every `goToReader` is a Replace, so readers never nest.
- **The R5 rule (A14).** Home reloads `recent.json` on every entry (`LauncherActivity.cpp:105`), and so
  does `PublicationsActivity.cpp:65`. If a save failed, the stale entry therefore shows up as "not first"
  and the rule fixes itself.
- **The host syntax check in §9.** I re-ran it verbatim, with a `WString.h` shim and a forward-declared
  `Print`, and it exits 0. `FsHelpers.cpp` also passes a host syntax check with the same shim, so it can
  be linked.

## Findings

### MAJOR 1: A9's condition for reusing the study state misses two inputs that change outside STUDY

**Claim (A9).** STUDY is reused when it is open, ready, and opened for this same `Epub`. The argument
for safety is that "STUDY is the only runtime writer of the study files".

**Problem.** `openPublication` also derives its state from inputs that are not study files. Two of them
can change between park and take, or can be a failure that a cold open would retry:

1. **The PubKeyRegistry decides `pubKey_`.** `resolvePubKey` returns the registered key whenever one
   exists, and `local-<path>` otherwise (`lib/StudyStore/StudyStore/PubKey.cpp:24-35`). The registry
   has runtime writers at Home. The clearest one is the downloader's "already on the card" path, which
   records an identity for an **unchanged file** and leaves its cache alone
   (`PublicationDownloader.cpp:195-200`: "The identity is recorded even on the skip path: the file may
   predate the registry, and an unregistered publication loses its tags when it moves").

   The failing sequence: a sideloaded workbook is opened (key `local-…`), the owner goes Home →
   Meetings or Buscar, the same issue is "already on the card" and gets registered, and the workbook is
   opened again. The take hits: same path, same size, same `book.bin`. `isOpenFor` is true, so
   `pubKey_` stays `local-…`. Every tag saved in that session goes to `local-….json` (`StudyStore.cpp:116`).
   The next cold open resolves the registered key and never reads that file. Nothing migrates
   `local-` keys: `pubKeyIsStable`/`LOCAL_PREFIX` appear nowhere outside `PubKey.cpp`, and
   `MigrationRunner.cpp:270-275` resolves keys only for legacy highlight files. Today the same session
   would have saved under the registered key. A2 parks any book, not only the Bible, so this path is
   live.
2. **Failed palette or passages loads.** `openPublication` latches `saveDisabled_` on a `Failed` palette
   or passages load (`StudyStore.cpp:54-64`), and `Failed` means "DATA MAY STILL EXIST"
   (`lib/Serialization/TempAdoption.h:73`). A cold open retries the read. A warm open would keep the
   empty or partial `passages_`/`palette_` for the rest of the session, so the owner's tags vanish from
   view after a transient read error that a cold open would have recovered from. A9 retries a failed
   *unit index* (`units_->ready()`) but not these two failures, which is inconsistent.

**Evidence.** Cited inline above. The Bible itself is unaffected by (1): with `isBible && canonVerified`
the key is always `BIBLE_PUB_KEY` (`PubKey.cpp:25`; `StudyStore.cpp:45-49`).

**Fix (inline).** Have `isOpenFor` also require two things:

- (a) The last `openPublication` loaded the palette and passages cleanly. Record it with a flag that
  `openPublication` sets and `closePublication` clears. Reusing `!saveDisabled_` is not enough,
  because that latch is session-wide by design.
- (b) Either `study::pubKeyIsStable(pubKey_)`, or `resolvePubKey` re-run on the current registry entry
  equals `pubKey_`. Do the re-resolution only when the key is `local-`, so the Bible's warm path pays
  nothing.

Then rewrite A9's safety argument to name the registry as an input, and list both cases under §6.4.
Putting the reuse rule in a pure helper would let it be host-tested alongside `ParkedBookOf`.

### MAJOR 2: §6.4 and §9.1 get Clear cache and Delete cache wrong for the Bible, because Home rebuilds `book.bin` before any take

**Claim.** §6.4 row 3 (Settings → Clear cache) says "`book.bin` missing → `reopen` false → cold
rebuild". Row 7 (Reader menu → Delete cache) says "`reopen` false → cold rebuild". A5 rests on that
table: "Coverage is checked event by event in §6.4". Its residual risk needs an identical-size
replacement "whose cache was then rebuilt with an identical header before the take", as if the rebuild
were a coincidence. §9.1 step 4 expects "a take-miss line naming the cache, then a full re-index; the
page opens at the right place". Step 5 expects "a take miss, then a rebuild".

**Problem.** For the Bible, the rebuild before the take always happens. `LauncherActivity::onEnter` →
`resolveTargets()` (`LauncherActivity.cpp:79`) → `CoverBand::thumbPathFor(biblePath, …)` (`:120`) →
`CoverThumb::pathFor`. Because the thumbnail lives inside the cache directory
(`Epub::getThumbBmpPath`, `Epub.cpp:668`), a cleared cache always misses it, and
`epub.load(true, true)` rebuilds `book.bin` (`CoverThumb.cpp:32-41`). All of this happens before Home
is even painted. By the time the owner taps the Bible:

- `reopen()` passes, because the same EPUB gives the same header and size.
- With embedded style **on** (the owner's setting), the take misses on the CSS cache, not on
  `book.bin`. The re-index is logged at Home entry, not after the take.
- With embedded style **off**, the take **hits**, including immediately after the owner explicitly chose
  reader menu → Delete cache (`EpubReaderActivity.cpp:890-905` clears the cache of the very `Epub` that
  is then parked). The cumulative sizes and metadata strings in memory then come from the file the owner
  asked to throw away. That is safe when the file was sound, but it defeats the action's purpose when the
  cache was wrong in a way that kept its header and size. The fix would not take effect until a reboot.
- Step 4's "opens at the right place" is false today and will stay false. Clear cache removes the whole
  `epub_*` directory (`ClearCacheActivity.cpp:115-131`), `progress.bin` included
  (`EpubReaderActivity.cpp:197`), so the book reopens at the text reference. The owner would report a
  regression that this change did not cause.

**Fix (inline).**

- (a) Rewrite rows 3 and 7 for the Bible. The thumbnail rebuilds `book.bin` at Home entry, the take then
  misses on CSS when embedded style is on and hits when it is off. Keep "reopen false" for books whose
  thumbnail Home does not regenerate.
- (b) Do not park after the reader's own Delete cache. Set a flag in the `DELETE_CACHE` case, mirroring
  the `pendingReadFolderMove` exclusion in A8. The owner asked for a rebuild, so the warm path should not
  hand back state read from the deleted file. If the spec keeps the warm hit instead, it must say so as a
  labelled assumption.
- (c) Reword A5's residual risk. For the Bible, the rebuild before the take is guaranteed, so the residual
  risk is just "same byte size and same header".
- (d) Fix §9.1 steps 4–5 to the real log sequence. Step 4: `Cache not found, building spine/TOC cache`
  under the Launcher, a take miss naming the CSS cache, the CSS rebuild, then the page at the text
  reference. Step 5: no park, a cold load, and the saved page.

### MINOR 3: the 64-byte href scratch has no rule or test for hrefs longer than 64 bytes

**Claim.** Component 1 skips the href "by reading into a fixed 64-byte stack scratch". Test (1) covers
"hrefs over 15 bytes and an empty href".

**Problem.** EPUB hrefs longer than 64 bytes are ordinary (nested `OEBPS/Text/…` paths). Under A4, any
short read fails the load, so an implementation that refuses or mis-skips a long href makes the book
impossible to open. The load fails, `Epub::load` rebuilds, and the reload fails again with
`Failed to reload cache after writing` (`Epub.cpp:511-513`).

**Fix.** Specify the skip as a loop over the scratch, or as `BufferedFileReader::seek(position() + len)`,
which is free inside the window (`BufferedFile.h:113-118`). Add test cases for an href of about 200
bytes and for an entry that straddles a 4,096-byte refill.

### MINOR 4: the reuse branch does not say that `highlightsLoaded = true`

**Claim.** §6.3 step 3 says "`openPublication` skipped".

**Problem.** `highlightsLoaded` is assigned only from `openPublication`'s return value
(`EpubReaderActivity.cpp:235`), and it defaults to `false` (`EpubReaderActivity.h:71`). It gates two
things: highlight painting (`:1674`) and the place recorded on exit (`captureLeftPlace`, `:1573`), which
feeds Home's "Continue at". A literal "skip the call" would silently disable both on exactly the warm
path, and no step in §9.1 would notice.

**Fix.** State `highlightsLoaded = true` on the reuse branch. Add §9.1 checks that, on a warm open, a
tagged passage still paints and Home's "Continue at" still updates after leaving the book.

### MINOR 5: a marked link source now survives a warm open, contrary to A10's "exactly two places"

**Problem.** `linkSource_` is reset only in `closePublication` (`StudyStore.cpp:78`). Its contract says it
is "cleared when the publication closes" (`StudyStore.h:142-146`). A warm open skips that reset, so after
Home → the same book, a passage marked as link source is still marked (`HighlightsActivity.cpp:351-354`).
After a sleep it is not. The index is still valid, since the passages document is unchanged, but this is
a third difference between warm and cold opens that the spec does not record.

**Fix.** Either clear `linkSource_` on the reuse branch, which keeps today's behaviour, or record the new
behaviour as intended and update the `StudyStore.h` comment.

### MINOR 6: lock widening and accuracy nits

- **§10 says "hpipe's file lock enforces" serialisation with t4.** t3 is registered for the prefix
  `src/activities/reader/ReaderActivity`, and that prefix does not cover `EpubReaderActivity`
  (`hpipe show --task t3`). The lock only widens from the plan's `Files:` lines
  (`herdr-plugin-pipeline/src/lib/gating.ts:42-60`, `supervisor/tasks.ts:268-279`). Say that the plan
  must declare `src/activities/reader/EpubReaderActivity`, `lib/Epub/Epub.`, `ParkedBook`,
  `src/util/RecentBooksDoc` and the two new test directories.
- **A8/§6.2 say "the slot is empty while a reader lives".** That is not literally true:
  `ReaderActivity::onEnter` can `finish()` before `loadBook` runs (`ReaderActivity.cpp:46-50`), so no take
  happens. It is harmless, because that reader reads nothing. But §8 should say whether `park(null)` keeps
  or clears the previous occupant.
- **§9.1 step 2 says "second and later opens log the recents skip".** The first cold open after boot
  also skips whenever the Bible was already first in `recent.json`.
- **Citations.** `src/RecentBook.h:16` should be `:14`. `Epub.cpp:497-501` should be `:498-503`.

VERDICT: CLEAR
