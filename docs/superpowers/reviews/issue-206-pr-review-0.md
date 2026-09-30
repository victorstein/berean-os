Tier: standard

# PR #211 review 0 — typed Bible references in verse search (#206)

Reviewed `git diff main...HEAD` on `feature/206-typed-references` (head `950c3bdc`) against issue
#206, the spec `docs/superpowers/specs/2026-09-30-issue-206-design.md` and the plan
`docs/superpowers/plans/2026-09-30-issue-206-plan.md`. `TypedReferenceTest` rebuilt from
`build/test` and run: 25/25 pass.

## Intent

**Issue acceptance criteria.**

- *Host tests cover full names, abbreviations, Spanish and English, chapter-only references,
  ranges and malformed input (no false positives on ordinary words)* — met.
  `test/ui_layout/TypedReferenceTest.cpp:98-107` (full names, both languages),
  `:109-118` (abbreviations, including two-byte exact `Is.`/`Ps.`), `:120-125` (case/accents),
  `:134-144` (chapter-only, verse, range with `-`, spaced `-`, en dash), `:172-194` (two-letter
  function words, ordinary words with and without numbers, and 20 malformed shapes).
- *Device: `Isa 40:31` and `Gén 1` open the right place* — not verifiable here; it is items 1-2 of
  the PR's device checklist, explicitly marked "not yet run". That is the human tester's step, as
  CLAUDE.md requires.
- *"Go to Isaiah 40:31" above the word hits; selecting it opens there* — implemented:
  `BibleSearchActivity.cpp:927-928` (row 1 label), `:700-702` → `finishWithReference`
  (`:707-712`), which hands the reader the same `ChapterResult{spine, "", offset}` shape
  `finishWithVerse` uses.

**Spec requirements, checked one by one.**

- A2 grammar, `:` only, `-`/en dash, spaces around separators: `TypedReference.cpp:35-115`.
- A3 chapter required / A6 1..255, ≤3 digits, end ≥ start: `:62-74`, `:107-115`.
- A4 normalisation (fold, then drop ASCII punctuation and non-ASCII separators):
  `:124-158`. The added separator set (U+00A0, U+202F, U+FEFF) goes slightly beyond the spec's
  "drop non-alphanumeric ASCII", but it is the same real-markup hazard recorded in project memory
  ("Offline vs device passage extraction") and is tested (`TypedReferenceTest.cpp:127-132`). A
  justified refinement, not scope creep.
- A5 exact-beats-prefix, single exact wins, ≥2-byte key, ≥3-byte prefix: `:175-195`,
  `:246`; tests `:146-179`.
- A7 range kept for the label, opens at the first verse: `TypedReferenceTest.cpp:348-354`.
- A8 single-chapter rewrite without a book list, only for chapter-only input, only when chapter 2
  is absent: `TypedReference.cpp:281-292`; tests `:362-379`.
- A9/A10 missing places not found; chapter-only has no offset: `:293-298`; tests `:332-360`.
- Lower bound at `verseCount` is not-found, checked before `verse()`: `:226`; test `:381-384`.
- A11 no "No verse contains…" line under a Go-to row, header counts the row:
  `BibleSearchActivity.cpp:900`, `:980`.
- A12 row order and initial selection: `:556`.
- A13 label from the publication's full name via `tr(STR_BIBLE_SEARCH_GO_TO)`: `:524-529`.
  Buffer sizes hold the worst case (48 B name + " 255:255-255" fits `place[64]`; "Ir a " + 60
  fits `goToLabel[80]`), so no mid-codepoint truncation.
- A14 table keeps abbreviations; `joinToc` clears them, `load()` fills after:
  `BibleBookNameTable.cpp:41-57`.
- `firstResultRow()` routed through every conversion site the spec lists: `listCount` (`:567`),
  `ensureRows` (`:573-576`, with the spec's exact arithmetic), `activateIndex` (`:704`),
  `buildResults` (`:930`); `nav.reset` uses the A12 row directly. `moveTo`, `scrollPage` and
  `navigateButtons` all go through `listCount()`, so nothing literal survives.
- Resolve time is logged beside query time (`:541-543`), per spec review MINOR 1.

**Scope.** Nothing beyond the spec: no Home/menu parsing, no index or store change, no new UI
component. i18n keys are committed with the hand-off lines in the PR body, as #197 did; the
top-level `test/CMakeLists.txt` is untouched.

**Plan divergence.** The only one — `abbreviationForBook()` not added — is stated in the plan
(`plan.md:1112`) and in the PR body. None unexplained.

**Tests.** They assert behaviour (parsed triples, resolved spine/offset, not-found vs. I/O error
via a header-only `ByteSource`) against a real in-memory index built with `IndexBuilder`, not the
implementation's internals. Fixtures are short name lists, consistent with the public-repo rule.

## Quality

- `TypedReference.{h,cpp}` follows the `BibleReference.h` pattern (pure, Arduino-free, built into
  `test/ui_layout`), and the new CMake target mirrors `BibleReferenceTest` plus the source list of
  `test/bible_search_index`.
- `BibleBookNameTable::load` now reads labels exactly as `BibleNavigationActivity::loadBooks`
  does (`collectText=true`, `takeBookNav()`, index-aligned `page.labels`,
  `copyUtf8Truncated`). The grid's table gains an abbreviations array that stays empty because
  it calls `joinToc` only; the spec accepted that cost (§2 "Memory") and the activity object is
  already in PSRAM, so it is not raised as a finding.
- `finishWithReference` mirrors `finishWithVerse`'s leave sequence (`leaving`, `clearTapFlash`,
  `setResult`, `finish`).
- Error handling matches the file's shape: `LOG_ERR` and fall back to no row on an index read
  failure; `LOG_DBG` for a place the index lacks; the word query then runs and reports its own
  failure through the existing path.
- Allocation: one `std::string` reserved to 64 B per entered query, never per frame; key buffers
  are fixed `char[65]` on the loop task's stack (well under the 256 B local rule). Justified in
  the spec and matches `runQuery`'s existing allocation class.
- Comments state reasons (why `MIN_PREFIX_BYTES`, why the separator set, why the lower bound is
  checked before `verse()`), not narration. No dead or commented-out code.
- One small duplicate and one disclosed gap, below.

## Findings

1. **MINOR** — Second copy of the reference-formatting rule.
   `BibleSearchActivity.cpp:597-603` (`ensureRows`) formats a hit row as `"%s %u:%u"` with a
   numbers-only fallback when the book name is empty; `TypedReference.cpp:254-270`
   (`formatTypedReference`) now encodes the same rule, including the same fallback. The two can
   drift (e.g. a future separator change). Optional: have `ensureRows` call
   `formatTypedReference` with a `TypedReference{book, chapter, verse}`. Not required for this PR.

2. **MINOR** — The Go-to label is not prewarmed.
   `BibleSearchActivity.cpp:950-973` (`prewarmRows`) covers `rows[]` references and snippets
   only; `goToLabel` (`:928`) is drawn without the fallback-glyph prewarm pass. Correctness is
   unaffected (slower first paint on a non-Latin publication, one row). Already disclosed in the
   PR body as a known gap; recorded here so it is not lost.

No BLOCKER or MAJOR findings. The remaining open item is the on-device checklist in the PR body,
which only the human tester can run.

VERDICT: CLEAR
