Tier: standard

# Issue #203 spec review 1

Reviewed: `docs/superpowers/specs/2026-09-30-issue-203-design.md` (revision 1, at `1cc20cfb`), against
issue #203 (`gh issue view 203 --repo victorstein/berean-os`), the research note, and review 0.

## Review 0's fixes

All nine review 0 findings were applied correctly. None of the fixes is wrong or half-done.

- **MAJOR 1 (RTC memory).** A6 now states the premise correctly. `PowerManager::deepSleep` is a plain
  `esp_deep_sleep_start()` (`freeink-sdk/libs/hardware/PowerManager/src/PowerManager.cpp:93-96`), and
  the `RTC_NOINIT_ATTR` users exist (`lib/Logging/Logging.cpp:12-19`, `src/main.cpp:137-139`). A6 also
  says plainly that "later entries don't re-scan" holds only within a wake. I checked whether a silent
  restart could clear the RAM cache mid-wake. It cannot: on touch boards `silentRestart` returns
  through `finishWifiSessionWithoutRestart` (`src/main.cpp:149-167`), so leaving a Wi-Fi screen
  (`src/network/WifiSession.cpp:28`) does not reboot.
- **MAJOR 2 (spine hint).** The row does carry `"s"` (`lib/StudyStore/StudyStore/PassageDoc.cpp:279`,
  read at `:321`). `StudyStore::locate` passes it as the hint (`src/study/StudyStore.cpp:156-158`).
  Only a Verse unit is searched for (`:191-194`). `ReaderEntryIntent` has the `unit` and `spineHint`
  fields that A12 fills (`src/activities/reader/ReaderEntryIntent.h:18-19`).
- **MINOR 1–7.**
  - The strip passes `todayIsLocal ? &today : nullptr`, which matches `MeetingsActivity.cpp:156,207-210`.
  - The seed is FNV followed by splitmix32, using the existing `fnv1a32` (`StudySleepPick.h:31-40`).
  - Empty results are cached, the cache is keyed on the file size, and there are two hints.
  - The refresh flag is chosen per target. Today's tile really does pass the default `false`
    (`LauncherActivity.cpp:431`, `ActivityManager.h:86`).
  - The Meetings card uses `LibraryIcon` in place of the cover.
  - The Spanish lines and `STR_GO_TO` are handed off (`english.yaml:477`, `spanish.yaml:423`).

## Other claims checked

- **A21 arithmetic.**
  - Line heights, from the regular faces' `advanceY`:
    - `notoserif_14_regular.h:4104` = 40;
    - `notoserif_12_regular.h:3704` = 34;
    - `ubuntu_10_regular.h:3719` = 24;
    - `notosans_8_regular.h:3663` = 23.
  - `getLineHeight` returns the regular face's `advanceY` (`GfxRenderer.cpp:2141-2149`).
  - Metrics: Lyra is 5/44/8/40 (`LyraTheme.h:11-18`) and Base is 5/45/10/30 (`BaseTheme.h:134-141`).
    The insets are 9/3/3/3 (`BoardConfig.h:625-630`).
  - Recomputed: the icon row is 79, Meetings 86, the verse card 172, and Recent 143 (Lyra) or 113
    (Base). The span is 800 − 3 − 5 − 14 = 778. That gives a hero of 266 (Lyra) or 288 (Base), plates
    of 93 or 94, and art of 173 or 194. Every value in the table is correct.
- **The hero.** The hero is at x = 8 with width 464, as today's tile is (`LauncherActivity.cpp:260-262`).
  Today's tile is 464×321, so it already asks for `thumb_773` (`CoverBandGeometry.h:46-48`). The hero
  reuses that file.
- **Places and routing.** `pickRecent` skips by chapter (`PlacesDoc.cpp:78-87`), and
  `openAt(place)` passes `place.spineIndex` (`ReaderEntryIntent.h:27-33`).
- **Icons and keys.** Every icon exists (`src/components/icons/{bookmark,search,library,settings2}.h`).
  Every cited `english.yaml` line is correct. `·` is already used in UI strings
  (`english.yaml:9,34,454`).

No finding reverses a decision, changes scope, or needs the owner. All six findings below are MINOR
and can be fixed inline.

## MINOR 1: The card's fit gate pays full-text wraps at up to three rungs for rows the card can never hold, with no watchdog reset

**Claim.** A5: "Fit is decided before sampling … through `fitPassage` …, against the card's text box.
The rungs, in order: [serif 14, serif 12, Ubuntu 10]". A10: "The scan is bounded (A4) and timed in the
log".

**Problem.**
- **It reads as a three-rung gate.** The sleep screen gates on one rung only, the floor
  (`StudySleepScreen.cpp:176-178`, `&gate.floor, 1`). As A5 is written, each row is gated at all three
  rungs.
- **Each rung wraps the whole text before checking height.** `fitPassage` wraps every word
  (`StudySleepFit.h:108-110`, `wrap` then `fitsHeight`). Each word appended re-measures the growing
  line (`:75-84`).
- **The only byte bound is the sleep screen's.** `FIT_PREFILTER_BYTES = 4096` is sized for "roughly …
  40 lines" (`StudySleepPick.h:25-29`). The card holds 2–4 lines. From the glyph advances
  (12.4 fixed point, `EpdFontData.h:134`), Ubuntu 10 sets "abcdefghij" in 99 px, so four lines of the
  roughly 440 px text box hold only about 180 bytes.
- **The work is multiplied.** Almost every multi-verse row, which is most rows, is therefore wrapped
  in full up to three times, on the loop task, with input unpolled (A10), over up to 204 KB
  (`MAX_FILE_BYTES`, `StudySleepScreen.cpp:42`).
- **Nothing resets the watchdog.** Other long loop-task work in this tree resets it against "the 5 s
  watchdog window" (`src/network/CatalogIndexStore.cpp:29-30,101`, `src/util/TaskWatchdog.h`). The
  spec says nothing about this.

**Fix.**
- Gate on the floor rung only (Ubuntu 10, 4 lines), as the sleep screen does. Choose the display rung
  once, for the pick alone (see MINOR 2).
- Add a card-sized byte prefilter (a named constant, for example 512 bytes, pinned in the test) ahead
  of `fitPassage`. Rows it rejects count as `rowsOverPrefilter`, so A11's "too long" hint still works.
- Call `resetTaskWatchdogIfSubscribed()` per file and every N rows in the lifted scan.

## MINOR 2: The fitted lines aren't cached, so every repaint re-fits the verse, against "Nothing is allocated per frame"

**Claim.**
- A8's cache holds `{valid, dated, day, fileBytes, hasPick, emptyReason, pick}`.
- Architecture: "Formatted lines go into fixed `char[]` members. Nothing is allocated per frame."
- A19: every selection move is a `FAST_REFRESH` render.

**Problem.** Drawing the card needs the chosen rung and the line breaks. The cache holds neither, so
`render` has to call `fitPassage` again on every paint, including every selection move. That call
builds a `std::vector<std::string_view>`, span vectors, a working `std::string`, and a
`std::vector<std::string>` of lines (`StudySleepFit.h:45-47,72-73,90-93`). These are per-frame heap
allocations in internal SRAM, which contradicts the Architecture claim. It also lets the drawn layout
drift from the one that was gated.

**Fix.** In `ensurePick`, run `fitPassage` once for the pick over the three card rungs. Store the rung
index and up to four line spans (byte offsets into `pick.text`) in the A8 cache. `render` then draws
each span with `snprintf("%.*s")` into a fixed buffer. Add the rung and spans to A8's list of what the
cache holds.

## MINOR 3: The first paint is rendered twice, and the second render overlaps the scan, which is the sharing A10 gives as its reason for rejecting a task

**Claim.**
- Data flow 1 ends `onEnter` with `requestUpdate()`.
- Data flow 3: "If `versePending`: `requestUpdateAndWait()`, then … scans".
- A10 rejects a separate task because it "would share the renderer's font measurement
  (`measurePassage`) with the render task, which nothing here does today."

**Problem.** The design produces the sharing it rules out.
1. `onEnter`'s `requestUpdate()` sets `requestedUpdate`, and the end of that manager iteration
   notifies the render task (`ActivityManager.cpp:167-172`).
2. On the next iteration, `loop()` calls `requestUpdateAndWait()`, which notifies the render task
   again (`:322-323`).
3. If render 1 is already running, which is likely because the HALF refresh is slow, the render task
   wakes the waiter as soon as render 1 finishes (`:61-69`).
4. It then takes the pending notification and starts render 2 (`:48`).
5. Render 2, a redundant FAST repaint of the same pending card, now runs while the loop task scans
   and calls `renderer.getTextWidth` without the `RenderLock`.

For built-in Latin faces `getTextWidth` is effectively read-only (`GfxRenderer.cpp:600-627`), so this
is probably benign. Even so, it costs an extra panel refresh on every first entry, and A10's stated
rationale is false for its own design. The cited precedent shows the same shape:
`UiListActivity::onEnter` calls `requestUpdate()` (`UiListActivity.cpp:26`), followed by
`MeetingsActivity::loop`'s wait (`MeetingsActivity.cpp:285-293`).

**Fix.** When `versePending` is set, leave out `onEnter`'s `requestUpdate()`. The loop's
`requestUpdateAndWait()` then becomes the first and only pre-scan paint, and it still gets HALF
because `firstRenderDone` is false (`LauncherRefresh.h:15`). Alternatively, hold a `RenderLock` for
the scan. Correct A10's sentence about sharing either way.

## MINOR 4: The Meetings card has no horizontal budget, and its range line collides with the strip in Spanish every week

**Claim.** A21: the Meetings card is 86 px high, with "title, range, %" stacked, and "the strip …
sits beside it and fits inside". A13 draws `LibraryIcon` in the cover's place.

**Problem.** Only heights are given, and the width does not work.
- **The available width.** The card is 464 wide. Take away 2·`PAD`, the 32 px icon plus `PAD`, a
  7-cell strip of about 24 px a cell (168), and a gap. That leaves the text column about 232 px.
- **The range lines are wider.** Measured from `notosans_8_regular.h` advances, in the small font the
  card uses:
  - "Week of 21–27 September" is 205 px;
  - "Week of 28 September – 4 October" is 272 px;
  - "Semana del 21 al 27 de septiembre" is 274 px (`spanish.yaml:366`);
  - "Semana del 28 de septiembre al 4 de octubre" is 353 px (`:367`).

The owner's UI is Spanish (review 0, MINOR 7), so the range overruns the strip every week. Nothing
says whether it truncates, wraps or moves. `test/home_layout` checks only vertical placement, so it
would not catch this.

**Fix.** Give the Meetings card's horizontal geometry in `HomeLayout`: the icon, the text column, the
strip's cell width, and the strip's rows. Pin it in the test. Either:
- put the range on its own full-width row under the title, with the strip beside the title and % rows
  only; or
- draw the range through `renderer.truncatedText` and say so.

## MINOR 5: With no Bible, the data flow sets `versePending` and scans, which contradicts A15

**Claim.**
- A15: with no Bible, "the verse card render[s] empty, and nothing is scanned".
- Data flow 1: "if there is a Bible and `HomeVerse` is valid for today's key, the card is filled now.
  **Otherwise** `versePending` is set."

**Problem.** "Otherwise" includes the no-Bible case, so the loop would scan `bible.json`. That file
exists independently of the EPUB, and its pick opens nothing, because every tap routes to Download.

**Fix.** Rewrite step 1 as: "no Bible: the card is empty and `versePending` stays clear; a Bible and a
valid cache: the card is filled; otherwise `versePending` is set."

## MINOR 6: Meetings and Publications both use `LibraryIcon`

**Claim.** A13 draws `LibraryIcon` on the Meetings card, and A14 gives Publications `LibraryIcon`.

**Problem.** Two different destinations on one screen share one glyph. Today they are told apart:
Meetings uses `LibraryIcon` and Publications uses `SearchIcon` (`LauncherActivity.cpp:384-388`), and
`SearchIcon` now goes to verse Search.

**Fix.** Give Publications a different existing icon, such as `BookIcon` (`src/components/icons/book.h`)
or the folder icon (`folder.h`), so that A14's "no new icon art" still holds.

VERDICT: CLEAR
