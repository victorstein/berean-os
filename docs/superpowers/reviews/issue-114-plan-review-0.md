Tier: heavy

# Issue #114 plan review, pass 0

Reviewed: `docs/superpowers/plans/2026-09-27-issue-114-plan.md` (the plan) against
`docs/superpowers/specs/2026-09-27-issue-114-design.md` (the spec), on `9d8abdf9`. Decision d1, the
unchanged default sleep mode, and the read-only status of `src/study/` and `lib/StudyStore/` are
settled and are not reopened here.

## Method

Every API the plan calls was read at its declaration. Tasks 1–6 were also **executed**: the tree was
exported with `git archive HEAD` into a scratch directory, the plan's code blocks for Tasks 1–6 were
applied at the anchors the plan names (every anchor matched verbatim), and both new host suites were
configured, built and run with the repo's own `test/CMakeLists.txt`. Result: `StudySleepPickTest`
28/28 pass and `CrossPointStateTest` 4/4 pass. Task 8 is device code and was checked by reading
rather than compiling, because `pio` is off limits for this review.

## What checked out

- **FILES lines** (plan:7-12) are at column 0, outside any fence, and repo-relative. Every file a task
  touches is covered: the three new `boot_sleep` files, `SleepActivity.cpp`, `CrossPointState.{h,cpp}`,
  `CrossPointSettings.h`, `SettingsList.h`, both YAMLs, `test/CMakeLists.txt`, `test/study_sleep_pick/`,
  `test/crosspoint_state/` and `docs/file-formats.md`. The build output (`build/`) and the generated
  i18n headers are gitignored (`.gitignore:13`, `.gitignore:8`).
- **Anchors exist verbatim**: `add_subdirectory(launcher_refresh)` (`test/CMakeLists.txt:122`); the
  budget comment, `recentOverlaySleepFill`, `pushRecentOverlaySleep` and the `fromJson` line
  (`src/CrossPointState.h:33-34`, `src/CrossPointState.cpp:43-45,83`); the enum and label slot
  (`CrossPointSettings.h:63-64`, `SettingsList.h:241`); the `switch`/`default:`
  (`SleepActivity.cpp:534-549`); the `file-formats.md` sentence (`docs/file-formats.md:418-420`).
- **APIs match their call sites**: `HalClock::getDate(Date&)` / `getTime(uint8_t&, uint8_t&)` and
  `halClock` (`lib/hal/HalClock.h:7,28-41`); `SETTINGS.clockUtcOffsetQ` (`CrossPointSettings.h:262`);
  `sdpaths::PASSAGES_DIR` is a `char[]`, so `sizeof` is valid (`lib/Serialization/SdPaths.h:30`);
  `readDocFromFileStreamed` / `readDocFromFileChecked` are public statics that never rename
  (`PersistableStore.h:76,81`; `PersistableStore.cpp:124-162`); `DocReadStatus` has
  `Ok/Missing/Unreadable/ParseError` (`DocReadStatus.h:8-12`); `TagPalette::fromJson` / `name(TagId)`
  / `study::toTagId` (`TagPalette.h:27,54,65`); `ChapterCompletion::fromJson` / `readCount` /
  `readCountInBook` / `canonicalChapterCount` returning `uint8_t` (so the `std::max` compiles)
  (`ChapterCompletion.h:24,41-42,50`); `study::BIBLE_PUB_KEY` (`PubKey.h:37`), and the completion path
  it builds matches `ChapterCompletionFile.cpp:16`; `HalFile` has `getName`, `size`, `isDirectory`,
  `rewindDirectory`, `openNextFile`, move-assignment and `operator bool` (`HalStorage.h:71-97`);
  every `GfxRenderer` call is `const` with the used overload (`GfxRenderer.h:186-243,278-303`);
  `berean_mark::draw(const GfxRenderer&, …)` (`BereanMark.h:14`); `makeUniqueNoThrow` forwards
  constructor args (`Memory.h:25-26`); U+201C is in `notoserif_18_bold.h:4488`.
- **Read-only (A3)**: no `*File::load` is called; the only write is `APP_STATE.saveToFileAtomic()`
  after the draw (plan:1314-1315), mirroring `SleepActivity.cpp:447-448`.
- **Spec mapping**: A1 (7a), A2 (8c), A3–A7 (8b `pickPassage`/`offerFile`/`offerRow`), A8–A9 (Task 3),
  A10–A11 (Tasks 1, 2, 6, 8b), A12–A13 (Task 5, 8b `formatDate`), A14 (7c), A15/d1 (`hasReference`,
  plan:1233), A16 (`tagName`), A17 (`loadCompletion` treats `Missing` as an empty strip), A18
  (`drawScreen`, `clearScreen()` first), A19 (`Candidate`, `static_assert`s at plan:1008-1009), A20,
  the heap/PSRAM logging, the Known-limits list and the device checks (Task 10) all have a step.
  Spec tests 1–8 all appear, except the half of 5a noted below.
- **Arithmetic**: the date-shift range is {-1,0,+1}; `weekdayFromDays` is right for negative days
  (checked −5 → Saturday, −11 → Sunday, −12 → Saturday); the state budget comment's "15 keys / ten
  scalars / ~1,440 B" recounts correctly.

## Findings

### MAJOR 1 — Tasks 5 and 6 commit a tree whose firmware build fails

**Claim.** Each task "leaves the tree working and committable" (ground rules, plan:19-20).

**Problem.** Task 5 adds the comment `// 0 = Sunday, the order of STR_WEEKDAYS and
Rtc::DateTime::weekday.` to `src/activities/boot_sleep/StudySleepPick.h` (plan:605) and commits it
(plan:665). `STR_WEEKDAYS` does not reach `english.yaml` until Task 7c (plan:897). `gen_i18n.py` scans
the **raw text** of every `.h`/`.cpp`/`.c` under `src` and `lib`, comments included, and exits 1 on
any `STR_*` identifier missing from English. It runs as the PlatformIO `pre:` step, so `pio run` fails
at the Task 5 and Task 6 commits, whether or not anything includes the header. Task 7b's expected
failure also reports two missing keys, not the one the plan predicts (plan:891).

**Evidence.** `scripts/gen_i18n.py:267` (`\bSTR_[A-Za-z0-9_]+\b`), `:274-284` (rglob over
`.cpp/.h/.c`, no comment stripping), `:872-879` (missing keys are CRITICAL). The project memory
`gen-i18n-scans-comments-for-str-keys.md` records this exact failure on PR #47. `STR_MONTHS_SHORT` in
the other comment (plan:621) is safe, because it already exists (`english.yaml:439`).

**Fix.** At plan:605, reword the comment without the identifier: `// 0 = Sunday, the order of the
weekday word list and Rtc::DateTime::weekday.` (Moving Task 7 ahead of Task 5 would also work, but
the rewording is the smaller change.)

### MINOR 1 — The mark is 32 px; the spec says 40

`MARK_SIZE = 32` (plan:1003), but A18 item 7 says "`berean_mark::draw` at 40 px" (spec:267). Nothing
in the plan explains the change. **Fix:** set `MARK_SIZE = 40`. There is room: the footer plus the
content block is about 600 px on an 800 px portrait panel.

### MINOR 2 — Spec test 5a's second half ("e absent → same key as e == u") has no test

Spec:378 asks for both halves. Task 1 covers "different `e` gives a different key" (plan:102-104). The
`e`-absent or `e`-invalid fallback lives in `offerRow` (plan:1044-1045), device-only code that no test
reaches. **Fix:** add a pure `inline std::string_view effectiveEndUnit(std::string_view start,
std::string_view end, bool endValid)` to `StudySleepPick.h` with a test that shows
`passageKey(p, u, effectiveEndUnit(u, "", false)) == passageKey(p, u, u)`, and call it from
`offerRow`.

### MINOR 3 — The cap-reached cases log nothing at INF, and hitting `MAX_ENTRIES` is invisible

The spec's error table says "`MAX_TOTAL_BYTES` reached, or `MAX_ENTRIES` hit while counting →
`LOG_INF`" (spec:355). In the plan, the byte cap only sets `totals.capHit` (plan:1072-1075), and the
entry cap sets nothing at all (plan:1101-1104). So the device log cannot tell a 512-entry truncation
from a genuine 512 files. **Fix:** add `LOG_INF(MODULE, "Passage scan stopped at the %u-byte cap", …)`
in the byte-cap branch, and after pass 1 add `if (count == MAX_ENTRIES) LOG_INF(MODULE, "Passage count
capped at %u", …)`.

### MINOR 4 — Task 8 starts without a failing test, though part of it is pure

Task 8 is built, not test-driven (plan:921-922). Most of it is I/O and drawing, which is fair. But the
eligibility rule in `readPassageFileName` (plan:1031-1036) is pure string logic: it requires `.json`,
rejects names starting with `.`, and excludes `*.json.tmp`. That last exclusion is what keeps the
leftover `.tmp` (A3) off the screen. **Fix:** move a `bool isPassageFileName(std::string_view)` into
`StudySleepPick.h`, give it a red/green test (`"bible.json"` yes; `".x.json"`, `"a.json.tmp"`,
`".json"`, `"a.jso"` no), and keep only the `isDirectory`/`getName` I/O in the `.cpp`.

### MINOR 5 — The test's aggregate initialisers emit five `-Wmissing-field-initializers` warnings

Built with the repo's `-Wall -Wextra -pedantic` (`test/CMakeLists.txt:42-46`), `ScriptedRandom
keepFirst{{0, 1, 1, 1}}` and similar lines (plan:302-315) each warn "missing field 'bounds'
initializer". Nothing fails, but it adds noise to CI's host build (`ci.yml:148`). **Fix:** give
`ScriptedRandom` a constructor, `explicit ScriptedRandom(std::vector<uint32_t> v = {}) :
values(std::move(v)) {}`.

### MINOR 6 — A failed completion allocation is silent

`auto completion = makeUniqueNoThrow<study::ChapterCompletion>(); if (completion && loadCompletion(...))`
(plan:1309-1310) drops the strip without a log. CLAUDE.md "Always use `makeUniqueNoThrow`" requires a
`LOG_ERR` on every null allocation. **Fix:** `if (!completion) LOG_ERR(MODULE, "OOM: chapter record;
no progress strip");`.

### MINOR 7 — The PR body file has no stated location

Task 10.5 says "Write the body to a file" (plan:1387) but not where. A file inside the worktree would
be an untracked file outside `FILES:`. **Fix:** name the scratchpad, for example
`$SCRATCH/pr-114-body.md`.

## Verdict rationale

The plan is concrete, internally consistent (the `study_sleep::` names and signatures match from Task 1
through the Task 8 call sites), and its host-tested half compiles and passes as written. MAJOR 1 is a
one-line comment rewording that neither reverses a decision nor changes scope, so it is fixed inline.
The MINORs are small corrections.

VERDICT: CLEAR
