Tier: heavy

# Spec review 0: berean-o-20 (t7), the reader opens a preaching reference and swipes back

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-20-design.md` at `96a04c29`, against
`hpipe brief --task t7`, the research note, the parent spec
`docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §6, and the code at that commit.

Most of the design holds up. I checked these and found them correct: copying the origin in the
constructor (A1); the swipe precedence (A4); treating the button and the gesture alike (A5); the
swipe page-turn consequence (A7); the OOM path (A11); and place recording (A12,
`EpubReaderActivity.cpp:1697`, `:1737`). Home and sleep drop the origin (`ActivityManager.cpp:76-81`,
`:241-244`, `src/main.cpp:594-601`). Nothing persists the origin. The reader's destructor parks the
Bible before `PreachingActivity::onEnter` runs (`ActivityManager.cpp:146-161`). One finding blocks:
the mechanism that is supposed to open the Bible at the verse cannot reach the verse.

## BLOCKER 1: `navigateToHref` cannot land on a verse; every Predicación reference opens at its chapter's start

**Claim.** Goal 1 and `Route::Href`: "An `OpenHref` intent opens the Bible at that verse via
`navigateToHref(href, false)`". The spec treats a missing anchor as a rare defensive case (A8, and
"The anchor miss"). Device check 6 expects "no `Anchor ... not found` for the 20 questions'
references".

**Problem.** The anchor path can never resolve an NWT verse. Verse markers are empty `<span>`s.
`ChapterHtmlSlimParser` deliberately leaves span ids out of the anchor map, so
`Section::findAnchor("chapterC_verseV")` always returns `nullopt`. The build loop then lays out the
whole spine item looking for the anchor (`EpubReaderActivity.cpp:1417-1419`), and the page falls
back to 0. As specified:

- every reference from a question card opens at the top of its chapter, not at the verse;
- the new `LOG_ERR` fires on every open;
- device checks 1 and 6 fail the first time they are run.

The brief's goal ("opens the Bible at that verse") and its prescribed mechanism (`navigateToHref`)
contradict each other on this EPUB. The spec inherited the contradiction from the parent spec §6
and the brief without checking it.

**Evidence.**

- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:109`:
  `bool isNonNavigableInlineElement(const char* name) { return strcmp(name, "span") == 0; }`
- That predicate gates anchor recording at `:454`:
  `if (isTocAnchor || (!isNonNavigableInlineElement(name) && ...))`. This is upstream's
  `2ea042a6 fix: skip <span> anchors (#2303)`.
- The only bypass is a TOC anchor (`Section.cpp:400-411`). Neither NWT TOC names a verse:
  `unzip -p ~/Downloads/nwt_S.epub OEBPS/toc.ncx | grep -c 'chapter[0-9]*_verse'` → `0`, and the
  same for `OEBPS/toc.xhtml` → `0`.
- `lib/Epub/Epub/VerseAnchors.h:9`: "Verse markers in these EPUBs are empty spans with ids shaped
  `chapter<N>_verse<M>`". For example, `OEBPS/1001061123-split83.xhtml` has
  `<span id="chapter83_verse18"></span>`.
- I scanned every `href="…#chapterC_verseV"` in `OEBPS/1001061251`–`1270.xhtml`. Result:
  `verse hrefs: 109, files missing: 0, anchors missing: 0, element carrying the id: {'span': 109}`.
  104 of the 109 point past verse 1 and 66 point at verse 10 or later. All of them would land on
  the wrong page.
- The repo has already decided this once.
  `docs/superpowers/specs/2026-09-12-bible-verse-navigation-design.md:72-80`, "Why anchors cannot
  be used": "Verse markers **are** spans, so verse ids never enter the anchor map and
  `pendingAnchor` can never resolve them … The working route is … verse → visible offset → …
  `pendingOffsetJump`".
- Every existing verse jump follows that rule:
  - the verse grid hands back an offset and an empty anchor (`ChapterResult{spineIndex, "", offsetJump}`,
    `BibleNavigationActivity.cpp:390`), consumed at `EpubReaderActivity.cpp:1030-1032`;
  - search hands back `verse.offsetJump` (`:1047`);
  - `Route::Locate` uses `location->offset` (`:1119`).

**Fix.** Resolve the verse to a visible-text offset and navigate by offset. This mirrors
`BibleNavigationActivity::loadVerses` (`BibleNavigationActivity.cpp:250-264`, sink at `:37-39`),
and everything it needs is already in `src/activities/reader/`.

1. `Route::Href`, off-lock in `onBookLoaded` as `Route::Locate` is:
   - Call `epub->resolveHrefToSpineIndex(intent.href)`. On `< 0`, `LOG_ERR` and show
     `STR_LINK_TARGET_NOT_FOUND`, as A9 already says.
   - Parse the fragment's `chapterC_verseV` with a small pure function in a firmware-free header.
     Host-test it: well-formed, no fragment, a non-verse fragment, junk, and overflow.
   - Stream that spine item into a `VerseAnchors::Scanner` with
     `SpineHtmlStream::stream(epub, spine, renderer, sink, &scanner)`. This is `WhenMissing::Inflate`
     and safe here because no lock is held; `SpineHtmlStream.h:36-38` says it leaves the HTML cache
     behind, so the build that follows does not inflate a second time.
   - Find the anchor whose `(chapter, verse)` matches and call
     `navigateTo({.spineIndex = spine, .offsetJump = anchor->offset})`.
   - If nothing matches, `LOG_ERR` in the route and call `navigateTo({.spineIndex = spine})`, the
     file's start. This is the brief's "open that file at its start and LOG_ERR".
2. Drop the `LOG_ERR` at the shared section-load site (A8). The miss is now known in the route, off
   the render path. Footnotes and the chapter picker are not touched, and A8's trade-off goes away.
3. Drop or re-scope A10. `navigateToHref` no longer needs to return `bool`.
4. Add the resource note. The scanner's expat parser and an anchor vector of 176 × 8 B at most
   (Psalm 119), plus `SpineHtmlStream`'s 2 KB chunk buffer, are all transient and freed before
   `navigateTo`. Null-check the scanner as `loadVerses` does (`:256-260`).
5. Change device check 6 so it confirms that verse V is on screen, not that a log line is absent.

The other route is to record verse-grammar span ids in `ChapterHtmlSlimParser`. That belongs to
`lib/Epub` (epub-dev, outside T7's surface), needs a `SECTION_FILE_VERSION` bump that invalidates
every cached section on every device, and grows the anchor map in long chapters. I don't recommend
it.

The brief and the parent spec §6 both name `navigateToHref(href, false)`. Record the departure with
`hpipe decide --task t7`, recommending the offset route on the grounds that the brief's goal ("opens
the Bible at that verse") outranks the mechanism it named. Don't depart from it silently.

## MINOR 1: two comments in the reader's surface go stale

**Claim.** The Architecture table lists every file the change touches.

**Problem.** After T7, a left-edge swipe with an origin and an empty stack leaves the reader. Two
comments say the opposite, and CLAUDE.md requires comments that are true of the merged state.

**Evidence.**

- `src/activities/reader/ReaderUtils.h:249-254`: "The reading surface deliberately has no left-edge
  swipe-to-exit path".
- `src/activities/reader/ReturnStack.h:15-16`: "The left-edge swipe is this device's only Return and
  goes inert once the ring empties".

**Fix.** Add both to the file table, rewording each to cover the Predicación origin. Both files are
under `src/activities/reader/`, which T7 owns. The project memory "Reader swipe is Return-only" will
need the same correction, but that is outside the repo and is the orchestrator's to update.

## MINOR 2: A6 says a long edge hold "stays inert"; in swipe page-turn mode it pages back

**Claim.** A6: "A long edge hold passes on, so it stays inert."

**Problem.** That is true only in tap mode. With `TOUCH_READER_SWIPE`, a slow left-edge swipe also
reads as a right swipe:

- `PassOn` reaches `handleBackNavigation`, which returns `false` for the gesture (`ReaderUtils.h:255`);
- `detectTouchPageTurn` has set `prev` (`ReaderUtils.h:88-92`), and swipe mode leaves `heldMs` at 0
  (`:80`);
- so the reader pages back (`EpubReaderActivity.cpp:741-742`).

This is today's behaviour too, so nothing regresses. But a tester who reads A6 literally will think
the paging is a bug.

**Fix.** Say "behaves as today: inert in tap mode, a page back in swipe mode".

## MINOR 3: wrong reason given for "no new side effect"

**Claim.** "Both are `const` reads (`src/MappedInputManager.cpp:263-285`)."

**Problem.** `getHeldTime()` is a `const` method, but it writes the `mutable` `touchHeldOverrideValid`
(`MappedInputManager.cpp:283`; members at `MappedInputManager.h:120-122`). The conclusion still
holds, for a different reason. `&&` short-circuits, so `getHeldTime()` runs only on a Back release.
For a gesture, `wasEdgeSwipe` has just refreshed the override in the same frame (`:218`, `:130-134`),
so the read returns it without clearing it. For a button, `handleBackNavigation` makes the same call
anyway (`ReaderUtils.h:261`).

**Fix.** Replace "const reads" with that reasoning, so a later change doesn't lean on a guarantee the
method doesn't give.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 0
