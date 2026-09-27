Tier: heavy

# PR #167 — code-quality review (issue #114, study sleep screen)

Scope: the code in `git diff 547a7e88...HEAD` (the PR base): `src/CrossPointState.{h,cpp}`,
`src/CrossPointSettings.h`, `src/SettingsList.h`, `src/activities/boot_sleep/{SleepActivity.cpp,
StudySleepPick.h, StudySleepScreen.{h,cpp}}`, the two new host suites and the i18n keys. The spec's
deliberate choices (reading files directly instead of through `PassageFile`/`TagPaletteFile`/`ChapterCompletionFile`
because those can rename a `.tmp`; not using `PassageDoc::fromJson`; the local `daysFromCivil`; no PSRAM
allocator) are settled and are not reopened here.

## What mirrors its siblings correctly

- `StudySleepScreen.{h,cpp}` is a free-function namespace that paints straight to the renderer and
  pushes a `HALF_REFRESH`, which is how `MigrationScreen.h:15-21` and `renderDefaultSleepScreen`
  (`SleepActivity.cpp:641-662`) do it. The `render` → `false` → `renderDefaultSleepScreen()` fallback at
  `SleepActivity.cpp:548-550` follows the `renderCoverSleepScreen` / `renderCustomSleepScreen`
  fallback shape.
- The count pass, then `rewindDirectory()`, then a second pass in `pickPassage`
  (`StudySleepScreen.cpp:149-183`) is modelled on `selectRandomSleepFile` (`SleepActivity.cpp:414-449`).
  It also ends the same way: push to the `APP_STATE` ring, then `saveToFileAtomic()`
  (`StudySleepScreen.cpp:373-374` vs `SleepActivity.cpp:448-449`).
- The new ring fields, `toJson` and `fromJson` (`CrossPointState.cpp:64-67, 108-119`) copy the two
  existing rings line for line, including the cursor clamp. The `SAVE_BUDGET` comment
  (`CrossPointState.h:38-40`) was updated, and the key and scalar counts it states check out at 15
  and 10.
- The randomness seam is a plain function pointer plus a `void*` context
  (`StudySleepPick.h:80`), which is the callback form CLAUDE.md prescribes instead of `std::function`.
  Allocations go through `makeUniqueNoThrow`, and each has a `LOG_ERR` on OOM
  (`StudySleepScreen.cpp:344-349, 364-366`).
- The i18n additions reuse the existing `STR_MONTHS_SHORT` and `catalog::wordAt` word-list idiom
  (`CatalogStamp.cpp:46`) rather than adding per-month keys.
- Tests: the pure header is host-tested the way `test/launcher_bible` tests `LauncherBible.h`. The
  sampler tests use a scripted RNG that records every `bound` it receives
  (`StudySleepPickTest.cpp:95-106, 124`), so they check that the reservoir really draws with
  probability 1/k, not only which passage wins. The date tests cover the year crossing, the leap day,
  the clamped offset and the undersized-buffer paths. `CrossPointStateTest` runs the real `fromJson`
  against the in-memory fake the way `test/persistable_store` does.

No BLOCKER or MAJOR findings.

## Findings

### MINOR 1 — `pushRecentStudySleep` inlines the ring push its siblings delegate

`src/CrossPointState.cpp:46-50` rewrites the body of `pushRecentIndex` (`CrossPointState.cpp:22-26`)
by hand. `pushRecentSleep` and `pushRecentOverlaySleep` (`:38-44`) are one-line delegations to that
helper. The helper takes `uint16_t*`, so the new `uint32_t` ring could not call it as it stands. The
fix that keeps one way of doing this is to make the file-local helper a template over the element
type, `template <typename T> void pushRecentIndex(T* recent, uint8_t& pos, uint8_t& fill, T value)`,
and make `pushRecentStudySleep` a one-line call like its siblings. `ageOf` (`StudySleepPick.h:70-77`)
reimplements `isRecentIndex` for a real reason: it has to stay host-pure, and it returns an age rather
than a bool. It is not part of this finding.

### MINOR 2 — the `offerRow` comment claims parity with `PassageDoc::fromJson` that the code deliberately lacks

`src/activities/boot_sleep/StudySleepScreen.cpp:91-92` says: "The same gates PassageDoc::fromJson
applies, so a row the rest of the device drops cannot surface here." That is not true in two
directions:

- `fromJson` rejects a whole file over one bad link row or an over-budget total
  (`lib/StudyStore/StudyStore/PassageDoc.cpp:245-248, 256`). `offerFile` does not, and spec A5 says
  this is on purpose. So rows from a file the rest of the device refuses to load *can* appear here.
- `offerRow` adds a gate that `fromJson` does not have: it skips a row whose snippet is empty
  (`StudySleepScreen.cpp:102`).

Both behaviours are the right ones. The comment misstates them, though, and the next reader will
trust it. Suggested wording: "Row-level gates only, as `PassageDoc::fromJson` applies them to `u`, `e`, `x`, `r`
and `t`. A file `fromJson` would reject whole still contributes its valid rows, and a row with no
snippet is skipped because there is nothing to show."

### MINOR 3 — the completion path is rebuilt instead of calling `ChapterCompletionFile::path`

`src/activities/boot_sleep/StudySleepScreen.cpp:216-217` builds `"%s/%s.json"` from
`sdpaths::COMPLETION_DIR`. That repeats the layout `ChapterCompletionFile::path`
(`src/study/ChapterCompletionFile.cpp:16`) owns. The spec's reason for avoiding
`ChapterCompletionFile::load` (it can rename a `.tmp`) does not apply to `path()`, which only builds a
string, and calling it is a read of `src/study/`, not an edit. If the layout ever changes, this copy
drifts silently and the progress strip disappears with no error, because a mismatched path reads as
`Missing`, which draws an empty strip. The tag palette read already goes through the owner's constant
(`sdpaths::TAGS_FILE`, which `TagPaletteFile::PATH` aliases), so doing the same here would make the
two reads consistent. `path()` returns a `std::string`, which costs one small allocation per sleep.
That is negligible next to the `JsonDocument` parses around it.

### MINOR 4 — one sampler test loops without exercising anything

`test/study_sleep_pick/StudySleepPickTest.cpp:133-138` runs `roll` over 0..3, as if to show that no
random draw can pick the recent passage. It has only one fresh passage, though, so the sampler's only
call is `random_(ctx, 1)` (`StudySleepPick.h:102`), and `scripted` reduces every roll to
`value % 1 == 0`. All four iterations therefore run the same path. The assertion still holds, but the
loop suggests coverage that isn't there. A version that tests the claim would offer two fresh passages
and a recent one between them, e.g. `{"a", nullopt}, {"shown", 0}, {"b", nullopt}`, then check that
every script gives `"a"` or `"b"` and never `"shown"`. Otherwise, drop the loop.

### MINOR 5 — unused include

`src/activities/boot_sleep/StudySleepScreen.cpp:20` includes `<cstring>`, but nothing in the file uses
it (no `mem*` or `str*` calls; the `memcpy` is in `StudySleepPick.h`, which includes `<cstring>`
itself).

## Summary

The change mirrors its nearest siblings closely: the migration screen's free-function renderer, the
sleep-folder picker's two-pass scan and ring push, and the existing ring serialisation. Its tests are
designed to check behaviour, not just to exist. All five findings are small cleanups that can be fixed
inline. None of them reverses a decision or needs the user's judgment.

VERDICT: CLEAR
