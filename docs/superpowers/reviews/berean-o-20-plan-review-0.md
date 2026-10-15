Tier: heavy

# Plan review, pass 0: berean-o-20 (t7, the reader's way back to Predicación)

Plan: `docs/superpowers/plans/2026-10-15-berean-o-20-plan.md`
Spec: `docs/superpowers/specs/2026-10-15-berean-o-20-design.md` (decision d1 not reopened)
Tree: `1f54d2b0` on `feature/preaching-reader-return`

## How this was checked

I read the plan against the spec and against the code it edits. I did not apply or build it.

- **Exact-text replacements.** Each "replace exactly" block is in the tree as the plan quotes it:
  - the swipe branch, `src/activities/reader/EpubReaderActivity.cpp:694-698`;
  - the `Route::Href` stub, `:1132-1134`;
  - the `entryIntent` member and the constructor's init list, `EpubReaderActivity.h:59-60` and
    `:224-227`;
  - the `navigateToHref` declaration, `:204`;
  - the two comments, `ReaderUtils.h:249-254` and `ReturnStack.h:15-18`.
  - The anchors for the insertions are there too: `feedChapterNumberReader` at `.cpp:87` inside
    the anonymous namespace (`:65-163`), and `navigateToHref` ending before `restoreSavedPosition`
    at `:2150`.
- **Real data.** The preaching section was checked against the local NWT
  (`~/Downloads/nwt_S.epub`, files `1001061251`–`1270`):
  - 107 distinct Bible hrefs, every one matching
    `^<digits>(-split<digits>)?\.xhtml#chapter<N>_verse<M>$`, so none fails `VerseHref::parse`'s
    `%c` trailing-junk test;
  - every id an href names exists in its target file, as `<span id="chapterC_verseV">`, so it
    reaches the VerseAnchors scan;
  - the longest href is 45 bytes, `1001061123-split119.xhtml#chapter119_verse105`, the one the
    plan pins at plan `:198-200`.
- **Mechanism.**
  - `referencedVerseOffset` (plan `:539-550`) does the same thing as
    `BibleNavigationActivity::loadVerses`, step for step (`BibleNavigationActivity.cpp:250-264`;
    sink `:37-39`).
  - Its offset feeds `navigateTo`'s `pendingOffsetJump` the way the verse grid's does
    (`EpubReaderActivity.cpp:1030-1032`, `:2118`).
  - `openReference` stays a direct statement of the `Route::Href` case, outside any `RenderLock`
    (spec A15). `ReaderActivity::onEnter` calls `onBookLoaded()` before `requestUpdate()` with no
    lock held (`ReaderActivity.cpp:42-67`).
- **Swipe side effects.**
  - `wasReleased(Back)` → `wasBackGesture` only mutates `touchHeldOverrideValid` on a hit
    (`MappedInputManager.cpp:211-219`).
  - The `&&` keeps `getHeldTime()` (`:278-284`) behind a Back release. On a miss nothing changes,
    so evaluating it every loop is inert, as the spec argues (`spec:263-272`).
- **Coverage.**
  - Goal 1 → step 6; goal 2 → step 5a; goal 3 → step 5b; goal 4 → steps 1–4.
  - The architecture table's eight rows (`spec:104-113`) are each a step.
  - A1–A15 are honoured: A9's message is the existing `STR_LINK_TARGET_NOT_FOUND`, A8's log text
    matches device check 6's search string, and A12 has its own test (plan `:234-237`).
  - Every spec test bullet (`spec:408-427`) is a named test. The counts the plan expects, 4 and 11,
    match the `TEST`s it writes.
- **File lock.** Every touched file is on a column-0 `FILES:` line outside any fence
  (plan `:7-11`). Step 8 reverts anything clang-format touches outside them. No step pushes, opens
  a PR or runs `hpipe submit`.

## MAJOR

### M1 — The "no new warnings" check re-runs a build that has nothing left to compile, so it can never find a warning

**Claim.** Steps 5c and 6c (plan `:442-452`, `:562-567`) verify "no warnings in touched files".

**Problem.** Each step runs `pio run -e x4pro 2>&1 | tail -20` and then runs `pio run -e x4pro`
a second time, piped into `grep 'warning:'`.

- The first run compiles the edited files, but `tail -20` keeps only the last 20 lines. Those are
  the link and size summary, not the compile diagnostics.
- The second run is incremental with nothing changed. SCons recompiles nothing, prints no compiler
  output, and the `grep` always falls through to `echo "no warnings in touched files"`.

So a real `-Wall` warning in `EpubReaderActivity.cpp` or a new header is discarded by the first
command and never produced by the second. The check passes whatever the code is. That changes a
verification outcome: nothing else catches it, because the build has no `-Werror`
(`platformio.ini` and `scripts/` have none) and CI only compiles (`.github/workflows/ci.yml`).
The project forbids committing when "the build fails or warns" (`CLAUDE.md:829`), and says not to
"repeat a target that already passed" (`CLAUDE.md:597-599`). The second run is that repeat, and it
tests nothing.

**Fix (inline).** Replace both blocks (5c and 6c) with one build captured to a log, grepped
afterwards:

```bash
~/.platformio/penv/bin/pio run -e x4pro > build/pio-x4pro.log 2>&1; echo "rc=$?"; tail -5 build/pio-x4pro.log
grep -n 'warning:' build/pio-x4pro.log | grep -i 'reader/\(EpubReaderActivity\|ReaderBackSwipe\|VerseHref\)' || echo "no warnings in touched files"
```

Expected: `rc=0`, `SUCCESS` in the tail, then `no warnings in touched files`. (`build/` is
gitignored, plan `:35`.) Step 6c's run is the last build after the last code edit, so its log is
the one that counts. Step 8's conditional rebuild (plan `:656-662`) is not affected by this.

## MINOR

### m1 — Two new comments say a passed-on button Back goes Home; it can go to the file browser

**Claim.** The `ReaderBackSwipe.h` comment says `PassOn` "leaves the release to
ReaderUtils::handleBackNavigation, which ignores the swipe and sends a button Back Home"
(plan `:140-141`). `ReturnStack.h`'s new text says that once the ring is empty "a held Left goes
Home" (plan `:620-621`).

**Problem.** `handleBackNavigation` sends a short Back to the file browser when
`SETTINGS.backShortToFileBrowser` is set, and a long one to the file browser by default
(`ReaderUtils.h:240-246`, `:261-266`; setting at `CrossPointSettings.h:351`). The spec itself says
"Home or the file browser" (`spec:144-146`). Comment drift only; behaviour is right.

**Fix.**
- Plan `:140-141`: "... which ignores the swipe and sends a button Back Home or to the file
  browser, as its settings say."
- Plan `:620-621`: "otherwise the swipe goes inert and a held Left leaves the book, as it always
  has."

### m2 — The `ReaderUtils.h` rewording still asserts what A7 says is no longer always true

**Claim.** Spec A7 (`spec:331-341`) has the rewording say that, with an origin and an empty stack,
a short left-quarter swipe in swipe page-turn mode now goes to the card instead of paging back.
Plan `:596-597` keeps "in swipe page-turn mode a right swipe must page back instead" as the lead
clause.

**Problem.** It is literally true of this handler, and the next sentence says the Epub reader
decides first. But a reader skimming the comment gets the old rule. That is the confusion that
spec-review MINOR 1 had this comment reworded to remove.

**Fix.** Replace the first two lines of the plan `:596-603` block with: "This handler never exits
on the left-edge swipe, so that in swipe page-turn mode a right swipe can page back. The Epub
reader decides the swipe before this: it returns through its ReturnStack, or, once that is empty,
to the Predicación card it was opened from, which takes a short left-quarter swipe even in swipe
mode (ReaderBackSwipe.h)." Keep the rest as written.

### m3 — `USER_GUIDE.md` will describe the reader's Back as it was before this change (outside this task's lock)

**Claim.** After step 5, a reader opened from Predicación leaves the book on a short left-edge
swipe or a held Left once its return stack is empty.

**Problem.** The guide says the opposite:
- "Swipe right from the left edge | **Back** (on the reading surface, only to return from a link)"
  (`USER_GUIDE.md:49`);
- the reading-surface paragraph at `:60-62`;
- "Back … returns to that position rather than leaving the book" (`:159-160`).

The guide does not mention Predicación at all yet, so this is part of a larger gap in the
feature's documentation, and `USER_GUIDE.md` is not on this task's `FILES:` lines. Not a defect in
the plan.

**Fix.** Do not widen the lock. File it with `hpipe discover`, so that whichever task documents
Predicación also updates `USER_GUIDE.md:49`, `:60-62` and `:159-160`.

---

No BLOCKER. The plan carries out the spec as written. The exact texts it replaces are in the tree,
the verse route matches the verse grid's scan step for step, and every reference in the shipped
preaching section parses and names a marker that exists. M1 is a verification hole, fixed inline by
the single-build command above. The MINORs are comment wording plus one follow-up.

VERDICT: CLEAR
