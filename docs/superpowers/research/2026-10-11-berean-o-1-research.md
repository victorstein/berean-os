# berean-o-1 (R20) — research: FAST, not HALF, entering Bible search and Publications

Branch `perf/r20-fast-entry-refresh`, based on `1ba1cd34`. Every claim below cites a line read in
this worktree or a command run in it. Device figures come from the baseline
(`git show docs/252-responsiveness-baseline:docs/superpowers/notes/responsiveness-baseline.md`), measured on the
owner's UC8179 unit. None were re-measured here.

## Who owns the behaviour

| Concern | Owner |
|---|---|
| Bible search's per-frame HALF/FAST choice | `src/activities/reader/BibleSearchActivity.cpp:1011-1035` (`render`), flag at `BibleSearchActivity.h:116` |
| The HALF requests in Bible search | `BibleSearchActivity.cpp:114` (Opening), `:250` (Failed), `:373` (Ready), `:444` (Starting); `enterState` stores the flag at `:137` |
| Publications' entry HALF | `src/activities/catalog/PublicationsActivity.cpp:55-56` (`onEnter`) |
| The shared list flag and where it is used | `src/activities/UiListActivity.h:85-89`; `UiListActivity.cpp:169` (`render`) |
| Other users of that flag, which must not change | `MeetingsActivity.cpp:67-68` (entry), `BibleNavigationActivity.cpp:370` (Chapter/Verse → Book), `PublicationsActivity.cpp:117-119` (cover changed after a delete or download) |
| What a FAST or HALF does on each panel | `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.cpp`, `Ssd1677Driver.cpp` (submodule at `67f7e01`) |

## How each screen is reached

**Publications** has one entry: Home's Publications tile → `LauncherActivity::openPublications`
(`LauncherActivity.cpp:432-436`). `grep -rn PublicationsActivity src/` finds no other constructor
call. The brief says both screens are also entered from the reader. For Publications that is not
true: the panel before its first frame is always Home.

**Bible search** has one constructor call, `EpubReaderActivity::openBibleSearch`
(`EpubReaderActivity.cpp:976-988`), which is reached two ways:

1. **From the reader menu.** `MenuAction::SEARCH_BIBLE` → `openBibleSearch(CancelTo::Menu)` (`:810-811`). The panel
   shows the menu sheet. On the usual path that is the sheet drawn over the page with a FAST
   (`EpubReaderMenuActivity.cpp:536-542`). It is the cleared sheet with a HALF only when no page
   snapshot exists (owner decision d1, `docs/superpowers/specs/2026-09-30-issue-200-design.md:44-50`).
2. **From Home's Search tile.** `HomeTargets::route` maps `Target::Search` to the reader with
   `Kind::Search` (`HomeTargets.h:69-70`). `ReaderActivity::onEnter` calls `onBookLoaded()` before it
   requests its first render (`ReaderActivity.cpp:58-66`). `onBookLoaded` takes `Route::Search` →
   `openBibleSearch(CancelTo::Page)` (`EpubReaderActivity.cpp:1066-1067`). The push runs in the same
   `ActivityManager::loop` pass (`ActivityManager.cpp:139-160`) before the deferred render is signalled
   (`:167-172`, `:285-295`), so the reader never paints a page. **The panel still shows Home.**

So the predecessor of Bible search's first frame is either the reader menu or Home. It is never a
bare anti-aliased page.

## Current control flow

### Bible search

- `onEnter` → `enterState(State::Opening, /*fullRefresh=*/true)` (`:114`) → `fullRefreshPending = true`
  (`:137`) → `requestUpdate`.
- `render` takes the flag with `exchange(false)` (`:1016`), draws the screen, then calls
  `displayBuffer(full ? HALF_REFRESH : FAST_REFRESH)` (`:1032`), which **blocks**
  (`GfxRenderer.cpp:1724-1728`). After that it publishes `shownState` (`:1033`).
- `loop` runs a busy state's SD work only `if (shownState.load() == state)` (`:178`). For Opening
  that work is `bookNames.load` + `openIndex` (`:189-193`). **The index open (≈500 ms, 2.2 s on the
  first search after boot) cannot start until the waveform ends.** Every millisecond the entry
  refresh saves comes straight off the time to the keyboard.
- The Opening frame is the header plus a centred "Loading" line (`buildScreen`, `:770-772`), which
  is mostly white. `openIndex` then pushes `KeyboardEntryActivity` (`:473-492`), which paints with a
  default FAST (`KeyboardEntryActivity.cpp:954`; the default is FAST at `HalDisplay.h:41`).

Audit of the other HALF requests in this activity:

| Site | State | What the panel shows before it | How often |
|---|---|---|---|
| `:114` | Opening | Home, or the reader menu | **every search entry** |
| `:444` | Starting (Prompt → Prepare) | the Prompt dialog, painted FAST (`showPrompt`, `:234`) | once per index build or resume |
| `:373` | Ready (build finished) | the Finishing frame, after up to ~100 FAST progress repaints (`publishProgress`, `:315-341`; cadence in the #94 commit's device checklist) | once per index build |
| `:250` | Failed | a progress frame | on a build failure |

Only `:114` is on an entry path. The other three are inside the one-time index build.

### Publications

`onEnter` (`PublicationsActivity.cpp:52-61`) first runs `refresh()`. That can paint a FAST loading
popup over Home when a cover thumbnail has to be generated (`BaseTheme.cpp:499`, via `drawPopup`).
It then sets `halfRefreshPending`. `UiListActivity::render` uses the flag once at `:169`.
`UiListActivity::onEnter` (`UiListActivity.cpp:20-27`) does not touch it. Inside `refresh()`, the
cover-change HALF at `:119` cannot fire on entry, because `mastheadCover_` starts empty and the
guard is `!mastheadCover_.empty()`.

## Why each HALF was added

- **Bible search, `5c385845` (#94, 2026-09-24).** The spec says: "A HALF_REFRESH (the codebase's
  clean refresh) on entry, on the progress view, on "Listo" and on a failure"
  (`docs/superpowers/specs/2026-09-24-bible-verse-search-design.md:81-84`). It gives no reason
  about ghosting and cites no device observation. At the time, the entry point was a button band
  under the book grid (the #94 commit body, "Entry point"), not the reader menu or Home.
- **Publications (and Meetings), `06e52c84` (#219, 2026-09-29).** The spec's decision A8 says:
  "both are only entered from the launcher …, the art-heavy screen the issue allows one HALF from"
  (`docs/superpowers/specs/2026-09-30-issue-204-design.md:98-112`). Issue #204 itself says "Entering
  from another art-heavy screen **may** use one HALF". Its rule for the whole overhaul is "Screens stay
  on the fast differential refresh; use a HALF refresh only on a clean entry"
  (`gh issue view 204 --repo victorstein/berean-os`). The HALF was allowed, not required, and no
  device ghost is recorded behind it. The #204 PR review only checks that the HALF exists
  (`docs/superpowers/reviews/issue-204-pr-review-0.md:26-27`).

Neither HALF was added to fix a ghost someone observed.

## What a FAST does over each predecessor

**Home is pure 1-bit.** Grepping `src/` and `lib/GfxRenderer` for every grayscale entry point
(`displayGrayBuffer`, `copyGrayscale*`, `storeBwBuffer`, `GRAYSCALE_MSB`) finds only the reader, the
sleep screen, the screenshot utility and the renderer itself. The launcher and Publications run no gray
pass, and issue #204 states "art is pre-dithered 1-bit". A FAST from Home therefore goes from one
black-and-white frame to another.

**UC8179 (the owner's unit).**
- A FAST is a whole-panel DU partial: `PTIN` (`Uc8179Driver.cpp:345`) with the OLD plane (DTM1)
  holding the previous frame, which `displayFinish` resyncs after every refresh (`:374`). Every
  pixel that changes is driven in its own direction, and unchanged pixels are not driven
  (`:284-290`).
- A HALF loads DTM1 with the complement of the target (`:292`, `:301`), so every pixel is driven.
  It runs the OTP GC waveform: 1,506 ms in the baseline against 551–566 ms for a FAST.
- **After an anti-aliased page**, `displayGray` sets `_redriveAfterGray` (`:577`). The next FAST is
  intercepted (`:174-176`) and becomes `transitionGrayscaleBase`, the stock non-flashing
  `XTF_PRE_BW_MID` transition (`:183-212`), which is the 674 ms `8179_gray_pre_DRF`. The driver comment
  says "an explicit Half remains the strong purge" (`:572-575`).
- On the menu path the menu's own first FAST has already taken that transition, so the search
  FAST is a plain DU over a black-and-white panel. On the Home path no gray pass ever ran.
- **Log line:** a FAST and a HALF both log `Wait complete:  8179_DRF (N ms)` (`:364`, printed at
  `EpdBus.cpp:293`). The facade logs no mode (`FreeInkDisplay.cpp:568-598`). **Only the duration tells
  the two apart**: ~560 ms for FAST, ~1,506 ms for HALF.

**SSD1677.**
- Every anti-aliased page ends in `restoreBwBuffer` → `display.cleanupGrayscaleBuffers(frameBuffer)`
  (`GfxRenderer.cpp:2352`). That rewrites RED with the black-and-white base and clears
  `_inGrayscaleMode` (`Ssd1677Driver.cpp:638-647`). The next FAST is a plain differential update
  against that base.
- Only when the cleanup was skipped does the driver promote FAST to HALF itself (`:448-458`).
- There is no revert waveform ("stock parity", `:448-453`). AA fringe charge on pixels that are
  white in the base is not driven by a FAST, and is cleared by the next periodic HALF.
- That is already true of every SSD1677 transition out of the reader: page turns, reader → menu
  (FAST, `EpubReaderMenuActivity.cpp:540-541`), menu → Go to grid (the grid never sets the flag on
  entry; `:370` only fires on → Book), and reader → Home (`LauncherActivity.cpp:364-370`).
- The first paint after boot or wake is promoted separately (`:429-445`). That does not apply here.

**FAST over Home's art already ships elsewhere.** Home → reader page, Home → Go to grid (which has
its own masthead art), Home → Tags (`Route::Highlights`) and Home → Settings all paint their first
frame with FAST. Only Meetings, Publications and Bible search take an entry HALF. (`grep -rn
"HALF_REFRESH\|halfRefreshPending.store(true)\|fullRefreshPending.store(true)" src/` lists every
HALF outside the reader's page path; the remainder are sleep, wake, BMP viewer and screenshot.) The
baseline measured Home → Go to at one 551 ms FAST to the picker. **Whether any of those paths leaves
a visible ghost has not been recorded.** Code cannot settle it. It needs eyes on the panel.

## Nearest existing examples

- **A HALF only on a clean entry, decided by a pure helper:** `src/activities/launcher/LauncherRefresh.h:15-17`
  (`launcherNeedsCleanPaint`), host-tested in `test/launcher_refresh/LauncherRefreshTest.cpp`, with
  `static_assert`s plus three TESTs. The suite is registered at `test/CMakeLists.txt:125`.
- **A HALF only when the predecessor can't be diffed:** the reader menu's `cleanEntry`
  (`EpubReaderMenuActivity.cpp:536-542`). It takes a FAST over the saved page and a HALF on a
  cleared screen. This is owner decision d1 (#217, `a22af1d1`).
- **The minimal change:** the flag idiom itself. Removing `halfRefreshPending.store(true)` at
  `PublicationsActivity.cpp:56` and passing `fullRefresh=false` at `BibleSearchActivity.cpp:114`
  changes nothing else. Meetings keeps its own `store(true)` (`MeetingsActivity.cpp:68`), and the
  base class still defaults to false (`UiListActivity.h:89`).
- A conditional rule for Bible search has a natural key. The reader already knows the origin as
  `CancelTo::Page` (from Home, the panel holds Home) versus `CancelTo::Menu` (the panel holds the
  menu) at `EpubReaderActivity.cpp:976`. Publications has only one origin, so "HALF only after
  cover art" means it keeps its HALF.

## Tool and package versions

- PlatformIO Core 6.1.19 (`~/.platformio/penv/bin/pio --version`; `pio` is not on `PATH`)
- Platform: pioarduino `platform-espressif32` 55.03.37 (`platformio.ini:15`)
- `freeink-sdk` at `67f7e01` ("fix: confirm X4 Pro VBUS detect on GPIO21", `git submodule status`)
- clang-format 21, required by `bin/bootstrap` (`REQUIRED_CLANG_FORMAT_MAJOR=21`)
- This worktree has no `.pio/` or `.venv/` yet, so `./bin/bootstrap` runs before the first build or
  format.

## Expected effect

- Per entry, on UC8179, a FAST saves about 1,506 − 560 ≈ 950 ms of blocking waveform. On Bible
  search that comes straight off the time to the keyboard, because the index open is gated on the
  frame (`:178`).
- Baseline before: search → keyboard ~2.7 s (4.6 s on the first search after boot), and Publications
  settles at 1,940 ms.
- Expected after, if both entries go FAST: ~1.75 s (~3.65 s), and ~990 ms.
- SSD1677 is unmeasured. `HalDisplay.h:16`'s "1720ms" for HALF is an upstream comment, not a
  measurement.

## Scope and tier

- The change stays in `src/activities` (the ui surface). It changes no on-disk format, no SDK code
  and no shared contract.
- A conditional rule for search would add a constructor argument to `BibleSearchActivity`, whose
  only caller is in the same surface.
- The standard tier holds.

## Decision d1 (owner, 2026-10-11): FAST from every origin

The options were:

- **(a)** FAST on both entries from every predecessor;
- **(b)** FAST for search from the reader menu, with a HALF kept for the two Home-origin entries
  (Publications, and search via the Search tile);
- **(c)** no change.

The owner chose **(a)**. What that means for the change:

- Drop `halfRefreshPending.store(true)` at `PublicationsActivity.cpp:56`, and its comment at `:55`.
- Pass `fullRefresh=false` at `BibleSearchActivity.cpp:114`.
- Keep the Meetings entry HALF (`MeetingsActivity.cpp:68`) and the three build-time HALFs: Starting
  (`BibleSearchActivity.cpp:444`), Ready (`:373`) and Failed (`:250`). Each gets a one-line reason in
  the code.
- Leave every other refresh choice alone. That includes `BibleNavigationActivity.cpp:370` and the
  Publications cover-change HALF (`PublicationsActivity.cpp:119`).
- The PR lists the device checks: open each screen from Home, and from an anti-aliased reader page
  via the menu, and look for a ghost of the cover or the page.
- If a ghost shows on the device, the fallback is (b): a HALF only on the Home-origin entries, with
  the origin passed to `BibleSearchActivity` as `CancelTo::Page` versus `CancelTo::Menu`
  (`EpubReaderActivity.cpp:976`).
