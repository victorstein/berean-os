# berean-o-1 (R20) design: FAST, not HALF, entering Bible search and Publications

Research: `docs/superpowers/research/2026-10-11-berean-o-1-research.md`. Base `1ba1cd34`. The files
this touches are unchanged on `origin/main` as of `478cf1ed` (`git diff --stat HEAD...origin/main`
over them is empty). Owner decision: **d1 = (a)**, recorded in the research note's last section.

Modelled on: the per-frame flag idiom both screens already use, unchanged —
`BibleSearchActivity`'s `fullRefreshPending` (`BibleSearchActivity.h:116`, `.cpp:137,1016,1032`) and
`UiListActivity`'s `halfRefreshPending` (`UiListActivity.h:85-89`, `.cpp:169`). The rule "FAST
unless the entry has something a differential refresh cannot clear" mirrors
`launcherNeedsCleanPaint` (`src/activities/launcher/LauncherRefresh.h:15-17`) and the reader menu's
`cleanEntry` (`EpubReaderMenuActivity.cpp:537-541`, owner decision d1 of #200).

## Problem

Both screens paint their first frame with a HALF. On the owner's UC8179 that is 1,506 ms, against
551–566 ms for a FAST (baseline, "Panel waveforms on this unit").

- **Bible search:**
  - `onEnter` calls `enterState(State::Opening, /*fullRefresh=*/true)` (`BibleSearchActivity.cpp:114`).
  - The frame is displayed by a blocking `displayBuffer(HALF_REFRESH)` (`:1032`, `GfxRenderer.cpp:1724-1728`).
  - The index open waits for `shownState` to equal `state` (`:178`, published at `:1033`), so the
    whole HALF sits on the critical path to the keyboard.
  - Baseline search → keyboard: ~2.7 s, or 4.6 s on the first search after boot.
- **Publications:**
  - `onEnter` sets `halfRefreshPending` (`PublicationsActivity.cpp:55-56`), and
    `UiListActivity::render` uses it (`UiListActivity.cpp:169`).
  - Baseline entry settles at 1,940 ms.

Neither HALF was added for a ghost anyone observed. #94 chose HALF as "the codebase's clean refresh"
(`docs/superpowers/specs/2026-09-24-bible-verse-search-design.md:81-84`). #204 allowed one ("may")
on entry from an art-heavy screen (`docs/superpowers/specs/2026-09-30-issue-204-design.md:98-112`).

## Goal

1. Bible search's first frame is a FAST from both origins: the reader menu, and Home's Search tile.
2. Publications' first frame is a FAST. Its only origin is Home.
3. Every HALF left in `BibleSearchActivity` carries a one-line reason at its call site.
4. No other screen's refresh choice changes.
5. Expected on UC8179: each entry's first `Wait complete:  8179_DRF` drops from ~1,506 ms to ~560 ms.
   That waveform is the figure R20 claims.
   - Against the `1ba1cd34` baseline, end to end: search → keyboard goes from ~2.7 s to ~1.75 s
     (4.6 s to ~3.65 s on the first search after boot), and Publications settles at ~990 ms instead
     of 1,940 ms.
   - These are estimates (baseline minus ~950 ms). Only the owner's device run confirms them.
   - The search → keyboard figure also depends on the index-open time, which berean-o-6 (R11) changes
     (Risks). The PR recomputes the end-to-end figure against the `main` it is rebased on.

## Non-goals

- **Meetings' entry HALF** (`MeetingsActivity.cpp:67-68`). R20 names only search and Publications,
  and the brief forbids other screens' changes. The same reasoning likely applies there, so it is
  reported with `hpipe discover`, not changed here.
- Any SDK or panel-driver change: UC8179 post-AA interception, SSD1677 FAST→HALF promotion,
  waveform tables.
- The fallback (b), a HALF only for Home-origin entries. It is documented (A3) but not built unless
  the device shows a ghost.
- The other HALFs in `BibleNavigationActivity.cpp:370`, `PublicationsActivity.cpp:117-119`, the
  launcher, the reader menu, the reader and the sleep screen.
- New logging, new timing instrumentation, or a refresh-mode log line (A10).

## Assumptions (each one a decision for the review to attack)

- **A1 — Publications' entry is a FAST, from its only origin.**
  - Delete `halfRefreshPending.store(true)` and its comment (`PublicationsActivity.cpp:55-56`).
    Nothing replaces them: `UiListActivity` defaults the flag to false (`UiListActivity.h:89`), and
    `UiListActivity::onEnter` does not set it (`UiListActivity.cpp:20-27`).
  - Its only constructor call is `LauncherActivity::openPublications` (`LauncherActivity.cpp:432-436`),
    so the predecessor is always Home, or Home with the loading popup FAST-painted over it
    (`refresh()` → `GUI.drawPopup`, `BaseTheme.cpp:499`).
  - Home runs no gray pass. Grepping `src/` and `lib/GfxRenderer` for `displayGrayBuffer`,
    `copyGrayscale*`, `storeBwBuffer` and `GRAYSCALE_MSB` finds only the reader, sleep, screenshot
    and renderer code. So this is a 1-bit → 1-bit differential update.
  - No comment is left explaining the absence. Continue's reader page, the Go to grid, Tags and
    Settings also enter FAST over Home without one. The reasoning lives in the commit and PR.
    - Recents and the Verse tile are not precedents: they open the reader with
      `allowFastInitialRefresh = false` (`HomeTargets.h:45,57-64`), so their first page is a HALF
      (`ReaderActivity.h:15`, `ReaderUtils.h:166`).
  - The base-class comment is changed so it no longer prescribes this HALF (A8).
- **A2 — Bible search's Opening frame is a FAST, from both origins.**
  - `:114` becomes `enterState(State::Opening);`. The default is `fullRefresh = false`
    (`BibleSearchActivity.h:177`).
  - The comment above it ("… so both wait for this frame", `:112-113`) stays true and stays.
  - **Origin 1, the reader menu** (`EpubReaderActivity.cpp:810-811`). The panel holds the menu sheet:
    - Over-page, it is FAST (`EpubReaderMenuActivity.cpp:537-541`).
    - Cleared, it is HALF (d1 of #200).
    - On UC8179, any AA residue was already transitioned by the menu's own first FAST, which the
      driver routes through `transitionGrayscaleBase` (`Uc8179Driver.cpp:174-176`, `:183-212`).
    - On SSD1677, the reader's AA page ended in `restoreBwBuffer` → `cleanupGrayscaleBuffers`
      (`GfxRenderer.cpp:2352`, `Ssd1677Driver.cpp:638-647`). The search FAST is then the same plain
      differential update that menu → Go to already uses.
  - **Origin 2, Home's Search tile** (`HomeTargets.h:69-70` → `EpubReaderActivity.cpp:1066-1067`).
    The reader never paints, because `onBookLoaded` pushes search before the deferred render
    (`ReaderActivity.cpp:58-66`, `ActivityManager.cpp:139-160,167-172`). So the panel holds Home:
    1-bit → 1-bit, as in A1.
- **A3 — One rule for every origin (d1 = a).** There is no constructor flag and no
  origin-conditional HALF.
  - If the owner's device check shows a ghost, the fallback is (b): a HALF only for Home-origin
    entries.
  - For search, that means passing whether `cancelTo == CancelTo::Page` into the constructor
    (`EpubReaderActivity.cpp:976-980`, its only caller). Publications would simply keep its HALF.
  - The fallback is a separate change and is not built here.
- **A4 — Starting keeps its HALF** (`BibleSearchActivity.cpp:444`). Reason line: "The build's first
  clean paint: its progress view stays up for the whole build."

  - Starting is reached only from Prompt → Prepare (`:433-446`), once per build or resume.
  - It is not an entry path, so the brief's "entry logs a FAST" does not cover it.
- **A5 — Ready keeps its HALF** (`:373`). Reason line: it replaces a view that only FAST repaints have
  touched since Starting, up to ~100 of them (`publishProgress`, `:315-341`). It fires once per build.
- **A6 — Failed keeps its HALF** (`showFailure`, `:250`). Reason line: "Closes the build's progress
  view with a clean paint, as Ready does."
  - No FAST-count claim: `beginBuild`'s failures (`:260`, `:272`) replace the Starting frame, itself
    a HALF, before any `publishProgress` (`:284`).
  - The other callers are `stepBuild` (`:304`) and `finishBuild` (`:363`). All four are rare.
- **A7 — The `render()` header comment is rewritten for the new rule** (`:1012-1015`). Today it reads
  "a full refresh on entering the screen, the progress view and completion; fast ones in between",
  which would become false. The new text says HALF on the build's progress view, its completion and
  a failure, and FAST otherwise, entry included. It is written for the merged state, with no
  before/after narration (CLAUDE.md, "Comments").
- **A8 — `UiListActivity.h`'s flag comment describes the mechanism only; nothing else changes.**
  - Today `:86-89` states a rule: "Set it where a paint replaces cover art with something else, which
    a differential refresh would leave ghosted."
  - Publications' entry, now FAST, is such a paint. Left as it is, the comment would prescribe the
    HALF that A1 deletes.
  - It becomes: "The next paint is a HALF refresh instead of FAST, then it resets. Each setter says
    why. Mirrors BibleSearchActivity's fullRefreshPending."
  - Every remaining setter already carries its own reason: `MeetingsActivity.cpp:67`,
    `BibleNavigationActivity.cpp:368-369` and `PublicationsActivity.cpp:117-118`.
  - The edit is to a comment only. `UiListActivity.cpp` and the flag itself are untouched.
  - Also untouched: `MeetingsActivity.cpp:67-68`, `BibleNavigationActivity.cpp:370`,
    `PublicationsActivity.cpp:117-119`, `LauncherActivity.cpp:364-370`,
    `EpubReaderMenuActivity.cpp:537-541`, `BibleSearchActivity.h`, and every `freeink-sdk` file.
- **A9 — The driver behaviour this relies on is used as it is.**
  - UC8179 FAST is a whole-panel DU partial against the previous frame (`Uc8179Driver.cpp:292-293`,
    `:345`, `:374`).
  - UC8179 HALF loads DTM1 with the target's complement and runs the OTP GC waveform (`:301`, `:326`).
  - The SSD1677 driver promotes FAST to HALF itself when gray cleanup was skipped (`Ssd1677Driver.cpp:448-458`).
  - The first paint after boot or wake is promoted separately (`:429-445`). That cannot be the
    search or Publications entry, because Home or the reader paints first.
- **A10 — There is no new log line. The proof is the existing waveform wait.**
  - A FAST and a HALF both print `Wait complete:  8179_DRF (N ms)` on UC8179 (`Uc8179Driver.cpp:364`,
    `EpdBus.cpp:293`), and `Wait complete: refresh (N ms)` on SSD1677 (`Ssd1677Driver.cpp:314,366,412`).
    Only the duration differs.
  - The entry frame's line is the first `Wait complete` after an anchor that already exists:
    - search via the menu: after `MENU heap closed: …` (`EpubReaderMenuActivity.cpp:181,214`);
    - search via Home: after `READER Book loaded in N ms` (`ReaderActivity.cpp:60`);
    - Publications: after `PUBS Memory on entry: …` (`PublicationsActivity.cpp:57`), which is logged
      in `onEnter` after any loading popup and before the first render.
  - The baseline identified the same frames the same way. A launcher-style `Paint: mode=` line
    (`LauncherActivity.cpp:366-367`) was considered. It would add a log to every search frame, or
    to every list paint if put in `UiListActivity`, for no decision the duration doesn't already
    settle.
- **A11 — Night mode cannot promote these entries.**
  - The facade turns FAST into HALF after an inversion change (`FreeInkDisplay.cpp:573-575`), and
    the inversion is resolved per render from `appliesNightMode` (`ActivityManager.cpp:58`).
  - Only `ReaderActivity` (`ReaderActivity.h:60`) and `PassageSelectActivity` (`.h:63`) apply it. The
    base class says false (`Activity.h:50`).
  - Neither predecessor (Home, the menu) nor the two screens apply it, so no inversion flip happens
    at these entries. With night mode on, reader → menu flips it and the menu takes its own HALF
    (#200 d1). That is unchanged.
- **A12 — No resource, format or shared-file impact.** There is no allocation, store, on-disk format,
  i18n key, `test/CMakeLists.txt` line or `src/main.cpp` line, so the PR has no "For the
  orchestrator" lines.
- **A13 — No new host test (see Testing strategy).** The change removes two requests for a HALF. No
  host suite compiles either activity (`grep -rln "BibleSearchActivity\|PublicationsActivity" test/`
  finds only comments, in `RecentBooksDocTest.cpp:205` and `ReferenceConsistencyTest.cpp:39`), and
  the stub `HalDisplay` has no recording seam (`test/stubs/HalDisplay.h:20`).
  - Extracting a `LauncherRefresh.h`-style helper would mean either moving `BibleSearchActivity`'s
    private `State` enum out, or adding a parallel enum. There are also no inputs to decide on:
    `launcherNeedsCleanPaint` takes two runtime flags, but here each frame's choice is fixed per
    state.
  - Such a test would pin a lookup table the call sites must remember to consult. It could not catch
    the real risk, a call site that still passes `true`. The source invariant below catches that.

## Architecture

There are no new files and no new types. The edits:

| File | Change |
|---|---|
| `src/activities/catalog/PublicationsActivity.cpp` | Delete `:55-56` (comment + `halfRefreshPending.store(true)`). |
| `src/activities/UiListActivity.h` | Comment only at `:86-89`: the mechanism, not a rule (A8). |
| `src/activities/reader/BibleSearchActivity.cpp` | `:114` → `enterState(State::Opening);`. One-line reason comments above `:250`, `:373` and `:444` (A4–A6); `:373` keeps its existing line about Ready's frame. Rewrite the `render()` comment at `:1012-1015` (A7). |

Nothing else moves. `BibleSearchActivity::enterState` keeps its `fullRefresh` parameter: Starting
and Ready still pass `true`.

## Data and control flow

**Search from the reader menu:**

1. Menu tap → `MENU heap closed` (menu `onExit`).
2. Reader result handler → `openBibleSearch(CancelTo::Menu)` → push.
3. `onEnter` → `enterState(Opening)`, with `fullRefreshPending` left false.
4. `render` → `displayBuffer(FAST_REFRESH)`, which blocks ~560 ms on UC8179 → `shownState = Opening`.
5. `loop` → `runBusyWork` → `bookNames.load` + `openIndex` → keyboard (FAST, `KeyboardEntryActivity.cpp:954`).

**Search from Home:**

1. Home tap → `goToReader(…, Kind::Search)` → reader `onEnter` → `loadBook` → `READER Book loaded in N ms`.
2. `onBookLoaded` → `openBibleSearch(CancelTo::Page)` → push before any reader render.
3. From here it is the same as steps 3–5 above.

**Search when no index is ready:** Opening (FAST) → Prompt (FAST, `showPrompt` sets no flag,
`:234-243`) → Prepare → Starting (**HALF**, A4) → Building (FAST repaints) → Finishing (FAST) →
Ready (**HALF**, A5) → keyboard. Failed (**HALF**, A6) can replace any build frame.

**Publications:**

1. Home tap → `openPublications` → `onEnter` → `refresh()` (with an optional FAST loading popup)
   → `PUBS Memory on entry`.
2. First `render` → `halfRefreshPending.exchange(false)` is false → FAST.
3. A later cover change still sets the flag (`:119`): HALF on that paint only.

## Error handling

There are no new failure modes. A FAST cannot fail differently from a HALF: both go through
`HalDisplay::displayBuffer`, and neither returns a status. The only risk is visual: a ghost of
Home's art or the menu sheet left on the entry frame.

- The owner's device check looks for that.
- A3's fallback answers it.
- On both panels the next HALF clears any residue: Starting/Ready on the build path, and the
  reader's periodic HALF via `pagesUntilFullRefresh` (`ReaderUtils.h:166`) after a picked verse.

## Testing strategy

TDD has no failing host test to start from (A13). The change is verified at three levels instead.

1. **Source invariant.** The plan runs this check before and after the edit:

   ```bash
   grep -rn "halfRefreshPending.store(true)\|fullRefreshPending.store(true)\|fullRefresh=\*/true" src/
   ```

   - **Before:** nine lines. `BibleNavigationActivity.cpp:370`; `BibleSearchActivity.cpp:114`, `:137`,
     `:250`, `:373` and `:444`; `PublicationsActivity.cpp:56` and `:119`; and `MeetingsActivity.cpp:68`.
   - **After:** seven lines. `PublicationsActivity.cpp:56` and `BibleSearchActivity.cpp:114` are gone.
     These remain:
     - `MeetingsActivity.cpp:68`;
     - `BibleNavigationActivity.cpp:370`;
     - the Publications cover change (`:117` after the edit);
     - in `BibleSearchActivity.cpp`, `enterState`'s own store at `:137` (which stays), `:250`
       (`showFailure`), `:373` (Ready) and `:444` (Starting).

     The `BibleSearchActivity.cpp` line numbers move with the added comment lines.
   - This pins goal 4: no other screen's choice moved.
2. **Build and format.**
   - `./bin/bootstrap` first: this worktree has no `.pio/` or `.venv/` (research, "Tool and package versions").
   - Then `pio run -e x4pro` (the build the owner flashes), then `./bin/clang-format-fix` over the
     whole tree.
   - Host tests are not rerun. No file they compile changes (A13).
3. **Device (owner only — never claimed in the PR).** Flash the `x4pro` dev build, keep serial
   logging on (`cat /dev/cu.usbmodem*`), and run:
   - **Publications from Home.** Expect the first `Wait complete` after `PUBS Memory on entry` to read
     ~560 ms, not ~1,506. Look for any ghost of Home's covers or tiles.
   - **Search from Home's Search tile.** Expect the first `Wait complete` after `READER Book loaded in`
     to read ~560 ms. Look for a ghost of Home.
   - **Search from the reader menu, AA off.** Expect the first `Wait complete` after `MENU heap closed`
     to read ~560 ms. Look for a ghost of the page or the sheet.
   - **The same with text anti-aliasing on.** Before it, the menu's open logs `8179_gray_pre_DRF` (~674 ms).
     The search entry's line is then a plain ~560 ms `8179_DRF`. Look for gray fringes from the page.
   - **Unchanged:**
     - Meetings still enters with ~1,506 ms.
     - Chapter → Book on the grid still takes one HALF.
     - Preparing an index still shows a HALF on "Preparando…" and on "Listo".
   - If any ghost shows, the PR is not merged as is. A3's fallback (b) is the next change.

## Hand-offs (shared files)

None (A12).

## Risks

- **Ghosting on a panel or unit the owner did not test.** SSD1677 units are unmeasured (baseline,
  "Not measured"). The SSD1677 path relies on the same plain differential FAST its menu → Go to and
  reader → Home transitions already take (A2). Owner decision d1 accepts this, with (b) as the fallback.
- **A sibling task shares `BibleSearchActivity.cpp`.** berean-o-6 (R11, `perf/r11-search-index-resident`)
  keeps the search index open across entries.
  - Its spec gives this task the entry refresh: "R20 (t1) owns `enterState(State::Opening,
    /*fullRefresh=*/true)` … this task does not touch it"
    (`git show origin/perf/r11-search-index-resident:docs/superpowers/specs/2026-10-11-berean-o-6-design.md`,
    lines 63-64).
  - Its edits are in `onExit`, the Opening case of `runBusyWork` and `openIndex`. Those do not
    overlap the lines here.
  - As of this spec, its branch has no code change to any file this task touches
    (`git diff --name-only 1ba1cd34...origin/perf/r11-search-index-resident` over them is empty).
  - Whichever lands second rebases. The implement phase re-reads the file first.
  - The end-to-end search → keyboard figure depends on both changes (Goal 5).
- **Line-number drift in comments elsewhere.** `src/util/RecentBooksDoc.h:29` cites
  `PublicationsActivity.cpp:56` for a 48-byte cap. That cap is already at `:76`, and deleting two
  lines moves it to `:74`. This change doesn't touch that comment. The citation was already stale
  before it.

## Amendments (after review 0)

Review: `docs/superpowers/reviews/berean-o-1-spec-review-0.md`, verdict CLEAR with 1 MAJOR and
4 MINORs. All five are accepted and applied above:

- **MAJOR-1 → A8 and Architecture.**
  - `UiListActivity.h:86-89` stated "set it where a paint replaces cover art", which would have
    prescribed the HALF A1 deletes.
  - It is now a comment-only edit that describes the mechanism. Each setter keeps its own reason.
  - A1 points at it.
- **MINOR-1 → A1.** The FAST-over-Home precedents are now Continue's reader page, Go to, Tags and
  Settings. Recents and Verse open the reader with a HALF, so they are no longer cited. The research
  note keeps its original wording as history.
- **MINOR-2 → Testing strategy, step 1.**
  - The before-output is the nine lines the grep prints.
  - The after-list now includes `enterState`'s own store at `BibleSearchActivity.cpp:137`, giving
    seven lines.
- **MINOR-3 → A4 and A6.** A4's reason line no longer narrates the change. A6 no longer claims only
  FAST repaints preceded a failure, which is false for `beginBuild`'s two failure paths.
- **MINOR-4 → Goal 5 and Risks.**
  - The claimed figure is the entry waveform, 1,506 → ~560 ms.
  - The end-to-end search → keyboard figure is recomputed in the PR against the `main` it is
    rebased on.
  - berean-o-6 (R11) is named as the sibling sharing `BibleSearchActivity.cpp`, with edits that do
    not overlap.
