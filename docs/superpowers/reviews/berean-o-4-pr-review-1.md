Tier: standard

# PR review 1 — berean-o-4 (PR #258): close the reader menu without re-rendering the page

Reviewed against the brief (`hpipe brief --task t4`), the spec
(`docs/superpowers/specs/2026-10-11-berean-o-4-design.md`) and the plan
(`docs/superpowers/plans/2026-10-11-berean-o-4-plan.md`). The code diff is nine files:
`MenuCloseRestore.h`, `PageFrameCache.{h,cpp}`, `EpubReaderActivity.{h,cpp}`, `GfxRenderer.{h,cpp}`, and
`test/ui_layout/{MenuCloseRestoreTest.cpp,CMakeLists.txt}`. The `CHANGELOG.md`, `version.txt`, `platformio.ini`
and manifest lines in `git diff main...HEAD` come from the merge of `main` (release 1.29.10), not from this PR.

I also read the code the change depends on: `renderBook`, `renderContents`, `openReaderMenu`,
`reopenReaderMenu` and `loop` in `EpubReaderActivity.cpp`; `ReaderActivity::render`; `ActivityManager`'s
pop and render task; `EpubReaderMenuActivity`'s close paths and in-place actions; `FreeInkDisplay`'s gray
entry points; and `Uc8179Driver`'s `copyGrayscaleLsb`, `copyGrayscaleMsb`, `displayGray` and cleanup.

What I ran:
- `gh pr checks 258`: every check passes (Build x4pro, unit-tests, cppcheck, clang-format, title).
- `ctest -R MenuCloseRestore`: 12/12 pass locally.
- `pio run -e x4pro-gh_release` on HEAD: SUCCESS, no compiler warning in the changed files.

## Intent

**Acceptance criteria**

- **AC1, no `bw_render`/`gray_lsb` re-render on a quiet close: met in code.**
  - A cancelled close with nothing changed sets `pageRestoreRequested` (`EpubReaderActivity.cpp:304-315`).
  - `renderBook` returns from `restorePageFromCache()` before the margins, the section work, `loadPage` and
    `renderContents` (`:1252-1257`). So neither `Page render:` nor `Rendered page in` is logged.
  - The restore logs its own line, whose field names avoid `bw_render` and `gray_lsb` (`:1585-1586`).
  - The PR body gives the baseline line (1,160 ms, n=6), the exact `Page restore:` line that will carry the
    after figure, and what to flash and do. It says plainly that the after figure is not measured yet. The
    brief's shared context requires exactly this ("Never claim a device result you have not seen").
- **AC2, every menu action still re-renders: met.**
  - `decide()` returns `Action` for any non-cancelled result before it looks at anything else
    (`MenuCloseRestore.h:24`). The confirm path is unchanged (`:316-318`).
  - Orientation, auto turn before or after, night mode before or after, and end of book each block the
    restore too.
  - I checked the menu's in-place actions (`EpubReaderMenuActivity.cpp:296-346`):
    - rotation is caught by `orientationChanged`;
    - the auto-turn popup is caught by `autoTurnAfter`;
    - night mode is caught by its before/after pair;
    - frontlight is the only one left, and it does not change the page (spec A-13).
- **AC3, AA restore keeps its grays: met.**
  - The restore replays the gray upload, the gray refresh and the cleanup from the saved masks
    (`:1570-1583`).
  - `fit()` refuses when grays were expected but not both saved and shown (`MenuCloseRestore.h:89`). So a
    skipped or unsaved gray pass falls back to today's re-render, never to a flat page.
  - I checked the UC8179 path in the driver:
    - `copyGrayscaleBuffers(lsb, msb)` makes exactly the two driver calls the render makes
      (`FreeInkDisplay.cpp:796-801`).
    - The `_grayBase` fold reads only the mask pointers and the base snapshotted by `displayStart` from the
      framebuffer (`Uc8179Driver.cpp:273-274`, `:473-515`).
    - `displayGray` ignores `fb` (`:576`).
  - So a restore that leaves the B/W page in the framebuffer is equivalent to the render, which leaves the
    MSB mask there at that point.

**Spec requirements**

- **Capture points:** every capture point in data flow §1 is present, behind the single `capturePage` flag
  (`EpubReaderActivity.cpp:1724`, `:1787-1790`, `:1838-1841`, `:1857`, `:1907`, `:1933`, `:1940`, `:1944`).
  This covers review-1 finding 2, which gated the gray captures too.
- **Handler (A-14, A-15):** the before-state is captured by value, and the flag is set under `RenderLock`,
  as `pendingScreenshot` is (`:925-928`).
- **Consuming the request:** `renderBook` consumes the flag as its first statement (`:1236`). The restore runs
  after the spine clamps and the end-of-book return, and `invalidate()` follows a refusal (`:1252-1258`).
- **Restore state (A-20):** it draws the bookmark toast and sets `lastRenderCompleteMs`, `pageShown` and
  `pageRendered`.
- **Memory (A-9):** explicit PSRAM, the B/W plane allocated on first `captureBw`, the gray pair together with
  a rollback, a per-kind OOM latch, and `release()` in `onExit` (`:1691`).
- **Logging (A-23):** all four log lines are present, with the specified text.
- **The deliberate exclusions are stated, not silent:** image pages, night mode, and the tiled one-buffer and
  strip-scratch branches. Each is a non-goal or assumption in the spec, refuses as `empty` or `gray-missing`,
  and falls back to today's render.

**Scope**

- No reduction. The PR answers every research question in the brief: what is saved, whether it can be
  restored, and what must invalidate it.
- No expansion. The only change outside `src/activities/reader` is the one-line `GfxRenderer` pass-through
  that spec A-19 calls for. `GfxRenderer` is on the ui surface.
- `EpubReaderMenuActivity`, `ReaderActivity`, the shared append points and `freeink-sdk` are untouched, as
  the spec and brief require.

**Tests**

- The 12 tests cover spec T-1 to T-7 and exercise the pure decisions through their public functions.
- The `CapturedPage` records in them are built through the lifecycle methods, as review-1 finding 1 asked,
  rather than by setting fields.
- `PageFrameCache` and the reader wiring are not host-built. The spec justifies this (no `esp_heap_caps.h`
  stub, and adding one would be a new stub pattern), and the device steps V-1 to V-9 cover them.
- One term of the gray predicate is not pinned by any test. See finding 1.

**Plan divergence**

- None. Each file matches the plan's code blocks apart from clang-format line wrapping.
- The call-site counts match Task 4 Step 12 (4 × `captureGrayPlane`, 3 × `noteGrayShown`) and Task 5
  Step 6.
- Task 6 Step 4's file set holds: the nine declared files only.
- The spec's data flow sketches the `Page cache:` line inside `renderContents`. The reviewed plan puts it in
  `renderBook` after `Rendered page in` (Task 4 Step 10). The behaviour is the same: one line per capturing
  render.

## Quality

- **Patterns mirrored, not reinvented:**
  - `MenuCloseRestore.h` follows the pure-header-plus-host-test shape of `ReaderMenuSheetLayout.h` and
    `LauncherRefresh.h`.
  - Its test target copies the sibling block in `test/ui_layout/CMakeLists.txt` line for line, and includes
    the header the same way `ReaderMenuSheetLayoutTest.cpp:3` does.
  - `PageFrameCache`'s `PsramFree`/`PsramBuffer`/`allocatePsram` is the `CatalogIndexStore.h:86-91`,
    `.cpp:40-46` idiom, as the brief directs. That makes it a second private copy of a four-line deleter.
    Lifting it into `lib/Memory` would touch the network surface and is outside this bead, so it is not a
    finding.
  - The request flag copies `pendingScreenshot`. The restore's B/W step reuses
    `ReaderUtils::displayWithRefreshCycle`, so the HALF cadence is unchanged.
- **Naming and structure** match the siblings:
  - `restorePageFromCache` beside `restoreSavedPosition`;
  - `pageCache` beside `navCache`;
  - enum-plus-`name()` for the log reasons;
  - designated initialisers as `navigateTo({...})` already uses them.
- **Dead code and comments:**
  - There is no dead or commented-out code.
  - The size-mismatch guard in `captureBw` (`PageFrameCache.cpp:34-38`) cannot fire on a fixed framebuffer,
    but it guards the `memcpy` length into a buffer sized by an earlier call, so it earns its place.
  - The comments explain why rather than what. Examples: the first-statement consume (`:1235`), the
    one-buffer exclusion (`:1837`), and the order-only-logs note in `decide()`.
- **Error handling** has the established shape:
  - `LOG_ERR` once on OOM, with a latch;
  - a refusal logged at `LOG_DBG` with its reason, then a fall-through to today's path;
  - display calls returning `void`, as they do in `renderContents`.
- **Concurrency:**
  - Every write to `pageRestoreRequested` and every `pageCache` call is on the render task or under
    `RenderLock`.
  - The handler runs with the pop's lock released (`ActivityManager.cpp:126-127`), so the nested
    `RenderLock` cannot self-deadlock.
- **Tests:**
  - The tests are well named, and each asserts one outcome.
  - The helpers `quietCancel()`, `flatPage()` and `grayPage()` keep each case to the one change under test.
  - The gap is finding 1.

## Findings

1. **MINOR: the gray-completeness test does not pin the LSB term.**
   - **The code:** `fit()` refuses with `!(page.grayShown && page.grayLsb && page.grayMsb)`
     (`src/activities/reader/MenuCloseRestore.h:89`).
   - **The tests:** every `GrayMissing` case in `test/ui_layout/MenuCloseRestoreTest.cpp` either has MSB
     unset or `grayShown` unset:
     - `oneMissing` marks LSB only (`:133`);
     - `notShown` has both planes but is not shown;
     - `shownUnsaved` has no planes (`:143-146`);
     - `ResetForgetsEverything` and `ANewPageNeverInheritsThePreviousPlanes` have no planes.
   - **The gap:** deleting `page.grayLsb &&` from the predicate leaves all 12 tests green. Spec T-5 exists to
     protect exactly this predicate, which is AC3's "no flat page" guard.
   - **The fix, inline:** add the symmetric case, `beginPage(SPINE, PAGE, true)`, `markPlane(false)`,
     `markGrayShown()`, which should give `Fit::GrayMissing`.
   - **Why MINOR:** no current capture path can produce MSB without LSB, so this is test strength, not a live
     defect.

No BLOCKER or MAJOR findings. The restore is correct by construction on the owner's UC8179 path. I checked it
against the driver code, not only the spec's citations. On SSD1677 it replays a sequence the firmware already
runs: a blocking B/W refresh, then strip writes, then the gray refresh and cleanup, as image pages do today.
On UC8279 it takes the same no-strip branch as UC8179. The PR flags both of those panels as untested on the
device (V-9), which is honest.

Device verification for the owner is as the PR body states: V-1/V-2 for the `Menu closed: restore` →
`Page restore: ... total=` pair with no `Page render` line, and V-5/V-6 for the per-turn copy cost and the
48,000 / 144,000 B PSRAM delta.

VERDICT: CLEAR
