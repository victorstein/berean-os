Tier: standard

# Issue #206 plan review 0

Plan: `docs/superpowers/plans/2026-09-30-issue-206-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-206-design.md`
Branch `feature/206-typed-references` at `26e9d4b2`.

## How this was checked

- **Host code compiled and run, task by task.** I pulled every code block out of the plan and
  applied each step's edits in order. That gave six states: after 1c, 2a (before 2b), 2b, 3, 4b
  and 5b. Each state was built with the flags from `crosspoint_test_common`
  (`-std=gnu++20 -Wall -Wextra -pedantic`) against the plan's Task 1a source list, linked with
  googletest 1.17, and run in a scratch directory outside the worktree. The results match the
  plan's expectations at every step:
  - Task 1: 5 pass.
  - Task 2a: `AUniquePrefixNamesTheBook` fails and the other 7 pass, as the plan says.
  - Task 2b: 8 pass.
  - Task 3: 11 pass.
  - Task 4: 14 pass.
  - Task 5: 24 pass, including `AReadFailureIsReported`. `IndexReader::open` reads only the
    header (`IndexReader.cpp:18-47`), and `verse()` reads straight through `readAt`
    (`:95-103`), so `readHeaderOnly` behaves as the plan intends.
- **Every `old_string`, checked mechanically.** Every replacement target in Tasks 6 and 8 occurs
  exactly once in its file:
  - `BibleBookNameTable.h`: class comment, `joinToc` declaration, `names` member.
  - `BibleBookNameTable.cpp`: the `load()` scanner block, the `joinToc` clear loop.
  - `BibleSearchActivity.h`: the `results` / `resultsTruncated` pair, `finishWithVerse`.
  - `BibleSearchActivity.cpp`:
    - the `runSearch` head and its `LOG_INF`;
    - the publish block (`:526-532`), `listCount` (`:543`), `ensureRows` (`:549-551`);
    - `activateIndex` (`:675`), `if (results.empty()) {` (`:864`);
    - the `listIndex - 1` branch (`:891-892`) and `drawChrome` (`:942`).
- **Row mapping, including the paths outside the named sites.**
  - The Task 8e grep's expected survivors are right: `:81` `kept - 1`, plus the new
    `std::max(visibleRows, 1)`.
  - After the edits, no hit left in the file maps between rows and results.
  - `loop()`'s `ensureRows(viewTop, viewRows)` (`:161`) and the end of `runSearch`,
    `ensureRows(0, …)`, both go through the corrected `ensureRows`.
  - `prewarmRows` works on cache indices, not list rows.
- **The APIs the plan calls exist.**
  - `BibleNav::Scanner(bool collectText)` and `takeBookNav()` → `BookNavPage{targets, labels, sections}`
    (`BibleNavScanner.h:36-40,46,62`).
  - `ChapterResult{int, std::string, std::optional<uint32_t>}` (`ActivityResult.h:30-37`).
    The reader passes `offsetJump` straight into `navigateTo` (`EpubReaderActivity.cpp:750-752`).
  - `REFERENCE_BYTES = 64` (`BibleSearchActivity.h:86`). That covers a 47 B name plus
    `" 255:255-255"` plus a NUL (60 B), and `goToLabel` has 80 B.
  - Stack in `runSearch`: `label[80]` + `place[64]` + `ResolvedReference` is about 160 B, under
    the 256 B rule.
- **Spec → plan coverage.**
  - A2–A6 map to Tasks 1–3.
  - A7–A10 and the verseCount rule from spec review 0's MINOR 2 map to Task 5.
  - A11–A13 map to Tasks 4, 7 and 8.
  - A14 maps to Task 6.
  - Every parser and resolver case in the spec's testing strategy is present, and the logging
    rows of the error table are implemented.
  - The device checklist is the spec's, plus one item (Judas 5).
- **`FILES:` lines.** The plan's `FILES:` lines (lines 6-10, at column 0, outside any fence)
  list all ten files the steps edit.
  - Task 7's `gen_i18n.py` run and Task 9's `pio run` regenerate `lib/I18n/I18nKeys.h`,
    `I18nStrings.*`. Those files are gitignored build output, so they are not part of the lock.

I found no BLOCKER or MAJOR. The plan is literal enough that an implementer with no other context
arrives at the spec. The findings below are small and can be fixed inline.

## Findings

### MINOR 1 — Task 1 commits a test file that warns

- **Claim:** Task 1 leaves the tree "working and committable".
- **Problem:** Task 1b defines `expectNotAReference` in an anonymous namespace
  (plan `:160-163`), but nothing calls it until Task 2a. The Task 1 commit therefore builds with
  `-Wunused-function` from the test target's own `-Wall -Wextra`.
- **Evidence:** compiling the Task 1 state gives
  `TypedReferenceTest.cpp: warning: unused function 'expectNotAReference' [-Wunused-function]`.
  The flags come from `test/CMakeLists.txt:42-46`. The warning is not fatal, since there is no
  `-Werror`.
- **Fix:** move `expectNotAReference` out of the 1b block and into the start of Task 2a's append.
  Task 2a is its first caller, and it can sit in its own `namespace { }` there.

### MINOR 2 — `normalise` keeps non-ASCII space and punctuation bytes

- **Claim:** A4 normalisation makes `1 Juan`, `1Juan` and `1 juan` meet.
- **Problem:** the plan's `normalise` (`:373-384`) keeps every byte ≥ 0x80. It therefore keeps
  U+00A0, U+202F, `’` and similar characters, while `Fold.cpp`'s own `isTokenCodepoint`
  (`Fold.cpp:88-99`) treats them as separators. A TOC title or nav label with a no-break space
  (`1 Juan`) would normalise to `1\xC2\xA0juan` and never match the typed `1 Juan`, whether
  exactly or by prefix.
- **Evidence:**
  - The plan follows spec A4 to the letter (spec `:48-50`), so this is a spec-level gap, not a
    plan deviation.
  - I found no NBSP in the book-name fixtures. `BibleNavLabelsTest.cpp:20-27` uses an ordinary
    space in `1 Cr\xC3\xB3n.`.
  - The same publisher's verse markup does carry U+00A0 and U+202F (project memory, "Offline vs
    device passage extraction").
  - The risk is plausible, not demonstrated.
- **Fix, optional:** in `normalise`, decode the folded text by codepoint. Drop U+0080–U+00BF,
  U+2000–U+206F and U+FEFF, and keep everything else, which mirrors `isTokenCodepoint`'s
  separator set. Add one parser case where a name contains `\xC2\xA0`. If this is deferred, the
  human tester should try `1 Juan 4:8` on the Spanish Bible, which the device checklist does not
  cover today.

### MINOR 3 — Tasks 6–8 are committed without any build check

- **Claim:** every step starts with a failing test and leaves the tree committable.
- **Problem:**
  - Task 6 (`BibleBookNameTable`), Task 7 (YAML) and Task 8 (activity) have no test. They
    cannot have a host test: `Epub.h` and the Arduino renderer are not host-buildable, and the
    spec says so (spec `:294-295`).
  - They are also committed without a compile. The first `pio run` is in Task 9, so a typo in
    Task 6 would land as a broken commit and be fixed in a later one.
- **Evidence:** plan `:948`, `:1074`, `:1347`. Task 9 (`:1355`) is the first firmware build.
  The repo rule is "build once after the last code edit" (CLAUDE.md, Testing checklist 1), so
  deferring the build is allowed. The cost is only that the intermediate commits have not been
  verified.
- **Fix, optional:** either accept this as is, or say in Task 9 that a build failure there is
  fixed by amending the offending task's commit (`git commit --fixup`), not with a trailing fix
  commit.

### MINOR 4 — The spec's `abbreviationForBook()` accessor is dropped

- **Claim:** every spec requirement maps to a step.
- **Problem:** spec §2 (`:184-185`) asks for `abbreviationForBook(uint8_t)`. The plan leaves it
  out on purpose (`:1072`), because `nameSource()` is its only reader.
- **Evidence:** plan `:1072` against spec `:184-185`. Nothing in the spec or the plan calls the
  accessor. The device checklist does not depend on it, and the grid keeps its own copy.
- **Fix:** none needed in the plan. It is a sound YAGNI cut and already stated. Record it in the
  PR body next to the other hand-offs, so a reader comparing the PR with the spec is not
  surprised.

## Verdict

The plan is sound. Every code block builds and every step's expectation holds when it is
executed literally. The row-arithmetic rewrite reaches every site the spec names, and the grep
guard is correct.

VERDICT: CLEAR
