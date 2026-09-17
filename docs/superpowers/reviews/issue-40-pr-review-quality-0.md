# PR #68 — code quality review, pass 0

**Branch**: `fix/40-bound-recent-book-strings` · **Base**: `main` · **Stage**: 2 of 2 (quality)
**Scope**: `src/RecentBook.h`, `src/RecentBooksStore.{h,cpp}`, `src/util/RecentBooksDoc.{h,cpp}`,
`test/recent_books_doc/`, `test/CMakeLists.txt`. Docs under `docs/superpowers/` read as context,
not reviewed as code.

Stage 1 (`issue-40-pr-review-intent-0.md`) returned CLEAR and recorded four items as
accepted-and-not-findings. Those are not re-opened here.

## What I ran

| Check | Result |
|---|---|
| `cmake -S test -B <tmp>` then `cmake --build -j8` | exit 0. The only warnings are pre-existing (`lib/Epub/Epub/ParsedText.cpp:968,970`, `lib/EpdFont/FontDecompressor.cpp:522,523`). Nothing from the new files. |
| `ctest --test-dir <tmp> -j8` | **591/591 passed**, 0.52 s. `-R RecentBooksDoc` → **15/15**. Matches the PR body exactly. |
| `~/.platformio/penv/bin/pio run -e x4pro` | **SUCCESS**, 50 s. Flash 81.1 %, RAM 19.5 %. Both `static_assert`s in `src/RecentBooksStore.cpp:125-131` compiled. |
| `./bin/clang-format-fix` (whole tree, venv on PATH) | exit 0, `git status --short` empty. |
| Convergence of `RecentBooksDoc::normalise` | Verified empirically — see MINOR 1. |

## Is the mirroring real?

Yes, and it is close to line-for-line rather than cosmetic.

- `src/util/RecentBooksDoc.h:11-19` reproduces `src/util/BookmarkDoc.h:10-17`'s header contract —
  same "format rules, no storage access, the shell is X", same "deliberately free of `<Arduino.h>`",
  same "never calls `measureJson`; the store is the only place that measures".
- The struct split matches: `src/RecentBook.h` sits beside `src/BookmarkEntry.h` and is reached as
  `#include "../RecentBook.h"` from `src/util/`, exactly as `BookmarkDoc.h:10` reaches
  `BookmarkEntry.h`. `src/components/themes/BaseTheme.h:10` already forward-declared `RecentBook`,
  so the move breaks nothing.
- `test/recent_books_doc/CMakeLists.txt` is a byte-for-byte analogue of
  `test/bookmark_doc/CMakeLists.txt` (same source list shape, same three include dirs, same three
  link targets, same `gtest_discover_tests`), and `RecentBooksDocTest.cpp:1-4` mirrors
  `BookmarkDocTest.cpp:1-3`'s preamble.
- `src/util/RecentBooksDoc.cpp:64-69` carries forward the `| ""` const-char* read with the same
  flash-cost comment `BookmarkDoc.cpp:38-39` and `PersistableStore.h:112-115` both state.
- The `bool& needsResave` out-parameter is not invented here: `PersistableStoreBase::extractPassword`
  (`lib/Serialization/PersistableStore.h:93-95`) uses the identical idiom, and
  `src/RecentBooksStore.cpp:20` routes it through `requestResave()` rather than calling
  `saveToFileAtomic()`, which is exactly what `PersistableStore.h:41-44,118` demands and what
  `src/CrossPointSettings.cpp:230` and `src/WifiCredentialStore.cpp:106` already do.

The derived-budget machinery (`DOC_WRAPPER_BYTES`, `ENTRY_OVERHEAD_BYTES`, `ESCAPE_FACTOR`,
`worstCaseBytes()`) has no sibling, but it is not a second way to do an existing thing: neither
`BookmarkDoc` nor `PassageDoc` has a bounded record count, so neither could derive a budget, and
`PersistableStore.h:135-137,167-169` explicitly invites a bounded store to declare a tighter
`SAVE_BUDGET`. This is the first store that qualifies.

Every mutation path into `recentBooks` is covered. `addBook` (`src/RecentBooksStore.cpp:37-39`),
`updateBook` (`:61`) and `fromJson` (`:15`) all normalise; `updatePath` and `pruneMissing` only touch
`path`/`coverBmpPath` or remove entries, neither of which is capped by design; `getBooks()` returns a
const reference. Call sites confirmed: `ReaderActivity.cpp:60`, `EpubReaderActivity.cpp:435`,
`HomeActivity.cpp:75`, `RecentBooksActivity.cpp:71-72`, `PublicationDownloader.cpp:120`,
`CardBooks.cpp:61`.

Every `file:line` citation embedded in the new comments checks out, which is worth saying because
stale citations are the usual failure mode of comments this dense:
`LauncherActivity.cpp:83` (cap 48), `:96-99` (the `looksLikeABible` lambda), `:103` (cap 40),
`:132` (cap 30), `PublicationsActivity.cpp:56` (cap 48), `Utf8.cpp:148` (`utf8SafeTruncateBuffer`'s
signature, immediately above the `buf[len - 1]` walk-back), `Epub.h:48` (the `"/epub_" + hash`
cache key), `Epub.cpp:653` (`"/thumb_[HEIGHT].bmp"`). `ENTRY_OVERHEAD_BYTES = 52` and
`DOC_WRAPPER_BYTES = 12` are correct by hand-count and are additionally pinned by
`RecentBooksDocTest.cpp:31-45` against the real serialiser.

---

## Findings

### MINOR 1 — the load path's convergence property is host-testable and was delegated to the device

`src/util/RecentBooksDoc.cpp:32-40` · `src/RecentBooksStore.cpp:15-21` ·
`test/recent_books_doc/RecentBooksDocTest.cpp:142-152`

`normalise()` is not idempotent in one pass: `utf8SafeSummary` trims *before* it truncates
(`lib/Utf8/Utf8.cpp:184-200`), so a cap landing just after a space leaves a trailing space that the
next load trims and reports as another change. The PR body states the consequence correctly — "at
most two resaves, then none" — and then hands it to the human as device-verification item 2.

That property is the one the load path structurally depends on, and it matters more than the PR body
lets on: `loadFromFile()` is called on **every entry to the launcher and to Publications**
(`LauncherActivity.cpp:77`, `PublicationsActivity.cpp:45`, plus `main.cpp:413`). A `normalise()` that
did not reach a fixed point would mean an atomic SD write — temp file plus rename — on every entry to
the home screen, forever, on a battery device. Nothing in the 15 new tests would catch that.

I verified the claim holds. Applying `utf8SafeSummary` repeatedly at caps 128 and 96 over nine
adversarial inputs (long runs of `"ab "`, `"a "`, `"word  "`, tab runs, embedded newlines, 60×U+4E16
with and without trailing spaces, leading/trailing whitespace) reaches a fixed point in **1 or 2
passes in every case, never more**. So this is a coverage gap, not a defect.

It is three lines to close, against the existing `RecentBooksDoc::normalise`:

```cpp
TEST(RecentBooksDocNormalise, ConvergesWithinTwoPasses) {
  std::string s;
  while (s.size() < 400) s += "ab ";
  RecentBook book = makeBook("/books/b.epub", s.c_str(), s.c_str(), "");
  EXPECT_TRUE(RecentBooksDoc::normalise(book));
  RecentBooksDoc::normalise(book);  // the trailing-space trim, if any
  EXPECT_FALSE(RecentBooksDoc::normalise(book))
      << "a third pass must be a no-op, or the launcher resaves recent.json on every entry";
}
```

Closely related and worth folding into the same test pass: `needsResave` is asserted true only for a
**length**-driven change (`RecentBooksDocTest.cpp:201-224`). The whitespace-collapse change — the one
that causes the *second* resave — is exercised at `:186-200` (`KeepsTheBibleHeuristicsMarkers`, which
feeds an interior double space) but that test discards `normalise()`'s return value entirely. Adding
`EXPECT_TRUE(RecentBooksDoc::normalise(book))` for the double-space case pins it at zero cost.

### MINOR 2 — unused `<string>` include, a small deviation from the mirrored header

`src/util/RecentBooksDoc.h:6`

The header declares `size_t` (`<cstddef>`), `std::vector<RecentBook>&` (`<vector>`), `JsonDocument` /
`JsonVariantConst` (`<ArduinoJson.h>`) and `RecentBook&` (`"../RecentBook.h"`). It never names
`std::string` — that belongs to `src/RecentBook.h:2`, which already includes it. `BookmarkDoc.h`, the
model, includes exactly `<ArduinoJson.h>`, `<SaveBudget.h>`, `<cstddef>`, `<vector>` and its entry
header, with no `<string>`. Drop line 6.

---

## Considered and cleared

Recorded so the next pass does not re-open them.

- **`RecentBooksDoc::fromJson` can never return `false`.** Four tests `ASSERT_TRUE` a value that is
  structurally constant. `RecentBooksDoc.h:81-88` states why the `bool` is kept — the CRTP contract
  at `PersistableStore.h:105` requires the store's `fromJson` to return `bool`, and unlike
  `BookmarkDoc` there is no format version to refuse (design doc A9). Keeping the signature aligned
  with the sibling is worth more than shaving a constant, and the assertions become live the moment a
  version check is added. Not dead code in any sense that costs anything.
- **Zero headroom in the budget.** `RecentBooksDocTest.cpp:255-261` asserts the worst case measures
  *exactly* `SAVE_BUDGET`, where `BookmarkDoc.h:36-42` deliberately leaves 34 bytes of slack. The
  difference is defensible: ArduinoJson is pinned to 7.4.2 in both `platformio.ini:141` and
  `test/CMakeLists.txt:27-32`, so host and device cannot diverge, `persist::fitsBudget` is `<=`
  (`SaveBudget.h:22`), and `RecentBooksDoc.h:57-59` names the remedy (raise a named allowance) rather
  than leaving a future reader to guess. Stricter than the sibling, not looser.
- **Three ways to size a budget now exist** (`DEFAULT_SAVE_BUDGET` for BookmarkDoc, a hand-picked
  200 000 for `PassageDoc`, derived here). Each store's budget answers a different question, and
  `PersistableStore.h:167-169` sanctions the per-store override. Not a competing mechanism.
- **`normalise()`'s return is discarded at `RecentBooksStore.cpp:38` and `:62`.** Correct — those
  paths save unconditionally; only the load path needs the flag.
- **Comment density.** The new comments are uniformly "why", not paraphrase: the
  `capField(...) || changed` ordering trap (`RecentBooksDoc.cpp:33-34`), the `requestResave`
  deadlock (`RecentBooksStore.cpp:17-19`), why `path` is never truncated (`RecentBooksDoc.h:43-47`),
  why NUL is erased rather than reasoned about (`:29-31`). Per CLAUDE.md these earn their place.
- **`std::string` temporaries in `capField`.** Twenty short-string copies per load, on the boot and
  launcher-entry path, not a render path. `BookmarkDoc.cpp:44` does the same per bookmark.
- **`test/CMakeLists.txt` committed**, the `PATH_BUDGET_ALLOWANCE` allowance-vs-bound distinction,
  the `BookmarkDoc`-shaped split on a good-first-issue, and `utf8SafeSummary` jamming words around a
  newline — all four accepted by the stage-1 intent review.

---

## Verdict rationale

No BLOCKER. No MAJOR. The two MINORs are additive: one test and one deleted `#include`, neither of
which changes behaviour, reverses a decision, or needs a judgment the human has not already made.
The change mirrors `BookmarkDoc`/`BookmarkFile` structurally rather than superficially, every write
path is covered, the tests pin measured facts rather than restate arithmetic, and the build, the
full 591-test host suite and the format gate are all green here.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
