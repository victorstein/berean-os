# bereanOS Phase 1 — the study store

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the per-book highlight store with a publication-keyed, unit-addressed tag store, and migrate the user's existing 48 tags and 63 tagged passages into it without losing one.

**Architecture:** A tagged passage is addressed by a `Unit` — a verse, a numbered paragraph, or a raw document offset — rather than by a byte position in a file. Two expat scanners produce units; a per-publication index caches them lazily, one document at a time. The migration reads the old store, resolves each old `(spineIndex, offset)` pair through that index, and writes a global store beside it. The old store is never deleted.

**Tech Stack:** C++20, expat (`XML_GE=0`), ArduinoJson v7, GoogleTest on the host, PlatformIO/ESP-IDF on the device.

> **Revision note — 2026-09-14, after adversarial review.**
> Two independent reviewers found nine blocker-class defects in the first draft.
> The ones that changed the design rather than the code:
>
> - **The acceptance checks passed on a failed migration.** The plan claimed a
>   book path was recoverable from a flattened highlight filename. It is not —
>   `pathflatten::toCacheName` is lossy three ways and says so in its own header.
>   A migration that could not find the EPUB wrote every passage as a raw
>   document offset, which paints identically to today, so all four device checks
>   passed while the phase achieved nothing. Task 14 now recovers the path by
>   walking the card, and the report and checklist break results down **by unit
>   kind** so "migrated" and "actually addressed" cannot be confused.
> - **Deleting a tag deleted passages.** `TagFilterActivity` already has a
>   long-press delete. With a global palette and a `removeTagEverywhere` that
>   erased zero-tag passages, one tidy-up gesture destroyed every passage that
>   carried only that tag. Palette deletion is now **retire-only**; no palette
>   operation can remove a passage.
> - **Sharding by Bible book was wrong.** It cut the highlights browser down to
>   the open book's marks, and it did not even work: 2,595 NWT documents carry no
>   verse, so every one of their passages landed in a single unsharded
>   `bible-0.json` with the same ceiling. Sharding is gone; the passages file is
>   **streamed** instead, which is what the spec asked for in the first place.
> - **The `.migrated` rename voided the rollback the spec calls non-negotiable.**
>   A Phase 0 build resolves the original filename and would have found nothing.
>   Migration progress now lives in a separate ledger and the old files are not
>   touched at all.
> - **The fingerprint had no producer.** Nothing in the plan extracted a unit's
>   visible text, and the repo's two candidate mechanisms are known to disagree
>   about U+202F/U+00A0. One shared extractor is now Task 4, used by both the
>   migration and the paint path so they cannot drift.
> - **The dry run proved less than the first draft claimed.** The `ref` strings
>   were written by the device using `VerseAnchors` against the same offsets the
>   dry run re-resolves with `VerseAnchors`, so 63/63 agreement mostly shows the
>   host reproduces the device on identical bytes. It is kept as a regression
>   gate, moved before the code that depends on it, and re-pointed at a copy
>   pulled off the card rather than a desktop unzip.
>
> The reverse index moved to Phase 3: at 63 passages it costs more than the scan
> it replaces, which the first draft admitted and then scheduled anyway.

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
- **`data-pid` order is not document order.** In 40 of 180 documents — and in 40
  of the **87** that carry `data-pid` at all, i.e. 46% of the documents where a
  pid search could even run — the values
  run e.g. `1,2,3,4,5,6,40,7,42,8` — study-question `<div class="gen-field">`
  boxes are interleaved with body paragraphs at high pid values. **Anchors sort
  by offset; the pid is the address, never the sort key.** A binary search on pid
  would be wrong in 22% of documents.
- The attribute appears on `p`, `div`, `h1`, `h2`, `h3`, `h4` and `legend`, so
  the scanner keys on the **attribute name**, never an element whitelist. Note
  `data-rel-pid` also exists and must not match.

---

## File structure

> **Directory layout is load-bearing.** PlatformIO puts `lib/<Name>` on the
> include path, never `lib` itself, so `#include "StudyStore/Unit.h"` only
> resolves if the headers sit at `lib/StudyStore/StudyStore/`. This mirrors
> `lib/Epub/Epub/HighlightDoc.h`, which is included as `"Epub/HighlightDoc.h"`
> for exactly this reason. The host suite puts `${REPO_ROOT}/lib` on the include
> path and so hides the mistake until the first `pio run`.

**New, host-testable, no Arduino and no HalStorage** — these are the units that
carry the correctness risk, so every one of them is pure:

| File | Responsibility |
|---|---|
| `lib/StudyStore/StudyStore/Unit.h` / `.cpp` | The `Unit` value type, ordering, and its JSON encoding |
| `lib/StudyStore/StudyStore/UnitAnchors.h` / `.cpp` | Façade: run the right scanner(s) for a document, apply precedence |
| `lib/Epub/Epub/ParagraphAnchors.h` / `.cpp` | The `data-pid` expat scanner |
| `lib/StudyStore/StudyStore/UnitText.h` / `.cpp` | Extract one unit's visible codepoints — the single producer for fingerprints |
| `lib/StudyStore/StudyStore/UnitFingerprint.h` / `.cpp` | Length + CRC32 over a unit's visible codepoints |
| `lib/StudyStore/StudyStore/TaggedPassage.h` | The record |
| `lib/StudyStore/StudyStore/PassageDoc.h` / `.cpp` | Format rules for one publication's passages: parse, validate, serialise, budget |
| `lib/StudyStore/StudyStore/TagPalette.h` / `.cpp` | Global tags: ids, names, tombstones, `nextTagId` |
| `lib/StudyStore/StudyStore/PubKey.h` / `.cpp` | The pubkey resolution ladder |
| `lib/StudyStore/StudyStore/UnitIndexFormat.h` / `.cpp` | The on-disk unit-index header and document table |
| `lib/StudyStore/StudyStore/MigrationPlanner.h` / `.cpp` | Pure: old doc + anchors → passages + report rows |

**New, firmware-only** — thin storage and lifecycle shells over the above:

| File | Responsibility |
|---|---|
| `src/study/PassageFile.{h,cpp}` | Atomic, budgeted, **streamed** read/write of `/.berean/passages/<pubkey>.json` |
| `src/study/TagPaletteFile.{h,cpp}` | Same for `/.berean/tags.json` |
| `src/study/UnitIndexCache.{h,cpp}` | Lazy per-document build, invalidation, `/.berean/units/<pubkey>.bin` |
| `src/study/StudyStore.{h,cpp}` | The one object the activities talk to |
| `src/study/MigrationRunner.{h,cpp}` | Drives the migration, keeps the ledger, writes `migration-report.json` |
| `src/study/BookPathIndex.{h,cpp}` | Recovers a book path from a flattened store filename by walking the card |

**Moved:** `src/util/PathFlatten.{h,cpp}` → `lib/PathFlatten/`. `lib/StudyStore/StudyStore/PubKey.cpp`
needs it, and PlatformIO compiles `lib/` into a static library that `src/` links —
a library translation unit cannot include from `src/`. No file under `lib/` in this
repo does. `test/path_flatten/CMakeLists.txt` and `src/util/HighlightFile.cpp`
update to the new path.

**Modified:** the five activities that speak `HighlightDoc` today
(`EpubReaderActivity`, `HighlightsActivity`, `PassageSelectActivity`,
`TagFilterActivity`, `TagPickerActivity`), **plus `src/activities/ActivityResult.h`**,
whose `ChapterResult`/tag payload carries "indices into `HighlightDoc::tags()`" —
those become allocated ids, not indices, and it is the sixth file referencing the
old model. Also `CrossPointWebServer.cpp` for the report route and `main.cpp` for
the boot-time migration call.

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

### 2. The reverse index is not in this phase

The spec puts `/.berean/tagindex/<tagid>.bin` in Phase 1 so that "show me
everything tagged X" does not open every passages file. Its own justification is
"at 100 publications". Phase 1 has **one**.

At this scale the index is net-negative. Its entries address passages
positionally, so any edit that reorders or removes one invalidates the whole
file and forces a rebuild — which opens every passages file, the exact cost it
was introduced to avoid. Even on a hit it must open each passages file to
materialise a result. The query it replaces is currently zero I/O against an
in-memory document.

It moves to Phase 3, alongside Buscar, which is what actually creates a hundred
publications. Until then `passagesWithTag` is a scan, and the interface it sits
behind does not change when the index arrives.

---

## Task 1: The `Unit` value type

**Files:**
- Create: `lib/StudyStore/StudyStore/Unit.h`, `lib/StudyStore/StudyStore/Unit.cpp`
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

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/Unit.h`**

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

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/Unit.cpp`**

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
  // Kind first, so the ordering is a strict weak ordering consistent with the
  // defaulted operator==. Without it a DocumentOffset and a Verse unit compare
  // mutually non-less yet unequal, which is silently wrong in a std::set or
  // std::map even though std::sort tolerates it.
  if (a.kind != b.kind) return a.kind < b.kind;
  if (a.book != b.book) return a.book < b.book;
  // Paragraph units carry no positional meaning in `minor` -- data-pid runs out
  // of document order in 46% of the documents that have it -- so they order by
  // pid only to be deterministic, never to mean "earlier in the document".
  // Callers needing document order must use the anchor list's offsets.
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
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
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
git add lib/StudyStore/StudyStore/Unit.h lib/StudyStore/StudyStore/Unit.cpp test/unit_type test/CMakeLists.txt
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
  // Measured over 180 real documents: median 55, p95 111, max 413 (a lff_S
  // page of 321 gen-field boxes plus 66 legends). Sized for the p95 rather
  // than the max -- 448 entries would be 3.5 KB of DRAM held per scan for a
  // case that occurs twice in 180 documents.
  state->anchors.reserve(112);

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
- Create: `lib/StudyStore/StudyStore/UnitAnchors.h`, `lib/StudyStore/StudyStore/UnitAnchors.cpp`
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

TEST(UnitAnchorsResolve, DoesNotStampABookOntoAParagraphUnit) {
  auto a = study::scanUnits(kArticleDoc, strlen(kArticleDoc));
  a.book = 19;  // a Bible document that has pids but no verse markers
  const study::Unit u = study::resolve(a, a.anchors[0].offset + 1);
  EXPECT_EQ(u.kind, study::UnitKind::Paragraph);
  EXPECT_EQ(u.book, 0) << "Unit.h promises book is 0 for anything but a Verse";
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

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/UnitAnchors.h`**

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

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/UnitAnchors.cpp`**

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
  // `book` is meaningful only for a Verse unit -- Unit.h says "0 otherwise", and
  // stamping it on a Paragraph would also change which file the passage is
  // filed under.
  const uint8_t book = units.kind == UnitKind::Verse ? units.book : 0;
  return Unit{units.kind, book, best->major, best->minor, documentOffset - best->offset};
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
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitAnchors.cpp
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

Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/StudyStore/UnitAnchors.h lib/StudyStore/StudyStore/UnitAnchors.cpp test/unit_anchors test/CMakeLists.txt
git commit -m "feat: resolve a document offset to a unit, verse first"
```

---

## Task 4: Unit text extraction — the fingerprint's only producer

The first draft had no producer for "a unit's visible text" and three consumers
of it: the migration's fingerprint, the paint-time fingerprint check, and the
unit index's content CRC. Worse, the repo already contains two mechanisms that
*could* supply it and they are known to disagree —
`PassageSelectActivity::selectionLabel` joins laid-out word boxes with a plain
`' '`, while an XHTML character walk preserves the U+202F and U+00A0 the Spanish
NWT puts between a verse number and its text.

If the migration fingerprints one way and the paint path checks the other, every
fingerprint differs, the degradation rule fires, and **all 63 marks silently stop
painting**. One extractor, used by both, is the only way that cannot happen.

**Files:**
- Create: `lib/StudyStore/StudyStore/UnitText.h`, `lib/StudyStore/StudyStore/UnitText.cpp`
- Create: `test/unit_text/UnitTextTest.cpp`, `test/unit_text/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`test/unit_text/UnitTextTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstring>

#include "StudyStore/UnitText.h"

namespace {

// The Spanish NWT's real shape: a marker span, the verse number in sup, then a
// NARROW NO-BREAK SPACE (U+202F) before the text.
const char* kDoc =
    "<html><head><title>skipme</title></head><body>"
    "<p id=\"p3\" data-pid=\"3\">"
    "<span id=\"chapter1_verse7\"></span><strong><sup>7</sup></strong> alpha bravo "
    "<span id=\"chapter1_verse8\"></span><strong><sup>8</sup></strong> charlie delta"
    "</p></body></html>";

TEST(UnitText, ExtractsExactlyTheTextOfOneVerse) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  ASSERT_EQ(units.anchors.size(), 2u);
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);
  EXPECT_NE(text.find("alpha bravo"), std::string::npos);
  EXPECT_EQ(text.find("charlie"), std::string::npos) << "a unit stops where the next one starts";
}

TEST(UnitText, ExtractsTheLastUnitToTheEndOfTheDocument) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[1]);
  EXPECT_NE(text.find("charlie delta"), std::string::npos);
}

// The load-bearing one. The count must agree with VisibleOffsetCounter, which
// is what produced the anchor offsets in the first place.
TEST(UnitText, CodepointCountAgreesWithTheOffsetsThatDelimitIt) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  ASSERT_EQ(units.anchors.size(), 2u);
  const uint32_t span = units.anchors[1].offset - units.anchors[0].offset;
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);

  uint32_t codepoints = 0;
  for (const unsigned char c : text) {
    if ((c & 0xC0u) != 0x80u) ++codepoints;
  }
  EXPECT_EQ(codepoints, span) << "if these disagree, every stored offset is wrong by the difference";
}

TEST(UnitText, PreservesANarrowNoBreakSpaceRatherThanNormalisingIt) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);
  EXPECT_NE(text.find(" "), std::string::npos)
      << "normalising here but not in the offset counter shifts every later offset";
}

TEST(UnitText, ExpandsAKnownEntityToOneCodepoint) {
  const char* doc =
      "<!DOCTYPE html SYSTEM \"about:legacy-compat\">"
      "<html><body><p data-pid=\"1\">alpha&nbsp;bravo</p></body></html>";
  const auto units = study::scanUnits(doc, strlen(doc));
  ASSERT_EQ(units.anchors.size(), 1u);
  const std::string text = study::extractUnitText(doc, strlen(doc), units, units.anchors[0]);
  EXPECT_EQ(text.find("&nbsp;"), std::string::npos) << "the entity must be expanded, not carried literally";
}

TEST(UnitText, ReturnsEmptyForADocumentWithNoUnits) {
  const char* doc = "<html><body><p>alpha</p></body></html>";
  const auto units = study::scanUnits(doc, strlen(doc));
  EXPECT_TRUE(study::extractUnitText(doc, strlen(doc), units, study::UnitAnchor{0, 0, 0}).empty());
}

TEST(UnitText, DocumentCrcIsStableAndChangesWithAWord) {
  const char* other =
      "<html><body><p data-pid=\"1\">alpha bravo charlie</p></body></html>";
  const char* changed =
      "<html><body><p data-pid=\"1\">alpha bravo charlee</p></body></html>";
  EXPECT_EQ(study::documentVisibleCrc(other, strlen(other)), study::documentVisibleCrc(other, strlen(other)));
  EXPECT_NE(study::documentVisibleCrc(other, strlen(other)), study::documentVisibleCrc(changed, strlen(changed)));
}

}  // namespace
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target UnitTextTest
```

Expected: FAIL — `StudyStore/UnitText.h: No such file or directory`.

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/UnitText.h`**

```cpp
#pragma once

#include <cstddef>
#include <string>

#include "StudyStore/UnitAnchors.h"

// The ONE producer of "a unit's visible text". Both the fingerprint written at
// migration time and the fingerprint checked at paint time must come from here,
// or they will disagree: the repo's other candidate, selectionLabel, joins
// laid-out word boxes with a plain space and loses the U+202F the Spanish NWT
// puts before verse text. A disagreement makes every fingerprint mismatch, and
// the degradation rule then refuses to paint any mark at all.
//
// Counting is VisibleOffsetCounter's, unmodified, so the extracted text's
// codepoint count equals the distance between consecutive anchor offsets. That
// invariant is what ties a stored offset to real text; UnitTextTest asserts it.
namespace study {

// Visible text of the unit starting at `anchor`, up to the next anchor or the
// end of the body. Empty when the document has no units.
std::string extractUnitText(const char* xhtml, size_t length, const DocumentUnits& units, const UnitAnchor& anchor);

// CRC32 over the whole document's visible codepoints, for unit-index
// invalidation. Same traversal, so it cannot drift from the offsets.
uint32_t documentVisibleCrc(const char* xhtml, size_t length);

}  // namespace study
```

- [ ] **Step 4: Implement `UnitText.cpp`.** Register the same four expat handlers
      `VerseAnchors.cpp` uses — `onStart`/`onEnd` driving `VisibleOffsetCounter`,
      `onText`, and `XML_SetDefaultHandlerExpand` for entities. Accumulate into a
      `std::string` only while `counter.offset` is within `[anchor.offset,
      nextAnchor.offset)`. **Do not normalise, trim, or collapse whitespace** —
      any transform here breaks the codepoint-count invariant the third test
      asserts. `documentVisibleCrc` runs the same traversal accumulating a CRC
      instead of a string, reusing the nibble-wise `crc32` from Task 7.

- [ ] **Step 5: Register the test** — sources are `UnitText.cpp`, `UnitAnchors.cpp`,
      `Unit.cpp`, `UnitFingerprint.cpp`, `VerseAnchors.cpp`, `ParagraphAnchors.cpp`,
      `htmlEntities.cpp` plus the three expat `.c` files, with
      `XML_GE=0 XML_CONTEXT_BYTES=1024`; include dirs as in Task 3. Append
      `add_subdirectory(unit_text)` to `test/CMakeLists.txt`.

- [ ] **Step 6: Run and watch it pass.** Expected: `[  PASSED  ] 7 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/StudyStore/UnitText.h lib/StudyStore/StudyStore/UnitText.cpp test/unit_text test/CMakeLists.txt
git commit -m "feat: extract a unit's visible text from one place only"
```

---

## Task 5: The pubkey resolution ladder

The spec derives a pubkey from symbol, issue and language. **That derivation has
no source on disk.** The OPF of `w_S_202601` carries
`<dc:identifier id="BookId">urn:uuid:18854DEA-B691-4E92-95D0-3B066C062D83</dc:identifier>`
— a random UUID — and no metadata field anywhere in the file contains the symbol
`w`. The downloader knows the symbol at download time; nothing else does.

Hence a ladder, and a registry the downloader writes.

**Files:**
- Create: `lib/StudyStore/StudyStore/PubKey.h`, `lib/StudyStore/StudyStore/PubKey.cpp`
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
  es.canonVerified = true;
  es.bookPath = "/books/Traduccion del Nuevo Mundo (nwt-S).epub";

  study::PubKeyInputs en{};
  en.isBible = true;
  en.canonVerified = true;
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
  in.canonVerified = true;
  in.registered = study::RegisteredPub{"nwt", "", "S"};
  in.bookPath = "/books/nwt.epub";
  EXPECT_EQ(study::resolvePubKey(in), "bible");
}

TEST(PubKeyLadder, ABibleWithAnUnverifiedCanonDoesNotShareTheGlobalKey) {
  study::PubKeyInputs in{};
  in.isBible = true;
  in.canonVerified = false;
  in.registered = study::RegisteredPub{"byz", "", "E"};
  in.bookPath = "/books/other.epub";
  EXPECT_NE(study::resolvePubKey(in), "bible")
      << "a different canon shifts every book number and lands marks in the wrong book";
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
  bible.canonVerified = true;
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

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/PubKey.h`**

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
  // The book-nav page listed exactly 66 books. The shared `bible` key means a
  // mark made in one translation resolves in another, which is only safe if the
  // canon and its ordering match -- a Bible with extra or reordered books would
  // shift every book number and land marks in the wrong book. Anything
  // unverified gets a per-publication key instead.
  bool canonVerified = false;
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

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/PubKey.cpp`**

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
  if (in.isBible && in.canonVerified) return BIBLE_PUB_KEY;

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
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PubKey.cpp
  ${REPO_ROOT}/lib/PathFlatten/PathFlatten.cpp
)

target_include_directories(PubKeyTest PRIVATE
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/PathFlatten
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

> `PathFlatten` moves from `src/util/` to `lib/PathFlatten/` as part of this task
> — a `lib/` translation unit cannot include from `src/`. Update
> `test/path_flatten/CMakeLists.txt` and `src/util/HighlightFile.cpp` to match.

- [ ] **Step 6: Run and watch it pass**

```bash
cd test && cmake -S . -B build && cmake --build build --target PubKeyTest && ./build/pub_key/PubKeyTest
```

Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/StudyStore/PubKey.h lib/PathFlatten lib/StudyStore/StudyStore/PubKey.cpp test/pub_key test/CMakeLists.txt
git commit -m "feat: resolve publication identity for the study store"
```

---

## Task 6: The global tag palette

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
- Create: `lib/StudyStore/StudyStore/TagPalette.h`, `lib/StudyStore/StudyStore/TagPalette.cpp`
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
  const auto next = q.add("nueva");
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(static_cast<uint16_t>(*next), static_cast<uint16_t>(*after) + 1)
      << "nextTagId must survive the round trip";
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

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/TagPalette.h`**

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

// A scoped enum, not a bare uint16_t, because the model it replaces used
// POSITIONAL INDICES into a per-book vector and the two are not
// interchangeable. TagPickerActivity's selection today is
// std::vector<uint16_t> of indices; with a bare alias every mis-wiring of
// index to id compiles silently and lands the wrong tag on a real passage.
enum class TagId : uint16_t {};

constexpr uint16_t toRaw(const TagId id) { return static_cast<uint16_t>(id); }
constexpr TagId toTagId(const uint16_t raw) { return static_cast<TagId>(raw); }

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
  uint16_t nextRaw_ = 1;  // 0 is reserved; see the spec's open item on untagged passages
};

}  // namespace study
```

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/TagPalette.cpp`**

```cpp
#include "StudyStore/TagPalette.h"

#include <algorithm>

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
  if (nextRaw_ == UINT16_MAX) return std::nullopt;

  const TagId id = toTagId(nextRaw_++);
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
  doc["n"] = nextRaw_;
  const auto tags = doc["t"].to<JsonArray>();
  for (const auto& e : entries_) {
    const auto row = tags.add<JsonObject>();
    row["i"] = toRaw(e.id);
    row["n"] = e.name;
    if (!e.active) row["r"] = true;
  }
}

bool TagPalette::fromJson(const JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;
  const int version = doc["v"] | 0;
  if (version <= 0 || version > FORMAT_VERSION) return false;

  entries_.clear();
  uint16_t highest = 0;

  for (const JsonVariantConst v : doc["t"].as<JsonArrayConst>()) {
    const uint32_t id = v["i"] | 0u;
    const char* name = v["n"] | "";
    if (id == 0 || id > UINT16_MAX || name[0] == '\0') continue;

    std::string text(name);
    if (text.size() > MAX_TAG_NAME_BYTES) text.resize(MAX_TAG_NAME_BYTES);

    const TagId tagId = toTagId(static_cast<uint16_t>(id));
    bool duplicate = false;
    for (const auto& e : entries_) duplicate = duplicate || e.id == tagId;
    if (duplicate) continue;

    entries_.push_back({tagId, std::move(text), !(v["r"] | false)});
    highest = std::max(highest, static_cast<uint16_t>(id));
  }

  const uint32_t stored = doc["n"] | 0u;
  nextRaw_ = static_cast<uint16_t>(std::max<uint32_t>(stored, highest + 1u));
  return true;
}

}  // namespace study
```

- [ ] **Step 5: Register the test**

`test/tag_palette/CMakeLists.txt`:

```cmake
add_executable(TagPaletteTest
  TagPaletteTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
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
git add lib/StudyStore/StudyStore/TagPalette.h lib/StudyStore/StudyStore/TagPalette.cpp test/tag_palette test/CMakeLists.txt
git commit -m "feat: add the global tag palette with retired-id tombstones"
```

---

## Task 7: The unit fingerprint

What tells a reopened passage whether the text it was attached to is still the
text that is there. Length plus CRC32 over the unit's **visible** codepoints —
the same codepoints `VisibleOffsetCounter` counts, so markup changes that do not
change what the reader sees do not invalidate a mark.

**Files:**
- Create: `lib/StudyStore/StudyStore/UnitFingerprint.h`, `lib/StudyStore/StudyStore/UnitFingerprint.cpp`
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

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/UnitFingerprint.h`**

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

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/UnitFingerprint.cpp`**

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
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
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
git add lib/StudyStore/StudyStore/UnitFingerprint.h lib/StudyStore/StudyStore/UnitFingerprint.cpp test/unit_fingerprint test/CMakeLists.txt
git commit -m "feat: fingerprint a unit's visible text"
```

---

## Task 8: The passage record and its per-publication document

### One file per publication, read by streaming

`bible` is a single pubkey, so every Bible passage the user ever makes lands in
one file. At ~300 serialised bytes per passage, `SDCardManager::readFile`'s
50,000-byte silent truncation caps that at roughly 150 passages — and the user
has 63 after one book of study.

The first draft sharded by canonical Bible book to escape that. Review killed it
on two counts. It cut `HighlightsActivity` — documented as showing "this ONE
book's" marks — down to the open *Bible* book's marks, roughly 3 instead of 63,
which contradicts this phase's own rule that no screen changes. And it did not
even work: `Unit::book` is 0 for every `DocumentOffset` unit, which in the NWT is
2,595 documents of front matter, appendices, concordance and study notes, so all
of their passages landed in one unsharded `bible-0.json` with the identical
ceiling.

**The passages file is therefore one file per pubkey, and it is streamed.** This
is what the spec asked for and the first draft skipped: *"Anything that can exceed
~40 KB does not use `Storage.readFile` at all. Stream it."* `lib/JsonParser`
already provides the streaming reader, and `test/streaming_json_parser` already
tests it. `SAVE_BYTE_BUDGET` still bounds the write — a file too big to write is
still refused rather than truncated — but it is raised to a figure the read path
can actually honour, because the read no longer goes through the 50,000-byte cap.

**Files:**
- Create: `lib/StudyStore/StudyStore/TaggedPassage.h`, `lib/StudyStore/StudyStore/PassageDoc.h`, `lib/StudyStore/StudyStore/PassageDoc.cpp`
- Create: `test/passage_doc/PassageDocTest.cpp`, `test/passage_doc/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write `lib/StudyStore/StudyStore/TaggedPassage.h`** (no test of its own — it is a
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

#include <algorithm>

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
  p.tags = {study::toTagId(3), study::toTagId(17)};
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

TEST(PassageDocValidation, TruncatesAnOverlongSnippetWithoutSplittingACodepoint) {
  study::TaggedPassage p = samplePassage();
  // Accented Spanish, so a raw byte cut lands mid-sequence. An ASCII fixture
  // here cannot fail and so proves nothing -- the first draft used one.
  std::string accented;
  while (accented.size() < 400) accented += "transformación ";
  p.snippet = accented;
  p.reference = std::string("Génesis 1:1 ") + accented;

  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  const auto& stored = doc.passages()[0];
  EXPECT_LE(stored.snippet.size(), study::PassageDoc::MAX_SNIPPET_BYTES);
  EXPECT_LE(stored.reference.size(), study::PassageDoc::MAX_REFERENCE_BYTES);

  for (const std::string& text : {stored.snippet, stored.reference}) {
    ASSERT_FALSE(text.empty());
    const unsigned char last = static_cast<unsigned char>(text.back());
    EXPECT_FALSE((last & 0xC0u) == 0x80u) << "truncation left a dangling continuation byte";
    EXPECT_FALSE((last & 0xE0u) == 0xC0u) << "truncation left a lead byte with no continuation";
  }
}

TEST(PassageDocValidation, DedupesAndCapsTags) {
  study::TaggedPassage p = samplePassage();
  for (const uint16_t raw : {5, 5, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12}) p.tags.push_back(study::toTagId(raw));
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  const auto& tags = doc.passages()[0].tags;
  EXPECT_LE(tags.size(), study::PassageDoc::MAX_TAGS_PER_PASSAGE);
  EXPECT_EQ(std::count(tags.begin(), tags.end(), study::toTagId(5)), 1);
}

TEST(PassageDocValidation, RefusesAPassageWithNoTags) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  study::PassageDoc doc;
  EXPECT_FALSE(doc.add(p)) << "a highlight exists only to carry tags; all 63 of the user's do";
}

TEST(PassageDocRemove, RemovingTheLastTagKeepsThePassage) {
  study::TaggedPassage p = samplePassage();  // tags {3, 17}
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));

  doc.removeTagEverywhere(study::toTagId(3));
  ASSERT_EQ(doc.passages().size(), 1u);
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::toTagId(17)}));

  doc.removeTagEverywhere(study::toTagId(17));
  ASSERT_EQ(doc.passages().size(), 1u)
      << "a palette edit must never destroy a passage: TagFilterActivity deletes a tag on a long-press";
  EXPECT_EQ(doc.untaggedCount(), 1u);
}

TEST(PassageDocSetTags, LeavesThePassageUntouchedWhenTheNewListIsEmpty) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));
  EXPECT_FALSE(doc.setTags(0, {}));
  ASSERT_EQ(doc.passages().size(), 1u) << "a refused setTags must not consume the passage";
  EXPECT_EQ(doc.passages()[0].tags.size(), 2u);
}

TEST(PassageDocSetTags, DoesNotReorderTheDocument) {
  study::PassageDoc doc;
  study::TaggedPassage first = samplePassage();
  study::TaggedPassage second = samplePassage();
  second.reference = "Salmos 119:146";
  ASSERT_TRUE(doc.add(first));
  ASSERT_TRUE(doc.add(second));

  ASSERT_TRUE(doc.setTags(0, {study::toTagId(9)}));
  EXPECT_EQ(doc.passages()[0].reference, "Salmos 119:145")
      << "the UI and every stored index address passages positionally";
}

TEST(PassageDocBudget, RefusesAnAddThatWouldExceedTheWriteBudget) {
  study::PassageDoc doc;
  size_t added = 0;
  while (doc.add(samplePassage())) ++added;
  ASSERT_GT(added, 0u);
  EXPECT_LE(doc.measureBytes(), study::PassageDoc::SAVE_BYTE_BUDGET);
}

TEST(PassageDocLoad, ReportsFailureRatherThanTruncatingAnOversizeDocument) {
  // A file written by a build with a larger budget, or by a format bump that
  // added a field, must not load as a silently shortened document -- that is the
  // silent-truncation failure the whole budget discipline exists to prevent.
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION;
  const auto rows = json["p"].to<JsonArray>();
  for (size_t i = 0; i < 2000; ++i) {
    const auto row = rows.add<JsonObject>();
    row["u"] = "v:19:119:145:0";
    row["e"] = "v:19:119:145:108";
    row["x"] = std::string(100, 'x');
    const auto tags = row["t"].to<JsonArray>();
    tags.add(3);
  }

  study::PassageDoc doc;
  EXPECT_FALSE(doc.fromJson(json.as<JsonVariantConst>()))
      << "an oversize document is a load failure the caller must refuse to save over";
}

}  // namespace
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cd test && cmake -S . -B build && cmake --build build --target PassageDocTest
```

Expected: FAIL — `StudyStore/PassageDoc.h: No such file or directory`.

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/PassageDoc.h`**

```cpp
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "StudyStore/TaggedPassage.h"

// One publication's tagged passages, with all format rules and no storage
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
  // Not persist::DEFAULT_SAVE_BUDGET: that figure exists to stay clear of
  // SDCardManager::readFile's 50,000-byte truncation, and this document is read
  // through lib/JsonParser instead, which has no such cap. The budget here
  // bounds a single write so a card-full or an absurd store is refused rather
  // than half-written; it is not a truncation guard.
  static constexpr size_t SAVE_BYTE_BUDGET = 200000;
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

  // Drops `id` from every passage in THIS document. Passages left with no tags
  // are KEPT and reported by untaggedCount(), never deleted: the palette is
  // global and TagFilterActivity already deletes a tag on a long-press, so a
  // delete that removed passages would let one tidy-up gesture destroy work
  // across every publication. Palette deletion is retire-only; see TagPalette.
  void removeTagEverywhere(TagId id);

  // Passages currently carrying no tags, awaiting re-tagging.
  size_t untaggedCount() const;

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

- [ ] **Step 5: Write `lib/StudyStore/StudyStore/PassageDoc.cpp`**

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

#include <Utf8.h>

#include <algorithm>

namespace study {

bool PassageDoc::add(TaggedPassage passage) {
  if (passage.tags.empty()) return false;
  // utf8SafeSummary, never resize(): every one of these strings is Spanish and
  // a raw byte cut can land after a lead byte, producing an invalid sequence
  // that ArduinoJson will then serialise. HighlightDoc::addHighlight uses the
  // same helper for the same reason.
  passage.snippet = utf8SafeSummary(std::move(passage.snippet), MAX_SNIPPET_BYTES);
  passage.reference = utf8SafeSummary(std::move(passage.reference), MAX_REFERENCE_BYTES);

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

// Replaces entry `index`'s tags IN PLACE. Returns false without touching the
// entry when `index` is out of range or the new list is empty -- matching
// HighlightDoc::setTags, whose contract is explicitly "the entry is untouched in
// that case". An earlier draft erased first and re-added, which destroyed the
// passage whenever the re-add was refused.
bool PassageDoc::setTags(const size_t index, std::vector<TagId> tags) {
  if (index >= passages_.size()) return false;

  std::vector<TagId> normalised;
  normalised.reserve(std::min(tags.size(), MAX_TAGS_PER_PASSAGE));
  for (const TagId id : tags) {
    if (normalised.size() >= MAX_TAGS_PER_PASSAGE) break;
    if (std::find(normalised.begin(), normalised.end(), id) == normalised.end()) normalised.push_back(id);
  }
  if (normalised.empty()) return false;

  passages_[index].tags = std::move(normalised);
  return true;
}

void PassageDoc::removeTagEverywhere(const TagId id) {
  for (auto& p : passages_) {
    p.tags.erase(std::remove(p.tags.begin(), p.tags.end(), id), p.tags.end());
  }
}

size_t PassageDoc::untaggedCount() const {
  size_t n = 0;
  for (const auto& p : passages_) {
    if (p.tags.empty()) ++n;
  }
  return n;
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
      if (id > 0 && id <= UINT16_MAX) p.tags.push_back(toTagId(static_cast<uint16_t>(id)));
    }
    // Load path: normalise, but NEVER drop for budget. An earlier draft funnelled
    // this through add(), so a file at or over budget silently lost its tail,
    // still reported success, and the next save made the loss permanent -- the
    // spec's headline failure mode, one layer up. A document that will not fit
    // is a load FAILURE the caller must refuse to save over, not a truncation.
    if (p.tags.empty()) continue;
    p.snippet = utf8SafeSummary(std::move(p.snippet), MAX_SNIPPET_BYTES);
    p.reference = utf8SafeSummary(std::move(p.reference), MAX_REFERENCE_BYTES);
    passages_.push_back(std::move(p));
  }
  return measureBytes() <= SAVE_BYTE_BUDGET;
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
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PassageDoc.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
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

Expected: `[  PASSED  ] 11 tests.`

- [ ] **Step 8: Commit**

```bash
./bin/clang-format-fix -g
git add lib/StudyStore/StudyStore/TaggedPassage.h lib/StudyStore/StudyStore/PassageDoc.h lib/StudyStore/StudyStore/PassageDoc.cpp test/passage_doc test/CMakeLists.txt
git commit -m "feat: add the tagged passage record and its budgeted document"
```

---

## Task 9: The migration planner

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
- Create: `lib/StudyStore/StudyStore/MigrationPlanner.h`, `lib/StudyStore/StudyStore/MigrationPlanner.cpp`
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
  in.unitText = [](void*, const study::Unit&) { return std::string("Te he llamado con todo el corazon"); };
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

- [ ] **Step 3: Write `lib/StudyStore/StudyStore/MigrationPlanner.h`**

```cpp
#pragma once

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
  // Visible text of a unit, for the fingerprint -- a context pointer and a plain
  // function pointer, not std::function: CLAUDE.md prohibits std::function in
  // library code (~2-4 KB per signature plus a heap-allocated closure) and the
  // pair costs nothing here.
  void* unitTextCtx = nullptr;
  std::string (*unitText)(void* ctx, const Unit&) = nullptr;
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

- [ ] **Step 4: Write `lib/StudyStore/StudyStore/MigrationPlanner.cpp`**

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

  if (in.unitText) p.fingerprint = fingerprintOf(in.unitText(in.unitTextCtx, p.start));

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
  ${REPO_ROOT}/lib/StudyStore/StudyStore/MigrationPlanner.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitAnchors.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
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
git add lib/StudyStore/StudyStore/MigrationPlanner.h lib/StudyStore/StudyStore/MigrationPlanner.cpp test/migration_planner test/CMakeLists.txt
git commit -m "feat: plan a legacy highlight's migration to a unit address"
```

---

---

## Task 10: The dry-run gate

**Moved ahead of every task that depends on it.** The first draft placed this
after the migration and the rewire, which is not a gate — it is a post-mortem.

It is also reframed. The first draft claimed 63/63 agreement "proved" the
migration. It does not: the `ref` strings were written by the device using
`VerseAnchors` against the same offsets this re-resolves with `VerseAnchors`, so
agreement mostly demonstrates that a host build reproduces a device build on
identical bytes. That is worth having as a **regression gate** — it would catch a
broken scanner, a changed counter, or an entity-handling slip — and it is worth
nothing as proof of the write paths, the fingerprint, the book assignment or the
tag-id allocation. Those are covered by Tasks 4, 6, 8, 9 and 16.

**Files:**
- Create: `scripts/migration_dryrun.py`, `tools/migration_dryrun/main.cpp`, `tools/migration_dryrun/CMakeLists.txt`

Nothing here is committed with data in it. The markup and the passage text are
the user's copy of a copyrighted translation; the tool takes paths on the command
line and the repo stores neither.

- [ ] **Step 1: Pull the EPUB off the card, not from the CDN.** Project memory
      records that the user's on-card NWT is a TOC-edited repack, so a desktop
      unzip of the published download is a different file. The gate must run
      against what the device will actually read.

```bash
curl -s "http://<device-ip>/download?path=/books/<nwt file>.epub" -o /tmp/nwt-from-card.epub
mkdir -p /tmp/nwt-card && (cd /tmp/nwt-card && unzip -q /tmp/nwt-from-card.epub)
```

- [ ] **Step 2: Write `scripts/migration_dryrun.py`** — reads the OPF to build the
      spine, reads a legacy highlights JSON, resolves each `spineIndex` to its
      document path, and emits TSV rows of
      `spineIndex, start, end, ref, documentPath, bookNumber`. The book number
      comes from `biblebooknav.xhtml` plus the chapter-nav pages it points at —
      see Task 11's note on why the nav page alone is not enough.

- [ ] **Step 3: Write `tools/migration_dryrun/main.cpp`** — links the real
      `MigrationPlanner`, `UnitAnchors`, `UnitText`, `VerseAnchors`,
      `ParagraphAnchors` and expat; reads the TSV; prints per-passage lines and a
      summary:

```
 total=63  verse=63  paragraph=0  document-offset=0  ref-mismatch=0  dropped=0
```

  Exit non-zero when `ref-mismatch > 0` **or** when `verse < total`, so a
  regression that silently degrades addressing fails the gate rather than
  reporting success.

- [ ] **Step 4: Run it. Expected: `total=63 verse=63 ref-mismatch=0`.** Any other
      result stops the phase.

- [ ] **Step 5: Commit the tool, not the data**

```bash
git status --short   # confirm no .epub, .xhtml, .tsv or highlights JSON is staged
git add scripts/migration_dryrun.py tools/migration_dryrun
git commit -m "test: gate the migration on a repeatable offline dry run"
```

---

## Task 11: The unit index — a cache, and only a cache

**Files:**
- Create: `lib/StudyStore/StudyStore/UnitIndexFormat.{h,cpp}`, `test/unit_index_format/`
- Create: `src/study/UnitIndexCache.{h,cpp}`
- Modify: `test/CMakeLists.txt`

### Two corrections from review

**The per-document content CRC cost exactly what it saved.** The first draft
validated a cached document by comparing a CRC32 of its current visible
codepoints — which requires SD read, inflate and a full expat walk, i.e. the
entire rebuild minus a few `push_back`s. A cache whose validation costs as much
as a miss is not a cache.

**And the cheap checks were unreachable.** `HalFile` exposes no modification
time, `CLAUDE.md` forbids reaching past it into `FsFile`, and no
`dateTimeCallback` is registered anywhere in the repo, so FAT timestamps on
device-written files are a constant. `sourceMtime` is gone.

So the index validates on **source file size alone**, and a same-size content
change is deliberately not detected here. That is not a hole, because it is not
this layer's job: **the per-passage fingerprint is what catches changed text**,
at paint time, where the consequence is visible and recoverable. Layering the
same check twice, once expensively, bought nothing.

That also makes the file a pure cache, which resolves the atomicity question the
first draft got wrong: **losing it costs nothing**, so it needs corruption
*detection*, not crash *atomicity*. A CRC over the header and table, checked on
open, and the whole file is rebuilt on mismatch.

### On-disk layout — `/.berean/units/<pubkey>.bin`

```
Header (32 bytes, little-endian)
  magic          uint32   'B','U','I','1'
  formatVersion  uint16   1
  documentCount  uint16
  sourceSize     uint32   EPUB file size at build time
  tableCrc       uint32   CRC32 over the document table
  tableOffset    uint32
  bookMapOffset  uint32   spine index -> canonical book, built once
  reserved       uint32

Document table entry (12 bytes each, indexed by spine index)
  dataOffset     uint32   0 when this document has not been indexed yet
  anchorCount    uint16
  kind           uint8
  book           uint8
  reserved       uint32

Anchor record (8 bytes each)
  offset         uint32
  major          uint16
  minor          uint16
```

Every multi-byte field moves through `memcpy`. The S3 tolerates unaligned loads
where the C3 faults, but `CLAUDE.md`'s rule is unconditional and this is shared
code.

- [ ] **Step 1: Host-test the format.** One test each:

```cpp
TEST(UnitIndexFormat, RoundTripsAHeaderThroughBytes);
TEST(UnitIndexFormat, RoundTripsADocumentEntryWithItsAnchors);
TEST(UnitIndexFormat, RejectsAWrongMagic);
TEST(UnitIndexFormat, RejectsAFutureFormatVersion);
TEST(UnitIndexFormat, ReportsADocumentWithDataOffsetZeroAsNotYetIndexed);
TEST(UnitIndexFormat, DetectsAChangedSourceSizeAsStale);
TEST(UnitIndexFormat, DetectsACorruptedTableViaItsCrc);
TEST(UnitIndexFormat, ReadsEveryFieldThroughMemcpyOnAnUnalignedBuffer);
```

  The last builds the bytes, copies them into a `std::vector<uint8_t>` at offset
  1, parses from `buf.data() + 1`, and asserts every field. Run it under
  `-fsanitize=address,undefined`; a pointer cast trips UBSan there.

- [ ] **Step 2: Run it and watch it fail.**

- [ ] **Step 3: Implement `UnitIndexFormat.{h,cpp}`** following the layout above.

- [ ] **Step 4: Build the spine-to-book map — and budget for what it really costs.**

  Review corrected the first draft here. `BibleNav::Scanner` on
  `biblebooknav.xhtml` returns 66 hrefs to **chapter-nav pages**, not to
  chapters. Mapping an arbitrary content spine index to a book — the user's marks
  sit at spine 198–1322 — needs each of those nav pages opened and its chapter
  list resolved, exactly as `BibleNavigationActivity::loadChapters` does, plus
  the five single-chapter books (Obadiah, Philemon, 2 John, 3 John, Jude) that
  have no nav page and point straight at a spine item.

  That is ~66 extra document reads. It is **not** cheap, so it does not happen in
  `openPublication`. It is built once, on the first request for a book number,
  and persisted in the index file's `bookMapOffset` block so it never runs twice.
  Yield with `vTaskDelay(1)` between nav pages.

```cpp
// Canonical book for a spine index, or 0 outside the Bible text. Building this
// opens all 66 chapter-nav pages the book-nav page points at; it is persisted
// in the index so that cost is paid once per publication, never at open.
uint8_t bookFor(uint16_t spineIndex);
```

- [ ] **Step 5: Write `src/study/UnitIndexCache.{h,cpp}`.** `unitsFor(spineIndex)`:
      return the in-RAM copy on a hit; else read the entry and load its anchors
      if `dataOffset != 0`; else read the document through `Epub`, run
      `study::scanUnits`, stamp `book` for a Verse document, append the anchors,
      rewrite the entry, update `tableCrc`. One document is held in RAM at a time
      — measured p95 is 111 anchors, max 413, so at most ~3.3 KB.

      **`vTaskDelay(1)` after every document**, not every publication. The
      watchdog is `CONFIG_ESP_TASK_WDT_PANIC=y` at 5 s and a panic during a
      boot-time pass reboots into the same pass.

- [ ] **Step 6: Build and commit**

```bash
cd test && cmake --build build && ctest --test-dir build --output-on-failure
cd .. && /Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add lib/StudyStore/StudyStore/UnitIndexFormat.h lib/StudyStore/StudyStore/UnitIndexFormat.cpp src/study/UnitIndexCache.h src/study/UnitIndexCache.cpp test/unit_index_format test/CMakeLists.txt
git commit -m "feat: cache a publication's units lazily, one document at a time"
```

---

## Task 12: The storage shells and the `StudyStore` façade

**Files:**
- Create: `src/study/PassageFile.{h,cpp}`, `src/study/TagPaletteFile.{h,cpp}`, `src/study/StudyStore.{h,cpp}`

### Four invariants review put here

**1. The save latch is carried forward.** `EpubReaderActivity.h:60-62` sets
`highlightsSaveDisabled` on `LoadResult::Failed` — "the file may still hold the
user's data, so saving over it for the rest of the session would risk destroying
it." The first draft kept the four-state `LoadResult` and then dropped the latch,
so an unparseable passages file loaded as empty and the next tag overwrote it.
`StudyStore` owns a `saveDisabled_` flag set on `Failed`, and `save()` is a no-op
that returns false while it is set.

**2. Passages are addressed by index with explicit staleness checks**, not by
pointer or reference. Today's code does this deliberately
(`if (docIndex >= highlights().size()) return; // stale index`) because
`TagPickerActivity` is entered and exited while the document mutates underneath.
The first draft's `removePassage(const TaggedPassage&)` had no stated identity
rule and `passagesInDocument` handed out pointers into a vector that `add()`
reallocates.

**3. Saves stay synchronous, with rollback on failure.** Today every edit saves
immediately and reverts the in-memory change if the save fails
(`HighlightsActivity.cpp:291-303`). The first draft replaced that with a dirty
flag and a debounce, which loses an edit on Power-off, has nothing to roll back
to on `WriteFailed`, and shows the user a tag that was never persisted. A tag
edit is a handful of times per reading session, not per page turn — the SD cost
is not worth the loss window.

**4. The PSRAM gate is preserved.** Highlight loading is gated on
`BOARD_HAS_PSRAM` in `loadBook()` because a resident document plus two live
`JsonDocument`s is a real risk against a non-PSRAM board's ~50 KB free heap. The
X4 Pro has PSRAM, but the gate stays rather than silently regressing a board the
fork may still build.

- [ ] **Step 1: Write `src/study/PassageFile.h`**

```cpp
#pragma once

#include <string>

#include "StudyStore/PassageDoc.h"

// Moves bytes between PassageDoc and /.berean/passages/<pubkey>.json.
//
// Reads through lib/JsonParser, NOT Storage.readFile: that caps at 50,000 bytes
// and returns a silently truncated string, which for this file would mean the
// user's older passages simply cease to exist on the next boot.
namespace PassageFile {

std::string path(const std::string& pubKey);

enum class LoadResult : uint8_t { Loaded, Empty, RecoveredFromTemp, Failed };

// Failed means the bytes could not be read, parsed or validated AND THE FILE MAY
// STILL HOLD THE USER'S DATA. The caller must latch saving off for the session.
LoadResult load(const std::string& pubKey, study::PassageDoc& doc);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

// Measures before writing and refuses over budget. writeDocToFileAtomic only --
// never writeDocToFile, which is the non-atomic variant.
SaveResult save(const std::string& pubKey, const study::PassageDoc& doc);

}  // namespace PassageFile
```

- [ ] **Step 2: Implement it**, reusing `highlightLoadAction` from
      `src/util/HighlightFileAction.h` for the `.tmp` promotion state machine. It
      is already host-tested and the states are identical. Do not reimplement it.

- [ ] **Step 3: Write and implement `TagPaletteFile.{h,cpp}`** — same shape, one
      file at `/.berean/tags.json`. This one is small enough for the ordinary
      `PersistableStore` read path.

- [ ] **Step 4: Write `src/study/StudyStore.h`**

```cpp
#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/TagPalette.h"
#include "study/UnitIndexCache.h"

class StudyStore {
 public:
  static StudyStore& getInstance();

  bool openPublication(Epub& epub);
  void closePublication();

  const study::TagPalette& palette() const { return palette_; }

  // Adds a tag name, or returns the existing id. Never allocates behind a
  // failed save: the palette is written before the id is handed out.
  std::optional<study::TagId> addTagName(const std::string& name);

  // RETIRES the tag. No passage is ever removed by a palette operation -- the
  // palette is global and a long-press in TagFilterActivity reaches it, so a
  // destructive delete would let one gesture wipe work across every publication.
  bool retireTag(study::TagId id);

  // Passages in this spine document, resolved to current offsets for painting.
  // `index` addresses passages_ and must be re-checked against size() after any
  // mutation -- sub-activities mutate the document while these are held.
  struct PaintedPassage {
    size_t index;
    uint32_t startOffset;
    uint32_t endOffset;
    bool wholeUnit;  // offsets did not fit; paint the unit, not the span
  };
  std::vector<PaintedPassage> passagesInDocument(uint16_t spineIndex);

  const std::vector<study::TaggedPassage>& passages() const { return passages_.passages(); }

  bool addPassage(uint16_t spineIndex, uint32_t startOffset, uint32_t endOffset, const std::string& snippet,
                  const std::string& reference, std::vector<study::TagId> tags);
  bool removePassage(size_t index);
  bool setPassageTags(size_t index, std::vector<study::TagId> tags);

  bool saveDisabled() const { return saveDisabled_; }

 private:
  StudyStore() = default;
  bool save();

  study::TagPalette palette_;
  study::PassageDoc passages_;
  std::string pubKey_;
  bool saveDisabled_ = false;
  std::unique_ptr<UnitIndexCache> units_;
};

#define STUDY StudyStore::getInstance()
```

- [ ] **Step 5: Implement `StudyStore.cpp`.** The degradation rule, exactly as the
      spec's table states it:

  | Fingerprint | Offsets | Behaviour |
  |---|---|---|
  | matches | fit | paint the span |
  | matches | do not fit | `wholeUnit = true`; paint the unit |
  | **differs** | — | **return nothing**; the mark is not painted |

  Every mutator saves synchronously and reverts its in-memory change if the save
  fails, returning false so the caller can tell the user. `passagesInDocument`
  never writes and never triggers an index build beyond the one document it was
  asked about.

- [ ] **Step 6: Build and commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add src/study
git commit -m "feat: add the study store over atomic, streamed files"
```

---

## Task 13: Recovering a book path from a legacy store filename

The defect that would have shipped a silently useless migration. Task 12 of the
first draft asserted "the source filename is a flattened book path, so the book
path is recoverable." `pathflatten::toCacheName` erases the first character, maps
both `/` and `\` to `_` — indistinguishable from a literal underscore — and drops
everything from the last dot. Three lossy transforms, documented in its own
header.

**Files:**
- Create: `src/study/BookPathIndex.{h,cpp}`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <optional>
#include <string>

// Recovers the book path a /.crosspoint/highlights/<name>.json file was named
// after. toCacheName is lossy in three ways, so this cannot be inverted -- it is
// resolved by walking the card and re-flattening each candidate until one
// matches, which is exact by construction.
namespace BookPathIndex {

// The EPUB whose flattened name equals `flattenedStem`, or nullopt when no file
// on the card produces it. Searches the configured download folder first, then
// the card root, at a bounded depth.
std::optional<std::string> resolve(const std::string& flattenedStem);

}  // namespace BookPathIndex
```

- [ ] **Step 2: Implement it.** Enumerate `*.epub` under `SETTINGS.downloadFolder`
      and the root (depth 2 is enough for how this card is organised), compute
      `pathflatten::toCacheName` for each, and compare. Ambiguity — two files
      flattening to the same stem — resolves to **no match**, not a guess: a
      wrong EPUB would address every passage into the wrong book.

- [ ] **Step 3: Build and commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add src/study/BookPathIndex.h src/study/BookPathIndex.cpp
git commit -m "feat: recover a book path from a flattened store filename"
```

---

## Task 14: Rewire the five activities — before the migration runs

**Reordered.** The first draft migrated at Task 12 and rewired at Task 13, so any
build flashed between them read through `HighlightFile` after the sources had
been consumed — reporting `Empty`, which means "safe to save over", so one new
highlight would write a fresh file over the migrated-away data.

| File | What changes |
|---|---|
| `EpubReaderActivity.{h,cpp}` | `STUDY.openPublication(epub)`; paint pass asks `STUDY.passagesInDocument(spine)`; keeps the `BOARD_HAS_PSRAM` gate and the save latch |
| `PassageSelectActivity.{h,cpp}` | Emits a document offset range into `STUDY.addPassage` |
| `TagPickerActivity.{h,cpp}` | Reads `STUDY.palette()`; selection becomes `std::vector<study::TagId>` |
| `HighlightsActivity.{h,cpp}` | Lists `STUDY.passages()` for the open publication |
| `TagFilterActivity.{h,cpp}` | Long-press now calls `STUDY.retireTag` — **retire, not delete** |
| `ActivityResult.h` | Its tag payload carries ids, not indices into `HighlightDoc::tags()` |

**Every screen keeps its current layout.** If a row moves, a list reorders or a
count changes, that is a regression in this task.

- [ ] **Step 1: `EpubReaderActivity`** — the lifecycle owner. Build.
- [ ] **Step 2: `PassageSelectActivity`.** Note its stored `end` is *the last
      word's start offset + 1* (`PassageSelectActivity.cpp:377-378`), a semantic
      that holds today only because `VisibleRange::contains` is half-open on word
      starts. `StudyStore::addPassage` must document and preserve it, or every
      mark shifts by one word — invisible to every host test. Build.
- [ ] **Step 3: `TagPickerActivity` and `ActivityResult.h`.** The index-to-id
      boundary. `study::TagId` is a scoped enum precisely so a mis-wiring here is
      a compile error rather than a wrong tag on a real passage. Build.
- [ ] **Step 4: `HighlightsActivity` and `TagFilterActivity`.** Confirm the
      long-press path retires and cannot remove a passage. Build.
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

  `pio check` is not optional — skipping it in Phase 0 put two `duplicateBreak`
  failures into CI.

---

## Task 15: The migration runner, its ledger and its report

**Files:**
- Create: `src/study/MigrationRunner.{h,cpp}`
- Modify: `src/main.cpp`, `src/network/CrossPointWebServer.cpp`, `src/activities/meetings/MeetingDownloadActivity.cpp`

### The rename is gone

The first draft renamed each source to `<name>.json.migrated` as its commit
point. That is resumable, and it **voids the rollback the spec calls
non-negotiable**: a Phase 0 build resolves
`pathflatten::toCacheName(bookPath) + ".json"`, finds nothing, reports `Empty` —
which means "safe to save over" — and the user's data is one highlight away from
being overwritten by the older firmware.

Progress lives in `/.berean/migration-ledger.json` instead: a list of source
filenames already migrated, with the byte size and passage count each had. The
old directory is not touched at all. Resumability is unchanged; the rollback
survives.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <cstdint>

namespace MigrationRunner {

struct Summary {
  uint16_t sourceFiles = 0;
  uint16_t highlightsRead = 0;
  uint16_t passagesWritten = 0;   // counted from the store AFTER a successful save
  uint16_t addressedVerse = 0;
  uint16_t addressedParagraph = 0;
  uint16_t addressedDocumentOffset = 0;
  uint16_t referenceMismatches = 0;
  uint16_t pendingUpgrade = 0;
  uint16_t dropped = 0;
  uint16_t tagsAdopted = 0;
};

bool runIfPending(Summary& summary);
bool pending();

inline constexpr const char* REPORT_PATH = "/.berean/migration-report.json";
inline constexpr const char* LEDGER_PATH = "/.berean/migration-ledger.json";

}  // namespace MigrationRunner
```

  The `addressed*` breakdown is the fix for the defect that made the first
  draft's checklist worthless: a migration that resolved nothing still reported
  `written=63`. `addressedVerse` is the number that says the phase worked.

- [ ] **Step 2: Implement the per-file pass**

  1. Load with the existing `HighlightFile::load`. On `Failed`, record it and
     **skip without ledgering** — the data may still be there.
  2. `BookPathIndex::resolve(stem)` for the book path. If it resolves, open the
     EPUB for the Bible flag and the unit index; if not, `sourceAvailable =
     false` and every passage migrates `pendingUpgrade`.
  3. `study::adoptTagNames(palette, doc.tags())` **first**, so the user's two
     defined-but-unused tags survive.
  4. `study::planMigration` per highlight; add each result to the store.
  5. Save the passages file and the palette. **Count `passagesWritten` from the
     store after the save returns `Ok`**, never from the planner's outcomes — the
     planner can say `Resolved` for a passage that `PassageDoc::add` then
     refuses.
  6. Append to the ledger only when every save returned `Ok`.

  `vTaskDelay(1)` between documents, not between source files: there is one
  source file, and 50 documents at ~30 ms is comfortable while 200 would not be.

- [ ] **Step 3: Write the report.** `/.berean/migration-report.json`:

```json
{
  "v": 1,
  "summary": {
    "sourceFiles": 1, "highlightsRead": 63, "passagesWritten": 63,
    "addressedVerse": 63, "addressedParagraph": 0, "addressedDocumentOffset": 0,
    "referenceMismatches": 0, "pendingUpgrade": 0, "dropped": 0, "tagsAdopted": 48
  },
  "files": [
    { "source": "…nwt-S… .json", "pubKey": "bible", "bookPath": "/books/…epub",
      "read": 63, "written": 63, "verse": 63, "mismatches": 0, "pending": 0, "drops": [] }
  ]
}
```

  Budgeted like every other store: over budget, write the summary plus the first
  N file rows and a `"truncated": true` flag. A report that refuses to save would
  be an absurd way to fail a successful migration.

- [ ] **Step 4: Call it at boot**, in `src/main.cpp` after storage is up and before
      the first activity:

```cpp
MigrationRunner::Summary migration;
if (MigrationRunner::pending()) {
  LOG_INF("MIGRATE", "Migrating study data...");
  if (!MigrationRunner::runIfPending(migration)) {
    LOG_ERR("MIGRATE", "Migration incomplete; legacy store untouched");
  }
  LOG_INF("MIGRATE", "read=%u written=%u verse=%u para=%u docoff=%u mismatch=%u pending=%u dropped=%u tags=%u",
          migration.highlightsRead, migration.passagesWritten, migration.addressedVerse,
          migration.addressedParagraph, migration.addressedDocumentOffset, migration.referenceMismatches,
          migration.pendingUpgrade, migration.dropped, migration.tagsAdopted);
}
```

- [ ] **Step 5: Serve the report.** Beside the other routes near
      `CrossPointWebServer.cpp:148`:

```cpp
server->on("/migration", HTTP_GET, [this] { handleMigrationReport(); });
```

  Streams `REPORT_PATH` as `application/json`, or 404 with
  `{"status":"no migration has run"}`.

- [ ] **Step 6: Write the pubkey registry when a publication is downloaded.**

  `PubKey.h` specifies `/.berean/pubkeys.json`, and the first draft had nothing
  writing it — so the two weekly meeting publications, the ones the user actually
  tags besides the Bible, would get an unstable `local-<flattened path>` key now
  and a different key when Phase 3 lands, orphaning every tag on them.

  `MeetingDownloadActivity` already knows the symbol, issue and language at
  download time. On a completed download, record
  `{ "<book path>": { "s": "w", "i": "202607", "l": "S" } }`. Small, atomic,
  budgeted.

- [ ] **Step 7: Build and commit**

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
./bin/clang-format-fix -g
git add src/study/MigrationRunner.h src/study/MigrationRunner.cpp src/main.cpp src/network/CrossPointWebServer.cpp src/activities/meetings/MeetingDownloadActivity.cpp
git commit -m "feat: migrate the legacy highlight store, leaving it intact"
```

---

## Task 16: Device verification

Human-tester scope. Do this **before** telling the user the phase is ready, and
report the numbers rather than a claim.

- [ ] **Step 0: Back up the card — before any of this phase is flashed.**

```bash
curl -s "http://<device-ip>/download?path=/.crosspoint/highlights/<file>.json" -o pre-migration.json
python3 -c "import json;d=json.load(open('pre-migration.json'));print(len(d.get('t',[])),'tags',len(d.get('h',[])),'passages')"
```

  Expected: `48 tags 63 passages`.

- [ ] **Step 1: Side-load a dev build.** OTA installs the release build, which has
      `LOG_LEVEL=0` and no serial output — useless for a first run.

```bash
/Volumes/stein/.platformio/penv/bin/pio run -e x4pro
curl -H "Expect:" -F "file=@.pio/build/x4pro/firmware.bin" "http://<device-ip>/upload?path=/"
```

  Then *Settings → SD firmware update*. The `Expect:` suppression is required —
  the ESP32 web server never answers `100-continue` and the transfer hangs.

- [ ] **Step 2: Watch the migration**

```bash
cat /dev/cu.usbmodem31101 > serial.log &
```

  Expected: `read=63 written=63 verse=63 para=0 docoff=0 mismatch=0 pending=0 dropped=0 tags=48`.

  **`verse=63` is the acceptance criterion, not `written=63`.** A run reporting
  `written=63 docoff=63` means the migration found no EPUB and addressed nothing
  — and would paint identically, which is exactly how the first draft's checklist
  could have passed on a failure.

  Never pass a baud rate to this transport; the native USB-JTAG bridge has no
  line settings.

- [ ] **Step 3: Read the report**

```bash
curl -s "http://<device-ip>/migration" | python3 -m json.tool
```

- [ ] **Step 4: The checks that would catch data loss**

  | Check | Expected |
  |---|---|
  | `/.crosspoint/highlights/<file>.json` still present, unrenamed | yes — the rollback |
  | `summary.addressedVerse` | **63** |
  | Tag list | **48** names, including `igualdad` and `transformación` |
  | Passage list for the open book | **63** |
  | A marked verse paints the same span | yes, not the whole verse |
  | Long-press a tag in the filter row | tag disappears from pickers; **passage count unchanged** |
  | Power-cycle | migration does not run again |

- [ ] **Step 5: Report the numbers to the user** — the report JSON and this table,
      not a summary claim.

---

## Self-review

**Spec coverage.** Unified `Unit` → Task 1; `data-pid` scanner → Task 2; unit
index → Task 11; tag store → Tasks 6, 8, 12; migration → Tasks 9, 13, 15. The
acceptance criterion — "`migration-report.json` served over the web server; old
store retained" — is Task 15 Steps 3–5 and Task 16 Step 4.

**Deviations from the spec, all argued above:**

1. `Unit` gains a `book` field; without it the Bible's language-free pubkey does
   not work and Genesis 1:1 collides with Matthew 1:1.
2. The passages file is streamed rather than sharded.
3. Migration defers address resolution when the EPUB cannot be found, and the
   report distinguishes deferred from resolved.
4. Progress is a ledger, not a rename, so the spec's rollback promise holds.
5. Palette deletion is retire-only; no palette operation removes a passage.
6. **The reverse index moves to Phase 3.** At one publication and 63 passages a
   full scan is milliseconds, while the index costs a rebuild on every tag edit
   (its entries are positional, so any edit invalidates it) and still opens the
   passages files to materialise results. The spec's own justification is "at 100
   publications" — which is what Phase 3 creates.

**Open items carried forward, not dropped:**

- **Whether `bible` is safe across translations is unresolved.** A non-NWT Bible
  with a different canon order shifts every book number, and Psalm superscription
  numbering differs between translations by one verse across ~100 psalms — which
  the fingerprint would turn into "paint nothing" for exactly the cross-language
  comparison the language-free key exists to serve. Phase 1 ships with `bible`
  gated on a 66-entry book-nav that matches the canonical order, and anything
  else getting a per-publication key. **Revisit before a second translation is
  ever loaded.**
- The fingerprint covers the start and end units only; a change strictly inside a
  multi-unit span is not detected. Spans are 47–268 codepoints against verses of
  similar size, so this is a minority of passages, but it is real.
- "Paint nothing" has no UI surface until Phase 2, so a changed passage silently
  stops painting while still appearing in the list. Phase 2 owns *el texto
  cambió*.
- Document-filename stability under a corrected reissue remains unverified; the
  `documentSpine` fallback stands on that uncertainty.
- `ReturnStack` capacity remains a Phase 2 decision.

---

## What comes after

Phase 2 (launcher shell, four sections, the new input model, two-tap selection,
`MappedInputManager` deleted) and Phase 3 (Buscar, and the reverse index that
earns its place there) each get their own plan, written once this one lands.
