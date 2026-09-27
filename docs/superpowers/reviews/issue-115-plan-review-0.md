Tier: heavy

# Issue #115 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-27-issue-115-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-115-design.md` (cleared at pass 1)
Base checked: `origin/main` at `547a7e88`. The plan claims the branch is rebased onto it, and that holds (`git merge-base HEAD origin/main` = `547a7e88`).

## Summary

The plan is concrete and mostly executable as written. I checked it against the code, not only against the spec. The API calls it relies on exist with the signatures it uses:

- `BookCardProps` fields and the `coverPainter` pointer type (`book-card.h:14-45`);
- `Screen::frame/target/theme/setContentMargin/list` (`FreeInkApp.h:59-65, 245-301`);
- `Rect`/`Insets`/`Size` field order (`FreeInkUICore.h:25-28, 87-102`);
- `TextStyle::color`/`align` (`FreeInkUICore.h:534-544`), and the GfxRenderer target honours `Color::White` (`FreeInkUIGfxRenderer.h:134`);
- `const` `drawBitmap`/`drawRoundedRect` (`GfxRenderer.h:239, 260`);
- `HalClock::getTime/getDate` (`HalClock.h:28, 41`);
- `SettingInfo::Enum` taking the vector by value (`SettingsActivity.h:80`);
- `catalog::wordAt/copyOut`, where `copyOut` writes nothing on failure (`CatalogLabel.cpp:15-43`);
- the `Epub` methods (`Epub.h:52-81`).

Three other points check out:

- The layout band arithmetic matches `list()`'s drop rule (`list.h:318-321, 436-446`). The frame's safe rect is the full screen, so `setContentMargin` from screen edges is exact (`FreeInkUIGfxRenderer.h:46-56`).
- The calendar code is correct. I traced the tests by hand: 2026 has a week 53 while 2025 and 2027 do not, and the `civilFromDays` inverse is right.
- `refresh()` runs on the main task with no `RenderLock` held on both paths: `onEnter` and the result handler (`ActivityManager.cpp:122-126, 155-156`). So `clampSelection()` taking a `RenderLock` cannot deadlock.

The one material defect is the order in which the test build is registered: as written, no host test can be configured until Task 7.

## MAJOR

### MAJOR 1: Step 0.2 registers two test directories that do not exist until Tasks 6 and 7, so every host-test command in Tasks 2 to 6 fails at CMake configure

- **Claim** (plan:77-86): add `add_subdirectory(meeting_week_view)` and `add_subdirectory(book_progress)` to `test/CMakeLists.txt` in Task 0. Tasks 2 to 5 then run `cmake -S test -B build/test && cmake --build ...` (plan:214-216, 290-292, 348-350, 385-387, 442-444, 474-476, 516-518, 560-562).
- **Problem**: `test/book_progress/` is created in Step 6.1 (plan:580) and `test/meeting_week_view/` in Step 7.1 (plan:766). CMake stops configuring on `add_subdirectory given source "…/meeting_week_view" which is not an existing directory`. `build/` does not exist in this worktree yet (`ls build` → no such directory), so there is no stale generated tree to fall back on.
  - Every RED step in Tasks 2 to 5 therefore "fails" for the wrong reason.
  - Every GREEN step (2.5, 3.4, 4.4, 5.4) cannot pass.
  - Step 6.2/6.4 still fails on the missing `meeting_week_view`.
  
  An implementer following the plan literally stalls at Step 2.2.
- **Evidence**: plan:77-86 against plan:580 and plan:766; `test/CMakeLists.txt:82-83` is where the lines go.
- **Fix**: move each line into the task that creates its directory:
  - Step 6.1 adds `add_subdirectory(book_progress)`;
  - Step 7.1 adds `add_subdirectory(meeting_week_view)`.
  
  Step 0.2 is then deleted or reduced to a note. Neither line is committed, which is unchanged. The `FILES:` entry for `test/CMakeLists.txt` stays.

## MINOR

### MINOR 1: The ASCII-hyphen deviation from A12 rests on an unverified premise that is false

- **Claim** (plan:38-40): English uses `"Week of %u-%u %s"` because "whether the built-in Ubuntu UI subset carries U+2013 was not verified".
- **Problem**: it does. The range line is drawn with `bodyText`, which is the Ubuntu 12 slot (`UIScale.h:16-17`), and that font's interval table covers U+2013–U+2015. The deviation from the spec's A12 table has no reason left.
- **Evidence**: `lib/EpdFont/builtinFonts/ubuntu_12_regular.h:3116` (`{ 0x2013, 0x2015, 0x323 }`).
- **Fix**: restore the spec's formats:
  - `"Week of %u–%u %s"` and `"Week of %u %s – %u %s"` in Step 1.1 and in `EN_SAME_MONTH`/`EN_TWO_MONTHS` (plan:946-947);
  - the expected strings at plan:960 and 971;
  - drop the deviation paragraph.

### MINOR 2: The #114 merge check in Step 0.1 always reports "merged", and the merged-#114 path departs from A14 without saying so

- **Claim** (plan:74, 42-46): `git log --oneline origin/main | grep -i "sleep screen"` detects #114. If it has merged, "the follow-up line stays".
- **Problem**:
  - The grep already matches `28c4cdc3 feat: … a Bible-centred sleep screen (#18)` on today's `origin/main`, so it cannot tell whether #114 has landed.
  - Spec A14 (spec:410-413) says that if #114 has merged, this branch reuses #114's helpers or folds both onto `WolWeekScan` in the same change. The plan keeps the duplicate as a follow-up either way.
  
  #114 has not merged today (`git branch -r` lists `origin/feat/114-study-sleep-screen`; nothing on `main`), so this only matters if it lands before `implement`.
- **Evidence**: `git log --oneline origin/main | grep -i sleep` → `28c4cdc3 … sleep screen (#18)`; spec:410-413.
- **Fix**:
  - Detect #114 by its content, for example `git grep -q '^STR_WEEKDAYS:' origin/main -- lib/I18n/translations/english.yaml`.
  - Either add a conditional step that applies A14, or list "keep the duplicate even if #114 merged" as a stated deviation in "What changed since the spec was written".

### MINOR 3: `refresh()` rewrites the render task's data without the render lock, over a longer window than before

- **Claim** (plan:1754-1771, 1703-1752): `refreshCards` and `refreshWeek` assign `cards_[i] = Card{}`, `rangeLine_`, `strip_` and `weekdayLetters_` in place. Only `clampSelection()` takes a `RenderLock`.
- **Problem**: the download result handler runs after `ActivityManager` releases its lock (`ActivityManager.cpp:122-126`). A render notification already queued from the child activity can therefore run `buildScreen` while `refresh()` runs. Now `refresh()` also generates thumbnails and possibly `book.bin` on exactly that path (spec behaviour difference 2), which takes seconds. `drawCard` would then read `card.title.c_str()` while the string is being destroyed. The old rows had the same pattern, but only a few microseconds of exposure.
- **Evidence**: plan:1766, 1916-1917; `ActivityManager.cpp:47-60, 118-126`.
- **Fix**: build the new cards, range line, strip and letters into locals, doing all the SD work first. Then move them into the members, together with `cardCount_`, `weekShown_` and `anyMeetingDay_`, inside one `RenderLock` scope that also does the clamp.

### MINOR 4: Tasks 9, 10 and 11 commit code that has never been compiled

- **Claim** (plan:1179-1180, 1349-1350, 1413): each of these tasks commits and defers the first compile to Task 12.
- **Problem**: these tasks have no failing test. That is acceptable for settings registration, a mechanical move and an SD-bound function. But a compile error in the `CoverThumb` extraction would first show up in Task 12, mixed in with the screen rewrite, and the fix would land in the wrong commit. The brief asks for every step to leave the tree committable.
- **Evidence**: plan:1179-1180, 1349-1350, 1413, 1994-1995.
- **Fix**: run the locked `pio run -e x4pro` once at the end of Task 11, before the screen rewrite. It is one extra incremental build and covers Tasks 9 to 11. Task 12's build then only exercises the new screen.

### MINOR 5: Small deviations from the spec's A8 and its Error-handling table

- **Fonts**: `computeLayout` hardcodes `UI_12_FONT_ID`/`UI_10_FONT_ID` (plan:1630-1631), where spec A8 (spec:228-229) says to use the IDs `uiScaleSpec()` binds. They are equal today (`UIScale.h:16-17`). Use `uiScaleSpec().bodyFontId`/`.smallFontId` so the Refresh band cannot drift from the theme's list fonts.
- **Unreadable header**: spec:642 asks for a `LOG_DBG` when a thumbnail header is unreadable. `fillCard` logs only when a size was read but does not fit (plan:1791-1804). Add an `else` branch that logs when `sizeOf` fails for a non-empty `coverPath`.

## Checked and sound

**Every spec requirement maps to a step:**
- A1/A2 → Tasks 6 and 11;
- A3 → Tasks 4 and 12;
- A4 → Tasks 5 and 12 (`refreshWeek`);
- A5/A6 → Task 12 (`refreshCards`/`fillCard`);
- A7 → Task 10;
- A8 → `computeLayout`/`fillCard`;
- A9 → `drawCard`, `drawRefresh`, `handleCustomInput` and `clampSelection`;
- A10 → Task 9;
- A11 → `buildWeekStrip`/`drawLegend`;
- A12 → Task 8;
- A14 → Tasks 1 and 13.5 (see MINOR 2).

**Other checks:**
- **Names and signatures** stay the same from step to step (`CivilDate`, `WeekStrip`, `copyWordAt`, `CoverThumb::{pathFor,sizeOf,drawNative}`, `readBookProgressPercent`).
- **New link dependencies are satisfied.** `isoWeekFromKey` adds a dependency on `WolWeekScan.cpp`, and the only suite that compiles `MeetingWeekTable.cpp` already links it (`test/meeting_week_table/CMakeLists.txt`).
- **No new name collides** with an existing symbol in `src/`, `lib/` or the SDK.
- **`FILES:` lines** cover every tracked file the steps touch. The comma-separated form has precedent in earlier plans. `build/pio-115.log` and the generated i18n headers are gitignored (`.gitignore:8-13`).
- **Persistence** of the two settings is automatic, as A10 says (`CrossPointSettings.cpp:67-84, 166-169`).
- **Step 10.3** names the three launcher call sites exactly (`LauncherActivity.cpp:135, 160, 381`), and nothing outside the launcher references the removed members.

VERDICT: CLEAR
