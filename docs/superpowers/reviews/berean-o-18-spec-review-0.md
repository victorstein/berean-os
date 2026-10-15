Tier: heavy

# Spec review 0: berean-o-18 (Elegir temas, the Predicación topic picker)

Reviewed: `docs/superpowers/specs/2026-10-15-berean-o-18-design.md` at `ab86ec1f`, checked against
the brief (`hpipe brief --task t5`), the research note
`docs/superpowers/research/2026-10-15-berean-o-18-research.md` (decision d1 taken as settled), the
parent spec `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §3, §7 and §9, and
`.claude/agents/ui-dev.md`. The batch base has not moved:
`git log HEAD..origin/batch/20261015-predicaci-n-193x` is empty after a fetch.

| Severity | Count |
|---|---|
| BLOCKER | 0 |
| MAJOR | 3 |
| MINOR | 4 |

d1 is applied correctly, and it closes the empty-palette hazard. The chips come only from
`STUDY.palette()`, which `openPublication` loads right before the push
(`StudyStore.cpp:72`). `canResumePublication` compares the `shared_ptr` itself
(`StudyStore.cpp:115`). The reader resumes only a parked book it took
(`EpubReaderActivity.cpp:219-224, 274`), so the "parked Bible resumes" claim holds.

The spec's citations hold, line for line. The MAJORs are about what the picker writes and what it
compares, and about one device check whose result depends on history the spec doesn't state.

## What was checked and holds

- **A1.**
  - `UiListActivity::loop` is overridable (`UiListActivity.h:22`).
  - Input is refreshed before any activity runs: `gpio.update()` is at `main.cpp:657`, and
    `activityManager.loop()` is at `:800`.
  - The only caller of `onBackButton()` that the picker can reach is `UiListActivity.cpp:50`. So
    intercepting `wasReleased(Back)` really does make the cancelling exit unreachable.
  - While the retire popup is open, the swipe is a touch snapshot. `OptionPopup::handleInput`
    swallows it without dismissing (`OptionPopup.h:72-108`). Behaviour matches the base either way.
- **A5's order is load-bearing, and the spec has it right.**
  - `finish()` sets `Pop`. `goHome()` then replaces it with `Replace`, which runs every stacked
    `onExit` (`ActivityManager.cpp:144-151`). That includes `PreachingActivity::onExit`, which parks
    the Bible.
  - The reverse order would lose Home: `popActivity()` resets a pending activity
    (`ActivityManager.cpp:267-273`). The plan must keep `saveAndReturn()` before `goHome()`.
- **A9.**
  - `listCount()` is `topics.size() + 1`, which is the ring position of the Elegir row
    (`PreachingActivity.cpp:102-105`).
  - Ring 0 is the tab bar (`UiTabListActivity.cpp:49-61`).
- **A10.** `openPublication` never draws. `UnitIndexCache::begin` touches only SD
  (`UnitIndexCache.cpp:50-122`). `PassageFile`, `PubKeyRegistry` and `TagPaletteFile` have no
  renderer use (grep for `renderer|GUI.|FrameBufferLoan` is empty).
- **Toasts.** Both screens the picker can leave to call `PostedMessage::drawNext`:
  - `UiListActivity.cpp:170` (no override in `UiTabListActivity.h`);
  - `LauncherActivity.cpp:376`.
- **Test 3's log check is observable.** The counts pass logs `Counts: …` at `LOG_INF`
  (`PreachingPassages.cpp:185`), and `x4pro` builds with `-DLOG_LEVEL=2` (`platformio.ini:174`).

## MAJOR

### M1. Every exit writes `preaching.json`, even when nothing changed

**Claim** (:213-220): `saveAndReturn()` always calls `topicsFile.save(picks)`, from the swipe and
from Home.

**Problem.**

- `CLAUDE.md:322`: "Guard redundant writes … every one costs serialisation, SD I/O, and
  `storageMutex` contention." The spec does not mention it.
- Each visit that only looks at the picker does an atomic tmp-and-rename write
  (`PreachingTopicsFile.cpp:94`).
- The visible regression is in the latched state.
  - `save()` returns `Refused` there (`PreachingTopicsFile.cpp:64-67`).
  - Every visit then ends with "Couldn't save your topics", even when the owner toggled nothing.
  - Under A5 this also happens on Home.
  - Error-table row 4 ("On leaving: `STR_PREACHING_SAVE_FAILED`") makes that the specified
    behaviour.

**Fix.**

1. Keep the selection seeded in `onEnter` (A3) as a loop-task member.
2. In `saveAndReturn()`, skip `save()` and the toast when `picks` has the same ids as that seed. Use
   the four-iterator `std::is_permutation` (see M3). Still set the `TagSelectionResult`.
3. A retired pick makes `picks` differ from the seed, so the save still runs and prunes it.

Then update the docs and tests:

- Reword error row 4 to "On leaving after a change".
- Test 5 is unchanged (it picks).
- Add a check: latched, change nothing, swipe → no toast.

### M2. Device check 7 fails on the first visit after boot, by design

**Claim.**

- Memory (:306-309) says opening `STUDY` holds only the `bible.json` passages, in PSRAM.
- Test 7 (:335): "Free heap equal before Elegir temas and after returning."

**Problem.**

- `ESP.getFreeHeap()` counts internal heap only:
  `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`,
  `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/Esp.cpp:163-165`.
- On the boot-to-Home path that test 1 sets up, `STUDY` is closed before the tap. After the return
  it holds three things in internal heap:
  - `palette_`: a vector of 48 `Entry` records, each about 32 B with a `std::string`
    (`TagPalette.h`, `entries_`), plus heap names over the SSO length;
  - the `UnitIndexCache` object (`StudyStore.cpp:86`), which also keeps a `shared_ptr<Epub>` to the
    Bible (`UnitIndexCache.h:87`);
  - `pubKey_`.
- `closePublication()` never clears `palette_` (`StudyStore.cpp:100-109`).
- So the first round trip shows a drop of roughly 1.5–2 KB, and it looks exactly like a leak.
- A round trip with `STUDY` already open on the Bible changes nothing, because `canResume` is true.
- A round trip after reading another publication swaps that publication's state for the Bible's. The
  result depends on history the check doesn't state.

**Fix.**

- In Memory, list `STUDY`'s resident state after the first open (palette, unit index with its
  `Epub` reference, the passages in PSRAM), held until the next `openPublication`. That is the same
  state reading the Bible leaves behind.
- Rewrite test 7: "Open Elegir temas once and return (the 'Study opened' line appears). Note free
  heap. Open it again, change nothing, return (no 'Study opened' line). Free heap is equal."

### M3. "As sets (`std::is_permutation`)" names an overload that misses an unpick

**Claim** (A8, :166-168): the result is compared with `session.topics()` "as sets
(`std::is_permutation`)".

**Problem.**

- The three-iterator `std::is_permutation(first1, last1, first2)` compares `[first1, last1)` only
  against the same number of elements from `first2`.
- Both lists are in `activeIdsByName` order:
  - `picks` from `selectedInOrder` (`TagChipRow.h:166-174`);
  - `topics()` from `PreachingSession.cpp:125-127`.
- Say the owner unpicks the alphabetically last tag. `picks = [a]` and `topics = [a, b]` compare
  `[a]` with `[a]` and return true.
- The result is "unchanged": no reload, and Mis temas keeps listing `b`, which is no longer in
  `preaching.json`.
- A longer `picks` reads past the end of `topics`, which is undefined behaviour.
- None of the device checks unpicks the last-named tag, so the bug would ship unseen.

**Fix.**

- Name the four-iterator overload:
  `std::is_permutation(picks.begin(), picks.end(), topics.begin(), topics.end())`. It is C++14, and
  the build is `gnu++2a`.
- Use the same form for M1's seed comparison.
- Add to test 2 or 3: unpick the last tag in name order and swipe; Mis temas drops it.

## MINOR

### m1. A failed save still reloads and recounts

**Claim** (A7, A8): the picker returns `picks` "whether or not the save succeeded". The parent
reloads when they differ from `session.topics()`.

**Problem.**

- After a `TooLarge`, `WriteFailed` or `Refused` save with toggles, `preaching.json` is unchanged.
- But the comparison sees a change, so `onTopicsChanged()` re-reads both files and runs the
  `bible.json` counts pass for nothing.
- The brief: "Re-run T2's counts pass only when the selection changed."

**Fix.**

- On a non-`Ok` save, return what the file still holds, minus tags retired in `STUDY`:
  `selectedInOrder(STUDY.palette().activeIdsByName(), topicsFile.tags())`.
- `tags()` is unchanged on failure (`PreachingTopicsFile.cpp:96`).
- A retired pick still shows as a change, and nothing else does.

### m2. d1's reopen rests on an unstated invariant, and the device plan doesn't test it

**Claim** (:111-112): `canResumePublication`, not `isOpen()`, so that a `STUDY` open for another
publication is reopened on the Bible.

**Problem.**

- That reopen calls `closePublication()` on whatever `STUDY` held (`StudyStore.cpp:65`). It is safe
  only because no reader is ever alive beneath Predicación.
- `goToPreaching` is the only way in, and it is a replace (`ActivityManager.cpp:232-239`). The spec
  never states this.
- No device check covers the case d1 refined. All seven start from a closed `STUDY` or one already on
  the Bible.

**Fix.**

- State the invariant in the d1 section.
- Add a check: read a non-Bible publication, go Home, then Predicación → Elegir temas. The "Study
  opened" line appears. Retire a tag. It is gone from `bible.json`, and the other publication's file
  is untouched.

### m3. A failed open latches tagging off on the whole device, not only in the picker

**Claim** (error-table row 2): if tags or `bible.json` is unreadable, "Picker opens; a retire is
refused".

**Problem.**

- `saveDisabled_` survives `closePublication` for the whole boot (`StudyStore.cpp:107-108`).
- After a visit to Elegir temas with an unreadable `bible.json`:
  - every tag edit in every publication is refused until reboot;
  - the reader shows `STR_HIGHLIGHTS_LOAD_FAILED` on every book open
    (`EpubReaderActivity.cpp:282-284`).
- Opening the Bible in the reader already does this. What is new is reaching it without opening the
  Bible.

**Fix.** Put the boot-wide scope in that row, so the owner and the tester know what to expect.

### m4. Two latch descriptions are wrong in the tags.json case

**Claim.**

- A3 (:141): "If the load latched, `tags()` is empty (`PreachingTopicsFile.cpp:58`)".
- Error-table row 3: "tags.json unreadable as STUDY loads it | Palette empty, so no chips".

**Problem.**

- `:58` empties `tags_` only when `preaching.json` failed. A latch from `tags.json` alone keeps the
  stored picks.
- `openPublication` loads into the existing `palette_` without resetting it
  (`StudyStore.cpp:72`), and `closePublication` keeps it (`:100-109`).
- So after any earlier clean open this boot, an unreadable `tags.json` shows the previous palette as
  chips, not an empty grid.
- No write gets through either way: both stores refuse. But a tester following row 3 would expect an
  empty grid and see chips.

**Fix.**

- Say "empty when `preaching.json` failed" in A3.
- In row 3, say the grid shows whatever palette `STUDY` last loaded, or none after a fresh boot.

VERDICT: CLEAR
