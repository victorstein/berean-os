# Implementation-plan review pass 0 — issue #40

Plan: `docs/superpowers/plans/2026-09-17-issue-40-plan.md`
Spec: `docs/superpowers/specs/2026-09-17-issue-40-design.md` (cleared, pass 0)

## What was verified, not read

Before ranking anything I rebuilt the plan's code outside the repo and ran every
assertion it makes, against the real `lib/Utf8/Utf8.cpp` and the pinned
ArduinoJson 7.4.2 (`/Volumes/stein/Documents/development/personal/berean-os/.pio/libdeps/x4pro/ArduinoJson`,
`library.json:10` -> `"version": "7.4.2"`).

`src/util/RecentBooksDoc.{h,cpp}` as the plan writes them compile clean under
`-std=c++20 -Wall -Wextra -pedantic`. All 15 tests' assertions pass:

| Plan claim | Measured |
|---|---|
| `DOC_WRAPPER_BYTES` 12, `ENTRY_OVERHEAD_BYTES` 52 | `{"books":[]}` = 12, `{"path":"","title":"","author":"","coverBmpPath":""}` = 52 |
| ten empty entries measure 541 | 541 |
| `SAVE_BUDGET` = 11,421 | 11,421 |
| worst case fits by exactly zero bytes | measured 11,421 == 11,421 |
| realistic ten entries < `SAVE_BUDGET / 3` | 2,951 < 3,807 |
| 60 x U+4E16 capped at 128 lands on 126 | 126, `% 3 == 0` |
| 60 x U+4E16 capped at 96 lands on a boundary | 96, `% 3 == 0` |
| `std::string(400,'x')` caps to exactly 128 | 128 |
| step 6's inline byte counts (70, 64, 61, 60, 59, 35 / 52, 51, 36) | all nine exact |
| `"Nuevo Mundo"` / `"New World"` survive `normalise` | all three fixtures, incl. the double-space one |

I also re-measured `ESCAPE_FACTOR` per character class rather than trusting the
spec. The plan's header comment is exactly right: 2 bytes for
`" \ \b \f \n \r \t`, **1 byte** for every other control character (0x01, 0x07,
0x0B, 0x0E, 0x1F, 0x7F all measured at 1.000 bytes/char), 1 byte for raw high
bytes (0x80, 0xFF), 1:1 for 3- and 4-byte UTF-8, and **6 bytes for NUL** — so
erasing NUL in `normalise` is what makes `ESCAPE_FACTOR = 2` enforced rather
than assumed, precisely as the plan says.

Every `file:line` citation I spot-checked in the plan is correct:
`src/RecentBooksStore.h:8-15` is exactly the `struct RecentBook` block;
`src/components/themes/BaseTheme.h:10` and `src/activities/home/HomeActivity.h:9`
are the two `struct RecentBook;` forward declarations; `MAX_RECENT_BOOKS` is
referenced only at `src/RecentBooksStore.cpp:27,29,58,59`, all inside the
lines 1-80 block step 9 replaces, so removing it from the header is safe;
`src/RecentBooksStore.cpp:139-140` is the old `static_assert`;
`LauncherActivity.cpp:83` (48), `:96-99` (the `looksLikeABible` lambda),
`:103` (40), `:132` (30), `PublicationsActivity.cpp:56` (48),
`RecentBooksActivity.cpp:39-41`, `Epub.h:48`, `Epub.cpp:653`,
`src/main.cpp:40` (`#include "util/ButtonNavigator.h"`) all land where claimed.
Step 1's baseline counts are right: `test/utf8_summary/Utf8SummaryTest.cpp` has
9 `TEST`s and `test/save_budget/SaveBudgetTest.cpp` has 5.

Spec coverage: all thirteen assumptions A1-A13 and all thirteen host tests
(1-13, including 4b) map to a step. The RED phases genuinely fail — step 3 fails
CMake configure (`add_executable` naming a source that does not exist), steps 4
and 5 fail to link (declared-not-defined), step 7 asserts 128 against an
un-normalised 1,800, and step 8's `ErasesEmbeddedNuls` fails because
`utf8SafeSummary` does not touch `'\0'` (`std::isspace('\0')` is false, and only
`'\n'` is removed — `lib/Utf8/Utf8.cpp:185-201`). The running test counts
2 -> 5 -> 9 -> 11 -> 12 -> 15 are all correct. Every step leaves the tree
buildable: `src/util/RecentBooksDoc.cpp` enters the PlatformIO build at step 3
with `normalise`/`fromJson` declared but undefined, which is harmless because
nothing in the firmware references them until step 9.

This is a well-built plan. The two MAJORs below are not about its engineering.

---

## MAJOR 1 — the plan file contains three literal NUL bytes, inside code blocks it tells the implementer to paste verbatim

**Claim.** Plan `:6-8`: "Read the spec's **Assumptions** section once before
starting — the numbers below come from it, and the comments you will paste cite
it." Steps 3 and 8 then give complete file bodies to create.

**Problem.** Three of those comment lines contain a raw `0x00` byte where the
text should read the six-character escape `\` `u0000`. Pasting them writes NUL bytes
into `src/util/RecentBooksDoc.h`, `src/util/RecentBooksDoc.cpp` and
`test/recent_books_doc/RecentBooksDocTest.cpp`. Nothing in the plan's own
verification catches it: clang compiles a NUL inside a `//` comment silently
(`c++ -std=c++20 -Wall -Wextra`, exit 0, no diagnostic) and `clang-format`
passes it through byte-for-byte, so `./bin/clang-format-fix` and `pio run` both
stay green.

What it costs is search and review:

- `grep` classifies the file as binary. On a scratch header carrying one NUL,
  `grep -rn "MAX_TITLE_BYTES" hdr.h` prints **nothing** and exits 1. Three of
  the four new source files would become invisible to the repo's primary
  evidence tool — against a `CLAUDE.md` that requires "a file and a line, and
  the line must have been read".
- `git` treats the blob as binary, so the PR diff for those files degrades to
  "Binary files differ" on GitHub.

**Evidence.** Byte offsets 9370, 24779 and 28142 of the plan, which is already
committed this way in `e7456f6e`:

```
line 268  b'// one exception is NUL, which becomes \x00 \xe2\x80\x94 six bytes \xe2\x80\x94 so normalise() erases'
line 664  b'// \x00 \xe2\x80\x94 six bytes, three times ESCAPE_FACTOR. ArduinoJson stor'
line 734  b'// the serialiser and expands to \x00 \xe2\x80\x94 six bytes, which would break'
```

Those are, in order, the `ESCAPE_FACTOR` comment in `RecentBooksDoc.h`
(plan `:266-272`), the `ErasesEmbeddedNuls` test comment (plan `:663-667`) and
the `eraseNuls` comment (plan `:733-735`). As rendered they also read as
nonsense, the escape having vanished from the sentence.

A symptom worth naming because it will confuse whoever fixes this: `grep`
already refuses to search the plan document itself. `grep -c 'Step'
docs/superpowers/plans/2026-09-17-issue-40-plan.md` prints nothing and exits 1.

**Fix.** Replace each `0x00` with the six literal characters backslash,
`u`, `0`, `0`, `0`, `0` -- i.e. the JSON escape for U+0000 -- in the
plan itself, and add a line to step 10's verification block so a recurrence is
caught rather than committed:

```bash
! LC_ALL=C grep -rlP '\x00' src test   # no NUL bytes in any source file
```

---

## MAJOR 2 — steps 10 and 11 push and open a PR with no approval gate

**Claim.** Step 10 ends with `git push` (plan `:960`); step 11 runs
`gh pr create --repo victorstein/berean-os ...` (plan `:977-979`). Neither step
pauses.

**Problem.** `CLAUDE.md`, *Git workflow -> Rules*, item 2: "Never push to any
remote, or open or close a PR, without explicit user approval. Complete local
work and any requested local commit, then stop." An implementer executing this
plan literally — which is the standard this review is asked to hold it to —
violates that rule twice. The spec does not ask for either action; its
*Testing strategy -> Firmware gates* section stops at `pio run` and
`clang-format-fix`.

**Evidence.** Plan `:956-961` and `:974-979`; `CLAUDE.md` *Git workflow ->
Rules* 2; spec `:671-679`.

**Fix.** In step 10, drop the bare `git push` and end the step at the local
commit with an explicit line: "Stop here. Pushing and opening the PR need the
user's go-ahead (`CLAUDE.md`, Git workflow rule 2) — ask, then run step 11."
Retitle step 11 "The pull request — only after the user approves" and leave its
body as-is; the PR content is good and should not be rewritten.

---

## MINOR 1 — one of the spec's five human-verification items is dropped

**Claim.** Plan `:1024-1033`, the PR body's "## Needs a device" list, has four
items.

**Problem.** The spec lists five (*Testing strategy -> What only the human
tester can verify*, spec `:681-696`). Item 5, "No `PERSIST` refusal line appears
in normal use", is missing. It is the one item that directly tests what this
change risks — a tightened budget producing the silent refusal the spec's
*Why a refusal is the thing to avoid* (spec `:45-50`) is built around.

**Evidence.** Spec `:696` against plan `:1026-1033`.

**Fix.** Add a fifth bullet to the PR body's "Needs a device" list:
"- No `PERSIST` refusal line in the serial log during normal use."

---

## MINOR 2 — step 6 de-accents the fixtures the spec asked for verbatim, on a rationale the repo contradicts

**Claim.** Plan `:594-596`: "The accents are stripped from the fixtures on
purpose: the byte counts in the spec … are for the accented forms, and the ASCII
forms are shorter, so the assertion still holds and the file stays free of
encoding surprises."

**Problem.** The spec asks for the opposite in as many words — test 4's fixtures
are "the exact strings in those two lists, not a remembered range" (spec `:630`),
and A1/A2 tabulate them accented with their byte counts (spec `:104-111`,
`:147-149`). The stated reason does not hold: twelve existing host-test files
already carry raw UTF-8, including this suite's two closest siblings,
`test/passage_doc/PassageDocTest.cpp` and `test/highlight_doc/HighlightDocTest.cpp`
(also `unit_text`, `tag_palette`, `catalog_index`, `bible_book_join`,
`meeting_filename`, `migration_planner`, `pub_media_json`, `unit_fingerprint`,
`font_page_slots`, `utf8_compose`). Step 5 of this same plan already writes raw
multi-byte data as `"\xe4\xb8\x96"`.

The assertion does still pass — I ran it — so the cost is fidelity, not
correctness: the ASCII forms measure 70/64/61/60/59/35 and 52/51/36 bytes
against the real 72/66/63/60/59/39 and 52/51/38, so the test pins the cap
against strings that are not the ones A1 justified 128 with.

**Evidence.** Plan `:546-571`, `:594-596`; spec `:104-111`, `:147-149`, `:630`.
Measured: `find test -name '*.cpp'` piped through
`LC_ALL=C grep -P '[\x80-\xff]'` returns 12 files.

**Fix.** Use the accented strings from the spec's tables verbatim in the
`titles[]` and `authors[]` arrays, and correct the inline byte comments to
72/66/63/60/59/39 and 52/51/38. Delete the paragraph at plan `:594-596`,
keeping its last sentence about the interior double space.

---

## MINOR 3 — the "a path can contain none of them" comment is true of paths the firmware writes, not of paths it reads

**Claim.** Plan `:269-271`, inside the `ESCAPE_FACTOR` comment: "A path can
contain none of them: SdFat rejects `"`, `\` and everything below 0x20 in both a
FAT LFN and an exFAT name, so `path` and `coverBmpPath` serialise 1:1 and are
not multiplied below."

**Problem.** SdFat's validation applies to names of files on the card. The
`path` *value in `recent.json`* is an arbitrary JSON string, and this plan
deliberately never re-bounds it — step 5's `NeverTouchesPathOrCoverBmpPath` pins
exactly that (plan `:472-480`), per A6. So a hand-edited or corrupted
`recent.json` whose `path` carries `"` or `\` flows through `fromJson` verbatim
(plan `:421`) and doubles on the next `toJson`; ten such entries at the
allowance would add 5,120 bytes to a budget that fits by zero, producing the
silent save refusal A6 and spec `:45-50` argue against.

This needs no code change — it is the same accepted class as A6's
">512-byte path" case, and the plan is faithfully carrying the spec's own A8
wording (spec `:281-284`). But the comment as written claims a guarantee the
code does not have, and a future reader will rely on it.

**Evidence.** Plan `:269-271` against plan `:472-480` and `:421`; spec
`:229-247` (A6), `:281-284` (A8).

**Fix.** Qualify the sentence, e.g.: "…so a path this firmware writes serialises
1:1 and is not multiplied below. A hand-edited `recent.json` could break that;
the consequence is the same save refusal `PATH_BUDGET_ALLOWANCE` already accepts
(A6)."

---

## MINOR 4 — step 10's `git add -A` sweeps whatever else is untracked

**Claim.** Plan `:957-959`:

```bash
git status --short                       # nothing .gitignore-excluded: no .pio/, no *.generated.h
git add -A
git commit -m "style: clang-format the recent-books bounds change"
```

**Problem.** `git add -A` stages every untracked file in the tree, not just the
formatted sources. At that point the tree also holds review artefacts — this
file among them — and any scratch output. The `git status` line above it warns
only about `.gitignore`-excluded paths, which is the one class `-A` cannot stage
anyway.

**Evidence.** Plan `:957-959`. Every other commit in the plan names its paths
explicitly (`:102`, `:338`, `:435`, `:603`, `:932`).

**Fix.** Match the rest of the plan:
`git add src/RecentBook.h src/RecentBooksStore.h src/RecentBooksStore.cpp src/util/RecentBooksDoc.h src/util/RecentBooksDoc.cpp test/recent_books_doc test/CMakeLists.txt`

---

## Not findings

Recorded so a later pass does not re-open them.

- **The exact-fit budget.** `AWorstCaseDocumentFitsTheDerivedBudget` asserting
  `measured == SAVE_BUDGET` is brittle by design (A10), and I confirmed it is
  exact today. The plan's failure message already says to raise a named
  allowance rather than loosen the assertion. Correct as written.
- **Step 9 has no host test.** It is the step with the most behaviour change
  (`addBook`/`updateBook` normalisation, the `requestResave` wiring) and its
  only automated gates are two `static_assert`s that check the budget, not the
  wiring. That is A11's accepted consequence — `RecentBooksStore.cpp` reaches
  `<Arduino.h>` through `PersistableStore.h:3` and also includes `<Epub.h>` and
  `<HalStorage.h>` — not a plan defect. The PR body flags it for the device.
- **`requestResave()` from `RecentBooksStore::fromJson`.** Protected member of
  `PersistableStoreBase`, reached through the derived class's own `this`;
  legal, and `loadFromFile` performs the save after releasing `storeMutex`
  (`PersistableStore.h:194-197`). No deadlock.
- **`RecentBook book{path, title, author, coverBmpPath}`** in step 9's
  `addBook`. `RecentBook` keeps a user-*declared* `operator==` but no
  user-provided constructor, so it remains an aggregate. Compiles.
- **`capField(...) || changed`, not `changed || capField(...)`.** The note at
  plan `:523-525` is right, and the four-line `normalise` in step 8 keeps the
  ordering correct on every line.
- **`add_subdirectory` placement.** The spec says after `bookmark_doc`
  (`test/CMakeLists.txt:114`), the plan says after `font_page_slots` (`:115`,
  the current last line). Both work.
- **The plan's commit message already corrects** the spec's Risks-table
  "byte 14 / byte 4" to A1's byte 16 / byte 0. I measured byte 15 for the ASCII
  form and byte 16 for the accented one — the plan's header comment is right.
- **Environment facts in the plan's preamble all check out**:
  `~/.platformio/penv/bin/pio` exists, `clang-format` is in the main checkout's
  `.venv/bin`, the submodule is already initialised, and `platformio.ini`
  declares no `src_filter`, so `src/util/RecentBooksDoc.cpp` is picked up
  automatically (as `src/util/BookmarkDoc.cpp` already is).

---

## Verdict

0 BLOCKERs, 2 MAJORs, 4 MINORs. Both MAJORs are fixable inline in the plan
document — three byte replacements and an approval gate on the last two steps —
and neither reverses a decision, changes scope, or needs a call only the human
can make. The engineering is sound: I reproduced every number and every
assertion independently and nothing moved.

VERDICT: CLEAR
