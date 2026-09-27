Tier: heavy

# Issue #114 PR #167 review: intent, pass 0

Reviewed `feat/114-study-sleep-screen` at `2530e300` against issue #114, the spec
`docs/superpowers/specs/2026-09-27-issue-114-design.md` (A1–A20) and the plan
`docs/superpowers/plans/2026-09-27-issue-114-plan.md`. Decision d1 is settled and was not reopened.

## Method

- Read the issue, the PR body, the spec, the plan's task list and its change log, and plan review 0.
- Read every changed source and test file in full: `StudySleepPick.h`, `StudySleepScreen.{h,cpp}`,
  the `SleepActivity.cpp` hook, the `CrossPointState` / `CrossPointSettings` / `SettingsList` hunks,
  both YAMLs, both test suites and `docs/file-formats.md`.
- Extracted the plan's Task 8 code block (plan lines 980–1393) and diffed it against
  `StudySleepScreen.cpp`. The only differences are clang-format line wrapping at three sites.
- Grepped every `SETTINGS.sleepScreen` consumer (`main.cpp:259`, `SettingsActivity.cpp:368`,
  `BmpViewerActivity.cpp:70,197,227`, `SleepActivity.cpp:502,510,535,657,811`). None needs a `STUDY`
  branch: each one tests for a specific other mode, and `renderCoverSleepScreen` is not reached from
  `STUDY`.

## Issue acceptance criteria

| Issue item | Where | Status |
|---|---|---|
| Date line, offset applied, omitted when the clock was never set | `StudySleepScreen.cpp:185-195`, `StudySleepPick.h:194-215` | Met. The line is also dropped when `getTime` fails |
| Snippet in the largest Noto Serif italic, centred and wrapped, under an opening quote | `StudySleepScreen.cpp:283-315` | Met. The quote is black, not grey (1-bit panel), listed under Known limits |
| Reference in bold, or the publication title when there is none | `StudySleepScreen.cpp:316-320` | Title fallback dropped under decision d1, stated in the PR body's Deviations and Known limits |
| First tag as an outlined pill, named from `tags.json`, omitted when unlabelled | `StudySleepScreen.cpp:105-112,197-211,321-328` | Met |
| 66 spines, height scaled by chapter count, filled by `readCountInBook`, finished books solid, "N / 1189" plus the finished count | `StudySleepScreen.cpp:236-270` | Met |
| BereanMark and "Berean" at the foot | `StudySleepScreen.cpp:334-335` | Met |
| Uniform reservoir sampling (1/k) with `esp_random` | `StudySleepPick.h:98-109`, `StudySleepScreen.cpp:83` | Met. Arduino `random()` is `esp_random() % k` on this core (spec A8) |
| One document in memory at a time, the whole store never loaded | `StudySleepScreen.cpp:134` (a local in `offerFile`) | Met. There is no ArduinoJson filter; the PR body gives the reason |
| Recent ring in `CrossPointState`, keyed on a stable id (not array position), recent passages skipped while others remain | `CrossPointState.h:29-33`, `StudySleepPick.h:36-43,70-77,105-113` | Met. The key also covers the end unit |
| Caps on files and bytes, falling back to default | `StudySleepScreen.cpp:41-45,121-129,154-160` | Met with a stated refinement: when the byte cap is hit, the pick made so far is kept |
| `STUDY` appended after `TRANSPARENT_CUSTOM`; the default is unchanged | `CrossPointSettings.h:64`, `SettingsList.h:242` | Met |
| `STR_*` for the option, weekday and month names, in en and es | both YAMLs (tail); `STR_MONTHS_SHORT` reused | Met |
| No passages → default screen; an unreadable store → `LOG_ERR`, default; never write to the store | `SleepActivity.cpp:548-550`, `StudySleepScreen.cpp:136-139,150-158` | Met. Unreadable files are skipped one by one (spec error table), and the only write is `state.json` |
| Host tests: uniform draw, repeat skip, the lone passage shown again, date formatting including the unset clock | `StudySleepPickTest.cpp:119-160,233-275` | Met |
| Device: repeated sleeps, heap logged | `StudySleepScreen.cpp:342-355`; PR device checklist | Met (flagged for the human tester) |

## Spec coverage

Every assumption A1–A20 is implemented as written:

- **A3.** It reads `readDocFromFileStreamed` / `readDocFromFileChecked` and never calls a `*File::load`.
- **A5.** It applies the `unitFromCompact` gate, the fallback from `e` to `u`, the `x` requirement,
  `utf8SafeSummary` clamps and the tag range.
- **A6.** It runs a counting pass, then a cyclic pass from a random start.
- **A9.** It uses two reservoirs with a strictly-greater, first-seen tie-break.
- **A10–A11.** It keeps a uint32 ring with a push after the draw, and there is no format bump.
- **A12–A13.** The date shift clamps `q` at 104.
- **A17.** A `Missing` completion file gives an empty strip, while `Unreadable` or a rejected file gives no strip.
- **A18.** `clearScreen()` comes first and the screen is not inverted.
- **A19.** The candidates are heap-held, with `static_assert`s on the capacities.

Spec tests 1–8, including 4a, 5a and 5b, each have a matching test. `CrossPointState` has its own
round-trip, wraparound and corrupt-cursor tests (`CrossPointStateTest.cpp`).

Scope expansion: none. The only files outside the spec's Architecture list are the
`test/crosspoint_state` suite, which is in the plan's FILES list, and the `docs/file-formats.md` note,
which is plan Task 9.

Plan divergence: the plan wrote `wordAt` inline in `StudySleepPick.h` (plan line 659). The PR uses
`catalog::wordAt` from `lib/Catalog/Catalog/CatalogLabel.h`, which landed on main in the meantime. The
PR body explains this under "Adjusted to the tree", and `test/study_sleep_pick/CMakeLists.txt` links
`CatalogLabel.cpp` to match. There is no other divergence.

The tests exercise behaviour rather than restating the code. For example, the sampler tests assert
which snippet wins for a given RNG script and the bounds the RNG was asked for, and the date tests
assert rendered strings across year and leap-day rollovers.

## Findings

### MINOR 1: the entry cap bounds only eligible files, not the directory walk

The issue asks for bounded work on the way to sleep, and spec A6 says `MAX_ENTRIES = 512` guards "a
pathological directory". Both passes count only entries that pass `pubKeyFromFileName`:

- The counting loop's guard is `count < MAX_ENTRIES` (`StudySleepScreen.cpp:154-156`), and `count`
  goes up only for eligible names.
- The second pass is bounded by `index < count` (`:169-172`), with `index` also going up only for
  eligible names.

A directory full of non-`.json` entries is therefore walked end to end, twice. In practice
`/.berean/passages/` holds `*.json` plus at most one `*.json.tmp` per publication, so the
exposure is theoretical.

Fix inline, optionally: count every visited entry against a separate walk cap. It is not needed for
correctness.

## Verdict rationale

Every acceptance criterion in the issue is met, or deviates for a reason the PR states:

- the title fallback (d1);
- no filter, because the reader takes none;
- keeping the pick when the byte cap is hit, because the random start makes it a uniform draw over a
  window;
- a black quote mark, because the panel is 1-bit.

The implementation matches the plan's code line for line apart from formatting. There is no scope
creep or silent reduction. The one finding is a theoretical MINOR.

VERDICT: CLEAR
