# Issue #108 plan review, pass 1

Plan: `docs/superpowers/plans/2026-09-26-issue-108-plan.md`.
Spec: `docs/superpowers/specs/2026-09-26-issue-108-design.md` (including its "Amendment from plan review 0").
Previous pass: `docs/superpowers/reviews/issue-108-plan-review-0.md` (BLOCKER 1, MINOR 1–4).

## How this was checked

I executed the plan rather than only reading it. All work ran on a scratch copy outside the worktree; nothing in the worktree changed.

- **Find texts.** A script walked the plan in order, tracked the file named by each `Edit`/`Create` line, and applied every Find → Replace block literally. All 70 find blocks matched with the expected count. The block at `plan:433-454` matched exactly twice, as the plan says. The others matched once. Nothing had to be adjusted by hand.
- **Step 1 (characterisation).** `DitherersTest` on the unchanged header gave `[  PASSED  ] 3 tests.`
- **Steps 2–12 applied.** `DitherersTest` gave `[  PASSED  ] 7 tests.` The full ctest run gave `100% tests passed out of 858`. Some suites failed gtest discovery on the first build. That is the known first-build gtest race; step 1.5 already warns about it, and ctest is green afterwards.
- **Firmware (the pass-0 BLOCKER).**
  - `pio run -e x4pro-gh_release` gave `SUCCESS`.
  - `pio run -e x4pro` gave `SUCCESS`. The first x4pro attempt failed with `Arduino.h: No such file or directory` in files this plan does not touch, straight after `*** Reinstall Arduino framework ***`. That was a shared-package reinstall racing the build, not the plan. The rerun succeeded unchanged.
  - Neither log shows a warning in any touched file.
- **Verification greps.** I ran every grep the plan specifies (2.4, 4.5, 6.5, 8.14, 11.3, 13.1a/b) and compared the output with the plan's stated expectation. One does not match (MINOR 1).
- **Inventory.** I grepped the unmodified tree for bare new-expressions. Every non-comment hit is one the plan converts: BitmapHelpers ×8, Bitmap.cpp ×2, PngToBmp ×5, Page ×2, Epub ×4, ImageDecoderFactory ×2, the parser ×7, HalFile ×2, the web server ×3, Section ×1 and DNSServer ×1. After the plan, only the comment the plan predicts is left.

## Pass-0 fixes: applied correctly and completely

| Pass-0 finding | Where addressed | Status |
|---|---|---|
| BLOCKER 1, `<Memory.h>` in `BitmapHelpers.h` | `plan:342-369`, rows at `:390-394`, `:419-423`, `:499-503`; build at 2.4b `:572-581`; spec amended `spec:58`, `spec:290-295` | Fixed. `<new>`/`<memory>`/`<utility>` are included, and rows are `new (std::nothrow) int16_t[n]()` in the member-initialiser list. The init order matches the declaration order (`width, rowCount, errorCurRow, errorNextRow`, `BitmapHelpers.h:316-320`), so there is no `-Wreorder`. Both firmware envs build. The D1 seam still intercepts the rows: the OOM tests fire at N=1..3/1..2 and not at N+1. |
| MINOR 1, dead `if (!server)` | `plan:1709-1738` | Fixed. Only the allocation line changes, and the existing check at `CrossPointWebServer.cpp:126` becomes the live path with `OOM: WebServer`. |
| MINOR 2, WebDAV path skips `stop()` | `plan:1782-1785` | Fixed. The reason is stated (`server->begin()` at `:187` has not run at `:184`). |
| MINOR 3, step 4.5 expected output | `plan:827-832` | Fixed. The actual grep output matches it exactly: pairs at 521/558/566/622, the rowBuffer-branch trio at 628-630, and the comment at 811. |
| MINOR 4, local-only `test/CMakeLists.txt` | `plan:16-20` | Fixed. The note sits beside the `FILES:` lines. |

## Spec coverage

Every spec row maps to a step: row 1→2, 2→3, 3→4, 4/A3/A7→5, 5→6, 6→7, 7–8/A1/A2→8, 9–10→9, 11→10, 12–13/A4/A5→11, 14/A6→12. The Testing strategy items and D1 conditions 1–4 map to steps 1–2. The PR notes (CMake line, device checks, NG2 follow-ups) are at 14.3. Names stay consistent from step to step: `valid()`, `markAllocationFailed`, `allocationFailed_`, `ScopedNthNothrowFailure`, `fired()`.

I also traced the parser's post-failure paths against the source, because a null `currentPage` after a failed allocation is the main risk:

- `addLineToPage` and `makePages` return first while the flag is set (`plan:1411`, `:1482`).
- The footnote fallback in `makePages` is already guarded (`ChapterHtmlSlimParser.cpp:1732`).
- `emitHorizontalRule` re-allocates a null page before dereferencing it at `:364`.
- The image branch handles a null page at `:768-790` before `:822`.
- `finishParse` returns before `completePageFn` (`plan:1370-1374`). By then the XML parser and file are already released (`:1621-1626`), so the early return leaks nothing.

`finalizeBuild` → `abandonBuild()` → `return false` is consistent with `buildSomeMore`'s `Error` handling (`Section.cpp:459-462`).

The plan has no `FILES:` omissions. Every path touched in steps 1–13 is on a column-0 `FILES:` line at `plan:7-14`, either as a repo-relative path or under `test/ditherers/`. The only exception is `test/CMakeLists.txt`, which is deliberate, never staged, and noted at `plan:16-20`.

Steps 3–12 have no failing test first. That follows from the spec (`spec:222-224`: the parser, Section, Epub, reader and web server are in no host build), and the plan says so openly (`plan:33-36`). It is not a plan defect.

---

## MINOR 1. Step 8.14's grep does not print "nothing"

**Claim.** `plan:1537-1541` says the grep `reset(new\|new Page\|new ParsedText\|new (std::nothrow) Page` should print nothing.

**Problem.** The alternative `new (std::nothrow) Page` is a prefix of `new (std::nothrow) PageHorizontalRule` and `new (std::nothrow) PageImage`. Both sites are deliberately kept (NG3). On the applied tree the grep prints:

```
398:      new (std::nothrow) PageHorizontalRule(width, ruleThickness, xPos, currentPageNextY));
835:                    std::shared_ptr<PageImage>(new (std::nothrow) PageImage(imageBlock, xPos, self->currentPageNextY));
```

The plan's own ground rule (`plan:24-26`) is "stop and re-read" whenever reality does not match. A literal implementer would stop here on a correct tree. The sentence that follows mentions these sites, but only as "stay", so it does not reconcile them with "Expected: nothing".

**Fix.** Change the last alternative to `new (std::nothrow) Page()`, or keep the pattern and change the expectation to "exactly two lines: the `PageHorizontalRule` and `PageImage` sites (NG3)".

## MINOR 2. The `Page.cpp` OOM messages do not use A8's `OOM: <what>` format

**Claim.** Step 7 logs `"Deserialization failed: could not allocate PageImage"` / `"… Page"` (`plan:1109`, `:1126`).

**Problem.** Spec A8 (`spec:183-185`) fixes the message format as `OOM: <what>`. The spec's device check 1 and the PR body (`plan:1964-1965`) use "no `OOM:` lines in the serial log" as the signal. A `Page` OOM would not match that search. The plan copied the model site's wording (`Page.cpp:48`), which is a defensible local convention. Still, it is the only new allocation site that departs from A8.

**Fix.** Use `LOG_ERR("PGE", "OOM: PageImage")` and `LOG_ERR("PGE", "OOM: Page")`.

## MINOR 3. The plan's pass-0 changelog contradicts the amended spec

**Claim.** `plan:1986-1987` says the change "departs from the wording of spec row 1 (\"from `makeUniqueNoThrow<int16_t[]>`\")".

**Problem.** The same commit amended spec row 1 (`spec:58`) and added "Amendment from plan review 0" (`spec:290-295`). The plan and spec now agree, so the sentence is stale. A later reviewer may go looking for a spec deviation that no longer exists.

**Fix.** Reword it to "Spec row 1 was amended to match (spec, Amendment from plan review 0)."

---

No BLOCKER or MAJOR findings. The pass-0 BLOCKER is fixed and verified by building both firmware envs. The plan applies verbatim, and all 858 host tests pass.

VERDICT: CLEAR
