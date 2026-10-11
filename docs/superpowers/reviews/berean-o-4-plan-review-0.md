Tier: standard

# berean-o-4 plan review 0

- Plan: `docs/superpowers/plans/2026-10-11-berean-o-4-plan.md`
- Spec: `docs/superpowers/specs/2026-10-11-berean-o-4-design.md` (after review 1)
- Base: `43c74d24` (`src/` and `lib/` as at `origin/main` `478cf1ed`)

## Summary

The plan is sound. Every spec requirement maps to a step, and every code block compiles against the tree
as it stands. I checked this two ways:

- **Host code.** I built Tasks 1–2's header and tests from the plan text, outside the repo, with the
  suite's own flags.
- **Firmware edits.** I matched each replacement anchor against the source.

The one finding is a MINOR wording slip in an "Expected" line. No BLOCKER or MAJOR.

## What was verified, with evidence

### The anchors exist and are unique

Every "replace this" block in Tasks 3–5 occurs **exactly once** in the base tree:

- `GfxRenderer.h:323-325` and `GfxRenderer.cpp:2269`;
- `EpubReaderActivity.h:14-16` and `:60-62`;
- `EpubReaderActivity.cpp:18-20`, `:1230-1234`, `:1642-1643`, `:1703-1704`, `:1750-1751`, `:1764-1769`
  (six-space indent), `:1814-1819` (eight-space indent), `:1838-1840`, `:1844-1846`, `:1849-1852`, `:1492`,
  `:1610-1615`, and `:287-302` (the `openReaderMenu` handler);
- the insertion point `void EpubReaderActivity::onEndOfBookRendered()`, at `:1517`.

Three anchors depend on an earlier task's text: plan:404, plan:1008 and plan:1139. Each one matches what
that task inserts. None of `pageCache`, `restorePageFromCache` or `pageRestoreRequested` appears in the
base `.cpp`, so the grep counts in Task 4 Step 12 and Task 5 Step 6 are exact.

### Host TDD holds

I assembled the plan's code with `-std=c++20 -Wall -Wextra -pedantic` (`test/CMakeLists.txt:4-6,42-46`)
against the gtest v1.17.0 that the suite fetches:

| Check | Result |
|---|---|
| Task 1 alone (plan:122-201, 214-265) | `[  PASSED  ] 4 tests.` |
| Task 2's appended tests before Step 3 | Fail to compile, as Step 2 says |
| After Step 3 (plan:411-470) | `[  PASSED  ] 12 tests.` |

The plan's code produces no warning. The only warning in the build comes from gtest's own
`gtest-printers.h` and would appear in every suite.

The tests cover the spec's T-1 to T-7 completely:

- T-2's seven single changes, at plan:148-176;
- T-5's four cases plus `shownUnsaved`, at plan:341-361;
- T-7's reset, and the rule that a new page inherits no planes, at plan:363-380.

### The APIs the firmware edits call

| Plan call | Signature in the tree |
|---|---|
| `writeGrayscalePlaneStrip(bool, const uint8_t*, int, int)` | `GfxRenderer.h:330`; it asserts `yStart <= panelHeight - numRows` (`.cpp:2275`), and `getDisplayHeight()` satisfies that |
| `hasFrameBuffer()` | `GfxRenderer.h:351` |
| `getDisplayHeight()` | `GfxRenderer.h:374` |
| `copyGrayscaleBuffers(const uint8_t*, const uint8_t*)` | `HalDisplay.h:88`, `.cpp:102-104` |
| `display` | a reference member (`GfxRenderer.h:44`), so the new `const` pass-through can call a non-`const` HAL method |

The rest of what Tasks 4–5 rely on:

- **`displayWithRefreshCycle(renderer, pagesUntilFullRefresh)`** blocks by default (`ReaderUtils.h:165-177`).
- **`pagesUntilFullRefresh` and `forcedRefreshPending`** are `protected` (`ReaderActivity.h:13-16`).
- **`RenderLock lock;`** is the existing idiom (`EpubReaderActivity.cpp:909-912`, `RenderLock.h:10`).
- **Designated initializers in a call argument** are already used in this file (`EpubReaderActivity.cpp:970-972`).
- **`std::exchange`:** `<utility>` is already included (`:22`).

### Panel behaviour

These are the driver paths the restore replays, read in the SDK:

- **UC8179 ignores `fb` in `displayGray`** (`Uc8179Driver.cpp:517-578`). Its `copyGrayscaleLsb`/`Msb` fold
  the masks against the `_grayBase` snapshot, which `display()` and `transitionGrayscaleBase()` take from the
  B/W framebuffer (`:182-186`, `:269-276`, `:473-515`). The restore loads the same B/W bytes into the
  framebuffer before it calls `display()`, so the driver sees the same sequence as a render.
- **SSD1677 also ignores `fb`** (`Ssd1677Driver.cpp:604`). A full-height strip is
  `480 × 100 = 48,000` B, under its `uint16_t` length (`:595`).
- **The only async display is `renderContents`' own** (`EpubReaderActivity.cpp:1718`), and every branch
  drains it with `waitRefreshComplete()` before returning (`:1753`, `:1779`). So the restore's `memcpy`
  into the framebuffer never races a running refresh.
- **Paper Mono's combined-base path** (`PaperMonoDriver.h:57`) is not linked on the X4 Pro
  (`FreeInkDisplay.cpp:153-175`), so the restore does not need to model it.

### Close decision inputs

`decide()`'s inputs cover every setting the menu changes in place: Night mode (`EpubReaderMenuActivity.cpp:333`),
Frontlight (`:342`, which A-13 allows), the pending orientation and the auto-turn option (`:202-203`).
`toggleAutoPageTurn(0)` returns at once, so an off→off close leaves the section alone
(`EpubReaderActivity.cpp:1094-1097`, `AutoPageTurn.h:14-18`).

### The plan against the brief's questions

- **Spec to step mapping.** G-1 to G-5 and A-1 to A-23 each land in a step. Capture is Task 4: the
  `capturePage` gate is A-6/A-12, and the capture points are A-3/A-4. The record and `fit()` are Task 2
  (A-5, A-8). PSRAM and the latches are Task 3 (A-9). The decision, the flag and the restore are Task 5
  (A-11, A-14 to A-22). All four log lines of A-23 have a step. The PR section carries G-5 and V-1 to V-9.
  The spec puts the `Page cache:` line inside `renderContents`; the plan puts it in `renderBook` after
  `Rendered page in`. That is equivalent, because `renderContents` has a single caller (`:1491`).
- **Consistent names and signatures.** Each signature Task 3 declares matches what Tasks 4–5 call:
  `captureBw`, `captureGrayPlane(bool, const uint8_t*, size_t)`, `noteGrayShown`, `captured`, `bw`,
  `grayLsb`, `grayMsb` and `copyMillis`. The same holds for the `CapturedPage` methods and fields that
  Tasks 2–3 share.
- **No placeholders.** Every edit is shown as full replacement text.
- **Failing test first.** Only Tasks 1–2 can start with a failing test. Tasks 3–5 are firmware-only, which
  the spec's testing strategy sets out (`PageFrameCache` is not host-built; the record's state changes are
  covered by T-5/T-7). Each of those tasks is gated on a `pio run`, and Tasks 4–5 also on a grep count.
- **Each task can be committed.** Task 3 adds code nothing calls yet. Task 4 captures but does not restore.
  Task 5 completes the feature. Each one builds.
- **File lock.** Plan:28-31 are `FILES:` lines at column 0, outside any code fence, with repo-relative
  paths and no globs. They name all nine files the plan touches. `build/`, `.venv/` and `.pio/` are
  gitignored (`.gitignore:13`, `:18`). Task 6 Step 4 checks the diff against those nine files.

## Findings

### MINOR

**M-1. The expected compiler error in Task 2 Step 2 is worded differently from what a compiler prints.**

- **Claim.** plan:398 expects an error "such as `no type named 'CapturedPage' in namespace 'MenuCloseRestore'`".
- **Problem.** No compiler prints that. Clang reports a `using`-declaration of an unknown name as
  "no member named …", and GCC as "'CapturedPage' has not been declared in 'MenuCloseRestore'". The step
  still fails as intended, and "such as" hedges the text. But an implementer comparing the output literally
  may stop to wonder.
- **Evidence.** Building plan:122-201 plus plan:295-392 against the Task 1 header gives
  `error: no member named 'CapturedPage' in namespace 'MenuCloseRestore'` (Apple clang, host suite flags).
- **Fix.** Change plan:398 to: "FAIL to compile: `CapturedPage`, `Fit` and `Position` are not members of
  `MenuCloseRestore` yet (clang: `no member named 'CapturedPage' in namespace 'MenuCloseRestore'`)."

## Verdict

CLEAR. M-1 can be fixed inline.

VERDICT: CLEAR
