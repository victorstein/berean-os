Tier: standard

# Plan review 0 — issue #195 (retire Bible chapter-completion recording)

**Plan:** `docs/superpowers/plans/2026-09-29-issue-195-plan.md`
**Spec:** `docs/superpowers/specs/2026-09-29-issue-195-design.md` (v3)
**Tree read at:** `7189a9c9`

## What was checked, and held up

- **Spec → step mapping.** Every assumption has a step. A-2/A-3/D1 map to Steps 1–3, A-4/A-5 to Step 4,
  A-6 to Step 5, A-7/A-8/A-9 to Step 6, A-10/A-11 and the `PersistableStore.h` comment to Step 7,
  Testing §2 to Step 8, Testing §3 to Steps 6, 8 and 9, A-12/A-13 to Step 10, and Testing §4 plus the
  "Shared files touched" requirement to Step 11. A-1 and A-14 need no step. A-9 and A-10 follow
  t5-d1 as settled.
- **The quoted "replace" texts match the tree, character for character.** Checked:
  `EpubReaderActivity.cpp:36-37` (include block), `:219-221`, `:903`, `:907-909`, `:915-917`,
  `:940-945`; `EpubReaderActivity.h:81-86`; `StudyStore.cpp:8`, `:19-21`, `:71-76`, `:89`, `:110-127`;
  `StudyStore.h:10`, `:130-144`, `:193-200`; `test/storage_io/CMakeLists.txt:1-3`, `:15`, `:21`,
  `:24`; `test/CMakeLists.txt:117`; `lib/Serialization/SdPaths.h:32`; `PersistableStore.h:110-112`;
  `english.yaml:373-374`; `USER_GUIDE.md:431`; `docs/file-formats.md:350-376`, which ends right
  before `## The \`/.crosspoint/*.json\` stores (shared rules)` at `:377`.
- **Names and signatures stay consistent.** `forwardTurnLeavesDocument(int, int, bool)` in Step 2
  has the same body as `lib/StudyStore/StudyStore/ChapterCompletion.h:66-68`. Steps 1 and 3 call it
  with the same arity. No `using namespace study` exists anywhere in `src` or `lib`, so Step 3's
  unqualified call resolves to the new global function, and the tree still compiles while
  `study::forwardTurnLeavesDocument` exists.
- **Host-test wiring.** `test/auto_page_turn/CMakeLists.txt` already puts `${REPO_ROOT}/src` on the
  include path. Step 1 therefore fails for the stated reason, and Step 2 turns it green. The only
  CMake references to the deleted sources are `test/storage_io/CMakeLists.txt:15, 21, 24` and
  `test/chapter_completion/`, and Step 6 removes both. Nothing under `.github/` names the suite.
- **The test count.** `ctest --test-dir build/test -N` on the existing build prints
  `Total Tests: 1302`, and `grep -c -E "ChapterCompletion|CanonicalChapters"` prints `32`. So
  1302 − 32 + 2 = 1272 is correct, and the renamed `ForwardTurn` suite brings the count grep to 0.
- **The removal grep.** Outside the files being deleted, every grepped token occurs only in
  `StudyStore.*`, `EpubReaderActivity.*`, `PersistableStore.h:111`, `english.yaml` and
  `test/storage_io/CMakeLists.txt`, and the plan edits all of them. `BIBLE_BOOK_COUNT`,
  `canonicalChapterCount` and `completion_` have no other users. `gen_i18n.py` scans only
  `src`/`lib` `.c/.cpp/.h` files and skips the generated headers
  (`scripts/gen_i18n.py:257-280, 831`), so removing the YAML keys in Step 7, after their uses go in
  Step 4, cannot trip its missing-key exit (`:869-877`).
- **Transitive includes.** `StudyStore.h:10` is the only way `ChapterCompletion.h` is reached.
  Its contents stay reachable another way: `<ArduinoJson.h>` through `StudyStore/PassageDoc.h:3`,
  and `UnitAnchors.h` through `study/UnitIndexCache.h:10` → `StudyStore/UnitIndexFormat.h:8`. No
  includer of `StudyStore.h` uses `std::array`. Step 8's fallback is very unlikely to be needed.
- **FILES lock.** Every file a step edits, creates or deletes appears on a column-0 `FILES:` line
  (`plan:9-15`), with `test/chapter_completion/` as a directory prefix. `build/pio-195.log` sits
  under the gitignored `build`.
- **Push and upstream.** The branch tracks `origin/refactor/195-retire-chapter-completion`, and
  `push.autoSetupRemote=true` is set, so Step 11's bare `git push` works. The #185, #187 and #194
  plans end the same way.

No BLOCKER or MAJOR findings.

## MINOR

**MINOR 1 — Step 8's missing-include fallback could edit a file outside the lock.**
Claim: `plan:447-450` says that if the build fails, the implementer should "include the header
that actually declares it" in whichever file needs it. Problem: that file might be one of the other
`StudyStore.h` includers (`BibleSearchStore.cpp`, `HighlightsActivity.cpp`,
`PassageSelectActivity.cpp`, `PassageLinksActivity.cpp`, `TagPickerActivity.*`,
`TagFilterActivity.h`), and none of them is on a `FILES:` line. Evidence: the `FILES:` lines at
`plan:9-15` list only `StudyStore.*` and `EpubReaderActivity.*` among these files. Fix: limit the
fallback to adding the include to `src/study/StudyStore.h`, which is already locked. If the fix is
needed anywhere else, stop and escalate. (Per the transitive-include check above, the fallback
should never fire.)

**MINOR 2 — Step 9's `git add -A` sweeps in anything left untracked.**
Claim: `plan:473` and `:481` stage the whole tree, and the second command commits it as
`style: clang-format`. Problem: any uncommitted pipeline file present at that point, such as a
review file like this one that hasn't been committed yet, would land in a style commit and put an
unlocked path in the diff. Evidence: `plan:473, 481`. Fix: use `git add -u` for both (every new
file is already tracked by Step 2), or stage only the paths clang-format changed.

**MINOR 3 — The `SdPaths.h` comment is two lines, and A-11 asks for one.**
Claim: `plan:413-416` adds a two-line comment. Spec A-11 says "a one-line comment". Problem: a
small literal deviation, harmless in substance. Fix: shorten it to one line, for example
`// Retired in #195; older firmware's files stay on cards, so the path is reserved.`, or record
the deviation in the PR.

VERDICT: CLEAR
