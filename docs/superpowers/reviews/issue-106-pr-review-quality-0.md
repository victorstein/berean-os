Tier: heavy

# PR #176 review (code quality): `-Wall` on repo sources, warnings at zero

- **PR:** #176, `fix/106-enable-wall` at `3e23657e` (code at `4c7ee16f`)
- **Evidence read:** `gh pr diff 176 --repo victorstein/berean-os` (every non-`docs/superpowers` hunk),
  the touched files at HEAD around each hunk, the sibling scripts `scripts/patch_jpegdec.py` and
  `scripts/tests/test_build_catalog_index.py`, and `git grep maybe_unused main -- src lib` for the
  existing idiom. I re-ran `python3 -m unittest discover -s scripts/tests`: 37 tests, OK.
- **Not re-run:** `pio run` (shared build lock). The zero-warning census is taken from the PR body
  and the intent review's artefacts.

## Does it mirror existing patterns?

**Build script.** `scripts/enable_repo_warnings.py:1-34` follows the shape of the other `pre:`
scripts:
- a module docstring that gives the reason;
- `Import("env")  # noqa: F821 (SCons-injected global)`, the same line `scripts/patch_jpegdec.py:19` uses;
- paths derived from `env["PROJECT_DIR"]`.

Registration sits in the existing `[base] extra_scripts` list (`platformio.ini:137`), so both envs
inherit it. There is no second mechanism.

**Pure module and tests.** Splitting the SCons-free rule into `scripts/repo_warnings.py` mirrors
`build_catalog_index.py` and its test. `scripts/tests/test_repo_warnings.py:7-10` uses exactly the
same `sys.path.insert` idiom, with the same comment, as `test_build_catalog_index.py:8-11`. So the
new test runs in the existing script-tests job and adds no new harness.

**Log-only locals.** `[[maybe_unused]] const ... = millis();` is already the house idiom on `main`:
`src/study/BibleSearchIndexer.cpp:152,228,256` and `src/activities/reader/BibleSearchActivity.cpp:512`.
The PR applies it uniformly: 44 sites, all attribute-first, with no `(void)x;` casts and no
`#if LOG_LEVEL` blocks mixed in. I spot-checked these, and each variable's only reader is a `LOG_*`
call:
- `Section.cpp:279` → `:315`
- `JpegToFramebufferConverter.cpp:502` → `:510`
- `PngToFramebufferConverter.cpp:472` → `:483`
- `BookMetadataCache.cpp:292` → `:293`
- `main.cpp:799` → `:805`
- `CrossPointWebServerActivity.cpp:134` → `:140`

**Switch completion.** Every switch is completed with explicit labels and no `default:`, so `-Wswitch`
keeps working for the next enum value:
- `EpubReaderActivity.cpp:789-793` puts `AUTO_PAGE_TURN` and `ROTATE_SCREEN` next to `NIGHT_MODE` and
  `FRONTLIGHT` (`:782-788`). It uses the same "handled elsewhere; `break`" shape, with a one-line
  reason. That reason is accurate: `EpubReaderMenuActivity.cpp:109,124` opens the popups, and
  `:150` returns the values in `MenuResult`.
- `BibleDownloadActivity.cpp:275` adds `NoEpubEdition` to the fall-through group that already ends
  in `fail(...)`. It does not add a second failure path.

**C3 guard.** `HalGPIO.cpp:44,115` wraps the helpers in the same `#if FREEINK_MCU_C3` that guards
their only caller at `:118`. It does not add a new macro and does not delete code.

## Structure and readability of the rewrites

- **`EpubReaderActivity.cpp:1048-1053` and `ReaderBookmarks.cpp:125-128`.** Each nested `?:`
  becomes an `if`/`else if` that starts from a default-constructed (or `visibleOffset`-seeded)
  `std::optional`. These read better than the originals. The negated condition in
  `EpubReaderActivity.cpp:1051`, `!pendingPageJump.has_value() && pendingAnchor.empty() &&
  currentSpineIndex == cachedSpineIndex`, is the correct De Morgan form of the old test. Losing
  `const` on the two locals is the accepted cost of the `if` form.
- **`CrossPointWebServerActivity.cpp:203-205`.** The runtime `if`/`else` over a `constexpr` value
  becomes a `static_assert` and one call. This removes a branch that the compiler already proved
  dead, and it deletes two comments that restated the code (`// Start soft AP`,
  `// Open network (no password)`). `#include <string>` at `:13` is the correct header for
  `std::char_traits`.
- **`FontDownloadActivity.cpp:130,136`.** The named `JsonArray` locals (`styleList`, `fileList`)
  are named after their JSON keys, like the surrounding `fObj`/`fileObj`. The dead `contentTop` at
  the old `:638` is deleted, not suppressed, which is the right choice for a truly dead value.
- **Init-list reorders** (`ChapterHtmlSlimParser.h:163-164`, `ProgressMapper.cpp:658`) move only the
  initialisers, not the declarations. That is the minimal edit.
- **Comments.** The only new comments are "why" comments:
  - `repo_warnings.py:8-10` gives the provenance of the vendored list;
  - `repo_warnings.py:17-18` explains why the drive check precedes `relpath`;
  - `EpubReaderActivity.cpp:790-791`.

  None restates the next line.

## Tests

`test_repo_warnings.py` tests behaviour through the real `posixpath` and `ntpath` modules via the
`pathmod` seam. It does not mock them. The cases target the ways this kind of predicate usually
fails:
- prefix look-alikes: `lib/expatfoo`, `srcx`, `libx`, and the sibling `berean-os-old`;
- `..` normalisation in both directions;
- a file at the project root;
- Windows case and slash folding;
- a cross-drive path that must not raise. This one would fail against a naive `relpath`
  implementation.

`subTest` is used for the table-driven groups, so a failure names the path. The SCons glue
(`add_wall_to_repo_sources`) has no unit test. That is reasonable, since it cannot run outside
SCons, and the verbose compile-line proof in the PR body covers it.

## Duplication check

- `VENDORED_LIBS` (`repo_warnings.py:11`) overlaps with the exclusions in `bin/clang-format-fix:53-54`
  and `bin/clang-format-fix.ps1:98`, but the lists are not the same concept. The formatter excludes
  `lib/uzlib` and `lib/miniz/third_party` for formatting reasons and does not exclude expat. A shared
  list would couple two unrelated policies, so keeping them separate is correct.
- No other ownership or path-classification helper exists under `scripts/`.

## Findings

No BLOCKER and no MAJOR.

### MINOR 1: `WifiSelectionActivity.cpp:977-980`: the new case group is unreachable, and its comment implies otherwise

`render()` returns early for both `PASSWORD_ENTRY` and `HIDDEN_SSID_ENTRY` at `:945-947`, so the
switch never sees either value. The PR body says this ("`PASSWORD_ENTRY` is unreachable in that
switch"). The reworded comment at `:979`, "Transitioning to/from a keyboard subactivity - nothing to
draw", repeats the early-return comment at `:943-944`. It reads as if this `case` is where that
transition is handled.

Explicit labels are still the right fix, because they keep `-Wswitch` exhaustive. The wording is
the only issue.

Fix inline if you want: say the case is unreachable because of the early return, for example
`// Unreachable: render() returns early for keyboard-entry states.`, or leave the comment as the
pre-existing text. This does not affect behaviour or scope.

VERDICT: CLEAR
