# Bible Book Grid Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Bible book list with a paged grid of the publication's own abbreviations, one page per testament, and show full names in the chapter and verse headers.

**Architecture:** `BibleNav::Scanner` keeps link text and `<strong>` section headings in addition to link targets. A new pure header, `BookGridLayout.h`, picks the column count from the widest label and splits books into section-aligned pages. `BibleNavigationActivity` then treats all three levels as grids. The book level pages through `BookGrid::Layout`; the chapter and verse levels keep `NumberGrid`'s uniform pages. The vertical list window at the book level is deleted.

**Tech Stack:** C++20, ESP32-S3 firmware (PlatformIO, `pio`), expat, FreeInkUI `keyGrid`, GoogleTest host suite (CMake).

**Spec:** `docs/superpowers/specs/2026-09-23-bible-book-grid-design.md`

---

## Ground rules for every task

- **Branch:** work on `feature/bible-book-grid`. Run `./bin/bootstrap` once in a fresh worktree. `pio` lives at `/Volumes/stein/.platformio/penv/bin/pio`.
- **Host tests:**
  - Build and run with `cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test --output-on-failure`.
  - On a fresh configure the first build can race gtest. Re-run once before believing a failure.
- **CLAUDE.md is binding:**
  - no bare `new`;
  - locals under 256 B;
  - `tr()` for every UI string, and new keys go in `lib/I18n/translations/english.yaml` only;
  - never mention an unlanded `STR_*` name in a comment (`gen_i18n.py` scans comments);
  - comments only for a non-obvious *why*.
- **Commits:**
  - Use conventional commit messages.
  - End each message with the line `Claude-Session: https://claude.ai/code/session_01YWFtxUFE3M7RQ1A8C8M6cm`.
  - No co-author or generated-by trailers.

## File map

| File | Change | Responsibility |
|---|---|---|
| `lib/Epub/Epub/BibleNavScanner.h/.cpp` | Modify | Also collect `<a>` text and `<strong>` headings; new `takeBookNav()` |
| `test/bible_nav_scanner/BibleNavLabelsTest.cpp` | Create | Tests against a verbatim `nwt_S` excerpt |
| `test/bible_nav_scanner/CMakeLists.txt` | Modify | Add the new test source to `BibleNavScannerTest` |
| `src/activities/reader/BookGridLayout.h` | Create | Pure column choice and section-aligned pagination |
| `test/number_grid/BookGridLayoutTest.cpp` | Create | Host tests for `BookGridLayout.h` |
| `test/number_grid/CMakeLists.txt` | Modify | Add `BookGridLayoutTest` executable |
| `src/activities/reader/BibleNavigationActivity.h/.cpp` | Modify | Book level becomes a grid; header titles; remove list window |
| `docs/superpowers/specs/2026-09-13-bible-number-grid-design.md` | Modify | One-line note that the book-list non-goal is superseded |

---

### Task 1: Scanner collects link text and section headings

**Files:**
- Modify: `lib/Epub/Epub/BibleNavScanner.h`
- Modify: `lib/Epub/Epub/BibleNavScanner.cpp`
- Create: `test/bible_nav_scanner/BibleNavLabelsTest.cpp`
- Modify: `test/bible_nav_scanner/CMakeLists.txt`

- [ ] **Step 1: Wire the new test file into the existing executable**

In `test/bible_nav_scanner/CMakeLists.txt`, change the `add_executable` block to:

```cmake
add_executable(BibleNavScannerTest
  BibleNavScannerTest.cpp
  BibleNavLabelsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BibleNavScanner.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
```

- [ ] **Step 2: Write the failing tests**

Create `test/bible_nav_scanner/BibleNavLabelsTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <string>

#include "BibleNavScanner.h"

namespace {

// Verbatim structure of nwt_S.epub's OEBPS/biblebooknav.xhtml, trimmed to two
// rows per section. The spacer row carries a U+00A0 outside any <a> or
// <strong>, and most <a>s are preceded by a literal space inside the <td>.
const std::string kSpanishBookNav =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<html dir=\"ltr\" class=\"dir-ltr\" xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"es\">\n"
    "<head>\n<title>Navegaci\xC3\xB3n por la Biblia</title>\n<meta charset=\"utf-8\" />\n"
    "<link rel=\"stylesheet\" href=\"css/epubs.css\" type=\"text/css\" />\n</head>\n"
    "<body dir=\"ltr\" xml:lang=\"es\" class=\"jwac dir-ltr ml-S ms-ROMAN pub-nwt\">"
    "<table class=\"w_navigation w_bibleBook\"><tr><td colspan=\"5\"><strong>Escrituras Hebreas</strong></td></tr>\n"
    "<tr><td class=\"w_navigation w_bibleBook\"><a href=\"biblechapternav1.xhtml\">G\xC3\xA9n.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav2.xhtml\">\xC3\x89x.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav13.xhtml\">1 Cr\xC3\xB3n.</a></td></tr>\n"
    "<tr><td colspan=\"5\">\xC2\xA0 </td></tr>\n"
    "<tr><td colspan=\"5\"><strong>Escrituras Griegas</strong></td></tr>\n"
    "<tr><td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav40.xhtml\">Mat.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"1001061169.xhtml\">Jud.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav66.xhtml\">Apoc.</a></td></tr></table>\n"
    "</body>\n</html>\n";

BibleNav::BookNavPage scanBookNav(const std::string& xhtml) {
  BibleNav::Scanner scanner;
  EXPECT_TRUE(scanner.valid());
  EXPECT_TRUE(scanner.feed(xhtml.data(), xhtml.size(), /*isFinal=*/true));
  return scanner.takeBookNav();
}

}  // namespace

TEST(BibleNavLabels, LinkTextIsTheAbbreviationInDocumentOrder) {
  const auto page = scanBookNav(kSpanishBookNav);

  ASSERT_EQ(page.labels.size(), 6u);
  EXPECT_EQ(page.labels[0], "G\xC3\xA9n.");
  EXPECT_EQ(page.labels[1], "\xC3\x89x.");
  EXPECT_EQ(page.labels[2], "1 Cr\xC3\xB3n.");
  EXPECT_EQ(page.labels[5], "Apoc.");
}

TEST(BibleNavLabels, TargetsStayParallelToLabels) {
  const auto page = scanBookNav(kSpanishBookNav);

  ASSERT_EQ(page.targets.size(), page.labels.size());
  EXPECT_EQ(page.targets[0], "biblechapternav1.xhtml");
  EXPECT_EQ(page.targets[4], "1001061169.xhtml");
}

TEST(BibleNavLabels, HeadingsRecordTheFirstBookAfterThem) {
  const auto page = scanBookNav(kSpanishBookNav);

  ASSERT_EQ(page.sections.size(), 2u);
  EXPECT_EQ(page.sections[0].title, "Escrituras Hebreas");
  EXPECT_EQ(page.sections[0].firstLink, 0);
  EXPECT_EQ(page.sections[1].title, "Escrituras Griegas");
  EXPECT_EQ(page.sections[1].firstLink, 3);
}

TEST(BibleNavLabels, WhitespaceIsTrimmedAndCollapsed) {
  const std::string xhtml =
      "<html><body><a href=\"a.xhtml\">\n  1   Sam.\t</a><strong>  Two\n Words </strong></body></html>";
  const auto page = scanBookNav(xhtml);

  ASSERT_EQ(page.labels.size(), 1u);
  EXPECT_EQ(page.labels[0], "1 Sam.");
  ASSERT_EQ(page.sections.size(), 1u);
  EXPECT_EQ(page.sections[0].title, "Two Words");
  EXPECT_EQ(page.sections[0].firstLink, 1);
}

TEST(BibleNavLabels, TextInsideNestedElementsIsKept) {
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\"><span>Gen</span>.</a></body></html>");

  ASSERT_EQ(page.labels.size(), 1u);
  EXPECT_EQ(page.labels[0], "Gen.");
}

TEST(BibleNavLabels, AnEmptyLinkYieldsAnEmptyLabel) {
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\"></a><a href=\"b.xhtml\">B</a></body></html>");

  ASSERT_EQ(page.labels.size(), 2u);
  EXPECT_EQ(page.labels[0], "");
  EXPECT_EQ(page.labels[1], "B");
}

TEST(BibleNavLabels, APageWithoutHeadingsHasNoSections) {
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\">A</a></body></html>");

  EXPECT_TRUE(page.sections.empty());
  EXPECT_EQ(page.labels.size(), 1u);
}

TEST(BibleNavLabels, OverlongTextIsCappedOnACharacterBoundary) {
  // 40 two-byte characters: 80 bytes, well past the cap.
  std::string longText;
  for (int i = 0; i < 40; i++) longText += "\xC3\xA9";
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\">" + longText + "</a></body></html>");

  ASSERT_EQ(page.labels.size(), 1u);
  EXPECT_LE(page.labels[0].size(), BibleNav::MAX_TEXT_BYTES);
  EXPECT_EQ(page.labels[0].size() % 2, 0u);
}

TEST(BibleNavLabels, AMalformedPageYieldsNothing) {
  BibleNav::Scanner scanner;
  const std::string broken = "<html><body><a href=\"a.xhtml\">A</b></body></html>";
  EXPECT_FALSE(scanner.feed(broken.data(), broken.size(), /*isFinal=*/true));

  const auto page = scanner.takeBookNav();
  EXPECT_TRUE(page.targets.empty());
  EXPECT_TRUE(page.labels.empty());
  EXPECT_TRUE(page.sections.empty());
}

TEST(BibleNavLabels, TakeStillReturnsOnlyTargets) {
  BibleNav::Scanner scanner;
  ASSERT_TRUE(scanner.feed(kSpanishBookNav.data(), kSpanishBookNav.size(), /*isFinal=*/true));

  const auto targets = scanner.take();
  ASSERT_EQ(targets.size(), 6u);
  EXPECT_EQ(targets[5], "biblechapternav66.xhtml");
}
```

- [ ] **Step 3: Run the tests to confirm they fail**

Run: `cmake -S test -B build/test && cmake --build build/test --target BibleNavScannerTest 2>&1 | tail -5`
Expected: compile errors: `BookNavPage` is not a member of `BibleNav`, and `takeBookNav` and `MAX_TEXT_BYTES` are missing.

- [ ] **Step 4: Add the API to the header**

In `lib/Epub/Epub/BibleNavScanner.h`, after `CHAPTER_NAV_PREFIX`, add:

```cpp
// Link and heading text past this many bytes is cut on a UTF-8 boundary, so a
// malformed page cannot grow memory without bound. The longest real
// abbreviation measured (nwt_S) is 7 characters.
inline constexpr size_t MAX_TEXT_BYTES = 47;

struct BookNavSection {
  std::string title;
  // Index into BookNavPage::targets of the first link after the heading.
  int firstLink = 0;
};

// biblebooknav.xhtml read in full: each link's target and visible text (the
// publication's own abbreviation), plus the <strong> headings grouping them.
struct BookNavPage {
  std::vector<std::string> targets;
  std::vector<std::string> labels;
  std::vector<BookNavSection> sections;
};
```

In `class Scanner`, after `take()`, add:

```cpp
  // Targets, labels and sections together. Empty after a failed feed. Like
  // take(), moves the collected data out.
  BookNavPage takeBookNav();
```

- [ ] **Step 5: Implement text collection in the scanner**

In `lib/Epub/Epub/BibleNavScanner.cpp`, replace the `State` struct and `onStart` with the following, and add `onEnd` and `onText`:

```cpp
struct State {
  std::vector<std::string> links;
  std::vector<std::string> labels;
  std::vector<BookNavSection> sections;
  // Nesting depth inside the current <a> / <strong>; text is collected while
  // either is positive so child elements like <span> keep their text.
  int linkDepth = 0;
  int headingDepth = 0;
  // Whether the element that opened linkDepth was an <a> we recorded a target
  // for; an <a> without href has no row, so its text must not become one.
  bool linkRecorded = false;
  std::string pendingText;
};

void appendCapped(std::string& out, const char* text, const int length) {
  for (int i = 0; i < length; i++) {
    const char c = text[i];
    const bool whitespace = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (whitespace) {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  if (out.size() <= MAX_TEXT_BYTES) return;
  size_t cut = MAX_TEXT_BYTES;
  // Back off UTF-8 continuation bytes so the cap never splits a character.
  while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
  out.resize(cut);
}

std::string trimmed(std::string text) {
  while (!text.empty() && text.back() == ' ') text.pop_back();
  const size_t first = text.find_first_not_of(' ');
  return first == std::string::npos ? std::string() : text.substr(first);
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<State*>(userData);
  if (self->linkDepth > 0) {
    self->linkDepth++;
    return;
  }
  if (self->headingDepth > 0) {
    self->headingDepth++;
    return;
  }
  if (strcmp(name, "strong") == 0) {
    self->headingDepth = 1;
    self->pendingText.clear();
    return;
  }
  if (strcmp(name, "a") != 0) return;

  self->linkDepth = 1;
  self->linkRecorded = false;
  self->pendingText.clear();
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], "href") != 0) continue;
    std::string_view tail = filenameTail(atts[i + 1]);
    // Chapter targets carry no fragment in these publications, but a stray one
    // would otherwise become part of the filename and match no spine entry.
    const size_t hash = tail.find('#');
    if (hash != std::string_view::npos) tail = tail.substr(0, hash);
    if (!tail.empty() && self->links.size() < MAX_LINKS_PER_PAGE) {
      self->links.emplace_back(tail);
      self->linkRecorded = true;
    }
    break;
  }
}

void XMLCALL onEnd(void* userData, const XML_Char*) {
  auto* self = static_cast<State*>(userData);
  if (self->linkDepth > 0) {
    if (--self->linkDepth == 0 && self->linkRecorded) self->labels.push_back(trimmed(std::move(self->pendingText)));
    return;
  }
  if (self->headingDepth > 0 && --self->headingDepth == 0) {
    self->sections.push_back(
        BookNavSection{trimmed(std::move(self->pendingText)), static_cast<int>(self->links.size())});
  }
}

void XMLCALL onText(void* userData, const XML_Char* text, const int length) {
  auto* self = static_cast<State*>(userData);
  if (self->linkDepth > 0 || self->headingDepth > 0) appendCapped(self->pendingText, text, length);
}
```

In the `Scanner()` constructor, reserve labels and register the new handlers. Replace:

```cpp
  state->links.reserve(MAX_LINKS_PER_PAGE);
```

with:

```cpp
  state->links.reserve(MAX_LINKS_PER_PAGE);
  state->labels.reserve(MAX_LINKS_PER_PAGE);
```

and replace:

```cpp
  XML_SetStartElementHandler(parser, onStart);
```

with:

```cpp
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
```

After `Scanner::take()`, add:

```cpp
BookNavPage Scanner::takeBookNav() {
  if (!state_ || failed_) return {};
  auto* state = static_cast<State*>(state_);
  BookNavPage page;
  page.targets = std::move(state->links);
  page.labels = std::move(state->labels);
  page.sections = std::move(state->sections);
  return page;
}
```

Also check `MAX_LINKS_PER_PAGE`'s previous uses. The old `onStart` did not cap `emplace_back`; the new one caps at 151. Check that no existing test in `BibleNavScannerTest.cpp` feeds more than 151 links:

```bash
grep -n "151\|152\|MAX_LINKS" test/bible_nav_scanner/BibleNavScannerTest.cpp
```

If one does, keep the uncapped behaviour by removing `&& self->links.size() < MAX_LINKS_PER_PAGE` from the condition. The activity already truncates to `MAX_BOOKS` and `MAX_CHAPTERS`.

- [ ] **Step 6: Run the scanner suite**

Run: `cmake --build build/test --target BibleNavScannerTest && ctest --test-dir build/test -R "BibleNav" --output-on-failure`
Expected: all `BibleNavLabels.*` tests pass, and every pre-existing `BibleNavScanner.*` test still passes.

- [ ] **Step 7: Commit**

```bash
git add lib/Epub/Epub/BibleNavScanner.h lib/Epub/Epub/BibleNavScanner.cpp test/bible_nav_scanner/
git commit -m "feat: read book abbreviations and sections from the Bible nav page"
```

---

### Task 2: Pure book-grid layout

**Files:**
- Create: `src/activities/reader/BookGridLayout.h`
- Create: `test/number_grid/BookGridLayoutTest.cpp`
- Modify: `test/number_grid/CMakeLists.txt`

- [ ] **Step 1: Add the test executable**

Append to `test/number_grid/CMakeLists.txt`:

```cmake
add_executable(BookGridLayoutTest
  BookGridLayoutTest.cpp
)

target_include_directories(BookGridLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BookGridLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookGridLayoutTest)
```

- [ ] **Step 2: Write the failing tests**

Create `test/number_grid/BookGridLayoutTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "activities/reader/BookGridLayout.h"

namespace {

constexpr int PORTRAIT_W = 480;
constexpr int PORTRAIT_H = 650;
// "1 Crón." in the 12pt UI body font, rounded up.
constexpr int SPANISH_WIDEST_LABEL = 64;
constexpr int NWT_SECTION_STARTS[] = {0, 39};

}  // namespace

TEST(BookGridColumns, SpanishAbbreviationsGetFiveColumnsInPortrait) {
  EXPECT_EQ(BookGrid::columnsFor(PORTRAIT_W, SPANISH_WIDEST_LABEL), 5);
}

TEST(BookGridColumns, WiderLabelsGetFewerColumnsButNeverBelowTheFloor) {
  EXPECT_LT(BookGrid::columnsFor(PORTRAIT_W, 120), 5);
  EXPECT_EQ(BookGrid::columnsFor(PORTRAIT_W, 1000), BookGrid::MIN_COLS);
}

TEST(BookGridColumns, TinyLabelsAreCappedAtTheCeiling) {
  EXPECT_EQ(BookGrid::columnsFor(PORTRAIT_W, 4), BookGrid::MAX_COLS);
}

TEST(BookGridPages, TheNwtSplitsIntoOnePagePerTestament) {
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  ASSERT_EQ(layout.pageCount, 2);
  EXPECT_EQ(layout.pages[0].first, 0);
  EXPECT_EQ(layout.pages[0].count, 39);
  EXPECT_EQ(layout.pages[0].section, 0);
  EXPECT_EQ(layout.pages[1].first, 39);
  EXPECT_EQ(layout.pages[1].count, 27);
  EXPECT_EQ(layout.pages[1].section, 1);
  EXPECT_EQ(layout.cols, 5);
  EXPECT_EQ(layout.rows, 8);
}

TEST(BookGridPages, EveryPageStaysWithinTheCellCap) {
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  EXPECT_LE(layout.cols * layout.rows, NumberGrid::MAX_CELLS);
  for (int p = 0; p < layout.pageCount; p++) EXPECT_LE(layout.pages[p].count, layout.cols * layout.rows);
}

TEST(BookGridPages, NoSectionsPagesContinuously) {
  const auto layout = BookGrid::layoutFor(66, nullptr, 0, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  ASSERT_GE(layout.pageCount, 2);
  EXPECT_EQ(layout.pages[0].section, -1);
  int covered = 0;
  for (int p = 0; p < layout.pageCount; p++) {
    EXPECT_EQ(layout.pages[p].first, covered);
    covered += layout.pages[p].count;
  }
  EXPECT_EQ(covered, 66);
}

TEST(BookGridPages, ASectionLargerThanAPageSplitsWithinItself) {
  // A short rect forces few rows, so the 39-book section cannot fit one page.
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, 200, SPANISH_WIDEST_LABEL);

  ASSERT_GT(layout.pageCount, 2);
  EXPECT_EQ(layout.pages[0].section, 0);
  EXPECT_EQ(layout.pages[1].section, 0);
  EXPECT_EQ(layout.pages[layout.pageCount - 1].section, 1);
  int covered = 0;
  for (int p = 0; p < layout.pageCount; p++) covered += layout.pages[p].count;
  EXPECT_EQ(covered, 66);
}

TEST(BookGridPages, InvalidSectionStartsFallBackToContinuous) {
  constexpr int outOfRange[] = {0, 90};
  constexpr int unordered[] = {39, 0};
  constexpr int notFromZero[] = {5, 39};

  for (const int* starts : {outOfRange, unordered, notFromZero}) {
    const auto layout = BookGrid::layoutFor(66, starts, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);
    EXPECT_EQ(layout.pages[0].section, -1);
  }
}

TEST(BookGridPages, PageOfFindsTheOwningPage) {
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  EXPECT_EQ(BookGrid::pageOf(layout, 0), 0);
  EXPECT_EQ(BookGrid::pageOf(layout, 38), 0);
  EXPECT_EQ(BookGrid::pageOf(layout, 39), 1);
  EXPECT_EQ(BookGrid::pageOf(layout, 65), 1);
  EXPECT_EQ(BookGrid::pageOf(layout, 200), 1);
  EXPECT_EQ(BookGrid::pageOf(layout, -3), 0);
}

TEST(BookGridPages, LayoutFitsTheStackBudget) {
  static_assert(sizeof(BookGrid::Layout) < 128, "Layout is returned by value on the render stack");
  SUCCEED();
}

TEST(BookGridPages, NoBooksMeansNoPages) {
  const auto layout = BookGrid::layoutFor(0, nullptr, 0, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);
  EXPECT_EQ(layout.pageCount, 0);
}
```

- [ ] **Step 3: Run to confirm failure**

Run: `cmake -S test -B build/test && cmake --build build/test --target BookGridLayoutTest 2>&1 | tail -3`
Expected: `fatal error: activities/reader/BookGridLayout.h: No such file or directory`.

- [ ] **Step 4: Implement the header**

Create `src/activities/reader/BookGridLayout.h`:

```cpp
#pragma once

#include <algorithm>
#include <cstdint>

#include "NumberGridLayout.h"

// Layout arithmetic for the Bible book level: how many abbreviation columns fit,
// and how books split into pages that never straddle a section (testament).
// Free of FreeInkUI, Arduino and GfxRenderer so the host suite can exercise it
// (test/number_grid); the widest label is measured by the caller.
namespace BookGrid {

constexpr int MIN_COLS = 3;
constexpr int MAX_COLS = 6;
// Horizontal room a key needs around its label so the text never touches the
// key border.
constexpr int LABEL_PADDING = 16;
// Two testaments in the NWT; a publication with more headings than this pages
// continuously instead.
constexpr int MAX_SECTIONS = 4;
// Layout is returned by value onto the render task's stack, so it is kept well
// under the 256-byte local budget: 12 pages of 6 bytes. 66 books need at most
// 12 pages at MIN_COLS x 2 rows, and any real content rect earns far more rows.
constexpr int MAX_PAGES = 12;

struct Page {
  int16_t first = 0;
  int16_t count = 0;
  // Index into the caller's section titles, or -1 when paging continuously.
  int16_t section = -1;
};

struct Layout {
  int cols = 0;
  int rows = 0;
  Page pages[MAX_PAGES] = {};
  int pageCount = 0;
};

inline int columnsFor(const int contentW, const int widestLabelPx, const int gap = NumberGrid::GAP) {
  const int stride = std::max(widestLabelPx, 0) + LABEL_PADDING + gap;
  const int fit = contentW > 0 ? (contentW + gap) / stride : 0;
  return std::clamp(fit, MIN_COLS, MAX_COLS);
}

// Section starts are usable only when they begin at book 0, strictly increase
// and stay inside the book count; anything else is a page we do not understand.
inline bool sectionsUsable(const int bookCount, const int* sectionStarts, const int sectionCount) {
  if (!sectionStarts || sectionCount <= 0 || sectionCount > MAX_SECTIONS) return false;
  if (sectionStarts[0] != 0) return false;
  for (int s = 1; s < sectionCount; s++) {
    if (sectionStarts[s] <= sectionStarts[s - 1] || sectionStarts[s] >= bookCount) return false;
  }
  return true;
}

inline void appendPages(Layout& layout, const int first, const int count, const int section) {
  const int perPage = layout.cols * layout.rows;
  for (int offset = 0; offset < count && layout.pageCount < MAX_PAGES; offset += perPage) {
    layout.pages[layout.pageCount++] = Page{static_cast<int16_t>(first + offset),
                                            static_cast<int16_t>(std::min(perPage, count - offset)),
                                            static_cast<int16_t>(section)};
  }
}

inline Layout layoutFor(const int bookCount, const int* sectionStarts, const int sectionCount, const int contentW,
                        const int contentH, const int widestLabelPx, const int gap = NumberGrid::GAP) {
  Layout layout;
  if (bookCount <= 0) return layout;

  const bool sectioned = sectionsUsable(bookCount, sectionStarts, sectionCount);
  int largestRun = bookCount;
  if (sectioned) {
    largestRun = 0;
    for (int s = 0; s < sectionCount; s++) {
      const int end = s + 1 < sectionCount ? sectionStarts[s + 1] : bookCount;
      largestRun = std::max(largestRun, end - sectionStarts[s]);
    }
  }

  layout.cols = columnsFor(contentW, widestLabelPx, gap);
  const int rowsNeeded = (largestRun + layout.cols - 1) / layout.cols;
  const int rowsFit = contentH > 0 ? (contentH + gap) / (NumberGrid::MIN_CELL + gap) : 0;
  layout.rows = std::max(1, std::min({rowsNeeded, rowsFit, NumberGrid::MAX_CELLS / layout.cols}));

  if (!sectioned) {
    appendPages(layout, 0, bookCount, -1);
    return layout;
  }
  for (int s = 0; s < sectionCount; s++) {
    const int end = s + 1 < sectionCount ? sectionStarts[s + 1] : bookCount;
    appendPages(layout, sectionStarts[s], end - sectionStarts[s], s);
  }
  return layout;
}

inline int pageOf(const Layout& layout, const int book) {
  if (layout.pageCount <= 0) return 0;
  for (int p = layout.pageCount - 1; p > 0; p--) {
    if (book >= layout.pages[p].first) return p;
  }
  return 0;
}

}  // namespace BookGrid
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build/test --target BookGridLayoutTest && ctest --test-dir build/test -R BookGrid --output-on-failure`
Expected: all `BookGrid*` tests pass.

These checks fix the numbers the tests expect:
- **Columns:** stride = 64 + 16 + 8 = 88, and (480 + 8) / 88 = 5.
- **Rows:** rowsNeeded = ceil(39 / 5) = 8; rowsFit = (650 + 8) / 64 = 10; the cell cap allows 48 / 5 = 9. The smallest is 8.
- **Short rect:** (200 + 8) / 64 = 3 rows, so 15 books a page and the Hebrew section needs 3 pages.

- [ ] **Step 6: Commit**

```bash
git add src/activities/reader/BookGridLayout.h test/number_grid/
git commit -m "feat: lay out Bible books in section-aligned grid pages"
```

---

### Task 3: Book level becomes a grid; headers show full names

**Files:**
- Modify: `src/activities/reader/BibleNavigationActivity.h`
- Modify: `src/activities/reader/BibleNavigationActivity.cpp`

No host test is possible here, because the activity needs FreeInkUI and GfxRenderer. The logic it depends on is covered by Tasks 1 and 2. Verification is `pio run`, `pio check` and the device checklist.

- [ ] **Step 1: Header changes**

In `BibleNavigationActivity.h`:

1. Add `#include "BookGridLayout.h"` beside `#include "NumberGridLayout.h"`.
2. Replace the class comment's book-level sentence (line 21) with:

```cpp
// Every level is a paged grid. The book level shows the publication's own
// abbreviations from biblebooknav.xhtml, one page per testament heading; the
// chapter and verse levels are number grids, so a high reference costs pages
// instead of screens. Tap or Confirm a chapter to list its verses.
```

3. Remove `ROW_WINDOW`, `windowLabels`, `windowItems`, `windowStart`, `windowCount` and `refreshRowWindow`. Delete the whole block from `// Matches EpubReaderChapterSelectionActivity` (the `ROW_WINDOW` constant) and the `// Book level only: the row window...` block.
4. After `BOOK_NAME_BYTES`, add:

```cpp
  // The publication's abbreviation for each book, cell labels at the book level.
  static constexpr int BOOK_ABBREV_BYTES = 16;
  // Room for a full book name, a space and a chapter number.
  static constexpr int HEADER_TITLE_BYTES = BOOK_NAME_BYTES + 8;
```

5. After `int bookCount = 0;`, add:

```cpp
  char bookAbbrev[MAX_BOOKS][BOOK_ABBREV_BYTES] = {};
  char sectionTitle[BookGrid::MAX_SECTIONS][BOOK_NAME_BYTES] = {};
  int sectionStart[BookGrid::MAX_SECTIONS] = {};
  int sectionCount = 0;
  // Measured once in loadBooks(); the column count follows from it.
  int widestAbbrevPx = 0;
  BookGrid::Layout bookLayout{};
  char headerTitle[HEADER_TITLE_BYTES] = {};
  char pageIndicator[12] = {};
```

6. Replace `bool isGridLevel() const { return level != Level::Book; }` and the `buildNumberGrid` declaration with:

```cpp
  void buildGrid(UiScreen& screen);
  // Page arithmetic that differs by level: the book level pages by section
  // (bookLayout), the number levels by NumberGrid's uniform pages.
  int gridPageCount() const;
  int gridPageOf(int index) const;
  int gridPageFirst(int page) const;
  int gridCellsPerPage() const;
  const char* cellLabel(int row, int cell);
  // Grid top inset: the book level adds a section sub-header below the title.
  int subHeaderHeight() const;
```

- [ ] **Step 2: Load abbreviations and sections**

In `BibleNavigationActivity.cpp`, add `#include "BookGridLayout.h"` beside the other local includes. In `loadBooks()`, replace:

```cpp
  std::vector<std::string> targets = scanner.take();
  if (targets.empty()) return false;
```

with:

```cpp
  BibleNav::BookNavPage page = scanner.takeBookNav();
  std::vector<std::string>& targets = page.targets;
  if (targets.empty()) return false;
```

After the existing `for (int i = 0; i < bookCount; i++) { ... copyTruncated(bookName[i], ...) }` loop, add:

```cpp
  const int bodyFont = uiScaleSpec().bodyFontId;
  widestAbbrevPx = 0;
  for (int i = 0; i < bookCount; i++) {
    const bool hasAbbrev = i < static_cast<int>(page.labels.size()) && !page.labels[i].empty();
    copyTruncated(bookAbbrev[i], BOOK_ABBREV_BYTES, hasAbbrev ? page.labels[i] : std::string(bookName[i]));
    widestAbbrevPx = std::max(widestAbbrevPx, renderer.getTextWidth(bodyFont, bookAbbrev[i]));
  }

  sectionCount = 0;
  for (const auto& section : page.sections) {
    if (sectionCount == BookGrid::MAX_SECTIONS) break;
    copyTruncated(sectionTitle[sectionCount], BOOK_NAME_BYTES, section.title);
    sectionStart[sectionCount] = section.firstLink;
    sectionCount++;
  }
```

- [ ] **Step 3: Level-aware page arithmetic**

Add these member definitions after `listCount()`:

```cpp
int BibleNavigationActivity::gridCellsPerPage() const {
  return level == Level::Book ? bookLayout.cols * bookLayout.rows : grid.cellsPerPage();
}

int BibleNavigationActivity::gridPageCount() const {
  return level == Level::Book ? bookLayout.pageCount : NumberGrid::pageCount(listCount(), grid.cellsPerPage());
}

int BibleNavigationActivity::gridPageOf(const int index) const {
  return level == Level::Book ? BookGrid::pageOf(bookLayout, index)
                              : NumberGrid::pageOfIndex(index, grid.cellsPerPage());
}

int BibleNavigationActivity::gridPageFirst(const int page) const {
  if (level != Level::Book) return NumberGrid::pageFirstCell(page, grid.cellsPerPage());
  if (page < 0 || page >= bookLayout.pageCount) return 0;
  return bookLayout.pages[page].first;
}

int BibleNavigationActivity::subHeaderHeight() const {
  return level == Level::Book && sectionCount > 0 ? UITheme::getInstance().getMetrics().tabBarHeight : 0;
}
```

- [ ] **Step 4: Replace every grid-level branch**

Now that every level is a grid:

1. **`enterLevel`:** replace the `if (isGridLevel()) { ... } else { nav.follow(listCount()); }` block with:

```cpp
    // reset() leaves visibleRows at 1 and only syncToProps -- the list path,
    // which no level takes -- ever writes it, so follow(), scrollBy() and
    // pageRows() would treat a single cell as a whole viewport. One grid "row"
    // is one page; the first build corrects it once geometry is known.
    nav.visibleRows = gridCellsPerPage() > 0 ? gridCellsPerPage() : 1;
    nav.top = gridPageFirst(gridPageOf(nav.selected));
```

   Also remove the `windowStart = -1;` and `windowCount = 0;` lines.

2. **`refreshRowWindow`:** delete it entirely.

3. **`moveGridSelection`:** replace `nav.top = NumberGrid::pageStartFor(clamped, count, grid.cellsPerPage());` with `nav.top = gridPageFirst(gridPageOf(clamped));`.

4. **`handleCustomInput`:** replace the body with:

```cpp
  if (gridCellsPerPage() <= 0) return false;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;

  const int page = gridPageOf(nav.top);
  const int next = swipe == MappedInputManager::SwipeDir::Up ? page + 1 : page - 1;
  // Consumed either way: the base loop would otherwise scroll the viewport a
  // single cell off its page boundary.
  if (next >= 0 && next < gridPageCount()) moveGridSelection(gridPageFirst(next));
  return true;
```

5. **`navigateButtons`:** replace the body with:

```cpp
  const int cols = level == Level::Book ? bookLayout.cols : grid.cols;
  if (cols <= 0) {
    UiListActivity::navigateButtons();
    return;
  }
  buttonNavigator.onNextRelease([this, cols] { moveGridSelection(nav.selected + cols); });
  buttonNavigator.onPreviousRelease([this, cols] { moveGridSelection(nav.selected - cols); });
  buttonNavigator.onNextContinuous([this] {
    const int page = gridPageOf(nav.selected);
    moveGridSelection(page + 1 < gridPageCount() ? gridPageFirst(page + 1) : listCount() - 1);
  });
  buttonNavigator.onPreviousContinuous([this] {
    const int page = gridPageOf(nav.selected);
    moveGridSelection(page > 0 ? gridPageFirst(page - 1) : 0);
  });
```

6. **`buildScreen`:**
   - Add `subHeaderHeight()` to the top inset: `static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + subHeaderHeight())`.
   - Replace everything after the empty-state `return;` with `buildGrid(screen);`, which deletes the `fui::ListProps` book-list path.

- [ ] **Step 5: One grid builder for all levels**

Rename `buildNumberGrid` to `buildGrid` and replace its body with:

```cpp
void BibleNavigationActivity::buildGrid(UiScreen& screen) {
  const fui::Rect body = screen.body();
  const int count = listCount();

  int rows = 0;
  int cols = 0;
  int pageFirst = 0;
  int pageCells = 0;
  if (level == Level::Book) {
    bookLayout = BookGrid::layoutFor(bookCount, sectionStart, sectionCount, body.width, body.height, widestAbbrevPx);
    if (bookLayout.pageCount == 0) return;
    rows = bookLayout.rows;
    cols = bookLayout.cols;
    const int page = BookGrid::pageOf(bookLayout, nav.selected);
    pageFirst = bookLayout.pages[page].first;
    pageCells = bookLayout.pages[page].count;
    snprintf(pageIndicator, sizeof(pageIndicator), "%d/%d", page + 1, bookLayout.pageCount);
  } else {
    grid = NumberGrid::geometryFor(body.width, body.height);
    rows = grid.rows;
    cols = grid.cols;
    pageFirst = NumberGrid::pageStartFor(nav.top, count, grid.cellsPerPage());
    pageCells = NumberGrid::cellsOnPage(count, pageFirst, grid.cellsPerPage());
  }
  const int cellsPerPage = rows * cols;

  // A geometry change re-pages around the selection rather than leaving
  // nav.top on a page the new geometry no longer has.
  nav.visibleRows = cellsPerPage;
  nav.top = pageFirst;

  for (int i = 0; i < cellsPerPage; i++) {
    const int row = pageFirst + i;
    fui::KeyGridKey cell;
    if (i < pageCells) {
      cell.label = cellLabel(row, i);
      // ACTION_ROW dispatch (onRowAction) indexes the level by this value, so
      // it is the absolute row, not the cell's place on the page.
      cell.value = static_cast<int16_t>(row);
    } else {
      // The page stays rectangular, with uniform cells across pages; a disabled
      // cell registers no interaction.
      cell.kind = fui::KeyKind::Disabled;
      cell.enabled = false;
    }
    cells[i] = cell;
  }

  fui::KeyGridProps props;
  props.keys = cells;
  props.rows = static_cast<uint8_t>(rows);
  props.cols = static_cast<uint8_t>(cols);
  // keyGrid compares this against a page-relative cell index, unlike the
  // absolute value each cell carries.
  props.selectedIndex = static_cast<int16_t>(NumberGrid::pageRelativeIndex(nav.selected, pageFirst, cellsPerPage));
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.gap = NumberGrid::GAP;
  // Above the cell size, ensureMinTouchRect would grow each hit rect past its
  // own cell and neighbouring cells would swallow each other's taps.
  props.minTouchSize = static_cast<int16_t>(
      NumberGrid::cellSizeFor(body.width, body.height, NumberGrid::Geometry{cols, rows}));
  props.labelText = screen.theme().bodyText;
  props.labelText.align = fui::TextAlign::Center;
  props.keyStyles = screen.theme().key;
  fui::keyGrid(screen.frame(), body, props);
}

const char* BibleNavigationActivity::cellLabel(const int row, const int cell) {
  if (level == Level::Book) return bookAbbrev[row];
  const unsigned number =
      level == Level::Verse ? static_cast<unsigned>(verseAnchors[row].verse) : static_cast<unsigned>(row + 1);
  snprintf(cellLabels[cell], CELL_LABEL_BYTES, "%u", number);
  return cellLabels[cell];
}
```

`MAX_GRID_CELLS` is 48, and `BookGrid::layoutFor` caps `rows * cols` at `NumberGrid::MAX_CELLS`, so `cells[i]` stays in bounds.

- [ ] **Step 6: Record the book for single-chapter books**

In `activateIndex`, `Level::Book` case, move `selectedBook = index;` above the `if (bookIsDirect[index])` block. Back from a single-chapter book's verses (`onBackButton`) and the verse header both read `selectedBook`. Today it is stale on that path.

```cpp
    case Level::Book:
      selectedBook = index;
      // The five single-chapter books have no chapter level, so their row is
      // the only route to their verses.
      if (bookIsDirect[index]) {
        openVerseList(bookTargetSpine[index], -1);
        return;
      }
      if (!loadChapters(index)) {
```

- [ ] **Step 7: Titles and the section sub-header**

Replace `drawChrome()` with:

```cpp
void BibleNavigationActivity::drawChrome() {
  const bool hasBook = selectedBook >= 0 && selectedBook < bookCount && bookName[selectedBook][0] != '\0';
  const char* title = tr(STR_SELECT_BOOK);
  if (level == Level::Chapter) {
    title = hasBook ? bookName[selectedBook] : tr(STR_SELECT_CHAPTER);
  } else if (level == Level::Verse) {
    if (hasBook) {
      const int chapter = selectedChapterRow >= 0 ? selectedChapterRow + 1 : 1;
      snprintf(headerTitle, sizeof(headerTitle), "%s %d", bookName[selectedBook], chapter);
      title = headerTitle;
    } else {
      title = tr(STR_SELECT_VERSE);
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, title);

  if (subHeaderHeight() == 0 || bookLayout.pageCount == 0) return;
  const int page = BookGrid::pageOf(bookLayout, nav.selected);
  const int section = bookLayout.pages[page].section;
  GUI.drawSubHeader(renderer,
                    Rect{safe.x, safe.y + metrics.topPadding + metrics.headerHeight, safe.width, subHeaderHeight()},
                    section >= 0 ? sectionTitle[section] : "", pageIndicator);
}
```

**Check the draw order.** `drawChrome` must run after `buildScreen` in the same render, so `pageIndicator` and `bookLayout` are current. Read `UiListActivity::render` (`src/activities/UiListActivity.cpp`). If `drawChrome` runs first, compute the page and indicator from `bookLayout` in `drawChrome` itself: the previous build's layout is still valid for the current selection. In either case, never read `pageIndicator` before the first build (`bookLayout.pageCount == 0` guards that).

**Check the sub-header rect.** Look at `ButtonRemapActivity.cpp:130` for how an existing screen passes the `tabBarHeight` rect to `drawSubHeader`, and match it.

- [ ] **Step 8: Build the firmware**

Serialise the build with other agents:

```bash
L=/tmp/berean-pio.lock; until mkdir "$L" 2>/dev/null; do sleep 10; done
/Volumes/stein/.platformio/penv/bin/pio run; rc=$?; rmdir "$L"; echo "exit $rc"
```

Expected: `SUCCESS`, and no warnings from `src/activities/reader/BibleNavigationActivity.cpp`, `BookGridLayout.h` or `BibleNavScanner.cpp`. If the compiler reports unused `isGridLevel`, `refreshRowWindow` or `windowItems`, a Step 4 deletion was missed. Remove the stragglers.

- [ ] **Step 9: Static analysis and format**

```bash
L=/tmp/berean-pio.lock; until mkdir "$L" 2>/dev/null; do sleep 10; done
/Volumes/stein/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high; rc=$?; rmdir "$L"; echo "exit $rc"
./bin/clang-format-fix && git diff --exit-code
```

Expected: `No defects found`, exit 0, and a clean diff after formatting. If formatting changed files, stage them into this task's commit.

- [ ] **Step 10: Commit**

```bash
git add src/activities/reader/BibleNavigationActivity.h src/activities/reader/BibleNavigationActivity.cpp
git commit -m "feat: show Bible books as a grid of abbreviations by testament"
```

---

### Task 4: Docs, full verification, PR

**Files:**
- Modify: `docs/superpowers/specs/2026-09-13-bible-number-grid-design.md`

- [ ] **Step 1: Mark the superseded non-goal**

In `2026-09-13-bible-number-grid-design.md`, under `## Non-goals`, append to the "Gridding the book list" bullet:

```markdown
  *Superseded 2026-09-23 by `2026-09-23-bible-book-grid-design.md`: the publication's own
  `biblebooknav.xhtml` supplies the abbreviations.*
```

- [ ] **Step 2: Full host suite**

Run: `cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test --output-on-failure 2>&1 | tail -3`
Expected: `100% tests passed`.

- [ ] **Step 3: Whole-tree format, then commit**

```bash
./bin/clang-format-fix && git diff --exit-code
git add docs/superpowers/specs/2026-09-13-bible-number-grid-design.md
git commit -m "docs: note the book grid supersedes the list non-goal"
```

- [ ] **Step 4: Push and open the PR**

Write the body to a file and pass it with `--body-file`, because backticks in `--body` execute. The body covers:
- what changed, with file:line references;
- the verification actually run (test counts, `pio run` and `pio check` results);
- the device checklist below, numbered;
- `Closes` is not needed, because no issue exists;
- the final line `https://claude.ai/code/session_01YWFtxUFE3M7RQ1A8C8M6cm`.

```bash
git push -u origin feature/bible-book-grid
gh pr create --repo victorstein/berean-os --base main \
  --title "feat: show Bible books as a grid of their abbreviations" --body-file /tmp/pr-body.md
```

Device checklist for the PR body:

1. Open the Bible, then book navigation. Page 1 is titled "Escrituras Hebreas" with `1/2`, and shows 39 abbreviations, about 5 per row. Every label fits its cell, including `1 Crón.`.
2. Swipe up: page 2 is "Escrituras Griegas" `2/2` with 27 books. Swipe down returns to page 1.
3. Tap `Sof.`: the chapter grid's header reads `Sofonías`. Tap `2`: the verse grid's header reads `Sofonías 2`.
4. Tap `Abd.`, a single-chapter book: the verse grid opens with header `Abdías 1`. Back returns to the book grid on page 1 with `Abd.` selected.
5. Buttons: Up and Down move by a row, and holding moves a page. From the last Hebrew row, Down continues into the Greek page.
6. Back from the chapter grid returns to the book grid, on the page of the book you came from.
7. `ESP.getFreeHeap()` is unchanged within noise across opening and closing book navigation five times.

- [ ] **Step 5: CI**

Run: `gh pr checks <n> --repo victorstein/berean-os --watch`, then confirm with `gh pr view <n> --repo victorstein/berean-os --json statusCheckRollup`. `gh pr checks` collapses runs that share a name. Fix any failure before handing back.
