# bereanOS Phase 1 — the study store

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the per-book highlight store with a publication-keyed, unit-addressed tag store, and migrate the user's existing 48 tags and 63 tagged passages into it without losing one.

**Architecture:** A tagged passage is addressed by a `Unit` — a verse, a numbered paragraph, or a raw document offset — rather than by a byte position in a file. Two expat scanners produce units; a per-publication index caches them lazily, one document at a time. The migration reads the old store, resolves each old `(spineIndex, offset)` pair through that index, and writes a global store beside it. The old store is never deleted.

**Tech Stack:** C++20, expat (`XML_GE=0`), ArduinoJson v7, GoogleTest on the host, PlatformIO/ESP-IDF on the device.

---

## What the real data says

Every decision below is measured against the user's own device backup
(`berean-os-backup-2026-09-14/`) and against four real publications extracted
locally. Numbers, not assumptions:

| | |
|---|---|
| Tags in palette | **48** (2 with zero usage: `igualdad`, `transformación`) |
| Tagged passages | **63**, none untagged |
| Distinct spine documents marked | **50** of the NWT's 3,937 |
| Marked documents that are verse-addressable | **50 / 50** |
| Highlights resolving to a `Verse` unit | **63 / 63** |
| Span length | 47–268 codepoints, median 114 |

**The migration was dry-run before this plan was written.** A host tool built
from the repo's own `VerseAnchors.cpp` and `lib/expat` resolved all 63 stored
offsets against the real NWT documents and compared each result against the
`ref` string the device saved alongside it:

```
 total=63  match=63  mismatch=0  no-anchor=0
```

So Phase 1 is not discovering whether this works. It is productionising a
resolution that is already known-correct on the only data that matters.

### Corrections to the design spec

Four claims in `2026-09-13-berean-os-design.md` did not survive measurement.
They are corrected here and the spec should be amended when this plan lands.

1. **The spec's NWT verse-document count (2,445) is wrong; it is 1,189.** The
   2,445 figure counted every file containing the *string* `chapter<N>_verse<M>`,
   which includes cross-reference `<a href="…#chapter10_verse1">` links. Only
   1,189 files carry `id="chapter<N>_verse<M>"` — an actual marker.

2. **Every verse-addressable document is also paragraph-addressable.** Measured
   on `nwtfull`: 1,342 documents carry `data-pid`, 1,189 carry verse ids, and the
   intersection is 1,189 — i.e. all of them. The spec treated the two scanners as
   covering disjoint publications. They overlap completely inside the Bible, so
   **a precedence rule is mandatory**, not optional. Coverage is therefore:
   1,189 Verse, 153 Paragraph-only, 2,595 DocumentOffset.

3. **`dc:identifier` cannot supply the pubkey.** The spec derives a pubkey from
   symbol/issue/language. The OPF of `w_S_202601` carries
   `<dc:identifier id="BookId">urn:uuid:18854DEA-…</dc:identifier>` — a random
   UUID, with the symbol appearing nowhere in the metadata. Task 4 replaces the
   assumed derivation with a resolution ladder.

4. **The `Unit` address had no book, so the Bible's language-free pubkey did
   not work.** The spec keys Bible passages on `bible` precisely so Salmos
   119:145 and Psalm 119:145 are one object — but its `Unit` carries only
   chapter, verse and offset, and the only other field naming the book is
   `document`, the filename. Filenames are language-specific: the Spanish NWT's
   Matthew is `1001061105-split*.xhtml` and the English edition's is not. As
   specified, the two renderings could never resolve to the same passage, and
   Genesis 1:1 and Matthew 1:1 collide outright.

   **Fix:** `Unit` gains a `book` field, 1–66, taken from the position of the
   book in `biblebooknav.xhtml` — which `BibleNavScanner` already reads and
   which lists the canon in the same order in every language. This is the one
   structural change this plan makes to the spec's data model.

### Three facts about `data-pid` the scanner depends on

Measured across 180 documents of `w_S_202601` and `lff_S`:

- **Units never nest.** 0 occurrences of a `data-pid` element inside another.
  So the anchors are a flat sequence and "greatest anchor at or below offset"
  resolves correctly, exactly as `VerseAnchors::find` already does.
- **`data-pid` is unique within a document.** 0 duplicates. It is a valid address.
- **`data-pid` order is not document order.** In 40 of 180 documents the values
  run e.g. `1,2,3,4,5,6,40,7,42,8` — study-question `<div class="gen-field">`
  boxes are interleaved with body paragraphs at high pid values. **Anchors sort
  by offset; the pid is the address, never the sort key.** A binary search on pid
  would be wrong in 22% of documents.
- The attribute appears on `p`, `div`, `h1`, `h2`, `h3`, `h4` and `legend`, so
  the scanner keys on the **attribute name**, never an element whitelist. Note
  `data-rel-pid` also exists and must not match.

---

## File structure

**New, host-testable, no Arduino and no HalStorage** — these are the units that
carry the correctness risk, so every one of them is pure:

| File | Responsibility |
|---|---|
| `lib/StudyStore/Unit.h` / `.cpp` | The `Unit` value type, ordering, and its JSON encoding |
| `lib/StudyStore/UnitAnchors.h` / `.cpp` | Façade: run the right scanner(s) for a document, apply precedence |
| `lib/Epub/Epub/ParagraphAnchors.h` / `.cpp` | The `data-pid` expat scanner |
| `lib/StudyStore/UnitFingerprint.h` / `.cpp` | Length + CRC32 over a unit's visible codepoints |
| `lib/StudyStore/TaggedPassage.h` | The record |
| `lib/StudyStore/PassageDoc.h` / `.cpp` | Format rules for one publication's passages: parse, validate, serialise, budget |
| `lib/StudyStore/TagPalette.h` / `.cpp` | Global tags: ids, names, tombstones, `nextTagId` |
| `lib/StudyStore/PubKey.h` / `.cpp` | The pubkey resolution ladder |
| `lib/StudyStore/UnitIndexFormat.h` / `.cpp` | The on-disk unit-index header and document table |
| `lib/StudyStore/MigrationPlanner.h` / `.cpp` | Pure: old doc + anchors → passages + report rows |

**New, firmware-only** — thin storage and lifecycle shells over the above:

| File | Responsibility |
|---|---|
| `src/study/PassageFile.{h,cpp}` | Atomic, budgeted read/write of `/.berean/passages/<pubkey>.json` |
| `src/study/TagPaletteFile.{h,cpp}` | Same for `/.berean/tags.json` |
| `src/study/UnitIndexCache.{h,cpp}` | Lazy per-document build, invalidation, `/.berean/units/<pubkey>.bin` |
| `src/study/TagIndexFile.{h,cpp}` | The reverse index, rebuildable from the passages files |
| `src/study/StudyStore.{h,cpp}` | The one object the activities talk to |
| `src/study/MigrationRunner.{h,cpp}` | Drives the migration, writes `migration-report.json` |

**Modified:** the five activities that speak `HighlightDoc` today
(`EpubReaderActivity`, `HighlightsActivity`, `PassageSelectActivity`,
`TagFilterActivity`, `TagPickerActivity`), plus `CrossPointWebServer.cpp` for the
report route and `main.cpp` for the boot-time migration call.

**Untouched:** `lib/Epub/Epub/HighlightDoc.*`, `src/util/HighlightFile.*`. They
stay compiled and working — they are what reads the old store during migration,
and what an OTA rollback to a Phase 0 build falls back on.

---

## What Phase 1 deliberately does not change

**The UI stays as it is.** The launcher, the four sections and the new input
model are Phase 2. Phase 1 rewires the five existing activities onto the new
store and leaves every screen looking exactly as it does today. That keeps the
diff honest: if a screen changes in Phase 1, something is wrong.

Consequence for device testing: the acceptance signal is the migration report
and the tag list contents, **not** how anything looks.

---

## Two design decisions this plan makes, and why

### 1. Migration resolves addresses when it can, and defers when it cannot

The spec says old pairs "resolve through the freshly built unit index" during
migration. That silently assumes the EPUB is on the card and still at the path
the highlight file was named after. Neither is guaranteed: the card can be out,
and `pathflatten::toCacheName` bakes the old path into the filename.

So each passage migrates in one of two states:

- **Resolved** — the source EPUB was found, the document scanned, and the passage
  carries a `Verse` or `Paragraph` unit.
- **Pending** — the EPUB was absent or unreadable. The passage is written with a
  `DocumentOffset` unit carrying the original `(spineIndex, offset)` and a
  `pendingUpgrade` flag. The next time that document is opened, `UnitIndexCache`
  upgrades it in place.

Nothing is ever dropped for want of a file, and the migration never blocks on
storage that may not be there. On the user's actual device all 63 will resolve
immediately — Pending is the safety net, not the expected path.

### 2. The reverse index is derived, and rebuilt rather than repaired

`/.berean/tagindex/<tagid>.bin` is a cache of a question the passages files can
always answer. Treating it as authoritative creates a consistency problem with
no transaction to solve it — and `POST /delete` on the web server can remove a
passages file behind its back. So the index header carries a generation counter
that every passages write bumps; on mismatch the index is **rebuilt from the
passages files**, not patched. A corrupt or stale index is a slow query, never
wrong data.

At today's scale (one publication, 63 passages) the rebuild is milliseconds. The
index earns its place when Buscar lands in Phase 3.

---

## Task 1: The `Unit` value type

**Files:**
- Create: `lib/StudyStore/Unit.h`, `lib/StudyStore/Unit.cpp`
- Create: `test/unit_type/UnitTest.cpp`, `test/unit_type/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/unit_type/UnitTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "StudyStore/Unit.h"

namespace {

TEST(UnitOrdering, VerseSortsByBookThenChapterThenVerseThenOffset) {
  const study::Unit a{study::UnitKind::Verse, 40, 1, 8, 0};
  const study::Unit b{study::UnitKind::Verse, 40, 1, 9, 0};
  const study::Unit c{study::UnitKind::Verse, 40, 2, 1, 0};
  const study::Unit d{study::UnitKind::Verse, 41, 1, 1, 0};
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b < c);
  EXPECT_TRUE(c < d);
  EXPECT_FALSE(d < a);
}

TEST(UnitOrdering, SameUnitOrdersByOffset) {
  const study::Unit a{study::UnitKind::Verse, 40, 1, 8, 4};
  const study::Unit b{study::UnitKind::Verse, 40, 1, 8, 12};
  EXPECT_TRUE(a < b);
}

// The whole reason `book` exists: the Bible's pubkey carries no language, and
// the document filename that would otherwise name the book differs between the
// Spanish and English NWT. Two renderings of one verse must compare equal.
TEST(UnitIdentity, TheSameVerseInTwoLanguagesIsOneAddress) {
  const study::Unit salmos{study::UnitKind::Verse, 19, 119, 145, 0};
  const study::Unit psalm{study::UnitKind::Verse, 19, 119, 145, 0};
  EXPECT_EQ(salmos, psalm);
}

TEST(UnitIdentity, TheSameChapterAndVerseInDifferentBooksAreNotEqual) {
  const study::Unit genesis{study::UnitKind::Verse, 1, 1, 1, 0};
  const study::Unit matthew{study::UnitKind::Verse, 40, 1, 1, 0};
  EXPECT_NE(genesis, matthew) << "without `book` these collide and a mark lands in the wrong book";
}

// data-pid values run out of document order in 22% of real documents, so the
// pid is an identifier and must never be used to order two paragraphs.
TEST(UnitOrdering, ParagraphDoesNotClaimAnOrderingByPid) {
  const study::Unit later{study::UnitKind::Paragraph, 0, 0, 7, 0};
  const study::Unit earlier{study::UnitKind::Paragraph, 0, 0, 40, 0};
  EXPECT_FALSE(study::orderableByAddress(later, earlier));
}

TEST(UnitRoundTrip, EncodesAndDecodesEachKind) {
  for (const study::Unit u : {study::Unit{study::UnitKind::Verse, 19, 119, 145, 3},
                              study::Unit{study::UnitKind::Paragraph, 0, 0, 40, 0},
                              study::Unit{study::UnitKind::DocumentOffset, 0, 0, 0, 1255}}) {
    EXPECT_EQ(study::unitFromCompact(study::unitToCompact(u)), u);
  }
}

TEST(UnitRoundTrip, RejectsAnUnknownKind) {
  EXPECT_FALSE(study::unitFromCompact("z:1:2:3:4").has_value());
}

TEST(UnitRoundTrip, RejectsABookOutsideTheCanon) {
  EXPECT_FALSE(study::unitFromCompact("v:67:1:1:0").has_value());
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitTest
```

Expected: FAIL — `StudyStore/Unit.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/Unit.h`**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Where a tagged passage lives, independent of byte positions in any one file.
//
// A Verse unit carries `book` because the Bible's pubkey is language-free, and
// the only other thing naming the book is the document filename -- which is
// language-SPECIFIC (the Spanish NWT's Matthew is 1001061105-split*.xhtml, the
// English one's is not). Without `book`, "Salmos 119:145 and Psalm 119:145 are
// the same verse" is not expressible. The number is the position in
// biblebooknav.xhtml, which lists the 66 books in canonical order in every
// language (BibleNavScanner.h).
//
// A document supports exactly one kind, chosen by UnitAnchors: Verse where the
// document carries chapter<N>_verse<M> markers, Paragraph where it carries
// data-pid, DocumentOffset otherwise. Measured on the NWT: 1,189 documents are
// Verse, 153 Paragraph, 2,595 DocumentOffset.
namespace study {

enum class UnitKind : uint8_t { DocumentOffset = 0, Paragraph = 1, Verse = 2 };

struct Unit {
  UnitKind kind = UnitKind::DocumentOffset;
  uint8_t book = 0;     // canonical Bible book 1-66 for Verse; 0 otherwise
  uint16_t major = 0;   // chapter for Verse; 0 otherwise
  uint16_t minor = 0;   // verse for Verse; data-pid for Paragraph; 0 otherwise
  uint32_t offset = 0;  // codepoints into the unit, or into the document

  bool operator==(const Unit&) const = default;
};

// Verse units carry a real sequence; Paragraph units do not, because data-pid
// is assigned by JW's content system and runs out of document order in 22% of
// real documents. Callers that need paragraph order must consult the anchor
// list's offsets instead.
bool orderableByAddress(const Unit& a, const Unit& b);

bool operator<(const Unit& a, const Unit& b);

// "v:19:119:145:3" (Psalms 119:145 +3), "p:0:0:40:0", "d:0:0:0:1255" --
// kind:book:major:minor:offset, stable across format versions and cheap to
// eyeball in a migration report.
std::string unitToCompact(const Unit& u);
std::optional<Unit> unitFromCompact(const std::string& s);

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/Unit.cpp`**

```cpp
#include "StudyStore/Unit.h"

#include <cstdio>

namespace study {
namespace {

char kindLetter(const UnitKind k) {
  switch (k) {
    case UnitKind::Verse: return 'v';
    case UnitKind::Paragraph: return 'p';
    case UnitKind::DocumentOffset: return 'd';
  }
  return 'd';
}

std::optional<UnitKind> kindFromLetter(const char c) {
  switch (c) {
    case 'v': return UnitKind::Verse;
    case 'p': return UnitKind::Paragraph;
    case 'd': return UnitKind::DocumentOffset;
    default: return std::nullopt;
  }
}

}  // namespace

bool orderableByAddress(const Unit& a, const Unit& b) {
  return a.kind == b.kind && a.kind != UnitKind::Paragraph;
}

bool operator<(const Unit& a, const Unit& b) {
  if (a.book != b.book) return a.book < b.book;
  if (a.major != b.major) return a.major < b.major;
  if (a.minor != b.minor) return a.minor < b.minor;
  return a.offset < b.offset;
}

std::string unitToCompact(const Unit& u) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%c:%u:%u:%u:%u", kindLetter(u.kind), u.book, u.major, u.minor, u.offset);
  return buf;
}

std::optional<Unit> unitFromCompact(const std::string& s) {
  if (s.size() < 2 || s[1] != ':') return std::nullopt;
  const auto kind = kindFromLetter(s[0]);
  if (!kind) return std::nullopt;

  unsigned book = 0, major = 0, minor = 0, offset = 0;
  char tail = '\0';
  if (sscanf(s.c_str() + 2, "%u:%u:%u:%u%c", &book, &major, &minor, &offset, &tail) != 4) return std::nullopt;
  if (book > 66 || major > UINT16_MAX || minor > UINT16_MAX) return std::nullopt;

  return Unit{*kind, static_cast<uint8_t>(book), static_cast<uint16_t>(major), static_cast<uint16_t>(minor), offset};
}

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/unit_type/CMakeLists.txt`:

```cmake
add_executable(UnitTest
  UnitTest.cpp
  ${REPO_ROOT}/lib/StudyStore/Unit.cpp
)

target_include_directories(UnitTest PRIVATE ${REPO_ROOT}/lib)

target_link_libraries(UnitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitTest)
```

Append to `test/CMakeLists.txt`, beside the other `add_subdirectory` lines:

```cmake
add_subdirectory(unit_type)
```

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitTest && ./build/unit_type/UnitTest
```

Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/Unit.h lib/StudyStore/Unit.cpp test/unit_type test/CMakeLists.txt
git commit -m "feat: add the Unit address type"
```

---

## Task 2: The `data-pid` scanner

Mirrors `VerseAnchors` exactly — same `VisibleOffsetCounter`, same three expat
handlers plus the entity-expanding default handler. Differs in what it matches
and what it stores.

**Files:**
- Create: `lib/Epub/Epub/ParagraphAnchors.h`, `lib/Epub/Epub/ParagraphAnchors.cpp`
- Create: `test/paragraph_anchors/ParagraphAnchorsTest.cpp`, `test/paragraph_anchors/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/paragraph_anchors/ParagraphAnchorsTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstring>

#include "ParagraphAnchors.h"

namespace {

// The shape real Watchtower documents use: body paragraphs interleaved with
// high-numbered study-question boxes, and a data-rel-pid attribute alongside.
const char* kDoc =
    "<html><head><title>skipme</title></head><body>"
    "<p id=\"p6\" data-pid=\"6\">alpha</p>"
    "<div class=\"gen-field\" id=\"p40\" data-pid=\"40\">bravo</div>"
    "<p id=\"p7\" data-pid=\"7\" data-rel-pid=\"[39]\">charlie</p>"
    "<h2 data-pid=\"10\">delta</h2>"
    "<legend data-pid=\"11\">echo</legend>"
    "<p>unnumbered</p>"
    "</body></html>";

TEST(ParagraphAnchorsScan, FindsEveryElementCarryingThePidAttribute) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_EQ(a.size(), 5u) << "p, div, h2 and legend all carry data-pid in real books";
  EXPECT_EQ(a[0].pid, 6);
  EXPECT_EQ(a[1].pid, 40);
  EXPECT_EQ(a[2].pid, 7);
  EXPECT_EQ(a[3].pid, 10);
  EXPECT_EQ(a[4].pid, 11);
}

TEST(ParagraphAnchorsScan, IsAscendingByOffsetNotByPid) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_EQ(a.size(), 5u);
  for (size_t i = 1; i < a.size(); ++i) EXPECT_LT(a[i - 1].offset, a[i].offset);
  EXPECT_GT(a[1].pid, a[2].pid) << "pid order is not document order; this is the normal case";
}

TEST(ParagraphAnchorsScan, DoesNotMatchDataRelPid) {
  const char* doc = "<html><body><p data-rel-pid=\"[39]\">alpha</p></body></html>";
  EXPECT_TRUE(ParagraphAnchors::scan(doc, strlen(doc)).empty());
}

TEST(ParagraphAnchorsScan, DoesNotCountTextOutsideBody) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_FALSE(a.empty());
  EXPECT_EQ(a[0].offset, 0u) << "<title> sits in <head> and must not advance the count";
}

TEST(ParagraphAnchorsScan, RejectsANonNumericPid) {
  const char* doc = "<html><body><p data-pid=\"7x\">alpha</p></body></html>";
  EXPECT_TRUE(ParagraphAnchors::scan(doc, strlen(doc)).empty());
}

TEST(ParagraphAnchorsFind, ReturnsTheGreatestAnchorAtOrBelowTheOffset) {
  const auto a = ParagraphAnchors::scan(kDoc, strlen(kDoc));
  ASSERT_EQ(a.size(), 5u);
  const auto* found = ParagraphAnchors::find(a, a[2].offset + 2);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->pid, 7);
}

TEST(ParagraphAnchorsFind, ReturnsNullBelowTheFirstAnchor) {
  std::vector<ParagraphAnchors::ParagraphAnchor> a{{10, 5}};
  EXPECT_EQ(ParagraphAnchors::find(a, 4), nullptr);
}

TEST(ParagraphAnchorsScan, ReturnsEmptyOnMalformedMarkup) {
  const char* doc = "<html><body><p data-pid=\"1\">alpha";
  EXPECT_TRUE(ParagraphAnchors::scan(doc, strlen(doc)).empty())
      << "a truncated list would resolve later passages to a stale anchor";
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target ParagraphAnchorsTest
```

Expected: FAIL — `ParagraphAnchors.h: No such file or directory`.

- [ ] **Step 3: Write `lib/Epub/Epub/ParagraphAnchors.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Resolves a visible-codepoint offset inside one spine item to the numbered
// paragraph containing it. JW publications put `data-pid` on the block element
// itself (`<p id="p7" data-pid="7">`); `data-pnum` is a different thing -- the
// printed paragraph number on a <span class="parNum"> -- and is not an address.
//
// Three properties of real markup this relies on, measured across 180 documents
// of w_S_202601 and lff_S:
//   * data-pid elements never nest, so the anchors are a flat sequence and
//     find() can take the greatest anchor at or below an offset.
//   * pid values are unique within a document, so a pid is a valid address.
//   * pid order is NOT document order -- interleaved study-question boxes carry
//     high pids -- so this list is sorted by OFFSET and the pid is payload.
//
// Counting mirrors VerseAnchors: the same VisibleOffsetCounter and the same
// entity-expanding default handler, because a deficit of one codepoint names
// the previous paragraph.
namespace ParagraphAnchors {

struct ParagraphAnchor {
  uint32_t offset;
  uint16_t pid;
};

// Ascending by offset. Empty when the document has no data-pid, and also empty
// when the parse fails part-way.
std::vector<ParagraphAnchor> scan(const char* xhtml, size_t length);

class Scanner {
 public:
  Scanner();
  ~Scanner();
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;

  bool valid() const { return parser_ != nullptr; }
  bool feed(const char* chunk, size_t length, bool isFinal);
  std::vector<ParagraphAnchor> take();

 private:
  void* parser_ = nullptr;
  void* state_ = nullptr;
  bool failed_ = false;
};

// The anchor covering `offset` -- the greatest one at or below it -- or nullptr.
const ParagraphAnchor* find(const std::vector<ParagraphAnchor>& anchors, uint32_t offset);

}  // namespace ParagraphAnchors
```

- [ ] **Step 4: Write `lib/Epub/Epub/ParagraphAnchors.cpp`**

Copy `lib/Epub/Epub/VerseAnchors.cpp` and change three things: the attribute
match, the value grammar, and the reserve. Everything else — `onText`, `onEnd`,
`onDefault`, the `Scanner` lifecycle, the `scan`/`find` bodies — is identical and
must stay identical.

```cpp
#include "ParagraphAnchors.h"

#include <expat.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "VisibleOffsetCounter.h"
#include "htmlEntities.h"

namespace ParagraphAnchors {
namespace {

struct State {
  VisibleOffsetCounter counter;
  std::vector<ParagraphAnchor> anchors;
};

void XMLCALL onText(void* userData, const XML_Char* text, const int len) {
  static_cast<State*>(userData)->counter.onCharacterData(text, len);
}

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<State*>(userData);
  self->counter.onStartElement(name);
  if (!self->counter.insideBody) return;

  for (int i = 0; atts && atts[i]; i += 2) {
    // Exact match: data-rel-pid sits beside data-pid on the same elements and
    // is a reference to another paragraph, not this one's address.
    if (strcmp(atts[i], "data-pid") != 0) continue;
    unsigned pid = 0;
    char tail = '\0';
    if (sscanf(atts[i + 1], "%u%c", &pid, &tail) == 1 && pid <= UINT16_MAX) {
      self->anchors.push_back({self->counter.offset, static_cast<uint16_t>(pid)});
    }
    break;
  }
}

void XMLCALL onEnd(void* userData, const XML_Char* name) { static_cast<State*>(userData)->counter.onEndElement(name); }

// See VerseAnchors.cpp: under XML_GE=0 every undeclared entity arrives here
// rather than at the character handler, and an uncounted `&nbsp;` shifts every
// later offset by one -- enough to name the previous paragraph.
void XMLCALL onDefault(void* userData, const XML_Char* s, const int len) {
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (value != nullptr) {
      onText(userData, value, static_cast<int>(strlen(value)));
      return;
    }
    onText(userData, s, len);
  }
}

}  // namespace

Scanner::Scanner() {
  auto* state = new (std::nothrow) State();
  if (!state) return;
  state->anchors.reserve(64);  // the densest measured document carries 58

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    delete state;
    return;
  }
  XML_SetUserData(parser, state);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  XML_SetDefaultHandlerExpand(parser, onDefault);

  parser_ = parser;
  state_ = state;
}

Scanner::~Scanner() {
  if (parser_) XML_ParserFree(static_cast<XML_Parser>(parser_));
  delete static_cast<State*>(state_);
}

bool Scanner::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!parser_ || failed_) return false;
  const XML_Status status =
      XML_Parse(static_cast<XML_Parser>(parser_), chunk, static_cast<int>(length), isFinal ? 1 : 0);
  if (status == XML_STATUS_ERROR) {
    failed_ = true;
    return false;
  }
  return true;
}

std::vector<ParagraphAnchor> Scanner::take() {
  if (failed_ || !state_) return {};
  return std::move(static_cast<State*>(state_)->anchors);
}

std::vector<ParagraphAnchor> scan(const char* xhtml, const size_t length) {
  Scanner scanner;
  if (!scanner.valid()) return {};
  if (!scanner.feed(xhtml, length, true)) return {};
  return scanner.take();
}

const ParagraphAnchor* find(const std::vector<ParagraphAnchor>& anchors, const uint32_t offset) {
  const ParagraphAnchor* best = nullptr;
  for (const auto& a : anchors) {
    if (a.offset > offset) break;
    best = &a;
  }
  return best;
}

}  // namespace ParagraphAnchors
```

- [ ] **Step 5: Register the test**

`test/paragraph_anchors/CMakeLists.txt` — same shape as
`test/verse_anchors/CMakeLists.txt`, which compiles expat from the repo's own
sources with the firmware's flags:

```cmake
add_executable(ParagraphAnchorsTest
  ParagraphAnchorsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ParagraphAnchorsTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(ParagraphAnchorsTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(ParagraphAnchorsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ParagraphAnchorsTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(paragraph_anchors)
```

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target ParagraphAnchorsTest && ./build/paragraph_anchors/ParagraphAnchorsTest
```

Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/Epub/Epub/ParagraphAnchors.h lib/Epub/Epub/ParagraphAnchors.cpp test/paragraph_anchors test/CMakeLists.txt
git commit -m "feat: scan data-pid paragraph anchors"
```

---

## Task 3: `UnitAnchors` — one document, one kind

The precedence rule. Measured on the NWT, all 1,189 verse-marked documents also
carry `data-pid`, so without this every Bible passage could be addressed two
ways and the migration would pick whichever scanner ran last.

**Files:**
- Create: `lib/StudyStore/UnitAnchors.h`, `lib/StudyStore/UnitAnchors.cpp`
- Create: `test/unit_anchors/UnitAnchorsTest.cpp`, `test/unit_anchors/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/unit_anchors/UnitAnchorsTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstring>

#include "StudyStore/UnitAnchors.h"

namespace {

// Every verse-marked document in the NWT also carries data-pid. This is that
// shape, and Verse must win.
const char* kBibleDoc =
    "<html><body><p id=\"p3\" data-pid=\"3\">"
    "<span id=\"chapter1_verse7\"></span><strong><sup>7</sup></strong> alpha bravo"
    "<span id=\"chapter1_verse8\"></span><strong><sup>8</sup></strong> charlie delta"
    "</p></body></html>";

const char* kArticleDoc =
    "<html><body><p id=\"p6\" data-pid=\"6\">alpha</p>"
    "<p id=\"p7\" data-pid=\"7\">bravo</p></body></html>";

const char* kPlainDoc = "<html><body><p>alpha bravo charlie</p></body></html>";

TEST(UnitAnchorsKind, VerseWinsWhereBothMarkersExist) {
  const auto a = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  EXPECT_EQ(a.kind, study::UnitKind::Verse);
  ASSERT_EQ(a.anchors.size(), 2u);
  EXPECT_EQ(a.anchors[0].major, 1);
  EXPECT_EQ(a.anchors[0].minor, 7);
  EXPECT_EQ(a.anchors[1].minor, 8);
}

TEST(UnitAnchorsKind, ParagraphWhereOnlyPidExists) {
  const auto a = study::scanUnits(kArticleDoc, strlen(kArticleDoc));
  EXPECT_EQ(a.kind, study::UnitKind::Paragraph);
  ASSERT_EQ(a.anchors.size(), 2u);
  EXPECT_EQ(a.anchors[0].major, 0);
  EXPECT_EQ(a.anchors[0].minor, 6);
}

TEST(UnitAnchorsKind, DocumentOffsetWhereNeitherExists) {
  const auto a = study::scanUnits(kPlainDoc, strlen(kPlainDoc));
  EXPECT_EQ(a.kind, study::UnitKind::DocumentOffset);
  EXPECT_TRUE(a.anchors.empty());
}

TEST(UnitAnchorsResolve, MapsAnOffsetToAUnitWithAnOffsetInsideIt) {
  auto a = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  a.book = 40;  // Matthew, as the index builder would stamp it
  const study::Unit u = study::resolve(a, a.anchors[1].offset + 4);
  EXPECT_EQ(u.kind, study::UnitKind::Verse);
  EXPECT_EQ(u.book, 40) << "the book must reach the address, or two languages cannot agree";
  EXPECT_EQ(u.major, 1);
  EXPECT_EQ(u.minor, 8);
  EXPECT_EQ(u.offset, 4u) << "offset is relative to the unit, not the document";
}

TEST(UnitAnchorsResolve, FallsBackToDocumentOffsetBeforeTheFirstAnchor) {
  const auto a = study::scanUnits(kArticleDoc, strlen(kArticleDoc));
  ASSERT_FALSE(a.anchors.empty());
  // Construct a position ahead of the first anchor by scanning a doc whose
  // first anchor is not at zero.
  const char* doc = "<html><body>lead text<p data-pid=\"6\">alpha</p></body></html>";
  const auto b = study::scanUnits(doc, strlen(doc));
  ASSERT_FALSE(b.anchors.empty());
  ASSERT_GT(b.anchors[0].offset, 0u);
  const study::Unit u = study::resolve(b, 0);
  EXPECT_EQ(u.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(u.offset, 0u);
}

TEST(UnitAnchorsResolve, WillNotMapAVerseBackIntoADifferentBook) {
  auto matthew = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  matthew.book = 40;
  const study::Unit inMark{study::UnitKind::Verse, 41, 1, 8, 0};
  EXPECT_FALSE(study::documentOffsetOf(matthew, inMark).has_value());
}

TEST(UnitAnchorsResolve, DocumentOffsetDocumentsKeepTheRawOffset) {
  const auto a = study::scanUnits(kPlainDoc, strlen(kPlainDoc));
  const study::Unit u = study::resolve(a, 1255);
  EXPECT_EQ(u.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(u.offset, 1255u);
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitAnchorsTest
```

Expected: FAIL — `StudyStore/UnitAnchors.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/UnitAnchors.h`**

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include "StudyStore/Unit.h"

// One document's addressable units, and the single kind that document supports.
//
// Precedence is Verse > Paragraph > DocumentOffset, and it is NOT a fallback
// chain over disjoint publications: measured on the NWT, all 1,189 verse-marked
// documents also carry data-pid. Without a precedence rule the same passage
// would address two ways depending on scan order.
namespace study {

struct UnitAnchor {
  uint32_t offset;  // visible codepoints into the document
  uint16_t major;
  uint16_t minor;
};

struct DocumentUnits {
  UnitKind kind = UnitKind::DocumentOffset;
  // Canonical Bible book 1-66, stamped by the caller from biblebooknav.xhtml's
  // ordering. scanUnits cannot know it -- the book is a property of where the
  // document sits in the spine, not of its markup.
  uint8_t book = 0;
  std::vector<UnitAnchor> anchors;  // ascending by offset; empty for DocumentOffset
};

// Runs the verse scanner first and the paragraph scanner only if it found
// nothing. Two passes over a document cost ~30 ms together and happen once per
// document per index build.
DocumentUnits scanUnits(const char* xhtml, size_t length);

// The unit containing `documentOffset`, with `Unit::offset` relative to that
// unit's start. Falls back to a DocumentOffset unit carrying the raw offset
// when the document has no units, or when the position precedes the first one.
Unit resolve(const DocumentUnits& units, uint32_t documentOffset);

// The document offset a unit starts at, for painting a resolved passage back
// onto the page. Returns nullopt when the unit is not in this document.
std::optional<uint32_t> documentOffsetOf(const DocumentUnits& units, const Unit& unit);

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/UnitAnchors.cpp`**

```cpp
#include "StudyStore/UnitAnchors.h"

#include "Epub/ParagraphAnchors.h"
#include "Epub/VerseAnchors.h"

namespace study {

DocumentUnits scanUnits(const char* xhtml, const size_t length) {
  DocumentUnits out;

  const auto verses = VerseAnchors::scan(xhtml, length);
  if (!verses.empty()) {
    out.kind = UnitKind::Verse;
    out.anchors.reserve(verses.size());
    for (const auto& v : verses) out.anchors.push_back({v.offset, v.chapter, v.verse});
    return out;
  }

  const auto paragraphs = ParagraphAnchors::scan(xhtml, length);
  if (!paragraphs.empty()) {
    out.kind = UnitKind::Paragraph;
    out.anchors.reserve(paragraphs.size());
    for (const auto& p : paragraphs) out.anchors.push_back({p.offset, 0, p.pid});
    return out;
  }

  return out;
}

Unit resolve(const DocumentUnits& units, const uint32_t documentOffset) {
  const UnitAnchor* best = nullptr;
  for (const auto& a : units.anchors) {
    if (a.offset > documentOffset) break;
    best = &a;
  }
  if (!best) return Unit{UnitKind::DocumentOffset, 0, 0, 0, documentOffset};
  return Unit{units.kind, units.book, best->major, best->minor, documentOffset - best->offset};
}

std::optional<uint32_t> documentOffsetOf(const DocumentUnits& units, const Unit& unit) {
  if (unit.kind == UnitKind::DocumentOffset) return unit.offset;
  if (unit.kind != units.kind) return std::nullopt;
  if (unit.kind == UnitKind::Verse && unit.book != units.book) return std::nullopt;
  for (const auto& a : units.anchors) {
    if (a.major == unit.major && a.minor == unit.minor) return a.offset + unit.offset;
  }
  return std::nullopt;
}

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/unit_anchors/CMakeLists.txt`:

```cmake
add_executable(UnitAnchorsTest
  UnitAnchorsTest.cpp
  ${REPO_ROOT}/lib/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/UnitAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(UnitAnchorsTest PRIVATE
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(UnitAnchorsTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(UnitAnchorsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitAnchorsTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(unit_anchors)
```

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitAnchorsTest && ./build/unit_anchors/UnitAnchorsTest
```

Expected: `[  PASSED  ] 7 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/UnitAnchors.h lib/StudyStore/UnitAnchors.cpp test/unit_anchors test/CMakeLists.txt
git commit -m "feat: resolve a document offset to a unit, verse first"
```

---

## Task 4: The pubkey resolution ladder

The spec derives a pubkey from symbol, issue and language. **That derivation has
no source on disk.** The OPF of `w_S_202601` carries
`<dc:identifier id="BookId">urn:uuid:18854DEA-B691-4E92-95D0-3B066C062D83</dc:identifier>`
— a random UUID — and no metadata field anywhere in the file contains the symbol
`w`. The downloader knows the symbol at download time; nothing else does.

Hence a ladder, and a registry the downloader writes.

**Files:**
- Create: `lib/StudyStore/PubKey.h`, `lib/StudyStore/PubKey.cpp`
- Create: `test/pub_key/PubKeyTest.cpp`, `test/pub_key/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/pub_key/PubKeyTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "StudyStore/PubKey.h"

namespace {

TEST(PubKeyLadder, ABibleIsAlwaysTheSameKeyRegardlessOfLanguageOrPath) {
  study::PubKeyInputs es{};
  es.isBible = true;
  es.bookPath = "/books/Traduccion del Nuevo Mundo (nwt-S).epub";

  study::PubKeyInputs en{};
  en.isBible = true;
  en.bookPath = "/books/New World Translation (nwt-E).epub";

  EXPECT_EQ(study::resolvePubKey(es), "bible");
  EXPECT_EQ(study::resolvePubKey(en), "bible")
      << "Salmos 119:145 and Psalm 119:145 are one verse; language is a rendering choice";
}

TEST(PubKeyLadder, ARegisteredPublicationUsesItsSymbolIssueAndLanguage) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/La Atalaya (ed. estudio) 2026-07.epub";
  in.registered = study::RegisteredPub{"w", "202607", "S"};
  EXPECT_EQ(study::resolvePubKey(in), "w-202607-S");
}

TEST(PubKeyLadder, ARegisteredPublicationWithNoIssueOmitsIt) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/Disfruta de la vida (lff-S).epub";
  in.registered = study::RegisteredPub{"lff", "", "S"};
  EXPECT_EQ(study::resolvePubKey(in), "lff-S");
}

TEST(PubKeyLadder, TheBibleFlagOutranksTheRegistry) {
  study::PubKeyInputs in{};
  in.isBible = true;
  in.registered = study::RegisteredPub{"nwt", "", "S"};
  in.bookPath = "/books/nwt.epub";
  EXPECT_EQ(study::resolvePubKey(in), "bible");
}

TEST(PubKeyLadder, AnUnregisteredBookFallsBackToItsFlattenedPath) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/Some Sideloaded Book.epub";
  EXPECT_EQ(study::resolvePubKey(in), "local-books_Some Sideloaded Book");
}

TEST(PubKeyLadder, AFallbackKeyIsAnnouncedAsUnstable) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/Some Sideloaded Book.epub";
  EXPECT_FALSE(study::pubKeyIsStable(study::resolvePubKey(in)))
      << "a local- key is path-derived and dies when the file moves; the UI must be able to say so";

  study::PubKeyInputs bible{};
  bible.isBible = true;
  EXPECT_TRUE(study::pubKeyIsStable(study::resolvePubKey(bible)));
}

TEST(PubKeySanitise, StripsSeparatorsThatWouldEscapeTheStoreDirectory) {
  study::PubKeyInputs in{};
  in.registered = study::RegisteredPub{"../../etc", "202607", "S"};
  const std::string key = study::resolvePubKey(in);
  EXPECT_EQ(key.find('/'), std::string::npos);
  EXPECT_EQ(key.find(".."), std::string::npos);
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target PubKeyTest
```

Expected: FAIL — `StudyStore/PubKey.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/PubKey.h`**

```cpp
#pragma once

#include <optional>
#include <string>

// Publication identity for the study store. Study data is keyed on this rather
// than on a file path, so re-downloading or renaming a publication keeps its
// tags -- unlike the EPUB cache, which hashes the path (lib/Epub/Epub.h:48) and
// loses everything when a book moves.
//
// There is no metadata field on disk that yields a symbol: JW's OPF carries a
// random urn:uuid as dc:identifier. Only the downloader knows the symbol, so it
// records one in /.berean/pubkeys.json and everything else consults that.
namespace study {

struct RegisteredPub {
  std::string symbol;    // "w", "mwb", "lff"
  std::string issue;     // "202607", or empty for a non-periodical
  std::string language;  // "S", "E"
};

struct PubKeyInputs {
  bool isBible = false;  // Epub::getBibleBookNavSpineIndex() >= 0
  std::optional<RegisteredPub> registered;
  std::string bookPath;
};

// The Bible's key excludes language deliberately: the verse address is the
// identity, so marks follow the user between renderings.
inline constexpr const char* BIBLE_PUB_KEY = "bible";

std::string resolvePubKey(const PubKeyInputs& in);

// False for a path-derived "local-" key, whose data does not survive the file
// moving. The tag list uses this to mark such passages rather than pretend.
bool pubKeyIsStable(const std::string& key);

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/PubKey.cpp`**

```cpp
#include "StudyStore/PubKey.h"

#include "PathFlatten.h"

namespace study {
namespace {

constexpr const char* LOCAL_PREFIX = "local-";

// The key becomes a filename under /.berean/passages/, so anything that could
// traverse or split a path is replaced rather than rejected -- rejecting would
// lose the passage, and these components come from a network response.
std::string sanitise(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    out.push_back((c == '/' || c == '\\' || c == '.' || c == ':') ? '_' : c);
  }
  return out;
}

}  // namespace

std::string resolvePubKey(const PubKeyInputs& in) {
  if (in.isBible) return BIBLE_PUB_KEY;

  if (in.registered && !in.registered->symbol.empty()) {
    std::string key = sanitise(in.registered->symbol);
    if (!in.registered->issue.empty()) key += "-" + sanitise(in.registered->issue);
    if (!in.registered->language.empty()) key += "-" + sanitise(in.registered->language);
    return key;
  }

  return LOCAL_PREFIX + pathflatten::toCacheName(in.bookPath);
}

bool pubKeyIsStable(const std::string& key) { return key.rfind(LOCAL_PREFIX, 0) != 0; }

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/pub_key/CMakeLists.txt`:

```cmake
add_executable(PubKeyTest
  PubKeyTest.cpp
  ${REPO_ROOT}/lib/StudyStore/PubKey.cpp
  ${REPO_ROOT}/src/util/PathFlatten.cpp
)

target_include_directories(PubKeyTest PRIVATE
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/src/util
)

target_link_libraries(PubKeyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PubKeyTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(pub_key)
```

> Confirm `PathFlatten.cpp`'s path before building — `test/path_flatten/CMakeLists.txt`
> already references it and is the authority.

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target PubKeyTest && ./build/pub_key/PubKeyTest
```

Expected: `[  PASSED  ] 7 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/PubKey.h lib/StudyStore/PubKey.cpp test/pub_key test/CMakeLists.txt
git commit -m "feat: resolve publication identity for the study store"
```

---

## Task 5: The global tag palette

Tag ids are allocated once and never reused; deleting retires an id. `nextTagId`
is persisted explicitly and retired ids are kept as tombstones, because the
obvious recovery from a corrupt `tags.json` — rebuild from the passages files —
recovers used ids and loses retired ones. The next new tag would then take an id
some passage still carries and display as the wrong tag.

The user's real palette is 48 names, two of them (`igualdad`,
`transformación`) currently applied to nothing. **Zero-usage tags are kept.**
They are vocabulary the user chose; silently dropping them is a loss they would
discover months later.

**Files:**
- Create: `lib/StudyStore/TagPalette.h`, `lib/StudyStore/TagPalette.cpp`
- Create: `test/tag_palette/TagPaletteTest.cpp`, `test/tag_palette/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/tag_palette/TagPaletteTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include "StudyStore/TagPalette.h"

namespace {

TEST(TagPaletteAdd, AllocatesAscendingIdsAndReturnsTheExistingIdForARepeat) {
  study::TagPalette p;
  const auto a = p.add("oracion");
  const auto b = p.add("perdon");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_LT(*a, *b);
  EXPECT_EQ(p.add("oracion"), a);
  EXPECT_EQ(p.activeCount(), 2u);
}

// The user's only accented tag name is "transformación". Matching must not fold
// accents: "transformacion" and "transformación" are different words, and
// merging them silently would rewrite a name the user chose.
TEST(TagPaletteAdd, DoesNotFoldAccents) {
  study::TagPalette p;
  const auto a = p.add("transformación");
  const auto b = p.add("transformacion");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(a, b);
}

TEST(TagPaletteAdd, MatchesExactlyIncludingCase) {
  study::TagPalette p;
  EXPECT_NE(p.add("Amor"), p.add("amor"));
}

TEST(TagPaletteAdd, RejectsAnEmptyName) { EXPECT_FALSE(study::TagPalette{}.add("").has_value()); }

TEST(TagPaletteRetire, KeepsTheIdOutOfCirculationForever) {
  study::TagPalette p;
  const auto a = p.add("odio");
  ASSERT_TRUE(a.has_value());
  p.retire(*a);
  EXPECT_EQ(p.activeCount(), 0u);
  EXPECT_FALSE(p.isActive(*a));
  const auto b = p.add("odio");
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(b, a) << "reusing a retired id rebinds every passage that still carries it";
}

TEST(TagPaletteRetire, StillResolvesARetiredNameForDisplay) {
  study::TagPalette p;
  const auto a = p.add("odio");
  p.retire(*a);
  EXPECT_EQ(p.name(*a), "odio") << "a passage still carrying it must not render as a blank chip";
}

TEST(TagPaletteRoundTrip, PreservesIdsNamesTombstonesAndNextId) {
  study::TagPalette p;
  const auto keep = p.add("fe");
  const auto gone = p.add("temporal");
  p.retire(*gone);
  const auto after = p.add("esperanza");

  JsonDocument doc;
  p.toJson(doc);

  study::TagPalette q;
  ASSERT_TRUE(q.fromJson(doc.as<JsonVariantConst>()));
  EXPECT_EQ(q.name(*keep), "fe");
  EXPECT_EQ(q.name(*gone), "temporal");
  EXPECT_FALSE(q.isActive(*gone));
  EXPECT_EQ(q.name(*after), "esperanza");
  EXPECT_EQ(q.add("nueva"), study::TagId{*after + 1}) << "nextTagId must survive the round trip";
}

TEST(TagPaletteRoundTrip, RejectsAFutureFormatVersion) {
  JsonDocument doc;
  doc["v"] = study::TagPalette::FORMAT_VERSION + 1;
  study::TagPalette q;
  EXPECT_FALSE(q.fromJson(doc.as<JsonVariantConst>()))
      << "reinterpreting a newer file is how an OTA rollback destroys data";
}

TEST(TagPaletteRoundTrip, RecoversNextIdFromTheHighestSeenWhenTheFieldIsMissing) {
  JsonDocument doc;
  doc["v"] = study::TagPalette::FORMAT_VERSION;
  const auto tags = doc["t"].to<JsonArray>();
  const auto row = tags.add<JsonObject>();
  row["i"] = 40;
  row["n"] = "ley";

  study::TagPalette q;
  ASSERT_TRUE(q.fromJson(doc.as<JsonVariantConst>()));
  const auto next = q.add("nueva");
  ASSERT_TRUE(next.has_value());
  EXPECT_GT(*next, 40) << "a hand-edited file must not be able to hand out a live id";
}

TEST(TagPaletteBudget, RefusesBeyondTheTagCeiling) {
  study::TagPalette p;
  for (size_t i = 0; i < study::TagPalette::MAX_ACTIVE_TAGS; ++i) {
    ASSERT_TRUE(p.add("t" + std::to_string(i)).has_value()) << "at i=" << i;
  }
  EXPECT_FALSE(p.add("one too many").has_value());
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target TagPaletteTest
```

Expected: FAIL — `StudyStore/TagPalette.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/TagPalette.h`**

```cpp
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The device's global tag vocabulary. Unlike HighlightDoc's per-book palette,
// an id here means the same tag in every publication, so "show me everything
// tagged misericordia" is answerable.
//
// Ids are allocated once and never reused. Deleting retires an id and leaves a
// tombstone, so a passage that still references it renders its real name rather
// than a blank chip or -- far worse -- some later tag's name.
namespace study {

using TagId = uint16_t;

class TagPalette {
 public:
  static constexpr int FORMAT_VERSION = 1;
  // The user's real palette is 48. This bounds growth; PassageDoc's byte budget
  // is what actually guarantees the file stays readable.
  static constexpr size_t MAX_ACTIVE_TAGS = 200;
  static constexpr size_t MAX_TAG_NAME_BYTES = 24;

  // Returns the existing id when the name already exists, a fresh id when it
  // does not, and nullopt when the name is empty, too long, or the palette is
  // full. Matching is exact -- byte equality, no case folding and no accent
  // folding, because "transformación" and "transformacion" are different words
  // and the user's palette contains one of them.
  std::optional<TagId> add(const std::string& name);

  // Retires `id`. The tag stops appearing in pickers but keeps resolving for
  // display. A no-op for an unknown id.
  void retire(TagId id);

  bool isActive(TagId id) const;
  // The name for any id ever allocated, active or retired; empty if unknown.
  const std::string& name(TagId id) const;
  size_t activeCount() const;

  // Active tags in allocation order, for a picker.
  std::vector<TagId> activeIds() const;

  void toJson(JsonDocument& doc) const;

  // Parses and validates. Rejects a future format version. Recovers nextTagId
  // as max(seen) + 1 when the field is absent or too low, so a hand-edited file
  // cannot hand out an id a passage already carries.
  bool fromJson(JsonVariantConst doc);

 private:
  struct Entry {
    TagId id = 0;
    std::string name;
    bool active = true;
  };

  std::vector<Entry> entries_;
  TagId nextId_ = 1;  // 0 is reserved; see the spec's open item on untagged passages
};

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/TagPalette.cpp`**

```cpp
#include "StudyStore/TagPalette.h"

namespace study {
namespace {
const std::string kEmpty;
}  // namespace

std::optional<TagId> TagPalette::add(const std::string& name) {
  if (name.empty() || name.size() > MAX_TAG_NAME_BYTES) return std::nullopt;

  for (auto& e : entries_) {
    if (e.name != name) continue;
    e.active = true;  // re-adding a retired name revives it under its own id
    return e.id;
  }

  if (activeCount() >= MAX_ACTIVE_TAGS) return std::nullopt;
  if (nextId_ == UINT16_MAX) return std::nullopt;

  const TagId id = nextId_++;
  entries_.push_back({id, name, true});
  return id;
}

void TagPalette::retire(const TagId id) {
  for (auto& e : entries_) {
    if (e.id == id) e.active = false;
  }
}

bool TagPalette::isActive(const TagId id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return e.active;
  }
  return false;
}

const std::string& TagPalette::name(const TagId id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return e.name;
  }
  return kEmpty;
}

size_t TagPalette::activeCount() const {
  size_t n = 0;
  for (const auto& e : entries_) {
    if (e.active) ++n;
  }
  return n;
}

std::vector<TagId> TagPalette::activeIds() const {
  std::vector<TagId> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_) {
    if (e.active) out.push_back(e.id);
  }
  return out;
}

void TagPalette::toJson(JsonDocument& doc) const {
  doc["v"] = FORMAT_VERSION;
  doc["n"] = nextId_;
  const auto tags = doc["t"].to<JsonArray>();
  for (const auto& e : entries_) {
    const auto row = tags.add<JsonObject>();
    row["i"] = e.id;
    row["n"] = e.name;
    if (!e.active) row["r"] = true;
  }
}

bool TagPalette::fromJson(const JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;
  const int version = doc["v"] | 0;
  if (version <= 0 || version > FORMAT_VERSION) return false;

  entries_.clear();
  TagId highest = 0;

  for (const JsonVariantConst v : doc["t"].as<JsonArrayConst>()) {
    const uint32_t id = v["i"] | 0u;
    const char* name = v["n"] | "";
    if (id == 0 || id > UINT16_MAX || name[0] == '\0') continue;

    std::string text(name);
    if (text.size() > MAX_TAG_NAME_BYTES) text.resize(MAX_TAG_NAME_BYTES);

    const TagId tagId = static_cast<TagId>(id);
    bool duplicate = false;
    for (const auto& e : entries_) duplicate = duplicate || e.id == tagId;
    if (duplicate) continue;

    entries_.push_back({tagId, std::move(text), !(v["r"] | false)});
    highest = std::max(highest, tagId);
  }

  const uint32_t stored = doc["n"] | 0u;
  nextId_ = static_cast<TagId>(std::max<uint32_t>(stored, highest + 1u));
  return true;
}

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/tag_palette/CMakeLists.txt`:

```cmake
add_executable(TagPaletteTest
  TagPaletteTest.cpp
  ${REPO_ROOT}/lib/StudyStore/TagPalette.cpp
)

target_include_directories(TagPaletteTest PRIVATE ${REPO_ROOT}/lib)

target_link_libraries(TagPaletteTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(TagPaletteTest)
```

> `test/highlight_doc/CMakeLists.txt` is the authority for how ArduinoJson is
> linked in this suite — copy its target name if `ArduinoJson` does not resolve.

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(tag_palette)
```

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target TagPaletteTest && ./build/tag_palette/TagPaletteTest
```

Expected: `[  PASSED  ] 10 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/TagPalette.h lib/StudyStore/TagPalette.cpp test/tag_palette test/CMakeLists.txt
git commit -m "feat: add the global tag palette with retired-id tombstones"
```

---

## Task 6: The unit fingerprint

What tells a reopened passage whether the text it was attached to is still the
text that is there. Length plus CRC32 over the unit's **visible** codepoints —
the same codepoints `VisibleOffsetCounter` counts, so markup changes that do not
change what the reader sees do not invalidate a mark.

**Files:**
- Create: `lib/StudyStore/UnitFingerprint.h`, `lib/StudyStore/UnitFingerprint.cpp`
- Create: `test/unit_fingerprint/UnitFingerprintTest.cpp`, `test/unit_fingerprint/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/unit_fingerprint/UnitFingerprintTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "StudyStore/UnitFingerprint.h"

namespace {

TEST(UnitFingerprint, IsStableForIdenticalText) {
  EXPECT_EQ(study::fingerprintOf("en el principio"), study::fingerprintOf("en el principio"));
}

TEST(UnitFingerprint, DiffersWhenAWordChanges) {
  EXPECT_NE(study::fingerprintOf("en el principio"), study::fingerprintOf("en el prinicipio"));
}

TEST(UnitFingerprint, DiffersWhenLengthChangesButContentRhymes) {
  EXPECT_NE(study::fingerprintOf("amor"), study::fingerprintOf("amores"));
}

TEST(UnitFingerprint, CountsCodepointsNotBytes) {
  // "transformación" is 14 codepoints and 15 bytes. Storing the byte length
  // would make the fingerprint disagree with every offset in the record.
  EXPECT_EQ(study::fingerprintOf("transformación").length, 14u);
}

TEST(UnitFingerprint, RoundTripsThroughItsCompactForm) {
  const study::Fingerprint f = study::fingerprintOf("en el principio");
  const auto parsed = study::fingerprintFromCompact(study::fingerprintToCompact(f));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, f);
}

TEST(UnitFingerprint, EmptyTextIsDistinguishableFromAbsent) {
  EXPECT_EQ(study::fingerprintOf("").length, 0u);
  EXPECT_FALSE(study::fingerprintFromCompact("").has_value());
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitFingerprintTest
```

Expected: FAIL — `StudyStore/UnitFingerprint.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/UnitFingerprint.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Whether the text a passage was attached to is still the text that is there.
//
// Computed over VISIBLE codepoints -- what the reader sees -- so a markup-only
// reissue does not orphan a mark, while a corrected word does. Length is in
// codepoints, matching the unit the offsets are in; a byte length would disagree
// with every offset in the record for any accented text.
namespace study {

struct Fingerprint {
  uint32_t length = 0;  // visible codepoints
  uint32_t crc = 0;

  bool operator==(const Fingerprint&) const = default;
};

Fingerprint fingerprintOf(std::string_view visibleText);

// "14:a1b2c3d4"
std::string fingerprintToCompact(const Fingerprint& f);
std::optional<Fingerprint> fingerprintFromCompact(const std::string& s);

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/UnitFingerprint.cpp`**

```cpp
#include "StudyStore/UnitFingerprint.h"

#include <cstdio>

namespace study {
namespace {

// CRC-32/ISO-HDLC, computed nibble-wise so the table is 64 bytes of flash
// rather than 1 KB. This runs once per unit on a page turn, not per character
// of a stream, so the halved throughput is invisible.
constexpr uint32_t kNibbleTable[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                       0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                       0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

uint32_t crc32(const std::string_view data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const unsigned char byte : data) {
    crc ^= byte;
    crc = (crc >> 4) ^ kNibbleTable[crc & 0x0Fu];
    crc = (crc >> 4) ^ kNibbleTable[crc & 0x0Fu];
  }
  return ~crc;
}

uint32_t countCodepoints(const std::string_view s) {
  uint32_t n = 0;
  for (const unsigned char c : s) {
    if ((c & 0xC0u) != 0x80u) ++n;  // every byte that is not a continuation starts one
  }
  return n;
}

}  // namespace

Fingerprint fingerprintOf(const std::string_view visibleText) {
  return Fingerprint{countCodepoints(visibleText), crc32(visibleText)};
}

std::string fingerprintToCompact(const Fingerprint& f) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u:%08x", f.length, f.crc);
  return buf;
}

std::optional<Fingerprint> fingerprintFromCompact(const std::string& s) {
  unsigned length = 0;
  unsigned crc = 0;
  char tail = '\0';
  if (sscanf(s.c_str(), "%u:%x%c", &length, &crc, &tail) != 2) return std::nullopt;
  return Fingerprint{length, crc};
}

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/unit_fingerprint/CMakeLists.txt`:

```cmake
add_executable(UnitFingerprintTest
  UnitFingerprintTest.cpp
  ${REPO_ROOT}/lib/StudyStore/UnitFingerprint.cpp
)

target_include_directories(UnitFingerprintTest PRIVATE ${REPO_ROOT}/lib)

target_link_libraries(UnitFingerprintTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitFingerprintTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(unit_fingerprint)
```

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitFingerprintTest && ./build/unit_fingerprint/UnitFingerprintTest
```

Expected: `[  PASSED  ] 6 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/UnitFingerprint.h lib/StudyStore/UnitFingerprint.cpp test/unit_fingerprint test/CMakeLists.txt
git commit -m "feat: fingerprint a unit's visible text"
```

---

## Task 7: The passage record and its per-publication document

### Why the Bible's passages are sharded by book

`bible` is a single pubkey, so every Bible passage the user ever makes would
land in one file. At ~300 serialised bytes per passage and a 45,000-byte budget
that ceiling is ~150 passages — and the user already has 63 after one book of
study. A Bible student hits that inside a year, and the failure mode when they
do is a refused save, which is recoverable but alarming.

So a passages file is addressed by `(pubkey, segment)`. For the Bible the
segment is the canonical book number, which `Unit::book` now carries; for
everything else it is 0 and the layout is unchanged. This also makes reading
Psalms load only Psalms' marks instead of the whole Bible's.

**Files:**
- Create: `lib/StudyStore/TaggedPassage.h`, `lib/StudyStore/PassageDoc.h`, `lib/StudyStore/PassageDoc.cpp`
- Create: `test/passage_doc/PassageDocTest.cpp`, `test/passage_doc/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write `lib/StudyStore/TaggedPassage.h`** (no test of its own — it is a
      plain aggregate, exercised by `PassageDocTest`)

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"
#include "StudyStore/UnitFingerprint.h"

namespace study {

// One tagged span. `start` and `end` are Units, so the passage survives the
// publication being re-downloaded; `document` and `documentSpine` are
// resolution HINTS for finding it again quickly, never the identity -- for the
// Bible they are language-specific and the Unit is not.
struct TaggedPassage {
  Unit start;
  Unit end;
  Fingerprint fingerprint;      // over the start unit's visible codepoints
  std::string document;         // filename inside the archive, a hint
  uint16_t documentSpine = 0;   // spine index, a weaker hint
  std::string snippet;          // bounded passage text, for the tag list
  std::string reference;        // "Salmos 119:145", display + migration cross-check
  std::vector<TagId> tags;      // global ids
  bool pendingUpgrade = false;  // migrated without the EPUB; upgrade on next open
};

}  // namespace study
```

- [ ] **Step 2: Write the failing test**

`test/passage_doc/PassageDocTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include "StudyStore/PassageDoc.h"

namespace {

study::TaggedPassage samplePassage() {
  study::TaggedPassage p;
  p.start = study::Unit{study::UnitKind::Verse, 19, 119, 145, 0};
  p.end = study::Unit{study::UnitKind::Verse, 19, 119, 145, 108};
  p.fingerprint = study::Fingerprint{114, 0xa1b2c3d4};
  p.document = "1001061130-split10.xhtml";
  p.documentSpine = 198;
  p.snippet = "Te he llamado con todo el corazon";
  p.reference = "Salmos 119:145";
  p.tags = {3, 17};
  return p;
}

TEST(PassageDocRoundTrip, PreservesEveryFieldOfAPassage) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));

  JsonDocument json;
  doc.toJson(json);

  study::PassageDoc back;
  ASSERT_TRUE(back.fromJson(json.as<JsonVariantConst>()));
  ASSERT_EQ(back.passages().size(), 1u);
  const auto& p = back.passages()[0];
  const auto& o = samplePassage();
  EXPECT_EQ(p.start, o.start);
  EXPECT_EQ(p.end, o.end);
  EXPECT_EQ(p.fingerprint, o.fingerprint);
  EXPECT_EQ(p.document, o.document);
  EXPECT_EQ(p.documentSpine, o.documentSpine);
  EXPECT_EQ(p.snippet, o.snippet);
  EXPECT_EQ(p.reference, o.reference);
  EXPECT_EQ(p.tags, o.tags);
}

TEST(PassageDocRoundTrip, RejectsAFutureFormatVersion) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION + 1;
  study::PassageDoc doc;
  EXPECT_FALSE(doc.fromJson(json.as<JsonVariantConst>()));
}

TEST(PassageDocRoundTrip, DropsAPassageWhoseStartUnitIsUnparseable) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION;
  const auto rows = json["p"].to<JsonArray>();
  const auto row = rows.add<JsonObject>();
  row["u"] = "nonsense";
  row["e"] = "v:19:119:145:0";

  study::PassageDoc doc;
  ASSERT_TRUE(doc.fromJson(json.as<JsonVariantConst>()));
  EXPECT_TRUE(doc.passages().empty()) << "an unaddressable passage cannot be painted or listed";
}

TEST(PassageDocValidation, TruncatesAnOverlongSnippetAndReference) {
  study::TaggedPassage p = samplePassage();
  p.snippet = std::string(500, 'x');
  p.reference = std::string(200, 'y');
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  EXPECT_LE(doc.passages()[0].snippet.size(), study::PassageDoc::MAX_SNIPPET_BYTES);
  EXPECT_LE(doc.passages()[0].reference.size(), study::PassageDoc::MAX_REFERENCE_BYTES);
}

TEST(PassageDocValidation, DedupesAndCapsTags) {
  study::TaggedPassage p = samplePassage();
  p.tags = {5, 5, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12};
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  const auto& tags = doc.passages()[0].tags;
  EXPECT_LE(tags.size(), study::PassageDoc::MAX_TAGS_PER_PASSAGE);
  EXPECT_EQ(std::count(tags.begin(), tags.end(), 5), 1);
}

TEST(PassageDocValidation, RefusesAPassageWithNoTags) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  study::PassageDoc doc;
  EXPECT_FALSE(doc.add(p)) << "a highlight exists only to carry tags; all 63 of the user's do";
}

TEST(PassageDocRemove, DropsTheTagEverywhereAndThenThePassage) {
  study::TaggedPassage p = samplePassage();  // tags {3, 17}
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));

  doc.removeTagEverywhere(3);
  ASSERT_EQ(doc.passages().size(), 1u);
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{17}));

  doc.removeTagEverywhere(17);
  EXPECT_TRUE(doc.passages().empty()) << "untagging to zero removes the passage; there is nothing left to carry";
}

TEST(PassageDocBudget, MeasuresSerialisedBytesAndRefusesOverBudget) {
  study::PassageDoc doc;
  size_t added = 0;
  while (doc.add(samplePassage())) ++added;
  ASSERT_GT(added, 0u);

  JsonDocument json;
  doc.toJson(json);
  EXPECT_LE(measureJson(json), study::PassageDoc::SAVE_BYTE_BUDGET)
      << "the document must never be able to grow past what SDCardManager::readFile can read back";
}

}  // namespace
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target PassageDocTest
```

Expected: FAIL — `StudyStore/PassageDoc.h: No such file or directory`.

- [ ] **Step 4: Write `lib/StudyStore/PassageDoc.h`**

```cpp
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "StudyStore/TaggedPassage.h"

// One (pubkey, segment)'s tagged passages, with all format rules and no storage
// access. The storage shell is src/study/PassageFile.
//
// Bytes are the safety invariant, not passage counts: SDCardManager::readFile
// silently truncates at 50,000 bytes, and a document that saves larger than
// that reads back unparseable, initialises empty, and is overwritten with {} on
// the next save. add() enforces the budget as it goes.
namespace study {

class PassageDoc {
 public:
  static constexpr int FORMAT_VERSION = 1;
  static constexpr size_t SAVE_BYTE_BUDGET = 45000;  // persist::DEFAULT_SAVE_BUDGET
  static constexpr size_t MAX_SNIPPET_BYTES = 120;
  static constexpr size_t MAX_REFERENCE_BYTES = 48;
  static constexpr size_t MAX_TAGS_PER_PASSAGE = 8;

  const std::vector<TaggedPassage>& passages() const { return passages_; }

  // Normalises (truncating snippet and reference, deduping and capping tags)
  // and appends. Returns false when the passage carries no tags, when its start
  // unit is unusable, or when adding it would exceed SAVE_BYTE_BUDGET.
  bool add(TaggedPassage passage);

  bool remove(size_t index);
  bool setTags(size_t index, std::vector<TagId> tags);

  // Drops `id` from every passage, and drops any passage left with none.
  void removeTagEverywhere(TagId id);

  // Passages whose start unit is in `document`, for the render pass.
  std::vector<const TaggedPassage*> findByDocument(const std::string& document) const;

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Serialised size of the current contents, for a pre-write budget check.
  size_t measureBytes() const;

 private:
  std::vector<TaggedPassage> passages_;
};

}  // namespace study
```

- [ ] **Step 5: Write `lib/StudyStore/PassageDoc.cpp`**

The field names are deliberately one or two characters — this file is measured
against a hard byte budget, and `"documentSpine"` costs 14 bytes per passage for
nothing.

| Key | Field |
|---|---|
| `v` | format version |
| `p` | passage array |
| `u` | start unit, compact |
| `e` | end unit, compact |
| `f` | fingerprint, compact |
| `d` | document filename |
| `s` | document spine index |
| `x` | snippet |
| `r` | reference |
| `t` | tag ids |
| `g` | pendingUpgrade flag, omitted when false |

```cpp
#include "StudyStore/PassageDoc.h"

#include <algorithm>

namespace study {

bool PassageDoc::add(TaggedPassage passage) {
  if (passage.tags.empty()) return false;
  if (passage.snippet.size() > MAX_SNIPPET_BYTES) passage.snippet.resize(MAX_SNIPPET_BYTES);
  if (passage.reference.size() > MAX_REFERENCE_BYTES) passage.reference.resize(MAX_REFERENCE_BYTES);

  std::vector<TagId> tags;
  tags.reserve(std::min(passage.tags.size(), MAX_TAGS_PER_PASSAGE));
  for (const TagId id : passage.tags) {
    if (tags.size() >= MAX_TAGS_PER_PASSAGE) break;
    if (std::find(tags.begin(), tags.end(), id) == tags.end()) tags.push_back(id);
  }
  passage.tags = std::move(tags);

  passages_.push_back(std::move(passage));
  if (measureBytes() > SAVE_BYTE_BUDGET) {
    passages_.pop_back();
    return false;
  }
  return true;
}

bool PassageDoc::remove(const size_t index) {
  if (index >= passages_.size()) return false;
  passages_.erase(passages_.begin() + static_cast<long>(index));
  return true;
}

bool PassageDoc::setTags(const size_t index, std::vector<TagId> tags) {
  if (index >= passages_.size()) return false;
  TaggedPassage updated = passages_[index];
  updated.tags = std::move(tags);
  passages_.erase(passages_.begin() + static_cast<long>(index));
  if (add(std::move(updated))) return true;
  return false;
}

void PassageDoc::removeTagEverywhere(const TagId id) {
  for (auto& p : passages_) {
    p.tags.erase(std::remove(p.tags.begin(), p.tags.end(), id), p.tags.end());
  }
  passages_.erase(std::remove_if(passages_.begin(), passages_.end(),
                                 [](const TaggedPassage& p) { return p.tags.empty(); }),
                  passages_.end());
}

std::vector<const TaggedPassage*> PassageDoc::findByDocument(const std::string& document) const {
  std::vector<const TaggedPassage*> out;
  for (const auto& p : passages_) {
    if (p.document == document) out.push_back(&p);
  }
  return out;
}

void PassageDoc::toJson(JsonDocument& doc) const {
  doc["v"] = FORMAT_VERSION;
  const auto rows = doc["p"].to<JsonArray>();
  for (const auto& p : passages_) {
    const auto row = rows.add<JsonObject>();
    row["u"] = unitToCompact(p.start);
    row["e"] = unitToCompact(p.end);
    row["f"] = fingerprintToCompact(p.fingerprint);
    row["d"] = p.document;
    row["s"] = p.documentSpine;
    row["x"] = p.snippet;
    row["r"] = p.reference;
    if (p.pendingUpgrade) row["g"] = true;
    const auto tags = row["t"].to<JsonArray>();
    for (const TagId id : p.tags) tags.add(id);
  }
}

bool PassageDoc::fromJson(const JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;
  const int version = doc["v"] | 0;
  if (version <= 0 || version > FORMAT_VERSION) return false;

  passages_.clear();
  for (const JsonVariantConst v : doc["p"].as<JsonArrayConst>()) {
    const auto start = unitFromCompact(v["u"] | "");
    if (!start) continue;  // unaddressable: cannot be painted or listed

    TaggedPassage p;
    p.start = *start;
    p.end = unitFromCompact(v["e"] | "").value_or(*start);
    p.fingerprint = fingerprintFromCompact(v["f"] | "").value_or(Fingerprint{});
    p.document = v["d"] | "";
    p.documentSpine = static_cast<uint16_t>(v["s"] | 0);
    p.snippet = v["x"] | "";
    p.reference = v["r"] | "";
    p.pendingUpgrade = v["g"] | false;
    for (const JsonVariantConst t : v["t"].as<JsonArrayConst>()) {
      const uint32_t id = t | 0u;
      if (id > 0 && id <= UINT16_MAX) p.tags.push_back(static_cast<TagId>(id));
    }
    add(std::move(p));  // re-validates and enforces the budget on the way in
  }
  return true;
}

size_t PassageDoc::measureBytes() const {
  JsonDocument doc;
  toJson(doc);
  return measureJson(doc);
}

}  // namespace study
```

> **Note for the implementer:** `measureBytes()` serialises the whole document,
> so `add()` is O(n²) across a bulk load. At the real ceiling (~150 passages per
> file) that is a few milliseconds and it runs on load and on tag edits, never on
> a page turn. Do not optimise it into an incremental estimate — an estimate that
> drifts under the true size is exactly the silent-truncation bug this guards.

- [ ] **Step 6: Register the test**

`test/passage_doc/CMakeLists.txt`:

```cmake
add_executable(PassageDocTest
  PassageDocTest.cpp
  ${REPO_ROOT}/lib/StudyStore/PassageDoc.cpp
  ${REPO_ROOT}/lib/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/TagPalette.cpp
)

target_include_directories(PassageDocTest PRIVATE ${REPO_ROOT}/lib)

target_link_libraries(PassageDocTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(PassageDocTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(passage_doc)
```

- [ ] **Step 7: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target PassageDocTest && ./build/passage_doc/PassageDocTest
```

Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 8: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/TaggedPassage.h lib/StudyStore/PassageDoc.h lib/StudyStore/PassageDoc.cpp test/passage_doc test/CMakeLists.txt
git commit -m "feat: add the tagged passage record and its budgeted document"
```

---

## Task 8: The migration planner

The heart of the phase, and the reason it is a pure function: it decides what
every one of the user's 63 passages becomes, and it must be testable without a
device, an SD card or an EPUB.

**The `ref` cross-check.** Every old highlight carries a human reference the
device wrote at the time — `"Apocalipsis 1:8"`. The spec calls that field
display-only and forbids using it to *locate* a passage, which is right. But it
is an independent witness for *verifying* one: if `(spineIndex, offset)` resolves
to a verse that disagrees with the reference already stored beside it, the
migration has gone wrong in a way that is detectable rather than silent. The
offline dry run (Task 14) confirmed 63/63 agreement before this plan was
written; the planner encodes that check so it stays true.

**Files:**
- Create: `lib/StudyStore/MigrationPlanner.h`, `lib/StudyStore/MigrationPlanner.cpp`
- Create: `test/migration_planner/MigrationPlannerTest.cpp`, `test/migration_planner/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/migration_planner/MigrationPlannerTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "StudyStore/MigrationPlanner.h"

namespace {

study::DocumentUnits psalm119() {
  study::DocumentUnits u;
  u.kind = study::UnitKind::Verse;
  u.book = 19;
  u.anchors = {{0, 119, 144}, {100, 119, 145}, {220, 119, 146}};
  return u;
}

study::LegacyHighlight legacy(const uint16_t spine, const uint32_t start, const uint32_t end,
                              const char* ref, std::vector<std::string> tags) {
  study::LegacyHighlight h;
  h.spineIndex = spine;
  h.start = start;
  h.end = end;
  h.reference = ref;
  h.tagNames = std::move(tags);
  h.snippet = "Te he llamado con todo el corazon";
  return h;
}

study::MigrationInputs inputs() {
  study::MigrationInputs in;
  in.pubKey = "bible";
  in.document = "1001061130-split10.xhtml";
  in.units = psalm119();
  in.unitText = [](const study::Unit&) { return std::string("Te he llamado con todo el corazon"); };
  return in;
}

TEST(MigrationPlanner, ResolvesALegacyOffsetToItsVerse) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"oracion"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->start.kind, study::UnitKind::Verse);
  EXPECT_EQ(out.passage->start.book, 19);
  EXPECT_EQ(out.passage->start.major, 119);
  EXPECT_EQ(out.passage->start.minor, 145);
  EXPECT_EQ(out.passage->start.offset, 8u) << "offset is relative to the verse, not the document";
  EXPECT_EQ(out.outcome, study::MigrationOutcome::Resolved);
}

TEST(MigrationPlanner, AgreesWithTheStoredReferenceAndSaysSo) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"oracion"}));
  EXPECT_TRUE(out.referenceAgrees);
}

TEST(MigrationPlanner, FlagsADisagreementWithTheStoredReferenceWithoutDroppingThePassage) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:200", {"oracion"}));
  ASSERT_TRUE(out.passage.has_value()) << "a disagreement is reported, never silently discarded";
  EXPECT_FALSE(out.referenceAgrees);
  EXPECT_EQ(out.outcome, study::MigrationOutcome::ResolvedReferenceMismatch);
}

TEST(MigrationPlanner, IgnoresTheBookNameWhenComparingReferences) {
  // The book name is language-specific and sometimes abbreviated; only the
  // chapter:verse tail is comparable.
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Sal. 119:145", {"fe"}));
  EXPECT_TRUE(out.referenceAgrees);
}

TEST(MigrationPlanner, DegradesToDocumentOffsetWhenTheDocumentHasNoUnits) {
  study::MigrationInputs in = inputs();
  in.units = study::DocumentUnits{};
  const auto out = study::planMigration(in, legacy(198, 108, 180, "", {"fe"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->start.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(out.passage->start.offset, 108u);
  EXPECT_EQ(out.outcome, study::MigrationOutcome::ResolvedDocumentOffset);
}

TEST(MigrationPlanner, MarksAPassagePendingWhenTheSourceBookWasUnavailable) {
  study::MigrationInputs in = inputs();
  in.sourceAvailable = false;
  const auto out = study::planMigration(in, legacy(198, 108, 180, "Salmos 119:145", {"fe"}));
  ASSERT_TRUE(out.passage.has_value()) << "an absent EPUB must never cost the user a passage";
  EXPECT_TRUE(out.passage->pendingUpgrade);
  EXPECT_EQ(out.passage->start.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(out.outcome, study::MigrationOutcome::PendingUpgrade);
}

TEST(MigrationPlanner, CarriesTheReferenceAndSnippetThrough) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"oracion"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->reference, "Salmos 119:145");
  EXPECT_FALSE(out.passage->snippet.empty());
  EXPECT_EQ(out.passage->documentSpine, 198);
  EXPECT_EQ(out.passage->document, "1001061130-split10.xhtml");
}

TEST(MigrationPlanner, DropsAPassageThatCarriedNoTags) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {}));
  EXPECT_FALSE(out.passage.has_value());
  EXPECT_EQ(out.outcome, study::MigrationOutcome::DroppedNoTags);
}

TEST(MigrationPlanner, FingerprintsTheStartUnitsText) {
  const auto out = study::planMigration(inputs(), legacy(198, 108, 180, "Salmos 119:145", {"fe"}));
  ASSERT_TRUE(out.passage.has_value());
  EXPECT_EQ(out.passage->fingerprint, study::fingerprintOf("Te he llamado con todo el corazon"));
}

TEST(MigrationPlanner, AllocatesTagIdsThroughThePaletteSoNamesDedupeAcrossBooks) {
  study::TagPalette palette;
  study::MigrationInputs in = inputs();
  in.palette = &palette;

  const auto a = study::planMigration(in, legacy(198, 108, 180, "Salmos 119:145", {"fe", "amor"}));
  const auto b = study::planMigration(in, legacy(199, 10, 40, "Salmos 120:1", {"amor"}));
  ASSERT_TRUE(a.passage.has_value());
  ASSERT_TRUE(b.passage.has_value());
  EXPECT_EQ(a.passage->tags[1], b.passage->tags[0]) << "one name, one global id";
  EXPECT_EQ(palette.activeCount(), 2u);
}

TEST(MigrationPlanner, KeepsATagNameThatNoPassageUses) {
  // The user's palette has two: `igualdad` and `transformacion`. Vocabulary the
  // user chose is not the migration's to discard.
  study::TagPalette palette;
  study::adoptTagNames(palette, {"igualdad", "transformación"});
  EXPECT_EQ(palette.activeCount(), 2u);
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target MigrationPlannerTest
```

Expected: FAIL — `StudyStore/MigrationPlanner.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/MigrationPlanner.h`**

```cpp
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "StudyStore/TaggedPassage.h"
#include "StudyStore/UnitAnchors.h"

// Decides what one legacy highlight becomes. Pure: no storage, no EPUB, no
// Arduino, so the whole migration is testable on the host.
namespace study {

// A HighlightEntry as it exists in /.crosspoint/highlights/, flattened so this
// header does not depend on the old model.
struct LegacyHighlight {
  uint16_t spineIndex = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  std::string snippet;    // the old `text`
  std::string reference;  // the old `ref`
  std::vector<std::string> tagNames;
};

enum class MigrationOutcome : uint8_t {
  Resolved,                   // addressed to a Verse or Paragraph unit
  ResolvedReferenceMismatch,  // addressed, but disagrees with the stored ref
  ResolvedDocumentOffset,     // the document has no units; kept the raw offset
  PendingUpgrade,             // the EPUB was unavailable; upgrade on next open
  DroppedNoTags,              // carried no tags; nothing to preserve
};

struct MigrationInputs {
  std::string pubKey;
  std::string document;
  DocumentUnits units;
  bool sourceAvailable = true;
  TagPalette* palette = nullptr;  // optional; when set, tag names become global ids
  // Visible text of a unit, for the fingerprint. Returns empty when unknown.
  std::function<std::string(const Unit&)> unitText;
};

struct MigrationResult {
  std::optional<TaggedPassage> passage;
  MigrationOutcome outcome = MigrationOutcome::DroppedNoTags;
  bool referenceAgrees = true;
  std::string resolvedReference;  // "119:145", what the address actually says
};

MigrationResult planMigration(const MigrationInputs& in, const LegacyHighlight& legacy);

// Registers every name in the palette, so a tag the user defined but never
// applied survives the migration.
void adoptTagNames(TagPalette& palette, const std::vector<std::string>& names);

// "Salmos 119:145" -> "119:145"; "" when there is no chapter:verse tail.
std::string referenceTail(const std::string& reference);

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/MigrationPlanner.cpp`**

```cpp
#include "StudyStore/MigrationPlanner.h"

#include <cstdio>

namespace study {

std::string referenceTail(const std::string& reference) {
  const size_t colon = reference.rfind(':');
  if (colon == std::string::npos) return "";
  size_t start = reference.find_last_of(" \t", colon);
  start = (start == std::string::npos) ? 0 : start + 1;
  unsigned chapter = 0, verse = 0;
  char tail = '\0';
  if (sscanf(reference.c_str() + start, "%u:%u%c", &chapter, &verse, &tail) != 2) return "";
  char buf[24];
  snprintf(buf, sizeof(buf), "%u:%u", chapter, verse);
  return buf;
}

void adoptTagNames(TagPalette& palette, const std::vector<std::string>& names) {
  for (const auto& name : names) palette.add(name);
}

MigrationResult planMigration(const MigrationInputs& in, const LegacyHighlight& legacy) {
  MigrationResult out;
  if (legacy.tagNames.empty()) {
    out.outcome = MigrationOutcome::DroppedNoTags;
    return out;
  }

  TaggedPassage p;
  p.document = in.document;
  p.documentSpine = legacy.spineIndex;
  p.snippet = legacy.snippet;
  p.reference = legacy.reference;

  // With no palette the caller is resolving addresses only (the dry-run tool
  // does this); the passage comes back with its address and no ids.
  if (in.palette) {
    for (const auto& name : legacy.tagNames) {
      if (const auto id = in.palette->add(name)) p.tags.push_back(*id);
    }
  }
  if (!in.sourceAvailable) {
    p.start = Unit{UnitKind::DocumentOffset, 0, 0, 0, legacy.start};
    p.end = Unit{UnitKind::DocumentOffset, 0, 0, 0, legacy.end};
    p.pendingUpgrade = true;
    out.passage = std::move(p);
    out.outcome = MigrationOutcome::PendingUpgrade;
    return out;
  }

  p.start = resolve(in.units, legacy.start);
  p.end = resolve(in.units, legacy.end);

  if (in.unitText) p.fingerprint = fingerprintOf(in.unitText(p.start));

  if (p.start.kind == UnitKind::DocumentOffset) {
    out.outcome = MigrationOutcome::ResolvedDocumentOffset;
    out.passage = std::move(p);
    return out;
  }

  if (p.start.kind == UnitKind::Verse) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u:%u", p.start.major, p.start.minor);
    out.resolvedReference = buf;
    const std::string stored = referenceTail(legacy.reference);
    out.referenceAgrees = stored.empty() || stored == out.resolvedReference;
  }

  out.outcome = out.referenceAgrees ? MigrationOutcome::Resolved : MigrationOutcome::ResolvedReferenceMismatch;
  out.passage = std::move(p);
  return out;
}

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/migration_planner/CMakeLists.txt`:

```cmake
add_executable(MigrationPlannerTest
  MigrationPlannerTest.cpp
  ${REPO_ROOT}/lib/StudyStore/MigrationPlanner.cpp
  ${REPO_ROOT}/lib/StudyStore/UnitAnchors.cpp
  ${REPO_ROOT}/lib/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(MigrationPlannerTest PRIVATE
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(MigrationPlannerTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(MigrationPlannerTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(MigrationPlannerTest)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(migration_planner)
```

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target MigrationPlannerTest && ./build/migration_planner/MigrationPlannerTest
```

Expected: `[  PASSED  ] 11 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/MigrationPlanner.h lib/StudyStore/MigrationPlanner.cpp test/migration_planner test/CMakeLists.txt
git commit -m "feat: plan a legacy highlight's migration to a unit address"
```

---

## Task 9: The unit index — format and lazy cache

Built **per document, on first use of that document**. The spec's arithmetic for
an eager build stands: 3,937 NWT documents × ~30 ms is 10–13 minutes, the task
watchdog fires at 5 s, and auto-sleep counts *input* inactivity
(`src/main.cpp:652`) which a background build does not reset.

**Files:**
- Create: `lib/StudyStore/UnitIndexFormat.h`, `lib/StudyStore/UnitIndexFormat.cpp`
- Create: `test/unit_index_format/UnitIndexFormatTest.cpp`, `test/unit_index_format/CMakeLists.txt`
- Create: `src/study/UnitIndexCache.h`, `src/study/UnitIndexCache.cpp`
- Modify: `test/CMakeLists.txt`

### On-disk layout — `/.berean/units/<pubkey>.bin`

One file per publication. The spec's rejection of file-per-document stands:
3,937 files in one FAT directory with `USE_UTF8_LONG_NAMES=1` is ~500 KB of
directory entries scanned linearly on every open, and ~126 MB of cluster slack
for ~2 MB of data.

```
Header (32 bytes, little-endian)
  magic          uint32   'B','U','I','1'
  formatVersion  uint16   1
  documentCount  uint16
  sourceSize     uint32   EPUB file size at build time
  sourceMtime    uint32   EPUB mtime at build time
  tableOffset    uint32   byte offset of the document table
  reserved       uint32[3]

Document table entry (16 bytes each, indexed by spine index)
  dataOffset     uint32   0 when this document has not been indexed yet
  dataLength     uint32
  contentCrc     uint32   CRC32 of the document's visible codepoints
  kind           uint8    UnitKind
  book           uint8    canonical Bible book, or 0
  anchorCount    uint16

Anchor record (8 bytes each)
  offset         uint32
  major          uint16
  minor          uint16
```

**Invalidation.** The header's `sourceSize`/`sourceMtime` catch a replaced file;
the per-document `contentCrc` catches a same-length correction that neither
would. This repo has been bitten by the weaker check already —
`BookMetadataCache.cpp:467` validates on cache version only, which is why
replacing an EPUB in place kept serving a stale TOC. Any mismatch rebuilds that
document, not the whole file.

**Alignment.** Every multi-byte field is read and written with `memcpy`, never a
pointer cast. The S3 tolerates unaligned loads where the C3 faults, but this
format is shared code and the rule in `CLAUDE.md` is unconditional.

- [ ] **Step 1: Write the failing test** — `test/unit_index_format/UnitIndexFormatTest.cpp`

Cover, one test each:

```cpp
TEST(UnitIndexFormat, RoundTripsAHeaderThroughBytes);
TEST(UnitIndexFormat, RoundTripsADocumentEntryWithItsAnchors);
TEST(UnitIndexFormat, RejectsAWrongMagic);
TEST(UnitIndexFormat, RejectsAFutureFormatVersion);
TEST(UnitIndexFormat, ReportsADocumentWithDataOffsetZeroAsNotYetIndexed);
TEST(UnitIndexFormat, DetectsAChangedSourceSizeAsStale);
TEST(UnitIndexFormat, DetectsAChangedContentCrcAsStalePerDocument);
TEST(UnitIndexFormat, ReadsEveryFieldThroughMemcpyOnAnUnalignedBuffer);
```

The last one is the alignment guard and must be written as: build the serialised
bytes, copy them into `std::vector<uint8_t>` at offset 1, parse from
`buf.data() + 1`, and assert every field. Run it under
`-fsanitize=address,undefined` locally; a pointer cast trips UBSan here.

- [ ] **Step 2: Run it and watch it fail.** Expected: `UnitIndexFormat.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/UnitIndexFormat.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "StudyStore/UnitAnchors.h"

namespace study {

inline constexpr uint32_t UNIT_INDEX_MAGIC = 0x31495542;  // "BUI1"
inline constexpr uint16_t UNIT_INDEX_VERSION = 1;
inline constexpr size_t UNIT_INDEX_HEADER_BYTES = 32;
inline constexpr size_t UNIT_INDEX_ENTRY_BYTES = 16;
inline constexpr size_t UNIT_INDEX_ANCHOR_BYTES = 8;

struct UnitIndexHeader {
  uint16_t documentCount = 0;
  uint32_t sourceSize = 0;
  uint32_t sourceMtime = 0;
  uint32_t tableOffset = 0;
};

struct UnitIndexEntry {
  uint32_t dataOffset = 0;  // 0 means not yet indexed
  uint32_t dataLength = 0;
  uint32_t contentCrc = 0;
  UnitKind kind = UnitKind::DocumentOffset;
  uint8_t book = 0;
  uint16_t anchorCount = 0;

  bool indexed() const { return dataOffset != 0; }
};

void writeHeader(uint8_t* out, const UnitIndexHeader& h);
std::optional<UnitIndexHeader> readHeader(const uint8_t* in, size_t length);

void writeEntry(uint8_t* out, const UnitIndexEntry& e);
UnitIndexEntry readEntry(const uint8_t* in);

void writeAnchors(uint8_t* out, const std::vector<UnitAnchor>& anchors);
std::vector<UnitAnchor> readAnchors(const uint8_t* in, uint16_t count);

// True when the EPUB behind this index has been replaced.
bool headerIsStale(const UnitIndexHeader& h, uint32_t sourceSize, uint32_t sourceMtime);

}  // namespace study
```

- [ ] **Step 4: Implement it**, every field through `memcpy`. Follow the
      little-endian order in the layout table above exactly.

- [ ] **Step 5: Register the test** (`add_subdirectory(unit_index_format)`; sources
      are `UnitIndexFormat.cpp`, `Unit.cpp`, `UnitAnchors.cpp` plus expat as in Task 3).

- [ ] **Step 6: Run and watch it pass.** Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 7: Write `src/study/UnitIndexCache.h`** — the firmware half, no host test

```cpp
#pragma once

#include <Epub.h>

#include <string>

#include "StudyStore/UnitIndexFormat.h"

// Lazily indexes one publication's documents, one document at a time, on first
// use. Never indexes a publication eagerly: the NWT's 3,937 documents would take
// 10-13 minutes and trip the task watchdog.
//
// Owned by the main task. Every SD access goes through HalStorage, which holds
// storageMutex -- SdFat is not thread-safe and the web server task also writes.
class UnitIndexCache {
 public:
  UnitIndexCache(Epub& epub, std::string pubKey);

  // The units for `spineIndex`, building and persisting them if absent or
  // stale. Returns a DocumentOffset-kind result when the document has no units
  // or the build fails -- readable, addressing degraded, never fatal.
  const study::DocumentUnits& unitsFor(uint16_t spineIndex);

  // Canonical Bible book for a spine index, or 0. Built once from
  // biblebooknav.xhtml via BibleNav::Scanner, memoised for the object's life.
  uint8_t bookFor(uint16_t spineIndex);

  bool ready() const { return ready_; }

 private:
  bool openOrCreate();
  bool buildDocument(uint16_t spineIndex);

  Epub& epub_;
  std::string pubKey_;
  bool ready_ = false;
  uint16_t cachedSpine_ = UINT16_MAX;
  study::DocumentUnits cached_;  // exactly one document held in RAM at a time
};
```

**Why one document in RAM:** the densest measured document carries 58 anchors —
under 500 bytes. Holding every indexed document would be unbounded; holding one
costs nothing and matches the access pattern, which is one document per page
turn.

- [ ] **Step 8: Implement `src/study/UnitIndexCache.cpp`.** Order of work inside
      `unitsFor`:
      1. Return `cached_` when `cachedSpine_ == spineIndex`.
      2. Read the entry; if `indexed()` and `contentCrc` matches the document's
         current visible-text CRC, load the anchors and return.
      3. Otherwise read the document through `Epub`, run `study::scanUnits`,
         stamp `book` from `bookFor(spineIndex)`, append the anchors to the
         file, rewrite the entry, and return.
      Every write goes through `Storage`; call `vTaskDelay(1)` between documents
      if a caller ever loops, so a bulk pass cannot trip the watchdog.

- [ ] **Step 9: Build the firmware and commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add lib/StudyStore/UnitIndexFormat.h lib/StudyStore/UnitIndexFormat.cpp src/study/UnitIndexCache.h src/study/UnitIndexCache.cpp test/unit_index_format test/CMakeLists.txt
git commit -m "feat: index a publication's units lazily, one document at a time"
```

---

## Task 10: The storage shells and the `StudyStore` façade

Thin, firmware-only, and disciplined per the spec's storage rules: atomic writes
only, an explicit byte budget checked before writing, a format version a future
build refuses rather than reinterprets, and `storageMutex` held on write.

**Files:**
- Create: `src/study/PassageFile.{h,cpp}`, `src/study/TagPaletteFile.{h,cpp}`, `src/study/StudyStore.{h,cpp}`

- [ ] **Step 1: Write `src/study/PassageFile.h`**

Model it on `src/util/HighlightFile.h`, which already solved this exact problem
— including the four-state load result that a `bool` collapses wrongly.

```cpp
#pragma once

#include <cstdint>
#include <string>

#include "StudyStore/PassageDoc.h"

// Moves bytes between PassageDoc and /.berean/passages/. All format rules live
// in PassageDoc; the .tmp promotion rule and the budget check live here.
//
// Single-writer: the main task owns this. If the web server ever writes
// passages, add a mutex -- PersistableStore.h documents the same hazard.
namespace PassageFile {

// The Bible shards by canonical book so one pubkey cannot grow past the budget;
// everything else uses segment 0 and a single file. See the plan's Task 7.
std::string path(const std::string& pubKey, uint8_t segment);

enum class LoadResult : uint8_t { Loaded, Empty, RecoveredFromTemp, Failed };

// Failed means the bytes could not be read, parsed or validated and the file
// may still hold the user's data -- the caller MUST NOT save after Failed.
LoadResult load(const std::string& pubKey, uint8_t segment, study::PassageDoc& doc);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

// Measures the serialised document and refuses BEFORE touching any file when it
// exceeds PassageDoc::SAVE_BYTE_BUDGET. Writes through writeDocToFileAtomic --
// never writeDocToFile, which is the non-atomic variant.
SaveResult save(const std::string& pubKey, uint8_t segment, const study::PassageDoc& doc);

}  // namespace PassageFile
```

- [ ] **Step 2: Implement `PassageFile.cpp`** by copying `src/util/HighlightFile.cpp`
      and changing the path helper and the document type. Reuse
      `highlightLoadAction` from `src/util/HighlightFileAction.h` — it is already
      host-tested and the state machine is identical. **Do not reimplement it.**

- [ ] **Step 3: Write and implement `src/study/TagPaletteFile.{h,cpp}`** — same shape,
      one file at `/.berean/tags.json`, no segment.

- [ ] **Step 4: Write `src/study/StudyStore.h`** — the one object the activities talk to

```cpp
#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/TagPalette.h"
#include "study/UnitIndexCache.h"

// The device's study data, for the publication currently open.
//
// Holds the global tag palette plus the passages for the open publication's
// current segment. Loads lazily and saves on change, debounced -- SD writes cost
// serialisation, I/O and storageMutex, and PersistableStore.h warns that taking
// that mutex on a read path stalls rendering.
class StudyStore {
 public:
  static StudyStore& getInstance();

  // Called when a book opens. Resolves the pubkey, loads the palette, and
  // prepares the unit index. Cheap: no document is scanned here.
  bool openPublication(Epub& epub);
  void closePublication();

  const study::TagPalette& palette() const { return palette_; }
  study::TagPalette& mutablePalette() { return palette_; }

  // Passages whose start unit is in this spine document, resolved to current
  // document offsets for painting. Returns nothing for a passage whose
  // fingerprint no longer matches -- see the degradation table in the spec.
  struct PaintedPassage {
    const study::TaggedPassage* passage;
    uint32_t startOffset;
    uint32_t endOffset;
  };
  std::vector<PaintedPassage> passagesInDocument(uint16_t spineIndex);

  // Adds a passage at a document offset range, allocating tag ids by name.
  bool addPassage(uint16_t spineIndex, uint32_t startOffset, uint32_t endOffset, const std::string& snippet,
                  const std::string& reference, const std::vector<study::TagId>& tags);

  bool removePassage(const study::TaggedPassage& passage);
  bool setPassageTags(const study::TaggedPassage& passage, std::vector<study::TagId> tags);

  // Every passage carrying `id`, across publications, via the reverse index.
  struct TaggedHit {
    std::string pubKey;
    uint8_t segment;
    study::TaggedPassage passage;
  };
  std::vector<TaggedHit> passagesWithTag(study::TagId id);

  bool save();

 private:
  StudyStore() = default;

  study::TagPalette palette_;
  study::PassageDoc passages_;
  std::string pubKey_;
  uint8_t segment_ = 0;
  bool dirty_ = false;
  std::unique_ptr<UnitIndexCache> units_;
};

#define STUDY StudyStore::getInstance()
```

- [ ] **Step 5: Implement `StudyStore.cpp`.** Three rules the implementer must hold:

  1. **Switching segment saves first.** Moving from Psalms to Proverbs loads a
     different passages file; the outgoing one is saved if dirty, before the
     incoming one is read. Losing a mark to a book change is the worst bug this
     phase could ship.
  2. **`passagesInDocument` never writes and never rebuilds the index eagerly.**
     It is on the page-turn path.
  3. **Degradation follows the spec's table exactly:** fingerprint matches and
     offsets fit → paint the span; fingerprint matches and offsets do not fit →
     paint the whole unit; **fingerprint differs → paint nothing** and leave it
     for the tag list to surface as *el texto cambió*.

- [ ] **Step 6: Build and commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add src/study
git commit -m "feat: add the study store and its atomic, budgeted files"
```

---

## Task 11: The reverse index

`/.berean/tagindex/<tagid>.bin` — a cache of a question the passages files can
always answer, so it is **rebuilt on mismatch, never patched**. A corrupt index
is a slow query; a trusted-but-wrong index is wrong data, and the web server's
`POST /delete` (`CrossPointWebServer.cpp:164`) can remove a passages file behind
its back.

**Files:**
- Create: `src/study/TagIndexFile.{h,cpp}`
- Create: `lib/StudyStore/TagIndexFormat.{h,cpp}`, `test/tag_index_format/`

```
Header (16 bytes)
  magic        uint32   'B','T','I','1'
  version      uint16
  entryCount   uint16
  generation   uint32   bumped by every passages write
  reserved     uint32

Entry (variable)
  pubKeyLen    uint8
  pubKey       char[pubKeyLen]
  segment      uint8
  passageIndex uint16
```

- [ ] **Step 1: Host-test the format** — round trip, wrong magic rejected, future
      version rejected, generation mismatch reported as stale, a truncated file
      reported as stale rather than parsed into garbage. Five tests.

- [ ] **Step 2: Implement `TagIndexFile`** with one entry point the store calls:

```cpp
// Returns the hits for `id`, rebuilding the index from the passages files first
// when the stored generation disagrees with the store's. Rebuild for the user's
// real data (one publication, 63 passages) is milliseconds.
std::vector<StudyStore::TaggedHit> lookup(study::TagId id, uint32_t currentGeneration);
```

- [ ] **Step 3: Build, run the host suite, commit**

```bash
cd test && cmake --build build && ctest --test-dir build --output-on-failure
cd .. && /Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add lib/StudyStore/TagIndexFormat.h lib/StudyStore/TagIndexFormat.cpp src/study/TagIndexFile.h src/study/TagIndexFile.cpp test/tag_index_format test/CMakeLists.txt
git commit -m "feat: add the rebuildable reverse tag index"
```

---

## Task 12: The migration runner and its report

Resumable and idempotent by construction: one source file at a time, renamed to
`<name>.json.migrated` on success. A battery death mid-migration resumes exactly
where it stopped, because the rename is the commit.

**The old store is never deleted.** It is the only rollback, and an OTA rollback
to a Phase 0 build across `app0`/`app1` is a real path — one this device has
already exercised twice.

**Files:**
- Create: `src/study/MigrationRunner.{h,cpp}`
- Modify: `src/main.cpp` (call it once at boot), `src/network/CrossPointWebServer.cpp` (serve the report)

- [ ] **Step 1: Write `src/study/MigrationRunner.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

// Migrates /.crosspoint/highlights/*.json into the /.berean/ study store.
//
// Resumable: each source file is renamed to <name>.json.migrated only after its
// passages are safely written, so an interrupted run resumes at the first file
// without that suffix. Idempotent: a file already renamed is skipped.
//
// The old store is NEVER deleted. An OTA rollback to a pre-Phase-1 build must
// still find the user's data.
namespace MigrationRunner {

struct Summary {
  uint16_t sourceFiles = 0;
  uint16_t highlightsRead = 0;
  uint16_t passagesWritten = 0;
  uint16_t referenceMismatches = 0;
  uint16_t pendingUpgrade = 0;
  uint16_t dropped = 0;
  uint16_t tagsAdopted = 0;
};

// Runs any pending migration. Returns false only on a failure that left the
// store inconsistent; "nothing to do" is true. Safe to call on every boot.
bool runIfPending(Summary& summary);

// True when at least one un-migrated source file exists.
bool pending();

inline constexpr const char* REPORT_PATH = "/.berean/migration-report.json";

}  // namespace MigrationRunner
```

- [ ] **Step 2: Implement `MigrationRunner.cpp`.** Per source file:

  1. Load it with the **existing** `HighlightFile::load`. On `Failed`, record the
     file in the report and **skip it without renaming** — the data may still be
     there and a rename would strand it.
  2. Derive the pubkey. The source filename is a flattened book path, so the
     book path is recoverable; open the EPUB if present to get the Bible flag.
     If the EPUB is absent, `sourceAvailable = false` and every passage in that
     file migrates as `PendingUpgrade`.
  3. `study::adoptTagNames(palette, doc.tags())` **first**, so tags the user
     defined but never applied survive. The user has two of these.
  4. For each highlight, call `study::planMigration` and add the resulting
     passage to the right `(pubKey, segment)` document.
  5. Save every touched passages file and the palette. **Only if every save
     returned `Ok`**, rename the source to `<name>.json.migrated`.
  6. Append a report row.

  Call `vTaskDelay(1)` between source files. The user's real run is one file,
  50 documents and 63 passages — about 1.5 seconds.

- [ ] **Step 3: Write the report.** `/.berean/migration-report.json`:

```json
{
  "v": 1,
  "ranAt": 1789345983,
  "summary": {
    "sourceFiles": 1, "highlightsRead": 63, "passagesWritten": 63,
    "referenceMismatches": 0, "pendingUpgrade": 0, "dropped": 0, "tagsAdopted": 48
  },
  "files": [
    {
      "source": "Traduccion del Nuevo Mundo (nwt-S) - WATCHTOWER.json",
      "pubKey": "bible",
      "read": 63, "written": 63, "mismatches": 0, "pending": 0,
      "drops": []
    }
  ]
}
```

  The report is budgeted like every other store: if it would exceed
  `persist::DEFAULT_SAVE_BUDGET`, write the summary and the first N file rows
  plus a `"truncated": true` flag. A report that refuses to save would be an
  absurd way to fail a successful migration.

- [ ] **Step 4: Call it at boot.** In `src/main.cpp`, after storage is up and
      before the first activity is entered:

```cpp
MigrationRunner::Summary migrationSummary;
if (MigrationRunner::pending()) {
  LOG_INF("MIGRATE", "Migrating study data...");
  if (!MigrationRunner::runIfPending(migrationSummary)) {
    LOG_ERR("MIGRATE", "Migration incomplete; old store retained");
  }
  LOG_INF("MIGRATE", "read=%u written=%u mismatches=%u pending=%u dropped=%u tags=%u",
          migrationSummary.highlightsRead, migrationSummary.passagesWritten,
          migrationSummary.referenceMismatches, migrationSummary.pendingUpgrade,
          migrationSummary.dropped, migrationSummary.tagsAdopted);
}
```

- [ ] **Step 5: Serve the report.** The web server already serves arbitrary SD
      paths through `/download`, so the report is reachable without new code —
      but it needs to be *findable*. Add one route beside the others at
      `CrossPointWebServer.cpp:148`:

```cpp
server->on("/migration", HTTP_GET, [this] { handleMigrationReport(); });
```

  `handleMigrationReport` streams `MigrationRunner::REPORT_PATH` as
  `application/json`, or returns 404 with `{"status":"no migration has run"}`.

- [ ] **Step 6: Build, flash nothing yet, commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add src/study/MigrationRunner.h src/study/MigrationRunner.cpp src/main.cpp src/network/CrossPointWebServer.cpp
git commit -m "feat: migrate the legacy highlight store into the study store"
```

---

## Task 13: Rewire the five activities

The blast radius is contained — `HighlightDoc` reaches exactly five activities:

| File | What changes |
|---|---|
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | Loads `STUDY.openPublication(epub)` instead of `HighlightFile::load`; the paint pass asks `STUDY.passagesInDocument(spine)` |
| `src/activities/reader/PassageSelectActivity.{h,cpp}` | Emits a document offset range; `STUDY.addPassage` turns it into a Unit |
| `src/activities/reader/TagPickerActivity.{h,cpp}` | Reads `STUDY.palette()` rather than a per-book palette |
| `src/activities/reader/HighlightsActivity.{h,cpp}` | Lists `STUDY` passages for the open publication |
| `src/activities/reader/TagFilterActivity.{h,cpp}` | Filters through `STUDY.passagesWithTag(id)` — now across publications |

**Every screen keeps its current layout.** This is a data-source swap, not a
redesign; the launcher and the new sections are Phase 2. If a row moves, a font
changes or a list reorders, that is a regression in this task.

- [ ] **Step 1: `EpubReaderActivity` first**, because it owns the lifecycle. Replace
      the `HighlightDoc` member with the `STUDY` calls, keeping the same paint
      geometry. Build.
- [ ] **Step 2: `PassageSelectActivity`.** It already produces a
      `VisibleRange`; route it into `STUDY.addPassage`. Build.
- [ ] **Step 3: `TagPickerActivity`.** The palette is now global and ids are
      `study::TagId`, not indices into a per-book vector. **This is the one place
      an off-by-one becomes a wrong tag on a real passage** — the old model used
      positional indices, the new one uses allocated ids, and they are not
      interchangeable. Build.
- [ ] **Step 4: `HighlightsActivity` and `TagFilterActivity`.** Build.
- [ ] **Step 5: Delete nothing.** `HighlightDoc`, `HighlightFile` and
      `HighlightFileAction` stay compiled — the migration reads through them and
      their host tests stay green.
- [ ] **Step 6: Full verification**

```bash
cd test && cmake --build build && ctest --test-dir build --output-on-failure
cd .. && /Volumes/stein/.platformio/penv/bin/pio run -e x4pro
/Volumes/stein/.platformio/penv/bin/pio check
./bin/clang-format-fix
git add -A && git commit -m "refactor: point the reader activities at the study store"
```

  `pio check` is not optional. Skipping it in Phase 0 is what put two
  `duplicateBreak` failures into CI.

---

## Task 14: The offline dry run, made repeatable

The migration was dry-run against the user's real backup before this plan
existed: all 63 passages resolved and all 63 agreed with their stored reference.
That run was a throwaway. This task makes it a tool, so the same check can be
repeated against the real data after every change to the planner — without a
device, and without checking copyrighted text into the repo.

**Files:**
- Create: `scripts/migration_dryrun.py`, `tools/migration_dryrun/main.cpp`, `tools/migration_dryrun/CMakeLists.txt`

**Nothing in this task is committed with data in it.** The publication markup and
the passage text are the user's copy of a copyrighted translation; the tool reads
them from paths given on the command line and the repo stores neither.

- [ ] **Step 1: Write `scripts/migration_dryrun.py`** — reads an extracted EPUB's
      OPF to build the spine, reads a legacy highlights JSON, and emits a TSV of
      `spineIndex, start, end, ref, documentPath` plus a book number derived from
      the position of the document's book in `biblebooknav.xhtml`.

- [ ] **Step 2: Write `tools/migration_dryrun/main.cpp`** — links the real
      `MigrationPlanner`, `UnitAnchors`, `VerseAnchors`, `ParagraphAnchors` and
      expat, reads the TSV, and prints one line per passage plus a summary:

```
 total=63  resolved=63  mismatch=0  document-offset=0  pending=0  dropped=0
```

  Exit non-zero when `mismatch > 0`, so it can gate a change to the planner.

- [ ] **Step 3: Run it against the real backup**

```bash
python3 scripts/migration_dryrun.py \
  --epub-dir  <extracted nwt_S> \
  --highlights "/Volumes/stein/Documents/development/personal/berean-os-backup-2026-09-14/highlights/Traducción del Nuevo Mundo (nwt-S) - WATCHTOWER.json" \
  --out /tmp/dryrun.tsv
cmake -S tools/migration_dryrun -B /tmp/dryrun-build && cmake --build /tmp/dryrun-build
/tmp/dryrun-build/migration_dryrun /tmp/dryrun.tsv
```

  Expected: `total=63  resolved=63  mismatch=0`. **Any other result stops the
  phase** — it means the planner disagrees with a result already known to be
  correct.

- [ ] **Step 4: Commit the tool, not the data**

```bash
git status --short   # confirm no .epub, .xhtml, .tsv or highlights JSON is staged
git add scripts/migration_dryrun.py tools/migration_dryrun
git commit -m "test: add a repeatable offline migration dry run"
```

---

## Task 15: Device verification

Human-tester scope. Do this **before** telling the user the phase is ready, and
report the numbers rather than a claim.

- [ ] **Step 1: Back up the device again.** The card holds the only copy of the
      user's study data, and this is the first build that writes a new store.

```bash
curl -s "http://<device-ip>/download?path=/.crosspoint/highlights/<file>.json" -o pre-migration.json
```

- [ ] **Step 2: Build and side-load a dev build** (OTA installs the release build,
      which has `LOG_LEVEL=0` and no serial output — useless for a first run):

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
curl -H "Expect:" -F "file=@.pio/build/x4pro/firmware.bin" "http://<device-ip>/upload?path=/"
```

  Then *Settings → SD firmware update*. The `Expect:` header suppression is not
  optional — the ESP32 web server never answers `100-continue` and the transfer
  hangs.

- [ ] **Step 3: Watch the migration in the serial log**

```bash
cat /dev/cu.usbmodem31101 > serial.log &
```

  Expected: `read=63 written=63 mismatches=0 pending=0 dropped=0 tags=48`.
  Never pass a baud rate to this transport — the native USB-JTAG bridge has no
  line settings and both `pio device monitor` and a baud-overridden `esptool`
  fail on it.

- [ ] **Step 4: Read the report over the web server**

```bash
curl -s "http://<device-ip>/migration" | python3 -m json.tool
```

- [ ] **Step 5: Check the four things that would mean data loss**

  | Check | Expected |
  |---|---|
  | `/.crosspoint/highlights/<file>.json.migrated` exists | yes — renamed, not deleted |
  | Tag list shows every name | **48**, including `igualdad` and `transformación` |
  | Passage count | **63** |
  | A marked verse still paints in the reader | the same span, not the whole verse |

- [ ] **Step 6: Confirm the fallback still exists.** Power-cycle and confirm the
      migration does **not** run twice (the summary should not reappear in the log).

- [ ] **Step 7: Report the numbers to the user** — the report JSON and the four
      checks above, not a summary claim.

---

## Self-review

**Spec coverage.** Every Phase 1 line of the spec's build order maps to a task:
unified `Unit` type → Task 1; `data-pid` scanner → Task 2; unit index → Tasks 9;
tag store → Tasks 5, 7, 10; reverse index → Task 11; migration → Tasks 8, 12.
The acceptance criterion — "`migration-report.json` served over the web server;
old store retained" — is Task 12 Step 5 and Task 15 Step 5.

**Deviations from the spec, all deliberate and argued above:**

1. `Unit` gains a `book` field. Without it the Bible's language-free pubkey does
   not work and Genesis 1:1 collides with Matthew 1:1.
2. Passages shard by Bible book, because one `bible` file hits the byte budget
   at ~150 passages and the user has 63 already.
3. Migration defers address resolution when the EPUB is absent rather than
   requiring it, so a missing card never costs a passage.
4. The reverse index is rebuilt on generation mismatch rather than patched.

**Known gaps carried forward, not silently dropped:**

- The spec's open item on whether a passage may exist with zero tags is
  **answered no** here, matching the current model and the user's real data (0
  of 63 untagged). Task 7 encodes it, and a reserved tag id 0 remains available
  if Phase 2 wants to revisit it.
- Document-filename stability under a corrected reissue is still unverified. The
  `documentSpine` fallback stands on that uncertainty. Unchanged by this phase.
- `ReturnStack` capacity remains a Phase 2 decision.

---

## What comes after

Phase 2 (launcher shell, four sections, the new input model, two-tap selection,
`MappedInputManager` deleted) and Phase 3 (Buscar) each get their own plan,
written once this one lands — their details depend on what the store actually
looks like in practice.
