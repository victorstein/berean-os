# Design: a name rank in TagPalette and its StudyStore consumers (btrial-1)

Bead `btrial-1`, task `t1` of run `berean-os-20261012-tag-chip-picker-2l3m`, branch
`feature/tag-order-rank`. Surface `data`, tier `heavy`.

- **Parent design (approved):** `docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`,
  D1, D2, §1 and §6. Where this spec is silent, the parent governs.
- **Research:** `docs/superpowers/research/2026-10-12-btrial-1-research.md`, cited below as
  "research §n" or "Fn".

Every behavioural decision is an assumption labelled **A1–A23**, collected in one section near the
end. Where the parent spec is open, this spec fixes the behaviour. Where it departs from the parent,
it says so under "Deviations from the batch spec".

## Problem

Every tag surface lists tags in allocation order:

- `TagPalette::activeIds()` returns "active tags in allocation order"
  (`lib/StudyStore/StudyStore/TagPalette.h:57-58`, `TagPalette.cpp:57-64`).
- `StudyStore::activeTags()` copies that order (`src/study/StudyStore.cpp:159-163`).
- `StudyStore::tagNamesFor()` joins a passage's names in stored order
  (`StudyStore.cpp:184-194`).

With the owner's ~48-tag palette, finding a tag means scanning the whole set. The passage-row
summary is cut to 24 bytes (`src/activities/reader/HighlightsActivity.cpp:90-95`), so a row shows
whichever tag happens to be stored first.

There is no display order anywhere in `lib/StudyStore` or `src/study` to read instead. The only fold
the repo has is `BibleSearch::foldAppend` (`lib/BibleSearch/BibleSearch/Fold.h:18-23`), and nothing
outside `lib/BibleSearch` and `TypedReference.cpp:140` calls it (research §1, §5).

## Goal

1. `TagPalette` caches one display order, with a rank for every entry, active and retired, and
   rebuilds it only when the set of names changes.
2. It exposes that order through the two signatures the UI tasks consume, exactly as the parent
   gives them (§1):
   - `std::vector<TagId> activeIdsByName() const`
   - `uint16_t nameRank(TagId id) const`
3. `StudyStore::activeTags()` returns display order.
4. `StudyStore::tagNamesFor()` joins a passage's tags in display order. The passage's stored order
   does not change.
5. The order is D2: skip one leading `¿` or `¡`, fold with `BibleSearch::foldAppend`, compare the
   keys bytewise, then break ties on the raw name bytes, then on the raw id.

## Non-goals

- **No file or version moves.** The palette file and `TagPalette::FORMAT_VERSION` (=1,
  `TagPalette.h:35`) stay as they are, and so do the passage file and its version. No store or
  cache version moves. `toJson` still writes `entries_` in allocation order (`TagPalette.cpp:70-75`).
- **Stored passage order is unchanged.** This task never reorders a passage's `tags`.
- **`activeIds()` is unchanged:** same order, same comment (`TagPalette.h:57-58`).
- **Nothing under `src/activities/` changes.** `TagChipView::buildEntries`
  (`src/activities/reader/TagChipView.cpp:16`) belongs to `t4` (`feature/tag-chip-grid-base`), and
  the picker's commit to `t5` (`feature/tag-picker-grid`).
- **The fold is untouched.** `Fold.h` and `Fold.cpp` do not change.
- **No search or matching changes.** `add` keeps exact byte matching (`TagPalette.h:41-46`,
  `TagPalette.cpp:15-19`).

## Modelled on

| Piece | Model |
|---|---|
| Fold into a fixed slot through one reused scratch | `src/activities/reader/TypedReference.cpp:138-150`, `:241-247` |
| Sort over flat byte keys, with a clean null-buffer failure | `lib/BibleSearch/BibleSearch/IndexBuilder.cpp:311-323` |
| Nothrow buffer, `LOG_ERR`, keep going | `lib/EpdFont/SdCardFont.cpp:1088-1098` |
| Linear id lookup with a sentinel result | `TagPalette::name()`, `TagPalette.cpp:42-47` |
| A pure helper pulled out of `StudyStore` so a host suite can test it | `src/study/StudyReuse.h:3-4`, tested by `test/parked_book/StudyReuseTest.cpp` (commit `36920945`) |
| OOM injection in a host test | `test/ditherers/FailingNothrowNew.{h,cpp}`, `CMakeLists.txt:16-19`, `DitherersTest.cpp:57-80` |
| A host target that needs Fold, Memory and the Logging stub | `test/bible_search_index/CMakeLists.txt:19-27` |
| Test names and failure messages | `test/tag_palette/TagPaletteTest.cpp` |

## Architecture

### `TagPalette` state

```cpp
 private:
  struct Entry {
    TagId id{};
    std::string name;
    bool active = true;
    uint16_t rank = 0;  // position in display order among all entries
  };

  void rebuildDisplayOrder();

  std::vector<Entry> entries_;
  std::vector<uint16_t> byName_;  // entry indices in display order
  uint16_t nextRaw_ = 1;  // 0 is UNLABELLED
```

The display order is cached in both directions (A1):

- `Entry::rank` answers "where does this id sit?" for `nameRank`.
- `byName_` answers "which entry sits at position *k*?" for `activeIdsByName`.

**Invariant I1.** Once a public mutator returns, `byName_.size() == entries_.size()`. `byName_` is a
permutation of `[0, n)`, and `entries_[byName_[k]].rank == k` for every `k`. Both the normal rebuild
and its OOM fallback establish it. No other code writes `rank` or `byName_`.

### Public API (signatures fixed by the parent §1)

```cpp
  // Active tags in allocation order, for a picker.           <- unchanged
  std::vector<TagId> activeIds() const;

  // Active tags in display order: case and accents ignored, ñ read as n, one leading ¿ or ¡
  // skipped, then the raw name, then the id.
  std::vector<TagId> activeIdsByName() const;

  // An id's position in display order among all entries, retired ones included, so a passage
  // that still carries a retired tag lists it in place. UINT16_MAX for UNLABELLED and unknown ids.
  uint16_t nameRank(TagId id) const;
```

- **`activeIdsByName()`** reserves `activeCount()`. It then walks `byName_` and pushes
  `entries_[i].id` for each active entry. That is O(n): no sort, and no allocation beyond the result
  (A13).
- **`nameRank(id)`** scans `entries_` as `name()` does (`TagPalette.cpp:42-47`). It returns
  `e.rank` for the matching entry, or `UINT16_MAX` if none matches. A lookup is an id compare plus
  a field read and never folds, as the parent's "Why a cache" requires. No entry ever carries
  `UNLABELLED` (A12), so it falls out as `UINT16_MAX` with no special case.

### Collation key (D2)

```cpp
constexpr size_t KEY_BYTES = TagPalette::MAX_TAG_NAME_BYTES;   // 24

struct SortKey {
  char key[KEY_BYTES];   // folded name, zero-padded
  uint16_t index;        // into entries_
};
```

To build an entry's key:

1. Take `std::string_view` over the name.
2. If it begins with the two bytes `C2 BF` (`¿`, U+00BF) or `C2 A1` (`¡`, U+00A1), drop those
   two bytes, and only those two (A5).
3. Fold the rest: `scratch.clear(); BibleSearch::foldAppend(scratch, view);`, as in
   `TypedReference.cpp:139-140`.
4. `memcpy` `min(scratch.size(), KEY_BYTES)` bytes into `key`. The `min` is a guard and never binds:
   a name is at most 24 bytes (`TagPalette.cpp:13`, `:92`), and a fold is never longer than its
   input (`Fold.h:21-22`, measured in research §3).

`makeUniqueNoThrow<SortKey[]>(n)` value-initialises (`lib/Memory/Memory.h:33`), so every slot
starts zeroed and needs no explicit padding (A4).

**Comparator** (a strict total order, A6):

1. `memcmp(a.key, b.key, KEY_BYTES)` if it is non-zero;
2. otherwise `entries_[a.index].name < entries_[b.index].name` if the names differ, which is
   unsigned bytewise per `std::char_traits<char>`. A probe confirmed `"a" < "\xC3\xA1"` and
   `"Amor" < "amor"` on host clang, where `char` is signed. On xtensa `char` is unsigned
   (`__CHAR_UNSIGNED__`);
3. otherwise `toRaw(id) <`.

### `rebuildDisplayOrder()`

```
n = entries_.size()
byName_.resize(n)
if n == 0: return                                    (A9: no allocation)
keys = makeUniqueNoThrow<SortKey[]>(n)
if !keys:
    LOG_ERR("TAGS", "OOM: %zu B for the tag display order; showing allocation order", n * sizeof(SortKey))
    for i in [0, n): entries_[i].rank = i; byName_[i] = i      (A10)
    return
scratch: std::string, reserve(KEY_BYTES)                       (A7)
for i in [0, n): keys[i].index = i; fold entries_[i].name into keys[i].key
std::sort(keys, keys + n, comparator)
for k in [0, n): byName_[k] = keys[k].index; entries_[keys[k].index].rank = k
```

`MODULE` is `"TAGS"`, as in `src/study/TagPaletteFile.cpp:12` (A11).

### When it runs (A8)

| Operation | Names change? | Rebuild? | Where |
|---|---|---|---|
| `add`, new name (append) | yes | **yes**, after `push_back` | `TagPalette.cpp:25` |
| `add`, name of a retired entry (revive) | no | no | `:15-19` |
| `add`, name of an active entry | no | no | `:15-19` |
| `add` refused (empty, too long, full, ids exhausted) | no | no | `:13`, `:21-22` |
| `retire` | no | no | `:29-33` |
| `fromJson`, accepted | yes | **yes**, before `return true` | `:105` |
| `fromJson`, rejected (non-object, version) | no, `entries_` untouched | no | `:79-81`, all before `clear()` at `:83` |
| copy / assignment | no | no; `rank` and `byName_` copy with `entries_` | `test/storage_io/TagPaletteFileTest.cpp:112` copies one |

`StudyStore::addTagName` rolls back a failed save with `retire` (`StudyStore.cpp:254-257`). The
entry stays with its rank, retired, and I1 still holds.

### `TagDisplayOrder.h`, a pure helper (A14)

This is a new header-only file, `lib/StudyStore/StudyStore/TagDisplayOrder.h`. It exists so the
ordering that `tagNamesFor` needs can be host-tested: `StudyStore.cpp` depends on Epub, Arduino and
SD and has no host target (research §1). That is the reason `src/study/StudyReuse.h:3-4` gives for
its own extraction.

```cpp
namespace study {

// A passage's tags in display order (TagPalette::nameRank, then raw id), for a list row. A copy,
// so stored order -- which the sleep screen reads for its tag pill -- never moves.
struct DisplayOrderedTags {
  std::array<TagId, PassageDoc::MAX_TAGS_PER_PASSAGE> ids{};
  size_t count = 0;
};

inline DisplayOrderedTags inDisplayOrder(const TagPalette& palette, const std::vector<TagId>& tags);

}  // namespace study
```

The body:

1. Take `count = min(tags.size(), MAX_TAGS_PER_PASSAGE)` (A15).
2. Fill a local `std::array<{uint16_t rank; TagId id;}, 8>`, calling `palette.nameRank` once per
   id.
3. `std::sort` the filled prefix by `(rank, toRaw(id))`.
4. Copy the ids out.

The local array is 8 × 4 B = 32 B and the result is 8 × 2 B + `size_t`, both well under the 256 B
stack rule.

### `StudyStore` consumers

- **`activeTags()`** (`StudyStore.cpp:159-163`) takes `ids = palette_.activeIdsByName()`, then
  `out.reserve(ids.size())`, then pushes `{id, palette_.name(id)}` for each id (A23). The
  `StudyStore.h:42-44` comment changes "allocation order" to "display order"; the rest stays.
- **`tagNamesFor()`** (`StudyStore.cpp:184-194`) gets `ordered = study::inDisplayOrder(palette_,
  passage.tags)`. It then joins `tagName(ordered.ids[i])` over `[0, ordered.count)`, skipping empty
  names and separating with `", "`, as today (`:188-191`).
  - Retired ids keep their rank and resolve (`StudyStore.h:59-61`).
  - UNLABELLED still gets `tr(STR_TAG_UNLABELLED)` (`:180`). It can only ever be a passage's sole
    tag (`TagPalette.h:29-30`, `PassageDoc.cpp:17-27`), so its position never matters.
  - The `StudyStore.h:59-60` comment gains "in display order".

### Library dependency and build

`TagPalette.cpp` gains `#include <BibleSearch/Fold.h>`, `<Logging.h>`, `<Memory.h>` and
`<cstring>`.

**Firmware.** The library has no manifest. LDF `chain` mode (the default, research F6) should make
`StudyStore` depend on `BibleSearch`, `Memory` and `Logging` (A22). `pio run` proves it.

**Host.** These are per-suite CMake changes only. The root `test/CMakeLists.txt` is untouched, since
`tag_palette` is already registered at `:115` (A20).

| Target | Sources added | Include dirs added | Other |
|---|---|---|---|
| `test/tag_palette` | `Fold.cpp`, `FailingNothrowNew.cpp`, `TagDisplayOrderTest.cpp` | `lib/BibleSearch`, `lib/Memory`, `test/stubs` | `-fno-builtin` (A21) |
| `test/migration_planner` | `Fold.cpp` | `lib/BibleSearch`, `lib/Memory`, `test/stubs` | |
| `test/passage_doc` | `Fold.cpp` | `lib/BibleSearch`, `lib/Memory`, `test/stubs` | |
| `test/storage_io` | `Fold.cpp` | `lib/BibleSearch` | already has stubs and Memory (`:29-31`) |

All the include paths are needed:

- Without `lib/Memory`, `<Memory.h>` resolves to the C library's `<memory.h>`
  (`lib/GfxRenderer/BitmapHelpers.h:9-11`).
- `<Logging.h>` must be the no-op stub (`test/stubs/Logging.h:6-8`).
- Adding `test/stubs` shadows nothing in those targets (research F2).

`test/tag_palette` gains its own `FailingNothrowNew.{h,cpp}`, copied from `test/ditherers` (A20).

## Data and control flow

```
TagPaletteFile::load ──► fromJson ──► entries_ rebuilt ──► rebuildDisplayOrder ──► rank, byName_
StudyStore::addTagName ─► add (append) ─────────────────► rebuildDisplayOrder
MigrationPlanner ───────► add (append) × k ─────────────► rebuildDisplayOrder × k          (A17)
retireTag / revive ─────► active flag only ─────────────► (no rebuild)

activeIdsByName ◄── byName_ (walk, filter active) ◄── StudyStore::activeTags ◄── TagPickerActivity (today)
                                                  ◄── TagChipView::buildEntries (t4)
nameRank ◄── Entry::rank (linear id scan) ◄── inDisplayOrder ◄── StudyStore::tagNamesFor
                                                                 ◄── HighlightsActivity::tagsValueFor, once per filtered passage
                                                                     (HighlightsActivity.cpp:97-106)
```

Every palette mutator, and so every rebuild, runs on the loop task. The readers mostly do too, with
one interim exception:

- Today's picker calls `addTagName` and `retireTag` under `RenderLock`
  (`TagPickerActivity.cpp:160-167`, `:264-277`), so the rebuild runs inside that lock (A16).
- **Until `t5` lands**, the picker's `buildScreen` calls `STUDY.activeTags()`
  (`TagPickerActivity.cpp:79`) on the render task. The render loop holds `RenderLock` around
  `render()` (`src/activities/ActivityManager.cpp:52-59`). `UiListActivity::render` reaches
  `buildScreen` through `renderUi()` (`src/activities/UiListActivity.cpp:27-31`, `:154-157`).
  - So that read walks `byName_` under the render lock.
  - It never overlaps a rebuild, only because every palette mutation that can rebuild or flip
    `active` also runs under `RenderLock`.
  - Moving such a mutation out of the lock while this picker exists would race on `byName_`.
- After `t5`, no render task reads the palette (parent A1).

## Error handling

| Failure | Behaviour |
|---|---|
| `SortKey[n]` allocation fails | `LOG_ERR`, then rank = entry index and `byName_` = identity. Tags show in allocation order, today's behaviour. The next successful rebuild restores name order (A10). Nothing is persisted, so nothing can be corrupted. |
| Scratch string, `byName_` or `entries_` growth fails | The throwing allocator aborts under `-fno-exceptions` (`Memory.h:9-10`). That is the same failure class as the `push_back` and `std::string` copies `add` and `fromJson` already make (`TagPalette.cpp:25`, `:91`, `:99`). No new class is added (A3, A7). |
| `fromJson` rejects a document | Returns false before touching `entries_`. Rank and `byName_` stay consistent with the old entries (I1). |
| A crafted file with an entry id 65535 | Pre-existing bug: the next `add` hands out id 0, i.e. `UNLABELLED` (`TagPalette.cpp:103-104`, probe in A12). It is out of scope and filed as a discovery. The rank code makes it no worse (A12). |

## Memory and cost

All measured sizes come from compiling a probe with host clang 21 and `xtensa-esp32s3-elf-g++`
14.2.0.

| Item | Size | Lives in |
|---|---|---|
| `Entry::rank` | **0 B**. `Entry` is 40 B (host) / 32 B (xtensa) with or without it, because the field fits existing padding | inside `entries_` |
| `byName_` | 2 B × entries, resident: 120 B at 60 | internal SRAM (< 4,096 B) |
| `SortKey[n]` | 26 B × entries, transient: 1,560 B at 60 | internal below ~157 entries, PSRAM above |
| Scratch string | 24 B capacity, transient | heap |
| `inDisplayOrder` | 32 B local + ~24 B result | stack |

**PSRAM threshold.** The ~157-entry crossover comes from the 4,096 B routing threshold,
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`. That value lives in the generated, gitignored
`sdkconfig.x4pro` (`.gitignore:33`; read at `:2152` of a sibling worktree's build output).

**Entry bound.** Entries are bounded in two ways:

- the palette read: `readDocFromFileChecked` → `Storage.readFile`, capped at 50,000 B
  (`lib/Serialization/PersistableStore.cpp:124-127`; CLAUDE.md "Storage discipline");
- the save budget (`TagPaletteFile.cpp:29`).

Both bound the palette at roughly 3,000 entries. The worst case is a ~78 KB transient `SortKey`
buffer in PSRAM, or the fallback if PSRAM is short.

**Cost per rebuild.** One rebuild is *n* folds of ≤ 24 B plus one sort of *n* 26-byte records. It
runs on:

- every publication open (`StudyStore.cpp:71`);
- every study sleep screen (`StudySleepScreen.cpp:169`; research F5);
- every new tag;
- each new name during the one-time migration (A17).

## Testing strategy

TDD, in this order. The full set of tests goes in `test/tag_palette/TagPaletteTest.cpp`, plus a new
`test/tag_palette/TagDisplayOrderTest.cpp`, both compiled into `TagPaletteTest`.

**Step 0: build plumbing, no behaviour.** Add `Fold.cpp` and the include directories to all four
targets, as tabled. Add the injector, `TagDisplayOrderTest.cpp` and `-fno-builtin` to
`test/tag_palette` only. Confirm all four still build and that the 194-test baseline stays green
(research §7). Once Step 3 is green, confirm `ctest --test-dir build/test -R TagDisplayOrder`
reports five tests. A test file left out of `add_executable` is never compiled, and the suite would
stay green with no coverage.

**Step 1: order (red, then green).**

| Test | Asserts |
|---|---|
| `TagPaletteOrder.IgnoresCase` | `beta`(1), `alfa`(2), `Alfa`(3) → `[3, 2, 1]`; equal keys tie on raw bytes, `A` < `a`. `Alfa` is allocated last so the id tiebreak alone cannot pass the test (plan review 0) |
| `TagPaletteOrder.IgnoresAccents` | `éxodo`, `esperanza`, `Ezequiel` → esperanza, éxodo, Ezequiel |
| `TagPaletteOrder.FoldsEnyeIntoN` | `anzuelo`, `año` → año first; `nube`, `ñu` → ñu first |
| `TagPaletteOrder.SkipsOneLeadingInvertedMark` | `reino`, `¿Por qué?`, `paz`, `¡Ánimo!` → ¡Ánimo!, paz, ¿Por qué?, reino |
| `TagPaletteOrder.SkipsOnlyTheFirstMark` | `¿¡Hola` sorts after `zeta` (A5) |
| `TagPaletteOrder.SortsDigitsAsText` | `2 Reyes`, `10 mandamientos` → `10 …` first (D2 accepted) |
| `TagPaletteOrder.BreaksANameTieOnTheId` | `fromJson` rows `{i:7, n:"fe"}`, `{i:3, n:"fe"}` → `[3, 7]` |
| `TagPaletteOrder.BreaksATruncatedNameTieOnTheId` | two 30-byte names equal in their first 24 bytes, loaded with ids 9 and 4 → `[4, 9]` (`TagPalette.cpp:92`) |
| `TagPaletteOrder.ExcludesRetiredTags` | a retired entry is absent from `activeIdsByName` |
| `TagPaletteOrder.LeavesActiveIdsInAllocationOrder` | `activeIds()` is unchanged on the same palette |
| `TagPaletteOrder.IsEmptyForAnEmptyPalette` | default palette and an empty `fromJson` → `{}` |
| `TagPaletteRank.OrdersRetiredIds` | active `alfa`, retired `beta`, active `gamma` → strictly increasing ranks |
| `TagPaletteRank.IsMaxForUnlabelledAndUnknownIds` | `nameRank(UNLABELLED)` and `nameRank(toTagId(999))` == `UINT16_MAX` |
| `TagPaletteRank.IsRebuiltByAnAppendingAdd` | `mu`, then `alfa` → `[alfa, mu]` |
| `TagPaletteRank.IsRebuiltByFromJson` | a file in order `zeta`, `alfa` → `[alfa, zeta]` |
| `TagPaletteRank.SurvivesACopy` | a copied palette gives the same `activeIdsByName` and ranks |
| `TagPaletteRank.RetireAndReviveLeaveRanksUnchanged` | every id's `nameRank` is equal before and after `retire`, and after re-`add` of the same name |

**Step 2: rebuild triggers and OOM (red, then green).** These use `failing_new::ScopedNthNothrowFailure`
and capture `fired()` and results inside the guard, then assert outside it, as
`DitherersTest.cpp:57-80` does. Inside the guard `SortKey[n]` is the only allocation the injector can
fail: `std::string` and `std::vector` use the never-failing throwing form, and ArduinoJson uses
`malloc` (research F3).

| Test | Asserts |
|---|---|
| `TagPaletteRebuild.AnAppendingAddMakesExactlyOneNothrowAllocation` | guard(1) around a new `add` → `fired()`; guard(2) → not fired |
| `TagPaletteRebuild.AcceptedFromJsonMakesExactlyOneNothrowAllocation` | the same, around `fromJson` of a non-empty palette |
| `TagPaletteRebuild.RetireDoesNotRebuild` | guard(1) around `retire` → not fired |
| `TagPaletteRebuild.ReviveDoesNotRebuild` | guard(1) around `add` of a retired name → not fired |
| `TagPaletteRebuild.ReAddingAnActiveNameDoesNotRebuild` | guard(1) around `add` of an active name → not fired |
| `TagPaletteRebuild.ARejectedFromJsonDoesNotRebuild` | guard(1) around a future-version `fromJson` → not fired, and the old order is kept |
| `TagPaletteRebuild.AnEmptyPaletteAllocatesNothing` | guard(1) around `fromJson` of zero tags → not fired |
| `TagPaletteOom.FallsBackToAllocationOrderOnAdd` | `zeta`, `mu`, then guard(1) around `add("alfa")` → `activeIdsByName() == activeIds()`, ranks 0, 1, 2 |
| `TagPaletteOom.FallsBackToAllocationOrderOnFromJson` | the same through `fromJson` |
| `TagPaletteOom.TheNextRebuildRestoresNameOrder` | after a fallback, an unguarded new `add` → name order |

The rebuild tests are the only way to observe "not rebuilt". A rebuild with unchanged names
produces identical ranks, so rank equality alone cannot tell the two apart.

**The OOM log line is not observable on the host.** The host `LOG_ERR` expands to nothing
(`test/stubs/Logging.h:6`), so the `TagPaletteOom.*` tests check only the fallback order. The log
line itself is checked in two ways:

- by inspection at PR review;
- for its format string, by `-Wformat` under `pio run` (`platformio.ini:70-72`). That check works
  because `logPrintf` carries `format(printf, 3, 4)` (`lib/Logging/Logging.h:42`).

No log-capturing stub is added for this task.

**Step 3: `inDisplayOrder` (red, then green), in `TagDisplayOrderTest.cpp`.**

| Test | Asserts |
|---|---|
| `TagDisplayOrder.OrdersAPassagesTagsByName` | stored `[gamma, alfa, beta]` → `[alfa, beta, gamma]`, count 3 |
| `TagDisplayOrder.PlacesARetiredTagByItsName` | a retired `beta` lands between `alfa` and `gamma` |
| `TagDisplayOrder.PutsUnknownIdsLastByRawId` | unknown ids 900 and 800 go after the known ones, as `[…, 800, 900]` |
| `TagDisplayOrder.LeavesTheStoredOrderAlone` | the input vector is unchanged afterwards |
| `TagDisplayOrder.ConsidersAtMostTheTagCap` | nine input ids → `count == MAX_TAGS_PER_PASSAGE` (A15) |

**Step 4: `StudyStore` glue.** No host target exists, so the existing tests do not reach it. It is
covered by the `inDisplayOrder` tests and the device checks below.

**Verification commands.**

```
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j
~/.platformio/penv/bin/pio run -e x4pro         # links lib/StudyStore -> lib/BibleSearch; no new warnings in
                                                # TagPalette.cpp, TagDisplayOrder.h, StudyStore.cpp
~/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high
                                               # as CI runs it (.github/workflows/ci.yml:76); scans src/,
                                               # which holds the rewritten StudyStore.cpp
./bin/clang-format-fix                         # the whole tree, then git status shows no reformat
```

**Device checks (owner).**

- Highlights passage rows list each passage's tags alphabetically. A row whose stored order was
  `oración, amor` reads `amor, oración`.
- The tag picker lists tags alphabetically, and a tag created from it appears in its alphabetical
  place. The batch is tested as one PR, after `t5` has replaced the picker.
- Serial shows no `TAGS` OOM line on opening a publication.
- `ESP.getFreeHeap()` over serial is flat across a publication open and close.

## Assumptions

Each assumption is a decision a reviewer can attack.

- **A1 (cache both directions).** Rank by id (`Entry::rank`) and id by rank (`byName_`) are both
  stored. With only one, the other direction costs either an extra linear scan per `nameRank`,
  which sits on the per-passage path, or a temporary plus a sort per `activeIdsByName`. Without
  `byName_`, an O(n) placement over `Entry::rank` would need a sentinel id, and every `uint16_t`
  value is a valid `TagId` in principle (see A12).
- **A2 (field placement).** `rank` is the last `Entry` member. It is free (memory table), and the
  two existing brace initialisers `{id, name, true}` (`TagPalette.cpp:25`) and
  `{tagId, std::move(text), …}` (`:99`) stay valid. A probe of that exact shape compiles clean
  under host `-Wall -Wextra -pedantic` and xtensa `-Wall -Wextra`. The rebuild that follows each one
  overwrites the placeholder 0.
- **A3 (`byName_` uses the throwing allocator).** `byName_` is a `std::vector` so the palette stays
  copyable (`TagPaletteFileTest.cpp:112`), which a `unique_ptr` buffer would break. Its `resize` can
  abort on OOM, as `entries_.push_back` already can.
- **A4 (key slot).** Keys sit in 24-byte zero-padded slots compared with one 24-byte `memcmp`. A
  fold contains a NUL only if the name does. Then `"a"` and `"a\0"` pad to equal keys, and the
  raw-name tiebreak still orders them, so the order stays total. No separate length is stored.
- **A5 (mark skip).** Exactly one leading `¿` or `¡` is skipped, matched on its exact UTF-8 bytes
  before folding. A mark that is not first, or a second mark, folds as it is and sorts after `z`
  (research §3). This is the brief's "skip one leading".
- **A6 (total order).** Ids are unique within `entries_`: `add` allocates fresh ids, and
  `fromJson` dedupes by id (`TagPalette.cpp:94-97`). So the three-level comparator never returns
  "equal" for two different entries, and `std::sort` is deterministic without `stable_sort`.
- **A7 (one nothrow allocation).** Only `SortKey[n]` comes from `makeUniqueNoThrow`. The scratch
  string is reserved once to 24 B and reused, as `TypedReference.cpp:241-242` does, and it uses
  the throwing allocator. Making it nothrow would mean a raw-buffer fold overload, which touches
  `Fold.h` (out of scope).
- **A8 (rebuild triggers).** These are as tabled under "When it runs": an appending `add` and an
  accepted `fromJson`, and nothing else.
- **A9 (empty palette).** `n == 0` clears `byName_` and returns before allocating. The rebuild tests
  can then count allocations exactly.
- **A10 (fallback).** The fallback assigns allocation order to every entry, not only the new one,
  so I1 holds and the order is coherent. It is not sticky: each rebuild starts from scratch.
- **A11 (log module).** The log module is `"TAGS"`, shared with `TagPaletteFile.cpp:12`. The OOM
  line names the bytes requested, with `%zu` as `FontInstaller.cpp:95` uses it.
- **A12 (`UINT16_MAX` is never a real rank).** There are at most 65,535 entries: ids are unique,
  non-zero (`TagPalette.cpp:89`; `nextRaw_` starts at 1, `TagPalette.h:75`) and `uint16_t`. So the
  largest rank is 65,534 and the sentinel cannot collide.
  - `nameRank(UNLABELLED)` returns the sentinel only because no entry carries id 0
    (`TagPalette.h:29-31`). A crafted file with id 65535 breaks that: `fromJson` wraps `nextRaw_` to
    0 (`TagPalette.cpp:103-104`).
  - A probe confirmed the next `add` returns id 0, and that a stored `"n":70000` wraps to 4464.
    That is filed as a discovery, not fixed here. Under it, `nameRank(UNLABELLED)` would return the
    rogue entry's rank instead of `UINT16_MAX`, and nothing else in this design changes.
- **A13 (`activeIdsByName` cost).** It is called per picker build and per chip-row rebuild, never
  per row (parent §1, "Why a cache"). O(n) with one reserved allocation.
- **A14 (helper placement).** `inDisplayOrder` lives in `lib/StudyStore`, not `src/study` as
  `StudyReuse.h` does. It uses only `lib/StudyStore` types (`TagPalette`, `PassageDoc`), and
  `test/tag_palette`, which already compiles `TagPalette.cpp`, tests it with no new target. It is
  header-only, so no other CMake target changes. The brief describes the ordering as inline in
  `tagNamesFor`. The helper does the same thing behind a testable seam.
- **A15 (cap).** `inDisplayOrder` considers at most `MAX_TAGS_PER_PASSAGE` (=8,
  `PassageDoc.h:45`) ids. More is unreachable: `normaliseTags` caps and dedupes on every path that
  sets tags (`PassageDoc.cpp:17-27`, used at `:151`, `:197`, `:329`), and `removeTagEverywhere` only
  shrinks (`:201-207`).
- **A16 (rebuild under `RenderLock`).** Today's picker holds `RenderLock` across `addTagName`
  (`TagPickerActivity.cpp:160-167`), so the rebuild runs under it. At ~60 entries that is ~60
  short folds and a 60-record sort, which is short next to the e-ink refresh the lock protects.
  `retireTag` does not rebuild.
- **A17 (migration).** `adoptTagNames` and `planMigration` call `add` in loops
  (`MigrationPlanner.cpp:24`, `:44`), so a migration of *k* new names rebuilds *k* times. *k* is at
  most 200 (`TagPalette.cpp:21`), and the migration runs once (`MigrationRunner.cpp:209`). This is
  accepted rather than adding a batch API to the palette.
- **A18 (batch-branch window).** Today's `TagPickerActivity::commitAndFinish` reports selected ids
  in `tags_` order (`TagPickerActivity.cpp:216-225`), and `tags_` is `STUDY.activeTags()` (`:27`,
  `:35`, `:79`). Once this task lands and until `t5` does, the batch branch's picker therefore
  **stores** a passage's tags in display order.
  - `t5`'s brief restores palette order (`hpipe brief --task t5`, lines 55-56: "palette allocation
    order (`activeIds()`), exactly as today"; acceptance line 83).
  - The batch ships as one PR (`hpipe status`: "ships as one PR"), so the window never reaches
    `main`. `t5` must not be dropped from the batch without moving the commit to `activeIds()`.
  - Only this spec records the window, and neither the `t5` worker nor the orchestrator reads it.
    So the plan repeats it, and the worker states it to the orchestrator in the last message of the
    implement turn: "t1 changes the picker's commit order until t5 lands; t5 must not be dropped
    from the batch."
- **A19 (template use).** This adds two `std::sort` instantiations to library code, one in
  `TagPalette.cpp` and one in `TagDisplayOrder.h`. There is precedent in `lib/`:
  `IndexBuilder.cpp:319`, `BookMetadataCache.cpp:153`, `SdCardFont.cpp:1097`.
- **A20 (test plumbing).** Per-suite CMake only. The injector is copied into `test/tag_palette`,
  because its header confines it to one suite (`test/ditherers/FailingNothrowNew.h:5-7`). Moving it
  to `test/stubs` would edit another surface's suite for no behavioural gain.
- **A21 (`-fno-builtin`).** It applies to the whole `TagPaletteTest` target, as on
  `DitherersTest` (`test/ditherers/CMakeLists.txt:16-19`). Without it, clang may elide the replaced
  `operator new[]`.
- **A22 (firmware link).** LDF `chain` should attach `BibleSearch` to `StudyStore` (research F6). If
  `pio run` fails to resolve or link it, the fix is a manifest or LDF setting this repo does not
  use for `StudyStore`. That fix is surfaced through `hpipe decide`, not chosen alone.
- **A23 (`activeTags` reserve).** `activeTags` reserves before its loop, per CLAUDE.md "The resource
  protocol" rule 7. Today's version does not (`StudyStore.cpp:160-161`).

## Deviations from the batch spec

| Batch spec says | This spec | Why |
|---|---|---|
| A rank of "one `uint16_t` per entry" | That, plus `byName_` (A1) | `activeIdsByName` needs the inverse. The total resident cost still matches the parent's §6 "2 B × entries", because `Entry::rank` is free (measured) |
| A "key buffer of `entries × 24` bytes" | `entries × 26` (`SortKey`) | The sort needs each key's entry index. That is one allocation and one OOM path instead of two |
| `tagNamesFor` sorts a local array | The same, inside `inDisplayOrder` (A14) | Host-testability |
| PSRAM "past ~170" entries (§6 table) | Past ~157 | 4,096 / 26 instead of 4,096 / 24 |

## Files touched

- `lib/StudyStore/StudyStore/TagPalette.h`, `TagPalette.cpp`
- `lib/StudyStore/StudyStore/TagDisplayOrder.h` (new)
- `src/study/StudyStore.h` (two comments), `src/study/StudyStore.cpp` (`activeTags`, `tagNamesFor`)
- `test/tag_palette/CMakeLists.txt`, `TagPaletteTest.cpp`, `TagDisplayOrderTest.cpp` (new),
  `FailingNothrowNew.h` and `FailingNothrowNew.cpp` (new, copied)
- `test/migration_planner/CMakeLists.txt`, `test/passage_doc/CMakeLists.txt`,
  `test/storage_io/CMakeLists.txt`

## Review 0 outcome

Review: `docs/superpowers/reviews/btrial-1-spec-review-0.md`, verdict CLEAR, with no BLOCKERs, no
MAJORs and five MINORs. All five are accepted and fixed inline in this revision. I checked each
finding's new citations against the source first.

| Finding | Change |
|---|---|
| MINOR 1: the A18 window is recorded only in this spec | A18 now requires the plan to repeat it and the implement turn's last message to state it to the orchestrator. The device check no longer names the interim picker |
| MINOR 2: `TagDisplayOrderTest.cpp` missing from the build table; Step 0 put the injector in all four targets | Added to the `test/tag_palette` row. Step 0 scopes the injector and `-fno-builtin` to `test/tag_palette` and checks that `ctest -R TagDisplayOrder` reports five tests |
| MINOR 3: "no render task reads the palette" is false until `t5` | The data-flow section names the interim render-task read of `activeTags()` and why it is safe: every rebuild-triggering mutation runs under `RenderLock` |
| MINOR 4: the `LOG_ERR` clause has no host check | The testing strategy says the line is checked by inspection at PR review and by `-Wformat` under `pio run` |
| MINOR 5: `pio check` missing | Added to the verification commands, with CI's flags |

Two of the review's citations are slightly off, and neither changes a fix:

- `logPrintf`'s attribute is at `lib/Logging/Logging.h:42`, not `:43`.
- The interim picker's `addTagName` lock is at `TagPickerActivity.cpp:160-167`. The review's
  `:186-189` is a comment about name validation.
