# PR #122 intent review, pass 0 (issue #108)

Reviewed: `gh pr diff 122` at head `a0796e6a`, against issue #108, the spec
`docs/superpowers/specs/2026-09-26-issue-108-design.md` and the plan
`docs/superpowers/plans/2026-09-26-issue-108-plan.md`.

## Issue acceptance criteria

Issue #108 asks for two things: convert the input-sized and large allocations to
`makeUniqueNoThrow` with a `LOG_ERR` and a failure return, and triage the remaining
`std::make_unique` calls.

| Issue site | Result |
|---|---|
| `BitmapHelpers.h` ditherer rows | Nothrow `new[]` into `unique_ptr<int16_t[]>` plus `valid()` (`lib/GfxRenderer/BitmapHelpers.h:33-37,106-114,206-213`). `makeUniqueNoThrow` is not used because of the include-path constraint recorded in the spec amendment and at `BitmapHelpers.h:9-11`. The allocation is the same one `makeUniqueNoThrow<int16_t[]>` makes (`lib/Memory/Memory.h:33`). |
| `PngToBmpConverter.cpp:632-649` | `makeUniqueNoThrow`, `valid()`, `LOG_ERR("PNG","OOM: …")`, `return false` (`lib/PngToBmpConverter/PngToBmpConverter.cpp:639-673`). `ScopedCleanup` covers the three remaining mallocs (`:627-631`). |
| `Bitmap.cpp:171-173` | Degrades to undithered output (A3) (`lib/GfxRenderer/Bitmap.cpp:165-179`). |
| `EpubReaderActivity.cpp:1054` | `showBuildError()` on null (`src/activities/reader/EpubReaderActivity.cpp:1055-1060`). |
| `Epub.cpp:362,364,383,495` | All four sites (`lib/Epub/Epub.cpp:363-369,388-392,504-508`). |
| `ChapterHtmlSlimParser.cpp:234,325,774,782` | All four, plus `:1667,1676,1704` and the two existing nothrow sites (A2), using the sticky flag. |
| `Page.cpp:79,172` | Both (`lib/Epub/Epub/Page.cpp:80-85,177-181`). |
| `CrossPointWebServer.cpp:112,191`, `DNSServer*` | Both. The `WebDAVHandler` at `:184` is also done (`src/network/CrossPointWebServer.cpp:185-206`). `DNSServer` is a `unique_ptr` (`src/activities/network/CrossPointWebServerActivity.cpp:35,235-243`). |
| `QrUtils.cpp:36` | `makeUniqueNoThrow<uint8_t[]>` and an early return (`src/util/QrUtils.cpp:37-41`). |

I checked completeness independently. A search of `lib/` and `src/` for new-expressions
without `std::nothrow` finds only comments and string literals, so no bare `new` remains in
firmware code. The issue's triage request is met by spec NG1: 69 fixed-size calls were
classified and left alone, and the reason is recorded.

## Spec requirements

The table rows 1–14 all ship, including the harder parts:

- **Sticky guard.** `addLineToPage` and `makePages` both return early on the flag
  (`ChapterHtmlSlimParser.cpp:1693-1695`, `:1739-1741`).
- **Parse status and begin.** `parseStep()` reports `Error` (`:1627-1629`), and
  `beginParse()` returns false when the root block fails (`:1572-1574`).
- **Trailing page.** `finishParse()` returns before `completePageFn` (`:1656-1660`).
  `Section::finalizeBuild` abandons the build on that result (`lib/Epub/Epub/Section.cpp:632-636`).
- **`startNewTextBlock`.** It allocates into a local and swaps only on success
  (`ChapterHtmlSlimParser.cpp:335-343`), as the spec requires.
- **Unguarded dereferences.** I checked every `currentPage->` in the parser (`:382,403,840,1727,1734,1782`).
  Each one sits behind an allocation it checks, or behind a `currentPage &&` guard.
- **Synchronous path.** `parseAndBuildPages()` already handles `Error` and returns
  `finishParse()`, so it inherits the new failure.
- **`PageImage` failure.** A failed `PageImage::deserialize` reaches its caller, which already
  returns null (`Page.cpp:207-211`).
- **A4.** On a `WebSocketsServer` failure the start calls `stop()` and then `reset()`. On a
  `WebDAVHandler` failure it calls only `reset()`, because `server->begin()` has not run yet.
  The plan explains that divergence from the spec (plan Step 11.1, after the code blocks).

Assumptions A1–A10 are all reflected. A7 (dead `Bitmap` members) and the `ScopedCleanup` in
the PNG converter both appear in the spec, so they are not scope expansion. No
on-disk format, I18n string or cache version changed (NG4).

## Tests

`test/ditherers/` exercises behaviour, not the implementation:

- **Golden test.** It was committed in `ab1c1582`, before the refactor in `aed844b4`, and its
  expected vectors have not changed since (`git diff ab1c1582 HEAD` touches only added tests).
  It runs 4 rows, enough to cycle the three-row rotation that `std::swap` replaced, and it
  runs across `reset()`.
- **OOM test.** It fails each row in turn and asserts `fired` and `!valid()`. It also asserts
  that no more allocations are made than there are rows (`DitherersTest.cpp:69-82`), so a
  silently skipped injection cannot pass.
- **D1.** The seam meets all four conditions: both nothrow forms and every delete form are
  replaced (`FailingNothrowNew.cpp`), it is disarmed by default through an RAII guard, it is
  confined to the suite, and the suite builds with `-fno-builtin`.

The parser, web server and reader paths are covered only by the firmware build and review.
That is the spec's stated strategy (Testing strategy, "Not host-testable"), so it is not a
silent reduction.

`test/CMakeLists.txt` is not edited, so CI does not run the new suite until the orchestrator
appends `add_subdirectory(ditherers)`. The spec and plan both require this, and the PR body
states it, so it is not a finding against this PR.

## Findings

### MINOR 1: orphaned comment above `markAllocationFailed`

`lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:287-288`. The existing comment
`// start a new text block if needed` described `startNewTextBlock`. The new helper was
inserted between them (plan step 8.3 used the function signature as its find anchor), so the
comment now sits above `markAllocationFailed`. Fix it inline: move the helper above the
comment, or drop the comment, since it restates the name of the function below it.

No BLOCKER or MAJOR findings. Every issue site and every spec row is implemented. Nothing
in scope was dropped silently, and nothing was added beyond the spec. The one divergence
from the plan-described spec, the A4 WebDAV teardown, is explained in the plan.

VERDICT: CLEAR
