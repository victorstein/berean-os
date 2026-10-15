Tier: heavy

# PR review 0: berean-o-20 (t7), the reader opens a preaching reference and swipes back to its card

Range: `origin/batch/20261015-predicaci-n-193x...origin/feature/preaching-reader-return` at `289d44c9`.
Brief: `hpipe brief --task t7`. Spec: `docs/superpowers/specs/2026-10-15-berean-o-20-design.md`.
Plan: `docs/superpowers/plans/2026-10-15-berean-o-20-plan.md`. Decision d1 (the verse-offset route
instead of `navigateToHref`) is settled and not reopened here.

`git log` over the range has no empty reply commits. This is the first review of the change.

What I ran:

- `cmake --build build/test` for `ReaderBackSwipeTest VerseHrefTest VerseAnchorsTest ReaderEntryIntentTest MenuCloseRestoreTest`,
  then `ctest -R 'ReaderBackSwipe|VerseHref|VerseAnchors|ReaderEntryIntent|MenuCloseRestore'`:
  `100% tests passed out of 51`.
- `build/pio-x4pro.log`, written after the last code commit, compiles
  `src/activities/reader/EpubReaderActivity.cpp` (line 553) and ends `x4pro SUCCESS`. Its only warning
  is the existing one from the third-party `WebSocketsClient.cpp:573`, so no warning is new.
- The touched files show no formatting diff, and the `style:` commit `289d44c9` is the whole-tree pass.

## Intent

**Opening at an href (brief acceptance 1).** `Route::Href` now calls `openReference(intent.href)`
(`src/activities/reader/EpubReaderActivity.cpp:1149-1151`). The function resolves the spine with
`resolveHrefToSpineIndex`, which strips the fragment (`lib/Epub/Epub.cpp:953-954`). It gets the
verse's offset from a `VerseAnchors::Scanner` fed by `SpineHtmlStream::stream`, then calls
`navigateTo({spine, offsetJump})` (`EpubReaderActivity.cpp:2169-2196`). This is the verse grid's
sequence step for step (`BibleNavigationActivity.cpp:250-264`, offset handed back at `:429`), so the
offset is in the unit `offsetJump` consumes. On a miss the file opens at its start with one
`LOG_ERR` (`:2178-2181`). An unresolved file gets `LOG_ERR` plus `STR_LINK_TARGET_NOT_FOUND`
(`:2171-2175`), the same as `Route::Locate`'s miss (`:1129-1131`). The call sits in the same
lock-free spot before the first render that `Route::Locate`'s `locatePlace` uses (spec A15), so the
`Inflate` popup's render lock cannot deadlock. It is the d1 mechanism, as approved.

**Keeping the origin (acceptance 2).** `preachingOrigin` is initialised from `intent.origin` in the
constructor's member-init list (`EpubReaderActivity.h:232-233`), so `entryIntent = {}` in
`onBookLoaded` cannot reach it. It is declared right after `entryIntent`, so the init order is fine.
The copy does not depend on the intent's kind, so the tag topic's `Kind::OpenAt`, which carries an
origin (`PreachingTopicActivity.cpp:220-224`), keeps it too. That covers the brief's "OpenAt from a
tag topic carries one too".

**The swipe (acceptance 2).** `ReaderBackSwipe::decide` (`src/activities/reader/ReaderBackSwipe.h:19-24`)
is the brief's three-way decision. It pops first, then goes to Predicación on an empty stack with an
origin, and otherwise returns `PassOn`. `PassOn` falls through to the unchanged
`handleBackNavigation()` (`EpubReaderActivity.cpp:717`), so a reader with no origin behaves as before.
The loop wiring (`:700-715`) keeps `getHeldTime()` behind `wasReleased(Back)`. I checked
`MappedInputManager.cpp:211-283`: `wasBackGesture()` sets the touch held-time override on a hit,
and `getHeldTime()` returns it without clearing it while no GPIO edge is pending. Calling it every
loop with an empty stack therefore changes no state that `handleBackNavigation` would not change
anyway. `goToPreaching` goes through the deferred `replaceActivity` (`ActivityManager.cpp:185-197`),
so returning right after it is safe.

**What drops the origin (acceptance 3).** The origin lives only in a member of the heap-allocated
reader. The only places in `src/` that name `PreachingOrigin` outside Predicación are the intent and
this member, and no store serialises either. Home replaces the reader through `ActivityManager`, and
wake reopens it through `goToReader(path, ...)` with no intent (`src/main.cpp:586`, `:600`). Neither
path in this diff records places, so D9 is unchanged.

**Host test (acceptance 4).** `ReaderBackSwipeTest` is its own executable in the existing
`test/ui_layout` suite. `VerseHrefTest` is in `test/verse_anchors`, as d1 widened the task's files to
allow. No new top-level suite was added.

**Scope.** `ReaderActivity.cpp` is untouched, and spec §Architecture explains why: it only forwards
the intent. Two things go past the brief, and the spec raises and argues both. First, a held Left
(the X4 Pro's synthetic button Back) also goes to Predicación (spec A5). The brief's branch at `:694`
already treated button and gesture alike, so this is the faithful extraction. Second, the origin
branch takes the short left-quarter swipe in swipe page-turn mode (A7). Neither is a silent
expansion. `lib/Epub`, `SECTION_FILE_VERSION` and the intent's fields are untouched, as the
non-goals say.

**Plan conformance.** The code and both tests match the plan's text for steps 1–8 character for
character. The commits follow the plan's step order and use its messages. The plan's step 8
formatting commit is present. I found no divergence to explain.

**Tests exercise behaviour.** `VerseHrefAgreement.AnHrefResolvesToTheMarkerItsFragmentNames`
(`test/verse_anchors/VerseHrefTest.cpp:63-77`) runs the real `VerseAnchors::scan` over a short,
public-repo-safe excerpt. It checks that `parse` plus `offsetOf` land on the offset the scanner
recorded for that marker, which is the property spec A11 depends on. The other parse tests pin the
overflow bound at 65536, the trailing-junk rule, and the 45-byte longest real href. The swipe tests
cover every cell of the decision table, including the return stack winning over an origin.

## Quality

- **Mirrors existing patterns.** `ReaderBackSwipe.h` has the same shape as `MenuCloseRestore.h`: a
  firmware-free namespace, an input struct, an `enum class` verdict, and a `constexpr decide`.
  `openReference` and `referencedVerseOffset` repeat `BibleNavigationActivity::loadVerses`'
  scanner/valid/stream/take sequence and its `LOG_ERR("…", "OOM: verse scanner")` shape. The
  `test/verse_anchors/CMakeLists.txt` block copies `VerseAnchorsTest`'s.
- **Duplication considered.** `feedVerseScanner` (`EpubReaderActivity.cpp:94-96`) is identical to
  the one at `BibleNavigationActivity.cpp:37-39`. Each translation unit already keeps its own
  file-local `ChunkSink` trampolines (`feedNavScanner`, `feedChapterNumberReader`). Sharing a
  three-line function would need a header of its own, so this follows the local convention and I
  have not listed it as a finding. `VerseHref::parse` repeats `VerseAnchors.cpp:35-37`'s grammar.
  Spec A11 chose that deliberately, to keep `lib/Epub` untouched, and the agreement test pins it.
- **Resources.** The new code adds no new allocation pattern. The scanner's `State` is
  `new (std::nothrow)` behind `valid()`, and the stream buffer uses `makeUniqueNoThrow` inside
  `SpineHtmlStream`. The stack holds two pointers and a vector header, and the origin adds 6 bytes to
  the activity. There is no `std::function`. Logging goes through `LOG_*`, and the one user-facing
  string is an existing `tr()` key.
- **Comments.** The new comments explain why rather than restating the code: the `getHeldTime`
  ordering hazard, the off-lock precondition for `openReference`, and why `offsetOf` returns by
  value. The rewrites in `ReaderUtils.h:249-256` and `ReturnStack.h:15-21` now describe the merged
  behaviour correctly. One phrase, "as it always has" in `ReturnStack.h`, leans toward history. It
  is not misleading, so it is below the reporting bar.
- **No dead code** and no commented-out code. The stub `LOG_INF` for `Route::Href` is removed.

## Findings

None at BLOCKER, MAJOR or MINOR.

For the owner's device check, which is beyond what a host review can show: spec checks 1, 6 and 10
are the ones that prove the verse-offset route and the swipe-mode band on real hardware. The heap
check can use the existing `MEM` line, printed every 10 s while serial is attached (`src/main.cpp:661-667`).

VERDICT: CLEAR
