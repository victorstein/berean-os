# PR #121 review, intent pass 0

Reviewed: `fix/99-halstorage-test-fake` at `9a69f055` against base `ad8e75f9`.

Checked against:

- issue #99
- the spec, `docs/superpowers/specs/2026-09-26-issue-99-design.md`
- the plan, `docs/superpowers/plans/2026-09-26-issue-99-plan.md`

## Evidence gathered

- **Build and run.** `cmake -S test -B $TMPDIR/b99 -DCMAKE_BUILD_TYPE=Release` was run with the uncommitted `add_subdirectory(storage_io)` line in the working tree.
  - `StorageIoTest`: `[  PASSED  ] 36 tests.`
  - `PaginationInvarianceTest`: `[  PASSED  ] 8 tests.`
  - full `ctest`: `100% tests passed out of 887`. This matches the PR body's claim of 851 + 36.
- **Host build warnings.** The only new ones are the two the spec predicts:
  - `TagPaletteFile.cpp:15` unused `MODULE`, called out in the PR body
  - `TextBlock.cpp:393` sign-compare, accepted in spec A-15
- **Plan fidelity.** I extracted the final "Write `<path>` with exactly this content" block for each of the 11 files the plan writes and diffed it against the committed file. All 11 are byte-identical:
  - `test/stubs/{Arduino.h,HalStorage.h,HalStorageFake.h,HalStorageFake.cpp,ObfuscationUtilsStub.cpp}`
  - `test/storage_io/*`
  - `test/pagination/CMakeLists.txt`

  The nine comment edits in Task 5 match the plan's replacement text exactly (`git diff origin/main...HEAD`). There is no unexplained divergence from the plan.
- **Fake fidelity to the real card.** Checked against the real source:
  - `SDCardManager::readFile` caps reads at `maxSize = 50000` (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202`). The fake has `READ_FILE_CAP` at `test/stubs/HalStorageFake.cpp:25,125`.
  - `SDCardManager::writeFile` removes an existing file before opening (`SDCardManager.cpp:282-284`). The fake does the same at `HalStorageFake.cpp:129`.
  - `rename` and `mkdir` pass straight through to `vol()` (`SDCardManager.h:57,61`), and `HalStorage` wraps them (`lib/hal/HalStorage.cpp:89,94-95`). This is consistent with the fake's `O_EXCL` modelling.

## Issue #99 acceptance

| Issue asks for | Where it is met |
|---|---|
| In-memory `HalStorage` fake, path→bytes map | `HalStorageFake.cpp:27-33` (`FakeCard::files`) |
| `exists`, `remove`, `rename`, `mkdir` | `HalStorageFake.cpp:92-119` |
| file read and write | `readFile`/`writeFile` at `:121-133`; streamed `HalFile` at `:135-245` |
| hook for a failed read | `storage_fake::failReadsOf` (`HalStorageFake.h:29`, `.cpp:122,150`) |
| hook for a failed rename | `storage_fake::failRenamesFrom` (`HalStorageFake.h:32`, `.cpp:97`) |
| tests for the atomic write | `test/storage_io/AtomicWriteTest.cpp:28-69`, five tests |
| tests for `.tmp` adoption | `test/storage_io/AdoptingReadTest.cpp:32-102`, ten tests through the shared `readDocFromFileAdopting` |
| one store's load/save round trip | `test/storage_io/TagPaletteFileTest.cpp:47-110`: round trip, Empty, RecoveredFromTemp, TooLarge, Failed, WriteFailed |
| *Also worth doing:* ASan/UBSan CI | Deferred on purpose. Spec non-goal and A-18 (`design.md:138,164`) cover it, and the PR's Follow-ups section names it. The suite was run once under ASan/UBSan by hand, as A-18 requires. |

## Spec requirements

- **Every test the spec lists exists and asserts the named outcome.** That covers the ten fake-pinning tests (`design.md:418-430`), the five atomic-write tests (`:434-442`), the nine adopting-read cases (`:446-460`) and the six `TagPaletteFile` cases (`:464-472`).
- **The chained test works as specified.** It covers a failed rename, then `clearFailures`, then an adopting read that promotes the `.tmp` (MAJOR 2 of spec review 0) at `AtomicWriteTest.cpp:56-69`.
- **The out-of-scope arm stays unasserted.** `DeleteTempReportEmpty` is not asserted for a per-store loader (A-17, `TagPaletteFileTest.cpp:1-3`). The shared helper's keep-the-`.tmp` policy is asserted at `AdoptingReadTest.cpp:61-67`, as the spec requires.
- **The write hook is a small expansion beyond the issue's two hooks.** It is justified in A-9 (`design.md:155`) as the only way to reach `PersistableStore.cpp:30-33`, and two tests use it. This is not scope creep.
- **Production code is untouched apart from comments.** The only `lib/` and `src/` changes are comment-only (`TempAdoption.h`, `HighlightFileAction.h`, `BookmarkSaveAction.h`), which A-20 permits.
- **The pagination suite moved onto the fake.** Its inert `HalFile` bodies are gone (`GfxRendererFake.cpp`, −8 lines), as A-15 requires.
- **The CMake registration line is correctly left out of the commit.** `add_subdirectory(storage_io)` is reported in the PR body, not committed, per `.claude/agents/data-dev.md:22-27` and A-14.

## Test quality

The tests drive unmodified production code:

- `PersistableStore.cpp`
- `TagPaletteFile.cpp`
- `TagPalette.cpp`

They assert observable card state (`fileBytes`, `exists`) and returned statuses, not internal calls. The fake is pinned separately by `HalStorageFakeTest.cpp`, so a test that passes only because the fake is lax would be caught there.

`AdoptingReadTest.cpp:93-102` checks both sides of the 50,000-byte truncation chain that `CLAUDE.md` warns about:

- past the cap, the read is a ParseError
- at `DEFAULT_SAVE_BUDGET` (45,000 bytes), the document reads back whole

## Findings

### MINOR 1: closing #99 leaves its ASan/UBSan suggestion untracked

The PR body ends with `Closes #99`. It lists the ASan/UBSan CI configuration only as prose under "Follow-ups", with no issue number. The deferral itself is legitimate: spec A-18 puts it out of scope and `ci.yml` is outside the data surface. But once #99 closes, the only record of the suggestion is a merged PR description.

**Fix inline:** open a follow-up issue for the sanitizer CI job and cite its number in the PR's Follow-ups section. This needs no code change and does not change scope.

No BLOCKER or MAJOR findings. Everything the issue asks for is built, every test the spec lists is present and passing, the committed code matches the plan byte for byte, and the one deferral is declared in both the spec and the PR.

VERDICT: CLEAR
