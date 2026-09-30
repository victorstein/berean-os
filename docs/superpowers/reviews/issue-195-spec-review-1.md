Tier: standard

# Review 1 of `docs/superpowers/specs/2026-09-29-issue-195-design.md` (v2)

Reviewed against issue #195 (`gh issue view 195 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-29-issue-195-research.md` (R), review 0, and the tree at
`3d0aa10e`. No `src`, `lib` or `test` file has changed since `3591e3fe`
(`git log --oneline -3 -- src lib test` → `3591e3fe` first), so review 0's line checks still apply.
Orchestrator decision t5-d1 (A-9, A-10) is settled and is not re-examined here.

## Review 0's fixes, checked

- **MAJOR 1 (test count): correctly applied.** `ctest --test-dir build/test -N | tail -1` →
  `Total Tests: 1302`. `ctest -N | grep -c -E "ChapterCompletion|CanonicalChapters"` → `32`.
  1302 − 32 + 2 = 1272, which matches Testing §3 and the Problem section.
- **MAJOR 2 (shared files): correctly applied.** A-9 and A-10 now rest on t5-d1. The #193
  precedent cited in A-9 is real: `git show fc0796fb -- test/CMakeLists.txt` shows
  `-add_subdirectory(passage_label)`.
- **MINOR 1: applied.** `lib/Serialization/PersistableStore.h:111` (`// ChapterCompletionFile,
  PassageFile, TagPaletteFile, BookmarkFile and`) is in the Architecture list.
- **MINOR 2: applied.** Every added symbol is in the grep. Today's word-anchored hits for those
  symbols are all in files that are being deleted, or at lines A-5 and A-6 remove.

## Re-verified independently

- **The whole write path is listed.** `grep -rn` over `src lib test` for every completion symbol
  hits only the files in A-5 to A-8, plus `PersistableStore.h:111`, `SdPaths.h:32, 56`,
  `SdPathsTest.cpp:35` and `english.yaml:373-374`. Every one of those is handled.
- **Removing `StudyStore.h:10` breaks no include.** The eight files that include `StudyStore.h`
  (`BibleSearchStore.cpp`, `HighlightsActivity.cpp`, `TagPickerActivity.{h,cpp}`,
  `PassageSelectActivity.cpp`, `EpubReaderActivity.cpp`, `TagFilterActivity.h`,
  `PassageLinksActivity.cpp`) use no `std::array`. They still get `<ArduinoJson.h>` through
  `PassageDoc.h:3` and `TagPalette.h:3`. `StudyStore.cpp:12` includes `UnitAnchors.h` directly.
- **`highlightsLoaded` keeps a reader** once `recordDocumentRead` is gone
  (`EpubReaderActivity.cpp:1393`), so the removal leaves no dead member behind.
- **The A-10 comment rule is scoped correctly.** `scripts/gen_i18n.py:831` defaults to scanning
  `src` and `lib`, and `:275` scans only `.cpp`, `.h` and `.c`. So the spec, the reviews and
  `test/` may name the two keys without breaking the build.
- **D1's test home works.** `test/auto_page_turn/CMakeLists.txt` already has `${REPO_ROOT}/src` on
  the include path and uses `gtest_discover_tests`. `PageTurn.h` does not collide with anything
  (`ls src/activities/reader | grep -i page` → `AutoPageTurn.h` only).
- **The documentation targets are complete.** Outside `src`, `lib`, `test` and
  `docs/superpowers`, `git grep -i` finds only `USER_GUIDE.md:431`, `docs/file-formats.md:350-356`
  and `CHANGELOG.md:262`. The changelog is history.

## Findings

### MINOR 1: the moved tests keep the retired suite name, which slips past the removal gate

**Claim.** A-3 says the two tests "move unchanged apart from dropping `study::`". Testing §1.1
repeats this. Testing §2's grep is meant to prove that nothing named after completion is left.

**Problem.** The tests are `TEST(ChapterCompletionTrigger, …)`
(`test/chapter_completion/ChapterCompletionTest.cpp:102, 111`). Moving them unchanged puts a
`ChapterCompletionTrigger` suite into `AutoPageTurnTest`, whose own suite is `AutoPageTurn`
(`ctest -N` → `Test #308: AutoPageTurn.StartsInactive`). `grep -w ChapterCompletion` does not
match `ChapterCompletionTrigger`, because `T` is a word character, so the gate passes anyway. The
spec's own check, `ctest -N | grep -c -E "ChapterCompletion|CanonicalChapters"`, then returns 2,
not 0. The spec never says what the check should return after the change.

**Evidence.** `sed -n 102,113p test/chapter_completion/ChapterCompletionTest.cpp`. Also
`ctest --test-dir build/test -N | grep -E "AutoPageTurn"`.

**Fix.**
- In A-3 and Testing §1.1, rename the suite to `ForwardTurn`. The test bodies and expectations stay
  as they are.
- In Testing §3, state that the same `ctest -N | grep -c -E "ChapterCompletion|CanonicalChapters"`
  check returns **0** after the change.

### MINOR 2: A-12 keeps present-tense behaviour claims in the retired section

**Claim.** A-12 keeps the version-1 description of `docs/file-formats.md:350-376` under a heading
marked retired, adds a lead paragraph, and replaces only the "Owned by" paths.

**Problem.** Two sentences in the kept text describe what the firmware does, and both become false:

- "A build that finds a larger number refuses the file and records nothing for the session rather
  than overwriting it" (`docs/file-formats.md:364-365`).
- "the save budget is 4,096 bytes" (`:376`).

After #195, no build reads the file at all. The lead paragraph contradicts these sentences, but it
does not correct them. A reader who jumps straight to the `v` bullet is misinformed.

**Evidence.** `sed -n 345,376p docs/file-formats.md`.

**Fix.** In A-12, also put those two sentences into the past tense, and scope them to the firmware
that wrote the file. For example: "Firmware from #78 to #195 refused a larger version rather than
overwrite it, and budgeted saves at 4,096 bytes." The layout bullets (`b`, and the hex encoding)
stay as they are.

VERDICT: CLEAR
