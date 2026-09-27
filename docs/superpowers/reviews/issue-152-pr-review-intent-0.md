Tier: heavy

# PR #159 intent review, pass 0: issue #152, batch 2 follow-ups

Reviewed: `gh pr diff 159` (commits `0f4c2956..12209e9b` over base `965f4265`), checked against
issue #152, `docs/superpowers/specs/2026-09-27-issue-152-design.md` and
`docs/superpowers/plans/2026-09-27-issue-152-plan.md`. I rebuilt and ran the host suite here with
`cmake --build build/test && ctest --test-dir build/test`: 998 of 998 pass. I did not run
`pio run` myself; the PR body reports it green.

## Acceptance criteria, item by item

| # | Issue asks | Where it lands | Met |
|---|---|---|---|
| 1 | Wrap the streamed reader in `BufferedFileReader` | `lib/Serialization/PersistableStore.cpp:16,22-34,154-155`. `JsonFileReader` sits over a 512-byte `serialization::BufferedFileReader`, which falls back to unbuffered passthrough on OOM (`BufferedFile.h:83-88`). `readBytes` is dropped as spec A3 says. | yes |
| 2 | Remove the redundant `mkdir` calls, or keep them with a comment | All nine are deleted: `PassageFile`, `ChapterCompletionFile`, `TagPaletteFile`, `PubKeyRegistry`, `MigrationRunner` (both), `MeetingWeekCache`, `BookmarkFile` and `HighlightFile`. Includes that only those calls used are removed exactly as plan 2.2 lists. `PubKeyRegistry.cpp:5` keeps `<HalStorage.h>` for `Storage.exists` at `:72`; `MigrationRunner.cpp` and `BookmarkFile.cpp` keep theirs. | yes |
| 3 | Delete `wasTapInRect`, `rowTouch`, `colTouch` and `getPressedFrontButton` | All four are gone from `src/MappedInputManager.{h,cpp}`, along with `RowTouch`. `grep -rn` over `src lib test` finds nothing left. | yes |
| 4 | Drop `RecentBook.coverBmpPath` and bump the version | The field is dropped along with its whole producer chain (spec A11): `updateBook`, `getDataFromBook`, `updatePath`'s cache parameters, `getBookThumbBmpPath` and `Epub::getThumbBmpPath()`. The budget constants are recomputed in `RecentBooksDoc.h:48,59-63`. **The version is deliberately not bumped.** See MINOR-1. | yes, with a documented deviation |
| 5 | Remove `HomeMenuItem` | The enum is deleted. `goHome(bool)` at `ActivityManager.h:90`, `onGoHome()` at `Activity.h:70`, `main.cpp:578`. The `goHome` comment now lists the real tiles (`ActivityManager.cpp:242-243`). | yes |
| 6 | Rewrite `USER_GUIDE.md` §4 for the launcher | §4 is rewritten to match `LauncherActivity.cpp:498-513,547-603`. §3, §10, §12, §13 and troubleshooting are reworded. `grep -n "Home screen\|Home ->\|Browse files\|Recent books" USER_GUIDE.md` finds nothing. | yes |
| 7 | `docs/i18n.md` example | `STR_BIBLE` replaces the old key at `:75` and `:191`. | yes |
| 8 | `touch-and-ui.md:7` | Now names the launcher. The table rows for the deleted helpers are gone, and the raw-accessor row is corrected (spec MAJOR-2). | yes |
| 9 | Stale citations | The publication-download spec carries a dated amendment and the strike. `AGENTS.md:280` cites `PersistableStore.cpp:93`, which I re-read: `writeDocToFile` is at `:93` and still uses `String` at `:95-96`, so keeping "still builds the whole document in one `String`" is correct, as spec 9b requires. `HighlightFile.h:39` is correct. | yes |
| Verify | Over-50 KB round trip through `readDocFromFileStreamed` | `AtomicWrite.ADocumentPastTheReadCapIsStreamedWholeAndReadBackWhole` still passes, and the new `TheStreamedReadPullsTheFileInChunksNotBytes` (`AtomicWriteTest.cpp:97-106`) proves the buffering. | yes |

## Spec and plan conformance

The diff follows the plan task by task. The commit sequence matches plan Tasks 1–7 one to one. The
code in Task 1.4, the tables in Tasks 2.1 and 2.2, and the replacement text in Tasks 7.1–7.5 are
reproduced verbatim. I found no divergence from the plan that the PR does not explain.

Scope holds in both directions:
- Every non-goal is respected. N1: `PassageFile` is still the only streamed-reader user. N2 and N3
  are untouched. N4, N5 and N7 appear as PR-body follow-ups rather than as edits.
  `test/CMakeLists.txt` is not in the diff.
- The only reach beyond the issue's literal list is spec A11 (removing the producer chain) and the
  `USER_GUIDE.md` §10, §13 and troubleshooting rewording. Both were decided in the spec with
  reasons: leaving them would create new zero-caller code, or leave dead "Home ->" routes. That is
  not unasked-for expansion.

## Tests exercise behaviour

- `HalStorageFake.ReadCallsAreCountedPerPathUntilReset` (`HalStorageFakeTest.cpp:157-169`) checks
  that the new fake control counts both `read` overloads, is keyed per path, and is cleared by
  `reset()` (`HalStorageFake.cpp:63` rebuilds `FakeCard`, which now holds `readCalls`).
- `TheStreamedReadPullsTheFileInChunksNotBytes` asserts an observable cost: fewer than 600
  `HalFile::read` calls for a 60 KB file. It does not restate the implementation. The PR reports it
  failing on the old reader at 60,008 calls. That matches the mechanism: one `read()` per character
  (`Json/Latch.hpp:38`) against a 512-byte refill at `BufferedFile.h:92`.
- `RecentBooksDoc.AFileStillCarryingACoverPathLoadsAndDropsItOnTheNextSave`
  (`RecentBooksDocTest.cpp:272-292`) covers the backward-compatibility guarantee. A legacy file
  loads, does not force a boot-time resave (spec A10, `EXPECT_FALSE(needsResave)`), and
  re-serialises without the key. `ToJsonWritesNoCoverPath` covers the forward side. The budget pins
  (367 and 9,967) are recomputed figures, not edits made to get a green run:
  18 + 9 + 10 × 34 = 367, and 18 + 9 + 10 × (34 + 2 × 224 + 512) = 9,967.

## Findings

### MINOR-1: the issue said to bump `recent.json`'s version; the PR does not, and the author should confirm that at merge

Issue #152 item 4 says "bump its version per `CLAUDE.md`", and the hpipe task notes repeat it. The
PR keeps `FORMAT_VERSION` at 1. That follows decision d1, which the orchestrator answered after the
worker escalated it (the task history shows `blocked-on-decision`). The decision was not made by
the issue author.

The deviation is not silent, and the reasoning holds up against the code:
- `fromJson` reads keys by name, so the old key is ignored (`RecentBooksDoc.cpp:69-71`).
- An older build reads the missing key as `""`, the value it already stores for a book with no
  thumbnail.
- `CLAUDE.md`'s actual rule is "refuses rather than reinterprets", and nothing here is
  reinterpreted.
- The precedent is real: `PassageDoc.h:20-23` writes linkless files as v1 for the same reason.
- A bump would make a rolled-back build refuse `recent.json` and then refuse every recents save.

The deviation is recorded in spec A9, in `docs/file-formats.md:476-482` (so nobody later "fixes"
the missing bump), and in the PR body's first-level bullets. It is flagged here only because it
departs from the human-written acceptance text. It needs no change unless the author disagrees.

No BLOCKER or MAJOR findings.

VERDICT: CLEAR
