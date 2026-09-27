Tiers: t1 heavy, t2 heavy, t3 standard

# Branch review 0: enhancements batch 2 (store writer, dead code, file page)

Scope: `e41880e3..4597f8a8` on `main`. That covers #145 (t1, issue #100), #146 (t2, issue #102) and #144 (t3, issue #105), with the release-please commits left out. Code was read on current `main` (`7161a0dd`).

## What I checked across the whole batch

- **Combined build.** All three PRs measured flash against their own base (t1 5,557,874 B; t2 and t3 5,557,906 B), so no PR built the merged tree. I built it: `pio run -e x4pro` on `7161a0dd` succeeds at Flash 5,460,778 B and RAM 65,092 B. The two warnings are the ones every PR already reported: `WebSocketsClient.cpp:573` and `CrossPointWebServerActivity.cpp:204`. The three PR deltas sum to roughly 5,460,878 B, which is within 100 B of the measured size.
- **Host tests on merged `main`.** 980 of 980 pass.
- **Deleted i18n keys (t2) against t1, t3 and the web pages.** I grepped the 30 keys removed from `english.yaml`, plus Hebrew's stray `STR_CALIBRE_WEB_URL`, across `src lib data scripts test` and the top-level docs. No code or page references any of them. The only hit is a documentation example (MINOR-4).
- **Theme list vs the web settings API (t2 × t3).** The API builds its option list from `SettingsList` (`src/network/CrossPointWebServer.cpp:1166-1179`). POST rejects a value outside `enumValues.size()` (`:1259-1263`), and `SettingsList.h:273-274` now offers two options. On load, `CrossPointSettings.cpp:166-169` clamps a stored 2 or 3 back to the struct default, which is `LYRA`. `UITheme::setTheme` maps `LYRA_3_COVERS`, `ROUNDEDRAFF` and `default:` to Lyra (`src/components/UITheme.cpp:34-43`). `SettingsPage.html` has no hardcoded theme names, so the web page and the device agree. This matches decision d1: Lyra stays and is the default.
- **Duplicated abstractions.** None were introduced. t1's writer reuses the existing `serialization::BufferedFileWriter` (`lib/Serialization/BufferedFile.h:26`) and does not add a second buffer. `HalFileReader` was moved from `PassageFile.cpp`, not copied, and `ensureParentDirectory` exists in one place (`lib/Serialization/PersistableStore.cpp:56`).
- **HighlightFile seam (t1 edited `HighlightFile.cpp`; t2 dropped its include from `EpubReaderActivity.cpp`).** They don't conflict. `EpubReaderActivity.cpp` has no remaining `HighlightFile::` use, and the merged build links.
- **jszip / optimiser (t3).** Nothing in `src lib scripts docs` references jszip, `handleJszip` or the optimiser, apart from the parked 2026-09-14 spec (MINOR-5). `docs/webserver-endpoints.md` has no stale row. A stale `src/network/html/js/jszip_minJs.generated.h` is still in this working tree, but it is gitignored, nothing includes it, and a clean clone does not have it.
- **Issue acceptance criteria.**
  - #100: the writer adaptor streams into the `.tmp` (`PersistableStore.cpp:64-88`). Both adaptors now live in `lib/Serialization`. The parent directory is derived from the path (`:56-62`, `:93`, `:104`). The round-trip host test exists (`test/storage_io/PassageFileTest.cpp`).
  - #102: every listed deletion landed. Issue #102 named Lyra for deletion; t2 kept it because Lyra is the default theme (orchestrator decision d1, which the brief records). The enum values are still readable, and the keys are gone from all 32 YAMLs.
  - #105: the optimiser and jszip are stripped and the flash delta is in the PR. The upload and side-load checks are on the device checklist, which is correct because only hardware can verify them.

No BLOCKER or MAJOR findings. Six MINORs, all fixable inline or as follow-ups.

## MINOR

**MINOR-1: the shared streaming reader is unbuffered, while its writer was buffered for the stated reason.**
`lib/Serialization/PersistableStore.cpp:35-37` justifies the 512 B write buffer: "every HalFile::write takes storageMutex, hence the buffer". Twenty lines above it, `HalFileReader::read()` (`:24`) does one `HalFile::read()` per byte, each under the mutex, and ArduinoJson pulls input one byte at a time (`.pio/libdeps/x4pro/ArduinoJson/src/ArduinoJson/Json/Latch.hpp:38`). A 200 KB passage file therefore costs about 200,000 mutex round-trips per load. The per-byte reader is older than this batch: its body moved verbatim from `PassageFile.cpp`. The new part is that t1 made it the recommended reader for "any store" (`PersistableStore.h:78-80`, `:94`). Wrapping the file in the existing `serialization::BufferedFileReader` would make the two directions consistent.

**MINOR-2: eight per-store `mkdir` calls are now redundant.**
`writeDocToFileAtomic` now creates the parent directory itself (`PersistableStore.cpp:104`, using SdFat's recursive `mkdir`). These callers still `Storage.mkdir` the same directory first:
- `src/study/PassageFile.cpp:36`
- `src/study/ChapterCompletionFile.cpp:36`
- `src/study/TagPaletteFile.cpp:36`
- `src/study/PubKeyRegistry.cpp:46`
- `src/study/MigrationRunner.cpp:118` and `:173`
- `src/network/MeetingWeekCache.cpp:57`
- `src/util/BookmarkFile.cpp:64`
- `src/util/HighlightFile.cpp:40`

Each one costs an extra `storageMutex` take plus an SD call on every save. Spec #100 kept them deliberately (`2026-09-27-issue-100-design.md:88-92`), and they are harmless. t1 deleted the comments in `BookmarkFile.cpp` and `HighlightFile.cpp` that explained these calls, so the calls now read as necessary when they are not.

**MINOR-3: t2 left `MappedInputManager` helpers with no callers.**
At `e41880e3`, the only callers of `rowTouch` and `colTouch` were `HomeActivity.cpp:234` and `:209`, and the only caller of `getPressedFrontButton` was `ButtonRemapActivity.cpp:87`. All three are still declared and defined (`src/MappedInputManager.h:75,78,104`; `.cpp:183,202,391`). `wasTapInRect` (`.h:66`) has no external callers either. t2 deliberately rewrote the `getPressedFrontButton` comment (`MappedInputManager.cpp:389-390`) to keep that method for "a future flow". Spec #102 does not mention `rowTouch` or `colTouch`. `docs/contributing/touch-and-ui.md:7` still says these helpers survive for "the theme-driven home screen", a screen that no longer exists.

**MINOR-4: user-facing docs describe the deleted Home screen and a deleted key.**
- `USER_GUIDE.md:88`, `:94-104` (§4, "Home screen", with **Browse files** and **Recent books**) and `:238` ("Home -> Browse files") describe `HomeActivity`, which t2 deleted.
- PR #146 lists `USER_GUIDE.md` §4 and `HomeMenuItem` as follow-ups, but no open issue tracks either one (`gh issue list --state open`).
- `docs/i18n.md:75` and `:191` still use `STR_BROWSE_FILES` as the worked example, and t2 deleted that key.

**MINOR-5: a parked spec and CLAUDE.md cite code that has moved or no longer exists.**
- `docs/superpowers/specs/2026-09-14-publication-download-design.md:140` points at the optimiser (`FilesPage.html:1570`, `:1750`), and `:158` says "The web-UI optimizer stays available". PR #144 says in its body that this clause no longer holds but did not amend the spec.
- `CLAUDE.md:279-280` cites `lib/Serialization/PersistableStore.cpp:11` for `writeDocToFile`, which is now at `:92`. The citation had already drifted before this batch; t1 moved the function further.

**MINOR-6: `RecentBook.coverBmpPath` is now written but never read for display.**
Its only display consumer was `UITheme::getCoverThumbPath`, which t2 deleted (it served `HomeActivity` and `RecentBooksActivity`). The field is still produced (`ReaderActivity.cpp:60`, `EpubReaderActivity.cpp:452`), persisted (`RecentBooksDoc.cpp:51,73`) and repointed on a move (`RecentBooksStore.cpp:94-95`), but nothing renders it. It is a harmless leftover that a later `recent.json` format revision could drop. It is out of scope for #102, which kept `RecentBooksStore` deliberately.

## Note for the orchestrator

During this review, `CHANGELOG.md` in the primary checkout carried an uncommitted edit adding a 1.16.11 "Bug Fixes" entry for #145. The orchestrator made that edit: #145 was missing from the 1.16.11 changelog. It moved the edit to PR #151 and reverted the checkout. It does not bear on this review.

VERDICT: CLEAR
