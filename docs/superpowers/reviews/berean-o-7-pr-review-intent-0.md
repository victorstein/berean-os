Tier: heavy

# berean-o-7 (R22) PR review, intent, pass 0

- PR: #263, `perf: skip the home verse scan on a same-day wake`. Base `main`, head `c3ce0660`.
- Measured against:
  - the brief (`hpipe brief --task t7`);
  - the spec, `docs/superpowers/specs/2026-10-11-berean-o-7-design.md`;
  - the plan, `docs/superpowers/plans/2026-10-11-berean-o-7-plan.md`.
- Scope reviewed: the ten commits `fd2868eb..c3ce0660` on top of the `origin/main` merge
  `a07f1a63`, as `git diff a07f1a63..HEAD`. Main's merged commits were not reviewed.

## How this was checked

- **Plan conformance.** I extracted the plan's final code blocks and diffed them against the
  branch. `HomeVerseDoc.cpp` (plan Task 5 Step 3), `HomeVerseFile.{h,cpp}` (Task 7),
  `HomeVerse.{h,cpp}` (Task 9) and `HomeVerseFileTest.cpp` (Task 7) are byte-identical. The only
  difference is the fence line. `HomeVerseDoc.h` is plan Task 2's header plus Task 6's budget block,
  verbatim.
- **Host tests.** I built them in a scratch directory: `cmake -S test -B <scratchpad>/build`, then
  `HomeVerseDocTest`, `HomeVerseFileTest`, `HomeLayoutTest` and `SdPathsTest`. Results: 14/14, 8/8,
  42/42 (with `HomeVerseCache.*`, unchanged) and 8/8, all passing. I did not run a firmware build,
  as instructed.
- **Budget.** I recounted it by hand:
  - The wrapper literal in `HomeVerseDoc.h:21` is 106 bytes.
  - `d:255:65535:65535:4294967295` is 28 bytes.
  - The sum is 3,943, which matches `HomeVerseDocTest.cpp:232` and the PR body.
  - ArduinoJson's `TextFormatter::writeChar` (in the fetched 7.4.2 source) escapes at most 2
    bytes per character, except NUL, which a C string cannot hold. So the `"` filler in
    `TheLargestEntryFitsTheBudget` (`HomeVerseDocTest.cpp:247-252`) is the real worst case, and the
    factor of 6 is a safe overestimate.
- **Wake path.** I re-read every link of the same-day wake:
  - `LauncherActivity.cpp:148`: `versePending = !biblePath.empty() && !HOME_VERSE.isCurrent()`.
  - `:84`: `onEnter` paints when nothing is pending.
  - `:288-306`: `drawVerse` draws the pick.
  - `:439-447`: the loop's scan-and-repaint runs only while pending.
  - `:441`, `:445`, `:479`, `:483`: the only other `requestUpdate` callers are the pending branch
    and button navigation. So on a same-day wake nothing else paints Home a second time.
  - `main.cpp:591-592`: a wake routes to `goHome(boot.needsWakeRefresh)` through `BootRoute::Home`.
- **Device log lines.** The lines the PR tells the owner to look for are real and enabled:
  - `x4pro` sets `-DLOG_LEVEL=2` with `ENABLE_SERIAL_LOG` (`platformio.ini:173-174`), so `LOG_DBG`
    prints (`lib/Logging/Logging.h:57-60`).
  - `LauncherActivity.cpp:51` is `MODULE = "LAUNCH"`.
  - `HomeVerse.cpp:99-100` produces `Verse restore: %s, current %d, %lu ms`, with the words from
    `HomeVerseFile::describe` (`HomeVerseFile.cpp:66-84`).
- **Build identity.** `esp_app_get_elf_sha256` in `framework-espidf/.../esp_app_desc.c:107-116`
  copies `MIN(size, sizeof(str))` and terminates, so a 9-byte buffer receives 8 hex digits.
- **Discovery.** `t7/x1` ("Precompute tomorrow's Home verse…") is recorded (`hpipe discoveries`).

## Brief acceptance criteria

| Criterion | Status | Evidence |
|---|---|---|
| A same-day wake logs no `Verse scan` | Met | `HomeVerse.cpp:72-76`: the first `isCurrent()` restores, and `validFor` then holds. `LauncherActivity.cpp:148` keeps `versePending` false, so `ensure()` and its `scan()` (`HomeVerse.cpp:150`, the only `Verse scan` line) never run. |
| A new day or a changed file still rescans | Met | The key is unchanged (`HomeVerseCache.h:40-43`, `:65`). A restored entry carries its stored key (`HomeVerseDoc.cpp:130-151`), so a new day or size gives `validFor` false and `ensure` scans (`HomeVerse.cpp:78-85`). Pinned by `HomeVerseDocTest.cpp:164-170`. "Changed file" means a changed size, which is the brief's own "same key `HomeVerse` uses now" and the spec's stated non-goal (spec :49-50). |
| Before/after wake → settled figure and the log lines that show it | Met | The PR body's "Before and after" gives 3,126 / 4,427 ms, citing `responsiveness-baseline.md:51-52`, which I re-read. It predicts ≈3.1–3.2 s and labels that a prediction, not a measurement. "How to verify" step 3 names the proving line and the `Wait complete` line that marks settled. |
| Host tests for the key and the persisted format, beside the existing home verse tests | Met | `test/home_layout/HomeVerseDocTest.cpp` (14 tests) and `HomeVerseFileTest.cpp` (8), beside `HomeVerseCacheTest.cpp`, plus the layout pin `HomeLayoutTest.cpp:225-233`. |
| "Say whether" the second paint goes away | Met | PR body, "What this does": Home paints once with the verse included. The mechanism is above. |
| Storage discipline for a new file | Met | Atomic write (`HomeVerseFile.cpp:63`). Budget and `overflowed()` checked before writing (`:53-62`). A version a future build refuses (`HomeVerseDoc.cpp:132`, tested at `HomeVerseDocTest.cpp:172-180`). The file is under `/.berean/` (`SdPaths.h:27`, `:54`). The owning task is named (`PersistableStore.h:110`). |
| Shared append points untouched; "For the orchestrator" present | Met | Neither `test/CMakeLists.txt` nor `src/main.cpp` is in the diff. The new executables are in `test/home_layout/CMakeLists.txt`, which `test/CMakeLists.txt:129` already adds. |
| PR title, base, trailer | Met | `perf: …`, lowercase and imperative, with base `main`. The body ends with the session link and then `Refs berean-o-7`. |

## Spec requirements

Every assumption A1–A15, the architecture table, the error table, the logging lines and both test
lists are implemented. The plan's self-review table maps each one to a task, and I spot-checked
each mapping against the code:

- **A4.**
  - Identity read: `HomeVerse.cpp:40-42`.
  - Unusable-id guard on both sides: `HomeVerseFile.cpp:25`, `:49`, and `HomeVerse.cpp:95-98`,
    `:107`.
  - Theme pin: `HomeLayoutTest.cpp:225-233`.
  - The repo has only two themes (`UITheme.cpp:32`, `:42`), so the pin covers every theme.
- **A6.** `restoreAttempted` gates the restore, which runs before any scan in both entry points
  (`HomeVerse.cpp:74`, `:80`, `:88-89`).
- **A7.** `scan()` returns false on both OOM paths (`HomeVerse.cpp:127-132`, `:154-160`) and true
  on the three completed paths (`:167`, `:177`, `:185`). `save()` has one call site (`:83`).
- **A10.** Type checks and the cross-field rule are at `HomeVerseDoc.cpp:49-62`, `:64-95` and
  `:143`. Tests 7, 10 and 11 of the spec's list are at `HomeVerseDocTest.cpp:191-229`.
- **A12.** The adopting read is `HomeVerseFile.cpp:28`, tested at `HomeVerseFileTest.cpp:66-78`.
- **A13.** Both documents use `PsramJsonAllocator::json()` (`HomeVerse.cpp:94`, `:110`). The host
  suite passes `nullptr` (`HomeVerseFile.cpp:16-18`).

**Departures from the spec.** The plan declares five ("Deviations from the spec's sketch",
plan :78-97), and all five are implemented as declared: `const char*` ids, `NoBuildId`, a bytes
return, the width/height-only pin, and the identity log on its own line.

There is one undeclared difference in mechanism. The spec's table calls for `is<uint32_t>()` plus
separate range checks (spec :286). The code uses the narrower `is<uint16_t>()` and `is<uint8_t>()`,
which in ArduinoJson range-check by themselves (`HomeVerseDoc.cpp:51-52`, `:66-67`). The behaviour
is the spec's: `s` 65536 is refused (`HomeVerseDocTest.cpp:214`). The plan's own code has it too,
so the PR and the plan agree. Not a finding.

**Scope.** Nothing outside the spec's file table is touched. The files beyond the original lock
(`SdPaths.h`, the `PersistableStore.h` comment, `test/home_layout/`, `test/sd_paths/`,
`docs/file-formats.md`) are:

- declared in spec :453-472;
- in the task's current file lock (`hpipe show --task t7`);
- the reason the tier was raised to heavy.

`StudySleepScreen` and `test/study_sleep_pick/` are untouched, as spec :41-43 says.

**Tests check the behaviour.**

- The golden documents (`HomeVerseDocTest.cpp:100-120`) pin the on-card contract.
- The round-trip and `validFor` cases test what a restore answers. `.tmp` adoption and the
  replace-after-refusal path go through the real `PersistableStore.cpp` against `HalStorageFake`.
- The only tests that restate the code are:
  - the `3943u` pin, which is the repo's `PlacesDocTest` idiom;
  - `EveryOutcomeHasALogWord`, which pins the log vocabulary the device steps depend on.
- `HomeVerse.cpp`'s gate has no host test. The spec chose and justified that (spec :432-435),
  because the scan needs `GfxRenderer`.

**Process.** The one TDD gap (Task 4) is disclosed in the PR body, with how the red run was
reproduced afterwards.

## Findings

### MINOR 1: the PR body does not say that the sleep-side `Study pick` is unchanged

**Evidence.**

- The brief's Goal names two parses of the same file: `[SLP] Study pick` at sleep (445 ms) and
  `[HOMEV] Verse scan` at wake (405 ms). The bead's title is "parse the verse file once per sleep
  cycle".
- The spec keeps the sleep pass on purpose, because it is a different, random pick (spec :41-43).
- The PR body states the other non-goal ("The first wake of each morning is unchanged"), but says
  nothing about the sleep-side parse. Its "Before and after" gives only wake figures.

**Why it matters.** A reader of the PR alone cannot tell whether the 445 ms at sleep was addressed.
What the PR delivers is this: on a same-day cycle the file is parsed once, at sleep; on a new-day
cycle it is parsed twice, as before. The baseline's "Sleep ~2.9 s" row (`responsiveness-baseline.md:53`)
is unchanged.

**Fix inline.** Add one line under "Before and after", for example: "Sleep is unchanged:
`Study pick` still parses `bible.json` (445 ms). It is a different, random pick (spec Non-goals),
so a same-day sleep-and-wake cycle now parses the file once instead of twice."

## Assessment

The PR does what the brief asked, in the way the cleared spec and plan said it would, and the code
matches the plan's code exactly. Every acceptance criterion is met. The figures are honest:
"before" is cited, and "after" is labelled a prediction.

No requirement is reduced silently. The two non-goals, the morning wake and same-size edits, come
from the brief's own acceptance criterion and key, and one of them is filed as `t7/x1`. Nothing
beyond the spec was added.

The single MINOR is one missing sentence in the PR body. It reverses no decision and needs no
owner judgment.

VERDICT: CLEAR
