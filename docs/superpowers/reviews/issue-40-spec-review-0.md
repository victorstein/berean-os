# Adversarial review — `docs/superpowers/specs/2026-09-17-issue-40-design.md` (pass 0)

Reviewed at `31672801` on branch `fix/40-bound-recent-book-strings`. Every file
reference below was read in this worktree; every measurement was run here.

## What I re-derived independently

The arithmetic holds. Compiled against the pinned ArduinoJson 7.4.2
(`/Volumes/stein/Documents/development/personal/berean-os/.pio/libdeps/x4pro/ArduinoJson`,
`library.properties:2` → `version=7.4.2`), building the exact shape
`RecentBooksStore::toJson` writes (`src/RecentBooksStore.cpp:11-20`):

```
empty doc:                12          (spec DOC_WRAPPER_BYTES = 12)
10 empty entries:         541         (spec 12 + 9 + 10*52 = 541)
10 worst-case entries:    11421       (spec SAVE_BUDGET = 11421)
128 x 0x01:               136         (+8 overhead -> no \u escaping)
128 x '\t':               264         (+8 -> 2x)
128 bytes of 4-byte UTF-8:136         (+8 -> 1x)
```

`ENTRY_OVERHEAD_BYTES = 52` is right by hand too
(`{"path":"","title":"","author":"","coverBmpPath":""}` = 52). A6's 512 is two
full FAT LFN components (`SdFat/src/common/FsStructs.h:107-111`, SdFat 2.3.1),
and `lfnReservedChar` does reject `"`, `\` and everything under `0x20`, so A8's
"no escaping in a path" is sound. A7's 57-byte derivation checks out:
`/.crosspoint` (12) + `/epub_` (6) + ≤20 digits + `/thumb_[HEIGHT].bmp` (19)
— `lib/Epub/Epub.h:46-48`, `lib/Epub/Epub.cpp:653`. A1's "72 bytes" and A2's
"52 bytes" are exact (`len(...encode())` = 72 and 52).

A13's two halves are both true as stated: `.claude/agents/data-dev.md:22-27`
says report-don't-edit, and `git log --oneline -- test/CMakeLists.txt` shows
`5bf4cb9b` (#65), `6156de32` (#64) and `d043fd84` (#47) each committing their
own `add_subdirectory` line.

A11's premise is real — `PersistableStore.h:3` is an unconditional
`#include <Arduino.h>`, and `src/util/HighlightFileAction.h:10-15` is the
settled reasoning. A12's is real — `PersistableStore.h:14-22`.

The findings below are what survived.

---

## MAJOR 1 — the legacy-rescue worked example cannot happen, and the failure A4/A5 actually prevent is a different one

**Claim.** Spec lines 442-445: *"A `recent.json` written by 1.9.10 with ten
4,000-byte titles therefore: loads in full, shrinks in memory, and is rewritten
once — never refused. Without A4+A5 it would load at ~80 KB, already past
`SD_READ_TRUNCATION_CAP`, and be unsavable forever."* A4's attack repeats it
(lines 154-157).

**Problem.** An ~80 KB `recent.json` never reaches `fromJson` at all, with or
without A4/A5, so it is neither loaded in full nor shrunk nor resaved. And
1.9.10 cannot have written one in the first place.

**Evidence.**

- `freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:201-208` —
  `constexpr size_t maxSize = 50000;` and the read loop stops at it, returning
  the truncated `String` with no error.
- `lib/Serialization/PersistableStore.cpp:50-59` — that truncated string goes
  straight to `deserializeJson`, which fails mid-token, and the read is
  classified `ParseError` (`lib/Serialization/DocReadStatus.h:17-21`).
- `lib/Serialization/PersistableStore.h:186-188` — `loadFromFile` returns
  `false` on any non-`Ok` status **before** `fromJson` runs. No `normalise`, no
  `requestResave()`, no resave. The list initialises empty and the next
  `addBook` overwrites the card copy.
- The premise is unreachable anyway: `src/RecentBooksStore.h:35` already sets
  `SAVE_BUDGET = persist::DEFAULT_SAVE_BUDGET` (45,000) and
  `PersistableStore.h:170-175` refuses above it, so no shipped build can have
  written an 80 KB `recent.json`.

**Why it matters beyond prose.** This paragraph is the entire argument for A4
and A5, it is the basis of human-test item 2 ("the list survives"), and it will
be copied into the PR body. As written it promises a rescue that the read path
makes impossible, and it understates the real hazard: an over-cap file is
*unloadable and then silently overwritten*, which is a data-loss path, not a
save refusal.

**Fix.** A4 and A5 are still correct and still necessary — keep them. Restate
the example in the band they actually rescue: a file between the new 11,421 and
the 45,000 that 1.9.10 permitted (e.g. ten entries with ~1,800-byte titles,
≈ 37 KB). Say explicitly that a file over `SD_READ_TRUNCATION_CAP` is not
rescuable by this change because `readDocFromFileChecked` cannot parse it
(`PersistableStore.cpp:50-59`), and that no shipped build can produce one. Drop
the "~80 KB / unsavable forever" sentence from A4's attack.

---

## MAJOR 2 — A1/A2 justify the caps purely as display decisions, but `title` is also a substring-matched key

**Claim.** A1: *"the cap must sit above what any reachable surface can display,
so a real publication never renders differently."* A2: *"`author` renders only
as the row subtitle."* Non-goals and Risks list no other consumer.

**Problem.** `title` is not display-only. The launcher decides which book the
**Bible tile** opens by substring-matching it.

**Evidence.** `src/activities/launcher/LauncherActivity.cpp:96-99`:

```cpp
const auto looksLikeABible = [](const RecentBook& book) {
  return book.path.find("nwt") != std::string::npos || book.title.find("Nuevo Mundo") != std::string::npos ||
         book.title.find("New World") != std::string::npos;
};
```

Both halves of this change touch that input. Truncation can remove a marker
that sits past byte 128. `utf8SafeSummary`'s whitespace collapse
(`lib/Utf8/Utf8.cpp:186-192`) rewrites the matched text — here it happens to
*help* (`"Nuevo  Mundo"` starts matching), which is exactly why it needs a test
rather than an assumption.

At 128 bytes against a 72-byte NWT title the marker is safe today, so this does
not move the number. What it does is invalidate the frame A1 reasons in: "above
what the widest surface can render" is not sufficient for a field that is also
parsed.

**Fix.** Two lines of spec and one test.

1. Add to A1: `title` is also the Bible-tile heuristic's input
   (`LauncherActivity.cpp:96-99`); the cap must sit above the position of
   `"Nuevo Mundo"` / `"New World"` in a real NWT title (byte 14 in the 72-byte
   Spanish title), which 128 does with large margin.
2. Add a host test beside test 4:
   `NormaliseKeepsTheBibleHeuristicsMarkers` — the real NWT titles in both
   languages, plus one with an interior double space, all still satisfy
   `find("Nuevo Mundo") != npos` / `find("New World") != npos` after
   `normalise`. Without it, a later "tidy the cap down to 64" passes test 4's
   byte-identity check on short titles and silently breaks the Bible tile for
   the long one.

---

## MINOR 1 — `ESCAPE_FACTOR = 2` is not the true worst case; NUL is 6×

**Claim.** A8 (spec:204-217) and research §4.1: *"2 is the true worst case and
`"`/`\` are the only characters that reach it."*

**Problem.** A NUL byte serialises to six.

**Evidence.**
`.pio/libdeps/x4pro/ArduinoJson/src/ArduinoJson/Json/TextFormatter.hpp:57-65`:

```cpp
void writeChar(char c) {
  char specialChar = EscapeSequence::escapeChar(c);
  if (specialChar) { writeRaw('\\'); writeRaw(specialChar); }
  else if (c) { writeRaw(c); }
  else { writeRaw("\\u0000"); }
}
```

Measured: 128 NUL bytes → 776 (vs 264 for 128 tabs). `toJson` assigns a
`std::string` (`RecentBooksStore.cpp:16-17`), which ArduinoJson stores
length-aware, so an embedded NUL survives to the serialiser.
`EscapeSequence.hpp:34` also shows the full 2-byte set is `"`, `\`, `\b`, `\f`,
`\n`, `\r`, `\t` — not just `"` and `\`; `utf8SafeSummary` leaves an interior
`\t` and `\r` in place, so the wider set matters and is still covered by 2.

Reachability is effectively nil: XML 1.0 forbids U+0000, so no OPF title can
carry one, and the load path reads `obj["title"] | ""` as `const char*`, which
stops at the first NUL.

**Fix.** Correct A8's sentence to name the real 2-byte set (`"`, `\`, `\b`,
`\f`, `\n`, `\r`, `\t`) and state the NUL exception with its unreachability
argument — or, cheaper and self-enforcing, have `normalise` erase `'\0'` and
pin it with one assertion in test 11.

---

## MINOR 2 — A4 cites a dead caller as the live second write path, contradicting the spec's own Non-goals

**Claim.** A4 (spec:147-149): *"`updateBook` (`:67-80`) assigns
`book.title = title; book.author = author;` from `HomeActivity.cpp:75`."*

**Problem.** `updateBook`'s only caller is inside `HomeActivity`, which this
same spec proves is never instantiated (Non-goals, lines 78-81; research §3).

**Evidence.** `grep -rn "addBook\|updateBook(" src lib` returns exactly one
`updateBook` call, `src/activities/home/HomeActivity.cpp:75`.
`src/activities/ActivityManager.cpp` constructs `RecentBooksActivity`
(`:211-213`) and never `HomeActivity` — the only `HomeActivity` mentions are
the `#include` at `:16` and the unrelated `isHomeActivity()` virtual
(`src/activities/Activity.h:53`).

Bounding `updateBook` is still right (it is public API and the shell must not
be able to store an unbounded string), but A4's load-bearing sentence —
"Bounding only `addBook` leaves the store unbounded" — rests on `fromJson`, not
on `updateBook`.

**Fix.** Reword A4: `fromJson` is the reachable second write path;
`updateBook` is the third and is currently reachable only from the
uninstantiated `HomeActivity`, bounded as defence-in-depth so the shell cannot
store an unbounded string.

---

## MINOR 3 — the load diagram puts `requestResave()` in the generic base

**Claim.** Spec:433-439 draws `if (needsResave) requestResave()` as a sibling
of `RecentBooksStore::fromJson`, under `PersistableStore<T>::loadFromFile`.

**Problem.** `loadFromFile` is generic and has no `needsResave`; it only reads
the `resaveRequested` flag (`PersistableStore.h:191`). `requestResave()` is
`protected` on `PersistableStoreBase` (`:44`) and must be called from inside
`RecentBooksStore::fromJson`, exactly as the header's own comment says
(`:40-44`).

**Fix.** Nest it one level: `RecentBooksStore::fromJson` → `RecentBooksDoc::fromJson(...)`
→ `if (needsResave) requestResave();`, then `loadFromFile` performs the save
after releasing the lock (`:194-197`).

---

## MINOR 4 — "one SD write on the first boot after upgrade, and zero after that" holds only when the resave succeeds

**Claim.** A5's attack (spec:167-169).

**Problem.** If the resave fails, the on-disk file is unchanged, so the next
`loadFromFile` normalises the same over-long strings, sets the flag again and
retries. `RECENT_BOOKS.loadFromFile()` is called from
`src/activities/launcher/LauncherActivity.cpp:77` and
`src/activities/catalog/PublicationsActivity.cpp:45` — i.e. on every entry to
the launcher and to Publications, not once per boot. Error handling (spec:482-483)
says "the next `addBook` retries the write"; in fact every screen entry does.

**Fix.** One sentence in A5 or in Error handling: a failed resave is retried on
every `loadFromFile`, which is a per-screen-entry SD write attempt, not a
per-boot one. It is bounded by the failure being logged each time, and the
in-memory list stays correct.

---

## MINOR 5 — test 4's fixtures do not exist

**Claim.** Testing strategy item 4: *"the six real publication titles from A1
(39-72 bytes) and three real authors (38-52)"*.

**Problem.** A1 lists one title and A2 one author. The "39" and "38" lower
bounds have no source in either document, and the test that is supposed to make
A1/A2 "a *display* decision rather than a number" would be written from
invented fixtures.

**Fix.** List the six titles and three authors verbatim in A1/A2 (with byte
counts), or reduce the test to the titles the spec can actually cite.

---

## MINOR 6 — `utf8SafeSummary` deletes `\n` without substituting a space

**Claim.** A3's attack covers the whitespace-collapse change with
`"Awake!   2026"` → `"Awake! 2026"` (also spec:452).

**Problem.** The collapse runs *first* and `std::unique` keeps the **first**
character of a run; the `'\n'` removal runs second. For a run that begins with
a newline, the survivor is the newline and it is then erased, jamming words
together: `"Despertad!\n No. 1"` → `"Despertad!No. 1"`.

**Evidence.** `lib/Utf8/Utf8.cpp:186-196` — `std::unique` with the
`isspace && isspace` predicate, then
`passage.erase(std::remove(passage.begin(), passage.end(), '\n'), ...)`.

This is pre-existing helper behaviour that `LauncherActivity.cpp:83` already
applies, as A3 says — but this change *persists* it and extends it to the
Recents row, which renders the raw string today.

**Fix.** Add the case to the before/after table so it is a decision rather than
a surprise, and add it to human-test item 1.

---

## MINOR 7 — zero margin between the worst case and the budget

Test 11 builds the worst case from the same constants `worstCaseBytes()` is
derived from, and the measured result is **exactly** 11,421. `persist::fitsBudget`
is `<=` (`lib/Serialization/SaveBudget.h:26`), so it fits by 0 bytes.

That is the deliberate consequence of A10 and is defensible — but it is worth
saying out loud, because `BookmarkDoc.h:36-42` went the other way and A10's
justification for not doing so ("deriving avoids that conversation entirely")
does not mention that the result is a test with no slack at all: any future
serialiser formatting change, even one that does not affect real data, fails
CI. Note it in A10 so the next reader raises a named constant rather than
switching `EXPECT_LE` to something looser.

---

## Answers to the spec's three open questions

1. **A6 — keep `PATH_BUDGET_ALLOWANCE = 512.** It is two full FAT LFN
   components (`FsStructs.h:107-111`), ~7× a realistic path, and today's
   45,000 budget already imposes the same class of limit at ~4,400 bytes per
   path — the failure mode is not new, only moved. Raising to 1024 buys margin
   against a scenario nobody has produced and costs 5,120 bytes of a budget
   whose whole point is to be tight. Leave the one-line lever documented.
2. **A1 — 128 is fine without a device measurement.** The issue explicitly
   accepts this class of change, every other surface caps the same field at
   30-48 bytes (`LauncherActivity.cpp:83,103,132`,
   `PublicationsActivity.cpp:56`), and `truncatedText` ellipsises anything
   longer at the row anyway (`lib/GfxRenderer/GfxRenderer.cpp:1771-1789`, reached
   via `freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIGfxRenderer.h:177-179`).
   Blocking on a `getTextWidth` run would gate a good-first-issue on hardware
   for a number with 1.8× headroom. Add MAJOR 2's marker test instead.
3. **A13 — commit the line.** `add_subdirectory(recent_books_doc)` after
   `bookmark_doc` (`test/CMakeLists.txt:114`). The data-dev rule exists to stop
   two *parallel* agents colliding in a shared append point; a single PR on its
   own branch is not that case, and #47, #64 and #65 all committed theirs. The
   spec's own "Known gap" concedes the entire CI guard is inert otherwise —
   shipping a test suite that never runs is strictly worse than the collision
   the rule protects against.

---

## Not findings — checked and clean

- `static_assert` rewrite: `RecentBooksStore.cpp:139-140` does pin the old
  budget and will fail the build on the change, as both documents claim.
- A3's choice of `utf8SafeSummary` over `utf8SafeTruncateBuffer`:
  `Utf8.cpp:148-165` really does index `buf[len-1]` after only `len <= 0`, and
  `test/utf8_summary/Utf8SummaryTest.cpp:32-37`
  (`DoesNotOverReadAShortBuffer`) exists to pin the clamp. Correct call.
- A9 (no `FORMAT_VERSION`): the four keys and their meanings are unchanged
  (`RecentBooksStore.cpp:11-20,31-34`), so forward and backward reads both
  work. Sound.
- A7: both `addBook` sites pass `getThumbBmpPath()`
  (`ReaderActivity.cpp:60`, `EpubReaderActivity.cpp:435`), and both
  `updatePath` callers pass `/.crosspoint`-shaped cache paths
  (`PublicationDownloader.cpp:120`, `EpubReaderActivity.cpp:148`).
- A6's "a truncated path deletes itself": `pruneMissing`
  (`RecentBooksStore.cpp:113-117`) via `Storage.exists` (`:111`) does exactly
  that, and it runs at the top of `addBook` (`:45`).
- Moving `MAX_RECENT_BOOKS` from `int` (`RecentBooksStore.h:21`) to `size_t`
  removes the existing signed/unsigned comparison at `:58`; no external user
  (`grep -rn MAX_RECENT_BOOKS src lib test` finds only the store).
- Non-goals' dead-code claim: `getDataFromBook` has no callers, and
  `HomeActivity` is never constructed. Both hold.

---

Two MAJORs, seven MINORs, no BLOCKER. Both MAJORs are corrections to reasoning
and one added test; neither reverses a decision, changes scope, or needs a call
only the human can make. The three open questions are answerable from the repo
and are answered above. Fix all nine inline and proceed.

VERDICT: CLEAR
