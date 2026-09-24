# Bible verse search: implementation notes

Findings from the real markup (`nwt_S.epub`, Spanish NWT) and deviations from
`docs/superpowers/plans/2026-09-24-bible-verse-search.md`, recorded as each task landed.

## Task 1: folding and tokenising

- **Other scripts are left untouched**, including their case. The plan allowed lower-casing
  "where ASCII-simple"; a partial Greek or Cyrillic case table would fold some letters and not
  others, so the choice is to fold none (`FoldTest.cpp`, `LeavesOtherScriptsUntouched`).
- **Combining marks U+0300..U+036F are dropped**, beyond the plan, so decomposed (NFD) text folds
  like precomposed text. The NWT is precomposed; this only guards other sources.
- **Malformed UTF-8 becomes a space.** The plan requires valid UTF-8 output without saying how;
  a space also separates tokens, so a bad byte never glues two words together.
- **`MIN_TOKEN_BYTES` counts bytes, as the plan says**, not letters as the spec's "one-letter
  tokens are dropped" reads. The two agree for every folded Latin letter; a lone unfolded
  non-Latin letter (2 bytes) is kept.
- **`lib/BibleSearch/library.json` carries only `name` and `version`.** The existing manifests
  (`lib/expat`, `lib/miniz`) exist to set a source filter or `srcDir`; this library needs neither,
  and `lib/StudyStore` has the same layout with no manifest at all.
- **`test/CMakeLists.txt` gains one `add_subdirectory` line per suite.** The epub-dev agent
  definition reserves that file for the orchestrator; the plan's ground rules ask the task to add
  the line, and this worktree has no parallel agent to collide with.
