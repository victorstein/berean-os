Tier: standard

# berean-o-1 (R20) spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-10-11-berean-o-1-design.md` (at `52ab5fe2`), checked against the
brief (`hpipe brief --task t1`), the research note, the human's d1 ruling (`hpipe bead show berean-o-1`,
comment "Ruling by the human on t1/d1"), the code at `HEAD`, and the `freeink-sdk` submodule.

## What holds

I checked these against the code and found them correct:

- **The two edits.** Both entry HALFs come from the lines the spec names, and removing them changes
  nothing else.
  - The flag sites are `BibleSearchActivity.cpp:114` and `PublicationsActivity.cpp:55-56`.
  - The defaults are `fullRefresh = false` (`BibleSearchActivity.h:177`) and
    `halfRefreshPending{false}` (`UiListActivity.h:89`).
  - `UiListActivity::onEnter` never sets the flag (`UiListActivity.cpp:20-27`).
  - `BibleSearchActivity` overrides `render`, so it never reads the inherited flag
    (`BibleSearchActivity.cpp:1011-1035`).
- **Origins.**
  - Publications has one constructor call (`LauncherActivity.cpp:434`).
  - Bible search has one (`EpubReaderActivity.cpp:980`). It is reached from the menu (`:810-811`) and
    from Home's Search tile (`HomeTargets.h:69-70` → `EpubReaderActivity.cpp:1066-1067`).
- **The Home-origin search frame is painted over Home.** The reader never paints first:
  - `ReaderActivity::onEnter` calls `onBookLoaded()` before `requestUpdate()`
    (`ReaderActivity.cpp:65-66`);
  - `requestUpdate` is deferred (`ActivityManager.cpp:285-295`);
  - the push is processed in the same `loop` pass before the render notify (`:139-172`).
- **The menu-origin search frame is painted over the menu sheet.** The result handler's push
  suppresses the popped reader's repaint (`ActivityManager.cpp:131-133`).
- **Driver behaviour.**
  - UC8179 FAST is a DU partial against the previous frame (`Uc8179Driver.cpp:292-293,345,374`).
  - A post-AA FAST is intercepted by `transitionGrayscaleBase` (`:174-176,182-212`), with
    `_redriveAfterGray` set at `:577`.
  - SSD1677 promotes FAST to HALF only when grayscale cleanup was skipped
    (`Ssd1677Driver.cpp:448-458`); cleanup is at `:638-647`.
  - A FAST and a HALF both log through `EpdBus.cpp:293`, as `" 8179_DRF"` (`Uc8179Driver.cpp:364`).
- **Night mode (A11).** The inversion is resolved per render (`ActivityManager.cpp:58`).
  `appliesNightMode` is true only for `ReaderActivity.h:60` and `PassageSelectActivity.h:63`.
- **Log anchors.**
  - `PUBS Memory on entry` (`PublicationsActivity.cpp:57`) comes after any popup and before the
    first render.
  - `READER Book loaded in` (`ReaderActivity.cpp:60`).
  - `MENU heap closed` (`EpubReaderMenuActivity.cpp:181,214`).
  - The baseline shows `8179_PON` only after boot or wake. The fading fix that would power the panel
    off after each refresh is hidden on X4 Pro (`SettingsActivity.cpp:55`).
- **Scope matches the d1 ruling.** The ruling reads: "Keep Meetings and the build-time HALFs
  (Starting, Ready, Failed), each with a one-line reason". Meetings already carries one
  (`MeetingsActivity.cpp:67`).
- **No drift on `origin/main`.** `git ls-remote origin main` gives `478cf1ed`, and none of the touched
  files differ from it.

## Findings

### MAJOR-1: A8's "the comment at `UiListActivity.h:85-89` stays accurate" is false after this change

- **Claim.**
  - A8: "`UiListActivity.{h,cpp}` are untouched: the comment at `:85-89` stays accurate for Meetings,
    the grid and the Publications cover change."
  - A1: "No comment is left explaining the absence. Every other Home entry … also enters FAST without
    one."
- **Problem.** The base-class comment does more than describe the flag: it states the rule for when to
  set it. That rule prescribes exactly the HALF that A1 deletes.
  - Publications' first frame replaces Home's cover tiles with a list and a different masthead.
    That is "a paint [that] replaces cover art with something else".
  - Meetings, entered from the same launcher tiles, keeps "Entered from the launcher's cover tiles:
    one clean paint over them".
  - After the merge, the base class says one thing, Meetings agrees with it, and Publications does
    the opposite with no comment.
  - In this repo agents are told to "mirror the nearest existing example". The likeliest next edit
    is someone "restoring" the missing HALF and losing the ~950 ms.
  - CLAUDE.md says comments are written for the merged state. This one would not be true of it.
  - A1 justifies leaving no comment with a precedent list that is partly wrong (MINOR-1).
- **Evidence.**
  - `UiListActivity.h:86-89`: "Set it where a paint replaces cover art with something else, which a
    differential refresh would leave ghosted."
  - `MeetingsActivity.cpp:67-68`.
  - `PublicationsActivity.cpp:117-118` and `BibleNavigationActivity.cpp:368-369` make the same
    "a differential paint would leave the old art ghosted" assertion.
  - All four come from `06e52c84` (#219, `git blame`).
- **Fix.** Make the base-class comment describe the mechanism only, and leave each reason to its call
  site. For example: "The next paint is a HALF refresh instead of FAST, then it resets. Each setter
  says why. Mirrors BibleSearchActivity's fullRefreshPending."
  - Every remaining setter already carries its reason: `MeetingsActivity.cpp:67`,
    `BibleNavigationActivity.cpp:368-369` and `PublicationsActivity.cpp:117-118`.
  - Add `UiListActivity.h` (comment only) to the Architecture table, and drop it from A8's
    "untouched" list.
  - `src/activities/UiListActivity` is already in the task's `files` (`hpipe show --task t1`).
  - A12 and A13 are unaffected: no behaviour or compile-relevant change.

### MINOR-1: The precedent "every other Home entry, the reader page included, enters FAST" is wrong for Recents and the Verse tile

- **Claim.**
  - Spec A1: "Every other Home entry (reader page, Go to grid, Tags, Settings) also enters FAST
    without one."
  - Research, "FAST over Home's art already ships elsewhere": "Only Meetings, Publications and Bible
    search take an entry HALF."
  - The same wording appeared in the d1 question.
- **Problem.** Only Continue opens the reader with a fast first page.
  - Recent0–2 and Verse route with `allowFastInitialRefresh = false`, so `pagesUntilFullRefresh`
    stays 0. Their first page is a HALF.
  - So does any book whose cache is missing, through `disableFastInitialRefresh()`.
  - This does not undercut d1. Continue (baseline: 551 + 300 ms), Go to (551 ms), Tags and Settings
    are real FAST-over-Home precedents, and the owner's device check is the arbiter.
- **Evidence.**
  - `HomeTargets.h:52-54`: Continue passes `/*fast=*/true`.
  - `HomeTargets.h:57-64`: Recent and Verse use `reader(Kind::OpenAt)`, whose `fast` defaults to false
    at `:45`.
  - `LauncherActivity.cpp:412` passes `route.allowFastInitialRefresh`.
  - `ReaderActivity.h:15` initialises `pagesUntilFullRefresh = 0`; only the fast path raises it
    (`ReaderActivity.cpp:19-22`).
  - `ReaderUtils.h:166` chooses `<= 1 → HALF_REFRESH`.
  - `EpubReaderActivity.cpp:173` calls `disableFastInitialRefresh()` for an uncached book.
- **Fix.** In A1, replace "reader page" with "Continue's reader page". In the Risks paragraph that
  leans on the same precedent, name Continue and Go to. The research note can keep its history, but
  the spec must not repeat the overstatement.

### MINOR-2: The source invariant's "exactly these remain" list misses a line its own grep matches

- **Claim.** Testing strategy step 1: after the edit, "Exactly these remain" are six sites: Meetings,
  the grid, the Publications cover change, and `BibleSearchActivity.cpp` `:250`, `:373` and `:444`.
- **Problem.** The pattern `fullRefreshPending.store(true)` also matches `enterState`'s own store.
  That line stays, so the grep returns seven lines, not six. An implementer or plan reviewer who runs
  the check as written sees an "unexpected" line.
  - "Before: it lists `PublicationsActivity.cpp:56`" undersells it too: the grep lists nine lines.
- **Evidence.** Run in this worktree:
  - The grep prints `src/activities/reader/BibleSearchActivity.cpp:137:  if (fullRefresh) fullRefreshPending.store(true);`.
  - The full current output is nine lines: `BibleNavigationActivity.cpp:370`,
    `BibleSearchActivity.cpp:114,137,250,373,444`, `PublicationsActivity.cpp:56,119` and
    `MeetingsActivity.cpp:68`.
- **Fix.**
  - Add "`BibleSearchActivity.cpp:137` (`enterState`'s store, which stays)" to the after-list.
  - State the before-output as those nine lines.

### MINOR-3: Two of the planned reason lines would be wrong or would narrate the change

- **Claim.**
  - A4's reason line: "… and is the build flow's first clean paint now that entry is FAST."
  - A6's reason line: "same as A5", that is, it "replaces a view that only FAST repaints have
    touched since Starting, up to ~100 of them".
- **Problem.**
  - "now that entry is FAST" is before/after narration. CLAUDE.md ("Comments") forbids that, and
    the spec's own A7 rules it out for the `render()` comment.
  - A6's reason is false for two of `showFailure`'s four callers. `beginBuild`'s failures at `:260`
    (OOM) and `:272` (`begin()` failed) replace the Starting frame, which was itself a HALF, with no
    FAST in between. The first `publishProgress` runs only after `begin()` succeeds.
- **Evidence.**
  - `BibleSearchActivity.cpp:255-273`: the `beginBuild` failure paths.
  - `:284`: the first `publishProgress(/*force=*/true)`, after `begin()` at `:263`.
  - Starting is entered with `fullRefresh=true` at `:444`.
  - CLAUDE.md, "Comments": "write them for the merged state … Remove before/after narration".
- **Fix.**
  - A4: "The build's first clean paint: its progress view stays up for the whole build."
  - A6: "Closes the build's progress view with a clean paint, as Ready does." Drop the FAST-count
    claim.

### MINOR-4: The shared-file sibling R11 is not named, and the end-to-end after-figure depends on it

- **Claim.** Goal 5: "Search → keyboard goes from ~2.7 s to ~1.75 s (4.6 s to ~3.65 s on the first
  search after boot)." The spec says nothing about other tasks editing `BibleSearchActivity.cpp`.
- **Problem.** `perf/r11-search-index-resident` (berean-o-6) edits the same file and targets the
  index-open time, which is part of that end-to-end figure.
  - The hunks do not collide. R11 changes `onExit`, the Opening case of `runBusyWork` and
    `openIndex`, and leaves `onEnter` and `enterState` to R20.
  - But if R11 merges first, the PR's quoted "~1.75 s" is wrong.
  - The brief requires the PR to give "the expected after".
- **Evidence.** In `git show origin/perf/r11-search-index-resident:docs/superpowers/specs/2026-10-11-berean-o-6-design.md`:
  - lines 63-64: "R20 (t1) owns `enterState(State::Opening, /*fullRefresh=*/true)` … this task does
    not touch it";
  - lines 330-345: its three edits;
  - lines 49-51: its frame → keyboard targets.
- **Fix.**
  - Add a Risks bullet that names berean-o-6 as sharing `BibleSearchActivity.cpp`, with disjoint
    hunks.
  - Make the R20 claim the entry waveform: `8179_DRF` from 1,506 to ~560 ms.
  - Have the PR compute the end-to-end search → keyboard figure against the `main` it is rebased on.

No BLOCKERs. The MAJOR is a comment fix inside the task's own files. It reverses no decision, changes
no scope and needs no ruling from the owner. Counts: 0 BLOCKER, 1 MAJOR, 4 MINOR, all fixable inline.

VERDICT: CLEAR
