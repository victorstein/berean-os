# Plan: the reader opens a preaching reference and swipes back to its card (berean-o-20, t7)

Spec: `docs/superpowers/specs/2026-10-15-berean-o-20-design.md` (cleared review 1). Read it first; this
plan is its implementation, step by step. Every path below is relative to the worktree root,
`/Volumes/stein/.herdr/worktrees/berean-os/feature-preaching-reader-return`.

FILES: src/activities/reader/ReaderBackSwipe.h, src/activities/reader/VerseHref.h
FILES: src/activities/reader/EpubReaderActivity.h, src/activities/reader/EpubReaderActivity.cpp
FILES: src/activities/reader/ReaderUtils.h, src/activities/reader/ReturnStack.h
FILES: test/ui_layout/ReaderBackSwipeTest.cpp, test/ui_layout/CMakeLists.txt
FILES: test/verse_anchors/VerseHrefTest.cpp, test/verse_anchors/CMakeLists.txt

## Ground rules for whoever carries this out

- Re-read every file before you edit it. A sibling task may have changed it since this plan was
  written; if an "exact text to replace" below is not in the file, stop and report — do not guess.
- Code is self-documenting. A comment is only for a non-obvious *why* (a constraint, a gotcha, a
  workaround). No comments that restate code or narrate history. The comments in this plan's code are
  the ones to keep; do not add more.
- `CLAUDE.md` rules apply: `makeUniqueNoThrow` for fallible allocations, no `std::function`, `tr()` for
  UI text, locals under 256 B, HAL classes only, `LOG_*` not `Serial`.
- Commit after each step with the message given, ending with the line
  `Claude-Session: https://claude.ai/code/session_01VsFi75VnF9Qq1cRGmQSpL6`. Never push.
- `git add` exactly the files the step names. Never `git add -A`.
- `pio` is not on PATH: use `~/.platformio/penv/bin/pio`.

## Step 0 — bootstrap (no commit)

```bash
./bin/bootstrap
cmake -S test -B build/test
```

Expected: bootstrap inits submodules and links clang-format into `.venv/bin`; cmake configures
(fetching googletest on first run). `build/` is gitignored.

## Step 1 — failing test for the swipe decision

Create `test/ui_layout/ReaderBackSwipeTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "activities/reader/ReaderBackSwipe.h"

namespace {

using ReaderBackSwipe::Action;
using ReaderBackSwipe::Swipe;

Swipe shortRelease(const int depth, const bool origin) {
  return {.shortBackRelease = true, .returnDepth = depth, .hasOrigin = origin};
}

}  // namespace

static_assert(ReaderBackSwipe::decide({}) == Action::PassOn);
static_assert(ReaderBackSwipe::decide({.shortBackRelease = true, .returnDepth = 1, .hasOrigin = true}) ==
              Action::PopReturn);
static_assert(ReaderBackSwipe::decide({.shortBackRelease = true, .returnDepth = 0, .hasOrigin = true}) ==
              Action::ToPreaching);
static_assert(ReaderBackSwipe::decide({.shortBackRelease = true, .returnDepth = 0, .hasOrigin = false}) ==
              Action::PassOn);

TEST(ReaderBackSwipe, NoShortReleasePassesOnWhateverTheState) {
  for (const int depth : {0, 1, 16}) {
    for (const bool origin : {false, true}) {
      EXPECT_EQ(ReaderBackSwipe::decide({.shortBackRelease = false, .returnDepth = depth, .hasOrigin = origin}),
                Action::PassOn)
          << "depth " << depth << ", origin " << origin;
    }
  }
}

TEST(ReaderBackSwipe, TheReturnStackWinsOverTheOrigin) {
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(1, false)), Action::PopReturn);
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(1, true)), Action::PopReturn);
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(16, true)), Action::PopReturn);
}

TEST(ReaderBackSwipe, AnEmptyStackWithAnOriginGoesToPreaching) {
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(0, true)), Action::ToPreaching);
}

TEST(ReaderBackSwipe, AnEmptyStackWithoutAnOriginPassesOn) {
  EXPECT_EQ(ReaderBackSwipe::decide(shortRelease(0, false)), Action::PassOn);
}
```

Append to the end of `test/ui_layout/CMakeLists.txt` (a blank line, then):

```cmake
add_executable(ReaderBackSwipeTest
  ReaderBackSwipeTest.cpp
)

target_include_directories(ReaderBackSwipeTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReaderBackSwipeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderBackSwipeTest)
```

Run:

```bash
cmake -S test -B build/test && cmake --build build/test --target ReaderBackSwipeTest
```

Expected: **fails to compile** — `activities/reader/ReaderBackSwipe.h: No such file or directory`.
(If instead gtest itself fails to build on this first build, re-run the same command once: a fresh
configure can race on gtest.)

No commit yet — the tree does not build.

## Step 2 — the swipe decision

Create `src/activities/reader/ReaderBackSwipe.h` (modelled on `src/activities/reader/MenuCloseRestore.h`):

```cpp
#pragma once

#include <cstdint>

// What a short Back -- the left-edge swipe or a held Left key -- does on the reading surface.
// Free of firmware includes so the host suite can exercise it (test/ui_layout).
namespace ReaderBackSwipe {

struct Swipe {
  bool shortBackRelease = false;  // Back released under ReaderUtils::GO_BACK_OR_HOME_MS
  int returnDepth = 0;            // returnStack.count()
  bool hasOrigin = false;         // the reader was opened from Predicación
};

// PassOn leaves the release to ReaderUtils::handleBackNavigation, which ignores the swipe and sends a
// button Back Home.
enum class Action : uint8_t { PassOn, PopReturn, ToPreaching };

constexpr Action decide(const Swipe& swipe) {
  if (!swipe.shortBackRelease) return Action::PassOn;
  if (swipe.returnDepth > 0) return Action::PopReturn;
  if (swipe.hasOrigin) return Action::ToPreaching;
  return Action::PassOn;
}

}  // namespace ReaderBackSwipe
```

Run:

```bash
cmake --build build/test --target ReaderBackSwipeTest && ./build/test/ui_layout/ReaderBackSwipeTest
```

Expected: builds; `[  PASSED  ] 4 tests.`

Commit: `git add src/activities/reader/ReaderBackSwipe.h test/ui_layout/ReaderBackSwipeTest.cpp test/ui_layout/CMakeLists.txt`

```
feat(reader): decide what the reader's Back does

Pops the return stack first, then goes to the Predicación card the reader was opened from, and
otherwise leaves the release to handleBackNavigation as before.
```

## Step 3 — failing test for the href's verse

Create `test/verse_anchors/VerseHrefTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "activities/reader/VerseHref.h"

namespace {

void expectVerse(const char* href, const uint16_t chapter, const uint16_t verse) {
  const auto parsed = VerseHref::parse(href);
  ASSERT_TRUE(parsed.has_value()) << href;
  EXPECT_EQ(parsed->chapter, chapter) << href;
  EXPECT_EQ(parsed->verse, verse) << href;
}

}  // namespace

TEST(VerseHrefParse, ReadsTheChapterAndVerseOfAPreachingReference) {
  expectVerse("1001061123-split83.xhtml#chapter83_verse18", 83, 18);
}

TEST(VerseHrefParse, ReadsTheLongestReferenceInTheSection) {
  expectVerse("1001061123-split119.xhtml#chapter119_verse105", 119, 105);
}

TEST(VerseHrefParse, ReadsASameFileFragment) { expectVerse("#chapter3_verse16", 3, 16); }

TEST(VerseHrefParse, AcceptsTheLargestUint16Numbers) {
  expectVerse("x.xhtml#chapter65535_verse65535", 65535, 65535);
}

TEST(VerseHrefParse, RefusesAnHrefWithNoVerse) {
  EXPECT_FALSE(VerseHref::parse("1001061123-split83.xhtml").has_value());
  EXPECT_FALSE(VerseHref::parse("1001061123-split83.xhtml#").has_value());
  EXPECT_FALSE(VerseHref::parse("1001061123-split83.xhtml#p12").has_value());
  EXPECT_FALSE(VerseHref::parse("").has_value());
}

TEST(VerseHrefParse, RefusesTrailingJunk) { EXPECT_FALSE(VerseHref::parse("x.xhtml#chapter1_verse2x").has_value()); }

// 65536, not a larger number: %u past UINT32_MAX is undefined and wraps on the host.
TEST(VerseHrefParse, RefusesANumberPastUint16) {
  EXPECT_FALSE(VerseHref::parse("x.xhtml#chapter65536_verse1").has_value());
  EXPECT_FALSE(VerseHref::parse("x.xhtml#chapter1_verse65536").has_value());
}

TEST(VerseHrefOffsetOf, FindsTheMatchingAnchor) {
  const std::vector<VerseAnchors::VerseAnchor> anchors = {{0, 83, 17}, {40, 83, 18}, {90, 83, 19}};
  EXPECT_EQ(VerseHref::offsetOf(anchors, {83, 18}), std::optional<uint32_t>(40));
}

TEST(VerseHrefOffsetOf, MissesAVerseNotInTheScan) {
  const std::vector<VerseAnchors::VerseAnchor> anchors = {{0, 83, 17}, {40, 83, 18}};
  EXPECT_FALSE(VerseHref::offsetOf(anchors, {83, 20}).has_value());
  EXPECT_FALSE(VerseHref::offsetOf({}, {83, 18}).has_value());
}

TEST(VerseHrefOffsetOf, MatchesTheChapterAsWellAsTheVerse) {
  const std::vector<VerseAnchors::VerseAnchor> anchors = {{10, 1, 5}, {20, 2, 5}};
  EXPECT_EQ(VerseHref::offsetOf(anchors, {2, 5}), std::optional<uint32_t>(20));
}

// The href and the scan read the same id grammar, so the offset an href resolves to is the one the
// scan recorded for that marker.
TEST(VerseHrefAgreement, AnHrefResolvesToTheMarkerItsFragmentNames) {
  const char* doc =
      "<html><body><p>"
      "<span id=\"chapter83_verse17\"></span><sup>17</sup> abcde "
      "<span id=\"chapter83_verse18\"></span><sup>18</sup> fghij"
      "</p></body></html>";
  const auto anchors = VerseAnchors::scan(doc, std::strlen(doc));
  ASSERT_EQ(anchors.size(), 2u);
  ASSERT_GT(anchors[1].offset, 0u);

  const auto verse = VerseHref::parse("1001061123-split83.xhtml#chapter83_verse18");
  ASSERT_TRUE(verse.has_value());
  EXPECT_EQ(VerseHref::offsetOf(anchors, *verse), std::optional<uint32_t>(anchors[1].offset));
}
```

Append to the end of `test/verse_anchors/CMakeLists.txt` (a blank line, then):

```cmake
add_executable(VerseHrefTest
  VerseHrefTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(VerseHrefTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(VerseHrefTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(VerseHrefTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(VerseHrefTest)
```

Run:

```bash
cmake -S test -B build/test && cmake --build build/test --target VerseHrefTest
```

Expected: **fails to compile** — `activities/reader/VerseHref.h: No such file or directory`. No commit.

## Step 4 — `VerseHref`

Create `src/activities/reader/VerseHref.h`:

```cpp
#pragma once

#include <Epub/VerseAnchors.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

// The verse a Bible href points at, from its `#chapter<N>_verse<M>` fragment. Verse markers are
// empty spans, which never enter a section's anchor map, so a verse is reached by its marker's offset
// in a VerseAnchors scan rather than by the href's anchor. Arduino-free for test/verse_anchors.
namespace VerseHref {

struct Verse {
  uint16_t chapter;
  uint16_t verse;
};

// The same grammar VerseAnchors reads marker ids with, so an href and a scan agree on what names a
// verse. nullopt for no fragment, another fragment, trailing junk, or a number past uint16_t.
inline std::optional<Verse> parse(const char* href) {
  const char* hash = std::strchr(href, '#');
  if (!hash) return std::nullopt;
  unsigned chapter = 0;
  unsigned verse = 0;
  char tail = '\0';
  if (std::sscanf(hash + 1, "chapter%u_verse%u%c", &chapter, &verse, &tail) != 2) return std::nullopt;
  if (chapter > UINT16_MAX || verse > UINT16_MAX) return std::nullopt;
  return Verse{static_cast<uint16_t>(chapter), static_cast<uint16_t>(verse)};
}

// By value, so the result outlives a temporary `anchors` such as Scanner::take()'s.
inline std::optional<uint32_t> offsetOf(const std::vector<VerseAnchors::VerseAnchor>& anchors, const Verse verse) {
  for (const auto& anchor : anchors) {
    if (anchor.chapter == verse.chapter && anchor.verse == verse.verse) return anchor.offset;
  }
  return std::nullopt;
}

}  // namespace VerseHref
```

Run:

```bash
cmake --build build/test --target VerseHrefTest VerseAnchorsTest && ./build/test/verse_anchors/VerseHrefTest && ./build/test/verse_anchors/VerseAnchorsTest
```

Expected: both build; `VerseHrefTest` `[  PASSED  ] 11 tests.`; `VerseAnchorsTest` passes as before.

Commit: `git add src/activities/reader/VerseHref.h test/verse_anchors/VerseHrefTest.cpp test/verse_anchors/CMakeLists.txt`

```
feat(reader): read the verse a Bible href points at

Verse markers are spans and never reach a section's anchor map, so a preaching reference is opened
by its marker's offset in a VerseAnchors scan.
```

## Step 5 — keep the origin; route Back through the decision

### 5a. `src/activities/reader/EpubReaderActivity.h`

Replace exactly:

```cpp
  // Consumed once by onBookLoaded(); None thereafter.
  ReaderEntryIntent entryIntent;
```

with:

```cpp
  // Consumed once by onBookLoaded(); None thereafter. Its origin is copied into preachingOrigin.
  ReaderEntryIntent entryIntent;
  // Where in Predicación this reader was opened from, for the Back that returns there. Kept for the
  // reader's whole life and never persisted, so Home and sleep drop it.
  PreachingOrigin preachingOrigin;
```

Replace exactly:

```cpp
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh),
        entryIntent(intent) {}
```

with:

```cpp
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh),
        entryIntent(intent),
        preachingOrigin(intent.origin) {}
```

(`preachingOrigin` is declared right after `entryIntent`, so the init order matches the declaration
order and `-Wreorder` stays quiet. `PreachingOrigin` comes in through `ReaderEntryIntent.h`.)

### 5b. `src/activities/reader/EpubReaderActivity.cpp`

In the include block, after `#include "ReaderActivity.h"`, add:

```cpp
#include "ReaderBackSwipe.h"
```

Replace exactly (in `EpubReaderActivity::loop`, today around line 694):

```cpp
  if (returnStack.count() > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }
```

with:

```cpp
  // getHeldTime() can clear the touch held-time override, so it stays behind wasReleased(Back), which
  // sets it for a swipe in this same frame.
  const auto backSwipe = ReaderBackSwipe::decide(
      {.shortBackRelease = mappedInput.wasReleased(MappedInputManager::Button::Back) &&
                           mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS,
       .returnDepth = returnStack.count(),
       .hasOrigin = preachingOrigin.isSet()});
  if (backSwipe == ReaderBackSwipe::Action::PopReturn) {
    restoreSavedPosition();
    return;
  }
  if (backSwipe == ReaderBackSwipe::Action::ToPreaching) {
    LOG_INF("ERS", "Back to Predicación");
    activityManager.goToPreaching(preachingOrigin);
    return;
  }
```

The `if (handleBackNavigation()) {` block that follows stays as it is.

### 5c. Build

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tail -20
```

Expected: `SUCCESS`. The first build in this worktree takes 3–4 minutes. Check for new warnings:

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -n 'warning:' | grep -i 'reader/\(EpubReaderActivity\|ReaderBackSwipe\|VerseHref\)' || echo "no warnings in touched files"
```

Expected: `no warnings in touched files`.

Commit: `git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp`

```
feat(reader): swipe back to the Predicación card

The reader keeps the intent's PreachingOrigin for its whole life. With the return stack empty, Back
goes to that card; with no origin it behaves as before.
```

## Step 6 — open a reference at its verse

### 6a. `src/activities/reader/EpubReaderActivity.h`

Replace exactly:

```cpp
  void navigateToHref(const std::string& href, bool savePosition = false);
```

with:

```cpp
  void navigateToHref(const std::string& href, bool savePosition = false);
  void openReference(const char* href);
  std::optional<uint32_t> referencedVerseOffset(int spineIndex, const char* href);
```

### 6b. `src/activities/reader/EpubReaderActivity.cpp`

In the include block, after `#include "SpineHtmlStream.h"`, add:

```cpp
#include "VerseHref.h"
```

In the anonymous namespace, directly after the end of `feedChapterNumberReader` (the function ending
`return reader->feed(chunk, length, isFinal) && !reader->resolved();` and its `}`), add:

```cpp

bool feedVerseScanner(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<VerseAnchors::Scanner*>(ctx)->feed(chunk, length, isFinal);
}
```

In `EpubReaderActivity::onBookLoaded`, replace exactly:

```cpp
    case ReaderEntryIntent::Route::Href:
      LOG_INF("ERS", "Href entry intent not honoured yet; opening normally");
      return;
```

with:

```cpp
    case ReaderEntryIntent::Route::Href:
      openReference(intent.href);
      return;
```

Directly after the end of `EpubReaderActivity::navigateToHref` (the function ending with
`LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());` and its
`}`), add:

```cpp

// Off-lock and before the first render is requested, as Route::Locate is: the verse scan may inflate
// the chapter, which takes the render lock for its popup.
void EpubReaderActivity::openReference(const char* href) {
  const int spineIndex = epub->resolveHrefToSpineIndex(href);
  if (spineIndex < 0) {
    LOG_ERR("ERS", "Reference did not resolve: %s", href);
    ReaderUtils::showMessage(renderer, tr(STR_LINK_TARGET_NOT_FOUND));
    return;
  }
  const auto offset = referencedVerseOffset(spineIndex, href);
  if (!offset) {
    LOG_ERR("ERS", "Verse of %s not found; opening spine %d at its start", href, spineIndex);
    navigateTo({.spineIndex = spineIndex});
    return;
  }
  navigateTo({.spineIndex = spineIndex, .offsetJump = *offset});
}

std::optional<uint32_t> EpubReaderActivity::referencedVerseOffset(const int spineIndex, const char* href) {
  const auto verse = VerseHref::parse(href);
  if (!verse) return std::nullopt;
  VerseAnchors::Scanner scanner;
  if (!scanner.valid()) {
    LOG_ERR("ERS", "OOM: verse scanner");
    return std::nullopt;
  }
  if (!SpineHtmlStream::stream(epub, spineIndex, renderer, feedVerseScanner, &scanner)) return std::nullopt;
  const auto anchors = scanner.take();
  return VerseHref::offsetOf(anchors, *verse);
}
```

Notes for the implementer (do not put these in the code):
- `SpineHtmlStream::stream` defaults to `WhenMissing::Inflate`, which takes the render lock for its
  popup. That is safe only because `onBookLoaded` runs with no lock held (spec A15). Never call
  `openReference` or `referencedVerseOffset` from inside a `RenderLock` scope.
- `navigateTo` takes the render lock itself; it must not be called under one either.
- `resolveHrefToSpineIndex` takes `const std::string&`; passing `href` builds one temporary.

### 6c. Build

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tail -20
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -n 'warning:' | grep -i 'reader/\(EpubReaderActivity\|ReaderBackSwipe\|VerseHref\)' || echo "no warnings in touched files"
```

Expected: `SUCCESS`, then `no warnings in touched files`.

Commit: `git add src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderActivity.cpp`

```
feat(reader): open a preaching reference at its verse

An OpenHref intent resolves the href's chapter, scans its verse markers as the verse grid does, and
jumps by offset. A verse that is not found opens the chapter at its start and logs.
```

## Step 7 — comments that described the old Back

### 7a. `src/activities/reader/ReaderUtils.h`

Replace exactly:

```cpp
  // The reading surface deliberately has no left-edge swipe-to-exit path: in
  // swipe page-turn mode a right swipe must page back instead. Home remains
  // available through the board's dedicated Home gesture/key. Back swipes stay
  // available in menus and other activities; only this reader-surface handler
  // ignores them. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
```

with:

```cpp
  // This handler never exits on the left-edge swipe: in swipe page-turn mode a
  // right swipe must page back instead. Home remains available through the
  // board's dedicated Home gesture/key. The Epub reader decides the swipe
  // before this, to return through its ReturnStack or to the Predicación card
  // it was opened from (ReaderBackSwipe.h). Back swipes stay available in menus
  // and other activities. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
```

### 7b. `src/activities/reader/ReturnStack.h`

Replace exactly:

```cpp
  // The left-edge swipe is this device's only Return and goes inert once the
  // ring empties, so an evicted entry strands the reader mid-chain rather than
  // costing one step. At 8 bytes a slot, buying the boundary well past any
  // chain a study session walks is the cheap side of that trade.
```

with:

```cpp
  // Back -- the left-edge swipe or a held Left key -- returns through this ring.
  // Once it empties, Back leaves for Predicación if the reader was opened from
  // there; otherwise the swipe goes inert and a held Left goes Home. Either way
  // an evicted entry strands the reader mid-chain rather than costing one step.
  // At 8 bytes a slot, buying the boundary well past any chain a study session
  // walks is the cheap side of that trade.
```

Comment-only change: no rebuild needed.

Commit: `git add src/activities/reader/ReaderUtils.h src/activities/reader/ReturnStack.h`

```
docs(reader): describe Back's way out to Predicación
```

## Step 8 — format and the full check (commit only if formatting changed files)

```bash
git status --short          # must be clean apart from build output, which is gitignored
./bin/clang-format-fix      # unsuffixed: the whole tree, as CI runs it
git status --short
```

If `clang-format-fix` changed any file, review the diff (whitespace only), `git add` exactly those
files and commit `style: clang-format the reader's way back`. It must not touch files outside this
plan's `FILES:` lines; if it does, revert those (`git checkout -- <file>`) and report them.

Then run every host suite this change touched, plus the reader suites next to it:

```bash
cmake --build build/test --target ReaderBackSwipeTest VerseHrefTest VerseAnchorsTest ReaderEntryIntentTest MenuCloseRestoreTest ReturnStackTest
ctest --test-dir build/test --output-on-failure -R 'ReaderBackSwipe|VerseHref|VerseAnchors|ReaderEntryIntent|MenuCloseRestore|ReturnStack'
```

Expected: `100% tests passed`.

And the firmware once more after formatting, only if formatting changed a `.cpp`/`.h`:

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tail -5
```

Expected: `SUCCESS`.

## What the implementer reports back

- The commit list (`git log --oneline origin/feature/preaching-reader-return..HEAD`).
- The ctest summary line and the `pio run` result.
- Any "exact text to replace" that did not match, and what the file held instead.

## Device check (for the owner at the end of the batch; not part of implementation)

The ten checks in the spec's "On the device" section, in particular: Predicación → a reference past
verse 10 → that verse on screen → swipe → the same card; a held Left on the opened verse → the card;
sleep and wake drop the origin; free heap equal across Predicación → Bible → back on the serial log.
