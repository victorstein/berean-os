# Reader menu close without a re-render — Implementation Plan (berean-o-4, R21)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A cancelled reader-menu close that changed nothing shows the page from a copy the reader saved when it
rendered it, instead of loading and rendering it again.

**Architecture:**
- `EpubReaderActivity` owns a `PageFrameCache`, which holds up to three PSRAM planes: B/W, gray LSB and gray MSB.
  `renderContents` fills it on every page render.
- The menu's result handler asks a pure helper, `MenuCloseRestore::decide()`, whether the close may restore, and
  sets a flag if so.
- The next `renderBook` checks `MenuCloseRestore::fit()`. If the saved page still matches, it replays the display
  calls the render would make — B/W refresh, gray upload, gray refresh, cleanup — from the saved planes.

**Tech stack:** C++20 (`gnu++2a` on the device, C++20 on the host), PlatformIO `x4pro` env, ESP-IDF
`heap_caps_malloc`, GoogleTest v1.17.0 host suite under `test/`.

**Spec:** `docs/superpowers/specs/2026-10-11-berean-o-4-design.md`, and the reviewed version that follows
`docs/superpowers/reviews/berean-o-4-spec-review-1.md`. Assumption numbers (A-n), test IDs (T-n) and device
checks (V-n) below are the spec's.

---

## Files

FILES: src/activities/reader/MenuCloseRestore.h, src/activities/reader/PageFrameCache.h, src/activities/reader/PageFrameCache.cpp
FILES: src/activities/reader/EpubReaderActivity.h, src/activities/reader/EpubReaderActivity.cpp
FILES: lib/GfxRenderer/GfxRenderer.h, lib/GfxRenderer/GfxRenderer.cpp
FILES: test/ui_layout/MenuCloseRestoreTest.cpp, test/ui_layout/CMakeLists.txt

| File | Responsibility |
|---|---|
| `src/activities/reader/MenuCloseRestore.h` (new) | Pure decisions. `decide()`, for whether a close may restore. `CapturedPage`, the record of what was saved, with its lifecycle methods. `fit()` and `restoresGray()`, for whether the saved page matches. Log names. No firmware includes. |
| `src/activities/reader/PageFrameCache.{h,cpp}` (new) | PSRAM planes: allocation, the OOM latch, `memcpy` and copy timing. Every state change goes through `CapturedPage`. |
| `lib/GfxRenderer/GfxRenderer.{h,cpp}` | One pass-through, `copyGrayscaleBuffers(lsb, msb)`. |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | Capture in `renderContents`, the decision in `openReaderMenu`'s handler, the restore in `renderBook`, release in `onExit`. |
| `test/ui_layout/MenuCloseRestoreTest.cpp` (new), `test/ui_layout/CMakeLists.txt` | Host tests T-1 to T-7, registered in the suite's own CMake file. |

Nothing to hand to the orchestrator. `test/CMakeLists.txt` already has `add_subdirectory(ui_layout)` at line 102.
No `src/main.cpp` or translation change is needed, and no user-facing string is added.

## Conventions every step follows

- **Code style.** Comments only for a non-obvious "why". `makeUniqueNoThrow` is not used here because the planes
  must be explicit PSRAM (`heap_caps_malloc`); the `PsramFree` deleter keeps RAII. Logging goes through
  `LOG_DBG`/`LOG_ERR` from `<Logging.h>`.
- **Before every commit:**
  1. `git add` the step's files. `bin/clang-format-fix` skips untracked files.
  2. Run `./bin/clang-format-fix`, the whole tree, as CI does.
  3. `git add` the same files again.
  4. Commit.
- **Commit trailer.** Every commit message ends with a blank line, then
  `Claude-Session: https://claude.ai/code/session_019L2VTToBdZHzfE6xpNyMQw`. No co-author or generated-by line.
- **PlatformIO is not on `PATH`.** Use `/Volumes/stein/.platformio/penv/bin/pio`. Never run two `pio run`s at once.
  If a build fails mentioning `idf_component.yml`, a concurrent build raced it; run the same command again.

---

### Task 0: Prepare the worktree

**Files:** none.

- [ ] **Step 1: Bootstrap the worktree** (submodule and clang-format 21 in `.venv/bin`)

Run: `./bin/bootstrap`
Expected: ends with `clang-format 21 already present in .venv/bin` or an equivalent success line, exit 0.

- [ ] **Step 2: Configure and build the host suite once**

Run:
```bash
cmake -S test -B build/test
cmake --build build/test -j
```
Expected: the build finishes. On a fresh configure, several suites can fail because gtest is built by many
targets at once. If so, run `cmake --build build/test -j` again; the second run succeeds.

- [ ] **Step 3: Build the firmware once, before any change**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`
Expected: `[SUCCESS]`. The first build in a worktree rebuilds the Arduino core (~3–4 min); later builds take
~25 s. If this fails, stop: the failure predates this work.

---

### Task 1: `MenuCloseRestore::decide()` — may this close restore? (T-1, T-2, T-3, T-6 for `Verdict`)

**Files:**
- Create: `src/activities/reader/MenuCloseRestore.h`
- Create: `test/ui_layout/MenuCloseRestoreTest.cpp`
- Modify: `test/ui_layout/CMakeLists.txt` (append at the end)

- [ ] **Step 1: Register the test executable**

Append to the end of `test/ui_layout/CMakeLists.txt`:

```cmake

add_executable(MenuCloseRestoreTest
  MenuCloseRestoreTest.cpp
)

target_include_directories(MenuCloseRestoreTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(MenuCloseRestoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MenuCloseRestoreTest)
```

- [ ] **Step 2: Write the failing tests**

Create `test/ui_layout/MenuCloseRestoreTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <iterator>
#include <set>
#include <string>

#include "activities/reader/MenuCloseRestore.h"

namespace {

using MenuCloseRestore::Close;
using MenuCloseRestore::Verdict;

// A cancelled close that changed nothing: the one case that restores.
Close quietCancel() {
  Close close;
  close.cancelled = true;
  return close;
}

}  // namespace

TEST(MenuCloseRestoreDecide, QuietCancelRestores) {
  EXPECT_EQ(MenuCloseRestore::decide(quietCancel()), Verdict::Restore);
}

TEST(MenuCloseRestoreDecide, EachChangeAloneBlocksWithItsOwnReason) {
  Close confirmed = quietCancel();
  confirmed.cancelled = false;
  EXPECT_EQ(MenuCloseRestore::decide(confirmed), Verdict::Action);

  Close endOfBook = quietCancel();
  endOfBook.atEndOfBook = true;
  EXPECT_EQ(MenuCloseRestore::decide(endOfBook), Verdict::EndOfBook);

  Close rotated = quietCancel();
  rotated.orientationChanged = true;
  EXPECT_EQ(MenuCloseRestore::decide(rotated), Verdict::Orientation);

  Close autoTurnStopped = quietCancel();
  autoTurnStopped.autoTurnBefore = true;
  EXPECT_EQ(MenuCloseRestore::decide(autoTurnStopped), Verdict::AutoTurn);

  Close autoTurnStarted = quietCancel();
  autoTurnStarted.autoTurnAfter = true;
  EXPECT_EQ(MenuCloseRestore::decide(autoTurnStarted), Verdict::AutoTurn);

  Close nightTurnedOff = quietCancel();
  nightTurnedOff.nightModeBefore = true;
  EXPECT_EQ(MenuCloseRestore::decide(nightTurnedOff), Verdict::NightMode);

  Close nightTurnedOn = quietCancel();
  nightTurnedOn.nightModeAfter = true;
  EXPECT_EQ(MenuCloseRestore::decide(nightTurnedOn), Verdict::NightMode);
}

TEST(MenuCloseRestoreDecide, ConfirmedActionReportsActionWhateverElseChanged) {
  Close everything;
  everything.cancelled = false;
  everything.orientationChanged = true;
  everything.autoTurnBefore = true;
  everything.autoTurnAfter = true;
  everything.nightModeBefore = true;
  everything.nightModeAfter = true;
  everything.atEndOfBook = true;
  EXPECT_EQ(MenuCloseRestore::decide(everything), Verdict::Action);
}

TEST(MenuCloseRestoreDecide, EveryVerdictHasADistinctName) {
  const Verdict all[] = {Verdict::Restore,     Verdict::Action,   Verdict::EndOfBook,
                         Verdict::Orientation, Verdict::AutoTurn, Verdict::NightMode};
  std::set<std::string> names;
  for (const Verdict verdict : all) {
    const char* label = MenuCloseRestore::name(verdict);
    ASSERT_NE(label, nullptr);
    EXPECT_STRNE(label, "");
    names.insert(label);
  }
  EXPECT_EQ(names.size(), std::size(all));
}
```

- [ ] **Step 3: Run it and see it fail**

Run: `cmake -S test -B build/test && cmake --build build/test --target MenuCloseRestoreTest`
Expected: FAIL to compile with `fatal error: 'activities/reader/MenuCloseRestore.h' file not found`.

- [ ] **Step 4: Write the minimal header**

Create `src/activities/reader/MenuCloseRestore.h`:

```cpp
#pragma once

#include <cstdint>

// Whether a reader-menu close may show the page the reader saved instead of rendering it again.
// Free of firmware includes so the host suite can exercise it (test/ui_layout).
namespace MenuCloseRestore {

// What the menu could have changed, as its result handler sees it.
struct Close {
  bool cancelled = false;
  bool orientationChanged = false;
  bool autoTurnBefore = false;
  bool autoTurnAfter = false;
  bool nightModeBefore = false;
  bool nightModeAfter = false;
  bool atEndOfBook = false;
};

enum class Verdict : uint8_t { Restore, Action, EndOfBook, Orientation, AutoTurn, NightMode };

// The order only decides which reason is logged; any of them blocks a restore.
constexpr Verdict decide(const Close& close) {
  if (!close.cancelled) return Verdict::Action;
  if (close.atEndOfBook) return Verdict::EndOfBook;
  if (close.orientationChanged) return Verdict::Orientation;
  // Every close re-applies the menu's auto-turn option, which starts at off, so auto turn on at either
  // end changes the status bar.
  if (close.autoTurnBefore || close.autoTurnAfter) return Verdict::AutoTurn;
  if (close.nightModeBefore || close.nightModeAfter) return Verdict::NightMode;
  return Verdict::Restore;
}

constexpr const char* name(const Verdict verdict) {
  switch (verdict) {
    case Verdict::Restore:
      return "restore";
    case Verdict::Action:
      return "action";
    case Verdict::EndOfBook:
      return "end-of-book";
    case Verdict::Orientation:
      return "orientation";
    case Verdict::AutoTurn:
      return "auto-turn";
    case Verdict::NightMode:
      return "night";
  }
  return "";
}

}  // namespace MenuCloseRestore
```

- [ ] **Step 5: Run it and see it pass**

Run: `cmake --build build/test --target MenuCloseRestoreTest && build/test/ui_layout/MenuCloseRestoreTest`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 6: Commit**

```bash
git add src/activities/reader/MenuCloseRestore.h test/ui_layout/MenuCloseRestoreTest.cpp test/ui_layout/CMakeLists.txt
./bin/clang-format-fix
git add src/activities/reader/MenuCloseRestore.h test/ui_layout/MenuCloseRestoreTest.cpp test/ui_layout/CMakeLists.txt
git commit -m "test: decide when a reader-menu close may restore the page" -m "Claude-Session: https://claude.ai/code/session_019L2VTToBdZHzfE6xpNyMQw"
```

---

### Task 2: `CapturedPage` and `fit()` — does the saved page still match? (T-4, T-5, T-6 for `Fit`, T-7)

**Files:**
- Modify: `src/activities/reader/MenuCloseRestore.h`
- Modify: `test/ui_layout/MenuCloseRestoreTest.cpp` (append at the end)

- [ ] **Step 1: Write the failing tests**

Append to the end of `test/ui_layout/MenuCloseRestoreTest.cpp`:

```cpp

namespace {

using MenuCloseRestore::CapturedPage;
using MenuCloseRestore::Fit;
using MenuCloseRestore::Position;

constexpr int SPINE = 12;
constexpr int PAGE = 3;
constexpr Position HERE{true, SPINE, PAGE};

// B/W only, as a page shown without anti-aliasing.
CapturedPage flatPage() {
  CapturedPage page;
  page.beginPage(SPINE, PAGE, false);
  return page;
}

// Every gray step done, as the render that saved it ran them.
CapturedPage grayPage() {
  CapturedPage page;
  page.beginPage(SPINE, PAGE, true);
  page.markPlane(true);
  page.markPlane(false);
  page.markGrayShown();
  return page;
}

}  // namespace

TEST(MenuCloseRestoreFit, NothingSavedIsEmpty) { EXPECT_EQ(MenuCloseRestore::fit(CapturedPage{}, HERE), Fit::Empty); }

TEST(MenuCloseRestoreFit, FlatPageAtTheSamePlaceFitsWithoutGrays) {
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), HERE), Fit::Fits);
  EXPECT_FALSE(MenuCloseRestore::restoresGray(flatPage()));
}

TEST(MenuCloseRestoreFit, NoSectionIsRefused) {
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), Position{false, SPINE, -1}), Fit::NoSection);
}

TEST(MenuCloseRestoreFit, AnotherSpineOrPageHasMoved) {
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), Position{true, SPINE + 1, PAGE}), Fit::Moved);
  EXPECT_EQ(MenuCloseRestore::fit(flatPage(), Position{true, SPINE, PAGE + 1}), Fit::Moved);
}

TEST(MenuCloseRestoreFit, GrayPageFitsOnlyWithBothPlanesShown) {
  EXPECT_EQ(MenuCloseRestore::fit(grayPage(), HERE), Fit::Fits);
  EXPECT_TRUE(MenuCloseRestore::restoresGray(grayPage()));

  CapturedPage oneMissing;
  oneMissing.beginPage(SPINE, PAGE, true);
  oneMissing.markPlane(true);
  oneMissing.markGrayShown();
  EXPECT_EQ(MenuCloseRestore::fit(oneMissing, HERE), Fit::GrayMissing);

  CapturedPage notShown;
  notShown.beginPage(SPINE, PAGE, true);
  notShown.markPlane(true);
  notShown.markPlane(false);
  EXPECT_EQ(MenuCloseRestore::fit(notShown, HERE), Fit::GrayMissing);

  CapturedPage shownUnsaved;
  shownUnsaved.beginPage(SPINE, PAGE, true);
  shownUnsaved.markGrayShown();
  EXPECT_EQ(MenuCloseRestore::fit(shownUnsaved, HERE), Fit::GrayMissing);
}

TEST(MenuCloseRestoreFit, ResetForgetsEverything) {
  CapturedPage page = grayPage();
  page.reset();
  EXPECT_EQ(MenuCloseRestore::fit(page, HERE), Fit::Empty);

  page.beginPage(SPINE, PAGE, true);
  page.markGrayShown();
  EXPECT_EQ(MenuCloseRestore::fit(page, HERE), Fit::GrayMissing);
}

TEST(MenuCloseRestoreFit, ANewPageNeverInheritsThePreviousPlanes) {
  CapturedPage page = grayPage();
  page.beginPage(SPINE, PAGE + 1, true);
  page.markGrayShown();
  EXPECT_FALSE(page.grayLsb);
  EXPECT_FALSE(page.grayMsb);
  EXPECT_EQ(MenuCloseRestore::fit(page, Position{true, SPINE, PAGE + 1}), Fit::GrayMissing);
}

TEST(MenuCloseRestoreFit, EveryFitHasADistinctName) {
  const Fit all[] = {Fit::Fits, Fit::Empty, Fit::NoSection, Fit::Moved, Fit::GrayMissing};
  std::set<std::string> names;
  for (const Fit result : all) {
    const char* label = MenuCloseRestore::name(result);
    ASSERT_NE(label, nullptr);
    EXPECT_STRNE(label, "");
    names.insert(label);
  }
  EXPECT_EQ(names.size(), std::size(all));
}
```

- [ ] **Step 2: Run it and see it fail**

Run: `cmake --build build/test --target MenuCloseRestoreTest`
Expected: FAIL to compile, because `CapturedPage`, `Fit` and `Position` are not members of `MenuCloseRestore` yet
(clang: `no member named 'CapturedPage' in namespace 'MenuCloseRestore'`).

- [ ] **Step 3: Add the record and `fit()` to the header**

In `src/activities/reader/MenuCloseRestore.h`, replace the closing line

```cpp
}  // namespace MenuCloseRestore
```

with:

```cpp
// What the reader saved of the last page it rendered. Every change goes through these methods, so a
// page can never keep planes saved for the one before it.
struct CapturedPage {
  bool bw = false;
  bool grayLsb = false;
  bool grayMsb = false;
  bool grayExpected = false;
  bool grayShown = false;
  int spineIndex = -1;
  int pageIndex = -1;

  void reset() { *this = CapturedPage{}; }
  void beginPage(const int spine, const int page, const bool expectGray) {
    reset();
    bw = true;
    spineIndex = spine;
    pageIndex = page;
    grayExpected = expectGray;
  }
  void markPlane(const bool lsbPlane) { (lsbPlane ? grayLsb : grayMsb) = true; }
  void markGrayShown() { grayShown = true; }
};

// Where the reader is when the close is handled.
struct Position {
  bool hasSection = false;
  int spineIndex = -1;
  int pageIndex = -1;
};

enum class Fit : uint8_t { Fits, Empty, NoSection, Moved, GrayMissing };

constexpr Fit fit(const CapturedPage& page, const Position& at) {
  if (!page.bw) return Fit::Empty;
  if (!at.hasSection) return Fit::NoSection;
  if (page.spineIndex != at.spineIndex || page.pageIndex != at.pageIndex) return Fit::Moved;
  // A gray pass that was skipped, or not saved, is retried by a re-render rather than left flat.
  if (page.grayExpected && !(page.grayShown && page.grayLsb && page.grayMsb)) return Fit::GrayMissing;
  return Fit::Fits;
}

constexpr bool restoresGray(const CapturedPage& page) { return page.grayExpected; }

constexpr const char* name(const Fit result) {
  switch (result) {
    case Fit::Fits:
      return "fits";
    case Fit::Empty:
      return "empty";
    case Fit::NoSection:
      return "no-section";
    case Fit::Moved:
      return "moved";
    case Fit::GrayMissing:
      return "gray-missing";
  }
  return "";
}

}  // namespace MenuCloseRestore
```

- [ ] **Step 4: Run it and see it pass**

Run: `cmake --build build/test --target MenuCloseRestoreTest && build/test/ui_layout/MenuCloseRestoreTest`
Expected: `[  PASSED  ] 12 tests.`

- [ ] **Step 5: Commit**

```bash
git add src/activities/reader/MenuCloseRestore.h test/ui_layout/MenuCloseRestoreTest.cpp
./bin/clang-format-fix
git add src/activities/reader/MenuCloseRestore.h test/ui_layout/MenuCloseRestoreTest.cpp
git commit -m "test: match a saved page against the reader's position" -m "Claude-Session: https://claude.ai/code/session_019L2VTToBdZHzfE6xpNyMQw"
```

---

### Task 3: The PSRAM cache and the renderer pass-through (A-9, A-19)

These are firmware-only. There is no host test, because `test/stubs/` has no `esp_heap_caps.h` and `GfxRenderer.cpp`
is not host-built. The check is the firmware build in Step 4. Nothing calls this code until Task 4.

**Files:**
- Create: `src/activities/reader/PageFrameCache.h`
- Create: `src/activities/reader/PageFrameCache.cpp`
- Modify: `lib/GfxRenderer/GfxRenderer.h:323-325`
- Modify: `lib/GfxRenderer/GfxRenderer.cpp:2267-2269`

- [ ] **Step 1: Add `GfxRenderer::copyGrayscaleBuffers`**

In `lib/GfxRenderer/GfxRenderer.h`, replace

```cpp
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;
```

with:

```cpp
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  // Uploads two saved masks, laid out like the framebuffer, in place of the framebuffer itself.
  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) const;
  void displayGrayBuffer() const;
```

In `lib/GfxRenderer/GfxRenderer.cpp`, replace

```cpp
void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }
```

with:

```cpp
void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) const {
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}
```

- [ ] **Step 2: Create `src/activities/reader/PageFrameCache.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "MenuCloseRestore.h"

// The last page renderContents drew, kept so a cancelled reader-menu close can show it again without
// rendering it: the B/W frame and, with anti-aliasing, the two gray masks the display was given. Each
// plane is the framebuffer's size, in PSRAM, allocated on first use and kept for the reader's life.
// Render task only, plus release() from onExit under RenderLock.
class PageFrameCache {
 public:
  void invalidate();
  // Starts a new page from the B/W framebuffer.
  void captureBw(const uint8_t* frame, size_t bytes, int spineIndex, int pageIndex, bool grayExpected);
  // One gray mask, laid out like the framebuffer. Ignored unless captureBw saved this page.
  void captureGrayPlane(bool lsbPlane, const uint8_t* plane, size_t bytes);
  void noteGrayShown();
  void release();

  const MenuCloseRestore::CapturedPage& captured() const { return record; }
  const uint8_t* bw() const { return bwBuffer.get(); }
  const uint8_t* grayLsb() const { return lsbBuffer.get(); }
  const uint8_t* grayMsb() const { return msbBuffer.get(); }
  // Time spent copying for the current page.
  unsigned long copyMillis() const { return copyMs; }

 private:
  // heap_caps_malloc is the only way to demand PSRAM; makeUniqueNoThrow's operator new takes no capability
  // argument. The deleter keeps the RAII guarantee anyway.
  struct PsramFree {
    void operator()(uint8_t* p) const;
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramFree>;
  static PsramBuffer allocatePsram(size_t bytes);

  PsramBuffer bwBuffer;
  PsramBuffer lsbBuffer;
  PsramBuffer msbBuffer;
  size_t planeBytes = 0;
  // One failed allocation is not retried for the reader's session, so there is no per-page retry or log spam.
  bool bwAllocationFailed = false;
  bool grayAllocationFailed = false;
  unsigned long copyMs = 0;
  MenuCloseRestore::CapturedPage record;
};
```

- [ ] **Step 3: Create `src/activities/reader/PageFrameCache.cpp`**

```cpp
#include "PageFrameCache.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <cstring>

void PageFrameCache::PsramFree::operator()(uint8_t* p) const { heap_caps_free(p); }

PageFrameCache::PsramBuffer PageFrameCache::allocatePsram(const size_t bytes) {
  return PsramBuffer(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void PageFrameCache::invalidate() {
  record.reset();
  copyMs = 0;
}

void PageFrameCache::captureBw(const uint8_t* frame, const size_t bytes, const int spineIndex, const int pageIndex,
                               const bool grayExpected) {
  invalidate();
  if (frame == nullptr || bytes == 0) return;
  if (!bwBuffer) {
    if (bwAllocationFailed) return;
    bwBuffer = allocatePsram(bytes);
    if (!bwBuffer) {
      bwAllocationFailed = true;
      LOG_ERR("ERS", "OOM: page cache plane (%u bytes, PSRAM)", static_cast<unsigned>(bytes));
      return;
    }
    planeBytes = bytes;
  }
  if (bytes != planeBytes) {
    LOG_ERR("ERS", "Page cache: frame is %u bytes, planes are %u", static_cast<unsigned>(bytes),
            static_cast<unsigned>(planeBytes));
    return;
  }
  const unsigned long start = millis();
  memcpy(bwBuffer.get(), frame, bytes);
  record.beginPage(spineIndex, pageIndex, grayExpected);
  copyMs += millis() - start;
}

void PageFrameCache::captureGrayPlane(const bool lsbPlane, const uint8_t* plane, const size_t bytes) {
  if (!record.bw || plane == nullptr || bytes != planeBytes) return;
  if (!lsbBuffer) {
    if (grayAllocationFailed) return;
    lsbBuffer = allocatePsram(planeBytes);
    msbBuffer = allocatePsram(planeBytes);
    if (!lsbBuffer || !msbBuffer) {
      lsbBuffer.reset();
      msbBuffer.reset();
      grayAllocationFailed = true;
      LOG_ERR("ERS", "OOM: page cache plane (%u bytes, PSRAM)", static_cast<unsigned>(2 * planeBytes));
      return;
    }
  }
  const unsigned long start = millis();
  memcpy(lsbPlane ? lsbBuffer.get() : msbBuffer.get(), plane, planeBytes);
  record.markPlane(lsbPlane);
  copyMs += millis() - start;
}

void PageFrameCache::noteGrayShown() {
  if (record.bw) record.markGrayShown();
}

void PageFrameCache::release() {
  invalidate();
  bwBuffer.reset();
  lsbBuffer.reset();
  msbBuffer.reset();
  planeBytes = 0;
}
```

- [ ] **Step 4: Build the firmware**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`
Expected: `[SUCCESS]`, with no new warning in `PageFrameCache.cpp` or `GfxRenderer.cpp` (`-Wall` is on for `src/` and
`lib/`).

- [ ] **Step 5: Commit**

```bash
git add src/activities/reader/PageFrameCache.h src/activities/reader/PageFrameCache.cpp lib/GfxRenderer/GfxRenderer.h lib/GfxRenderer/GfxRenderer.cpp
./bin/clang-format-fix
git add src/activities/reader/PageFrameCache.h src/activities/reader/PageFrameCache.cpp lib/GfxRenderer/GfxRenderer.h lib/GfxRenderer/GfxRenderer.cpp
git commit -m "perf: add a PSRAM page frame cache for the reader" -m "Claude-Session: https://claude.ai/code/session_019L2VTToBdZHzfE6xpNyMQw"
```

---

### Task 4: Capture every page the reader renders (A-2 to A-8, A-12, A-23 `Page cache:`)

After this task, every non-image page rendered with night mode off is saved, and logs
`Page cache: N planes in Xms`. Nothing restores yet, so behaviour on the device is unchanged apart from that log
line and the PSRAM in use. Re-read each file before editing it: a sibling task may have landed in the meantime.

**Files:**
- Modify: `src/activities/reader/EpubReaderActivity.h`
- Modify: `src/activities/reader/EpubReaderActivity.cpp`

- [ ] **Step 1: Add the cache to the reader**

In `src/activities/reader/EpubReaderActivity.h`, replace

```cpp
#include "AutoPageTurn.h"
#include "BibleBookIndex.h"
#include "EpubReaderMenuActivity.h"
```

with:

```cpp
#include "AutoPageTurn.h"
#include "BibleBookIndex.h"
#include "EpubReaderMenuActivity.h"
#include "PageFrameCache.h"
```

and replace

```cpp
  // Whether the last render drew a reading page, unlike the session-long pageShown. Cleared by
  // reopenReaderMenu before its render, set by renderBook, both under RenderLock.
  bool pageRendered = false;
```

with:

```cpp
  // Whether the last render drew a reading page, unlike the session-long pageShown. Cleared by
  // reopenReaderMenu before its render, set by renderBook, both under RenderLock.
  bool pageRendered = false;
  // The page renderContents last drew, for a cancelled menu close to show again. Render task only.
  PageFrameCache pageCache;
```

- [ ] **Step 2: Invalidate at the top of every `renderBook`**

In `src/activities/reader/EpubReaderActivity.cpp`, inside `renderBook()`, replace

```cpp
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
```

with:

```cpp
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  pageCache.invalidate();

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
```

- [ ] **Step 3: Decide which pages are captured**

In `renderContents`, replace

```cpp
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
```

with:

```cpp
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
  // Only these pages are ever restored on a menu close: an image page has its own display sequence, and
  // night mode always re-renders.
  const bool capturePage = !pageHasImages && SETTINGS.screenInverted == 0;
```

- [ ] **Step 4: Capture the B/W page before it is displayed**

In `renderContents`, replace

```cpp
  renderStatusBar();
  [[maybe_unused]] const auto tBwRender = millis();
```

with:

```cpp
  renderStatusBar();
  [[maybe_unused]] const auto tBwRender = millis();
  if (capturePage) {
    pageCache.captureBw(renderer.getFrameBuffer(), renderer.getBufferSize(), currentSpineIndex, section->currentPage,
                        needsTextGrayscale);
  }
```

- [ ] **Step 5: Capture the buffered tiled planes (SSD1677)**

In `renderContents`, replace

```cpp
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      [[maybe_unused]] const auto tGrayRender = millis();
```

with:

```cpp
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      [[maybe_unused]] const auto tGrayRender = millis();
      // With one buffer the MSB plane overwrites the LSB one; that low-heap case is left to a re-render.
      if (capturePage && msbPlaneBuf) {
        pageCache.captureGrayPlane(true, lsbPlaneBuf.get(), planeBytes);
        pageCache.captureGrayPlane(false, msbPlaneBuf.get(), planeBytes);
      }
```

- [ ] **Step 6: Record the buffered tiled gray pass as shown**

In `renderContents`, replace (six-space indent, followed by `tEnd`)

```cpp
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      [[maybe_unused]] const auto tGrayDisplay = millis();

      renderer.cleanupGrayscaleWithFrameBuffer();
      [[maybe_unused]] const auto tEnd = millis();
```

with:

```cpp
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      if (capturePage) pageCache.noteGrayShown();
      [[maybe_unused]] const auto tGrayDisplay = millis();

      renderer.cleanupGrayscaleWithFrameBuffer();
      [[maybe_unused]] const auto tEnd = millis();
```

- [ ] **Step 7: Record the strip-scratch gray pass as shown**

In `renderContents`, replace (eight-space indent, followed by `tCleanup`)

```cpp
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        [[maybe_unused]] const auto tGrayDisplay = millis();

        renderer.cleanupGrayscaleWithFrameBuffer();
        [[maybe_unused]] const auto tCleanup = millis();
```

with:

```cpp
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        if (capturePage) pageCache.noteGrayShown();
        [[maybe_unused]] const auto tGrayDisplay = millis();

        renderer.cleanupGrayscaleWithFrameBuffer();
        [[maybe_unused]] const auto tCleanup = millis();
```

- [ ] **Step 8: Capture the non-tiled masks (UC8179, UC8279)**

In `renderContents`, replace

```cpp
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      [[maybe_unused]] const auto tGrayLsb = millis();
```

with:

```cpp
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      if (capturePage) pageCache.captureGrayPlane(true, renderer.getFrameBuffer(), renderer.getBufferSize());
      [[maybe_unused]] const auto tGrayLsb = millis();
```

and replace

```cpp
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      [[maybe_unused]] const auto tGrayMsb = millis();
```

with:

```cpp
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      if (capturePage) pageCache.captureGrayPlane(false, renderer.getFrameBuffer(), renderer.getBufferSize());
      [[maybe_unused]] const auto tGrayMsb = millis();
```

- [ ] **Step 9: Record the non-tiled gray pass as shown**

In `renderContents`, replace

```cpp
      renderer.displayGrayBuffer();
      [[maybe_unused]] const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
```

with:

```cpp
      renderer.displayGrayBuffer();
      if (capturePage) pageCache.noteGrayShown();
      [[maybe_unused]] const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
```

- [ ] **Step 10: Log the capture cost once per page**

In `renderBook()`, replace

```cpp
    LOG_DBG("ERS", "Rendered page in %lums", millis() - start);
```

with:

```cpp
    LOG_DBG("ERS", "Rendered page in %lums", millis() - start);
    if (const auto& captured = pageCache.captured(); captured.bw) {
      LOG_DBG("ERS", "Page cache: %d planes in %lums",
              static_cast<int>(captured.bw) + static_cast<int>(captured.grayLsb) + static_cast<int>(captured.grayMsb),
              pageCache.copyMillis());
    }
```

- [ ] **Step 11: Release the planes when the reader exits**

Replace

```cpp
void EpubReaderActivity::onExit() {
  // ActivityManager holds RenderLock while it calls onExit, which is what captureLeftPlace needs;
  // ReaderActivity::onExit saves APP_STATE under the same lock.
  recordPlace(captureLeftPlace());
  ReaderActivity::onExit();
}
```

with:

```cpp
void EpubReaderActivity::onExit() {
  // ActivityManager holds RenderLock while it calls onExit, which is what captureLeftPlace needs;
  // ReaderActivity::onExit saves APP_STATE under the same lock.
  recordPlace(captureLeftPlace());
  pageCache.release();
  ReaderActivity::onExit();
}
```

- [ ] **Step 12: Check that exactly the planned call sites exist**

Run: `grep -o "pageCache\.[A-Za-z]*" src/activities/reader/EpubReaderActivity.cpp | sort | uniq -c`
Expected (the order of the lines may differ):
```
   1 pageCache.captureBw
   4 pageCache.captureGrayPlane
   1 pageCache.captured
   1 pageCache.copyMillis
   1 pageCache.invalidate
   3 pageCache.noteGrayShown
   1 pageCache.release
```

- [ ] **Step 13: Build the firmware**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`
Expected: `[SUCCESS]`, no new warnings in `EpubReaderActivity.cpp`.

- [ ] **Step 14: Commit**

```bash
git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp
./bin/clang-format-fix
git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp
git commit -m "perf: keep the last rendered page in the reader's frame cache" -m "Claude-Session: https://claude.ai/code/session_019L2VTToBdZHzfE6xpNyMQw"
```

---

### Task 5: Decide on close and restore the page (A-11 to A-22, A-23 `Menu closed:` / `Page restore:`)

**Files:**
- Modify: `src/activities/reader/EpubReaderActivity.h`
- Modify: `src/activities/reader/EpubReaderActivity.cpp`

- [ ] **Step 1: Declare the flag and the restore**

In `src/activities/reader/EpubReaderActivity.h`, replace

```cpp
  // The page renderContents last drew, for a cancelled menu close to show again. Render task only.
  PageFrameCache pageCache;
```

with:

```cpp
  // The page renderContents last drew, for a cancelled menu close to show again. Render task only.
  PageFrameCache pageCache;
  // Set by the reader menu's result handler under RenderLock; consumed by the next renderBook.
  bool pageRestoreRequested = false;
```

and replace

```cpp
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
```

with:

```cpp
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  // Shows pageCache's page through the display calls renderContents would make, without rendering it.
  // False, having drawn nothing, when the saved page does not match where the reader is.
  bool restorePageFromCache();
```

- [ ] **Step 2: Include `<cstring>` for `memcpy`**

In `src/activities/reader/EpubReaderActivity.cpp`, replace

```cpp
#include <algorithm>
#include <cassert>
#include <functional>
```

with:

```cpp
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
```

- [ ] **Step 3: Decide in the menu's result handler**

In `openReaderMenu`, replace

```cpp
  const ReaderMenuSheetLayout::RecentChipLabels recent =
      isBible() ? collectRecentChips(onScreen) : ReaderMenuSheetLayout::RecentChipLabels{};
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, readerMenuTitle(), SETTINGS.orientation,
                                               !currentPageFootnotes.empty(), !bookmarks.empty(), HIGHLIGHTS_SUPPORTED,
                                               tagTarget() == ReaderMenuModel::TagTarget::BibleTags, isBible(),
                                               tagsHereCount, pageOnScreen, recent),
      [this](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);
        if (SETTINGS.orientation != menu.orientation) {
          applyOrientation(menu.orientation);
        }
        toggleAutoPageTurn(menu.pageTurnOption);
        if (!result.isCancelled) {
          onReaderMenuConfirm(menu);
        }
      });
```

with:

```cpp
  const ReaderMenuSheetLayout::RecentChipLabels recent =
      isBible() ? collectRecentChips(onScreen) : ReaderMenuSheetLayout::RecentChipLabels{};
  const bool autoTurnBefore = autoTurn.active();
  const bool nightModeBefore = SETTINGS.screenInverted != 0;
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, readerMenuTitle(), SETTINGS.orientation,
                                               !currentPageFootnotes.empty(), !bookmarks.empty(), HIGHLIGHTS_SUPPORTED,
                                               tagTarget() == ReaderMenuModel::TagTarget::BibleTags, isBible(),
                                               tagsHereCount, pageOnScreen, recent),
      [this, autoTurnBefore, nightModeBefore](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);
        const bool orientationChanged = SETTINGS.orientation != menu.orientation;
        if (orientationChanged) {
          applyOrientation(menu.orientation);
        }
        toggleAutoPageTurn(menu.pageTurnOption);
        const auto verdict = MenuCloseRestore::decide({.cancelled = result.isCancelled,
                                                       .orientationChanged = orientationChanged,
                                                       .autoTurnBefore = autoTurnBefore,
                                                       .autoTurnAfter = autoTurn.active(),
                                                       .nightModeBefore = nightModeBefore,
                                                       .nightModeAfter = SETTINGS.screenInverted != 0,
                                                       .atEndOfBook = isAtEndOfBook()});
        LOG_DBG("ERS", "Menu closed: %s", MenuCloseRestore::name(verdict));
        if (verdict == MenuCloseRestore::Verdict::Restore) {
          RenderLock lock;
          pageRestoreRequested = true;
        }
        if (!result.isCancelled) {
          onReaderMenuConfirm(menu);
        }
      });
```

- [ ] **Step 4: Consume the request first, and restore before any render work**

In `renderBook()`, replace

```cpp
void EpubReaderActivity::renderBook() {
  if (!epub) return;
```

with:

```cpp
void EpubReaderActivity::renderBook() {
  // Consumed before anything can return, so a request never outlives the render that follows it.
  const bool restoreRequested = std::exchange(pageRestoreRequested, false);
  if (!epub) return;
```

and replace

```cpp
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  pageCache.invalidate();
```

with:

```cpp
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  if (restoreRequested && restorePageFromCache()) {
    if (bookmarks.toastVisible()) {
      GUI.drawToast(renderer, bookmarks.toastText());
    }
    return;
  }
  pageCache.invalidate();
```

- [ ] **Step 5: Add `restorePageFromCache`**

In `src/activities/reader/EpubReaderActivity.cpp`, insert this definition immediately before the line
`void EpubReaderActivity::onEndOfBookRendered() { autoTurn.stop(); }`:

```cpp
bool EpubReaderActivity::restorePageFromCache() {
  const MenuCloseRestore::Position position =
      section ? MenuCloseRestore::Position{true, currentSpineIndex, section->currentPage}
              : MenuCloseRestore::Position{false, currentSpineIndex, -1};
  const auto& captured = pageCache.captured();
  const auto match = MenuCloseRestore::fit(captured, position);
  if (match != MenuCloseRestore::Fit::Fits) {
    LOG_DBG("ERS", "Page restore refused: %s; rendering", MenuCloseRestore::name(match));
    return false;
  }
  if (!renderer.hasFrameBuffer()) {
    LOG_DBG("ERS", "Page restore refused: no-fb; rendering");
    return false;
  }

  [[maybe_unused]] const auto t0 = millis();
  memcpy(renderer.getFrameBuffer(), pageCache.bw(), renderer.getBufferSize());
  [[maybe_unused]] const auto tCopy = millis();
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  [[maybe_unused]] const auto tDisplay = millis();
  [[maybe_unused]] auto tGrayWrite = tDisplay;
  [[maybe_unused]] auto tGrayDisplay = tDisplay;
  if (MenuCloseRestore::restoresGray(captured)) {
    // The same upload each panel's own branch of renderContents makes, from the saved masks.
    if (renderer.supportsStripGrayscale()) {
      const int planeRows = renderer.getDisplayHeight();
      renderer.writeGrayscalePlaneStrip(true, pageCache.grayLsb(), 0, planeRows);
      renderer.writeGrayscalePlaneStrip(false, pageCache.grayMsb(), 0, planeRows);
    } else {
      renderer.copyGrayscaleBuffers(pageCache.grayLsb(), pageCache.grayMsb());
    }
    tGrayWrite = millis();
    renderer.displayGrayBuffer();
    tGrayDisplay = millis();
    renderer.cleanupGrayscaleWithFrameBuffer();
  }
  [[maybe_unused]] const auto tEnd = millis();
  LOG_DBG("ERS", "Page restore: copy=%lums display=%lums gray_write=%lums gray_display=%lums total=%lums",
          tCopy - t0, tDisplay - tCopy, tGrayWrite - tDisplay, tGrayDisplay - tGrayWrite, tEnd - t0);

  lastRenderCompleteMs = millis();
  pageShown = true;
  pageRendered = true;
  return true;
}

```

- [ ] **Step 6: Check the edits landed once each**

Run: `grep -n "pageRestoreRequested\|restorePageFromCache\|MenuCloseRestore::decide" src/activities/reader/EpubReaderActivity.cpp`
Expected:
- `pageRestoreRequested` twice: set in the handler, consumed in `renderBook`;
- `restorePageFromCache` twice: the call and the definition;
- `MenuCloseRestore::decide` once.

- [ ] **Step 7: Build the firmware**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro`
Expected: `[SUCCESS]`, no new warnings.

- [ ] **Step 8: Build the release env too**

Run: `/Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release`
Expected: `[SUCCESS]`. With `LOG_LEVEL=0` the `LOG_DBG` calls compile out, and this proves the timing locals carry
`[[maybe_unused]]` correctly and no warning appears.

- [ ] **Step 9: Commit**

```bash
git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp
./bin/clang-format-fix
git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp
git commit -m "perf: restore the saved page when the reader menu closes" -m "Claude-Session: https://claude.ai/code/session_019L2VTToBdZHzfE6xpNyMQw"
```

---

### Task 6: Final checks and push

**Files:** none new. Any formatting fix lands in files declared above.

- [ ] **Step 1: Whole host suite**

Run: `cmake --build build/test -j && ctest --test-dir build/test --output-on-failure -j`
Expected: `100% tests passed`. The output includes the twelve `MenuCloseRestore*` tests.

- [ ] **Step 2: Format check over the whole tree, as CI runs it**

Run: `./bin/clang-format-fix && git status --short`
Expected: no modified files. If any are listed, `git add` them and commit
`style: apply clang-format` with the session trailer.

- [ ] **Step 3: Static analysis**

Run, exactly as CI does (no `-e`, so it resolves `default_envs`):
`/Volumes/stein/.platformio/penv/bin/pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
Expected: exit 0. A defect in `PageFrameCache.cpp`, `MenuCloseRestore.h` or an edited reader line is this task's to
fix. If main already fails this check, compare with a run on `origin/main` before deciding a defect is new.

- [ ] **Step 4: Confirm the diff touches only declared files**

Run: `git diff --stat origin/main...HEAD -- . ':(exclude)docs/superpowers'`
Expected: only the nine files on the `FILES:` lines.

- [ ] **Step 5: Push**

Run: `git push`
Expected: the branch updates on `origin/perf/r21-menu-close-restore`.

---

## For the PR (implement phase)

- **Title:** `perf: close the reader menu without re-rendering the page`. It targets `main`.
- **Before figure:** the baseline line, AA on, n=6, 1,152–1,171 ms:
  `Page render: prewarm=8ms bw_render=44ms display=616ms ... gray_lsb=65ms gray_msb=64ms gray_display=357ms total=1160ms`.
- **After figure:** the owner's measured `Page restore: copy=… display=… gray_write=… gray_display=… total=…` line
  from V-1 (AA on) and V-2 (AA off). Never claim a figure that has not been seen.
- **What to flash:** the `x4pro` dev build of this branch, by cable or by `POST /upload?path=/` plus
  *Settings → SD firmware update*. Serial via `python3 scripts/debugging_monitor.py` or `cat /dev/cu.usbmodem*`.
- **Device steps:** the spec's V-1 to V-9. V-5 reads the `Page cache: N planes in Xms` line, and the target is
  ≤ 2 ms per plane.
- **Allocation, justified:**
  - PSRAM, explicitly through `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`: 48,000 B with AA off,
    144,000 B with AA on.
  - Allocated on first use, kept for the reader's life, freed in `onExit`.
  - Internal SRAM: only the cache's members.
  - Stack and static were rejected (spec A-10).
- **Untested:** SSD1677 and UC8279 (V-9).
- **Last line of the body:** `Refs berean-o-4`.
