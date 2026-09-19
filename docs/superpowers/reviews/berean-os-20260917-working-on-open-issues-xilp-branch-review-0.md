# Whole-branch review — batch `working-on-open-issues-xilp`

Reviewed as one change: PRs #64 (issue #51), #65 (#58), #68 (#40), #70 (#60), merged in that
order onto `main`. Local `HEAD` at review time is `55810b3e` (`git rev-parse HEAD`); `origin/main`
is one commit ahead at `f185a131`, the release-please 1.9.12 version bump.

Per-task review is not repeated here. This pass covers only what a per-task pass structurally
cannot see: the seams between the four branches, independently invented duplicates, assumptions one
task made that a later one invalidated, and requirements every PR passed without any PR
implementing.

---

## 1. Seams between tasks

### #51 ↔ #40 — the shared read path

This was the seam most likely to break: #51 rewrote `lib/Serialization/PersistableStore.{h,cpp}`
and #40's branch was cut before it merged, while `RecentBooksStore` sits on that exact read path.

The final merged state is coherent, and it is coherent by design rather than by luck. `#40`'s spec
names the dependency explicitly — `docs/superpowers/specs/2026-09-17-issue-40-design.md:498-504`
lists "Atomic write, `.tmp` adoption | `PersistableStore.cpp:22-44,63-106` (unchanged, #51)" and
`:536` puts `readDocFromFileAdopting :186` in its own call-graph. In the tree,
`RecentBooksStore` picks up adoption for free through the generic
`PersistableStore<T>::loadFromFile` (`lib/Serialization/PersistableStore.h:186`); `#40` added no
read path of its own and did not fork one.

**The interaction that mattered was the budget, and it is safe.** `#40` tightened `recent.json`
from the shared 45,000 (`lib/Serialization/SaveBudget.h:23`) to 11,421
(`src/util/RecentBooksDoc.h:60-70`). The repeat of the issue-#28 trap — a legacy over-budget file
that can never shrink because the refusal fires before the shrink — does not occur here, because
the shrink happens in memory first: `RecentBooksDoc::fromJson` caps the entry count
(`RecentBooksDoc.cpp:62`) and normalises every entry (`:70`) *before*
`saveToFileAtomic` calls `measureJson` (`PersistableStore.h:170`). A legacy 30 KB `recent.json`
therefore loads, normalises, requests a resave (`RecentBooksStore.cpp:20`), and writes back inside
the new budget.

The budget arithmetic checks out by hand against its own constants:
`12 + 9 + 10 * (52 + 2*(128+96) + 512 + 128) = 11421`, and `ENTRY_OVERHEAD_BYTES = 52` is the exact
length of `{"path":"","title":"","author":"","coverBmpPath":""}`.

**Every write path is inside the bound.** `addBook` (`RecentBooksStore.cpp:39`), `updateBook`
(`:61`) and `fromJson` (`RecentBooksDoc.cpp:70`) all call `normalise`. The other three mutators
cannot grow a bounded field: `removeByPath` (`:74`) and `pruneMissing` (`:101`) only erase, and
`updatePath` (`:88-91`) rewrites `path`/`coverBmpPath`, which carry explicit allowances by design
(`RecentBooksDoc.h:36-44`). `RecentBooksActivity.cpp:71-73` saves only after a prune.
`RecentBooksStore::getDataFromBook` (`:105-123`) does *not* normalise, but `grep -rn
getDataFromBook src` returns only its own definition and declaration — it has no callers and
reaches no store.

### #58 ↔ everything else

`#58` touches the font cache, not persistence; its only shared surfaces are `test/CMakeLists.txt`
and `test/stubs/`. One cross-task property is worth recording because it is what makes its new
`onExit` safe: `TextSettingsActivity::onExit` (`TextSettingsActivity.cpp:263-270`) mutates the
process-lifetime `FontDecompressor` that the render task reads, but
`ActivityManager::exitActivity(const RenderLock& lock)` (`ActivityManager.cpp:178-181`) calls
`onExit()` while holding the render lock, and `renderTaskLoop` takes the same lock before calling
`render()` (`:35-42`). No race.

### #60 ↔ persistence

`#60` removed a key from the settings schema in the same batch that rewrote how settings are read.
The two do not interact: gating is at `SettingsList.h:353-355`, in the list `toJson`/`fromJson`
iterate, entirely above `readDocFromFileAdopting`.

---

## 2. Independently invented duplicates — none found

I checked the four candidates named in the brief plus the UTF-8 and budget helpers.

- **Adoption logic.** `highlightLoadAction` genuinely *moved* rather than being copied. Nothing
  named `highlightLoadAction` or `HighlightLoadAction` survives anywhere in `src`, `lib` or `test`;
  `tempAdoptionAction` in `lib/Serialization/TempAdoption.h:29` now has six call sites —
  `PersistableStore.cpp:75`, `PassageFile.cpp:71`, `TagPaletteFile.cpp:38`, `BookmarkFile.cpp:55`,
  `HighlightFile.cpp:41` — and one test suite. The `#51` diff for `PassageFile.cpp` is purely
  mechanical: the `case` labels renamed and the include swapped.
- **`src/util/HighlightFileAction.h`** is now 24 lines holding only the save-side decision
  (`highlightSaveAction`, `:22`), and says so at `:6-8`. No overlap with `TempAdoption.h`.
- **Byte budgets.** `lib/Serialization/SaveBudget.h` remains the only home for the cap and the
  default. `RecentBooksDoc::SAVE_BUDGET` is derived, not a second constant, and two
  `static_assert`s at `RecentBooksStore.cpp:125-129` pin it to `worstCaseBytes()` and keep it
  strictly under `persist::DEFAULT_SAVE_BUDGET`.
- **UTF-8 truncation.** `RecentBooksDoc.cpp:23-28` calls the existing `utf8SafeSummary`
  (`lib/Utf8/Utf8.h:32`), the same helper `PassageDoc.cpp:28-29`, `HighlightDoc.cpp:62-63` and
  `BookmarkDoc.cpp:43` already use. The only new code is `eraseNuls` (`:13-18`), which is not a
  duplicate of anything and is justified at `:10-12` by the `ESCAPE_FACTOR` assumption it protects.
- **Capability gating.** `BEREAN_CAP_LONG_PRESS_PAGE_TURN` (`CrossPointSettings.h:29-35`) is a
  line-for-line mirror of the pre-existing `BEREAN_CAP_ROTATION` (`:8-14`), including the `#error`
  for an unhandled device. Same idiom, one macro each, no third mechanism.

---

## 3. Contradictions between an early assumption and a later build

### `longPressButtonBehavior` — the gated key still has five readers, and that is correct

`grep -rn longPressButtonBehavior src lib` finds the member (`CrossPointSettings.h:316`), the gated
list entries (`SettingsList.h:355-362`) and five live readers:
`ReaderActivity.cpp:146`, `EpubReaderActivity.cpp:613`, `:619`, `ReaderUtils.h:53`, and
`EndOfBookOptions.cpp:137`. Because the row is now outside `getSettingsList()`, nothing writes the
member any more, so it is permanently its initializer `OFF` and those five become dead branches —
which is exactly what `CrossPointSettings.h:16-17` states the change intends ("The reader's
`SKIP_HOLD_MS` branch is untouched; this gates only the Controls entry").

The one user-visible consequence — a user who had set `CHAPTER_SKIP` had `usePress == false`, and
now gets `true` — is genuinely inert, and I verified the claim rather than taking the PR's word:
`HalGPIO::wasPressed` (`HalGPIO.cpp:211-217`) and `HalGPIO::wasReleased` (`:221-226`) both return
the identical `synthesisedEdge(buttonIndex)` for `BTN_BACK`, `BTN_CONFIRM`, `BTN_UP` and `BTN_DOWN`
— every reachable button. Only `BTN_LEFT`/`BTN_RIGHT` reach the differing `inputMgr` fallthrough,
and those pins are unassigned. `powerTurn` also appears in *both* arms of the `next` ternary
(`ReaderUtils.h:63-68`), so the power-button page turn is unaffected either way.

No contradiction. This is the one place a later task changed what an earlier assumption rested on,
and the result is consistent.

---

## 4. Requirements every PR passed but no PR implemented

### The single-task constraint `#51` leans on — verified, still true

`PersistableStore.h:80-87` makes `readDocFromFileAdopting` safe without a lock by asserting that
every reader and writer of the adopting files runs on the Arduino loop task. That claim holds in
the final tree:

- `grep -rn xTaskCreate src lib` returns exactly one hit: `ActivityManager.cpp:34`
  (`xTaskCreatePinnedToCore`, the render task). Nothing in this batch added a second.
- `renderTaskLoop` (`ActivityManager.cpp:50-62`) does nothing but read `SETTINGS.screenInverted`
  and call `currentActivity->render()`. No activity's `render()` reaches a store load or save —
  `LauncherActivity::render` (`:452-480`) draws from already-resolved members; the store and
  registry calls live in `LauncherActivity::resolveTargets` (`:76`), reached from `onEnter` (`:67`).
- The remaining `xTaskCreate` hits are in `freeink-sdk` (`InputManager.cpp:179`,
  `AudioManager.cpp:313`, `BleKeyboardHost.cpp:421`). `InputManager::beginAsync` — the only one that
  could plausibly run alongside input handling — is never called: `grep -rn beginAsync src lib`
  returns only its declaration at `InputManager.h:204`. None of the three touches storage in any
  case.
- Every caller of the three unlocked `/.berean/` files is on the loop task:
  `MeetingWeekCache::load` from `LauncherActivity::resolveTargets` (`:150`) and
  `MeetingsActivity::refresh` (`:43`), `record` from `MeetingDownloadActivity.cpp:153`, whose
  transfer blocks the loop task for its whole duration (`MeetingDownloadActivity.h:49-50`);
  `PubKeyRegistry` from `StudyStore.cpp:34`, `PublicationDownloader.cpp:184,239`,
  `MeetingLibrary.cpp:44`, `LauncherActivity.cpp:124`, `PublicationsActivity.cpp:59`;
  `MigrationRunner` from `main.cpp:474,502` and the web server, whose `handleClient()` runs inside
  an activity loop (`CrossPointWebServerActivity.cpp:365`).

### Test suite growth — verified by running it

```
cmake -S test -B /tmp/branchreview -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/branchreview -j8
cd /tmp/branchreview && ctest
```

→ **`100% tests passed out of 593`**, `Total Test time (real) = 1.51 sec`. That is the expected
number.

Every suite is wired. I enumerated `test/*/` and checked each directory containing a
`CMakeLists.txt` against `add_subdirectory(<dir>)` in `test/CMakeLists.txt`: zero unwired. The four
directories with no `CMakeLists.txt` (`build`, `epubs`, `language`, `stubs`) are fixtures and
shared headers, not suites. The three lines the batch added are present at `test/CMakeLists.txt:97`
(`temp_adoption`), `:115` (`font_page_slots`) and `:116` (`recent_books_doc`).

The device build is green on the final merged state: `gh run list` shows `CI (build) => success`
for `55810b3e` on `main`.

---

## Findings

Two minors. Nothing blocking, nothing major.

### MINOR 1 — `PersistableStore.h` now gives two incompatible answers about threading

`lib/Serialization/PersistableStore.h:28-31` (pre-existing) says:

> Concurrent saves are reachable: the web server task saves settings while the main task can too.

`:80-87` (added by `#51`, 50 lines below, in the same header) says the opposite:

> safe without a lock of its own only because every reader and writer of the adopting files runs on
> the Arduino loop task

`:28-31` is the stale one — the web server has no task; `handleClient()` is called from the
activity loop at `CrossPointWebServerActivity.cpp:365`, and the only `xTaskCreate*` in `src` is the
render task. `#64`'s PR body flags the stale comment and deliberately leaves it. The result is that
`:86` instructs a future maintainer to check whether a background task touches `/.berean/`, while
`:31` tells that same maintainer one already exists. The failure mode is conservative (someone adds
an unnecessary mutex), which is why this is minor rather than major, but the two comments should
not both stay.

Suggested fix: correct `:28-31` to name the render task and the loop task, in a follow-up.

### MINOR 2 — `test/stubs/Arduino.h` partially disarms `test/pagination`'s compile-time guard

`#58` added `test/stubs/Arduino.h`. `test/pagination/CMakeLists.txt:8-10` documents that the stubs
directory exists so that "`<HalDisplay.h>`, `<HalStorage.h>` and `<Logging.h>` resolve to
`test/stubs` instead, which keeps `<Arduino.h>` and the freeink-sdk panel chain out of the build" —
and the enforcement for `<Arduino.h>` specifically was that the header did not resolve at all.
`test/pagination` and `test/minibidi_arabic` both put `${REPO_ROOT}/test/stubs` on their include
path (`pagination/CMakeLists.txt:26`, `minibidi_arabic/CMakeLists.txt:9`), so it now resolves for
them too.

Blast radius is small and the author saw it coming: the stub defines only `millis()` and `micros()`
(`test/stubs/Arduino.h:11-12`) and says as much at `:5-7`, so anything reaching for `String`,
`Serial` or a pin API still breaks the build, and the panel chain is held out by the `HalDisplay.h`
/ `HalStorage.h` stubs rather than by this one. But a layout source that acquires a `millis()`
dependency would now slip into `test/pagination` silently where it previously could not, and
`pagination/CMakeLists.txt:9-10` reads as though the old guarantee still holds.

Suggested fix: either scope the stub to `font_page_slots/` only, or amend the comment in
`test/pagination/CMakeLists.txt` to say what is actually enforced now.

---

## Observations, not findings

- `gh run list` shows `CI (build) => failure` on every recent `release-please--branches--main-*`
  branch (`64609c83`, `2aeda72d`, `2b99903b`). This predates the batch — the pattern is consistent
  across releases before it — and the release itself publishes fine (`Publish Release Firmware =>
  success` on `f185a131`). Out of scope here, but worth its own issue.
- `RecentBooksStore::getDataFromBook` (`RecentBooksStore.cpp:105-123`) has zero callers and is the
  one `RecentBook`-producing function that does not normalise. It is pre-existing dead code that
  `#40` left alone; harmless today, a trap if someone revives it.
- The host build emits pre-existing warnings from `lib/Epub/Epub/ParsedText.cpp:968,970`
  (`-Wsign-compare`) and `lib/EpdFont/FontDecompressor.cpp:522,523` (unused `label` / `total`, from
  `f1e9dc7f`, not from this batch). Neither is new.

---

## Verdict

The four branches integrate cleanly. The one seam that could genuinely have broken — `#40`'s store
landing on `#51`'s rewritten read path from a branch cut before it — was anticipated in `#40`'s own
design and is correct in the merged tree, including the legacy-shrink case that bit issue #28. No
abstraction was duplicated; `#51`'s helper move is a real consolidation and `#40` reused
`utf8SafeSummary` rather than inventing a cap. `#60`'s schema removal leaves five readers behind by
design, and the behaviour change that implies is provably inert at `HalGPIO.cpp:211-226`. The
concurrency constraint `#51` introduced still holds in the final tree, and no task in the batch
added a second FreeRTOS task. 593/593 host tests pass and every suite is wired into
`test/CMakeLists.txt`.

Two minors above, both documentation-shaped and both fixable in a follow-up. Minors: 2.

VERDICT: CLEAR
MAJORS: 0
